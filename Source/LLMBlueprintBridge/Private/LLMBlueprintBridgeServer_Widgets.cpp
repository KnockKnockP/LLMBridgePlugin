// Widget tree editing and cross-graph node copy for the bridge.
//
// Own translation unit, same reason as the world tools: LLMBlueprintBridgeServer.cpp is already
// oversized. add_widget could only append, so moving UI between Widget Blueprints meant rebuilding
// widgets and their graph logic node by node. These tools remove, move and copy instead.

#include "LLMBlueprintBridgeServer.h"

#include "Blueprint/WidgetTree.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphUtilities.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_Variable.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"

namespace
{
    TSharedRef<FJsonObject> WidgetMakeError(const FString& Code, const FString& Message)
    {
        TSharedRef<FJsonObject> Response = MakeShared<FJsonObject>();
        Response->SetBoolField(TEXT("ok"), false);
        Response->SetStringField(TEXT("code"), Code);
        Response->SetStringField(TEXT("message"), Message);
        return Response;
    }

    TSharedRef<FJsonObject> WidgetMakeOk(const TSharedPtr<FJsonObject>& Data)
    {
        TSharedRef<FJsonObject> Response = MakeShared<FJsonObject>();
        Response->SetBoolField(TEXT("ok"), true);
        Response->SetObjectField(TEXT("data"), Data);
        return Response;
    }

    FString WidgetGetString(const TSharedPtr<FJsonObject>& Arguments, const TCHAR* Field, const FString& Default = FString())
    {
        FString Value;
        return Arguments->TryGetStringField(Field, Value) && !Value.IsEmpty() ? Value : Default;
    }

    bool WidgetGetBool(const TSharedPtr<FJsonObject>& Arguments, const TCHAR* Field, bool bDefault)
    {
        bool bValue = bDefault;
        Arguments->TryGetBoolField(Field, bValue);
        return bValue;
    }

    int32 WidgetGetInt(const TSharedPtr<FJsonObject>& Arguments, const TCHAR* Field, int32 Default)
    {
        double Value = Default;
        return Arguments->TryGetNumberField(Field, Value) ? static_cast<int32>(Value) : Default;
    }

    TArray<TSharedPtr<FJsonValue>> WidgetStringArray(const TArray<FString>& Values)
    {
        TArray<TSharedPtr<FJsonValue>> Out;
        for (const FString& Value : Values)
            Out.Add(MakeShared<FJsonValueString>(Value));
        return Out;
    }

    // The widget plus every descendant reached through panel children, root first.
    void CollectSubtree(UWidget* Root, TArray<UWidget*>& OutWidgets)
    {
        OutWidgets.Add(Root);
        if (UPanelWidget* Panel = Cast<UPanelWidget>(Root))
        {
            for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
            {
                if (UWidget* Child = Panel->GetChildAt(Index))
                    CollectSubtree(Child, OutWidgets);
            }
        }
    }

    bool IsWithin(UWidget* Candidate, UWidget* Root)
    {
        for (UWidget* Current = Candidate; Current; Current = Current->GetParent())
        {
            if (Current == Root)
                return true;
        }
        return false;
    }

    // Graph nodes that read, write or bind to the named widget variable; widgets are self members of their Blueprint.
    TArray<FString> FindWidgetReferences(UBlueprint* Blueprint, FName WidgetName)
    {
        TArray<FString> References;

        TArray<UK2Node_Variable*> VariableNodes;
        FBlueprintEditorUtils::GetAllNodesOfClass<UK2Node_Variable>(Blueprint, VariableNodes);
        for (UK2Node_Variable* Node : VariableNodes)
        {
            if (Node->VariableReference.IsSelfContext() && Node->VariableReference.GetMemberName() == WidgetName)
                References.Add(FString::Printf(TEXT("%s:%s"), *Node->GetGraph()->GetName(), *Node->GetName()));
        }

        TArray<UK2Node_ComponentBoundEvent*> BoundEvents;
        FBlueprintEditorUtils::GetAllNodesOfClass<UK2Node_ComponentBoundEvent>(Blueprint, BoundEvents);
        for (UK2Node_ComponentBoundEvent* Node : BoundEvents)
        {
            if (Node->ComponentPropertyName == WidgetName)
                References.Add(FString::Printf(TEXT("%s:%s"), *Node->GetGraph()->GetName(), *Node->GetName()));
        }
        return References;
    }

    // Copies layout properties by name, so a slot keeps its padding and alignment even when the new parent is a
    // different panel type. Properties the target slot type lacks are skipped.
    void CopySlotLayout(UPanelSlot* From, UPanelSlot* To)
    {
        if (!From || !To)
            return;

        for (TFieldIterator<FProperty> It(To->GetClass()); It; ++It)
        {
            FProperty* Target = *It;
            const UClass* Owner = Target->GetOwnerClass();
            if (!Target->HasAnyPropertyFlags(CPF_Edit) || Owner == UPanelSlot::StaticClass() || Owner == UObject::StaticClass())
                continue;

            const FProperty* Source = From->GetClass()->FindPropertyByName(Target->GetFName());
            if (Source && Source->SameType(Target))
                Target->CopyCompleteValue(Target->ContainerPtrToValuePtr<void>(To), Source->ContainerPtrToValuePtr<void>(From));
        }
        To->SynchronizeProperties();
    }

    // Rebuilds Source and its panel children inside DestTree, copying the designer-editable properties.
    // Instanced subobjects are skipped: they belong to the source asset and would be shared, not copied.
    UWidget* CloneWidget(UWidgetTree* DestTree, UWidget* Source, TArray<UWidget*>& OutCreated)
    {
        UWidget* Clone = DestTree->ConstructWidget<UWidget>(Source->GetClass(), Source->GetFName());
        if (!Clone)
            return nullptr;
        OutCreated.Add(Clone);

        for (TFieldIterator<FProperty> It(Source->GetClass()); It; ++It)
        {
            FProperty* Property = *It;
            if (!Property->HasAnyPropertyFlags(CPF_Edit) ||
                Property->HasAnyPropertyFlags(CPF_Transient | CPF_InstancedReference | CPF_ContainsInstancedReference | CPF_PersistentInstance))
                continue;
            Property->CopyCompleteValue_InContainer(Clone, Source);
        }
        Clone->bIsVariable = Source->bIsVariable;
        Clone->SetDisplayLabel(Source->GetDisplayLabel());

        if (UPanelWidget* SourcePanel = Cast<UPanelWidget>(Source))
        {
            UPanelWidget* ClonePanel = CastChecked<UPanelWidget>(Clone);
            for (int32 Index = 0; Index < SourcePanel->GetChildrenCount(); ++Index)
            {
                UWidget* Child = SourcePanel->GetChildAt(Index);
                UWidget* ChildClone = Child ? CloneWidget(DestTree, Child, OutCreated) : nullptr;
                if (!ChildClone)
                    continue;
                UPanelSlot* NewSlot = ClonePanel->AddChild(ChildClone);
                CopySlotLayout(Child->Slot, NewSlot);
            }
        }
        Clone->SynchronizeProperties();
        return Clone;
    }

    // Takes Root and its descendants out of the Blueprint the way the UMG designer's delete does.
    void DetachSubtree(UWidgetBlueprint* Blueprint, UWidget* Root, TArray<FString>& OutRemovedNames)
    {
        TArray<UWidget*> Subtree;
        CollectSubtree(Root, Subtree);

        Blueprint->WidgetTree->RemoveWidget(Root);
        for (UWidget* Widget : Subtree)
        {
            const FName Name = Widget->GetFName();
            OutRemovedNames.Add(Name.ToString());
            Widget->Modify();
            // Release the name first: the GUID map is keyed by it and a later widget may reuse it.
            Blueprint->OnVariableRemoved(Name);
            Widget->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors);
        }
    }

    UPanelSlot* AttachAt(UPanelWidget* Parent, UWidget* Widget, int32 Index)
    {
        return Index >= 0 && Index <= Parent->GetChildrenCount() ? Parent->InsertChildAt(Index, Widget) : Parent->AddChild(Widget);
    }
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolRemoveWidget(const TSharedPtr<FJsonObject>& Arguments)
{
    const FString AssetPath = WidgetGetString(Arguments, TEXT("asset_path"));
    const FString WidgetName = WidgetGetString(Arguments, TEXT("name"));
    if (AssetPath.IsEmpty() || WidgetName.IsEmpty())
        return WidgetMakeError(TEXT("invalid_arguments"), TEXT("remove_widget needs 'asset_path' and 'name'."));
    if (!WidgetGetBool(Arguments, TEXT("confirm"), false))
        return WidgetMakeError(TEXT("confirmation_required"), TEXT("remove_widget requires confirm=true."));

    FString Error;
    UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(LoadBlueprint(AssetPath, Error));
    if (!Blueprint || !Blueprint->WidgetTree)
        return WidgetMakeError(TEXT("invalid_blueprint"), Error.IsEmpty() ? FString::Printf(TEXT("'%s' is not a Widget Blueprint."), *AssetPath) : Error);

    UWidget* Widget = Blueprint->WidgetTree->FindWidget(FName(*WidgetName));
    if (!Widget)
        return WidgetMakeError(TEXT("widget_not_found"), FString::Printf(TEXT("Widget '%s' was not found."), *WidgetName));

    TArray<UWidget*> Subtree;
    CollectSubtree(Widget, Subtree);
    TArray<FString> References;
    for (UWidget* Each : Subtree)
        References.Append(FindWidgetReferences(Blueprint, Each->GetFName()));
    if (References.Num() > 0 && !WidgetGetBool(Arguments, TEXT("allow_referenced"), false))
    {
        return WidgetMakeError(TEXT("widget_referenced"),
            FString::Printf(TEXT("Graph nodes still use '%s' or its children: %s. Delete them first or pass allow_referenced=true."),
                *WidgetName, *FString::Join(References, TEXT(", "))));
    }

    const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "RemoveWidget", "LLM Remove Widget"));
    Blueprint->Modify();
    Blueprint->WidgetTree->SetFlags(RF_Transactional);
    Blueprint->WidgetTree->Modify();
    TArray<FString> Removed;
    DetachSubtree(Blueprint, Widget, Removed);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

    TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
    Data->SetArrayField(TEXT("removed"), WidgetStringArray(Removed));
    Data->SetArrayField(TEXT("dangling_references"), WidgetStringArray(References));
    return WidgetMakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolMoveWidget(const TSharedPtr<FJsonObject>& Arguments)
{
    const FString AssetPath = WidgetGetString(Arguments, TEXT("asset_path"));
    const FString WidgetName = WidgetGetString(Arguments, TEXT("name"));
    const FString ParentName = WidgetGetString(Arguments, TEXT("parent"));
    const FString DestPath = WidgetGetString(Arguments, TEXT("dest_asset_path"), AssetPath);
    const int32 Index = WidgetGetInt(Arguments, TEXT("index"), -1);
    if (AssetPath.IsEmpty() || WidgetName.IsEmpty() || ParentName.IsEmpty())
        return WidgetMakeError(TEXT("invalid_arguments"), TEXT("move_widget needs 'asset_path', 'name' and 'parent'."));

    FString Error;
    UWidgetBlueprint* Source = Cast<UWidgetBlueprint>(LoadBlueprint(AssetPath, Error));
    UWidgetBlueprint* Dest = Source ? Cast<UWidgetBlueprint>(LoadBlueprint(DestPath, Error)) : nullptr;
    if (!Source || !Dest || !Source->WidgetTree || !Dest->WidgetTree)
        return WidgetMakeError(TEXT("invalid_blueprint"), Error.IsEmpty() ? TEXT("Both assets must be Widget Blueprints.") : Error);

    UWidget* Widget = Source->WidgetTree->FindWidget(FName(*WidgetName));
    if (!Widget)
        return WidgetMakeError(TEXT("widget_not_found"), FString::Printf(TEXT("Widget '%s' was not found."), *WidgetName));
    if (Widget == Source->WidgetTree->RootWidget)
        return WidgetMakeError(TEXT("invalid_widget"), TEXT("The root widget cannot be moved."));

    UPanelWidget* Parent = Cast<UPanelWidget>(Dest->WidgetTree->FindWidget(FName(*ParentName)));
    if (!Parent)
        return WidgetMakeError(TEXT("parent_not_found"), FString::Printf(TEXT("Panel widget '%s' was not found."), *ParentName));

    TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), Source->GetPathName());
    Data->SetStringField(TEXT("dest_asset_path"), Dest->GetPathName());
    Data->SetStringField(TEXT("parent"), ParentName);

    if (Source == Dest)
    {
        if (IsWithin(Parent, Widget))
            return WidgetMakeError(TEXT("invalid_parent"), TEXT("A widget cannot be moved into itself or its own children."));

        const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "MoveWidget", "LLM Move Widget"));
        Source->Modify();
        Source->WidgetTree->SetFlags(RF_Transactional);
        Source->WidgetTree->Modify();
        Widget->Modify();

        // Hold the old slot: removing the child detaches it but leaves the object alive for this call.
        UPanelSlot* OldSlot = Widget->Slot;
        int32 TargetIndex = Index;
        if (UPanelWidget* OldParent = Widget->GetParent())
        {
            // Inserting into the same panel shifts later indices down once the widget is out.
            if (OldParent == Parent && TargetIndex > OldParent->GetChildIndex(Widget))
                --TargetIndex;
            OldParent->Modify();
            OldParent->RemoveChild(Widget);
        }
        Parent->Modify();
        CopySlotLayout(OldSlot, AttachAt(Parent, Widget, TargetIndex));
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Source);

        Data->SetStringField(TEXT("object_path"), Widget->GetPathName());
        Data->SetNumberField(TEXT("index"), Parent->GetChildIndex(Widget));
        return WidgetMakeOk(Data);
    }

    TArray<UWidget*> Subtree;
    CollectSubtree(Widget, Subtree);
    TArray<FString> References;
    for (UWidget* Each : Subtree)
    {
        const FName Name = Each->GetFName();
        if (Dest->WidgetTree->FindWidget(Name) || FBlueprintEditorUtils::FindNewVariableIndex(Dest, Name) != INDEX_NONE)
            return WidgetMakeError(TEXT("name_conflict"), FString::Printf(TEXT("'%s' already exists in '%s'."), *Name.ToString(), *DestPath));
        References.Append(FindWidgetReferences(Source, Name));
    }
    if (References.Num() > 0 && !WidgetGetBool(Arguments, TEXT("allow_referenced"), false))
    {
        return WidgetMakeError(TEXT("widget_referenced"),
            FString::Printf(TEXT("Graph nodes in the source still use '%s' or its children: %s. Copy or delete them, or pass allow_referenced=true."),
                *WidgetName, *FString::Join(References, TEXT(", "))));
    }

    const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "MoveWidgetAcross", "LLM Move Widget Between Blueprints"));
    Source->Modify();
    Dest->Modify();
    Source->WidgetTree->SetFlags(RF_Transactional);
    Dest->WidgetTree->SetFlags(RF_Transactional);
    Source->WidgetTree->Modify();
    Dest->WidgetTree->Modify();
    Parent->Modify();

    TArray<UWidget*> Created;
    UWidget* Clone = CloneWidget(Dest->WidgetTree, Widget, Created);
    if (!Clone)
        return WidgetMakeError(TEXT("create_failed"), TEXT("Could not construct the widget in the destination."));
    CopySlotLayout(Widget->Slot, AttachAt(Parent, Clone, Index));
    for (UWidget* Each : Created)
        Dest->OnVariableAdded(Each->GetFName());

    TArray<FString> Removed;
    DetachSubtree(Source, Widget, Removed);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Dest);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Source);

    Data->SetStringField(TEXT("object_path"), Clone->GetPathName());
    Data->SetNumberField(TEXT("index"), Parent->GetChildIndex(Clone));
    Data->SetArrayField(TEXT("moved"), WidgetStringArray(Removed));
    Data->SetArrayField(TEXT("dangling_references"), WidgetStringArray(References));
    return WidgetMakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolCopyNodes(const TSharedPtr<FJsonObject>& Arguments)
{
    const FString AssetPath = WidgetGetString(Arguments, TEXT("asset_path"));
    const FString GraphName = WidgetGetString(Arguments, TEXT("graph"));
    const FString DestPath = WidgetGetString(Arguments, TEXT("dest_asset_path"), AssetPath);
    const FString DestGraphName = WidgetGetString(Arguments, TEXT("dest_graph"), GraphName);
    const int32 OffsetX = WidgetGetInt(Arguments, TEXT("offset_x"), 0);
    const int32 OffsetY = WidgetGetInt(Arguments, TEXT("offset_y"), 0);
    const TArray<TSharedPtr<FJsonValue>>* NodeValues = nullptr;
    if (AssetPath.IsEmpty() || GraphName.IsEmpty() || !Arguments->TryGetArrayField(TEXT("nodes"), NodeValues) || NodeValues->Num() == 0)
        return WidgetMakeError(TEXT("invalid_arguments"), TEXT("copy_nodes needs 'asset_path', 'graph' and a non-empty 'nodes' array."));

    FString Error;
    UBlueprint* Source = LoadBlueprint(AssetPath, Error);
    UEdGraph* SourceGraph = Source ? FindGraph(Source, GraphName, Error) : nullptr;
    UBlueprint* Dest = SourceGraph ? LoadBlueprint(DestPath, Error) : nullptr;
    UEdGraph* DestGraph = Dest ? FindGraph(Dest, DestGraphName, Error) : nullptr;
    if (!DestGraph)
        return WidgetMakeError(TEXT("resolve_failed"), Error);

    TSet<UObject*> ToExport;
    TArray<FString> Skipped;
    for (const TSharedPtr<FJsonValue>& Value : *NodeValues)
    {
        const FString NodeId = Value->AsString();
        UEdGraphNode* Node = FindNode(SourceGraph, NodeId, Error);
        if (!Node)
            return WidgetMakeError(TEXT("resolve_failed"), Error);
        // Function entry and result nodes belong to their graph; the editor refuses to copy them too.
        if (!Node->CanDuplicateNode())
        {
            Skipped.Add(NodeId);
            continue;
        }
        Node->PrepareForCopying();
        ToExport.Add(Node);
    }
    if (ToExport.Num() == 0)
        return WidgetMakeError(TEXT("nothing_to_copy"), TEXT("None of the listed nodes can be copied."));

    FString Text;
    FEdGraphUtilities::ExportNodesToText(ToExport, Text);
    if (!FEdGraphUtilities::CanImportNodesFromText(DestGraph, Text))
        return WidgetMakeError(TEXT("incompatible_graph"), FString::Printf(TEXT("Graph '%s' cannot accept these nodes."), *DestGraphName));

    const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "CopyNodes", "LLM Copy Blueprint Nodes"));
    Dest->Modify();
    DestGraph->Modify();
    TSet<UEdGraphNode*> Pasted;
    FEdGraphUtilities::ImportNodesFromText(DestGraph, Text, Pasted);

    TArray<TSharedPtr<FJsonValue>> NodesJson;
    TArray<FString> MissingFunctions;
    for (UEdGraphNode* Node : Pasted)
    {
        // Pasted nodes still carry the source GUIDs, which is how callers map old ids to new ones.
        const FString OldId = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
        Node->NodePosX += OffsetX;
        Node->NodePosY += OffsetY;
        Node->CreateNewGuid();

        if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
        {
            if (Call->FunctionReference.IsSelfContext() && !Call->GetTargetFunction())
                MissingFunctions.AddUnique(Call->FunctionReference.GetMemberName().ToString());
        }

        TSharedRef<FJsonObject> NodeJson = MakeShared<FJsonObject>();
        NodeJson->SetStringField(TEXT("source_id"), OldId);
        NodeJson->SetStringField(TEXT("id"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
        NodeJson->SetStringField(TEXT("object_name"), Node->GetName());
        NodeJson->SetStringField(TEXT("class"), Node->GetClass()->GetPathName());
        NodeJson->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
        NodeJson->SetNumberField(TEXT("x"), Node->NodePosX);
        NodeJson->SetNumberField(TEXT("y"), Node->NodePosY);
        NodesJson.Add(MakeShared<FJsonValueObject>(NodeJson));
    }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Dest);

    TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("dest_asset_path"), Dest->GetPathName());
    Data->SetStringField(TEXT("dest_graph"), DestGraph->GetName());
    Data->SetArrayField(TEXT("nodes"), NodesJson);
    Data->SetArrayField(TEXT("skipped"), WidgetStringArray(Skipped));
    Data->SetArrayField(TEXT("missing_self_functions"), WidgetStringArray(MissingFunctions));
    return WidgetMakeOk(Data);
}

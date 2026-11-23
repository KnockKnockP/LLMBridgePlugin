// Animation state machine tools for the bridge.
//
// The Blueprint action database does not list "add state" or "add transition" for a state machine
// graph - those come from UAnimationStateMachineSchema's own context menu. These tools go through
// the same FEdGraphSchemaAction_NewStateNode path the editor menu uses, so the new nodes get their
// bound graphs (state pose graph / transition rule graph) exactly as if placed by hand.

#include "LLMBlueprintBridgeServer.h"

#include "AnimGraphNode_TransitionResult.h"
#include "AnimStateNode.h"
#include "AnimStateNodeBase.h"
#include "AnimStateTransitionNode.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationStateMachineSchema.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"

namespace
{
    TSharedRef<FJsonObject> AnimGraphMakeError(const FString& Code, const FString& Message)
    {
        TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
        Root->SetBoolField(TEXT("ok"), false);
        Root->SetStringField(TEXT("code"), Code);
        Root->SetStringField(TEXT("message"), Message);
        return Root;
    }

    TSharedRef<FJsonObject> AnimGraphMakeOk(const TSharedPtr<FJsonObject>& Data)
    {
        TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
        Root->SetBoolField(TEXT("ok"), true);
        Root->SetObjectField(TEXT("data"), Data);
        return Root;
    }

    bool AnimGraphRequireString(const TSharedPtr<FJsonObject>& Arguments, const TCHAR* Field, FString& OutValue, FString& OutError)
    {
        if (!Arguments.IsValid() || !Arguments->TryGetStringField(Field, OutValue) || OutValue.TrimStartAndEnd().IsEmpty())
        {
            OutError = FString::Printf(TEXT("Missing required string field '%s'."), Field);
            return false;
        }
        OutValue.TrimStartAndEndInline();
        return true;
    }

    int32 AnimGraphOptionalInt(const TSharedPtr<FJsonObject>& Arguments, const TCHAR* Field, int32 Default)
    {
        double Value = 0.0;
        return (Arguments.IsValid() && Arguments->TryGetNumberField(Field, Value)) ? static_cast<int32>(Value) : Default;
    }

    TSharedRef<FJsonObject> AnimGraphNodeJson(const UEdGraphNode* Node)
    {
        TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
        Json->SetStringField(TEXT("id"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
        Json->SetStringField(TEXT("object_name"), Node->GetName());
        Json->SetStringField(TEXT("class"), Node->GetClass()->GetPathName());
        Json->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
        Json->SetNumberField(TEXT("x"), Node->NodePosX);
        Json->SetNumberField(TEXT("y"), Node->NodePosY);
        return Json;
    }

    /** Graph path relative to the Blueprint (what FindGraph accepts besides a bare name). */
    FString AnimGraphRelativePath(const UEdGraph* Graph, const UBlueprint* Blueprint)
    {
        return Graph ? Graph->GetPathName(Blueprint) : FString();
    }

    /** Finds a state (or conduit/alias) in a state machine graph by state name, node guid or object name. */
    UAnimStateNodeBase* FindStateNode(UEdGraph* StateMachineGraph, const FString& StateNameOrId)
    {
        FGuid Guid;
        const bool bHasGuid = FGuid::Parse(StateNameOrId, Guid);
        for (UEdGraphNode* Node : StateMachineGraph->Nodes)
        {
            UAnimStateNodeBase* State = Cast<UAnimStateNodeBase>(Node);
            if (!State || State->IsA<UAnimStateTransitionNode>())
            {
                continue;
            }
            if ((bHasGuid && State->NodeGuid == Guid) ||
                State->GetStateName().Equals(StateNameOrId, ESearchCase::IgnoreCase) ||
                State->GetName().Equals(StateNameOrId, ESearchCase::IgnoreCase))
            {
                return State;
            }
        }
        return nullptr;
    }
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolAddAnimState(const TSharedPtr<FJsonObject>& Arguments)
{
    FString AssetPath;
    FString GraphName;
    FString StateName;
    FString Error;
    if (!AnimGraphRequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
        !AnimGraphRequireString(Arguments, TEXT("graph"), GraphName, Error) ||
        !AnimGraphRequireString(Arguments, TEXT("name"), StateName, Error))
    {
        return AnimGraphMakeError(TEXT("invalid_arguments"), Error);
    }

    UBlueprint* Blueprint = LoadBlueprint(AssetPath, Error);
    if (!Blueprint)
    {
        return AnimGraphMakeError(TEXT("load_failed"), Error);
    }

    UEdGraph* Graph = FindGraph(Blueprint, GraphName, Error);
    if (!Graph)
    {
        return AnimGraphMakeError(TEXT("graph_not_found"), Error);
    }
    if (!Graph->IsA<UAnimationStateMachineGraph>())
    {
        return AnimGraphMakeError(TEXT("invalid_graph"), FString::Printf(TEXT("Graph '%s' is not a state machine graph. Pass the state machine graph (or its path from inspect_blueprint)."), *AnimGraphRelativePath(Graph, Blueprint)));
    }
    if (FindStateNode(Graph, StateName))
    {
        return AnimGraphMakeError(TEXT("already_exists"), FString::Printf(TEXT("State '%s' already exists in '%s'."), *StateName, *AnimGraphRelativePath(Graph, Blueprint)));
    }

    const FVector2f Location(static_cast<float>(AnimGraphOptionalInt(Arguments, TEXT("x"), 0)), static_cast<float>(AnimGraphOptionalInt(Arguments, TEXT("y"), 0)));

    const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "AddAnimState", "LLM Add Anim State"));
    Blueprint->Modify();
    Graph->Modify();

    UAnimStateNode* State = FEdGraphSchemaAction_NewStateNode::SpawnNodeFromTemplate<UAnimStateNode>(Graph, NewObject<UAnimStateNode>(), Location, false);
    if (!State || !State->GetBoundGraph())
    {
        return AnimGraphMakeError(TEXT("spawn_failed"), TEXT("The state node was not created."));
    }

    // The state's name is its bound graph's name; OnRenameNode goes through the same rename path as the editor.
    State->OnRenameNode(StateName);

    bool bAlwaysResetOnEntry = false;
    if (Arguments->TryGetBoolField(TEXT("always_reset_on_entry"), bAlwaysResetOnEntry))
    {
        State->bAlwaysResetOnEntry = bAlwaysResetOnEntry;
    }

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

    TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
    Data->SetStringField(TEXT("state_machine_graph"), AnimGraphRelativePath(Graph, Blueprint));
    Data->SetStringField(TEXT("state_name"), State->GetStateName());
    Data->SetObjectField(TEXT("node"), AnimGraphNodeJson(State));
    Data->SetStringField(TEXT("state_graph"), AnimGraphRelativePath(State->GetBoundGraph(), Blueprint));
    Data->SetBoolField(TEXT("always_reset_on_entry"), State->bAlwaysResetOnEntry);
    return AnimGraphMakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolAddAnimTransition(const TSharedPtr<FJsonObject>& Arguments)
{
    FString AssetPath;
    FString GraphName;
    FString FromName;
    FString ToName;
    FString Error;
    if (!AnimGraphRequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
        !AnimGraphRequireString(Arguments, TEXT("graph"), GraphName, Error) ||
        !AnimGraphRequireString(Arguments, TEXT("from_state"), FromName, Error) ||
        !AnimGraphRequireString(Arguments, TEXT("to_state"), ToName, Error))
    {
        return AnimGraphMakeError(TEXT("invalid_arguments"), Error);
    }

    UBlueprint* Blueprint = LoadBlueprint(AssetPath, Error);
    if (!Blueprint)
    {
        return AnimGraphMakeError(TEXT("load_failed"), Error);
    }

    UEdGraph* Graph = FindGraph(Blueprint, GraphName, Error);
    if (!Graph)
    {
        return AnimGraphMakeError(TEXT("graph_not_found"), Error);
    }
    if (!Graph->IsA<UAnimationStateMachineGraph>())
    {
        return AnimGraphMakeError(TEXT("invalid_graph"), FString::Printf(TEXT("Graph '%s' is not a state machine graph."), *AnimGraphRelativePath(Graph, Blueprint)));
    }

    UAnimStateNodeBase* FromState = FindStateNode(Graph, FromName);
    UAnimStateNodeBase* ToState = FindStateNode(Graph, ToName);
    if (!FromState || !ToState)
    {
        return AnimGraphMakeError(TEXT("state_not_found"), FString::Printf(TEXT("State '%s' was not found in '%s'."), FromState ? *ToName : *FromName, *AnimGraphRelativePath(Graph, Blueprint)));
    }
    if (FromState == ToState)
    {
        return AnimGraphMakeError(TEXT("invalid_arguments"), TEXT("from_state and to_state must differ."));
    }
    if (!FromState->GetOutputPin() || !ToState->GetInputPin())
    {
        return AnimGraphMakeError(TEXT("invalid_arguments"), TEXT("from_state needs an output pin and to_state needs an input pin (the entry node cannot be a target)."));
    }

    const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "AddAnimTransition", "LLM Add Anim Transition"));
    Blueprint->Modify();
    Graph->Modify();

    // Same steps as UAnimationStateMachineSchema::CreateAutomaticConversionNodeAndConnections.
    const FVector2f Location = (FVector2f(FromState->NodePosX, FromState->NodePosY) + FVector2f(ToState->NodePosX, ToState->NodePosY)) * 0.5f;
    UAnimStateTransitionNode* Transition = FEdGraphSchemaAction_NewStateNode::SpawnNodeFromTemplate<UAnimStateTransitionNode>(Graph, NewObject<UAnimStateTransitionNode>(), Location, false);
    if (!Transition)
    {
        return AnimGraphMakeError(TEXT("spawn_failed"), TEXT("The transition node was not created."));
    }
    Transition->CreateConnections(FromState, ToState);

    double CrossfadeDuration = 0.0;
    if (Arguments->TryGetNumberField(TEXT("crossfade_duration"), CrossfadeDuration))
    {
        Transition->CrossfadeDuration = static_cast<float>(CrossfadeDuration);
    }
    double PriorityOrder = 0.0;
    if (Arguments->TryGetNumberField(TEXT("priority_order"), PriorityOrder))
    {
        Transition->PriorityOrder = static_cast<int32>(PriorityOrder);
    }

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

    TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
    Data->SetStringField(TEXT("state_machine_graph"), AnimGraphRelativePath(Graph, Blueprint));
    Data->SetStringField(TEXT("from_state"), FromState->GetStateName());
    Data->SetStringField(TEXT("to_state"), ToState->GetStateName());
    Data->SetObjectField(TEXT("node"), AnimGraphNodeJson(Transition));
    Data->SetNumberField(TEXT("crossfade_duration"), Transition->CrossfadeDuration);
    Data->SetNumberField(TEXT("priority_order"), Transition->PriorityOrder);

    // The rule graph is named "Transition" like every other one, so hand back its path and result node:
    // connect a bool to the result node's bCanEnterTransition pin with the existing graph tools.
    UEdGraph* RuleGraph = Transition->GetBoundGraph();
    Data->SetStringField(TEXT("rule_graph"), AnimGraphRelativePath(RuleGraph, Blueprint));
    if (RuleGraph)
    {
        for (UEdGraphNode* Node : RuleGraph->Nodes)
        {
            if (Node && Node->IsA<UAnimGraphNode_TransitionResult>())
            {
                Data->SetObjectField(TEXT("result_node"), AnimGraphNodeJson(Node));
                Data->SetStringField(TEXT("result_pin"), TEXT("bCanEnterTransition"));
                break;
            }
        }
    }
    return AnimGraphMakeOk(Data);
}

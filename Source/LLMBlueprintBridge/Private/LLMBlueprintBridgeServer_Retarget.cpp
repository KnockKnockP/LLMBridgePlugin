// IK Retargeter tools for the bridge.
//
// Kept in its own translation unit for the same reason as the animation and sequencer tools.
// An IKRetargeter must only be mutated through UIKRetargeterController - it keeps the retarget
// pose sorted hierarchically and reinitializes any running processor, neither of which happens
// when the asset's fields are written directly.
//
// The retarget pose stores one bone-space delta per bone, applied as
//     GlobalRetargetPoseRotation = GlobalRefRotation * Delta
// so a delta measured as "target orientation relative to source orientation" can be written
// back inverted, with no change of basis.

#include "LLMBlueprintBridgeServer.h"

#include "AssetRegistry/AssetData.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EditorAnimUtils.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/PackageName.h"
#include "Retargeter/IKRetargetChainMapping.h"
#include "Retargeter/IKRetargetSettings.h"
#include "Retargeter/IKRetargeter.h"
#include "RetargetEditor/IKRetargetBatchOperation.h"
#include "RetargetEditor/IKRetargeterController.h"
#include "RetargetEditor/IKRetargeterPoseGenerator.h"
#include "Rig/IKRigDefinition.h"
#include "UObject/Package.h"

namespace
{
    TSharedRef<FJsonObject> RtgMakeError(const FString& Code, const FString& Message)
    {
        TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
        Root->SetBoolField(TEXT("ok"), false);
        Root->SetStringField(TEXT("code"), Code);
        Root->SetStringField(TEXT("message"), Message);
        return Root;
    }

    TSharedRef<FJsonObject> RtgMakeOk(const TSharedPtr<FJsonObject>& Data)
    {
        TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
        Root->SetBoolField(TEXT("ok"), true);
        if (Data.IsValid())
        {
            Root->SetObjectField(TEXT("data"), Data);
        }
        return Root;
    }

    FString RtgNormalizeObjectPath(FString AssetPath)
    {
        AssetPath.TrimStartAndEndInline();
        const TCHAR QuoteChar = TEXT('\'');
        int32 FirstQuote = INDEX_NONE;
        int32 LastQuote = INDEX_NONE;
        if (AssetPath.FindChar(QuoteChar, FirstQuote) && AssetPath.FindLastChar(QuoteChar, LastQuote) && LastQuote > FirstQuote)
        {
            AssetPath = AssetPath.Mid(FirstQuote + 1, LastQuote - FirstQuote - 1);
        }
        if (!AssetPath.Contains(TEXT(".")))
        {
            const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
            if (!AssetName.IsEmpty())
            {
                AssetPath = FString::Printf(TEXT("%s.%s"), *AssetPath, *AssetName);
            }
        }
        return AssetPath;
    }

    /** Loads the retargeter named by the given field and hands back its controller. */
    UIKRetargeterController* RtgLoadController(const TSharedPtr<FJsonObject>& Arguments,
                                               const TCHAR* FieldName,
                                               UIKRetargeter*& OutAsset,
                                               FString& OutError)
    {
        FString AssetPath;
        if (!Arguments.IsValid() || !Arguments->TryGetStringField(FieldName, AssetPath) || AssetPath.IsEmpty())
        {
            OutError = FString::Printf(TEXT("Missing required string field '%s'."), FieldName);
            return nullptr;
        }

        const FString ObjectPath = RtgNormalizeObjectPath(AssetPath);
        OutAsset = LoadObject<UIKRetargeter>(nullptr, *ObjectPath);
        if (!OutAsset)
        {
            OutError = FString::Printf(TEXT("Could not load an IKRetargeter at '%s'."), *ObjectPath);
            return nullptr;
        }

        UIKRetargeterController* Controller = UIKRetargeterController::GetController(OutAsset);
        if (!Controller)
        {
            OutError = FString::Printf(TEXT("IKRetargeter '%s' has no controller."), *ObjectPath);
            return nullptr;
        }
        return Controller;
    }

    /** "source"/"target", defaulting to target - the target pose is the one that gets aligned. */
    bool RtgResolveSide(const TSharedPtr<FJsonObject>& Arguments, ERetargetSourceOrTarget& OutSide, FString& OutError)
    {
        OutSide = ERetargetSourceOrTarget::Target;
        FString Requested;
        if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("source_or_target"), Requested) || Requested.IsEmpty())
        {
            return true;
        }
        if (Requested.Equals(TEXT("source"), ESearchCase::IgnoreCase))
        {
            OutSide = ERetargetSourceOrTarget::Source;
            return true;
        }
        if (Requested.Equals(TEXT("target"), ESearchCase::IgnoreCase))
        {
            OutSide = ERetargetSourceOrTarget::Target;
            return true;
        }
        OutError = FString::Printf(TEXT("'source_or_target' must be 'source' or 'target', got '%s'."), *Requested);
        return false;
    }

    bool RtgResolveAlignMethod(const TSharedPtr<FJsonObject>& Arguments, ERetargetAutoAlignMethod& OutMethod, FString& OutError)
    {
        OutMethod = ERetargetAutoAlignMethod::ChainToChain;
        FString Requested;
        if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("method"), Requested) || Requested.IsEmpty())
        {
            return true;
        }
        const FString Normalized = Requested.Replace(TEXT("_"), TEXT("")).Replace(TEXT(" "), TEXT(""));
        if (Normalized.Equals(TEXT("chaintochain"), ESearchCase::IgnoreCase))
        {
            OutMethod = ERetargetAutoAlignMethod::ChainToChain;
            return true;
        }
        if (Normalized.Equals(TEXT("meshtomesh"), ESearchCase::IgnoreCase))
        {
            OutMethod = ERetargetAutoAlignMethod::MeshToMesh;
            return true;
        }
        if (Normalized.Equals(TEXT("localrotationaxes"), ESearchCase::IgnoreCase))
        {
            OutMethod = ERetargetAutoAlignMethod::LocalRotationAxes;
            return true;
        }
        if (Normalized.Equals(TEXT("globalrotationaxes"), ESearchCase::IgnoreCase))
        {
            OutMethod = ERetargetAutoAlignMethod::GlobalRotationAxes;
            return true;
        }
        OutError = FString::Printf(
            TEXT("'method' must be chain_to_chain, mesh_to_mesh, local_rotation_axes or global_rotation_axes, got '%s'."),
            *Requested);
        return false;
    }

    /** Accepts {pitch,yaw,roll} degrees or {x,y,z,w} quaternion. */
    bool RtgParseRotation(const TSharedPtr<FJsonObject>& Arguments, FQuat& OutQuat, FString& OutError)
    {
        const TSharedPtr<FJsonObject>* RotationObject = nullptr;
        if (Arguments.IsValid() && Arguments->TryGetObjectField(TEXT("rotation"), RotationObject) && RotationObject && RotationObject->IsValid())
        {
            const TSharedPtr<FJsonObject>& Rot = *RotationObject;
            double W = 0.0;
            if (Rot->TryGetNumberField(TEXT("w"), W))
            {
                double X = 0.0;
                double Y = 0.0;
                double Z = 0.0;
                Rot->TryGetNumberField(TEXT("x"), X);
                Rot->TryGetNumberField(TEXT("y"), Y);
                Rot->TryGetNumberField(TEXT("z"), Z);
                const FQuat Parsed(X, Y, Z, W);
                if (Parsed.SizeSquared() < KINDA_SMALL_NUMBER)
                {
                    OutError = TEXT("'rotation' quaternion is zero length.");
                    return false;
                }
                OutQuat = Parsed.GetNormalized();
                return true;
            }

            double Pitch = 0.0;
            double Yaw = 0.0;
            double Roll = 0.0;
            Rot->TryGetNumberField(TEXT("pitch"), Pitch);
            Rot->TryGetNumberField(TEXT("yaw"), Yaw);
            Rot->TryGetNumberField(TEXT("roll"), Roll);
            OutQuat = FRotator(Pitch, Yaw, Roll).Quaternion();
            return true;
        }

        OutError = TEXT("Missing required object field 'rotation' ({pitch,yaw,roll} degrees or {x,y,z,w}).");
        return false;
    }

    TSharedPtr<FJsonObject> RtgQuatToJson(const FQuat& Quat)
    {
        TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
        const FRotator AsRotator = Quat.Rotator();
        Object->SetNumberField(TEXT("pitch"), AsRotator.Pitch);
        Object->SetNumberField(TEXT("yaw"), AsRotator.Yaw);
        Object->SetNumberField(TEXT("roll"), AsRotator.Roll);
        Object->SetNumberField(TEXT("angle"), FMath::RadiansToDegrees(Quat.GetAngle()));
        return Object;
    }

    void RtgCollectBoneNames(const TSharedPtr<FJsonObject>& Arguments, const TCHAR* FieldName, TArray<FName>& OutBones)
    {
        const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
        if (!Arguments.IsValid() || !Arguments->TryGetArrayField(FieldName, Entries) || !Entries)
        {
            return;
        }
        for (const TSharedPtr<FJsonValue>& Entry : *Entries)
        {
            FString Name;
            if (Entry.IsValid() && Entry->TryGetString(Name) && !Name.IsEmpty())
            {
                OutBones.AddUnique(FName(*Name));
            }
        }
    }

    const TCHAR* RtgSideName(const ERetargetSourceOrTarget Side)
    {
        return Side == ERetargetSourceOrTarget::Source ? TEXT("source") : TEXT("target");
    }
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolInspectIKRetargeter(const TSharedPtr<FJsonObject>& Arguments)
{
    UIKRetargeter* Asset = nullptr;
    FString Error;
    UIKRetargeterController* Controller = RtgLoadController(Arguments, TEXT("asset_path"), Asset, Error);
    if (!Controller)
    {
        return RtgMakeError(Error.StartsWith(TEXT("Missing")) ? TEXT("invalid_arguments") : TEXT("load_failed"), Error);
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), Asset->GetPathName());

    for (int32 SideIndex = 0; SideIndex < 2; ++SideIndex)
    {
        const ERetargetSourceOrTarget Side = SideIndex == 0 ? ERetargetSourceOrTarget::Source : ERetargetSourceOrTarget::Target;

        TSharedPtr<FJsonObject> SideObject = MakeShared<FJsonObject>();
        const UIKRigDefinition* IKRig = Controller->GetIKRig(Side);
        SideObject->SetStringField(TEXT("ik_rig"), IKRig ? IKRig->GetPathName() : FString());
        const USkeletalMesh* PreviewMesh = Controller->GetPreviewMesh(Side);
        SideObject->SetStringField(TEXT("preview_mesh"), PreviewMesh ? PreviewMesh->GetPathName() : FString());
        SideObject->SetStringField(TEXT("current_retarget_pose"), Controller->GetCurrentRetargetPoseName(Side).ToString());

        TArray<TSharedPtr<FJsonValue>> PoseNames;
        const TMap<FName, FIKRetargetPose>& Poses = Controller->GetRetargetPoses(Side);
        for (const TPair<FName, FIKRetargetPose>& Pose : Poses)
        {
            PoseNames.Add(MakeShared<FJsonValueString>(Pose.Key.ToString()));
        }
        SideObject->SetArrayField(TEXT("retarget_poses"), PoseNames);

        // Only the bones that actually carry an offset - an unedited pose would otherwise
        // print the entire skeleton as identity rotations.
        TArray<TSharedPtr<FJsonValue>> Offsets;
        const FIKRetargetPose& Current = Controller->GetCurrentRetargetPose(Side);
        for (const TPair<FName, FQuat>& Offset : Current.GetAllDeltaRotations())
        {
            const double AngleDegrees = FMath::RadiansToDegrees(Offset.Value.GetAngle());
            if (AngleDegrees < 0.001)
            {
                continue;
            }
            TSharedPtr<FJsonObject> Entry = RtgQuatToJson(Offset.Value);
            Entry->SetStringField(TEXT("bone"), Offset.Key.ToString());
            Offsets.Add(MakeShared<FJsonValueObject>(Entry));
        }
        SideObject->SetArrayField(TEXT("bone_rotation_offsets"), Offsets);
        SideObject->SetNumberField(TEXT("bone_rotation_offset_count"), Offsets.Num());

        const FVector RootOffset = Current.GetRootTranslationDelta();
        TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();
        RootObject->SetNumberField(TEXT("x"), RootOffset.X);
        RootObject->SetNumberField(TEXT("y"), RootOffset.Y);
        RootObject->SetNumberField(TEXT("z"), RootOffset.Z);
        SideObject->SetObjectField(TEXT("root_translation_offset"), RootObject);

        Data->SetObjectField(RtgSideName(Side), SideObject);
    }

    TArray<TSharedPtr<FJsonValue>> ChainPairs;
    // Named overload explicitly: the no-argument one is a different, deprecated accessor.
    if (const FRetargetChainMapping* Mapping = Controller->GetChainMapping(NAME_None))
    {
        for (const FRetargetChainPair& Pair : Mapping->GetChainPairs())
        {
            TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("target_chain"), Pair.TargetChainName.ToString());
            Entry->SetStringField(TEXT("source_chain"), Pair.SourceChainName.ToString());
            Entry->SetBoolField(TEXT("mapped"), Pair.SourceChainName != NAME_None);
            ChainPairs.Add(MakeShared<FJsonValueObject>(Entry));
        }
    }
    Data->SetArrayField(TEXT("chain_mapping"), ChainPairs);
    Data->SetNumberField(TEXT("chain_count"), ChainPairs.Num());

    return RtgMakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolCreateRetargetPose(const TSharedPtr<FJsonObject>& Arguments)
{
    UIKRetargeter* Asset = nullptr;
    FString Error;
    UIKRetargeterController* Controller = RtgLoadController(Arguments, TEXT("asset_path"), Asset, Error);
    if (!Controller)
    {
        return RtgMakeError(Error.StartsWith(TEXT("Missing")) ? TEXT("invalid_arguments") : TEXT("load_failed"), Error);
    }

    ERetargetSourceOrTarget Side;
    if (!RtgResolveSide(Arguments, Side, Error))
    {
        return RtgMakeError(TEXT("invalid_arguments"), Error);
    }

    FString PoseName;
    if (!Arguments->TryGetStringField(TEXT("name"), PoseName) || PoseName.IsEmpty())
    {
        return RtgMakeError(TEXT("invalid_arguments"), TEXT("Missing required string field 'name'."));
    }

    FString DuplicateFrom;
    Arguments->TryGetStringField(TEXT("duplicate_from"), DuplicateFrom);

    FName Created;
    if (!DuplicateFrom.IsEmpty())
    {
        Created = Controller->DuplicateRetargetPose(FName(*DuplicateFrom), FName(*PoseName), Side);
    }
    else
    {
        Created = Controller->CreateRetargetPose(FName(*PoseName), Side);
    }

    if (Created == NAME_None)
    {
        return RtgMakeError(TEXT("create_failed"), FString::Printf(TEXT("Could not create retarget pose '%s'."), *PoseName));
    }

    bool bSetCurrent = true;
    Arguments->TryGetBoolField(TEXT("set_current"), bSetCurrent);
    if (bSetCurrent)
    {
        Controller->SetCurrentRetargetPose(Created, Side);
    }

    Asset->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), Asset->GetPathName());
    Data->SetStringField(TEXT("pose"), Created.ToString());
    Data->SetStringField(TEXT("source_or_target"), RtgSideName(Side));
    Data->SetStringField(TEXT("current_retarget_pose"), Controller->GetCurrentRetargetPoseName(Side).ToString());
    Data->SetBoolField(TEXT("dirty"), true);

    bool bSave = false;
    Arguments->TryGetBoolField(TEXT("save"), bSave);
    if (bSave)
    {
        const TSharedRef<FJsonObject> SaveResult = ToolSaveAsset(Arguments);
        Data->SetBoolField(TEXT("saved"), SaveResult->GetBoolField(TEXT("ok")));
    }

    return RtgMakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolSetRetargetPoseBoneRotation(const TSharedPtr<FJsonObject>& Arguments)
{
    UIKRetargeter* Asset = nullptr;
    FString Error;
    UIKRetargeterController* Controller = RtgLoadController(Arguments, TEXT("asset_path"), Asset, Error);
    if (!Controller)
    {
        return RtgMakeError(Error.StartsWith(TEXT("Missing")) ? TEXT("invalid_arguments") : TEXT("load_failed"), Error);
    }

    ERetargetSourceOrTarget Side;
    if (!RtgResolveSide(Arguments, Side, Error))
    {
        return RtgMakeError(TEXT("invalid_arguments"), Error);
    }

    FString BoneName;
    if (!Arguments->TryGetStringField(TEXT("bone"), BoneName) || BoneName.IsEmpty())
    {
        return RtgMakeError(TEXT("invalid_arguments"), TEXT("Missing required string field 'bone'."));
    }

    FQuat Rotation;
    if (!RtgParseRotation(Arguments, Rotation, Error))
    {
        return RtgMakeError(TEXT("invalid_arguments"), Error);
    }

    const FName Bone(*BoneName);
    const FQuat Previous = Controller->GetRotationOffsetForRetargetPoseBone(Bone, Side);

    // "set" replaces the stored delta, "compose" stacks onto it - which is what an iterative
    // correction pass wants, since each pass only measures the residual left by the last one.
    FString Mode = TEXT("set");
    Arguments->TryGetStringField(TEXT("mode"), Mode);
    FQuat NewOffset = Rotation;
    if (Mode.Equals(TEXT("compose"), ESearchCase::IgnoreCase))
    {
        NewOffset = (Previous * Rotation).GetNormalized();
    }
    else if (!Mode.Equals(TEXT("set"), ESearchCase::IgnoreCase))
    {
        return RtgMakeError(TEXT("invalid_arguments"), FString::Printf(TEXT("'mode' must be 'set' or 'compose', got '%s'."), *Mode));
    }

    Controller->SetRotationOffsetForRetargetPoseBone(Bone, NewOffset, Side);
    Asset->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), Asset->GetPathName());
    Data->SetStringField(TEXT("bone"), BoneName);
    Data->SetStringField(TEXT("source_or_target"), RtgSideName(Side));
    Data->SetStringField(TEXT("pose"), Controller->GetCurrentRetargetPoseName(Side).ToString());
    Data->SetStringField(TEXT("mode"), Mode.ToLower());
    Data->SetObjectField(TEXT("previous_offset"), RtgQuatToJson(Previous));
    Data->SetObjectField(TEXT("offset"), RtgQuatToJson(NewOffset));
    Data->SetBoolField(TEXT("dirty"), true);

    bool bSave = false;
    Arguments->TryGetBoolField(TEXT("save"), bSave);
    if (bSave)
    {
        const TSharedRef<FJsonObject> SaveResult = ToolSaveAsset(Arguments);
        Data->SetBoolField(TEXT("saved"), SaveResult->GetBoolField(TEXT("ok")));
    }

    return RtgMakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolAutoAlignRetargetPose(const TSharedPtr<FJsonObject>& Arguments)
{
    UIKRetargeter* Asset = nullptr;
    FString Error;
    UIKRetargeterController* Controller = RtgLoadController(Arguments, TEXT("asset_path"), Asset, Error);
    if (!Controller)
    {
        return RtgMakeError(Error.StartsWith(TEXT("Missing")) ? TEXT("invalid_arguments") : TEXT("load_failed"), Error);
    }

    ERetargetSourceOrTarget Side;
    if (!RtgResolveSide(Arguments, Side, Error))
    {
        return RtgMakeError(TEXT("invalid_arguments"), Error);
    }

    ERetargetAutoAlignMethod Method;
    if (!RtgResolveAlignMethod(Arguments, Method, Error))
    {
        return RtgMakeError(TEXT("invalid_arguments"), Error);
    }

    // Alignment resolves the asset onto real skeletons, so both preview meshes have to be set.
    if (!Controller->GetPreviewMesh(ERetargetSourceOrTarget::Source) || !Controller->GetPreviewMesh(ERetargetSourceOrTarget::Target))
    {
        return RtgMakeError(TEXT("align_failed"),
            TEXT("Both the source and target preview meshes must be set on the retargeter before aligning."));
    }

    TArray<FName> Bones;
    RtgCollectBoneNames(Arguments, TEXT("bones"), Bones);

    // AutoAlignAllBones resets the whole pose first; the per-bone entry point does not, which is
    // what keeps a partial align from throwing away corrections already made on other bones.
    if (Bones.Num() == 0)
    {
        Controller->AutoAlignAllBones(Side, Method);
    }
    else
    {
        Controller->AutoAlignBones(Bones, Method, Side);
    }

    Asset->MarkPackageDirty();

    const FIKRetargetPose& Current = Controller->GetCurrentRetargetPose(Side);
    int32 OffsetCount = 0;
    double LargestAngle = 0.0;
    FString LargestBone;
    for (const TPair<FName, FQuat>& Offset : Current.GetAllDeltaRotations())
    {
        const double AngleDegrees = FMath::RadiansToDegrees(Offset.Value.GetAngle());
        if (AngleDegrees < 0.001)
        {
            continue;
        }
        ++OffsetCount;
        if (AngleDegrees > LargestAngle)
        {
            LargestAngle = AngleDegrees;
            LargestBone = Offset.Key.ToString();
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), Asset->GetPathName());
    Data->SetStringField(TEXT("source_or_target"), RtgSideName(Side));
    Data->SetStringField(TEXT("pose"), Controller->GetCurrentRetargetPoseName(Side).ToString());
    Data->SetNumberField(TEXT("requested_bone_count"), Bones.Num());
    Data->SetNumberField(TEXT("bone_rotation_offset_count"), OffsetCount);
    Data->SetNumberField(TEXT("largest_offset_degrees"), LargestAngle);
    Data->SetStringField(TEXT("largest_offset_bone"), LargestBone);
    Data->SetBoolField(TEXT("dirty"), true);

    bool bSave = false;
    Arguments->TryGetBoolField(TEXT("save"), bSave);
    if (bSave)
    {
        const TSharedRef<FJsonObject> SaveResult = ToolSaveAsset(Arguments);
        Data->SetBoolField(TEXT("saved"), SaveResult->GetBoolField(TEXT("ok")));
    }

    return RtgMakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolResetRetargetPose(const TSharedPtr<FJsonObject>& Arguments)
{
    UIKRetargeter* Asset = nullptr;
    FString Error;
    UIKRetargeterController* Controller = RtgLoadController(Arguments, TEXT("asset_path"), Asset, Error);
    if (!Controller)
    {
        return RtgMakeError(Error.StartsWith(TEXT("Missing")) ? TEXT("invalid_arguments") : TEXT("load_failed"), Error);
    }

    ERetargetSourceOrTarget Side;
    if (!RtgResolveSide(Arguments, Side, Error))
    {
        return RtgMakeError(TEXT("invalid_arguments"), Error);
    }

    FString PoseName;
    Arguments->TryGetStringField(TEXT("pose"), PoseName);
    const FName Pose = PoseName.IsEmpty() ? Controller->GetCurrentRetargetPoseName(Side) : FName(*PoseName);

    TArray<FName> Bones;
    RtgCollectBoneNames(Arguments, TEXT("bones"), Bones);

    Controller->ResetRetargetPose(Pose, Bones, Side);
    Asset->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), Asset->GetPathName());
    Data->SetStringField(TEXT("pose"), Pose.ToString());
    Data->SetStringField(TEXT("source_or_target"), RtgSideName(Side));
    Data->SetNumberField(TEXT("reset_bone_count"), Bones.Num());
    Data->SetBoolField(TEXT("dirty"), true);

    bool bSave = false;
    Arguments->TryGetBoolField(TEXT("save"), bSave);
    if (bSave)
    {
        const TSharedRef<FJsonObject> SaveResult = ToolSaveAsset(Arguments);
        Data->SetBoolField(TEXT("saved"), SaveResult->GetBoolField(TEXT("ok")));
    }

    return RtgMakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolRetargetAnimations(const TSharedPtr<FJsonObject>& Arguments)
{
    UIKRetargeter* Asset = nullptr;
    FString Error;
    UIKRetargeterController* Controller = RtgLoadController(Arguments, TEXT("retargeter"), Asset, Error);
    if (!Controller)
    {
        return RtgMakeError(Error.StartsWith(TEXT("Missing")) ? TEXT("invalid_arguments") : TEXT("load_failed"), Error);
    }

    const TArray<TSharedPtr<FJsonValue>>* AnimationEntries = nullptr;
    if (!Arguments->TryGetArrayField(TEXT("animations"), AnimationEntries) || !AnimationEntries || AnimationEntries->Num() == 0)
    {
        return RtgMakeError(TEXT("invalid_arguments"), TEXT("Missing required non-empty array field 'animations'."));
    }

    TArray<TWeakObjectPtr<UObject>> AssetsToRetarget;
    TArray<TSharedPtr<FJsonValue>> Missing;
    for (const TSharedPtr<FJsonValue>& Entry : *AnimationEntries)
    {
        FString Path;
        if (!Entry.IsValid() || !Entry->TryGetString(Path) || Path.IsEmpty())
        {
            continue;
        }
        UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr, *RtgNormalizeObjectPath(Path));
        if (!Sequence)
        {
            Missing.Add(MakeShared<FJsonValueString>(Path));
            continue;
        }
        AssetsToRetarget.Add(Sequence);
    }

    if (AssetsToRetarget.Num() == 0)
    {
        return RtgMakeError(TEXT("load_failed"), TEXT("None of the listed animations could be loaded."));
    }

    FIKRetargetBatchOperationContext Context;
    Context.IKRetargetAsset = Asset;
    Context.SourceMesh = Controller->GetPreviewMesh(ERetargetSourceOrTarget::Source);
    Context.TargetMesh = Controller->GetPreviewMesh(ERetargetSourceOrTarget::Target);
    Context.AssetsToRetarget = AssetsToRetarget;

    FString OverrideMesh;
    if (Arguments->TryGetStringField(TEXT("source_mesh"), OverrideMesh) && !OverrideMesh.IsEmpty())
    {
        Context.SourceMesh = LoadObject<USkeletalMesh>(nullptr, *RtgNormalizeObjectPath(OverrideMesh));
    }
    OverrideMesh.Reset();
    if (Arguments->TryGetStringField(TEXT("target_mesh"), OverrideMesh) && !OverrideMesh.IsEmpty())
    {
        Context.TargetMesh = LoadObject<USkeletalMesh>(nullptr, *RtgNormalizeObjectPath(OverrideMesh));
    }

    if (!Context.IsValid())
    {
        return RtgMakeError(TEXT("invalid_arguments"),
            TEXT("The retargeter needs a distinct source and target skeletal mesh. Set the preview meshes or pass 'source_mesh'/'target_mesh'."));
    }

    // Without an explicit destination the rule's folder defaults to "/Game", which would dump
    // every result at the content root. Fall back to writing each result beside its source.
    FString DestinationPath;
    if (Arguments->TryGetStringField(TEXT("destination_path"), DestinationPath) && !DestinationPath.IsEmpty())
    {
        if (!DestinationPath.StartsWith(TEXT("/Game")))
        {
            return RtgMakeError(TEXT("invalid_arguments"), TEXT("'destination_path' must be under /Game."));
        }
        Context.NameRule.FolderPath = DestinationPath;
        Context.bUseSourcePath = false;
    }
    else
    {
        Context.bUseSourcePath = true;
    }
    Arguments->TryGetStringField(TEXT("prefix"), Context.NameRule.Prefix);
    Arguments->TryGetStringField(TEXT("suffix"), Context.NameRule.Suffix);
    Arguments->TryGetStringField(TEXT("search"), Context.NameRule.ReplaceFrom);
    Arguments->TryGetStringField(TEXT("replace"), Context.NameRule.ReplaceTo);

    // Referenced assets default off here: the ladder batch is a flat list of sequences and
    // pulling in referenced assets would duplicate montages and blendspaces nobody asked for.
    Context.bIncludeReferencedAssets = false;
    Arguments->TryGetBoolField(TEXT("include_referenced"), Context.bIncludeReferencedAssets);
    Context.bOverwriteExistingFiles = false;
    Arguments->TryGetBoolField(TEXT("overwrite"), Context.bOverwriteExistingFiles);
    Arguments->TryGetBoolField(TEXT("export_only_animated_bones"), Context.bExportOnlyAnimatedBones);

    UIKRetargetBatchOperation* Operation = NewObject<UIKRetargetBatchOperation>();
    Operation->AddToRoot();
    Operation->RunRetarget(Context);
    Operation->RemoveFromRoot();

    // The operation keeps its result list private, so the outputs are resolved by replaying the
    // same rename rule the batch used and looking each one up. That also reports correctly under
    // overwrite=true, where nothing new appears in the content browser to diff against.
    TArray<TSharedPtr<FJsonValue>> Created;
    TArray<TSharedPtr<FJsonValue>> NotProduced;
    for (const TWeakObjectPtr<UObject>& SourceAsset : AssetsToRetarget)
    {
        const UObject* Source = SourceAsset.Get();
        if (!Source)
        {
            continue;
        }
        const FString NewName = Context.NameRule.Rename(Source);
        const FString Folder = Context.bUseSourcePath
            ? FPackageName::GetLongPackagePath(Source->GetPathName())
            : Context.NameRule.FolderPath;
        const FString NewPath = FString::Printf(TEXT("%s/%s.%s"), *Folder, *NewName, *NewName);
        if (LoadObject<UAnimSequence>(nullptr, *NewPath))
        {
            Created.Add(MakeShared<FJsonValueString>(NewPath));
        }
        else
        {
            NotProduced.Add(MakeShared<FJsonValueString>(NewPath));
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("retargeter"), Asset->GetPathName());
    Data->SetStringField(TEXT("source_mesh"), Context.SourceMesh->GetPathName());
    Data->SetStringField(TEXT("target_mesh"), Context.TargetMesh->GetPathName());
    Data->SetStringField(TEXT("destination_path"), Context.bUseSourcePath ? TEXT("<source folder>") : Context.NameRule.FolderPath);
    Data->SetNumberField(TEXT("requested"), AssetsToRetarget.Num());
    Data->SetArrayField(TEXT("created"), Created);
    Data->SetNumberField(TEXT("created_count"), Created.Num());
    Data->SetBoolField(TEXT("overwrite"), Context.bOverwriteExistingFiles);
    if (NotProduced.Num() > 0)
    {
        Data->SetArrayField(TEXT("not_produced"), NotProduced);
    }
    if (Missing.Num() > 0)
    {
        Data->SetArrayField(TEXT("missing_animations"), Missing);
    }

    return RtgMakeOk(Data);
}

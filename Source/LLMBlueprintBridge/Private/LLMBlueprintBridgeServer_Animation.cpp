// Animation tools for the bridge.
//
// Kept in its own translation unit so LLMBlueprintBridgeServer.cpp does not grow further.
// Raw animation data in UE5 is owned by IAnimationDataModel and may only be mutated through
// IAnimationDataController, which is what every write below goes through - never the
// UAnimSequence fields directly.

#include "LLMBlueprintBridgeServer.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/Skeleton.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace
{
    TSharedRef<FJsonObject> AnimMakeError(const FString& Code, const FString& Message)
    {
        TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
        Root->SetBoolField(TEXT("ok"), false);
        Root->SetStringField(TEXT("code"), Code);
        Root->SetStringField(TEXT("message"), Message);
        return Root;
    }

    TSharedRef<FJsonObject> AnimMakeOk(const TSharedPtr<FJsonObject>& Data)
    {
        TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
        Root->SetBoolField(TEXT("ok"), true);
        if (Data.IsValid())
        {
            Root->SetObjectField(TEXT("data"), Data);
        }
        return Root;
    }

    FString AnimNormalizeObjectPath(FString AssetPath)
    {
        AssetPath.TrimStartAndEndInline();
        int32 FirstQuote = INDEX_NONE;
        int32 LastQuote = INDEX_NONE;
        if (AssetPath.FindChar(TEXT('\''), FirstQuote) && AssetPath.FindLastChar(TEXT('\''), LastQuote) && LastQuote > FirstQuote)
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

    UAnimSequence* AnimLoadSequence(const TSharedPtr<FJsonObject>& Arguments, FString& OutError)
    {
        FString AssetPath;
        if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
        {
            OutError = TEXT("Missing required string field 'asset_path'.");
            return nullptr;
        }

        const FString ObjectPath = AnimNormalizeObjectPath(AssetPath);
        UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr, *ObjectPath);
        if (!Sequence)
        {
            OutError = FString::Printf(TEXT("Could not load an AnimSequence at '%s'."), *ObjectPath);
            return nullptr;
        }
        return Sequence;
    }

    /** Bone the caller asked for, or the skeleton root when they did not ask for one. */
    bool AnimResolveBoneName(const UAnimSequence* Sequence, const TSharedPtr<FJsonObject>& Arguments, FName& OutBoneName, FString& OutError)
    {
        FString Requested;
        if (Arguments.IsValid() && Arguments->TryGetStringField(TEXT("bone"), Requested) && !Requested.IsEmpty())
        {
            OutBoneName = FName(*Requested);
            return true;
        }

        const USkeleton* Skeleton = Sequence->GetSkeleton();
        if (!Skeleton || Skeleton->GetReferenceSkeleton().GetNum() <= 0)
        {
            OutError = TEXT("Sequence has no skeleton, so the root bone could not be resolved. Pass 'bone' explicitly.");
            return false;
        }

        OutBoneName = Skeleton->GetReferenceSkeleton().GetBoneName(0);
        return true;
    }

    bool AnimHasBoneTrack(const IAnimationDataModel* Model, FName BoneName)
    {
        TArray<FName> TrackNames;
        Model->GetBoneTrackNames(TrackNames);
        return TrackNames.Contains(BoneName);
    }

    /**
     * Reads the track through the model API rather than the deprecated FBoneAnimationTrack
     * accessors. A constant channel can report fewer transforms than the model has keys, so
     * the arrays are padded out to NumKeys - which is also what SetBoneTrackKeys wants back.
     */
    bool AnimReadTrack(const IAnimationDataModel* Model,
                       FName BoneName,
                       int32 NumKeys,
                       TArray<FVector3f>& OutPositions,
                       TArray<FQuat4f>& OutRotations,
                       TArray<FVector3f>& OutScales)
    {
        TArray<FTransform> Transforms;
        Model->GetBoneTrackTransforms(BoneName, Transforms);
        if (Transforms.Num() == 0)
        {
            return false;
        }

        OutPositions.Reset(NumKeys);
        OutRotations.Reset(NumKeys);
        OutScales.Reset(NumKeys);

        for (int32 KeyIndex = 0; KeyIndex < NumKeys; ++KeyIndex)
        {
            const FTransform& Transform = Transforms[FMath::Min(KeyIndex, Transforms.Num() - 1)];
            OutPositions.Add(FVector3f(Transform.GetTranslation()));
            OutRotations.Add(FQuat4f(Transform.GetRotation()));
            OutScales.Add(FVector3f(Transform.GetScale3D()));
        }
        return true;
    }

    /**
     * Composes a bone's transform up the reference-skeleton parent chain into component space.
     * The data model only stores parent-relative transforms, so a caller asking "where is the
     * hand at frame 20" needs this walk. Bones without a track fall back to the ref pose.
     * Cache is per-frame and keyed by bone index, which keeps a deep chain from being rewalked.
     */
    FTransform AnimComponentSpaceTransform(const IAnimationDataModel* Model,
                                           const FReferenceSkeleton& RefSkeleton,
                                           const TSet<FName>& TrackNames,
                                           int32 BoneIndex,
                                           const FFrameNumber& Frame,
                                           TMap<int32, FTransform>& Cache)
    {
        if (const FTransform* Cached = Cache.Find(BoneIndex))
        {
            return *Cached;
        }

        const FName BoneName = RefSkeleton.GetBoneName(BoneIndex);
        const FTransform LocalTransform = TrackNames.Contains(BoneName)
            ? Model->GetBoneTrackTransform(BoneName, Frame)
            : RefSkeleton.GetRefBonePose()[BoneIndex];

        const int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);
        const FTransform Result = ParentIndex == INDEX_NONE
            ? LocalTransform
            : LocalTransform * AnimComponentSpaceTransform(Model, RefSkeleton, TrackNames, ParentIndex, Frame, Cache);

        Cache.Add(BoneIndex, Result);
        return Result;
    }

    TSharedPtr<FJsonObject> AnimTransformToJson(const FTransform& Transform)
    {
        TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
        const FVector Translation = Transform.GetTranslation();
        Object->SetNumberField(TEXT("x"), Translation.X);
        Object->SetNumberField(TEXT("y"), Translation.Y);
        Object->SetNumberField(TEXT("z"), Translation.Z);

        const FRotator Rotator = Transform.GetRotation().Rotator();
        Object->SetNumberField(TEXT("pitch"), Rotator.Pitch);
        Object->SetNumberField(TEXT("yaw"), Rotator.Yaw);
        Object->SetNumberField(TEXT("roll"), Rotator.Roll);
        return Object;
    }

    TSharedPtr<FJsonObject> AnimKeyToJson(int32 KeyIndex,
                                          double TimeSeconds,
                                          const FVector3f& Position,
                                          const FQuat4f& Rotation)
    {
        TSharedPtr<FJsonObject> Key = MakeShared<FJsonObject>();
        Key->SetNumberField(TEXT("frame"), KeyIndex);
        Key->SetNumberField(TEXT("time"), TimeSeconds);
        Key->SetNumberField(TEXT("x"), Position.X);
        Key->SetNumberField(TEXT("y"), Position.Y);
        Key->SetNumberField(TEXT("z"), Position.Z);

        const FRotator3f Rotator = Rotation.Rotator();
        Key->SetNumberField(TEXT("pitch"), Rotator.Pitch);
        Key->SetNumberField(TEXT("yaw"), Rotator.Yaw);
        Key->SetNumberField(TEXT("roll"), Rotator.Roll);
        return Key;
    }
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolInspectAnimSequence(const TSharedPtr<FJsonObject>& Arguments)
{
    FString Error;
    UAnimSequence* Sequence = AnimLoadSequence(Arguments, Error);
    if (!Sequence)
    {
        return AnimMakeError(TEXT("load_failed"), Error);
    }

    const IAnimationDataModel* Model = Sequence->GetDataModel();
    if (!Model)
    {
        return AnimMakeError(TEXT("unsupported_animation_asset"), TEXT("Sequence has no animation data model."));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), Sequence->GetPathName());
    Data->SetStringField(TEXT("name"), Sequence->GetName());
    Data->SetStringField(TEXT("skeleton"), Sequence->GetSkeleton() ? Sequence->GetSkeleton()->GetPathName() : FString());
    Data->SetNumberField(TEXT("play_length"), Model->GetPlayLength());
    Data->SetNumberField(TEXT("number_of_frames"), Model->GetNumberOfFrames());
    Data->SetNumberField(TEXT("number_of_keys"), Model->GetNumberOfKeys());
    Data->SetNumberField(TEXT("frame_rate"), Model->GetFrameRate().AsDecimal());
    Data->SetBoolField(TEXT("enable_root_motion"), Sequence->bEnableRootMotion);
    Data->SetBoolField(TEXT("force_root_lock"), Sequence->bForceRootLock);
    Data->SetNumberField(TEXT("root_motion_root_lock"), static_cast<int32>(Sequence->RootMotionRootLock.GetValue()));

    TArray<FName> BoneTrackNames;
    Model->GetBoneTrackNames(BoneTrackNames);
    Data->SetNumberField(TEXT("bone_track_count"), BoneTrackNames.Num());

    FName RootBoneName;
    FString BoneError;
    if (AnimResolveBoneName(Sequence, Arguments, RootBoneName, BoneError))
    {
        Data->SetStringField(TEXT("root_bone"), RootBoneName.ToString());
        Data->SetBoolField(TEXT("has_root_track"), BoneTrackNames.Contains(RootBoneName));
    }

    return AnimMakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolGetRootMotionTrack(const TSharedPtr<FJsonObject>& Arguments)
{
    FString Error;
    UAnimSequence* Sequence = AnimLoadSequence(Arguments, Error);
    if (!Sequence)
    {
        return AnimMakeError(TEXT("load_failed"), Error);
    }

    const IAnimationDataModel* Model = Sequence->GetDataModel();
    if (!Model)
    {
        return AnimMakeError(TEXT("unsupported_animation_asset"), TEXT("Sequence has no animation data model."));
    }

    FName BoneName;
    if (!AnimResolveBoneName(Sequence, Arguments, BoneName, Error))
    {
        return AnimMakeError(TEXT("invalid_arguments"), Error);
    }

    if (!AnimHasBoneTrack(Model, BoneName))
    {
        return AnimMakeError(TEXT("bone_track_not_found"),
            FString::Printf(TEXT("Sequence has no bone track named '%s'."), *BoneName.ToString()));
    }

    const int32 NumKeys = Model->GetNumberOfKeys();
    TArray<FVector3f> Positions;
    TArray<FQuat4f> Rotations;
    TArray<FVector3f> Scales;
    if (!AnimReadTrack(Model, BoneName, NumKeys, Positions, Rotations, Scales))
    {
        return AnimMakeError(TEXT("bone_track_not_found"),
            FString::Printf(TEXT("Bone track '%s' returned no transforms."), *BoneName.ToString()));
    }

    int32 StartFrame = 0;
    int32 EndFrame = NumKeys - 1;
    if (Arguments.IsValid())
    {
        Arguments->TryGetNumberField(TEXT("start_frame"), StartFrame);
        Arguments->TryGetNumberField(TEXT("end_frame"), EndFrame);
    }
    StartFrame = FMath::Clamp(StartFrame, 0, FMath::Max(0, NumKeys - 1));
    EndFrame = FMath::Clamp(EndFrame, StartFrame, FMath::Max(0, NumKeys - 1));

    int32 MaxKeys = 512;
    if (Arguments.IsValid())
    {
        Arguments->TryGetNumberField(TEXT("max_keys"), MaxKeys);
    }
    MaxKeys = FMath::Clamp(MaxKeys, 1, 4096);

    const FFrameRate FrameRate = Model->GetFrameRate();
    const int32 Requested = EndFrame - StartFrame + 1;
    const int32 Stride = FMath::Max(1, FMath::DivideAndRoundUp(Requested, MaxKeys));

    TArray<TSharedPtr<FJsonValue>> Keys;
    for (int32 KeyIndex = StartFrame; KeyIndex <= EndFrame; KeyIndex += Stride)
    {
        const double TimeSeconds = FrameRate.AsInterval() * KeyIndex;
        Keys.Add(MakeShared<FJsonValueObject>(AnimKeyToJson(KeyIndex, TimeSeconds, Positions[KeyIndex], Rotations[KeyIndex])));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), Sequence->GetPathName());
    Data->SetStringField(TEXT("bone"), BoneName.ToString());
    Data->SetNumberField(TEXT("number_of_keys"), NumKeys);
    Data->SetNumberField(TEXT("start_frame"), StartFrame);
    Data->SetNumberField(TEXT("end_frame"), EndFrame);
    Data->SetNumberField(TEXT("stride"), Stride);
    Data->SetArrayField(TEXT("keys"), Keys);
    return AnimMakeOk(Data);
}

/** Shared write path for set_root_motion_keys and flatten_root_motion_axis. */
TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::WriteRootMotionTrack(const TSharedPtr<FJsonObject>& Arguments,
                                                                       TFunctionRef<bool(int32, TArray<FVector3f>&, TArray<FQuat4f>&, FString&)> Mutate,
                                                                       const FString& TransactionTitle)
{
    FString Error;
    UAnimSequence* Sequence = AnimLoadSequence(Arguments, Error);
    if (!Sequence)
    {
        return AnimMakeError(TEXT("load_failed"), Error);
    }

    if (!Sequence->GetPathName().StartsWith(TEXT("/Game/")))
    {
        return AnimMakeError(TEXT("invalid_arguments"), TEXT("Animation writes are limited to /Game assets."));
    }

    const IAnimationDataModel* Model = Sequence->GetDataModel();
    if (!Model)
    {
        return AnimMakeError(TEXT("unsupported_animation_asset"), TEXT("Sequence has no animation data model."));
    }

    FName BoneName;
    if (!AnimResolveBoneName(Sequence, Arguments, BoneName, Error))
    {
        return AnimMakeError(TEXT("invalid_arguments"), Error);
    }

    if (!AnimHasBoneTrack(Model, BoneName))
    {
        return AnimMakeError(TEXT("bone_track_not_found"),
            FString::Printf(TEXT("Sequence has no bone track named '%s'."), *BoneName.ToString()));
    }

    const int32 NumKeys = Model->GetNumberOfKeys();
    if (NumKeys <= 0)
    {
        return AnimMakeError(TEXT("unsupported_animation_asset"), TEXT("Sequence has no keys to edit."));
    }

    TArray<FVector3f> Positions;
    TArray<FQuat4f> Rotations;
    TArray<FVector3f> Scales;
    if (!AnimReadTrack(Model, BoneName, NumKeys, Positions, Rotations, Scales))
    {
        return AnimMakeError(TEXT("bone_track_not_found"),
            FString::Printf(TEXT("Bone track '%s' returned no transforms."), *BoneName.ToString()));
    }

    if (!Mutate(NumKeys, Positions, Rotations, Error))
    {
        return AnimMakeError(TEXT("invalid_arguments"), Error);
    }

    IAnimationDataController& Controller = Sequence->GetController();
    Controller.OpenBracket(FText::FromString(TransactionTitle), true);
    const bool bWrote = Controller.SetBoneTrackKeys(BoneName, Positions, Rotations, Scales, true);
    Controller.CloseBracket(true);

    if (!bWrote)
    {
        return AnimMakeError(TEXT("write_failed"),
            FString::Printf(TEXT("SetBoneTrackKeys rejected the write for bone '%s'."), *BoneName.ToString()));
    }

    Sequence->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), Sequence->GetPathName());
    Data->SetStringField(TEXT("bone"), BoneName.ToString());
    Data->SetNumberField(TEXT("number_of_keys"), NumKeys);
    Data->SetBoolField(TEXT("dirty"), true);

    bool bSave = false;
    if (Arguments.IsValid())
    {
        Arguments->TryGetBoolField(TEXT("save"), bSave);
    }
    if (bSave)
    {
        const TSharedRef<FJsonObject> SaveResult = ToolSaveAsset(Arguments);
        Data->SetBoolField(TEXT("saved"), SaveResult->GetBoolField(TEXT("ok")));
    }

    return AnimMakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolSetRootMotionKeys(const TSharedPtr<FJsonObject>& Arguments)
{
    const TArray<TSharedPtr<FJsonValue>>* KeyArray = nullptr;
    if (!Arguments.IsValid() || !Arguments->TryGetArrayField(TEXT("keys"), KeyArray) || KeyArray->Num() == 0)
    {
        return AnimMakeError(TEXT("invalid_arguments"), TEXT("Missing required non-empty array field 'keys'."));
    }

    int32 AppliedKeys = 0;
    auto Mutate = [KeyArray, &AppliedKeys](int32 NumKeys, TArray<FVector3f>& Positions, TArray<FQuat4f>& Rotations, FString& OutError) -> bool
    {
        for (const TSharedPtr<FJsonValue>& Entry : *KeyArray)
        {
            const TSharedPtr<FJsonObject>* KeyObject = nullptr;
            if (!Entry.IsValid() || !Entry->TryGetObject(KeyObject))
            {
                OutError = TEXT("Every entry in 'keys' must be an object.");
                return false;
            }

            int32 Frame = INDEX_NONE;
            if (!(*KeyObject)->TryGetNumberField(TEXT("frame"), Frame))
            {
                OutError = TEXT("Every entry in 'keys' needs a 'frame' number.");
                return false;
            }
            if (Frame < 0 || Frame >= NumKeys)
            {
                OutError = FString::Printf(TEXT("Frame %d is outside the sequence range 0..%d."), Frame, NumKeys - 1);
                return false;
            }

            // Unlisted channels keep their current value, so a caller can move Z alone.
            double Value = 0.0;
            if ((*KeyObject)->TryGetNumberField(TEXT("x"), Value)) { Positions[Frame].X = static_cast<float>(Value); }
            if ((*KeyObject)->TryGetNumberField(TEXT("y"), Value)) { Positions[Frame].Y = static_cast<float>(Value); }
            if ((*KeyObject)->TryGetNumberField(TEXT("z"), Value)) { Positions[Frame].Z = static_cast<float>(Value); }

            FRotator3f Rotator = Rotations[Frame].Rotator();
            bool bRotationTouched = false;
            if ((*KeyObject)->TryGetNumberField(TEXT("pitch"), Value)) { Rotator.Pitch = static_cast<float>(Value); bRotationTouched = true; }
            if ((*KeyObject)->TryGetNumberField(TEXT("yaw"), Value)) { Rotator.Yaw = static_cast<float>(Value); bRotationTouched = true; }
            if ((*KeyObject)->TryGetNumberField(TEXT("roll"), Value)) { Rotator.Roll = static_cast<float>(Value); bRotationTouched = true; }
            if (bRotationTouched)
            {
                Rotations[Frame] = FQuat4f(Rotator);
                Rotations[Frame].Normalize();
            }

            ++AppliedKeys;
        }
        return true;
    };

    TSharedRef<FJsonObject> Result = WriteRootMotionTrack(Arguments, Mutate, TEXT("Set root motion keys"));
    if (Result->GetBoolField(TEXT("ok")))
    {
        Result->GetObjectField(TEXT("data"))->SetNumberField(TEXT("applied_keys"), AppliedKeys);
    }
    return Result;
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolFlattenRootMotionAxis(const TSharedPtr<FJsonObject>& Arguments)
{
    FString Axis = TEXT("z");
    double TargetValue = 0.0;
    int32 StartFrame = 0;
    int32 BlendFrames = 0;
    int32 EndFrame = INDEX_NONE;
    if (Arguments.IsValid())
    {
        Arguments->TryGetStringField(TEXT("axis"), Axis);
        Arguments->TryGetNumberField(TEXT("value"), TargetValue);
        Arguments->TryGetNumberField(TEXT("start_frame"), StartFrame);
        Arguments->TryGetNumberField(TEXT("end_frame"), EndFrame);
        Arguments->TryGetNumberField(TEXT("blend_frames"), BlendFrames);
    }

    Axis = Axis.ToLower();
    if (Axis != TEXT("x") && Axis != TEXT("y") && Axis != TEXT("z"))
    {
        return AnimMakeError(TEXT("invalid_arguments"), TEXT("'axis' must be one of x, y, z."));
    }

    int32 TouchedKeys = 0;
    auto Mutate = [&Axis, TargetValue, StartFrame, EndFrame, BlendFrames, &TouchedKeys]
        (int32 NumKeys, TArray<FVector3f>& Positions, TArray<FQuat4f>& Rotations, FString& OutError) -> bool
    {
        const int32 First = FMath::Clamp(StartFrame, 0, NumKeys - 1);
        const int32 Last = EndFrame == INDEX_NONE ? NumKeys - 1 : FMath::Clamp(EndFrame, First, NumKeys - 1);
        const int32 Blend = FMath::Clamp(BlendFrames, 0, Last - First);

        for (int32 KeyIndex = First; KeyIndex <= Last; ++KeyIndex)
        {
            // Ease from the authored value into the target so the flattened tail does not pop.
            const float Alpha = Blend > 0 && KeyIndex - First < Blend
                ? static_cast<float>(KeyIndex - First) / static_cast<float>(Blend)
                : 1.0f;

            float& Channel = Axis == TEXT("x") ? Positions[KeyIndex].X
                           : Axis == TEXT("y") ? Positions[KeyIndex].Y
                                               : Positions[KeyIndex].Z;
            Channel = FMath::Lerp(Channel, static_cast<float>(TargetValue), Alpha);
            ++TouchedKeys;
        }
        return true;
    };

    TSharedRef<FJsonObject> Result = WriteRootMotionTrack(Arguments, Mutate, TEXT("Flatten root motion axis"));
    if (Result->GetBoolField(TEXT("ok")))
    {
        const TSharedPtr<FJsonObject> Data = Result->GetObjectField(TEXT("data"));
        Data->SetStringField(TEXT("axis"), Axis);
        Data->SetNumberField(TEXT("value"), TargetValue);
        Data->SetNumberField(TEXT("touched_keys"), TouchedKeys);
    }
    return Result;
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolGetBoneComponentTransform(const TSharedPtr<FJsonObject>& Arguments)
{
    FString Error;
    UAnimSequence* Sequence = AnimLoadSequence(Arguments, Error);
    if (!Sequence)
    {
        return AnimMakeError(TEXT("load_failed"), Error);
    }

    const IAnimationDataModel* Model = Sequence->GetDataModel();
    const USkeleton* Skeleton = Sequence->GetSkeleton();
    if (!Model || !Skeleton)
    {
        return AnimMakeError(TEXT("unsupported_animation_asset"), TEXT("Sequence has no animation data model or skeleton."));
    }

    const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();

    // Accept either a single "bone" or a "bones" array; without either, report every bone would
    // be far too much output, so the root bone stands in as the default.
    TArray<FString> RequestedBones;
    const TArray<TSharedPtr<FJsonValue>>* BoneArray = nullptr;
    if (Arguments.IsValid() && Arguments->TryGetArrayField(TEXT("bones"), BoneArray))
    {
        for (const TSharedPtr<FJsonValue>& Entry : *BoneArray)
        {
            FString BoneName;
            if (Entry.IsValid() && Entry->TryGetString(BoneName) && !BoneName.IsEmpty())
            {
                RequestedBones.Add(BoneName);
            }
        }
    }
    if (RequestedBones.Num() == 0)
    {
        FName SingleBone;
        if (!AnimResolveBoneName(Sequence, Arguments, SingleBone, Error))
        {
            return AnimMakeError(TEXT("invalid_arguments"), Error);
        }
        RequestedBones.Add(SingleBone.ToString());
    }

    TArray<int32> BoneIndices;
    for (const FString& BoneName : RequestedBones)
    {
        const int32 BoneIndex = RefSkeleton.FindBoneIndex(FName(*BoneName));
        if (BoneIndex == INDEX_NONE)
        {
            return AnimMakeError(TEXT("bone_not_found"),
                FString::Printf(TEXT("Skeleton has no bone named '%s'."), *BoneName));
        }
        BoneIndices.Add(BoneIndex);
    }

    TArray<FName> TrackNameArray;
    Model->GetBoneTrackNames(TrackNameArray);
    const TSet<FName> TrackNames{ TrackNameArray };

    const int32 NumKeys = Model->GetNumberOfKeys();
    int32 StartFrame = 0;
    int32 EndFrame = NumKeys - 1;
    int32 MaxKeys = 256;
    if (Arguments.IsValid())
    {
        Arguments->TryGetNumberField(TEXT("start_frame"), StartFrame);
        Arguments->TryGetNumberField(TEXT("end_frame"), EndFrame);
        Arguments->TryGetNumberField(TEXT("max_keys"), MaxKeys);
    }
    StartFrame = FMath::Clamp(StartFrame, 0, FMath::Max(0, NumKeys - 1));
    EndFrame = FMath::Clamp(EndFrame, StartFrame, FMath::Max(0, NumKeys - 1));
    MaxKeys = FMath::Clamp(MaxKeys, 1, 4096);

    const FFrameRate FrameRate = Model->GetFrameRate();
    const int32 Requested = EndFrame - StartFrame + 1;
    const int32 Stride = FMath::Max(1, FMath::DivideAndRoundUp(Requested, MaxKeys));

    TArray<TSharedPtr<FJsonValue>> Keys;
    for (int32 KeyIndex = StartFrame; KeyIndex <= EndFrame; KeyIndex += Stride)
    {
        const FFrameNumber Frame{ KeyIndex };
        TMap<int32, FTransform> Cache;

        TSharedPtr<FJsonObject> Bones = MakeShared<FJsonObject>();
        for (int32 Index = 0; Index < BoneIndices.Num(); ++Index)
        {
            const FTransform ComponentTransform =
                AnimComponentSpaceTransform(Model, RefSkeleton, TrackNames, BoneIndices[Index], Frame, Cache);
            Bones->SetObjectField(RequestedBones[Index], AnimTransformToJson(ComponentTransform));
        }

        TSharedPtr<FJsonObject> Key = MakeShared<FJsonObject>();
        Key->SetNumberField(TEXT("frame"), KeyIndex);
        Key->SetNumberField(TEXT("time"), FrameRate.AsInterval() * KeyIndex);
        Key->SetObjectField(TEXT("bones"), Bones);
        Keys.Add(MakeShared<FJsonValueObject>(Key));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), Sequence->GetPathName());
    Data->SetStringField(TEXT("space"), TEXT("component"));
    Data->SetNumberField(TEXT("number_of_keys"), NumKeys);
    Data->SetNumberField(TEXT("start_frame"), StartFrame);
    Data->SetNumberField(TEXT("end_frame"), EndFrame);
    Data->SetNumberField(TEXT("stride"), Stride);
    Data->SetArrayField(TEXT("keys"), Keys);
    return AnimMakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolListSkeletonBones(const TSharedPtr<FJsonObject>& Arguments)
{
    FString Error;
    UAnimSequence* Sequence = AnimLoadSequence(Arguments, Error);
    if (!Sequence)
    {
        return AnimMakeError(TEXT("load_failed"), Error);
    }

    const IAnimationDataModel* Model = Sequence->GetDataModel();
    const USkeleton* Skeleton = Sequence->GetSkeleton();
    if (!Model || !Skeleton)
    {
        return AnimMakeError(TEXT("unsupported_animation_asset"), TEXT("Sequence has no animation data model or skeleton."));
    }

    FString Query;
    if (Arguments.IsValid())
    {
        Arguments->TryGetStringField(TEXT("query"), Query);
    }

    TArray<FName> TrackNameArray;
    Model->GetBoneTrackNames(TrackNameArray);
    const TSet<FName> TrackNames{ TrackNameArray };

    const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
    TArray<TSharedPtr<FJsonValue>> Bones;
    for (int32 BoneIndex = 0; BoneIndex < RefSkeleton.GetNum(); ++BoneIndex)
    {
        const FName BoneName = RefSkeleton.GetBoneName(BoneIndex);
        if (!Query.IsEmpty() && !BoneName.ToString().Contains(Query))
        {
            continue;
        }

        const int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);
        TSharedPtr<FJsonObject> Bone = MakeShared<FJsonObject>();
        Bone->SetNumberField(TEXT("index"), BoneIndex);
        Bone->SetStringField(TEXT("name"), BoneName.ToString());
        Bone->SetStringField(TEXT("parent"), ParentIndex == INDEX_NONE
            ? FString()
            : RefSkeleton.GetBoneName(ParentIndex).ToString());
        Bone->SetBoolField(TEXT("has_track"), TrackNames.Contains(BoneName));
        Bones.Add(MakeShared<FJsonValueObject>(Bone));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), Sequence->GetPathName());
    Data->SetStringField(TEXT("skeleton"), Skeleton->GetPathName());
    Data->SetNumberField(TEXT("bone_count"), RefSkeleton.GetNum());
    Data->SetNumberField(TEXT("returned"), Bones.Num());
    Data->SetArrayField(TEXT("bones"), Bones);
    return AnimMakeOk(Data);
}

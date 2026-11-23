// Level Sequence authoring for the bridge.
//
// Own translation unit, same reason as the animation tools: LLMBlueprintBridgeServer.cpp is
// already oversized. Sequences are built through the MovieScene API (spawnable bindings plus
// skeletal animation sections) rather than by writing asset bytes.

#include "LLMBlueprintBridgeServer.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceBase.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Exporters/AnimSeqExportOption.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "LevelSequence.h"
#include "Misc/PackageName.h"
#include "MovieScene.h"
#include "MovieSceneCommonHelpers.h"
#include "Sections/MovieSceneSkeletalAnimationSection.h"
#include "Tracks/MovieSceneSkeletalAnimationTrack.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Components/SceneComponent.h"
#include "MovieSceneBinding.h"
#include "MovieSceneBindingProxy.h"
#include "MovieSceneBindingReferences.h"
#include "MovieSceneObjectBindingID.h"
#include "MovieScenePossessable.h"
#include "MovieSceneSection.h"
#include "MovieSceneSequence.h"
#include "MovieSceneSpawnable.h"
#include "MovieSceneTrack.h"
#include "ScopedTransaction.h"
#include "SequencerTools.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/UnrealType.h"

namespace
{
    TSharedRef<FJsonObject> SeqMakeError(const FString& Code, const FString& Message)
    {
        TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
        Root->SetBoolField(TEXT("ok"), false);
        Root->SetStringField(TEXT("code"), Code);
        Root->SetStringField(TEXT("message"), Message);
        return Root;
    }

    TSharedRef<FJsonObject> SeqMakeOk(const TSharedPtr<FJsonObject>& Data)
    {
        TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
        Root->SetBoolField(TEXT("ok"), true);
        if (Data.IsValid())
        {
            Root->SetObjectField(TEXT("data"), Data);
        }
        return Root;
    }

    FString SeqObjectPath(FString PackagePath)
    {
        PackagePath.TrimStartAndEndInline();
        if (PackagePath.Contains(TEXT(".")))
        {
            return PackagePath;
        }
        const FString AssetName = FPackageName::GetLongPackageAssetName(PackagePath);
        return AssetName.IsEmpty() ? PackagePath : FString::Printf(TEXT("%s.%s"), *PackagePath, *AssetName);
    }

    /** Accepts a Blueprint asset path or a native class path and hands back the class to spawn. */
    UClass* SeqResolveSpawnClass(const FString& Path)
    {
        if (const UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *SeqObjectPath(Path)))
        {
            return Blueprint->GeneratedClass;
        }
        return LoadObject<UClass>(nullptr, *Path);
    }

    /**
     * ULevelSequence::CreateSpawnable is protected and the engine's own path runs through a live
     * Sequencer's spawn register, so the template is built here the same way the engine does.
     *
     * It has to come from a real spawned actor: NewObject on the class alone produces a bare
     * actor whose Blueprint SCS components were never instantiated, which binds and animates but
     * draws nothing. The temporary actor is transient and destroyed straight after copying.
     */
    FGuid SeqAddSpawnable(UMovieScene* MovieScene, UClass* SpawnClass, const FString& SpawnName, FString& OutError)
    {
        if (!SpawnClass->IsChildOf(AActor::StaticClass()))
        {
            OutError = FString::Printf(TEXT("'%s' is not an Actor class, so it cannot be spawned in a sequence."), *SpawnClass->GetName());
            return FGuid();
        }

        UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
        if (!EditorWorld)
        {
            OutError = TEXT("No editor world is available to build the spawnable template from.");
            return FGuid();
        }

        FActorSpawnParameters SpawnParams;
        SpawnParams.ObjectFlags = RF_Transient;
        SpawnParams.bNoFail = true;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

        AActor* TemporaryActor = EditorWorld->SpawnActor<AActor>(SpawnClass, FTransform::Identity, SpawnParams);
        if (!TemporaryActor)
        {
            OutError = FString::Printf(TEXT("Could not spawn a temporary '%s' to build the template from."), *SpawnClass->GetName());
            return FGuid();
        }

        const FName TemplateName = MakeUniqueObjectName(MovieScene, SpawnClass, FName(*SpawnName));
        UObject* ObjectTemplate =
            MovieSceneHelpers::MakeSpawnableTemplateFromInstance(*TemporaryActor, MovieScene, TemplateName);

        TemporaryActor->Destroy();

        if (!ObjectTemplate)
        {
            OutError = TEXT("MakeSpawnableTemplateFromInstance returned nothing.");
            return FGuid();
        }
        return MovieScene->AddSpawnable(SpawnName, *ObjectTemplate);
    }
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolCreateLevelSequence(const TSharedPtr<FJsonObject>& Arguments)
{
    FString PackagePath;
    if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("package_path"), PackagePath) || PackagePath.IsEmpty())
    {
        return SeqMakeError(TEXT("invalid_arguments"), TEXT("Missing required string field 'package_path'."));
    }
    if (!PackagePath.StartsWith(TEXT("/Game/")))
    {
        return SeqMakeError(TEXT("invalid_arguments"), TEXT("Level sequences may only be created under /Game."));
    }
    if (FPackageName::DoesPackageExist(PackagePath))
    {
        return SeqMakeError(TEXT("already_exists"),
            FString::Printf(TEXT("A package already exists at '%s'."), *PackagePath));
    }

    const TArray<TSharedPtr<FJsonValue>>* TrackArray = nullptr;
    if (!Arguments->TryGetArrayField(TEXT("tracks"), TrackArray) || TrackArray->Num() == 0)
    {
        return SeqMakeError(TEXT("invalid_arguments"), TEXT("Missing required non-empty array field 'tracks'."));
    }

    double DisplayRate = 30.0;
    Arguments->TryGetNumberField(TEXT("display_rate"), DisplayRate);
    if (DisplayRate <= 0.0)
    {
        return SeqMakeError(TEXT("invalid_arguments"), TEXT("'display_rate' must be positive."));
    }

    const FString AssetName = FPackageName::GetLongPackageAssetName(PackagePath);
    UPackage* Package = CreatePackage(*PackagePath);
    if (!Package)
    {
        return SeqMakeError(TEXT("create_failed"), FString::Printf(TEXT("Could not create package '%s'."), *PackagePath));
    }

    ULevelSequence* Sequence = NewObject<ULevelSequence>(
        Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
    if (!Sequence)
    {
        return SeqMakeError(TEXT("create_failed"), TEXT("Could not create the level sequence object."));
    }
    Sequence->Initialize();

    UMovieScene* MovieScene = Sequence->GetMovieScene();
    if (!MovieScene)
    {
        return SeqMakeError(TEXT("create_failed"), TEXT("Level sequence has no movie scene."));
    }
    MovieScene->SetDisplayRate(FFrameRate(FMath::RoundToInt(DisplayRate), 1));

    const FFrameRate TickResolution = MovieScene->GetTickResolution();
    int32 LongestDuration = 0;
    TArray<TSharedPtr<FJsonValue>> Added;

    for (const TSharedPtr<FJsonValue>& Entry : *TrackArray)
    {
        const TSharedPtr<FJsonObject>* TrackObject = nullptr;
        if (!Entry.IsValid() || !Entry->TryGetObject(TrackObject))
        {
            return SeqMakeError(TEXT("invalid_arguments"), TEXT("Every entry in 'tracks' must be an object."));
        }

        FString SpawnablePath;
        if (!(*TrackObject)->TryGetStringField(TEXT("spawnable"), SpawnablePath) || SpawnablePath.IsEmpty())
        {
            return SeqMakeError(TEXT("invalid_arguments"), TEXT("Every track needs a 'spawnable' asset or class path."));
        }

        UClass* SpawnClass = SeqResolveSpawnClass(SpawnablePath);
        if (!SpawnClass)
        {
            return SeqMakeError(TEXT("load_failed"),
                FString::Printf(TEXT("Could not load a Blueprint or class at '%s'."), *SpawnablePath));
        }

        FString SpawnError;
        const FGuid BindingGuid = SeqAddSpawnable(MovieScene, SpawnClass, SpawnClass->GetName(), SpawnError);
        if (!BindingGuid.IsValid())
        {
            return SeqMakeError(TEXT("create_failed"),
                FString::Printf(TEXT("'%s' could not be made spawnable: %s"), *SpawnablePath, *SpawnError));
        }

        TSharedPtr<FJsonObject> AddedEntry = MakeShared<FJsonObject>();
        AddedEntry->SetStringField(TEXT("spawnable"), SpawnClass->GetPathName());
        AddedEntry->SetStringField(TEXT("binding_id"), BindingGuid.ToString());

        FString AnimationPath;
        if ((*TrackObject)->TryGetStringField(TEXT("animation"), AnimationPath) && !AnimationPath.IsEmpty())
        {
            UAnimSequenceBase* Animation = LoadObject<UAnimSequenceBase>(nullptr, *SeqObjectPath(AnimationPath));
            if (!Animation)
            {
                return SeqMakeError(TEXT("load_failed"),
                    FString::Printf(TEXT("Could not load an animation at '%s'."), *AnimationPath));
            }

            UMovieSceneSkeletalAnimationTrack* Track =
                MovieScene->AddTrack<UMovieSceneSkeletalAnimationTrack>(BindingGuid);
            if (!Track)
            {
                return SeqMakeError(TEXT("create_failed"), TEXT("Could not add a skeletal animation track."));
            }

            int32 StartFrame = 0;
            (*TrackObject)->TryGetNumberField(TEXT("start_frame"), StartFrame);

            // Section ranges live in tick resolution, while start_frame is authored in display
            // frames, so the caller's frame number has to be converted before placement.
            const FFrameNumber StartTick =
                FFrameRate::TransformTime(FFrameTime(StartFrame), MovieScene->GetDisplayRate(), TickResolution).FrameNumber;
            const int32 Duration = TickResolution.AsFrameNumber(Animation->GetPlayLength()).Value;

            UMovieSceneSkeletalAnimationSection* Section =
                NewObject<UMovieSceneSkeletalAnimationSection>(Track, NAME_None, RF_Transactional);
            Section->Params.Animation = Animation;
            Section->InitialPlacement(Track->GetAllSections(), StartTick, Duration, false);
            Track->AddSection(*Section);

            LongestDuration = FMath::Max(LongestDuration, StartTick.Value + Duration);

            AddedEntry->SetStringField(TEXT("animation"), Animation->GetPathName());
            AddedEntry->SetNumberField(TEXT("start_frame"), StartFrame);
            AddedEntry->SetNumberField(TEXT("play_length"), Animation->GetPlayLength());
        }

        Added.Add(MakeShared<FJsonValueObject>(AddedEntry));
    }

    if (LongestDuration > 0)
    {
        MovieScene->SetPlaybackRange(FFrameNumber(0), LongestDuration);
    }

    FAssetRegistryModule::AssetCreated(Sequence);
    Package->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), Sequence->GetPathName());
    Data->SetStringField(TEXT("package"), PackagePath);
    Data->SetNumberField(TEXT("display_rate"), MovieScene->GetDisplayRate().AsDecimal());
    Data->SetNumberField(TEXT("tick_resolution"), TickResolution.AsDecimal());
    Data->SetNumberField(TEXT("playback_end_tick"), LongestDuration);
    Data->SetArrayField(TEXT("tracks"), Added);
    Data->SetBoolField(TEXT("dirty"), true);

    bool bSave = false;
    Arguments->TryGetBoolField(TEXT("save"), bSave);
    if (bSave)
    {
        TSharedPtr<FJsonObject> SaveArgs = MakeShared<FJsonObject>();
        SaveArgs->SetStringField(TEXT("asset_path"), Sequence->GetPathName());
        const TSharedRef<FJsonObject> SaveResult = ToolSaveAsset(SaveArgs);
        Data->SetBoolField(TEXT("saved"), SaveResult->GetBoolField(TEXT("ok")));
    }

    return SeqMakeOk(Data);
}

namespace
{
    /** Display-frame number for a tick-resolution time, so callers read the frames Sequencer shows. */
    double SeqTickToDisplay(const FFrameNumber Tick, const FFrameRate TickResolution, const FFrameRate DisplayRate)
    {
        return FFrameRate::TransformTime(FFrameTime(Tick), TickResolution, DisplayRate).AsDecimal();
    }

    TSharedPtr<FJsonObject> SeqTransformToJson(const FTransform& Transform)
    {
        const FVector Location = Transform.GetLocation();
        const FRotator Rotation = Transform.GetRotation().Rotator();
        const FVector Scale = Transform.GetScale3D();

        TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
        Object->SetNumberField(TEXT("x"), Location.X);
        Object->SetNumberField(TEXT("y"), Location.Y);
        Object->SetNumberField(TEXT("z"), Location.Z);
        Object->SetNumberField(TEXT("pitch"), Rotation.Pitch);
        Object->SetNumberField(TEXT("yaw"), Rotation.Yaw);
        Object->SetNumberField(TEXT("roll"), Rotation.Roll);
        Object->SetNumberField(TEXT("scale_x"), Scale.X);
        Object->SetNumberField(TEXT("scale_y"), Scale.Y);
        Object->SetNumberField(TEXT("scale_z"), Scale.Z);
        return Object;
    }

    /**
     * Where the sequence has a binding's preview actor standing.
     *
     * Read by reflection off the custom binding rather than by casting to
     * UMovieSceneSpawnableActorBinding: that class is MinimalAPI, so MovieSceneTracks does not
     * export the reflection registration a Cast needs and the module fails to link - and it fails
     * only once a unity blob puts the right files together, which makes it a dependency worth not
     * having. ExecutionSequenceAlignment reads the same property the same way for the same reason.
     *
     * A possessable points at an actor placed in the authoring level and carries no template, which
     * is reported as "no authored transform" rather than as an error.
     */
    bool SeqResolveTemplate(ULevelSequence* Sequence,
                            const FGuid& BindingGuid,
                            FTransform& OutTransform,
                            FString& OutClassPath)
    {
        // Through the base pointer on purpose: ULevelSequence overrides only the const getter,
        // which hides the non-const one the mutable binding has to be read from.
        UMovieSceneSequence* MovieSceneSequence = Sequence;
        FMovieSceneBindingReferences* BindingReferences = MovieSceneSequence->GetBindingReferences();
        if (!BindingReferences)
        {
            return false;
        }

        UObject* CustomBinding = BindingReferences->GetCustomBinding(BindingGuid, 0);
        if (!CustomBinding)
        {
            return false;
        }

        const FObjectPropertyBase* ActorTemplateProperty = CastField<FObjectPropertyBase>(
            CustomBinding->GetClass()->FindPropertyByName(TEXT("ActorTemplate")));
        if (!ActorTemplateProperty)
        {
            return false;
        }

        const AActor* TemplateActor =
            Cast<AActor>(ActorTemplateProperty->GetObjectPropertyValue_InContainer(CustomBinding));
        const USceneComponent* TemplateRoot = TemplateActor ? TemplateActor->GetRootComponent() : nullptr;
        if (!TemplateRoot)
        {
            return false;
        }

        OutClassPath = TemplateActor->GetClass()->GetPathName();

        // The relative transform, not the world one. A template actor never belongs to a world, so
        // its cached component-to-world was never built and reads back as the identity.
        OutTransform = TemplateRoot->GetRelativeTransform();
        return true;
    }

    /** Channel names a transform section carries, in the order its double channels are laid out. */
    const TCHAR* SeqTransformChannelName(const int32 Index)
    {
        static const TCHAR* Names[] = {
            TEXT("Location.X"), TEXT("Location.Y"), TEXT("Location.Z"),
            TEXT("Rotation.X"), TEXT("Rotation.Y"), TEXT("Rotation.Z"),
            TEXT("Scale.X"),    TEXT("Scale.Y"),    TEXT("Scale.Z")
        };
        return (Index >= 0 && Index < UE_ARRAY_COUNT(Names)) ? Names[Index] : TEXT("");
    }

    /**
     * Every channel on a section, evaluated at EvalTime when one was asked for.
     *
     * Read through UMovieSceneSection::GetChannelProxy rather than through the concrete section
     * classes, so a transform track, a float track and a track this tool has never heard of all
     * report the same way and none of them needs a cast into MovieSceneTracks.
     *
     * Named values are collected into OutNamedValues so the caller can rebuild a transform from the
     * nine channels a transform section carries.
     */
    void SeqDescribeChannels(UMovieSceneSection* Section,
                             const bool bHasEvalTime,
                             const FFrameTime& EvalTime,
                             const bool bIncludeKeys,
                             const int32 MaxKeys,
                             const FFrameRate TickResolution,
                             const FFrameRate DisplayRate,
                             TArray<TSharedPtr<FJsonValue>>& OutChannels,
                             TMap<FName, double>& OutNamedValues)
    {
        FMovieSceneChannelProxy& Proxy = Section->GetChannelProxy();

        for (const FMovieSceneChannelEntry& Entry : Proxy.GetAllEntries())
        {
            const FName ChannelTypeName = Entry.GetChannelTypeName();
            const TArrayView<FMovieSceneChannel* const> Channels = Entry.GetChannels();

            const bool bIsDouble = ChannelTypeName == FMovieSceneDoubleChannel::StaticStruct()->GetFName();
            const bool bIsFloat = ChannelTypeName == FMovieSceneFloatChannel::StaticStruct()->GetFName();

#if WITH_EDITORONLY_DATA
            const TArrayView<const FMovieSceneChannelMetaData> MetaData = Entry.GetMetaData();
#endif

            for (int32 ChannelIndex = 0; ChannelIndex < Channels.Num(); ++ChannelIndex)
            {
                FMovieSceneChannel* Channel = Channels[ChannelIndex];
                if (!Channel)
                {
                    continue;
                }

                FName ChannelName = NAME_None;
#if WITH_EDITORONLY_DATA
                if (MetaData.IsValidIndex(ChannelIndex))
                {
                    ChannelName = MetaData[ChannelIndex].Name;
                }
#endif
                // A transform section's nine double channels are laid out in a fixed order, so the
                // name is recoverable even where editor metadata is not compiled in.
                if (ChannelName.IsNone() && bIsDouble && Channels.Num() == 9)
                {
                    ChannelName = FName(SeqTransformChannelName(ChannelIndex));
                }

                TSharedPtr<FJsonObject> ChannelJson = MakeShared<FJsonObject>();
                ChannelJson->SetStringField(TEXT("name"), ChannelName.IsNone()
                    ? FString::Printf(TEXT("%s[%d]"), *ChannelTypeName.ToString(), ChannelIndex)
                    : ChannelName.ToString());
                ChannelJson->SetStringField(TEXT("type"), ChannelTypeName.ToString());
                ChannelJson->SetNumberField(TEXT("key_count"), Channel->GetNumKeys());

                if (bHasEvalTime && bIsDouble)
                {
                    FMovieSceneDoubleChannel* DoubleChannel = static_cast<FMovieSceneDoubleChannel*>(Channel);
                    double Value = 0.0;
                    if (DoubleChannel->Evaluate(EvalTime, Value))
                    {
                        ChannelJson->SetNumberField(TEXT("value"), Value);
                        if (!ChannelName.IsNone())
                        {
                            OutNamedValues.Add(ChannelName, Value);
                        }
                    }
                }
                else if (bHasEvalTime && bIsFloat)
                {
                    FMovieSceneFloatChannel* FloatChannel = static_cast<FMovieSceneFloatChannel*>(Channel);
                    float Value = 0.0f;
                    if (FloatChannel->Evaluate(EvalTime, Value))
                    {
                        ChannelJson->SetNumberField(TEXT("value"), Value);
                        if (!ChannelName.IsNone())
                        {
                            OutNamedValues.Add(ChannelName, Value);
                        }
                    }
                }

                if (bIncludeKeys && (bIsDouble || bIsFloat))
                {
                    TArray<TSharedPtr<FJsonValue>> Keys;
                    TArrayView<const FFrameNumber> Times;
                    TArray<double> Values;

                    if (bIsDouble)
                    {
                        FMovieSceneDoubleChannel* DoubleChannel = static_cast<FMovieSceneDoubleChannel*>(Channel);
                        TMovieSceneChannelData<FMovieSceneDoubleValue> Data = DoubleChannel->GetData();
                        Times = Data.GetTimes();
                        for (const FMovieSceneDoubleValue& Key : Data.GetValues())
                        {
                            Values.Add(Key.Value);
                        }
                    }
                    else
                    {
                        FMovieSceneFloatChannel* FloatChannel = static_cast<FMovieSceneFloatChannel*>(Channel);
                        TMovieSceneChannelData<FMovieSceneFloatValue> Data = FloatChannel->GetData();
                        Times = Data.GetTimes();
                        for (const FMovieSceneFloatValue& Key : Data.GetValues())
                        {
                            Values.Add(Key.Value);
                        }
                    }

                    const int32 KeyCount = FMath::Min(Times.Num(), Values.Num());
                    // Decimated rather than truncated: a sampled shape of the whole channel is more
                    // use than its first N keys when a channel is denser than the cap.
                    const int32 Stride = (MaxKeys > 0 && KeyCount > MaxKeys)
                        ? FMath::DivideAndRoundUp(KeyCount, MaxKeys)
                        : 1;

                    for (int32 KeyIndex = 0; KeyIndex < KeyCount; KeyIndex += Stride)
                    {
                        TSharedPtr<FJsonObject> KeyJson = MakeShared<FJsonObject>();
                        KeyJson->SetNumberField(TEXT("frame"),
                            SeqTickToDisplay(Times[KeyIndex], TickResolution, DisplayRate));
                        KeyJson->SetNumberField(TEXT("tick"), Times[KeyIndex].Value);
                        KeyJson->SetNumberField(TEXT("value"), Values[KeyIndex]);
                        Keys.Add(MakeShared<FJsonValueObject>(KeyJson));
                    }

                    if (Stride > 1)
                    {
                        ChannelJson->SetNumberField(TEXT("stride"), Stride);
                    }
                    ChannelJson->SetArrayField(TEXT("keys"), Keys);
                }

                OutChannels.Add(MakeShared<FJsonValueObject>(ChannelJson));
            }
        }
    }

    /** The transform a section's nine named channels describe, when all nine evaluated. */
    bool SeqTransformFromChannels(const TMap<FName, double>& Values, FTransform& OutTransform)
    {
        const double* LocationX = Values.Find(TEXT("Location.X"));
        const double* LocationY = Values.Find(TEXT("Location.Y"));
        const double* LocationZ = Values.Find(TEXT("Location.Z"));
        const double* RotationX = Values.Find(TEXT("Rotation.X"));
        const double* RotationY = Values.Find(TEXT("Rotation.Y"));
        const double* RotationZ = Values.Find(TEXT("Rotation.Z"));
        if (!LocationX || !LocationY || !LocationZ || !RotationX || !RotationY || !RotationZ)
        {
            return false;
        }

        const double* ScaleX = Values.Find(TEXT("Scale.X"));
        const double* ScaleY = Values.Find(TEXT("Scale.Y"));
        const double* ScaleZ = Values.Find(TEXT("Scale.Z"));

        // Sequencer stores a rotator as X=Roll, Y=Pitch, Z=Yaw, which is not the order the FRotator
        // constructor takes - reading the channels positionally would swap pitch and roll.
        OutTransform = FTransform(
            FRotator(*RotationY, *RotationZ, *RotationX),
            FVector(*LocationX, *LocationY, *LocationZ),
            FVector(ScaleX ? *ScaleX : 1.0, ScaleY ? *ScaleY : 1.0, ScaleZ ? *ScaleZ : 1.0));
        return true;
    }

    /**
     * The binding an attach (or other 3D constraint) section follows, when the section has one.
     *
     * Read by reflection for the same MinimalAPI reason as SeqResolveTemplate: the constraint
     * section classes live in MovieSceneTracks and a Cast to them is a link dependency not worth having.
     */
    bool SeqGetConstraintBindingGuid(const UMovieSceneSection* Section, FGuid& OutGuid)
    {
        const FStructProperty* ConstraintProperty = CastField<FStructProperty>(
            Section->GetClass()->FindPropertyByName(TEXT("ConstraintBindingID")));
        if (!ConstraintProperty || ConstraintProperty->Struct != FMovieSceneObjectBindingID::StaticStruct())
        {
            return false;
        }
        OutGuid = ConstraintProperty->ContainerPtrToValuePtr<FMovieSceneObjectBindingID>(Section)->GetGuid();
        return true;
    }

    void SeqDescribeConstraint(const UMovieSceneSection* Section, const TSharedPtr<FJsonObject>& SectionJson)
    {
        FGuid ConstraintGuid;
        if (!SeqGetConstraintBindingGuid(Section, ConstraintGuid))
        {
            return;
        }
        SectionJson->SetStringField(TEXT("constraint_binding_id"), ConstraintGuid.ToString());

        const auto ReadName = [Section](const TCHAR* PropertyName, FName& OutName)
        {
            const FNameProperty* NameProperty = CastField<FNameProperty>(Section->GetClass()->FindPropertyByName(PropertyName));
            if (NameProperty)
            {
                OutName = NameProperty->GetPropertyValue_InContainer(Section);
            }
            return NameProperty != nullptr;
        };

        FName SocketName;
        if (ReadName(TEXT("AttachSocketName"), SocketName))
        {
            SectionJson->SetStringField(TEXT("attach_socket"), SocketName.ToString());
        }
        FName ComponentName;
        if (ReadName(TEXT("AttachComponentName"), ComponentName))
        {
            SectionJson->SetStringField(TEXT("attach_component"), ComponentName.ToString());
        }
    }

    /** One track and every section on it. */
    TSharedPtr<FJsonObject> SeqDescribeTrack(UMovieSceneTrack* Track,
                                             const bool bHasEvalTime,
                                             const FFrameTime& EvalTick,
                                             const bool bIncludeKeys,
                                             const int32 MaxKeys,
                                             const FFrameRate TickResolution,
                                             const FFrameRate DisplayRate,
                                             FTransform& OutEvaluatedTransform,
                                             bool& bOutHasEvaluatedTransform)
    {
        TSharedPtr<FJsonObject> TrackJson = MakeShared<FJsonObject>();
        TrackJson->SetStringField(TEXT("type"), Track->GetClass()->GetName());

        TArray<TSharedPtr<FJsonValue>> Sections;
        for (UMovieSceneSection* Section : Track->GetAllSections())
        {
            if (!Section)
            {
                continue;
            }

            TSharedPtr<FJsonObject> SectionJson = MakeShared<FJsonObject>();
            SectionJson->SetStringField(TEXT("type"), Section->GetClass()->GetName());

            const TRange<FFrameNumber> Range = Section->GetRange();
            if (Range.HasLowerBound())
            {
                SectionJson->SetNumberField(TEXT("start_frame"),
                    SeqTickToDisplay(Range.GetLowerBoundValue(), TickResolution, DisplayRate));
            }
            if (Range.HasUpperBound())
            {
                SectionJson->SetNumberField(TEXT("end_frame"),
                    SeqTickToDisplay(Range.GetUpperBoundValue(), TickResolution, DisplayRate));
            }

            if (const UMovieSceneSkeletalAnimationSection* AnimSection =
                    Cast<UMovieSceneSkeletalAnimationSection>(Section))
            {
                if (const UAnimSequenceBase* Animation = AnimSection->Params.Animation)
                {
                    SectionJson->SetStringField(TEXT("animation"), Animation->GetPathName());
                    SectionJson->SetNumberField(TEXT("animation_length"), Animation->GetPlayLength());
                }
                SectionJson->SetStringField(TEXT("slot_name"), AnimSection->Params.SlotName.ToString());
            }

            SeqDescribeConstraint(Section, SectionJson);

            // Only a section the requested frame actually falls inside is evaluated; one that has
            // already ended would otherwise report its last value as though it still drove the pose.
            const bool bEvaluateThisSection = bHasEvalTime && Range.Contains(EvalTick.FrameNumber);

            TArray<TSharedPtr<FJsonValue>> Channels;
            TMap<FName, double> NamedValues;
            SeqDescribeChannels(Section,
                                bEvaluateThisSection,
                                EvalTick,
                                bIncludeKeys,
                                MaxKeys,
                                TickResolution,
                                DisplayRate,
                                Channels,
                                NamedValues);
            SectionJson->SetArrayField(TEXT("channels"), Channels);

            if (bEvaluateThisSection)
            {
                FTransform SectionTransform;
                if (SeqTransformFromChannels(NamedValues, SectionTransform))
                {
                    SectionJson->SetObjectField(TEXT("evaluated_transform"),
                                                SeqTransformToJson(SectionTransform));
                    OutEvaluatedTransform = SectionTransform;
                    bOutHasEvaluatedTransform = true;
                }
            }

            Sections.Add(MakeShared<FJsonValueObject>(SectionJson));
        }

        TrackJson->SetArrayField(TEXT("sections"), Sections);
        return TrackJson;
    }
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolInspectLevelSequence(const TSharedPtr<FJsonObject>& Arguments)
{
    FString AssetPath;
    if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
    {
        return SeqMakeError(TEXT("invalid_arguments"), TEXT("Missing required string field 'asset_path'."));
    }

    ULevelSequence* Sequence = LoadObject<ULevelSequence>(nullptr, *SeqObjectPath(AssetPath));
    if (!Sequence)
    {
        return SeqMakeError(TEXT("load_failed"),
            FString::Printf(TEXT("Could not load a level sequence at '%s'."), *AssetPath));
    }

    UMovieScene* MovieScene = Sequence->GetMovieScene();
    if (!MovieScene)
    {
        return SeqMakeError(TEXT("load_failed"), TEXT("Level sequence has no movie scene."));
    }

    const FFrameRate DisplayRate = MovieScene->GetDisplayRate();
    const FFrameRate TickResolution = MovieScene->GetTickResolution();

    bool bIncludeKeys = false;
    Arguments->TryGetBoolField(TEXT("include_keys"), bIncludeKeys);

    int32 MaxKeys = 128;
    Arguments->TryGetNumberField(TEXT("max_keys"), MaxKeys);

    // at_frame is authored in the display frames Sequencer shows, while every range and channel in
    // the movie scene lives in tick resolution, so it is converted once here.
    double AtFrame = 0.0;
    bool bHasEvalTime = Arguments->TryGetNumberField(TEXT("at_frame"), AtFrame);
    if (!bHasEvalTime)
    {
        // Also accepted as a numeric string. The generated schema types an optional argument that
        // carries no default as a string, so a caller following that schema sends "29", not 29.
        FString AtFrameText;
        if (Arguments->TryGetStringField(TEXT("at_frame"), AtFrameText) && AtFrameText.IsNumeric())
        {
            AtFrame = FCString::Atod(*AtFrameText);
            bHasEvalTime = true;
        }
    }
    const FFrameTime EvalTick = bHasEvalTime
        ? FFrameRate::TransformTime(FFrameTime(FFrameNumber(FMath::RoundToInt32(AtFrame))), DisplayRate, TickResolution)
        : FFrameTime();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), Sequence->GetPathName());
    Data->SetStringField(TEXT("name"), Sequence->GetName());
    Data->SetNumberField(TEXT("display_rate"), DisplayRate.AsDecimal());
    Data->SetNumberField(TEXT("tick_resolution"), TickResolution.AsDecimal());

    const TRange<FFrameNumber> PlaybackRange = MovieScene->GetPlaybackRange();
    if (PlaybackRange.HasLowerBound())
    {
        Data->SetNumberField(TEXT("playback_start"),
            SeqTickToDisplay(PlaybackRange.GetLowerBoundValue(), TickResolution, DisplayRate));
    }
    if (PlaybackRange.HasUpperBound())
    {
        Data->SetNumberField(TEXT("playback_end"),
            SeqTickToDisplay(PlaybackRange.GetUpperBoundValue(), TickResolution, DisplayRate));
    }
    if (bHasEvalTime)
    {
        Data->SetNumberField(TEXT("at_frame"), AtFrame);
    }

    const UMovieScene* ConstMovieScene = MovieScene;
    TArray<TSharedPtr<FJsonValue>> Bindings;

    for (const FMovieSceneBinding& Binding : ConstMovieScene->GetBindings())
    {
        const FGuid& BindingGuid = Binding.GetObjectGuid();

        TSharedPtr<FJsonObject> BindingJson = MakeShared<FJsonObject>();
        BindingJson->SetStringField(TEXT("binding_id"), BindingGuid.ToString());

        FString AuthoredName;
        FString Kind = TEXT("unknown");
        if (const FMovieScenePossessable* Possessable = MovieScene->FindPossessable(BindingGuid))
        {
            AuthoredName = Possessable->GetName();
            Kind = TEXT("possessable");
            if (const UClass* PossessedClass = Possessable->GetPossessedObjectClass())
            {
                BindingJson->SetStringField(TEXT("class"), PossessedClass->GetPathName());
            }
            if (Possessable->GetParent().IsValid())
            {
                BindingJson->SetStringField(TEXT("parent_binding_id"), Possessable->GetParent().ToString());
            }
        }
        else if (const FMovieSceneSpawnable* Spawnable = MovieScene->FindSpawnable(BindingGuid))
        {
            AuthoredName = Spawnable->GetName();
            Kind = TEXT("spawnable");
        }
#if WITH_EDITORONLY_DATA
        // The display name, not FMovieSceneBinding::GetName, which is deprecated in 5.7. This is not
        // a rare fallback: a 5.7 spawnable lives in the custom binding system, where FindSpawnable
        // returns nothing, so the display name is the only name such a binding has.
        if (AuthoredName.IsEmpty())
        {
            AuthoredName = MovieScene->GetObjectDisplayName(BindingGuid).ToString();
        }
#endif
        BindingJson->SetStringField(TEXT("name"), AuthoredName);
        BindingJson->SetStringField(TEXT("kind"), Kind);

        FTransform TemplateTransform;
        FString TemplateClassPath;
        const bool bHasTemplate = SeqResolveTemplate(Sequence, BindingGuid, TemplateTransform, TemplateClassPath);
        if (bHasTemplate)
        {
            BindingJson->SetObjectField(TEXT("template_transform"), SeqTransformToJson(TemplateTransform));
            BindingJson->SetStringField(TEXT("template_class"), TemplateClassPath);
            if (Kind == TEXT("unknown"))
            {
                BindingJson->SetStringField(TEXT("kind"), TEXT("spawnable"));
            }
        }

        TArray<TSharedPtr<FJsonValue>> Tracks;
        FTransform EvaluatedTransform;
        bool bHasEvaluatedTransform = false;

        for (UMovieSceneTrack* Track : Binding.GetTracks())
        {
            if (!Track)
            {
                continue;
            }
            Tracks.Add(MakeShared<FJsonValueObject>(SeqDescribeTrack(Track,
                                                                     bHasEvalTime,
                                                                     EvalTick,
                                                                     bIncludeKeys,
                                                                     MaxKeys,
                                                                     TickResolution,
                                                                     DisplayRate,
                                                                     EvaluatedTransform,
                                                                     bHasEvaluatedTransform)));
        }
        BindingJson->SetArrayField(TEXT("tracks"), Tracks);

        // A transform track wins over the template, because it is what actually drives the binding
        // at that frame. With no transform track the template is where the binding stands, reported
        // under the same key so a caller comparing two bindings does not have to branch.
        if (bHasEvalTime)
        {
            if (bHasEvaluatedTransform)
            {
                BindingJson->SetObjectField(TEXT("transform_at_frame"), SeqTransformToJson(EvaluatedTransform));
                BindingJson->SetStringField(TEXT("transform_source"), TEXT("transform_track"));
            }
            else if (bHasTemplate)
            {
                BindingJson->SetObjectField(TEXT("transform_at_frame"), SeqTransformToJson(TemplateTransform));
                BindingJson->SetStringField(TEXT("transform_source"), TEXT("template"));
            }
            else
            {
                BindingJson->SetStringField(TEXT("transform_source"), TEXT("none"));
            }
        }

        Bindings.Add(MakeShared<FJsonValueObject>(BindingJson));
    }

    Data->SetArrayField(TEXT("bindings"), Bindings);

    // Tracks that belong to the movie scene rather than to a binding - camera cuts, fades and the
    // like. Left out, a sequence driven entirely by them would read back as empty.
    TArray<TSharedPtr<FJsonValue>> RootTracks;
    FTransform UnusedTransform;
    bool bUnusedHasTransform = false;
    for (UMovieSceneTrack* Track : ConstMovieScene->GetTracks())
    {
        if (!Track)
        {
            continue;
        }
        RootTracks.Add(MakeShared<FJsonValueObject>(SeqDescribeTrack(Track,
                                                                     bHasEvalTime,
                                                                     EvalTick,
                                                                     bIncludeKeys,
                                                                     MaxKeys,
                                                                     TickResolution,
                                                                     DisplayRate,
                                                                     UnusedTransform,
                                                                     bUnusedHasTransform)));
    }
    Data->SetArrayField(TEXT("root_tracks"), RootTracks);

    return SeqMakeOk(Data);
}

namespace
{
    /** A binding by its GUID string or by the name inspect_level_sequence reports for it. */
    const FMovieSceneBinding* SeqFindBinding(UMovieScene* MovieScene, const FString& BindingKey)
    {
        FGuid ParsedGuid;
        const bool bIsGuid = FGuid::Parse(BindingKey, ParsedGuid);

        const UMovieScene* ConstMovieScene = MovieScene;
        for (const FMovieSceneBinding& Binding : ConstMovieScene->GetBindings())
        {
            const FGuid& BindingGuid = Binding.GetObjectGuid();
            if (bIsGuid && BindingGuid == ParsedGuid)
            {
                return &Binding;
            }

            FString Name;
            if (const FMovieScenePossessable* Possessable = MovieScene->FindPossessable(BindingGuid))
            {
                Name = Possessable->GetName();
            }
            else if (const FMovieSceneSpawnable* Spawnable = MovieScene->FindSpawnable(BindingGuid))
            {
                Name = Spawnable->GetName();
            }
#if WITH_EDITORONLY_DATA
            if (Name.IsEmpty())
            {
                Name = MovieScene->GetObjectDisplayName(BindingGuid).ToString();
            }
#endif
            if (Name == BindingKey)
            {
                return &Binding;
            }
        }
        return nullptr;
    }

    /**
     * Writes one value at one tick and reports whether a key had to be added. An existing key keeps
     * its interpolation and tangent mode and only has its value replaced, so a baked curve keeps the
     * shape Sequencer gave it.
     */
    template <typename ChannelType, typename ValueType>
    bool SeqWriteKey(ChannelType* Channel, const FFrameNumber Tick, const double Value)
    {
        auto Data = Channel->GetData();
        const int32 KeyIndex = Data.FindKey(Tick);
        if (KeyIndex != INDEX_NONE)
        {
            Data.GetValues()[KeyIndex].Value = Value;
            return false;
        }
        Data.AddKey(Tick, ValueType(Value));
        return true;
    }
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolSetLevelSequenceChannelKeys(const TSharedPtr<FJsonObject>& Arguments)
{
    FString AssetPath;
    FString BindingKey;
    FString ChannelKey;
    if (!Arguments.IsValid()
        || !Arguments->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty()
        || !Arguments->TryGetStringField(TEXT("binding"), BindingKey) || BindingKey.IsEmpty()
        || !Arguments->TryGetStringField(TEXT("channel"), ChannelKey) || ChannelKey.IsEmpty())
    {
        return SeqMakeError(TEXT("invalid_arguments"),
            TEXT("Missing required string fields 'asset_path', 'binding' and 'channel'."));
    }

    const TArray<TSharedPtr<FJsonValue>>* KeyArray = nullptr;
    if (!Arguments->TryGetArrayField(TEXT("keys"), KeyArray) || KeyArray->Num() == 0)
    {
        return SeqMakeError(TEXT("invalid_arguments"), TEXT("Missing required non-empty array field 'keys'."));
    }

    ULevelSequence* Sequence = LoadObject<ULevelSequence>(nullptr, *SeqObjectPath(AssetPath));
    if (!Sequence)
    {
        return SeqMakeError(TEXT("load_failed"),
            FString::Printf(TEXT("Could not load a level sequence at '%s'."), *AssetPath));
    }
    if (!Sequence->GetPathName().StartsWith(TEXT("/Game/")))
    {
        return SeqMakeError(TEXT("invalid_arguments"), TEXT("Level sequence writes are limited to /Game assets."));
    }

    UMovieScene* MovieScene = Sequence->GetMovieScene();
    if (!MovieScene)
    {
        return SeqMakeError(TEXT("load_failed"), TEXT("Level sequence has no movie scene."));
    }

    const FMovieSceneBinding* Binding = SeqFindBinding(MovieScene, BindingKey);
    if (!Binding)
    {
        return SeqMakeError(TEXT("binding_not_found"),
            FString::Printf(TEXT("No binding named or identified by '%s'."), *BindingKey));
    }

    // Only named channels can be addressed - editor metadata, or a transform section's fixed
    // layout - which are the same names inspect_level_sequence reports.
    UMovieSceneSection* FoundSection = nullptr;
    FMovieSceneChannel* FoundChannel = nullptr;
    bool bFoundDouble = false;
    FString FoundTrackType;

    for (UMovieSceneTrack* Track : Binding->GetTracks())
    {
        if (!Track || FoundChannel)
        {
            continue;
        }
        for (UMovieSceneSection* Section : Track->GetAllSections())
        {
            if (!Section || FoundChannel)
            {
                continue;
            }
            for (const FMovieSceneChannelEntry& Entry : Section->GetChannelProxy().GetAllEntries())
            {
                const FName ChannelTypeName = Entry.GetChannelTypeName();
                const bool bIsDouble = ChannelTypeName == FMovieSceneDoubleChannel::StaticStruct()->GetFName();
                const bool bIsFloat = ChannelTypeName == FMovieSceneFloatChannel::StaticStruct()->GetFName();
                if (!bIsDouble && !bIsFloat)
                {
                    continue;
                }

                const TArrayView<FMovieSceneChannel* const> Channels = Entry.GetChannels();
#if WITH_EDITORONLY_DATA
                const TArrayView<const FMovieSceneChannelMetaData> MetaData = Entry.GetMetaData();
#endif
                for (int32 ChannelIndex = 0; ChannelIndex < Channels.Num(); ++ChannelIndex)
                {
                    FName ChannelName = NAME_None;
#if WITH_EDITORONLY_DATA
                    if (MetaData.IsValidIndex(ChannelIndex))
                    {
                        ChannelName = MetaData[ChannelIndex].Name;
                    }
#endif
                    if (ChannelName.IsNone() && bIsDouble && Channels.Num() == 9)
                    {
                        ChannelName = FName(SeqTransformChannelName(ChannelIndex));
                    }
                    if (Channels[ChannelIndex] && ChannelName.ToString() == ChannelKey)
                    {
                        FoundSection = Section;
                        FoundChannel = Channels[ChannelIndex];
                        bFoundDouble = bIsDouble;
                        FoundTrackType = Track->GetClass()->GetName();
                        break;
                    }
                }
                if (FoundChannel)
                {
                    break;
                }
            }
        }
    }

    if (!FoundChannel)
    {
        return SeqMakeError(TEXT("channel_not_found"),
            FString::Printf(TEXT("Binding '%s' has no float or double channel named '%s'."), *BindingKey, *ChannelKey));
    }

    const FFrameRate DisplayRate = MovieScene->GetDisplayRate();
    const FFrameRate TickResolution = MovieScene->GetTickResolution();

    // Parsed up front so a bad entry rejects the whole call before anything is touched.
    TArray<TPair<FFrameNumber, double>> Writes;
    for (const TSharedPtr<FJsonValue>& Entry : *KeyArray)
    {
        const TSharedPtr<FJsonObject>* KeyObject = nullptr;
        double Frame = 0.0;
        double Value = 0.0;
        if (!Entry.IsValid() || !Entry->TryGetObject(KeyObject)
            || !(*KeyObject)->TryGetNumberField(TEXT("frame"), Frame)
            || !(*KeyObject)->TryGetNumberField(TEXT("value"), Value))
        {
            return SeqMakeError(TEXT("invalid_arguments"), TEXT("Every key needs numeric 'frame' and 'value'."));
        }
        const FFrameNumber Tick = FFrameRate::TransformTime(
            FFrameTime(FFrameNumber(FMath::RoundToInt32(Frame))), DisplayRate, TickResolution).FrameNumber;
        Writes.Emplace(Tick, Value);
    }

    FScopedTransaction Transaction(FText::FromString(TEXT("Set Level Sequence Channel Keys")));
    if (!FoundSection->TryModify())
    {
        return SeqMakeError(TEXT("write_failed"), TEXT("The section is locked or read-only."));
    }

    int32 UpdatedKeys = 0;
    int32 AddedKeys = 0;
    for (const TPair<FFrameNumber, double>& Write : Writes)
    {
        const bool bAdded = bFoundDouble
            ? SeqWriteKey<FMovieSceneDoubleChannel, FMovieSceneDoubleValue>(
                static_cast<FMovieSceneDoubleChannel*>(FoundChannel), Write.Key, Write.Value)
            : SeqWriteKey<FMovieSceneFloatChannel, FMovieSceneFloatValue>(
                static_cast<FMovieSceneFloatChannel*>(FoundChannel), Write.Key, Write.Value);
        bAdded ? ++AddedKeys : ++UpdatedKeys;
    }

    // Auto tangents depend on neighbouring values, so they are rebuilt once after every write.
    if (bFoundDouble)
    {
        static_cast<FMovieSceneDoubleChannel*>(FoundChannel)->AutoSetTangents();
    }
    else
    {
        static_cast<FMovieSceneFloatChannel*>(FoundChannel)->AutoSetTangents();
    }

    FoundSection->MarkAsChanged();
    Sequence->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), Sequence->GetPathName());
    Data->SetStringField(TEXT("binding"), Binding->GetObjectGuid().ToString());
    Data->SetStringField(TEXT("channel"), ChannelKey);
    Data->SetStringField(TEXT("track_type"), FoundTrackType);
    Data->SetNumberField(TEXT("updated_keys"), UpdatedKeys);
    Data->SetNumberField(TEXT("added_keys"), AddedKeys);
    Data->SetNumberField(TEXT("key_count"), FoundChannel->GetNumKeys());
    Data->SetBoolField(TEXT("dirty"), true);

    bool bSave = false;
    Arguments->TryGetBoolField(TEXT("save"), bSave);
    if (bSave)
    {
        TSharedPtr<FJsonObject> SaveArgs = MakeShared<FJsonObject>();
        SaveArgs->SetStringField(TEXT("asset_path"), Sequence->GetPathName());
        const TSharedRef<FJsonObject> SaveResult = ToolSaveAsset(SaveArgs);
        Data->SetBoolField(TEXT("saved"), SaveResult->GetBoolField(TEXT("ok")));
    }

    return SeqMakeOk(Data);
}

namespace
{
    /** Loads the sequence and binding every sequence write below starts from. */
    bool SeqLoadForWrite(const TSharedPtr<FJsonObject>& Arguments,
                         ULevelSequence*& OutSequence,
                         UMovieScene*& OutMovieScene,
                         const FMovieSceneBinding*& OutBinding,
                         TSharedPtr<FJsonObject>& OutError)
    {
        FString AssetPath;
        FString BindingKey;
        if (!Arguments.IsValid()
            || !Arguments->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty()
            || !Arguments->TryGetStringField(TEXT("binding"), BindingKey) || BindingKey.IsEmpty())
        {
            OutError = SeqMakeError(TEXT("invalid_arguments"),
                TEXT("Missing required string fields 'asset_path' and 'binding'."));
            return false;
        }

        OutSequence = LoadObject<ULevelSequence>(nullptr, *SeqObjectPath(AssetPath));
        if (!OutSequence)
        {
            OutError = SeqMakeError(TEXT("load_failed"),
                FString::Printf(TEXT("Could not load a level sequence at '%s'."), *AssetPath));
            return false;
        }
        if (!OutSequence->GetPathName().StartsWith(TEXT("/Game/")))
        {
            OutError = SeqMakeError(TEXT("invalid_arguments"), TEXT("Level sequence writes are limited to /Game assets."));
            return false;
        }

        OutMovieScene = OutSequence->GetMovieScene();
        if (!OutMovieScene)
        {
            OutError = SeqMakeError(TEXT("load_failed"), TEXT("Level sequence has no movie scene."));
            return false;
        }

        OutBinding = SeqFindBinding(OutMovieScene, BindingKey);
        if (!OutBinding)
        {
            OutError = SeqMakeError(TEXT("binding_not_found"),
                FString::Printf(TEXT("No binding named or identified by '%s'."), *BindingKey));
            return false;
        }
        return true;
    }

    void SeqSaveIfAsked(const TSharedPtr<FJsonObject>& Arguments,
                        const UObject* Asset,
                        const TSharedPtr<FJsonObject>& Data,
                        TFunctionRef<TSharedRef<FJsonObject>(const TSharedPtr<FJsonObject>&)> Save)
    {
        bool bSave = false;
        Arguments->TryGetBoolField(TEXT("save"), bSave);
        if (bSave)
        {
            TSharedPtr<FJsonObject> SaveArgs = MakeShared<FJsonObject>();
            SaveArgs->SetStringField(TEXT("asset_path"), Asset->GetPathName());
            const TSharedRef<FJsonObject> SaveResult = Save(SaveArgs);
            Data->SetBoolField(TEXT("saved"), SaveResult->GetBoolField(TEXT("ok")));
        }
    }
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolOpenLevelSequence(const TSharedPtr<FJsonObject>& Arguments)
{
    FString AssetPath;
    if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
    {
        return SeqMakeError(TEXT("invalid_arguments"), TEXT("Missing required string field 'asset_path'."));
    }

    ULevelSequence* Sequence = LoadObject<ULevelSequence>(nullptr, *SeqObjectPath(AssetPath));
    if (!Sequence)
    {
        return SeqMakeError(TEXT("load_failed"),
            FString::Printf(TEXT("Could not load a level sequence at '%s'."), *AssetPath));
    }

    // A Control Rig section only builds its channel proxy once a Sequencer has bound its rig, so a
    // freshly loaded sequence reports no rig channels until it has been opened.
    UAssetEditorSubsystem* AssetEditors = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
    if (!AssetEditors || !AssetEditors->OpenEditorForAsset(Sequence))
    {
        return SeqMakeError(TEXT("open_failed"), TEXT("The asset editor subsystem could not open the sequence."));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), Sequence->GetPathName());
    Data->SetBoolField(TEXT("opened"), true);
    return SeqMakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolBakeLevelSequenceAnimation(const TSharedPtr<FJsonObject>& Arguments)
{
    ULevelSequence* Sequence = nullptr;
    UMovieScene* MovieScene = nullptr;
    const FMovieSceneBinding* Binding = nullptr;
    TSharedPtr<FJsonObject> Error;
    if (!SeqLoadForWrite(Arguments, Sequence, MovieScene, Binding, Error))
    {
        return Error.ToSharedRef();
    }

    FString PackagePath;
    if (!Arguments->TryGetStringField(TEXT("package_path"), PackagePath) || PackagePath.IsEmpty())
    {
        return SeqMakeError(TEXT("invalid_arguments"), TEXT("Missing required string field 'package_path'."));
    }
    if (!PackagePath.StartsWith(TEXT("/Game/")))
    {
        return SeqMakeError(TEXT("invalid_arguments"), TEXT("Animations may only be baked under /Game."));
    }
    // Refused rather than replaced: baking over an existing package runs the editor's delete path
    // first, and an animation built into a package marked deleted saves without its data model.
    // DoesPackageExist also catches a file on disk the registry does not list, such as an LFS
    // pointer that was never pulled.
    if (FPackageName::DoesPackageExist(PackagePath) || FindPackage(nullptr, *PackagePath))
    {
        return SeqMakeError(TEXT("already_exists"),
            FString::Printf(TEXT("A package already exists at '%s'. Bake to a new name."), *PackagePath));
    }

    UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!EditorWorld)
    {
        return SeqMakeError(TEXT("bake_failed"), TEXT("No editor world is available to evaluate the sequence in."));
    }

    // The skeleton has to be known before the model is initialised, and the player that resolves
    // the binding's mesh only exists inside the export, so it is taken from the animation the
    // binding already plays.
    USkeleton* Skeleton = nullptr;
    for (UMovieSceneTrack* Track : Binding->GetTracks())
    {
        if (!Cast<UMovieSceneSkeletalAnimationTrack>(Track))
        {
            continue;
        }
        for (UMovieSceneSection* Section : Track->GetAllSections())
        {
            const UMovieSceneSkeletalAnimationSection* AnimSection = Cast<UMovieSceneSkeletalAnimationSection>(Section);
            if (!Skeleton && AnimSection && AnimSection->Params.Animation)
            {
                Skeleton = AnimSection->Params.Animation->GetSkeleton();
            }
        }
    }
    // A binding animated only by a Control Rig has no section to read, so the caller names it.
    FString SkeletonPath;
    if (!Skeleton && Arguments->TryGetStringField(TEXT("skeleton"), SkeletonPath) && !SkeletonPath.IsEmpty())
    {
        Skeleton = LoadObject<USkeleton>(nullptr, *SeqObjectPath(SkeletonPath));
        if (!Skeleton)
        {
            return SeqMakeError(TEXT("load_failed"),
                FString::Printf(TEXT("Could not load a skeleton at '%s'."), *SkeletonPath));
        }
    }
    if (!Skeleton)
    {
        return SeqMakeError(TEXT("bake_failed"),
            TEXT("The binding has no skeletal animation section to take the skeleton from; pass 'skeleton'."));
    }

    UAnimSeqExportOption* ExportOptions = NewObject<UAnimSeqExportOption>(GetTransientPackage());
    ExportOptions->bExportTransforms = true;
    ExportOptions->bExportMorphTargets = true;
    ExportOptions->bExportAttributeCurves = true;
    ExportOptions->bExportMaterialCurves = true;
    ExportOptions->bRecordInWorldSpace = false;
    ExportOptions->bTransactRecording = false;
    // No custom time range on purpose: the engine then samples the animation section at the
    // sequence display rate, which is what the editor dialog does with the box unticked. A custom
    // range here recorded a one-frame animation at the tick rate.

    const FString AssetName = FPackageName::GetLongPackageAssetName(PackagePath);
    UPackage* Package = CreatePackage(*PackagePath);
    UAnimSequence* Animation = Package
        ? NewObject<UAnimSequence>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional)
        : nullptr;
    if (!Animation)
    {
        return SeqMakeError(TEXT("create_failed"), FString::Printf(TEXT("Could not create '%s'."), *PackagePath));
    }
    // What UAnimSequenceFactory does for the editor dialog, skeleton first. Without the model setup
    // saving drops the recording to an empty one-frame animation, and a model initialised before
    // the skeleton is known keeps no bone tracks.
    Animation->SetSkeleton(Skeleton);
    Animation->GetController().InitializeModel();
    Animation->GetController().SetNumberOfFrames(1);
    Animation->GetController().NotifyPopulated();

    const FMovieSceneBindingProxy BindingProxy(Binding->GetObjectGuid(), Sequence);
    const bool bBaked = USequencerToolsFunctionLibrary::ExportAnimSequence(
        EditorWorld, Sequence, Animation, ExportOptions, BindingProxy, false);
    if (!bBaked)
    {
        Animation->ClearFlags(RF_Public | RF_Standalone);
        Animation->MarkAsGarbage();
        return SeqMakeError(TEXT("bake_failed"),
            TEXT("ExportAnimSequence failed; the binding may not resolve to a skeletal mesh in the editor world."));
    }

    FAssetRegistryModule::AssetCreated(Animation);
    Package->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), Animation->GetPathName());
    Data->SetStringField(TEXT("sequence"), Sequence->GetPathName());
    Data->SetStringField(TEXT("binding"), Binding->GetObjectGuid().ToString());
    Data->SetNumberField(TEXT("play_length"), Animation->GetPlayLength());
    Data->SetNumberField(TEXT("number_of_keys"), Animation->GetNumberOfSampledKeys());
    Data->SetNumberField(TEXT("frame_rate"), Animation->GetSamplingFrameRate().AsDecimal());
    Data->SetBoolField(TEXT("has_data_model"), Animation->GetDataModelInterface().GetInterface() != nullptr);
    Data->SetStringField(TEXT("skeleton"), Skeleton->GetPathName());
    if (const IAnimationDataModel* Model = Animation->GetDataModel())
    {
        Data->SetNumberField(TEXT("bone_track_count"), Model->GetNumBoneTracks());
    }
    Data->SetBoolField(TEXT("dirty"), true);

    SeqSaveIfAsked(Arguments, Animation, Data,
        [this](const TSharedPtr<FJsonObject>& SaveArgs) { return ToolSaveAsset(SaveArgs); });
    return SeqMakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolSetLevelSequenceAnimation(const TSharedPtr<FJsonObject>& Arguments)
{
    ULevelSequence* Sequence = nullptr;
    UMovieScene* MovieScene = nullptr;
    const FMovieSceneBinding* Binding = nullptr;
    TSharedPtr<FJsonObject> Error;
    if (!SeqLoadForWrite(Arguments, Sequence, MovieScene, Binding, Error))
    {
        return Error.ToSharedRef();
    }

    FString AnimationPath;
    if (!Arguments->TryGetStringField(TEXT("animation"), AnimationPath) || AnimationPath.IsEmpty())
    {
        return SeqMakeError(TEXT("invalid_arguments"), TEXT("Missing required string field 'animation'."));
    }
    UAnimSequenceBase* Animation = LoadObject<UAnimSequenceBase>(nullptr, *SeqObjectPath(AnimationPath));
    if (!Animation)
    {
        return SeqMakeError(TEXT("load_failed"),
            FString::Printf(TEXT("Could not load an animation at '%s'."), *AnimationPath));
    }

    TArray<UMovieSceneSkeletalAnimationSection*> Sections;
    for (UMovieSceneTrack* Track : Binding->GetTracks())
    {
        if (!Cast<UMovieSceneSkeletalAnimationTrack>(Track))
        {
            continue;
        }
        for (UMovieSceneSection* Section : Track->GetAllSections())
        {
            if (UMovieSceneSkeletalAnimationSection* AnimSection = Cast<UMovieSceneSkeletalAnimationSection>(Section))
            {
                Sections.Add(AnimSection);
            }
        }
    }
    bool bCreateSection = false;
    Arguments->TryGetBoolField(TEXT("create_section"), bCreateSection);
    if (Sections.Num() == 0 && !bCreateSection)
    {
        return SeqMakeError(TEXT("track_not_found"), TEXT("The binding has no skeletal animation section."));
    }

    FScopedTransaction Transaction(FText::FromString(TEXT("Set Level Sequence Animation")));
    if (Sections.Num() == 0)
    {
        UMovieSceneSkeletalAnimationTrack* AnimationTrack = nullptr;
        for (UMovieSceneTrack* Track : Binding->GetTracks())
        {
            if (UMovieSceneSkeletalAnimationTrack* Existing = Cast<UMovieSceneSkeletalAnimationTrack>(Track))
            {
                AnimationTrack = Existing;
                break;
            }
        }
        MovieScene->Modify();
        if (!AnimationTrack)
        {
            AnimationTrack = MovieScene->AddTrack<UMovieSceneSkeletalAnimationTrack>(Binding->GetObjectGuid());
        }
        UMovieSceneSection* NewSection = AnimationTrack
            ? AnimationTrack->AddNewAnimation(MovieScene->GetPlaybackRange().GetLowerBoundValue(), Animation)
            : nullptr;
        UMovieSceneSkeletalAnimationSection* NewAnimSection = Cast<UMovieSceneSkeletalAnimationSection>(NewSection);
        if (!NewAnimSection)
        {
            return SeqMakeError(TEXT("write_failed"), TEXT("Could not add an animation section."));
        }
        Sections.Add(NewAnimSection);
    }
    TArray<TSharedPtr<FJsonValue>> Previous;
    for (UMovieSceneSkeletalAnimationSection* Section : Sections)
    {
        if (!Section->TryModify())
        {
            return SeqMakeError(TEXT("write_failed"), TEXT("An animation section is locked or read-only."));
        }
        const UAnimSequenceBase* OldAnimation = Section->Params.Animation;
        Previous.Add(MakeShared<FJsonValueString>(OldAnimation ? OldAnimation->GetPathName() : FString()));
        // The section range is left alone: the caller swaps in an animation of the same length.
        Section->Params.Animation = Animation;
        Section->MarkAsChanged();
    }
    Sequence->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), Sequence->GetPathName());
    Data->SetStringField(TEXT("animation"), Animation->GetPathName());
    Data->SetNumberField(TEXT("animation_length"), Animation->GetPlayLength());
    Data->SetArrayField(TEXT("previous_animations"), Previous);
    Data->SetNumberField(TEXT("section_count"), Sections.Num());
    Data->SetBoolField(TEXT("dirty"), true);

    SeqSaveIfAsked(Arguments, Sequence, Data,
        [this](const TSharedPtr<FJsonObject>& SaveArgs) { return ToolSaveAsset(SaveArgs); });
    return SeqMakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolRemoveLevelSequenceTrack(const TSharedPtr<FJsonObject>& Arguments)
{
    ULevelSequence* Sequence = nullptr;
    UMovieScene* MovieScene = nullptr;
    const FMovieSceneBinding* Binding = nullptr;
    TSharedPtr<FJsonObject> Error;
    if (!SeqLoadForWrite(Arguments, Sequence, MovieScene, Binding, Error))
    {
        return Error.ToSharedRef();
    }

    FString TrackType;
    if (!Arguments->TryGetStringField(TEXT("track_type"), TrackType) || TrackType.IsEmpty())
    {
        return SeqMakeError(TEXT("invalid_arguments"), TEXT("Missing required string field 'track_type'."));
    }

    // Collected first: RemoveTrack edits the binding's track array being iterated.
    TArray<UMovieSceneTrack*> Matches;
    for (UMovieSceneTrack* Track : Binding->GetTracks())
    {
        if (Track && Track->GetClass()->GetName() == TrackType)
        {
            Matches.Add(Track);
        }
    }
    // Tracks of the type still outered to the movie scene but no longer listed anywhere. RemoveTrack
    // only unlists a track, so without this the removed track and its sections stay in the package
    // and keep being saved - with their Control Rig dependencies - by every later save.
    TSet<const UMovieSceneTrack*> Listed;
    const UMovieScene* ConstMovieScene = MovieScene;
    for (const FMovieSceneBinding& Each : ConstMovieScene->GetBindings())
    {
        for (const UMovieSceneTrack* Track : Each.GetTracks())
        {
            Listed.Add(Track);
        }
    }
    for (const UMovieSceneTrack* Track : ConstMovieScene->GetTracks())
    {
        Listed.Add(Track);
    }
    Listed.Add(ConstMovieScene->GetCameraCutTrack());

    TArray<UObject*> Inner;
    GetObjectsWithOuter(MovieScene, Inner, false);
    TArray<UMovieSceneTrack*> Orphans;
    for (UObject* Object : Inner)
    {
        UMovieSceneTrack* Track = Cast<UMovieSceneTrack>(Object);
        if (Track && Track->GetClass()->GetName() == TrackType && !Listed.Contains(Track))
        {
            Orphans.Add(Track);
        }
    }

    if (Matches.Num() == 0 && Orphans.Num() == 0)
    {
        return SeqMakeError(TEXT("track_not_found"),
            FString::Printf(TEXT("The binding has no track of type '%s'."), *TrackType));
    }

    FScopedTransaction Transaction(FText::FromString(TEXT("Remove Level Sequence Track")));
    MovieScene->Modify();
    int32 Removed = 0;
    for (UMovieSceneTrack* Track : Matches)
    {
        if (MovieScene->RemoveTrack(*Track))
        {
            ++Removed;
            Orphans.Add(Track);
        }
    }
    // Moved out of the package the way Sequencer's own delete does, so the save no longer reaches
    // them. Rename is transactional, which keeps the removal undoable.
    for (UMovieSceneTrack* Track : Orphans)
    {
        Track->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors);
    }
    Sequence->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), Sequence->GetPathName());
    Data->SetStringField(TEXT("track_type"), TrackType);
    Data->SetNumberField(TEXT("removed_count"), Removed);
    Data->SetNumberField(TEXT("purged_count"), Orphans.Num());
    Data->SetBoolField(TEXT("dirty"), true);

    SeqSaveIfAsked(Arguments, Sequence, Data,
        [this](const TSharedPtr<FJsonObject>& SaveArgs) { return ToolSaveAsset(SaveArgs); });
    return SeqMakeOk(Data);
}

namespace
{
    /** Loads a level sequence by path for the copy tool, which works on two of them at once. */
    ULevelSequence* SeqLoadSequence(const FString& AssetPath, UMovieScene*& OutMovieScene, TSharedPtr<FJsonObject>& OutError)
    {
        ULevelSequence* Sequence = LoadObject<ULevelSequence>(nullptr, *SeqObjectPath(AssetPath));
        OutMovieScene = Sequence ? Sequence->GetMovieScene() : nullptr;
        if (!OutMovieScene)
        {
            OutError = SeqMakeError(TEXT("load_failed"),
                FString::Printf(TEXT("Could not load a level sequence with a movie scene at '%s'."), *AssetPath));
            return nullptr;
        }
        return Sequence;
    }

    /** The spawnable preview actor a custom binding carries, read by reflection as in SeqResolveTemplate. */
    const UObject* SeqCustomBindingTemplate(const UObject* CustomBinding)
    {
        const FObjectPropertyBase* ActorTemplateProperty = CustomBinding
            ? CastField<FObjectPropertyBase>(CustomBinding->GetClass()->FindPropertyByName(TEXT("ActorTemplate")))
            : nullptr;
        return ActorTemplateProperty ? ActorTemplateProperty->GetObjectPropertyValue_InContainer(CustomBinding) : nullptr;
    }
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolCopyLevelSequenceBindings(const TSharedPtr<FJsonObject>& Arguments)
{
    FString SourcePath;
    FString TargetPath;
    const TArray<TSharedPtr<FJsonValue>>* BindingArray = nullptr;
    if (!Arguments.IsValid()
        || !Arguments->TryGetStringField(TEXT("source_asset_path"), SourcePath) || SourcePath.IsEmpty()
        || !Arguments->TryGetStringField(TEXT("target_asset_path"), TargetPath) || TargetPath.IsEmpty()
        || !Arguments->TryGetArrayField(TEXT("bindings"), BindingArray) || BindingArray->Num() == 0)
    {
        return SeqMakeError(TEXT("invalid_arguments"),
            TEXT("Missing required fields 'source_asset_path', 'target_asset_path' and non-empty array 'bindings'."));
    }

    TSharedPtr<FJsonObject> Error;
    UMovieScene* SourceScene = nullptr;
    UMovieScene* TargetScene = nullptr;
    ULevelSequence* SourceSequence = SeqLoadSequence(SourcePath, SourceScene, Error);
    if (!SourceSequence)
    {
        return Error.ToSharedRef();
    }
    ULevelSequence* TargetSequence = SeqLoadSequence(TargetPath, TargetScene, Error);
    if (!TargetSequence)
    {
        return Error.ToSharedRef();
    }
    if (SourceSequence == TargetSequence)
    {
        return SeqMakeError(TEXT("invalid_arguments"), TEXT("Source and target are the same sequence."));
    }
    if (!TargetSequence->GetPathName().StartsWith(TEXT("/Game/")))
    {
        return SeqMakeError(TEXT("invalid_arguments"), TEXT("Level sequence writes are limited to /Game assets."));
    }

    // Through the base pointer for the same reason as SeqResolveTemplate.
    UMovieSceneSequence* SourceBase = SourceSequence;
    UMovieSceneSequence* TargetBase = TargetSequence;
    FMovieSceneBindingReferences* SourceReferences = SourceBase->GetBindingReferences();
    FMovieSceneBindingReferences* TargetReferences = TargetBase->GetBindingReferences();
    if (!SourceReferences || !TargetReferences)
    {
        return SeqMakeError(TEXT("unsupported"), TEXT("A sequence has no binding references to copy through."));
    }

    const UMovieScene* ConstSourceScene = SourceScene;
    const UMovieScene* ConstTargetScene = TargetScene;

    TSet<FGuid> Selected;
    for (const TSharedPtr<FJsonValue>& Entry : *BindingArray)
    {
        FString BindingKey;
        if (!Entry.IsValid() || !Entry->TryGetString(BindingKey) || BindingKey.IsEmpty())
        {
            return SeqMakeError(TEXT("invalid_arguments"), TEXT("Every entry in 'bindings' must be a non-empty string."));
        }
        const FMovieSceneBinding* Binding = SeqFindBinding(SourceScene, BindingKey);
        if (!Binding)
        {
            return SeqMakeError(TEXT("binding_not_found"),
                FString::Printf(TEXT("No binding named or identified by '%s' in the source."), *BindingKey));
        }
        Selected.Add(Binding->GetObjectGuid());
    }

    // Component bindings (a Niagara actor's NiagaraComponent0, say) are possessables parented to the
    // actor binding. Copying the actor without them would drop their tracks, so they come along.
    bool bIncludeChildren = true;
    Arguments->TryGetBoolField(TEXT("include_children"), bIncludeChildren);
    if (bIncludeChildren)
    {
        bool bGrew = true;
        while (bGrew)
        {
            bGrew = false;
            for (const FMovieSceneBinding& Binding : ConstSourceScene->GetBindings())
            {
                const FGuid& Guid = Binding.GetObjectGuid();
                const FMovieScenePossessable* Possessable = SourceScene->FindPossessable(Guid);
                if (!Selected.Contains(Guid) && Possessable && Selected.Contains(Possessable->GetParent()))
                {
                    Selected.Add(Guid);
                    bGrew = true;
                }
            }
        }
    }

    // Parents before children, so every copied possessable's parent is already in place.
    TArray<FGuid> Ordered;
    while (Ordered.Num() < Selected.Num())
    {
        const int32 Before = Ordered.Num();
        for (const FGuid& Guid : Selected)
        {
            if (Ordered.Contains(Guid))
            {
                continue;
            }
            const FMovieScenePossessable* Possessable = SourceScene->FindPossessable(Guid);
            const FGuid Parent = Possessable ? Possessable->GetParent() : FGuid();
            if (!Parent.IsValid() || !Selected.Contains(Parent) || Ordered.Contains(Parent))
            {
                Ordered.Add(Guid);
            }
        }
        if (Ordered.Num() == Before)
        {
            return SeqMakeError(TEXT("invalid_arguments"), TEXT("The selected bindings have a parent cycle."));
        }
    }

    // Everything is validated before the first write, so a refusal leaves the target untouched.
    const auto ResolvesInTarget = [&Selected, ConstTargetScene](const FGuid& Guid)
    {
        return Selected.Contains(Guid) || ConstTargetScene->FindBinding(Guid) != nullptr;
    };

    TArray<TSharedPtr<FJsonValue>> Unresolved;
    for (const FGuid& Guid : Ordered)
    {
        const FString Name = SourceScene->GetObjectDisplayName(Guid).ToString();
        if (ConstTargetScene->FindBinding(Guid) || TargetScene->FindPossessable(Guid))
        {
            return SeqMakeError(TEXT("already_exists"),
                FString::Printf(TEXT("The target already has binding %s ('%s')."), *Guid.ToString(), *Name));
        }
        const FMovieScenePossessable* Possessable = SourceScene->FindPossessable(Guid);
        if (!Possessable)
        {
            return SeqMakeError(TEXT("unsupported"),
                FString::Printf(TEXT("'%s' is a legacy spawnable; only possessable and custom bindings are copied."), *Name));
        }
        const FMovieSceneBinding* SourceBinding = ConstSourceScene->FindBinding(Guid);
        if (!SourceBinding)
        {
            return SeqMakeError(TEXT("binding_not_found"),
                FString::Printf(TEXT("'%s' has a possessable but no binding."), *Name));
        }
        // Decorations are objects owned by the source movie scene; a copied binding would share them
        // across packages, so a binding carrying any is refused rather than half copied.
        if (SourceBinding->GetDecorations().Num() > 0)
        {
            return SeqMakeError(TEXT("unsupported"),
                FString::Printf(TEXT("'%s' carries binding decorations, which this tool does not copy."), *Name));
        }
        if (Possessable->GetParent().IsValid() && !ResolvesInTarget(Possessable->GetParent()))
        {
            return SeqMakeError(TEXT("parent_missing"),
                FString::Printf(TEXT("'%s' is parented to %s, which the target does not have."),
                    *Name, *Possessable->GetParent().ToString()));
        }

        for (const FMovieSceneBindingReference& Reference : SourceReferences->GetReferences(Guid))
        {
            // PostDuplicate re-duplicates the preview actor into the target movie scene under its own
            // name, so a same-named object already there would be clobbered.
            const UObject* Template = SeqCustomBindingTemplate(Reference.CustomBinding.Get());
            if (Template && StaticFindObjectFast(nullptr, TargetScene, Template->GetFName()))
            {
                return SeqMakeError(TEXT("name_collision"),
                    FString::Printf(TEXT("The target movie scene already holds an object named '%s'."), *Template->GetName()));
            }
        }

        // An attach section follows another binding by GUID. The shared bindings (Executor, Target,
        // the weapon) carry the same GUIDs in sequences duplicated from each other; anything else
        // would attach to nothing, which is the bone loss this tool exists to avoid.
        for (const UMovieSceneTrack* Track : SourceBinding->GetTracks())
        {
            if (!Track)
            {
                continue;
            }
            for (const UMovieSceneSection* Section : Track->GetAllSections())
            {
                FGuid ConstraintGuid;
                if (Section && SeqGetConstraintBindingGuid(Section, ConstraintGuid)
                    && ConstraintGuid.IsValid() && !ResolvesInTarget(ConstraintGuid))
                {
                    TSharedPtr<FJsonObject> Missing = MakeShared<FJsonObject>();
                    Missing->SetStringField(TEXT("binding"), Name);
                    Missing->SetStringField(TEXT("constraint_binding_id"), ConstraintGuid.ToString());
                    Missing->SetStringField(TEXT("constraint_name"),
                        SourceScene->GetObjectDisplayName(ConstraintGuid).ToString());
                    Unresolved.Add(MakeShared<FJsonValueObject>(Missing));
                }
            }
        }
    }

    bool bAllowUnresolved = false;
    Arguments->TryGetBoolField(TEXT("allow_unresolved"), bAllowUnresolved);
    if (Unresolved.Num() > 0 && !bAllowUnresolved)
    {
        TSharedRef<FJsonObject> Refusal = SeqMakeError(TEXT("unresolved_reference"),
            TEXT("Attach sections follow bindings the target does not have. Pass allow_unresolved=true to copy anyway."));
        Refusal->SetArrayField(TEXT("unresolved"), Unresolved);
        return Refusal;
    }

    FScopedTransaction Transaction(FText::FromString(TEXT("Copy Level Sequence Bindings")));
    TargetSequence->Modify();
    TargetScene->Modify();

    TArray<TSharedPtr<FJsonValue>> Copied;
    for (const FGuid& Guid : Ordered)
    {
        const FMovieScenePossessable* SourcePossessable = SourceScene->FindPossessable(Guid);
        const FMovieSceneBinding* SourceBinding = ConstSourceScene->FindBinding(Guid);

        // Same GUID on purpose: attach sections and the child components' parent links point at it.
        // FMovieSceneBinding's GUID constructor is private in 5.7, so the source binding is copied
        // and emptied instead; its tracks are duplicated in below. No owner, so no removal events
        // fire on the source's tracks.
        FMovieSceneBinding NewBinding = *SourceBinding;
        NewBinding.StealTracks(nullptr);
        TargetScene->AddPossessable(*SourcePossessable, NewBinding);

#if WITH_EDITORONLY_DATA
        const FText DisplayName = SourceScene->GetObjectDisplayName(Guid);
        if (DisplayName.ToString() != SourcePossessable->GetName())
        {
            TargetScene->SetObjectDisplayName(Guid, DisplayName);
        }
#endif

        int32 ReferenceCount = 0;
        bool bCustomBinding = false;
        for (const FMovieSceneBindingReference& Reference : SourceReferences->GetReferences(Guid))
        {
            UMovieSceneCustomBinding* NewCustomBinding = nullptr;
            if (UMovieSceneCustomBinding* SourceCustomBinding = Reference.CustomBinding.Get())
            {
                // Outered to the target movie scene, as the engine creates them, so the binding's
                // PostDuplicate moves its preview actor template into the target package too.
                NewCustomBinding = DuplicateObject<UMovieSceneCustomBinding>(SourceCustomBinding, TargetScene,
                    MakeUniqueObjectName(TargetScene, SourceCustomBinding->GetClass(), SourceCustomBinding->GetFName()));
                bCustomBinding = true;
            }
            FUniversalObjectLocator Locator = Reference.Locator;
            TargetReferences->AddBinding(Guid, MoveTemp(Locator), Reference.ResolveFlags, NewCustomBinding);
            ++ReferenceCount;
        }

        int32 TrackCount = 0;
        const int32 SourceTrackCount = SourceBinding ? SourceBinding->GetTracks().Num() : 0;
        if (SourceBinding)
        {
            for (UMovieSceneTrack* Track : SourceBinding->GetTracks())
            {
                if (!Track)
                {
                    continue;
                }
                UMovieSceneTrack* NewTrack = DuplicateObject<UMovieSceneTrack>(Track, TargetScene,
                    MakeUniqueObjectName(TargetScene, Track->GetClass(), Track->GetFName()));
                if (NewTrack && TargetScene->AddGivenTrack(NewTrack, Guid))
                {
                    ++TrackCount;
                }
            }
        }

        TSharedPtr<FJsonObject> CopiedEntry = MakeShared<FJsonObject>();
        CopiedEntry->SetStringField(TEXT("binding_id"), Guid.ToString());
        CopiedEntry->SetStringField(TEXT("name"), SourceScene->GetObjectDisplayName(Guid).ToString());
        if (SourcePossessable->GetParent().IsValid())
        {
            CopiedEntry->SetStringField(TEXT("parent_binding_id"), SourcePossessable->GetParent().ToString());
        }
        CopiedEntry->SetBoolField(TEXT("custom_binding"), bCustomBinding);
        CopiedEntry->SetNumberField(TEXT("reference_count"), ReferenceCount);
        CopiedEntry->SetNumberField(TEXT("track_count"), TrackCount);
        CopiedEntry->SetNumberField(TEXT("source_track_count"), SourceTrackCount);
        Copied.Add(MakeShared<FJsonValueObject>(CopiedEntry));
    }

    TargetSequence->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("source_asset_path"), SourceSequence->GetPathName());
    Data->SetStringField(TEXT("target_asset_path"), TargetSequence->GetPathName());
    Data->SetArrayField(TEXT("copied"), Copied);
    Data->SetArrayField(TEXT("unresolved"), Unresolved);
    Data->SetBoolField(TEXT("dirty"), true);

    SeqSaveIfAsked(Arguments, TargetSequence, Data,
        [this](const TSharedPtr<FJsonObject>& SaveArgs) { return ToolSaveAsset(SaveArgs); });
    return SeqMakeOk(Data);
}

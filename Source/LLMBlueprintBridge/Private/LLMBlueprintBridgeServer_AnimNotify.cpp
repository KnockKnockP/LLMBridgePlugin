// Anim notify tools for the bridge: read, place and remove notifies on any UAnimSequenceBase
// (AnimSequence, AnimMontage, AnimComposite).
//
// Writes mirror what UAnimationBlueprintLibrary and the notify panel do - add an FAnimNotifyEvent,
// Link it to the asset so montage slots resolve, then RefreshCacheData - but build the notify object
// with its properties set first, so a bad property value fails the call before the asset is touched.

#include "LLMBlueprintBridgeServer.h"

#include "Animation/AnimMontage.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimTypes.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "GameplayTagContainer.h"
#include "Misc/PackageName.h"
#include "Misc/StringOutputDevice.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

namespace
{
    /** Two notifies closer than this are treated as the same placement. Well under one frame at 120 fps. */
    constexpr float NotifyTimeTolerance = 1.0e-3f;

    TSharedRef<FJsonObject> NotifyMakeError(const FString& Code, const FString& Message)
    {
        TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
        Root->SetBoolField(TEXT("ok"), false);
        Root->SetStringField(TEXT("code"), Code);
        Root->SetStringField(TEXT("message"), Message);
        return Root;
    }

    TSharedRef<FJsonObject> NotifyMakeOk(const TSharedPtr<FJsonObject>& Data)
    {
        TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
        Root->SetBoolField(TEXT("ok"), true);
        if (Data.IsValid())
        {
            Root->SetObjectField(TEXT("data"), Data);
        }
        return Root;
    }

    FString NotifyNormalizeObjectPath(FString AssetPath)
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

    UAnimSequenceBase* NotifyLoadAnimation(const TSharedPtr<FJsonObject>& Arguments, FString& OutError)
    {
        FString AssetPath;
        if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
        {
            OutError = TEXT("Missing required string field 'asset_path'.");
            return nullptr;
        }

        const FString ObjectPath = NotifyNormalizeObjectPath(AssetPath);
        UAnimSequenceBase* Animation = LoadObject<UAnimSequenceBase>(nullptr, *ObjectPath);
        if (!Animation)
        {
            OutError = FString::Printf(TEXT("Could not load an AnimSequence, AnimMontage or AnimComposite at '%s'."), *ObjectPath);
            return nullptr;
        }
        return Animation;
    }

    /** The UObject that defines what the event does: the notify, the notify state, or null for a skeleton (named) notify. */
    UObject* NotifyEventObject(const FAnimNotifyEvent& Event)
    {
        if (Event.Notify)
        {
            return Event.Notify;
        }
        return Event.NotifyStateClass;
    }

    /** Matches a class against a caller's filter: exact name, name without the Blueprint `_C` suffix, or a path substring. */
    bool NotifyClassMatches(const UClass* Class, const FString& Filter)
    {
        if (!Class || Filter.IsEmpty())
        {
            return false;
        }

        const FString Name = Class->GetName();
        const FString NameWithoutSuffix = Name.EndsWith(TEXT("_C")) ? Name.LeftChop(2) : Name;
        FString ShortFilter = FPackageName::GetShortName(Filter);
        int32 DotIndex = INDEX_NONE;
        if (ShortFilter.FindLastChar(TEXT('.'), DotIndex))
        {
            ShortFilter = ShortFilter.Mid(DotIndex + 1);
        }
        if (ShortFilter.EndsWith(TEXT("_C")))
        {
            ShortFilter.LeftChopInline(2);
        }

        return Name.Equals(Filter, ESearchCase::IgnoreCase)
            || NameWithoutSuffix.Equals(ShortFilter, ESearchCase::IgnoreCase)
            || Class->GetPathName().Contains(Filter, ESearchCase::IgnoreCase);
    }

    bool NotifyEventMatchesClass(const FAnimNotifyEvent& Event, const FString& Filter)
    {
        const UObject* Object = NotifyEventObject(Event);
        return Object && NotifyClassMatches(Object->GetClass(), Filter);
    }

    /**
     * Properties a designer edits on the notify: everything editable that the notify's own class
     * hierarchy adds, but not the engine base classes' bookkeeping (colors, editor preview flags).
     */
    bool NotifyIsUserProperty(const FProperty* Property)
    {
        if (!Property->HasAnyPropertyFlags(CPF_Edit) || Property->HasAnyPropertyFlags(CPF_Deprecated))
        {
            return false;
        }

        const UClass* Owner = Property->GetOwnerClass();
        return Owner != UAnimNotify::StaticClass()
            && Owner != UAnimNotifyState::StaticClass()
            && Owner != UObject::StaticClass();
    }

    TSharedPtr<FJsonObject> NotifyExportProperties(const UObject* Object)
    {
        TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
        if (!Object)
        {
            return Properties;
        }

        for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
        {
            const FProperty* Property = *It;
            if (!NotifyIsUserProperty(Property))
            {
                continue;
            }

            FString Exported;
            Property->ExportText_Direct(Exported, Property->ContainerPtrToValuePtr<void>(Object), nullptr, const_cast<UObject*>(Object), PPF_None);
            Properties->SetStringField(Property->GetName(), Exported);
        }
        return Properties;
    }

    FProperty* NotifyFindProperty(UClass* Class, const FString& Name)
    {
        if (FProperty* Exact = FindFProperty<FProperty>(Class, FName(*Name)))
        {
            return Exact;
        }

        // Blueprint variables and C++ properties are often written with different casing by callers.
        for (TFieldIterator<FProperty> It(Class); It; ++It)
        {
            if (It->GetName().Equals(Name, ESearchCase::IgnoreCase) ||
                It->GetAuthoredName().Equals(Name, ESearchCase::IgnoreCase))
            {
                return *It;
            }
        }
        return nullptr;
    }

    /**
     * Sets one property from JSON. Strings go through the editor's text import, so structs and object
     * references take the same text the details panel copies. A gameplay tag also accepts a bare tag
     * name, and an unknown tag is an error rather than a silently empty tag.
     */
    bool NotifyApplyProperty(UObject* Object, const FString& Name, const TSharedPtr<FJsonValue>& Value, FString& OutError)
    {
        FProperty* Property = NotifyFindProperty(Object->GetClass(), Name);
        if (!Property)
        {
            OutError = FString::Printf(TEXT("Notify class '%s' has no property '%s'."), *Object->GetClass()->GetName(), *Name);
            return false;
        }

        FString Text;
        switch (Value.IsValid() ? Value->Type : EJson::None)
        {
        case EJson::String:
            Text = Value->AsString();
            break;
        case EJson::Number:
            Text = FString::SanitizeFloat(Value->AsNumber());
            break;
        case EJson::Boolean:
            Text = Value->AsBool() ? TEXT("True") : TEXT("False");
            break;
        default:
            OutError = FString::Printf(TEXT("Property '%s' needs a string, number or boolean value."), *Name);
            return false;
        }

        if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
        {
            if (StructProperty->Struct == FGameplayTag::StaticStruct() && !Text.StartsWith(TEXT("(")))
            {
                if (!Text.IsEmpty() && !FGameplayTag::RequestGameplayTag(FName(*Text), false).IsValid())
                {
                    OutError = FString::Printf(TEXT("Gameplay tag '%s' is not registered."), *Text);
                    return false;
                }
                Text = FString::Printf(TEXT("(TagName=\"%s\")"), *Text);
            }
        }

        FStringOutputDevice ImportErrors;
        const TCHAR* End = Property->ImportText_Direct(*Text, Property->ContainerPtrToValuePtr<void>(Object), Object, PPF_None, &ImportErrors);
        if (!End || !ImportErrors.IsEmpty())
        {
            OutError = FString::Printf(TEXT("Property '%s' could not import '%s'%s%s"), *Property->GetName(), *Text,
                ImportErrors.IsEmpty() ? TEXT("") : TEXT(": "), *ImportErrors);
            return false;
        }
        return true;
    }

    FString NotifyTrackName(const UAnimSequenceBase* Animation, int32 TrackIndex)
    {
#if WITH_EDITORONLY_DATA
        if (Animation->AnimNotifyTracks.IsValidIndex(TrackIndex))
        {
            return Animation->AnimNotifyTracks[TrackIndex].TrackName.ToString();
        }
#endif
        return FString();
    }

    /** Index of the named track, creating it when allowed. An empty name means the first track. Call after Modify(). */
    int32 NotifyResolveTrack(UAnimSequenceBase* Animation, const FString& TrackName, bool bCreate, FString& OutError)
    {
#if WITH_EDITORONLY_DATA
        if (TrackName.IsEmpty())
        {
            if (Animation->AnimNotifyTracks.Num() == 0)
            {
                Animation->AnimNotifyTracks.Add(FAnimNotifyTrack(TEXT("1"), FLinearColor::White));
            }
            return 0;
        }

        for (int32 Index = 0; Index < Animation->AnimNotifyTracks.Num(); ++Index)
        {
            if (Animation->AnimNotifyTracks[Index].TrackName.ToString().Equals(TrackName, ESearchCase::IgnoreCase))
            {
                return Index;
            }
        }

        if (!bCreate)
        {
            OutError = FString::Printf(TEXT("Animation has no notify track named '%s'."), *TrackName);
            return INDEX_NONE;
        }
        return Animation->AnimNotifyTracks.Add(FAnimNotifyTrack(FName(*TrackName), FLinearColor::White));
#else
        OutError = TEXT("Notify tracks are editor-only data.");
        return INDEX_NONE;
#endif
    }

    TSharedPtr<FJsonObject> NotifyEventToJson(const UAnimSequenceBase* Animation, const FAnimNotifyEvent& Event, int32 Index, bool bIncludeProperties)
    {
        TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
        const UObject* Object = NotifyEventObject(Event);
        const bool bIsState = Event.NotifyStateClass != nullptr;
        const float Time = Event.GetTime();
        const float Duration = bIsState ? Event.GetDuration() : 0.0f;

        Json->SetNumberField(TEXT("index"), Index);
        Json->SetStringField(TEXT("kind"), Event.Notify ? TEXT("notify") : bIsState ? TEXT("state") : TEXT("named"));
        Json->SetStringField(TEXT("name"), Object
            ? (Event.Notify ? Event.Notify->GetNotifyName() : Event.NotifyStateClass->GetNotifyName())
            : Event.NotifyName.ToString());
        Json->SetStringField(TEXT("class"), Object ? Object->GetClass()->GetPathName() : FString());
        Json->SetNumberField(TEXT("time"), Time);

        const FFrameRate FrameRate = Animation->GetSamplingFrameRate();
        if (FrameRate.IsValid())
        {
            Json->SetNumberField(TEXT("frame"), FrameRate.AsFrameTime(Time).RoundToFrame().Value);
        }
        if (bIsState)
        {
            Json->SetNumberField(TEXT("duration"), Duration);
            Json->SetNumberField(TEXT("end_time"), Time + Duration);
        }

        Json->SetNumberField(TEXT("track_index"), Event.TrackIndex);
        Json->SetStringField(TEXT("track"), NotifyTrackName(Animation, Event.TrackIndex));
        Json->SetNumberField(TEXT("trigger_chance"), Event.NotifyTriggerChance);
        if (bIncludeProperties && Object)
        {
            Json->SetObjectField(TEXT("properties"), NotifyExportProperties(Object));
        }
        return Json;
    }

    /** True when the asset already has a notify of the same class, at the same time, with the same property values. */
    bool NotifyHasDuplicate(const UAnimSequenceBase* Animation, const UObject* Template, float Time)
    {
        const TSharedPtr<FJsonObject> Wanted = NotifyExportProperties(Template);
        for (const FAnimNotifyEvent& Event : Animation->Notifies)
        {
            const UObject* Existing = NotifyEventObject(Event);
            if (!Existing || Existing->GetClass() != Template->GetClass() ||
                !FMath::IsNearlyEqual(Event.GetTime(), Time, NotifyTimeTolerance))
            {
                continue;
            }

            const TSharedPtr<FJsonObject> Have = NotifyExportProperties(Existing);
            bool bSame = Have->Values.Num() == Wanted->Values.Num();
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Wanted->Values)
            {
                FString HaveValue;
                if (!bSame || !Have->TryGetStringField(Pair.Key, HaveValue) || HaveValue != Pair.Value->AsString())
                {
                    bSame = false;
                    break;
                }
            }
            if (bSame)
            {
                return true;
            }
        }
        return false;
    }

    bool NotifyShouldSave(const TSharedPtr<FJsonObject>& Arguments)
    {
        bool bSave = false;
        if (Arguments.IsValid())
        {
            Arguments->TryGetBoolField(TEXT("save"), bSave);
        }
        return bSave;
    }
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolInspectAnimNotifies(const TSharedPtr<FJsonObject>& Arguments)
{
    FString Error;
    UAnimSequenceBase* Animation = NotifyLoadAnimation(Arguments, Error);
    if (!Animation)
    {
        return NotifyMakeError(TEXT("load_failed"), Error);
    }

    FString ClassFilter;
    bool bIncludeProperties = true;
    if (Arguments.IsValid())
    {
        Arguments->TryGetStringField(TEXT("class_filter"), ClassFilter);
        Arguments->TryGetBoolField(TEXT("include_properties"), bIncludeProperties);
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), Animation->GetPathName());
    Data->SetStringField(TEXT("asset_class"), Animation->GetClass()->GetName());
    Data->SetNumberField(TEXT("play_length"), Animation->GetPlayLength());

    const FFrameRate FrameRate = Animation->GetSamplingFrameRate();
    if (FrameRate.IsValid())
    {
        Data->SetNumberField(TEXT("frame_rate"), FrameRate.AsDecimal());
    }

    TArray<TSharedPtr<FJsonValue>> Tracks;
#if WITH_EDITORONLY_DATA
    for (int32 Index = 0; Index < Animation->AnimNotifyTracks.Num(); ++Index)
    {
        TSharedPtr<FJsonObject> Track = MakeShared<FJsonObject>();
        Track->SetNumberField(TEXT("index"), Index);
        Track->SetStringField(TEXT("name"), Animation->AnimNotifyTracks[Index].TrackName.ToString());
        Tracks.Add(MakeShared<FJsonValueObject>(Track));
    }
#endif
    Data->SetArrayField(TEXT("tracks"), Tracks);

    if (const UAnimMontage* Montage = Cast<UAnimMontage>(Animation))
    {
        TArray<TSharedPtr<FJsonValue>> Sections;
        for (const FCompositeSection& Section : Montage->CompositeSections)
        {
            TSharedPtr<FJsonObject> SectionJson = MakeShared<FJsonObject>();
            SectionJson->SetStringField(TEXT("name"), Section.SectionName.ToString());
            SectionJson->SetNumberField(TEXT("start_time"), Section.GetTime());
            Sections.Add(MakeShared<FJsonValueObject>(SectionJson));
        }
        Data->SetArrayField(TEXT("sections"), Sections);
    }

    TArray<TSharedPtr<FJsonValue>> Notifies;
    for (int32 Index = 0; Index < Animation->Notifies.Num(); ++Index)
    {
        const FAnimNotifyEvent& Event = Animation->Notifies[Index];
        if (!ClassFilter.IsEmpty() && !NotifyEventMatchesClass(Event, ClassFilter))
        {
            continue;
        }
        Notifies.Add(MakeShared<FJsonValueObject>(NotifyEventToJson(Animation, Event, Index, bIncludeProperties)));
    }
    Data->SetNumberField(TEXT("notify_count"), Animation->Notifies.Num());
    Data->SetArrayField(TEXT("notifies"), Notifies);
    return NotifyMakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolAddAnimNotify(const TSharedPtr<FJsonObject>& Arguments)
{
    FString Error;
    UAnimSequenceBase* Animation = NotifyLoadAnimation(Arguments, Error);
    if (!Animation)
    {
        return NotifyMakeError(TEXT("load_failed"), Error);
    }
    if (!Animation->GetPathName().StartsWith(TEXT("/Game/")))
    {
        return NotifyMakeError(TEXT("invalid_arguments"), TEXT("Notify writes are limited to /Game assets."));
    }

    FString ClassPath;
    if (!Arguments->TryGetStringField(TEXT("notify_class"), ClassPath) || ClassPath.IsEmpty())
    {
        return NotifyMakeError(TEXT("invalid_arguments"), TEXT("Missing required string field 'notify_class'."));
    }
    UClass* NotifyClass = ResolveClass(ClassPath);
    if (!NotifyClass)
    {
        return NotifyMakeError(TEXT("class_not_found"),
            FString::Printf(TEXT("Could not resolve notify class '%s'. Pass a full path such as /Script/Project_S.SoundFeedbackAnimNotify or /Game/.../ANS_X.ANS_X_C."), *ClassPath));
    }
    const bool bIsState = NotifyClass->IsChildOf(UAnimNotifyState::StaticClass());
    if (!bIsState && !NotifyClass->IsChildOf(UAnimNotify::StaticClass()))
    {
        return NotifyMakeError(TEXT("invalid_arguments"),
            FString::Printf(TEXT("'%s' is neither an AnimNotify nor an AnimNotifyState."), *NotifyClass->GetName()));
    }
    if (NotifyClass->HasAnyClassFlags(CLASS_Abstract))
    {
        return NotifyMakeError(TEXT("invalid_arguments"), FString::Printf(TEXT("'%s' is abstract."), *NotifyClass->GetName()));
    }

    double Duration = 0.0;
    if (bIsState && (!Arguments->TryGetNumberField(TEXT("duration"), Duration) || Duration <= 0.0))
    {
        return NotifyMakeError(TEXT("invalid_arguments"), TEXT("A notify state needs a positive 'duration' in seconds."));
    }

    // Placement: an explicit time or frame, or one notify per matching anchor notify.
    const float PlayLength = Animation->GetPlayLength();
    TArray<float> Times;
    double Time = 0.0;
    double Frame = 0.0;
    const TSharedPtr<FJsonObject>* Anchor = nullptr;
    if (Arguments->TryGetObjectField(TEXT("anchor"), Anchor))
    {
        FString AnchorClass;
        FString Edge = TEXT("begin");
        double Offset = 0.0;
        (*Anchor)->TryGetStringField(TEXT("class"), AnchorClass);
        (*Anchor)->TryGetStringField(TEXT("edge"), Edge);
        (*Anchor)->TryGetNumberField(TEXT("offset"), Offset);
        if (AnchorClass.IsEmpty())
        {
            return NotifyMakeError(TEXT("invalid_arguments"), TEXT("'anchor' needs a 'class' to match existing notifies against."));
        }
        const bool bEnd = Edge.Equals(TEXT("end"), ESearchCase::IgnoreCase);
        if (!bEnd && !Edge.Equals(TEXT("begin"), ESearchCase::IgnoreCase))
        {
            return NotifyMakeError(TEXT("invalid_arguments"), TEXT("'anchor.edge' must be 'begin' or 'end'."));
        }

        for (const FAnimNotifyEvent& Event : Animation->Notifies)
        {
            if (NotifyEventMatchesClass(Event, AnchorClass))
            {
                const float EdgeTime = bEnd ? Event.GetTime() + Event.GetDuration() : Event.GetTime();
                Times.Add(FMath::Clamp(static_cast<float>(EdgeTime + Offset), 0.0f, PlayLength));
            }
        }
        if (Times.Num() == 0)
        {
            return NotifyMakeError(TEXT("anchor_not_found"),
                FString::Printf(TEXT("'%s' has no notify matching anchor class '%s'."), *Animation->GetName(), *AnchorClass));
        }
    }
    else if (Arguments->TryGetNumberField(TEXT("time"), Time))
    {
        Times.Add(static_cast<float>(Time));
    }
    else if (Arguments->TryGetNumberField(TEXT("frame"), Frame))
    {
        const FFrameRate FrameRate = Animation->GetSamplingFrameRate();
        if (!FrameRate.IsValid())
        {
            return NotifyMakeError(TEXT("invalid_arguments"), TEXT("Animation has no frame rate; pass 'time' instead of 'frame'."));
        }
        Times.Add(static_cast<float>(FrameRate.AsSeconds(FFrameTime::FromDecimal(Frame))));
    }
    else
    {
        return NotifyMakeError(TEXT("invalid_arguments"), TEXT("Pass one of 'time', 'frame' or 'anchor'."));
    }

    for (const float PlaceTime : Times)
    {
        if (PlaceTime < 0.0f || PlaceTime > PlayLength + KINDA_SMALL_NUMBER)
        {
            return NotifyMakeError(TEXT("invalid_arguments"),
                FString::Printf(TEXT("Time %.4f is outside the animation range 0..%.4f."), PlaceTime, PlayLength));
        }
    }

    // Build and validate the notify before touching the asset; every placement duplicates this template.
    UObject* Template = NewObject<UObject>(GetTransientPackage(), NotifyClass);
    const TSharedPtr<FJsonObject>* Properties = nullptr;
    if (Arguments->TryGetObjectField(TEXT("properties"), Properties))
    {
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Properties)->Values)
        {
            if (!NotifyApplyProperty(Template, Pair.Key, Pair.Value, Error))
            {
                return NotifyMakeError(TEXT("invalid_arguments"), Error);
            }
        }
    }

    double TriggerChance = 1.0;
    Arguments->TryGetNumberField(TEXT("trigger_chance"), TriggerChance);
    bool bSkipDuplicates = true;
    Arguments->TryGetBoolField(TEXT("skip_duplicates"), bSkipDuplicates);
    bool bCreateTrack = true;
    Arguments->TryGetBoolField(TEXT("create_track"), bCreateTrack);
    FString TrackName;
    Arguments->TryGetStringField(TEXT("track"), TrackName);

    TArray<TSharedPtr<FJsonValue>> Skipped;
    TArray<float> ToAdd;
    for (const float PlaceTime : Times)
    {
        if (bSkipDuplicates && NotifyHasDuplicate(Animation, Template, PlaceTime))
        {
            TSharedPtr<FJsonObject> Skip = MakeShared<FJsonObject>();
            Skip->SetNumberField(TEXT("time"), PlaceTime);
            Skip->SetStringField(TEXT("reason"), TEXT("same notify with the same properties already at this time"));
            Skipped.Add(MakeShared<FJsonValueObject>(Skip));
            continue;
        }
        ToAdd.Add(PlaceTime);
    }

    TArray<FGuid> AddedGuids;
    if (ToAdd.Num() > 0)
    {
        const FScopedTransaction Transaction(FText::FromString(TEXT("Add anim notify")));
        Animation->Modify();

        const int32 TrackIndex = NotifyResolveTrack(Animation, TrackName, bCreateTrack, Error);
        if (TrackIndex == INDEX_NONE)
        {
            return NotifyMakeError(TEXT("invalid_arguments"), Error);
        }

        for (const float PlaceTime : ToAdd)
        {
            UObject* Instance = DuplicateObject<UObject>(Template, Animation);
            Instance->SetFlags(RF_Transactional);

            FAnimNotifyEvent& Event = Animation->Notifies.AddDefaulted_GetRef();
            Event.Link(Animation, PlaceTime);
            Event.TriggerTimeOffset = GetTriggerTimeOffsetForType(Animation->CalculateOffsetForNotify(PlaceTime));
            Event.TrackIndex = TrackIndex;
            Event.NotifyTriggerChance = FMath::Clamp(static_cast<float>(TriggerChance), 0.0f, 1.0f);
            Event.Guid = FGuid::NewGuid();
            if (bIsState)
            {
                UAnimNotifyState* State = CastChecked<UAnimNotifyState>(Instance);
                Event.NotifyStateClass = State;
                Event.NotifyName = FName(*State->GetNotifyName());
                Event.SetDuration(FMath::Min(static_cast<float>(Duration), PlayLength - PlaceTime));
                Event.EndLink.Link(Animation, Event.EndLink.GetTime());
            }
            else
            {
                UAnimNotify* Notify = CastChecked<UAnimNotify>(Instance);
                Event.Notify = Notify;
                Event.NotifyName = FName(*Notify->GetNotifyName());
            }
            AddedGuids.Add(Event.Guid);
        }

        // Sorts the array by time, so indices are looked up again below.
        Animation->RefreshCacheData();
        Animation->MarkPackageDirty();
    }

    TArray<TSharedPtr<FJsonValue>> Added;
    for (int32 Index = 0; Index < Animation->Notifies.Num(); ++Index)
    {
        if (AddedGuids.Contains(Animation->Notifies[Index].Guid))
        {
            Added.Add(MakeShared<FJsonValueObject>(NotifyEventToJson(Animation, Animation->Notifies[Index], Index, true)));
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), Animation->GetPathName());
    Data->SetArrayField(TEXT("added"), Added);
    Data->SetArrayField(TEXT("skipped"), Skipped);
    Data->SetNumberField(TEXT("notify_count"), Animation->Notifies.Num());
    Data->SetBoolField(TEXT("dirty"), Added.Num() > 0);
    if (Added.Num() > 0 && NotifyShouldSave(Arguments))
    {
        Data->SetBoolField(TEXT("saved"), ToolSaveAsset(Arguments)->GetBoolField(TEXT("ok")));
    }
    return NotifyMakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolRemoveAnimNotify(const TSharedPtr<FJsonObject>& Arguments)
{
    FString Error;
    UAnimSequenceBase* Animation = NotifyLoadAnimation(Arguments, Error);
    if (!Animation)
    {
        return NotifyMakeError(TEXT("load_failed"), Error);
    }
    if (!Animation->GetPathName().StartsWith(TEXT("/Game/")))
    {
        return NotifyMakeError(TEXT("invalid_arguments"), TEXT("Notify writes are limited to /Game assets."));
    }

    bool bConfirm = false;
    Arguments->TryGetBoolField(TEXT("confirm"), bConfirm);
    if (!bConfirm)
    {
        return NotifyMakeError(TEXT("confirmation_required"), TEXT("Removing notifies is destructive; pass confirm=true."));
    }

    // Select by explicit indices (from inspect_anim_notifies), or by class with an optional time.
    TSet<int32> Selected;
    const TArray<TSharedPtr<FJsonValue>>* IndexArray = nullptr;
    double SingleIndex = 0.0;
    FString ClassFilter;
    if (Arguments->TryGetArrayField(TEXT("indices"), IndexArray) || Arguments->TryGetNumberField(TEXT("index"), SingleIndex))
    {
        TArray<int32> Requested;
        if (IndexArray)
        {
            for (const TSharedPtr<FJsonValue>& Value : *IndexArray)
            {
                Requested.Add(static_cast<int32>(Value->AsNumber()));
            }
        }
        else
        {
            Requested.Add(static_cast<int32>(SingleIndex));
        }
        for (const int32 Index : Requested)
        {
            if (!Animation->Notifies.IsValidIndex(Index))
            {
                return NotifyMakeError(TEXT("invalid_arguments"),
                    FString::Printf(TEXT("Notify index %d is outside 0..%d."), Index, Animation->Notifies.Num() - 1));
            }
            Selected.Add(Index);
        }
    }
    else if (Arguments->TryGetStringField(TEXT("class"), ClassFilter) && !ClassFilter.IsEmpty())
    {
        double Time = 0.0;
        const bool bHasTime = Arguments->TryGetNumberField(TEXT("time"), Time);
        for (int32 Index = 0; Index < Animation->Notifies.Num(); ++Index)
        {
            const FAnimNotifyEvent& Event = Animation->Notifies[Index];
            if (NotifyEventMatchesClass(Event, ClassFilter) &&
                (!bHasTime || FMath::IsNearlyEqual(Event.GetTime(), static_cast<float>(Time), NotifyTimeTolerance)))
            {
                Selected.Add(Index);
            }
        }
    }
    else
    {
        return NotifyMakeError(TEXT("invalid_arguments"), TEXT("Pass 'index', 'indices' or 'class' (with optional 'time')."));
    }

    TArray<TSharedPtr<FJsonValue>> Removed;
    if (Selected.Num() > 0)
    {
        TArray<int32> Ordered = Selected.Array();
        Ordered.Sort(TGreater<int32>());
        for (const int32 Index : Ordered)
        {
            Removed.Insert(MakeShared<FJsonValueObject>(NotifyEventToJson(Animation, Animation->Notifies[Index], Index, true)), 0);
        }

        const FScopedTransaction Transaction(FText::FromString(TEXT("Remove anim notify")));
        Animation->Modify();
        for (const int32 Index : Ordered)
        {
            Animation->Notifies.RemoveAt(Index);
        }
        Animation->RefreshCacheData();
        Animation->MarkPackageDirty();
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), Animation->GetPathName());
    Data->SetArrayField(TEXT("removed"), Removed);
    Data->SetNumberField(TEXT("notify_count"), Animation->Notifies.Num());
    Data->SetBoolField(TEXT("dirty"), Removed.Num() > 0);
    if (Removed.Num() > 0 && NotifyShouldSave(Arguments))
    {
        Data->SetBoolField(TEXT("saved"), ToolSaveAsset(Arguments)->GetBoolField(TEXT("ok")));
    }
    return NotifyMakeOk(Data);
}

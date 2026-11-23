// Niagara tools for the bridge.
//
// Kept in its own translation unit so LLMBlueprintBridgeServer.cpp does not grow further.
//
// There are two write paths here and they are not interchangeable:
//
//   - User parameters live on UNiagaraSystem::GetExposedParameters(). These are what the
//     "User Parameters" panel shows and what a spawning component can override per instance.
//
//   - Module inputs that were left as plain literals live in UNiagaraScript::RapidIterationParameters
//     under "Constants.<Emitter>.<Module>.<Input>". Writing one is what the stack UI does when you
//     type a number into a module input, and by design it does not need a recompile - that is what
//     "rapid iteration" means. An input driven by a dynamic input or a curve is not in this store
//     and cannot be reached from here; the tools report it as not found rather than guessing.
//
// Emitter removal is deliberately absent. Removing a handle also has to rebuild the system spawn
// and update graphs through FNiagaraStackGraphUtilities::RebuildEmitterNodes, which NiagaraEditor
// does not export, and a system left with dangling emitter nodes will not compile. Disabling an
// emitter is exported, reversible, and does the same job for iteration.

#include "LLMBlueprintBridgeServer.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/PackageName.h"
#include "NiagaraCommon.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraParameterStore.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraScript.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "NiagaraUserRedirectionParameterStore.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace
{
    TSharedRef<FJsonObject> NiagaraMakeError(const FString& Code, const FString& Message)
    {
        TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
        Root->SetBoolField(TEXT("ok"), false);
        Root->SetStringField(TEXT("code"), Code);
        Root->SetStringField(TEXT("message"), Message);
        return Root;
    }

    TSharedRef<FJsonObject> NiagaraMakeOk(const TSharedPtr<FJsonObject>& Data)
    {
        TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
        Root->SetBoolField(TEXT("ok"), true);
        if (Data.IsValid())
        {
            Root->SetObjectField(TEXT("data"), Data);
        }
        return Root;
    }

    FString NiagaraNormalizeObjectPath(FString AssetPath)
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

    UNiagaraSystem* NiagaraLoadSystem(const TSharedPtr<FJsonObject>& Arguments, const TCHAR* Field, FString& OutError)
    {
        FString AssetPath;
        if (!Arguments.IsValid() || !Arguments->TryGetStringField(Field, AssetPath) || AssetPath.IsEmpty())
        {
            OutError = FString::Printf(TEXT("Missing required string field '%s'."), Field);
            return nullptr;
        }

        const FString ObjectPath = NiagaraNormalizeObjectPath(AssetPath);
        UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *ObjectPath);
        if (!System)
        {
            OutError = FString::Printf(TEXT("Could not load a NiagaraSystem at '%s'."), *ObjectPath);
        }
        return System;
    }

    /**
     * Refuses to write a system that is open in the Niagara editor.
     *
     * An open asset editor owns a FNiagaraSystemViewModel that caches the emitter handles and the
     * stack built from them. Mutating the system underneath it leaves that cache pointing at freed
     * data, and the next thing the editor touches dereferences it - an access violation that takes
     * the whole editor down, not a failed tool call. Writes that only change a rapid iteration
     * value do not crash, but the open editor neither shows them nor keeps them: saving from that
     * window writes the view model's state straight back over them.
     *
     * There is no way to push these changes through the view model from here, so the honest answer
     * is to make the caller close the window.
     */
    bool NiagaraIsOpenInEditor(UObject* Asset)
    {
        if (!GEditor || !Asset)
        {
            return false;
        }

        UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
        return AssetEditorSubsystem != nullptr
            && AssetEditorSubsystem->FindEditorForAsset(Asset, /*bFocusIfOpen*/ false) != nullptr;
    }

    TSharedRef<FJsonObject> NiagaraMakeOpenInEditorError(const UNiagaraSystem& System)
    {
        return NiagaraMakeError(TEXT("asset_editor_open"),
            FString::Printf(
                TEXT("'%s' is open in the Niagara editor. Close that window before writing to it: editing the asset ")
                TEXT("behind an open editor crashes it on structural changes and silently loses rapid iteration writes."),
                *System.GetName()));
    }

    FString NiagaraEmitterNameList(const UNiagaraSystem& System)
    {
        TArray<FString> Names;
        for (const FNiagaraEmitterHandle& Handle : System.GetEmitterHandles())
        {
            Names.Add(Handle.GetName().ToString());
        }
        return FString::Join(Names, TEXT(", "));
    }

    int32 NiagaraFindEmitterIndex(const UNiagaraSystem& System, const FString& EmitterName, FString& OutError)
    {
        const TArray<FNiagaraEmitterHandle>& Handles = System.GetEmitterHandles();
        for (int32 Index = 0; Index < Handles.Num(); ++Index)
        {
            if (Handles[Index].GetName().ToString().Equals(EmitterName, ESearchCase::IgnoreCase))
            {
                return Index;
            }
        }

        OutError = FString::Printf(TEXT("Emitter '%s' not found in '%s'. Emitters: %s."),
            *EmitterName, *System.GetName(), *NiagaraEmitterNameList(System));
        return INDEX_NONE;
    }

    FString NiagaraScriptLabel(const UNiagaraScript* Script)
    {
        if (!Script)
        {
            return FString();
        }
        if (const UEnum* UsageEnum = StaticEnum<ENiagaraScriptUsage>())
        {
            return UsageEnum->GetNameStringByValue(static_cast<int64>(Script->GetUsage()));
        }
        return Script->GetName();
    }

    /**
     * Scripts a module-input write may land in.
     *
     * "System" targets the system spawn/update scripts; anything else is an emitter name. Emitter
     * scripts are gathered with bCompilableOnly off, because the emitter spawn/update scripts hold
     * inputs (spawn rate, loop duration) that are just as worth writing as the particle ones.
     */
    bool NiagaraCollectScripts(UNiagaraSystem& System, const FString& EmitterName, TArray<UNiagaraScript*>& OutScripts, FString& OutError)
    {
        if (EmitterName.Equals(TEXT("System"), ESearchCase::IgnoreCase))
        {
            OutScripts.Add(System.GetSystemSpawnScript());
            OutScripts.Add(System.GetSystemUpdateScript());
            OutScripts.RemoveAll([](const UNiagaraScript* Script) { return Script == nullptr; });
            return true;
        }

        const int32 EmitterIndex = NiagaraFindEmitterIndex(System, EmitterName, OutError);
        if (EmitterIndex == INDEX_NONE)
        {
            return false;
        }

        FVersionedNiagaraEmitterData* EmitterData = System.GetEmitterHandle(EmitterIndex).GetEmitterData();
        if (!EmitterData)
        {
            OutError = FString::Printf(TEXT("Emitter '%s' has no emitter data; stateless emitters are not supported."), *EmitterName);
            return false;
        }

        EmitterData->GetScripts(OutScripts, /*bCompilableOnly*/ false);
        OutScripts.RemoveAll([](const UNiagaraScript* Script) { return Script == nullptr; });
        return true;
    }

    /** How many float channels a type occupies, or 0 when it is not a float vector. */
    int32 NiagaraFloatChannelCount(const FNiagaraTypeDefinition& Type)
    {
        if (Type == FNiagaraTypeDefinition::GetVec2Def())
        {
            return 2;
        }
        if (Type == FNiagaraTypeDefinition::GetVec3Def() || Type == FNiagaraTypeDefinition::GetPositionDef())
        {
            return 3;
        }
        if (Type == FNiagaraTypeDefinition::GetVec4Def() || Type == FNiagaraTypeDefinition::GetColorDef() ||
            Type == FNiagaraTypeDefinition::GetQuatDef())
        {
            return 4;
        }
        return 0;
    }

    /**
     * Splits a vector written as a string into its component tokens.
     *
     * MCP clients serialise a value with no declared schema type as a string, so a vector arrives
     * here as text rather than as a JSON array however it was written. The rest of the bridge
     * already accepts that through FVector::InitFromString, so the same spellings are accepted
     * here: "X=0,Y=0,Z=1", "0 0 1", "[0,0,1]", "(0, 0, 1)".
     */
    void NiagaraSplitVectorString(const FString& Text, TArray<FString>& OutTokens)
    {
        FString Cleaned = Text;
        Cleaned.ReplaceInline(TEXT("["), TEXT(" "));
        Cleaned.ReplaceInline(TEXT("]"), TEXT(" "));
        Cleaned.ReplaceInline(TEXT("("), TEXT(" "));
        Cleaned.ReplaceInline(TEXT(")"), TEXT(" "));
        Cleaned.ReplaceInline(TEXT(","), TEXT(" "));
        Cleaned.ParseIntoArray(OutTokens, TEXT(" "), /*InCullEmpty*/ true);

        // Drop an "X="/"R=" style label so the token is left as a bare number.
        for (FString& Token : OutTokens)
        {
            int32 EqualsIndex = INDEX_NONE;
            if (Token.FindChar(TEXT('='), EqualsIndex))
            {
                Token.MidInline(EqualsIndex + 1);
            }
            Token.TrimStartAndEndInline();
        }
    }

    /**
     * Reads Count float channels out of a JSON value.
     *
     * Accepts [x,y,z], {x,y,z} / {r,g,b,a}, a vector spelled as a string, or a bare number that
     * fills every channel - the last form is what you want for a uniform scale. MinCount lets a
     * colour be written as three components with alpha left at whatever the caller pre-seeded
     * into Out.
     */
    bool NiagaraReadFloats(const TSharedPtr<FJsonValue>& Value, int32 Count, int32 MinCount, float* Out, FString& OutError)
    {
        if (!Value.IsValid())
        {
            OutError = TEXT("Missing required field 'value'.");
            return false;
        }

        double Number = 0.0;
        if (Value->TryGetNumber(Number))
        {
            for (int32 Index = 0; Index < Count; ++Index)
            {
                Out[Index] = static_cast<float>(Number);
            }
            return true;
        }

        const TArray<TSharedPtr<FJsonValue>>* ArrayValue = nullptr;
        if (Value->TryGetArray(ArrayValue))
        {
            if (ArrayValue->Num() < MinCount)
            {
                OutError = FString::Printf(TEXT("Expected at least %d numbers, got %d."), MinCount, ArrayValue->Num());
                return false;
            }
            const int32 Available = FMath::Min(Count, ArrayValue->Num());
            for (int32 Index = 0; Index < Available; ++Index)
            {
                double Component = 0.0;
                if (!(*ArrayValue)[Index].IsValid() || !(*ArrayValue)[Index]->TryGetNumber(Component))
                {
                    OutError = FString::Printf(TEXT("Element %d is not a number."), Index);
                    return false;
                }
                Out[Index] = static_cast<float>(Component);
            }
            return true;
        }

        const TSharedPtr<FJsonObject>* ObjectValue = nullptr;
        if (Value->TryGetObject(ObjectValue) && ObjectValue != nullptr && (*ObjectValue).IsValid())
        {
            static const TCHAR* Xyzw[] = { TEXT("x"), TEXT("y"), TEXT("z"), TEXT("w") };
            static const TCHAR* Rgba[] = { TEXT("r"), TEXT("g"), TEXT("b"), TEXT("a") };
            for (int32 Index = 0; Index < Count; ++Index)
            {
                double Component = 0.0;
                if ((*ObjectValue)->TryGetNumberField(Xyzw[Index], Component) ||
                    (*ObjectValue)->TryGetNumberField(Rgba[Index], Component))
                {
                    Out[Index] = static_cast<float>(Component);
                }
                else if (Index < MinCount)
                {
                    OutError = FString::Printf(TEXT("Missing component '%s'/'%s'."), Xyzw[Index], Rgba[Index]);
                    return false;
                }
            }
            return true;
        }

        FString Text;
        if (Value->TryGetString(Text))
        {
            TArray<FString> Tokens;
            NiagaraSplitVectorString(Text, Tokens);
            if (Tokens.Num() < MinCount)
            {
                OutError = FString::Printf(TEXT("Expected at least %d numbers in '%s', found %d."),
                    MinCount, *Text, Tokens.Num());
                return false;
            }

            const int32 Available = FMath::Min(Count, Tokens.Num());
            for (int32 Index = 0; Index < Available; ++Index)
            {
                if (!Tokens[Index].IsNumeric())
                {
                    OutError = FString::Printf(TEXT("Component %d ('%s') is not a number."), Index, *Tokens[Index]);
                    return false;
                }
                Out[Index] = FCString::Atof(*Tokens[Index]);
            }
            return true;
        }

        OutError = TEXT("'value' must be a number, an array of numbers, an object of components, or a vector string.");
        return false;
    }

    TSharedPtr<FJsonValue> NiagaraParameterToJson(const FNiagaraParameterStore& Store, const FNiagaraVariableBase& Variable)
    {
        const uint8* Data = Store.GetParameterData(Variable);
        if (!Data)
        {
            return nullptr;
        }

        const FNiagaraTypeDefinition& Type = Variable.GetType();
        if (Type == FNiagaraTypeDefinition::GetFloatDef())
        {
            return MakeShared<FJsonValueNumber>(*reinterpret_cast<const float*>(Data));
        }
        if (Type == FNiagaraTypeDefinition::GetIntDef())
        {
            return MakeShared<FJsonValueNumber>(*reinterpret_cast<const int32*>(Data));
        }
        if (Type == FNiagaraTypeDefinition::GetBoolDef())
        {
            return MakeShared<FJsonValueBoolean>(reinterpret_cast<const FNiagaraBool*>(Data)->GetValue());
        }

        const int32 Channels = NiagaraFloatChannelCount(Type);
        if (Channels > 0)
        {
            const float* Floats = reinterpret_cast<const float*>(Data);
            TArray<TSharedPtr<FJsonValue>> Components;
            for (int32 Index = 0; Index < Channels; ++Index)
            {
                Components.Add(MakeShared<FJsonValueNumber>(Floats[Index]));
            }
            return MakeShared<FJsonValueArray>(Components);
        }

        return nullptr;
    }

    bool NiagaraWriteParameter(FNiagaraParameterStore& Store,
        const FNiagaraVariableBase& Variable,
        const TSharedPtr<FJsonValue>& Value,
        FString& OutError)
    {
        const FNiagaraTypeDefinition& Type = Variable.GetType();
        const FNiagaraVariable Parameter(Type, Variable.GetName());

        if (Type == FNiagaraTypeDefinition::GetFloatDef())
        {
            float Scalar = 0.0f;
            if (!NiagaraReadFloats(Value, 1, 1, &Scalar, OutError))
            {
                return false;
            }
            Store.SetParameterData(reinterpret_cast<const uint8*>(&Scalar), Parameter, /*bAdd*/ false);
            return true;
        }

        if (Type == FNiagaraTypeDefinition::GetIntDef())
        {
            double Number = 0.0;
            if (!Value.IsValid() || !Value->TryGetNumber(Number))
            {
                OutError = TEXT("'value' must be a number for an int parameter.");
                return false;
            }
            const int32 Integer = static_cast<int32>(FMath::RoundToInt(Number));
            Store.SetParameterData(reinterpret_cast<const uint8*>(&Integer), Parameter, /*bAdd*/ false);
            return true;
        }

        if (Type == FNiagaraTypeDefinition::GetBoolDef())
        {
            bool bFlag = false;
            double Number = 0.0;
            if (Value.IsValid() && Value->TryGetBool(bFlag))
            {
                // Parsed straight from a JSON bool.
            }
            else if (Value.IsValid() && Value->TryGetNumber(Number))
            {
                bFlag = !FMath::IsNearlyZero(Number);
            }
            else
            {
                OutError = TEXT("'value' must be a bool or a number for a bool parameter.");
                return false;
            }

            FNiagaraBool Flag;
            Flag.SetValue(bFlag);
            Store.SetParameterData(reinterpret_cast<const uint8*>(&Flag), Parameter, /*bAdd*/ false);
            return true;
        }

        const int32 Channels = NiagaraFloatChannelCount(Type);
        if (Channels == 0)
        {
            OutError = FString::Printf(TEXT("Parameter '%s' has type '%s', which these tools cannot write."),
                *Variable.GetName().ToString(), *Type.GetName());
            return false;
        }

        // Alpha defaults to opaque, and a quat defaults to identity, so a colour or rotation may be
        // written with three components without silently becoming transparent or degenerate.
        float Components[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        const bool bLastChannelHasDefault =
            Type == FNiagaraTypeDefinition::GetColorDef() || Type == FNiagaraTypeDefinition::GetQuatDef();
        const int32 MinCount = bLastChannelHasDefault ? Channels - 1 : Channels;
        if (!NiagaraReadFloats(Value, Channels, MinCount, Components, OutError))
        {
            return false;
        }

        if (Type.GetSize() != Channels * static_cast<int32>(sizeof(float)))
        {
            OutError = FString::Printf(TEXT("Type '%s' is %d bytes, which does not match %d floats."),
                *Type.GetName(), Type.GetSize(), Channels);
            return false;
        }

        Store.SetParameterData(reinterpret_cast<const uint8*>(Components), Parameter, /*bAdd*/ false);
        return true;
    }

    /** One script's copy of a rapid iteration parameter. */
    struct FNiagaraResolvedParameter
    {
        UNiagaraScript* Script = nullptr;
        FNiagaraVariableBase Variable;
    };

    /**
     * Finds every copy of one rapid iteration parameter across a set of scripts.
     *
     * The caller may pass the full "Constants.<Emitter>.<Module>.<Input>" name or any trailing part
     * of it, e.g. "AddVelocityInCone.Cone Angle". A partial name that matches more than one
     * parameter *name* is rejected rather than resolved arbitrarily - picking one silently is how
     * you end up tuning the wrong emitter.
     *
     * One name can still live in several scripts. A module sitting in both the particle spawn and
     * particle update stacks keeps a copy in each store, and interpolated spawn replays the update
     * logic, so writing only the first one found leaves the other at its old value and the change
     * appears not to take - a speed limit still clamping at its old ceiling, for instance. Every
     * copy of the resolved name is returned so the caller can write them all.
     */
    bool NiagaraFindRapidIterationParameters(const TArray<UNiagaraScript*>& Scripts,
        const FString& ParameterName,
        TArray<FNiagaraResolvedParameter>& OutResolved,
        FString& OutError)
    {
        TArray<FString> MatchedNames;
        FString ExactName;

        for (const UNiagaraScript* Script : Scripts)
        {
            for (const FNiagaraVariableWithOffset& Variable : Script->RapidIterationParameters.ReadParameterVariables())
            {
                const FString Name = Variable.GetName().ToString();
                if (Name.Equals(ParameterName, ESearchCase::IgnoreCase))
                {
                    ExactName = Name;
                }
                else if (Name.EndsWith(FString(TEXT(".")) + ParameterName, ESearchCase::IgnoreCase))
                {
                    MatchedNames.AddUnique(Name);
                }
            }
        }

        // An exact name wins outright; a partial name has to land on exactly one.
        FString TargetName = ExactName;
        if (TargetName.IsEmpty())
        {
            if (MatchedNames.Num() > 1)
            {
                OutError = FString::Printf(TEXT("Parameter '%s' is ambiguous. Candidates: %s."),
                    *ParameterName, *FString::Join(MatchedNames, TEXT(", ")));
                return false;
            }
            if (MatchedNames.Num() == 0)
            {
                OutError = FString::Printf(
                    TEXT("Rapid iteration parameter '%s' not found. Inputs driven by a dynamic input or a curve do not appear here; ")
                    TEXT("run inspect_niagara_system to list what is writable."),
                    *ParameterName);
                return false;
            }
            TargetName = MatchedNames[0];
        }

        for (UNiagaraScript* Script : Scripts)
        {
            for (const FNiagaraVariableWithOffset& Variable : Script->RapidIterationParameters.ReadParameterVariables())
            {
                if (Variable.GetName().ToString().Equals(TargetName, ESearchCase::IgnoreCase))
                {
                    FNiagaraResolvedParameter& Resolved = OutResolved.AddDefaulted_GetRef();
                    Resolved.Script = Script;
                    Resolved.Variable = Variable;
                    break;
                }
            }
        }

        return OutResolved.Num() > 0;
    }

    /**
     * Compiles the system and waits for it.
     *
     * Niagara compiles asynchronously, so a caller that saves straight after a structural change
     * would write a package whose compiled scripts are still stale. Polling here keeps the tool
     * result honest about what ended up on disk.
     */
    bool NiagaraCompileAndWait(UNiagaraSystem& System, bool bForce)
    {
        System.RequestCompile(bForce);
        return System.PollForCompilationComplete();
    }
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolInspectNiagaraSystem(const TSharedPtr<FJsonObject>& Arguments)
{
    FString Error;
    UNiagaraSystem* System = NiagaraLoadSystem(Arguments, TEXT("asset_path"), Error);
    if (!System)
    {
        return NiagaraMakeError(TEXT("load_failed"), Error);
    }

    FString EmitterFilter;
    FString Query;
    bool bIncludeConstants = true;
    int32 Limit = 200;
    if (Arguments.IsValid())
    {
        Arguments->TryGetStringField(TEXT("emitter"), EmitterFilter);
        Arguments->TryGetStringField(TEXT("query"), Query);
        Arguments->TryGetBoolField(TEXT("include_constants"), bIncludeConstants);
        Arguments->TryGetNumberField(TEXT("limit"), Limit);
    }
    Limit = FMath::Clamp(Limit, 1, 4000);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), System->GetPathName());

    TArray<TSharedPtr<FJsonValue>> Emitters;
    for (int32 Index = 0; Index < System->GetNumEmitters(); ++Index)
    {
        const FNiagaraEmitterHandle& Handle = System->GetEmitterHandle(Index);
        const FString Name = Handle.GetName().ToString();
        if (!EmitterFilter.IsEmpty() && !Name.Equals(EmitterFilter, ESearchCase::IgnoreCase))
        {
            continue;
        }

        TSharedPtr<FJsonObject> Emitter = MakeShared<FJsonObject>();
        Emitter->SetNumberField(TEXT("index"), Index);
        Emitter->SetStringField(TEXT("name"), Name);
        Emitter->SetBoolField(TEXT("enabled"), Handle.GetIsEnabled());

        if (const FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData())
        {
            Emitter->SetBoolField(TEXT("local_space"), EmitterData->bLocalSpace);
            Emitter->SetBoolField(TEXT("determinism"), EmitterData->bDeterminism);

            TArray<TSharedPtr<FJsonValue>> Renderers;
            for (const UNiagaraRendererProperties* Renderer : EmitterData->GetRenderers())
            {
                if (Renderer)
                {
                    Renderers.Add(MakeShared<FJsonValueString>(Renderer->GetClass()->GetName()));
                }
            }
            Emitter->SetArrayField(TEXT("renderers"), Renderers);
        }
        else
        {
            Emitter->SetBoolField(TEXT("stateless"), true);
        }

        Emitters.Add(MakeShared<FJsonValueObject>(Emitter));
    }
    Data->SetNumberField(TEXT("emitter_count"), System->GetNumEmitters());
    Data->SetArrayField(TEXT("emitters"), Emitters);

    const FNiagaraUserRedirectionParameterStore& UserStore = System->GetExposedParameters();
    TArray<TSharedPtr<FJsonValue>> UserParameters;
    for (const FNiagaraVariableWithOffset& Variable : UserStore.ReadParameterVariables())
    {
        const FString Name = Variable.GetName().ToString();
        if (!Query.IsEmpty() && !Name.Contains(Query))
        {
            continue;
        }

        TSharedPtr<FJsonObject> Parameter = MakeShared<FJsonObject>();
        Parameter->SetStringField(TEXT("name"), Name);
        Parameter->SetStringField(TEXT("type"), Variable.GetType().GetName());
        if (const TSharedPtr<FJsonValue> Value = NiagaraParameterToJson(UserStore, Variable))
        {
            Parameter->SetField(TEXT("value"), Value);
        }
        UserParameters.Add(MakeShared<FJsonValueObject>(Parameter));
    }
    Data->SetArrayField(TEXT("user_parameters"), UserParameters);

    if (bIncludeConstants)
    {
        TArray<TSharedPtr<FJsonValue>> Constants;
        bool bTruncated = false;

        TArray<FString> Targets;
        if (!EmitterFilter.IsEmpty())
        {
            Targets.Add(EmitterFilter);
        }
        else
        {
            Targets.Add(TEXT("System"));
            for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
            {
                Targets.Add(Handle.GetName().ToString());
            }
        }

        for (const FString& Target : Targets)
        {
            TArray<UNiagaraScript*> Scripts;
            FString CollectError;
            if (!NiagaraCollectScripts(*System, Target, Scripts, CollectError))
            {
                continue;
            }

            for (const UNiagaraScript* Script : Scripts)
            {
                const FNiagaraParameterStore& Store = Script->RapidIterationParameters;
                for (const FNiagaraVariableWithOffset& Variable : Store.ReadParameterVariables())
                {
                    const FString Name = Variable.GetName().ToString();
                    if (!Query.IsEmpty() && !Name.Contains(Query))
                    {
                        continue;
                    }
                    if (Constants.Num() >= Limit)
                    {
                        bTruncated = true;
                        break;
                    }

                    TSharedPtr<FJsonObject> Constant = MakeShared<FJsonObject>();
                    Constant->SetStringField(TEXT("emitter"), Target);
                    Constant->SetStringField(TEXT("script"), NiagaraScriptLabel(Script));
                    Constant->SetStringField(TEXT("name"), Name);
                    Constant->SetStringField(TEXT("type"), Variable.GetType().GetName());
                    if (const TSharedPtr<FJsonValue> Value = NiagaraParameterToJson(Store, Variable))
                    {
                        Constant->SetField(TEXT("value"), Value);
                    }
                    Constants.Add(MakeShared<FJsonValueObject>(Constant));
                }

                if (bTruncated)
                {
                    break;
                }
            }

            if (bTruncated)
            {
                break;
            }
        }

        Data->SetArrayField(TEXT("constants"), Constants);
        Data->SetBoolField(TEXT("constants_truncated"), bTruncated);
    }

    return NiagaraMakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolSetNiagaraUserParameter(const TSharedPtr<FJsonObject>& Arguments)
{
    FString Error;
    UNiagaraSystem* System = NiagaraLoadSystem(Arguments, TEXT("asset_path"), Error);
    if (!System)
    {
        return NiagaraMakeError(TEXT("load_failed"), Error);
    }

    if (NiagaraIsOpenInEditor(System))
    {
        return NiagaraMakeOpenInEditorError(*System);
    }

    FString ParameterName;
    if (!Arguments->TryGetStringField(TEXT("parameter"), ParameterName) || ParameterName.IsEmpty())
    {
        return NiagaraMakeError(TEXT("invalid_arguments"), TEXT("Missing required string field 'parameter'."));
    }
    if (!ParameterName.StartsWith(TEXT("User."), ESearchCase::IgnoreCase))
    {
        ParameterName = FString(TEXT("User.")) + ParameterName;
    }

    FNiagaraUserRedirectionParameterStore& Store = System->GetExposedParameters();
    const FNiagaraVariableWithOffset* Found = nullptr;
    for (const FNiagaraVariableWithOffset& Variable : Store.ReadParameterVariables())
    {
        if (Variable.GetName().ToString().Equals(ParameterName, ESearchCase::IgnoreCase))
        {
            Found = &Variable;
            break;
        }
    }

    if (!Found)
    {
        TArray<FString> Names;
        for (const FNiagaraVariableWithOffset& Variable : Store.ReadParameterVariables())
        {
            Names.Add(Variable.GetName().ToString());
        }
        return NiagaraMakeError(TEXT("not_found"), FString::Printf(TEXT("User parameter '%s' not found. Available: %s."),
            *ParameterName, *FString::Join(Names, TEXT(", "))));
    }

    const FNiagaraVariableBase Variable = *Found;
    const TSharedPtr<FJsonValue> PreviousValue = NiagaraParameterToJson(Store, Variable);

    const TSharedPtr<FJsonValue> RequestedValue = Arguments->TryGetField(TEXT("value"));
    System->Modify();
    FString WriteError;
    if (!NiagaraWriteParameter(Store, Variable, RequestedValue, WriteError))
    {
        return NiagaraMakeError(TEXT("invalid_arguments"), WriteError);
    }
    System->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), System->GetPathName());
    Data->SetStringField(TEXT("parameter"), Variable.GetName().ToString());
    Data->SetStringField(TEXT("type"), Variable.GetType().GetName());
    if (PreviousValue.IsValid())
    {
        Data->SetField(TEXT("previous_value"), PreviousValue);
    }
    if (const TSharedPtr<FJsonValue> NewValue = NiagaraParameterToJson(Store, Variable))
    {
        Data->SetField(TEXT("value"), NewValue);
    }

    bool bSave = false;
    Arguments->TryGetBoolField(TEXT("save"), bSave);
    if (bSave)
    {
        const TSharedRef<FJsonObject> SaveResult = ToolSaveAsset(Arguments);
        Data->SetBoolField(TEXT("saved"), SaveResult->GetBoolField(TEXT("ok")));
    }

    return NiagaraMakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolSetNiagaraModuleInput(const TSharedPtr<FJsonObject>& Arguments)
{
    FString Error;
    UNiagaraSystem* System = NiagaraLoadSystem(Arguments, TEXT("asset_path"), Error);
    if (!System)
    {
        return NiagaraMakeError(TEXT("load_failed"), Error);
    }

    if (NiagaraIsOpenInEditor(System))
    {
        return NiagaraMakeOpenInEditorError(*System);
    }

    FString EmitterName;
    FString ParameterName;
    if (!Arguments->TryGetStringField(TEXT("emitter"), EmitterName) || EmitterName.IsEmpty())
    {
        return NiagaraMakeError(TEXT("invalid_arguments"), TEXT("Missing required string field 'emitter' (use \"System\" for system scripts)."));
    }
    if (!Arguments->TryGetStringField(TEXT("parameter"), ParameterName) || ParameterName.IsEmpty())
    {
        return NiagaraMakeError(TEXT("invalid_arguments"), TEXT("Missing required string field 'parameter'."));
    }

    TArray<UNiagaraScript*> Scripts;
    if (!NiagaraCollectScripts(*System, EmitterName, Scripts, Error))
    {
        return NiagaraMakeError(TEXT("not_found"), Error);
    }

    TArray<FNiagaraResolvedParameter> Resolved;
    if (!NiagaraFindRapidIterationParameters(Scripts, ParameterName, Resolved, Error))
    {
        return NiagaraMakeError(Error.Contains(TEXT("ambiguous")) ? TEXT("ambiguous") : TEXT("not_found"), Error);
    }

    const TSharedPtr<FJsonValue> RequestedValue = Arguments->TryGetField(TEXT("value"));
    const TSharedPtr<FJsonValue> PreviousValue =
        NiagaraParameterToJson(Resolved[0].Script->RapidIterationParameters, Resolved[0].Variable);

    TArray<TSharedPtr<FJsonValue>> WrittenScripts;
    for (const FNiagaraResolvedParameter& Target : Resolved)
    {
        Target.Script->Modify();
        FString WriteError;
        if (!NiagaraWriteParameter(Target.Script->RapidIterationParameters, Target.Variable, RequestedValue, WriteError))
        {
            return NiagaraMakeError(TEXT("invalid_arguments"), WriteError);
        }
        WrittenScripts.Add(MakeShared<FJsonValueString>(NiagaraScriptLabel(Target.Script)));
    }
    System->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), System->GetPathName());
    Data->SetStringField(TEXT("emitter"), EmitterName);
    Data->SetArrayField(TEXT("scripts"), WrittenScripts);
    Data->SetStringField(TEXT("parameter"), Resolved[0].Variable.GetName().ToString());
    Data->SetStringField(TEXT("type"), Resolved[0].Variable.GetType().GetName());
    if (PreviousValue.IsValid())
    {
        Data->SetField(TEXT("previous_value"), PreviousValue);
    }
    if (const TSharedPtr<FJsonValue> NewValue =
        NiagaraParameterToJson(Resolved[0].Script->RapidIterationParameters, Resolved[0].Variable))
    {
        Data->SetField(TEXT("value"), NewValue);
    }

    bool bCompile = false;
    Arguments->TryGetBoolField(TEXT("compile"), bCompile);
    if (bCompile)
    {
        Data->SetBoolField(TEXT("compiled"), NiagaraCompileAndWait(*System, /*bForce*/ false));
    }

    bool bSave = false;
    Arguments->TryGetBoolField(TEXT("save"), bSave);
    if (bSave)
    {
        const TSharedRef<FJsonObject> SaveResult = ToolSaveAsset(Arguments);
        Data->SetBoolField(TEXT("saved"), SaveResult->GetBoolField(TEXT("ok")));
    }

    return NiagaraMakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolAddNiagaraEmitter(const TSharedPtr<FJsonObject>& Arguments)
{
    FString Error;
    UNiagaraSystem* System = NiagaraLoadSystem(Arguments, TEXT("asset_path"), Error);
    if (!System)
    {
        return NiagaraMakeError(TEXT("load_failed"), Error);
    }

    if (NiagaraIsOpenInEditor(System))
    {
        return NiagaraMakeOpenInEditorError(*System);
    }

    FString SourceAssetPath;
    if (!Arguments->TryGetStringField(TEXT("source_asset_path"), SourceAssetPath) || SourceAssetPath.IsEmpty())
    {
        return NiagaraMakeError(TEXT("invalid_arguments"), TEXT("Missing required string field 'source_asset_path'."));
    }

    const FString SourceObjectPath = NiagaraNormalizeObjectPath(SourceAssetPath);
    UObject* SourceAsset = LoadObject<UObject>(nullptr, *SourceObjectPath);
    if (!SourceAsset)
    {
        return NiagaraMakeError(TEXT("load_failed"),
            FString::Printf(TEXT("Could not load an asset at '%s'."), *SourceObjectPath));
    }

    // Lifting an emitter out of another *system* produces a system that cannot be saved.
    //
    // The copy keeps its inheritance: FVersionedNiagaraEmitterData::VersionedParent and
    // VersionedParentAtLastMerge still point at the emitter it came from. Inside a system that
    // emitter is a private subobject of the source package, and SavePackage rejects a reference to
    // another package's private object outright - "Illegal reference to private object ... in its
    // 'VersionData' property". The add appears to succeed and every later save of the destination
    // fails, which is a worse failure than refusing here.
    //
    // Adding from a standalone emitter asset is fine, and is what the editor's own Add Emitter menu
    // does: the parent is then that public asset, which is a legal reference. Systems copy emitters
    // between each other through the clipboard instead, which re-parents them properly - that path
    // is not reachable from here.
    if (const UNiagaraSystem* SourceSystem = Cast<UNiagaraSystem>(SourceAsset))
    {
        return NiagaraMakeError(TEXT("unsupported_source"),
            FString::Printf(
                TEXT("'%s' is a NiagaraSystem. An emitter embedded in a system cannot be copied out of it: the copy keeps ")
                TEXT("a parent reference to a private subobject of that package, and every later save of '%s' fails with ")
                TEXT("\"Illegal reference to private object\". Pass a standalone NiagaraEmitter asset instead, or copy the ")
                TEXT("emitter between the two system editors by hand."),
                *SourceSystem->GetName(), *System->GetName()));
    }

    UNiagaraEmitter* SourceEmitter = Cast<UNiagaraEmitter>(SourceAsset);
    if (!SourceEmitter)
    {
        return NiagaraMakeError(TEXT("unsupported_source"),
            FString::Printf(TEXT("'%s' is a %s. 'source_asset_path' must name a standalone NiagaraEmitter asset."),
                *SourceAsset->GetName(), *SourceAsset->GetClass()->GetName()));
    }

    // AddEmitterToSystem reaches for the system's editor data with a CastChecked, which would take
    // the editor down rather than fail this call. A system that somehow has none is rejected here.
    if (!System->GetEditorData())
    {
        return NiagaraMakeError(TEXT("add_failed"),
            FString::Printf(TEXT("System '%s' has no editor data and cannot take a new emitter."), *System->GetName()));
    }

    // Copies the emitter into this system rather than referencing the source, so retuning the new
    // emitter cannot reach back and change the asset it came from.
    const FGuid NewHandleId = FNiagaraEditorUtilities::AddEmitterToSystem(
        *System, *SourceEmitter, SourceEmitter->GetExposedVersion().VersionGuid, /*bCreateCopy*/ true);
    if (!NewHandleId.IsValid())
    {
        return NiagaraMakeError(TEXT("add_failed"), FString::Printf(TEXT("Failed to add emitter '%s' to '%s'."),
            *SourceEmitter->GetName(), *System->GetName()));
    }

    FNiagaraEmitterHandle* NewHandle = nullptr;
    for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
    {
        if (Handle.GetId() == NewHandleId)
        {
            NewHandle = &Handle;
            break;
        }
    }

    FString NewName;
    if (NewHandle && Arguments->TryGetStringField(TEXT("new_name"), NewName) && !NewName.IsEmpty())
    {
        NewHandle->SetName(FName(*NewName), *System);
    }

    System->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), System->GetPathName());
    Data->SetStringField(TEXT("source_asset_path"), SourceEmitter->GetPathName());
    Data->SetStringField(TEXT("emitter"), NewHandle ? NewHandle->GetName().ToString() : FString());
    Data->SetStringField(TEXT("handle_id"), NewHandleId.ToString());
    Data->SetNumberField(TEXT("emitter_count"), System->GetNumEmitters());

    // Structural change, so this compiles by default - an added emitter that was never compiled
    // does nothing when the system plays.
    bool bCompile = true;
    Arguments->TryGetBoolField(TEXT("compile"), bCompile);
    if (bCompile)
    {
        Data->SetBoolField(TEXT("compiled"), NiagaraCompileAndWait(*System, /*bForce*/ false));
    }

    bool bSave = false;
    Arguments->TryGetBoolField(TEXT("save"), bSave);
    if (bSave)
    {
        const TSharedRef<FJsonObject> SaveResult = ToolSaveAsset(Arguments);
        Data->SetBoolField(TEXT("saved"), SaveResult->GetBoolField(TEXT("ok")));
    }

    return NiagaraMakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolSetNiagaraEmitterProperty(const TSharedPtr<FJsonObject>& Arguments)
{
    FString Error;
    UNiagaraSystem* System = NiagaraLoadSystem(Arguments, TEXT("asset_path"), Error);
    if (!System)
    {
        return NiagaraMakeError(TEXT("load_failed"), Error);
    }

    if (NiagaraIsOpenInEditor(System))
    {
        return NiagaraMakeOpenInEditorError(*System);
    }

    FString EmitterName;
    if (!Arguments->TryGetStringField(TEXT("emitter"), EmitterName) || EmitterName.IsEmpty())
    {
        return NiagaraMakeError(TEXT("invalid_arguments"), TEXT("Missing required string field 'emitter'."));
    }

    const int32 EmitterIndex = NiagaraFindEmitterIndex(*System, EmitterName, Error);
    if (EmitterIndex == INDEX_NONE)
    {
        return NiagaraMakeError(TEXT("not_found"), Error);
    }

    FNiagaraEmitterHandle& Handle = System->GetEmitterHandle(EmitterIndex);
    TArray<FString> Applied;

    bool bEnabled = false;
    if (Arguments->TryGetBoolField(TEXT("enabled"), bEnabled))
    {
        System->Modify();
        Handle.SetIsEnabled(bEnabled, *System, /*bRecompileIfChanged*/ false);
        Applied.Add(TEXT("enabled"));
    }

    bool bLocalSpace = false;
    if (Arguments->TryGetBoolField(TEXT("local_space"), bLocalSpace))
    {
        FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
        if (!EmitterData)
        {
            return NiagaraMakeError(TEXT("unsupported_emitter"),
                FString::Printf(TEXT("Emitter '%s' has no emitter data."), *EmitterName));
        }
        if (UNiagaraEmitter* Emitter = Handle.GetInstance().Emitter)
        {
            Emitter->Modify();
        }
        EmitterData->bLocalSpace = bLocalSpace;
        Applied.Add(TEXT("local_space"));
    }

    if (Applied.Num() == 0)
    {
        return NiagaraMakeError(TEXT("invalid_arguments"), TEXT("Provide at least one of 'enabled' or 'local_space'."));
    }

    System->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), System->GetPathName());
    Data->SetStringField(TEXT("emitter"), Handle.GetName().ToString());
    Data->SetBoolField(TEXT("enabled"), Handle.GetIsEnabled());
    if (const FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData())
    {
        Data->SetBoolField(TEXT("local_space"), EmitterData->bLocalSpace);
    }
    TArray<TSharedPtr<FJsonValue>> AppliedJson;
    for (const FString& Name : Applied)
    {
        AppliedJson.Add(MakeShared<FJsonValueString>(Name));
    }
    Data->SetArrayField(TEXT("applied"), AppliedJson);

    // Both of these change how the system compiles, so this defaults on for the same reason
    // add_niagara_emitter does.
    bool bCompile = true;
    Arguments->TryGetBoolField(TEXT("compile"), bCompile);
    if (bCompile)
    {
        Data->SetBoolField(TEXT("compiled"), NiagaraCompileAndWait(*System, /*bForce*/ false));
    }

    bool bSave = false;
    Arguments->TryGetBoolField(TEXT("save"), bSave);
    if (bSave)
    {
        const TSharedRef<FJsonObject> SaveResult = ToolSaveAsset(Arguments);
        Data->SetBoolField(TEXT("saved"), SaveResult->GetBoolField(TEXT("ok")));
    }

    return NiagaraMakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolCompileNiagaraSystem(const TSharedPtr<FJsonObject>& Arguments)
{
    FString Error;
    UNiagaraSystem* System = NiagaraLoadSystem(Arguments, TEXT("asset_path"), Error);
    if (!System)
    {
        return NiagaraMakeError(TEXT("load_failed"), Error);
    }

    bool bForce = false;
    if (Arguments.IsValid())
    {
        Arguments->TryGetBoolField(TEXT("force"), bForce);
    }

    const bool bCompiled = NiagaraCompileAndWait(*System, bForce);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), System->GetPathName());
    Data->SetBoolField(TEXT("compiled"), bCompiled);
    Data->SetBoolField(TEXT("force"), bForce);

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

    return NiagaraMakeOk(Data);
}

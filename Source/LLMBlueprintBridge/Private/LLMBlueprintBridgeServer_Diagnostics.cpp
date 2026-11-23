// Console, profiling and housekeeping tools for the bridge.
//
// These let an agent measure what it changes: run console commands (CVars, stat, Trace.*) in the
// PIE or editor world, read frame timings, tail the editor log, create assets that have no
// dedicated tool, and see what is unsaved before restarting the editor.

#include "LLMBlueprintBridgeServer.h"

#include "AssetToolsModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "DynamicRHI.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/World.h"
#include "Factories/Factory.h"
#include "FileHelpers.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformOutputDevices.h"
#include "IAssetTools.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/StringOutputDevice.h"
#include "RenderTimer.h"
#include "UObject/UObjectIterator.h"

namespace
{
    TSharedRef<FJsonObject> DiagMakeError(const FString& Code, const FString& Message)
    {
        TSharedRef<FJsonObject> Response = MakeShared<FJsonObject>();
        Response->SetBoolField(TEXT("ok"), false);
        Response->SetStringField(TEXT("code"), Code);
        Response->SetStringField(TEXT("message"), Message);
        return Response;
    }

    TSharedRef<FJsonObject> DiagMakeOk(const TSharedPtr<FJsonObject>& Data)
    {
        TSharedRef<FJsonObject> Response = MakeShared<FJsonObject>();
        Response->SetBoolField(TEXT("ok"), true);
        Response->SetObjectField(TEXT("data"), Data);
        return Response;
    }

    /** "pie" needs a running session; "auto" prefers PIE and falls back to the editor world. */
    UWorld* DiagResolveWorld(const FString& Which)
    {
        if (!GEditor)
        {
            return nullptr;
        }
        UWorld* PlayWorld = GEditor->PlayWorld.Get();
        UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
        if (Which == TEXT("pie"))
        {
            return PlayWorld;
        }
        if (Which == TEXT("editor"))
        {
            return EditorWorld;
        }
        return PlayWorld ? PlayWorld : EditorWorld;
    }
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolExecConsoleCommand(const TSharedPtr<FJsonObject>& Arguments)
{
    FString Command;
    if (!Arguments->TryGetStringField(TEXT("command"), Command) || Command.TrimStartAndEnd().IsEmpty() || !GEngine)
    {
        return DiagMakeError(TEXT("invalid_arguments"), TEXT("Missing required string field 'command'."));
    }
    FString Which = TEXT("auto");
    Arguments->TryGetStringField(TEXT("world"), Which);
    UWorld* World = DiagResolveWorld(Which);
    if (!World)
    {
        return DiagMakeError(TEXT("no_world"), FString::Printf(TEXT("No '%s' world is available."), *Which));
    }

    // Several commands can be chained with ';' like the console accepts.
    TArray<FString> Commands;
    Command.ParseIntoArray(Commands, TEXT(";"));
    FStringOutputDevice Output;
    Output.SetAutoEmitLineTerminator(true);
    TArray<TSharedPtr<FJsonValue>> Handled;
    for (FString& Single : Commands)
    {
        Single.TrimStartAndEndInline();
        if (Single.IsEmpty())
        {
            continue;
        }
        bool bHandled = GEngine->Exec(World, *Single, Output);
        // Engine stats (stat unit, stat namedevents, ...) need the game viewport, which only the
        // local player's console path reaches.
        APlayerController* Controller = World->IsGameWorld() ? World->GetFirstPlayerController() : nullptr;
        if (!bHandled && Controller)
        {
            Output.Log(Controller->ConsoleCommand(Single, false));
            bHandled = true;
        }
        Handled.Add(MakeShared<FJsonValueBoolean>(bHandled));
    }

    FString Text = MoveTemp(Output);
    constexpr int32 MaxOutputChars = 20000;
    const bool bTruncated = Text.Len() > MaxOutputChars;
    if (bTruncated)
    {
        Text.LeftInline(MaxOutputChars);
    }
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("world"), World->GetPathName());
    Data->SetBoolField(TEXT("pie"), World->IsPlayInEditor());
    Data->SetArrayField(TEXT("handled"), Handled);
    Data->SetStringField(TEXT("output"), Text);
    Data->SetBoolField(TEXT("truncated"), bTruncated);
    return DiagMakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolGetFrameStats(const TSharedPtr<FJsonObject>& Arguments)
{
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    const double DeltaMs = FApp::GetDeltaTime() * 1000.0;
    Data->SetNumberField(TEXT("frame_ms"), DeltaMs);
    Data->SetNumberField(TEXT("fps"), DeltaMs > 0.0 ? 1000.0 / DeltaMs : 0.0);
    Data->SetNumberField(TEXT("game_thread_ms"), FPlatformTime::ToMilliseconds(GGameThreadTime));
    Data->SetNumberField(TEXT("render_thread_ms"), FPlatformTime::ToMilliseconds(GRenderThreadTime));
    Data->SetNumberField(TEXT("rhi_thread_ms"), FPlatformTime::ToMilliseconds(GRHIThreadTime));
    Data->SetNumberField(TEXT("gpu_ms"), FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));
    if (UWorld* PlayWorld = GEditor ? GEditor->PlayWorld.Get() : nullptr)
    {
        Data->SetStringField(TEXT("pie_world"), PlayWorld->GetPathName());
        Data->SetNumberField(TEXT("pie_time_seconds"), PlayWorld->GetTimeSeconds());
    }
    return DiagMakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolReadLog(const TSharedPtr<FJsonObject>& Arguments)
{
    int32 MaxLines = 200;
    Arguments->TryGetNumberField(TEXT("lines"), MaxLines);
    MaxLines = FMath::Clamp(MaxLines, 1, 2000);
    FString Contains;
    Arguments->TryGetStringField(TEXT("contains"), Contains);

    const FString LogFilePath = FPlatformOutputDevices::GetAbsoluteLogFilename();
    if (GLog)
    {
        GLog->Flush();
    }
    // The log is open for writing by this process, so read with shared write access.
    FString Text;
    if (!FFileHelper::LoadFileToString(Text, *LogFilePath, FFileHelper::EHashOptions::None, FILEREAD_AllowWrite))
    {
        return DiagMakeError(TEXT("read_failed"), FString::Printf(TEXT("Could not read '%s'."), *LogFilePath));
    }
    TArray<FString> Lines;
    Text.ParseIntoArrayLines(Lines, false);
    TArray<TSharedPtr<FJsonValue>> Selected;
    for (int32 Index = Lines.Num() - 1; Index >= 0 && Selected.Num() < MaxLines; --Index)
    {
        if (Contains.IsEmpty() || Lines[Index].Contains(Contains))
        {
            Selected.Insert(MakeShared<FJsonValueString>(Lines[Index]), 0);
        }
    }
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("path"), LogFilePath);
    Data->SetNumberField(TEXT("total_lines"), Lines.Num());
    Data->SetArrayField(TEXT("lines"), Selected);
    return DiagMakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolListDirtyPackages(const TSharedPtr<FJsonObject>& Arguments)
{
    TArray<UPackage*> Content;
    TArray<UPackage*> Worlds;
    FEditorFileUtils::GetDirtyContentPackages(Content);
    FEditorFileUtils::GetDirtyWorldPackages(Worlds);
    auto ToJson = [](const TArray<UPackage*>& Packages)
    {
        TArray<TSharedPtr<FJsonValue>> Values;
        for (const UPackage* Package : Packages)
        {
            if (Package)
            {
                Values.Add(MakeShared<FJsonValueString>(Package->GetName()));
            }
        }
        return Values;
    };
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetArrayField(TEXT("content"), ToJson(Content));
    Data->SetArrayField(TEXT("worlds"), ToJson(Worlds));
    Data->SetNumberField(TEXT("count"), Content.Num() + Worlds.Num());
    return DiagMakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolCreateAsset(const TSharedPtr<FJsonObject>& Arguments)
{
    FString AssetPath;
    FString ClassPath;
    if (!Arguments->TryGetStringField(TEXT("asset_path"), AssetPath) || !Arguments->TryGetStringField(TEXT("class"), ClassPath))
    {
        return DiagMakeError(TEXT("invalid_arguments"), TEXT("Needs 'asset_path' (/Game/Folder/Name) and 'class' (/Script/Module.Class)."));
    }
    FString PackageName = AssetPath;
    if (PackageName.Contains(TEXT(".")))
    {
        PackageName = FPackageName::ObjectPathToPackageName(PackageName);
    }
    if (!FPackageName::IsValidLongPackageName(PackageName))
    {
        return DiagMakeError(TEXT("invalid_arguments"), FString::Printf(TEXT("'%s' is not a valid long package name."), *PackageName));
    }
    UClass* AssetClass = LoadObject<UClass>(nullptr, *ClassPath);
    if (!AssetClass || AssetClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
    {
        return DiagMakeError(TEXT("load_failed"), FString::Printf(TEXT("Could not load a concrete class '%s'."), *ClassPath));
    }
    if (FindObject<UObject>(nullptr, *(PackageName + TEXT(".") + FPackageName::GetLongPackageAssetName(PackageName))) ||
        FPackageName::DoesPackageExist(PackageName))
    {
        return DiagMakeError(TEXT("already_exists"), FString::Printf(TEXT("'%s' already exists."), *PackageName));
    }

    // Prefer the editor's own factory so assets get the same defaults as "New Asset" in the browser.
    UFactory* Factory = nullptr;
    for (TObjectIterator<UClass> It; It; ++It)
    {
        if (!It->IsChildOf(UFactory::StaticClass()) || It->HasAnyClassFlags(CLASS_Abstract))
        {
            continue;
        }
        const UFactory* Default = It->GetDefaultObject<UFactory>();
        if (Default->SupportedClass == AssetClass && Default->CanCreateNew())
        {
            Factory = NewObject<UFactory>(GetTransientPackage(), *It);
            break;
        }
    }

    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
    const FString FolderPath = FPackageName::GetLongPackagePath(PackageName);
    UObject* Asset = AssetTools.CreateAsset(AssetName, FolderPath, AssetClass, Factory);
    if (!Asset)
    {
        return DiagMakeError(TEXT("create_failed"), FString::Printf(TEXT("Could not create '%s' of class '%s'."), *PackageName, *ClassPath));
    }
    bool bSave = true;
    Arguments->TryGetBoolField(TEXT("save"), bSave);
    bool bSaved = false;
    if (bSave)
    {
        bSaved = UEditorLoadingAndSavingUtils::SavePackages({ Asset->GetPackage() }, false);
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("object_path"), Asset->GetPathName());
    Data->SetStringField(TEXT("class"), Asset->GetClass()->GetPathName());
    Data->SetStringField(TEXT("factory"), Factory ? Factory->GetClass()->GetName() : TEXT("none"));
    Data->SetBoolField(TEXT("saved"), bSaved);
    return DiagMakeOk(Data);
}

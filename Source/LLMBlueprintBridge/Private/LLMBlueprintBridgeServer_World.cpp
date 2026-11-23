// Level placement and Play-In-Editor control for the bridge.
//
// Own translation unit, same reason as the animation tools: LLMBlueprintBridgeServer.cpp is
// already oversized. PIE tools exist so an agent can check runtime behaviour, including what the
// frame actually looks like, instead of inferring it from assets.

#include "LLMBlueprintBridgeServer.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Misc/OutputDeviceNull.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "UnrealClient.h"

namespace
{
    TSharedRef<FJsonObject> WorldMakeError(const FString& Code, const FString& Message)
    {
        TSharedRef<FJsonObject> Response = MakeShared<FJsonObject>();
        Response->SetBoolField(TEXT("ok"), false);
        Response->SetStringField(TEXT("code"), Code);
        Response->SetStringField(TEXT("message"), Message);
        return Response;
    }

    TSharedRef<FJsonObject> WorldMakeOk(const TSharedPtr<FJsonObject>& Data)
    {
        TSharedRef<FJsonObject> Response = MakeShared<FJsonObject>();
        Response->SetBoolField(TEXT("ok"), true);
        Response->SetObjectField(TEXT("data"), Data);
        return Response;
    }

    FVector WorldReadVector(const TSharedPtr<FJsonObject>& Arguments, const TCHAR* Field)
    {
        const TSharedPtr<FJsonObject>* Object = nullptr;
        FVector Value = FVector::ZeroVector;
        if (Arguments->TryGetObjectField(Field, Object))
        {
            (*Object)->TryGetNumberField(TEXT("x"), Value.X);
            (*Object)->TryGetNumberField(TEXT("y"), Value.Y);
            (*Object)->TryGetNumberField(TEXT("z"), Value.Z);
        }
        return Value;
    }

    TSharedPtr<FJsonObject> WorldVectorToJson(const FVector& Value)
    {
        TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
        Json->SetNumberField(TEXT("x"), Value.X);
        Json->SetNumberField(TEXT("y"), Value.Y);
        Json->SetNumberField(TEXT("z"), Value.Z);
        return Json;
    }

    AActor* WorldFindPieActor(UWorld* PlayWorld, const FString& Name)
    {
        for (TActorIterator<AActor> It(PlayWorld); It; ++It)
        {
            if (It->GetName().Equals(Name, ESearchCase::IgnoreCase) ||
                It->GetActorNameOrLabel().Equals(Name, ESearchCase::IgnoreCase))
            {
                return *It;
            }
        }
        return nullptr;
    }

    APawn* WorldPiePlayerPawn(UWorld* PlayWorld)
    {
        APlayerController* Controller = PlayWorld->GetFirstPlayerController();
        return Controller ? Controller->GetPawn() : nullptr;
    }
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolSpawnLevelActor(const TSharedPtr<FJsonObject>& Arguments)
{
    FString ClassPath;
    if (!Arguments->TryGetStringField(TEXT("class"), ClassPath) || ClassPath.IsEmpty())
    {
        return WorldMakeError(TEXT("invalid_arguments"), TEXT("Missing required string field 'class'."));
    }
    UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!EditorWorld)
    {
        return WorldMakeError(TEXT("spawn_failed"), TEXT("No editor world is available."));
    }
    UClass* ActorClass = LoadClass<AActor>(nullptr, *ClassPath);
    if (!ActorClass)
    {
        return WorldMakeError(TEXT("load_failed"), FString::Printf(TEXT("Could not load actor class '%s'."), *ClassPath));
    }

    const FVector Location = WorldReadVector(Arguments, TEXT("location"));
    double Yaw = 0.0;
    Arguments->TryGetNumberField(TEXT("yaw"), Yaw);

    FScopedTransaction Transaction(FText::FromString(TEXT("Spawn Level Actor")));
    AActor* Actor = EditorWorld->SpawnActor<AActor>(ActorClass, Location, FRotator(0.0, Yaw, 0.0));
    if (!Actor)
    {
        return WorldMakeError(TEXT("spawn_failed"), TEXT("SpawnActor returned null."));
    }
    FString Label;
    if (Arguments->TryGetStringField(TEXT("label"), Label) && !Label.IsEmpty())
    {
        Actor->SetActorLabel(Label);
    }
    Actor->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("object_path"), Actor->GetPathName());
    Data->SetStringField(TEXT("label"), Actor->GetActorLabel());
    Data->SetObjectField(TEXT("location"), WorldVectorToJson(Actor->GetActorLocation()));
    Data->SetBoolField(TEXT("dirty"), true);
    return WorldMakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolPieControl(const TSharedPtr<FJsonObject>& Arguments)
{
    FString Action;
    if (!Arguments->TryGetStringField(TEXT("action"), Action) || !GEditor)
    {
        return WorldMakeError(TEXT("invalid_arguments"), TEXT("Missing required string field 'action'."));
    }

    UWorld* PlayWorld = GEditor->PlayWorld;
    if (Action == TEXT("start"))
    {
        if (!PlayWorld && !GEditor->IsPlaySessionRequestQueued())
        {
            FRequestPlaySessionParams Params;
            Params.WorldType = EPlaySessionWorldType::PlayInEditor;
            GEditor->RequestPlaySession(Params);
        }
    }
    else if (Action == TEXT("stop"))
    {
        if (PlayWorld)
        {
            GEditor->RequestEndPlayMap();
        }
    }
    else if (Action == TEXT("screenshot"))
    {
        FString Path;
        if (!Arguments->TryGetStringField(TEXT("path"), Path) || Path.IsEmpty())
        {
            return WorldMakeError(TEXT("invalid_arguments"), TEXT("screenshot needs 'path'."));
        }
        IFileManager::Get().Delete(*Path, false, true, true);
        FScreenshotRequest::RequestScreenshot(Path, false, false);
    }
    else if (Action == TEXT("save_level"))
    {
        // Deferred to the core ticker: requests run inside a game thread task, and saving a level
        // with landscape from there asserts in the task graph (recursive ProcessThreadUntilIdle).
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
        {
            FEditorFileUtils::SaveCurrentLevel();
            return false;
        }));
    }
    else if (Action != TEXT("status"))
    {
        return WorldMakeError(TEXT("invalid_arguments"), TEXT("action must be start, stop, status, screenshot or save_level."));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("action"), Action);
    Data->SetBoolField(TEXT("running"), PlayWorld != nullptr);
    Data->SetBoolField(TEXT("queued"), GEditor->IsPlaySessionRequestQueued());
    if (PlayWorld)
    {
        Data->SetStringField(TEXT("world"), PlayWorld->GetPathName());
        Data->SetNumberField(TEXT("time_seconds"), PlayWorld->GetTimeSeconds());
    }
    return WorldMakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolPieCallActorFunction(const TSharedPtr<FJsonObject>& Arguments)
{
    UWorld* PlayWorld = GEditor ? GEditor->PlayWorld.Get() : nullptr;
    if (!PlayWorld)
    {
        return WorldMakeError(TEXT("pie_not_running"), TEXT("Play In Editor is not running."));
    }
    FString ActorName;
    FString Command;
    if (!Arguments->TryGetStringField(TEXT("actor"), ActorName) ||
        !Arguments->TryGetStringField(TEXT("command"), Command))
    {
        return WorldMakeError(TEXT("invalid_arguments"), TEXT("Needs 'actor' and 'command' (function name plus arguments)."));
    }
    AActor* Actor = WorldFindPieActor(PlayWorld, ActorName);
    if (!Actor)
    {
        return WorldMakeError(TEXT("actor_not_found"), FString::Printf(TEXT("No PIE actor named '%s'."), *ActorName));
    }
    FOutputDeviceNull Output;
    const bool bCalled = Actor->CallFunctionByNameWithArguments(*Command, Output, nullptr, true);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("actor"), Actor->GetPathName());
    Data->SetBoolField(TEXT("called"), bCalled);
    return WorldMakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolPieGetActorState(const TSharedPtr<FJsonObject>& Arguments)
{
    UWorld* PlayWorld = GEditor ? GEditor->PlayWorld.Get() : nullptr;
    if (!PlayWorld)
    {
        return WorldMakeError(TEXT("pie_not_running"), TEXT("Play In Editor is not running."));
    }

    FString ActorName;
    AActor* Actor = Arguments->TryGetStringField(TEXT("actor"), ActorName) && !ActorName.IsEmpty()
        ? WorldFindPieActor(PlayWorld, ActorName)
        : Cast<AActor>(WorldPiePlayerPawn(PlayWorld));
    if (!Actor)
    {
        return WorldMakeError(TEXT("actor_not_found"), TEXT("Actor not found, or the player has no pawn yet."));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("actor"), Actor->GetPathName());
    Data->SetObjectField(TEXT("location"), WorldVectorToJson(Actor->GetActorLocation()));
    Data->SetNumberField(TEXT("yaw"), Actor->GetActorRotation().Yaw);
    Data->SetNumberField(TEXT("time_seconds"), PlayWorld->GetTimeSeconds());

    const ACharacter* Character = Cast<ACharacter>(Actor);
    USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : Actor->FindComponentByClass<USkeletalMeshComponent>();
    const TArray<TSharedPtr<FJsonValue>>* BoneNames = nullptr;
    if (Mesh && Arguments->TryGetArrayField(TEXT("bones"), BoneNames))
    {
        TSharedPtr<FJsonObject> Bones = MakeShared<FJsonObject>();
        for (const TSharedPtr<FJsonValue>& BoneName : *BoneNames)
        {
            const FName Bone(*BoneName->AsString());
            if (Mesh->GetBoneIndex(Bone) != INDEX_NONE)
            {
                Bones->SetObjectField(Bone.ToString(), WorldVectorToJson(Mesh->GetSocketLocation(Bone)));
            }
        }
        Data->SetObjectField(TEXT("bones"), Bones);
    }

    if (const APlayerController* Controller = PlayWorld->GetFirstPlayerController())
    {
        Data->SetStringField(TEXT("view_target"), GetNameSafe(Controller->GetViewTarget()));
        if (Controller->PlayerCameraManager)
        {
            Data->SetObjectField(TEXT("camera_location"),
                WorldVectorToJson(Controller->PlayerCameraManager->GetCameraLocation()));
        }
    }
    return WorldMakeOk(Data);
}

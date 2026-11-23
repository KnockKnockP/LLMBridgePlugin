#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "LLMBlueprintBridgeSettings.generated.h"

UCLASS(Config=EditorPerProjectUserSettings, DefaultConfig, meta=(DisplayName="LLM Blueprint Bridge"))
class LLMBLUEPRINTBRIDGE_API ULLMBlueprintBridgeSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	ULLMBlueprintBridgeSettings();

	UPROPERTY(Config, EditAnywhere, Category="Server")
	bool bAutoStart = true;

	UPROPERTY(Config, EditAnywhere, Category="Server", meta=(ClampMin="1024", ClampMax="65535"))
	int32 Port = 9877;

	/** MCP(JSON-RPC 2.0) 엔드포인트를 함께 연다. 기존 REST 경로는 그대로 유지된다. */
	UPROPERTY(Config, EditAnywhere, Category="MCP", meta=(DisplayName="Enable MCP Endpoint"))
	bool bEnableMcpEndpoint = true;

	/** MCP 엔드포인트 경로. UE5.8 의 Unreal MCP 와 같은 기본값을 쓴다. */
	UPROPERTY(Config, EditAnywhere, Category="MCP", meta=(DisplayName="MCP Url Path", EditCondition="bEnableMcpEndpoint"))
	FString McpUrlPath = TEXT("/mcp");

	UPROPERTY(Config, EditAnywhere, Category="Security")
	bool bLocalRequestsOnly = true;

	UPROPERTY(Config, EditAnywhere, Category="Security", meta=(PasswordField=true))
	FString AuthToken;

	virtual FName GetCategoryName() const override;
};

#pragma once

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"

class FJsonObject;

/**
 * 툴 실행 경계.
 *
 * MCP 엔드포인트가 FLLMBlueprintBridgeServer 의 구현을 알지 않아도 되도록 갈라 둔 인터페이스다.
 * UE5.8 로 올라가 Epic 의 IModelContextProtocolTool 로 이식할 때, 이 경계가 그대로 어댑터 지점이 된다.
 */
class ILLMBlueprintBridgeToolInvoker
{
public:
	virtual ~ILLMBlueprintBridgeToolInvoker() = default;

	/**
	 * 이름으로 툴을 실행한다. 반드시 게임 스레드에서 호출해야 한다.
	 * @return 성공 시 {"ok":true,"data":{...}}, 실패 시 {"ok":false,"code":"...","message":"..."}
	 */
	virtual TSharedRef<FJsonObject> InvokeToolByName(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments) = 0;
};

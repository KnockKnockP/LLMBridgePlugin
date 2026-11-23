#pragma once

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"

class FJsonObject;

/** MCP tools/list 한 항목. */
struct FLLMBlueprintBridgeToolDescriptor
{
	/** 툴 이름. MCP 규격상 [A-Za-z0-9_.-] 1~128자. */
	FString Name;

	/** 무엇을 하는 툴인지. Docs/API.md 항목을 그대로 옮긴 것이다. */
	FString Description;

	/** JSON Schema. 항상 {"type":"object"} 형태의 유효한 객체다. */
	TSharedPtr<FJsonObject> InputSchema;
};

/**
 * 브리지가 노출하는 툴의 이름/설명/입력 스키마 목록.
 *
 * 표 자체는 Docs/API.md 와 InvokeTool() 디스패치에서 생성된다.
 * 갱신: python Plugins/LLMBlueprintBridge/Tools/generate_tool_catalog.py
 */
class FLLMBlueprintBridgeToolCatalog
{
public:
	/** 최초 호출 시 생성 테이블을 파싱한다. 이후로는 캐시된 배열을 돌려준다. */
	static const TArray<FLLMBlueprintBridgeToolDescriptor>& Get();

	/** 이름으로 찾는다(대소문자 무시). 없으면 nullptr. */
	static const FLLMBlueprintBridgeToolDescriptor* Find(const FString& ToolName);
};

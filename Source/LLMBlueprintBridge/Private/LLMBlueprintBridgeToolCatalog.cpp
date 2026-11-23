#include "LLMBlueprintBridgeToolCatalog.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	/** 생성 테이블 한 줄. LLMBlueprintBridgeToolCatalog.gen.inl 이 이 형태로 채워진다. */
	struct FLLMBlueprintBridgeGeneratedTool
	{
		const TCHAR* Name;
		const TCHAR* Description;
		const TCHAR* InputSchemaJson;
	};

#include "LLMBlueprintBridgeToolCatalog.gen.inl"

	TSharedPtr<FJsonObject> ParseSchemaOrDefault(const TCHAR* SchemaJson)
	{
		TSharedPtr<FJsonObject> Schema;
		if (SchemaJson != nullptr)
		{
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FString(SchemaJson));
			FJsonSerializer::Deserialize(Reader, Schema);
		}

		// MCP 규격상 inputSchema 는 필수이며 {"type":"object"} 여야 한다.
		if (!Schema.IsValid())
		{
			Schema = MakeShared<FJsonObject>();
			Schema->SetStringField(TEXT("type"), TEXT("object"));
		}
		return Schema;
	}

	TArray<FLLMBlueprintBridgeToolDescriptor> BuildCatalog()
	{
		TArray<FLLMBlueprintBridgeToolDescriptor> Descriptors;
		Descriptors.Reserve(UE_ARRAY_COUNT(GLLMBlueprintBridgeGeneratedTools));

		for (const FLLMBlueprintBridgeGeneratedTool& Generated : GLLMBlueprintBridgeGeneratedTools)
		{
			FLLMBlueprintBridgeToolDescriptor& Descriptor = Descriptors.AddDefaulted_GetRef();
			Descriptor.Name = Generated.Name;
			Descriptor.Description = Generated.Description;
			Descriptor.InputSchema = ParseSchemaOrDefault(Generated.InputSchemaJson);
		}

		return Descriptors;
	}
}

const TArray<FLLMBlueprintBridgeToolDescriptor>& FLLMBlueprintBridgeToolCatalog::Get()
{
	static const TArray<FLLMBlueprintBridgeToolDescriptor> Catalog = BuildCatalog();
	return Catalog;
}

const FLLMBlueprintBridgeToolDescriptor* FLLMBlueprintBridgeToolCatalog::Find(const FString& ToolName)
{
	for (const FLLMBlueprintBridgeToolDescriptor& Descriptor : Get())
	{
		if (Descriptor.Name.Equals(ToolName, ESearchCase::IgnoreCase))
		{
			return &Descriptor;
		}
	}
	return nullptr;
}

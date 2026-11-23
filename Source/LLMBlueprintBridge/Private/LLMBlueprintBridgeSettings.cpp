#include "LLMBlueprintBridgeSettings.h"

ULLMBlueprintBridgeSettings::ULLMBlueprintBridgeSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("LLMBlueprintBridge");
}

FName ULLMBlueprintBridgeSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

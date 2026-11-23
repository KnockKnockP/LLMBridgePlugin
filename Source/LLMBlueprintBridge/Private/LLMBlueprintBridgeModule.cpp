#include "LLMBlueprintBridgeModule.h"

#include "LLMBlueprintBridgeServer.h"
#include "LLMBlueprintBridgeSettings.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FLLMBlueprintBridgeModule, LLMBlueprintBridge)

void FLLMBlueprintBridgeModule::StartupModule()
{
	if (IsRunningCommandlet())
	{
		return;
	}

	const ULLMBlueprintBridgeSettings* Settings = GetDefault<ULLMBlueprintBridgeSettings>();
	if (!Settings || !Settings->bAutoStart)
	{
		return;
	}

	Server = MakeUnique<FLLMBlueprintBridgeServer>();
	Server->Start();
}

void FLLMBlueprintBridgeModule::ShutdownModule()
{
	if (Server)
	{
		Server->Stop();
		Server.Reset();
	}
}

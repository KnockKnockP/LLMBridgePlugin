#pragma once

#include "Modules/ModuleInterface.h"
#include "Templates/UniquePtr.h"

class FLLMBlueprintBridgeServer;

class FLLMBlueprintBridgeModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	TUniquePtr<FLLMBlueprintBridgeServer> Server;
};

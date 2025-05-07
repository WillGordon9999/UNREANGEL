#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FAngelscriptHeaderModule : public FDefaultModuleImpl
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
#include "AngelscriptNativeBindsModule.h"
#include "AngelscriptBinds.h"
#include "AngelscriptRegister.h"
//#include "../ASBinds/AngelscriptRegister.h"

IMPLEMENT_MODULE(FAngelscriptNativeBindsModule, AngelscriptNativeBinds);

void FAngelscriptNativeBindsModule::StartupModule()
{
	Bind_Angelscript();
}

void FAngelscriptNativeBindsModule::ShutdownModule()
{
}


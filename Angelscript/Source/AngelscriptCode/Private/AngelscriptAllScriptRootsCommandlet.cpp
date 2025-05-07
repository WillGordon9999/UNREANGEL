#include "AngelscriptAllScriptRootsCommandlet.h"
#include "AngelscriptManager.h"
#include "Containers/StringFwd.h"

int32 UAngelscriptAllScriptRootsCommandlet::Main(const FString& Params)
{
	const auto AllScriptRoots = FAngelscriptManager::MakeAllScriptRoots();

	FStringBuilderBase Result;
	Result << TEXT('[');
	auto Separator = TEXT("");
	for (const auto& Root : AllScriptRoots)
	{
		Result << Separator << TEXT('\"') << Root << TEXT('\"');
		Separator = TEXT(", ");
	}
	Result << TEXT(']');

	UE_LOG(Angelscript, Display, TEXT("{ \"AngelscriptScriptRoots\": %s}\n"), *Result);

	return 0;
}

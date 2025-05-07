#include "AngelscriptBinds.h"
#include "AngelscriptManager.h"

#include "Kismet/KismetSystemLibrary.h"

AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_Logging([]
{
	FAngelscriptBinds::CompileOutIfNoLog(FAngelscriptBinds::BindGlobalFunction(
	"void Log(const FString& Text)", [](const FString& Text)
	{
		UE_LOG(Angelscript, Log, TEXT("%s"), *Text);
	}));

	FAngelscriptBinds::CompileOutIfNoLog(FAngelscriptBinds::BindGlobalFunction(
	"void LogInfo(const FString& Text)", [](const FString& Text)
	{
		UE_LOG(Angelscript, Log, TEXT("[Information] %s"), *Text);
	}));

	FAngelscriptBinds::CompileOutIfNoLog(FAngelscriptBinds::BindGlobalFunction(
	"void LogDisplay(const FString& Text)", [](const FString& Text)
	{
		UE_LOG(Angelscript, Display, TEXT("[Display] %s"), *Text);
	}));

	FAngelscriptBinds::CompileOutIfNoLog(FAngelscriptBinds::BindGlobalFunction(
	"void Error(const FString& Text)", [](const FString& Text)
	{
		UE_LOG(Angelscript, Error, TEXT("%s"), *Text);
	}));

	FAngelscriptBinds::CompileOutIfNoLog(FAngelscriptBinds::BindGlobalFunction(
	"void Warning(const FString& Text)", [](const FString& Text)
	{
		UE_LOG(Angelscript, Warning, TEXT("%s"), *Text);
	}));

	FAngelscriptBinds::CompileOutIfNoLog(FAngelscriptBinds::BindGlobalFunction(
		"void LogIf(bool Condition, const FString& Text)", [](bool Condition, const FString& Text)
		{
			if (Condition)
			{
				UE_LOG(Angelscript, Log, TEXT("%s"), *Text);
			}
		}));

	FAngelscriptBinds::CompileOutIfNoLog(FAngelscriptBinds::BindGlobalFunction(
		"void LogInfoIf(bool Condition, const FString& Text)", [](bool Condition, const FString& Text)
		{
			if (Condition)
			{
				UE_LOG(Angelscript, Log, TEXT("[Information] %s"), *Text);
			}
		}));

	FAngelscriptBinds::CompileOutIfNoLog(FAngelscriptBinds::BindGlobalFunction(
		"void LogDisplayIf(bool Condition, const FString& Text)", [](bool Condition, const FString& Text)
		{
			if (Condition)
			{
				UE_LOG(Angelscript, Display, TEXT("[Display] %s"), *Text);
			}
		}));

	FAngelscriptBinds::CompileOutIfNoLog(FAngelscriptBinds::BindGlobalFunction(
		"void ErrorIf(bool Condition, const FString& Text)", [](bool Condition, const FString& Text)
		{
			if (Condition)
			{
				UE_LOG(Angelscript, Error, TEXT("%s"), *Text);
			}
		}));

	FAngelscriptBinds::CompileOutIfNoLog(FAngelscriptBinds::BindGlobalFunction(
		"void WarningIf(bool Condition, const FString& Text)", [](bool Condition, const FString& Text)
		{
			if (Condition)
			{
				UE_LOG(Angelscript, Warning, TEXT("%s"), *Text);
			}
		}));

	// Logging with category, though not allowing verbosity suppression
	FAngelscriptBinds::CompileOutIfNoLog(FAngelscriptBinds::BindGlobalFunction(
	"void Log(const FName& CategoryName, const FString& Text)", [](const FName& CategoryName, const FString& Text)
	{
		FMsg::Logf(nullptr, 0, CategoryName, ELogVerbosity::Log, TEXT("%s"), *Text);
	}));

	FAngelscriptBinds::CompileOutIfNoLog(FAngelscriptBinds::BindGlobalFunction(
	"void LogInfo(const FName& CategoryName, const FString& Text)", [](const FName& CategoryName, const FString& Text)
	{
		FMsg::Logf(nullptr, 0, CategoryName, ELogVerbosity::Log, TEXT("[Information] %s"), *Text);
	}));

	FAngelscriptBinds::CompileOutIfNoLog(FAngelscriptBinds::BindGlobalFunction(
	"void LogDisplay(const FName& CategoryName, const FString& Text)", [](const FName& CategoryName, const FString& Text)
	{
		FMsg::Logf(nullptr, 0, CategoryName, ELogVerbosity::Display, TEXT("[Display] %s"), *Text);
	}));

	FAngelscriptBinds::CompileOutIfNoLog(FAngelscriptBinds::BindGlobalFunction(
	"void Error(const FName& CategoryName, const FString& Text)", [](const FName& CategoryName, const FString& Text)
	{
		FMsg::Logf(nullptr, 0, CategoryName, ELogVerbosity::Error, TEXT("%s"), *Text);
	}));

	FAngelscriptBinds::CompileOutIfNoLog(FAngelscriptBinds::BindGlobalFunction(
	"void Warning(const FName& CategoryName, const FString& Text)", [](const FName& CategoryName, const FString& Text)
	{
		FMsg::Logf(nullptr, 0, CategoryName, ELogVerbosity::Warning, TEXT("%s"), *Text);
	}));

	FAngelscriptBinds::CompileOutIfNoLog(FAngelscriptBinds::BindGlobalFunction(
	"void LogIf(bool Condition, const FName& CategoryName, const FString& Text)", [](bool Condition, const FName& CategoryName, const FString& Text)
	{
		if (Condition)
		{
			FMsg::Logf(nullptr, 0, CategoryName, ELogVerbosity::Log, TEXT("%s"), *Text);
		}
	}));

	FAngelscriptBinds::CompileOutIfNoLog(FAngelscriptBinds::BindGlobalFunction(
	"void LogInfoIf(bool Condition, const FName& CategoryName, const FString& Text)", [](bool Condition, const FName& CategoryName, const FString& Text)
	{
		if (Condition)
		{
			FMsg::Logf(nullptr, 0, CategoryName, ELogVerbosity::Log, TEXT("[Information] %s"), *Text);
		}
	}));

	FAngelscriptBinds::CompileOutIfNoLog(FAngelscriptBinds::BindGlobalFunction(
	"void LogDisplayIf(bool Condition, const FName& CategoryName, const FString& Text)", [](bool Condition, const FName& CategoryName, const FString& Text)
	{
		if (Condition)
		{
			FMsg::Logf(nullptr, 0, CategoryName, ELogVerbosity::Display, TEXT("[Display] %s"), *Text);
		}
	}));

	FAngelscriptBinds::CompileOutIfNoLog(FAngelscriptBinds::BindGlobalFunction(
	"void ErrorIf(bool Condition, const FName& CategoryName, const FString& Text)", [](bool Condition, const FName& CategoryName, const FString& Text)
	{
		if (Condition)
		{
			FMsg::Logf(nullptr, 0, CategoryName, ELogVerbosity::Error, TEXT("%s"), *Text);
		}
	}));

	FAngelscriptBinds::CompileOutIfNoLog(FAngelscriptBinds::BindGlobalFunction(
	"void WarningIf(bool Condition, const FName& CategoryName, const FString& Text)", [](bool Condition, const FName& CategoryName, const FString& Text)
	{
		if (Condition)
		{
			FMsg::Logf(nullptr, 0, CategoryName, ELogVerbosity::Warning, TEXT("%s"), *Text);
		}
	}));

	FAngelscriptBinds::BindGlobalFunction("void Throw(const FString& Text)", [](const FString& Text)
	{
		FAngelscriptManager::Throw(TCHAR_TO_ANSI(*Text));
	});

	FAngelscriptBinds::BindGlobalFunction("void ThrowIf(bool Condition, const FString& Text)", [](bool Condition, const FString& Text)
	{
		if (Condition)
		{
			FAngelscriptManager::Throw(TCHAR_TO_ANSI(*Text));
		}
	});

	FAngelscriptBinds::CompileOutIfNoLog(FAngelscriptBinds::BindGlobalFunction(
	"void Print(const FString& Text, float32 Duration = 5.f, FLinearColor Color = FLinearColor::LucBlue)",
		[](const FString& Text, float Duration, FLinearColor Color)
	{
		UKismetSystemLibrary::PrintString(FAngelscriptManager::CurrentWorldContext, Text, true, /*bPrintToLog=*/ (Duration > 0.f), Color, Duration);
	}));
	FAngelscriptBinds::SetPreviousBindRequiresWorldContext(true);

	FAngelscriptBinds::CompileOutIfNoLog(FAngelscriptBinds::BindGlobalFunction(
	"void PrintFromObject(const UObject WorldContextObject, const FString& Text, float32 Duration = 0.f, FLinearColor Color = FLinearColor::LucBlue)",
		[](const UObject* WorldContextObject, const FString& Text, float Duration, FLinearColor Color)
	{
		UKismetSystemLibrary::PrintString(WorldContextObject, Text, true, true, Color, Duration);
	}));

	FAngelscriptBinds::CompileOutIfNoLog(FAngelscriptBinds::BindGlobalFunction(
	"void PrintToScreen(const FString& Text, float32 Duration = 0.f, FLinearColor Color = FLinearColor::LucBlue)",
		[](const FString& Text, float Duration, FLinearColor Color)
	{
		UKismetSystemLibrary::PrintString(FAngelscriptManager::CurrentWorldContext, Text, true, false, Color, Duration);
	}));
	FAngelscriptBinds::SetPreviousBindRequiresWorldContext(true);

	FAngelscriptBinds::CompileOutIfNoLog(FAngelscriptBinds::BindGlobalFunction(
	"void PrintWarning(const FString& Text, float32 Duration = 8.f, FLinearColor Color = FLinearColor::Yellow)",
		[](const FString& Text, float Duration, FLinearColor Color)
	{
		const FString& WarningText = FString::Printf(TEXT("[Warning] ")) + Text;
		UKismetSystemLibrary::PrintString(FAngelscriptManager::CurrentWorldContext, WarningText, true, true, Color, Duration);
	}));
	FAngelscriptBinds::SetPreviousBindRequiresWorldContext(true);

	FAngelscriptBinds::CompileOutIfNoLog(FAngelscriptBinds::BindGlobalFunction(
	"void PrintError(const FString& Text, float32 Duration = 8.f, FLinearColor Color = FLinearColor::Red)",
		[](const FString& Text, float Duration, FLinearColor Color)
	{
		const FString& ErrorText = FString::Printf(TEXT("[Error] ")) + Text;
		UKismetSystemLibrary::PrintString(FAngelscriptManager::CurrentWorldContext, ErrorText, true, true, Color, Duration);
	}));
	FAngelscriptBinds::SetPreviousBindRequiresWorldContext(true);
});

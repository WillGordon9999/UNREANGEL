/*
	The bind database is a cache file generated in the editor that is used in the cooked
	game to correctly bind all C++ symbols to angelscript without requiring full 
	editor metadata to be in the cooked game.
*/
#pragma once
#include "CoreMinimal.h"

struct FAngelscriptPropertyBind
{
	FString Declaration;
	FString UnrealPath;
	FString GeneratedName;
	bool bCanWrite = false;
	bool bCanRead = false;
	bool bCanEdit = false;
	bool bGeneratedGetter = false;
	bool bGeneratedSetter = false;
	bool bGeneratedHandle = false;
	bool bGeneratedUnresolvedObject = false;

	inline friend FArchive& operator<<(FArchive& Archive, FAngelscriptPropertyBind& Data)
	{
		Archive << Data.Declaration;
		Archive << Data.UnrealPath;
		Archive << Data.bCanWrite;
		Archive << Data.bCanRead;
		Archive << Data.bCanEdit;
		Archive << Data.bGeneratedGetter;
		Archive << Data.bGeneratedSetter;
		Archive << Data.GeneratedName;
		Archive << Data.bGeneratedHandle;
		Archive << Data.bGeneratedUnresolvedObject;
		return Archive;
	}
};

struct FAngelscriptStructBind
{
	FString TypeName;
	FString UnrealPath;
	TArray<FAngelscriptPropertyBind> Properties;

	// * Temporary state
	class UScriptStruct* ResolvedStruct = nullptr;

	inline friend FArchive& operator<<(FArchive& Archive, FAngelscriptStructBind& Data)
	{
		Archive << Data.TypeName;
		Archive << Data.UnrealPath;
		Archive << Data.Properties;
		return Archive;
	}
};

struct FAngelscriptMethodBind
{
	FString Declaration;
	FString UnrealPath;
	FString ClassName;
	FString ScriptName;
	int8 WorldContextArgument = -1;
	int8 DeterminesOutputTypeArgument = -1;
	bool bStaticInUnreal = false;
	bool bStaticInScript = false;
	bool bGlobalScope = false;
	bool bNotAngelscriptProperty = false;
	bool bTrivial = false;

	// * Temporary state
	class UFunction* ResolvedFunction = nullptr;

	inline friend FArchive& operator<<(FArchive& Archive, FAngelscriptMethodBind& Data)
	{
		Archive << Data.Declaration;
		Archive << Data.UnrealPath;
		Archive << Data.bStaticInUnreal;
		Archive << Data.bStaticInScript;
		Archive << Data.bGlobalScope;
		Archive << Data.bNotAngelscriptProperty;
		Archive << Data.bTrivial;
		Archive << Data.WorldContextArgument;
		Archive << Data.DeterminesOutputTypeArgument;
		Archive << Data.ClassName;
		Archive << Data.ScriptName;
		return Archive;
	}
};

struct FAngelscriptClassBind
{
	FString TypeName;
	FString UnrealPath;
	TArray<FAngelscriptMethodBind> Methods;
	TArray<FAngelscriptPropertyBind> Properties;

	// * Temporary state
	class UClass* ResolvedClass = nullptr;

	inline friend FArchive& operator<<(FArchive& Archive, FAngelscriptClassBind& Data)
	{
		Archive << Data.TypeName;
		Archive << Data.UnrealPath;
		Archive << Data.Methods;
		Archive << Data.Properties;
		return Archive;
	}
};

struct FAngelscriptClassHeader
{
	FString UnrealPath;
	FString Header;

	inline friend FArchive& operator<<(FArchive& Archive, FAngelscriptClassHeader& Data)
	{
		Archive << Data.UnrealPath;
		Archive << Data.Header;
		return Archive;
	}
};

class FAngelscriptBindDatabase
{
public:

	FORCEINLINE static FAngelscriptBindDatabase& Get()
	{
		return Instance;
	}
	
	void Save(const FString& Filename);
	void Load(const FString& Filename, bool bGeneratingPrecompiledData);
	void Clear();

	TArray<FAngelscriptStructBind> Structs;
	TArray<FAngelscriptClassBind> Classes;
	TArray<UEnum*> BoundEnums;
	TArray<UDelegateFunction*> BoundDelegateFunctions;

	TMap<UObject*, FString> HeaderLinks;
	static FString GetSourceHeader(UField* Field);

private:
	void Serialize(FArchive& Archive);

	static FAngelscriptBindDatabase Instance;
};
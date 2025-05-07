#include "Engine/Engine.h"

#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"

#include "AngelscriptCodeModule.h"

#include "UObject/UObjectIterator.h"

#include "AngelscriptManager.h"
#include "AngelscriptType.h"
#include "AngelscriptBinds.h"
#include "AngelscriptBinds/Bind_Actor.h"

#include "StartAngelscriptHeaders.h"
//#include "as_scriptengine.h"
//#include "as_objecttype.h"
#include "source/as_scriptengine.h"
#include "source/as_objecttype.h"
#include "EndAngelscriptHeaders.h"

/**
 * Binds default methods that all AActors have
 */
AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_AActor_Base((int32)FAngelscriptBinds::EOrder::Late-1, []
{
	auto AActor_ = FAngelscriptBinds::ExistingClass("AActor");

	AActor_.Method("bool IsActorInitialized() const", METHOD_TRIVIAL(AActor, IsActorInitialized));
	AActor_.Method("bool HasActorBegunPlay() const", METHOD_TRIVIAL(AActor, HasActorBegunPlay));
	AActor_.Method("bool IsHidden() const", METHOD_TRIVIAL(AActor, IsHidden));
	AActor_.Method("FString GetActorNameOrLabel() const", METHOD_TRIVIAL(AActor, GetActorNameOrLabel));
	AActor_.Method("UGameInstance GetGameInstance() const", METHODPR_TRIVIAL(UGameInstance*, AActor, GetGameInstance, () const));

	AActor_.Method("void GetComponentsByClass(?& OutComponents) const",
	[](const AActor* Actor, TArray<UActorComponent*>& OutComponents, int TypeId)
	{
		auto& Manager = FAngelscriptManager::Get();
		asCTypeInfo* ScriptType = (asCTypeInfo*)Manager.Engine->GetTypeInfoById(TypeId);
		if (ScriptType == nullptr
			|| (ScriptType->flags & asOBJ_VALUE) == 0)
		{
			FAngelscriptManager::Throw("GetComponentsByClass must take a TArray of components as its out argument.");
			return;
		}

		asCObjectType* ObjectType = (asCObjectType*)(ScriptType);
		if (ObjectType->templateBaseType != FAngelscriptType::ArrayTemplateTypeInfo)
		{
			FAngelscriptManager::Throw("GetComponentsByClass must take a TArray of components as its out argument.");
			return;
		}

		auto* SubTypeInfo = ObjectType->templateSubTypes[0].GetTypeInfo();
		if (SubTypeInfo == nullptr
			|| (SubTypeInfo->GetFlags() & asOBJ_REF) == 0
			|| (SubTypeInfo->plainUserData == 0))
		{
			FAngelscriptManager::Throw("GetComponentsByClass must take a TArray of components as its out argument.");
			return;
		}

		UClass* SubClass = (UClass*)SubTypeInfo->plainUserData;
		if (!SubClass->IsChildOf<UActorComponent>())
		{
			FAngelscriptManager::Throw("GetComponentsByClass must take a TArray of components as its out argument.");
			return;
		}

		if (Actor == nullptr)
		{
			FAngelscriptManager::Throw("Actor was null.");
			return;
		}

		for (UActorComponent* Comp : Actor->GetComponents())
		{
			if (Comp == nullptr)
				continue;
			if (Comp->IsA(SubClass))
				OutComponents.Add(Comp);
		}
	});

	AActor_.Method("void GetComponentsByClass(UClass ComponentClass, ?& OutComponents) const",
	[](const AActor* Actor, UClass* ComponentClass, TArray<UActorComponent*>& OutComponents, int TypeId)
	{
		auto& Manager = FAngelscriptManager::Get();
		asCTypeInfo* ScriptType = (asCTypeInfo*)Manager.Engine->GetTypeInfoById(TypeId);
		if (ScriptType == nullptr
			|| (ScriptType->flags & asOBJ_VALUE) == 0)
		{
			FAngelscriptManager::Throw("GetComponentsByClass must take a TArray of components as its out argument.");
			return;
		}

		asCObjectType* ObjectType = (asCObjectType*)(ScriptType);
		if (ObjectType->templateBaseType != FAngelscriptType::ArrayTemplateTypeInfo)
		{
			FAngelscriptManager::Throw("GetComponentsByClass must take a TArray of components as its out argument.");
			return;
		}

		auto* SubTypeInfo = ObjectType->templateSubTypes[0].GetTypeInfo();
		if (SubTypeInfo == nullptr
			|| (SubTypeInfo->GetFlags() & asOBJ_REF) == 0
			|| (SubTypeInfo->plainUserData == 0))
		{
			FAngelscriptManager::Throw("GetComponentsByClass must take a TArray of components as its out argument.");
			return;
		}

		UClass* SubClass = (UClass*)SubTypeInfo->plainUserData;
		if (!SubClass->IsChildOf<UActorComponent>())
		{
			FAngelscriptManager::Throw("GetComponentsByClass must take a TArray of components as its out argument.");
			return;
		}

		if (Actor == nullptr)
		{
			FAngelscriptManager::Throw("Actor was null.");
			return;
		}

		if (ComponentClass == nullptr)
		{
			FAngelscriptManager::Throw("Component class was null.");
			return;
		}

		if (!ComponentClass->IsChildOf(SubClass))
		{
			FAngelscriptManager::Throw("Class specified to GetComponentsByClass is not a child of array element class.");
			return;
		}

		for (UActorComponent* Comp : Actor->GetComponents())
		{
			if (Comp == nullptr)
				continue;
			if (Comp->IsA(ComponentClass))
				OutComponents.Add(Comp);
		}
	});

#if !WITH_ANGELSCRIPT_HAZE
	AActor_.Method("APawn GetActorInstigator() const",
	[](const AActor* Actor) -> APawn*
	{
		return Actor->GetInstigator();
	});
		
	AActor_.Method("AController GetActorInstigatorController() const",
	[](const AActor* Actor) -> AController*
	{
		return Actor->GetInstigatorController();
	});
#endif
});

AActor* FAngelscriptActorBinds::SpawnActorFromMeta(class asCScriptFunction* Meta, const FVector& Location, const FRotator& Rotation, const FName& Name, ULevel* Level)
{
	UClass* ActorClass = (UClass*)Meta->userData;
	UObject* WorldContext = FAngelscriptManager::CurrentWorldContext;
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull);
	if (World == nullptr)
	{
		FAngelscriptManager::Throw("Invalid World Context");
		return nullptr;
	}

	if (ActorClass == nullptr)
	{
		FAngelscriptManager::Throw("Class was nullptr.");
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.Name = Name;
	Params.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;

	if (Level != nullptr)
		Params.OverrideLevel = Level;
	else if (World->IsGameWorld() && FAngelscriptCodeModule::GetDynamicSpawnLevel().IsBound())
		Params.OverrideLevel = FAngelscriptCodeModule::GetDynamicSpawnLevel().Execute();
	else if (auto* Comp = Cast<UActorComponent>(WorldContext))
		Params.OverrideLevel = Comp->GetOwner() ? Comp->GetOwner()->GetLevel() : nullptr;
	else if (auto* Actor = Cast<AActor>(WorldContext))
		Params.OverrideLevel = Actor->GetLevel();

	return World->SpawnActor(ActorClass, &Location, &Rotation, Params);
}

AActor* FAngelscriptActorBinds::SpawnActor(const TSubclassOf<AActor>& Class, const FVector& Location, const FRotator& Rotation, const FName& Name, bool bDeferredSpawn, ULevel* Level)
{
	UObject* WorldContext = FAngelscriptManager::CurrentWorldContext;
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull);
	if (World == nullptr)
	{
		FAngelscriptManager::Throw("Invalid World Context");
		return nullptr;
	}

	if (Class == nullptr)
	{
		FAngelscriptManager::Throw("Class was nullptr.");
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.Name = Name;
	Params.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
	Params.bDeferConstruction = bDeferredSpawn;

	if (Level != nullptr)
		Params.OverrideLevel = Level;
	else if (World->IsGameWorld() && FAngelscriptCodeModule::GetDynamicSpawnLevel().IsBound())
		Params.OverrideLevel = FAngelscriptCodeModule::GetDynamicSpawnLevel().Execute();
	else if (auto* Comp = Cast<UActorComponent>(WorldContext))
		Params.OverrideLevel = Comp->GetOwner() ? Comp->GetOwner()->GetLevel() : nullptr;
	else if (auto* Actor = Cast<AActor>(WorldContext))
		Params.OverrideLevel = Actor->GetLevel();

	return World->SpawnActor(Class, &Location, &Rotation, Params);
}

AActor* FAngelscriptActorBinds::SpawnPersistentActor(const TSubclassOf<AActor>& Class, const FVector& Location, const FRotator& Rotation, const FName& Name, bool bDeferredSpawn)
{
	UObject* WorldContext = FAngelscriptManager::CurrentWorldContext;
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull);
	if (World == nullptr)
	{
		FAngelscriptManager::Throw("Invalid World Context");
		return nullptr;
	}

	if (Class == nullptr)
	{
		FAngelscriptManager::Throw("Class was nullptr.");
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.Name = Name;
	Params.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
	Params.bDeferConstruction = bDeferredSpawn;

	return World->SpawnActor(Class, &Location, &Rotation, Params);
}

void FAngelscriptActorBinds::FinishSpawningActor(AActor* Actor)
{
	if (Actor == nullptr)
	{
		return;
	}

	if (Actor->HasActorBegunPlay())
	{
		FAngelscriptManager::Throw("Actor has already finished spawning. Did you pass bDeferredSpawn=true to the spawn call?");
		return;
	}
	Actor->FinishSpawning(Actor->GetActorTransform());
}

void FAngelscriptActorBinds::FinishSpawningActor_Transform(AActor* Actor, const FTransform& SpawnTransform)
{
	if (Actor == nullptr)
	{
		return;
	}

	if (Actor->HasActorBegunPlay())
	{
		FAngelscriptManager::Throw("Actor has already finished spawning. Did you pass bDeferredSpawn=true to the spawn call?");
		return;
	}
	Actor->FinishSpawning(SpawnTransform);
}

AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_Actors((int32)FAngelscriptBinds::EOrder::Late + 150, []
{
	for (UClass* Class : TObjectRange<UClass>())
	{
		if (!Class->IsChildOf(AActor::StaticClass()))
			continue;

		auto Type = FAngelscriptType::GetByClass(Class);
		if (!Type.IsValid())
			continue;

		FString ClassName = Type->GetAngelscriptTypeName();

		// Static accessors to get or spawn actors
		{
			FAngelscriptBinds::FNamespace ns(ClassName);

			// AActor::Spawn(const FVector& Location, const FRotator& Rotation, FName Name)
			{
				FString FuncDecl = FString::Printf(
					TEXT("%s Spawn(const FVector& Location = FVector::ZeroVector, ")
					TEXT("const FRotator& Rotation = FRotator::ZeroRotator, const FName& Name = NAME_None, ULevel Level = nullptr)"),
					*ClassName);

				FAngelscriptBinds::BindGlobalFunction(FuncDecl, FUNC(FAngelscriptActorBinds::SpawnActorFromMeta), Class);
				FAngelscriptBinds::PreviousBindPassScriptFunctionAsFirstParam();
			}
		}
	}

#if !WITH_ANGELSCRIPT_HAZE
	FAngelscriptBinds::BindGlobalFunction("void GetAllActorsOfClass(?& OutActors)",
	[](TArray<AActor*>& OutActors, int TypeId)
	{
		auto& Manager = FAngelscriptManager::Get();
		asCTypeInfo* ScriptType = (asCTypeInfo*)Manager.Engine->GetTypeInfoById(TypeId);
		if (ScriptType == nullptr
			|| (ScriptType->flags & asOBJ_VALUE) == 0)
		{
			FAngelscriptManager::Throw("GetAllActors must take a TArray of actors as its out argument.");
			return;
		}

		asCObjectType* ObjectType = (asCObjectType*)(ScriptType);
		if (ObjectType->templateBaseType != FAngelscriptType::ArrayTemplateTypeInfo)
		{
			FAngelscriptManager::Throw("GetAllActors must take a TArray of actors as its out argument.");
			return;
		}

		auto* ActorTypeInfo = ObjectType->templateSubTypes[0].GetTypeInfo();
		if (ActorTypeInfo == nullptr
			|| (ActorTypeInfo->GetFlags() & asOBJ_REF) == 0
			|| (ActorTypeInfo->plainUserData == 0))
		{
			FAngelscriptManager::Throw("GetAllActors must take a TArray of actors as its out argument.");
			return;
		}

		UClass* ActorClass = (UClass*)ActorTypeInfo->plainUserData;
		if (!ActorClass->IsChildOf<AActor>())
		{
			FAngelscriptManager::Throw("GetAllActors must take a TArray of actors as its out argument.");
			return;
		}

		UGameplayStatics::GetAllActorsOfClass(FAngelscriptManager::CurrentWorldContext, ActorClass, OutActors);
	});

	FAngelscriptBinds::BindGlobalFunction("void GetAllActorsOfClass(UClass Class, ?& OutActors)",
	[](UClass* ActorClass, TArray<AActor*>& OutActors, int TypeId)
	{
		auto& Manager = FAngelscriptManager::Get();
		asCTypeInfo* ScriptType = (asCTypeInfo*)Manager.Engine->GetTypeInfoById(TypeId);
		if (ScriptType == nullptr
			|| (ScriptType->flags & asOBJ_VALUE) == 0)
		{
			FAngelscriptManager::Throw("GetAllActors must take a TArray of actors as its out argument.");
			return;
		}

		asCObjectType* ObjectType = (asCObjectType*)(ScriptType);
		if (ObjectType->templateBaseType != FAngelscriptType::ArrayTemplateTypeInfo)
		{
			FAngelscriptManager::Throw("GetAllActors must take a TArray of actors as its out argument.");
			return;
		}

		auto* ActorTypeInfo = ObjectType->templateSubTypes[0].GetTypeInfo();
		if (ActorTypeInfo == nullptr
			|| (ActorTypeInfo->GetFlags() & asOBJ_REF) == 0
			|| (ActorTypeInfo->plainUserData == 0))
		{
			FAngelscriptManager::Throw("GetAllActors must take a TArray of actors as its out argument.");
			return;
		}

		UClass* ArraySubClass = (UClass*)ActorTypeInfo->plainUserData;
		if (!ArraySubClass->IsChildOf<AActor>())
		{
			FAngelscriptManager::Throw("GetAllActors must take a TArray of actors as its out argument.");
			return;
		}

		if (ActorClass == nullptr)
		{
			FAngelscriptManager::Throw("Actor class was null.");
			return;
		}

		if (!ActorClass->IsChildOf(ArraySubClass))
		{
			FAngelscriptManager::Throw("Class specified to GetAllActorsOfClass is not a child of array element class.");
			return;
		}

		UGameplayStatics::GetAllActorsOfClass(FAngelscriptManager::CurrentWorldContext, ActorClass, OutActors);
	});

	FAngelscriptBinds::BindGlobalFunction("void __Actor_GetAllByClass(UClass Class, ?& OutActors)",
	[](UClass* ActorClass, TArray<AActor*>& OutActors, int TypeId)
	{
		UGameplayStatics::GetAllActorsOfClass(FAngelscriptManager::CurrentWorldContext, ActorClass, OutActors);
	});

	FAngelscriptBinds::BindGlobalFunction("void GetAllActorsOfClassWithTag(FName TagName, ?& OutActors)",
	[](FName Tag, TArray<AActor*>& OutActors, int TypeId)
	{
		auto& Manager = FAngelscriptManager::Get();
		asCTypeInfo* ScriptType = (asCTypeInfo*)Manager.Engine->GetTypeInfoById(TypeId);
		if (ScriptType == nullptr
			|| (ScriptType->flags & asOBJ_VALUE) == 0)
		{
			FAngelscriptManager::Throw("GetAllActors must take a TArray of actors as its out argument.");
			return;
		}

		asCObjectType* ObjectType = (asCObjectType*)(ScriptType);
		if (ObjectType->templateBaseType != FAngelscriptType::ArrayTemplateTypeInfo)
		{
			FAngelscriptManager::Throw("GetAllActors must take a TArray of actors as its out argument.");
			return;
		}

		auto* ActorTypeInfo = ObjectType->templateSubTypes[0].GetTypeInfo();
		if (ActorTypeInfo == nullptr
			|| (ActorTypeInfo->GetFlags() & asOBJ_REF) == 0
			|| (ActorTypeInfo->plainUserData == 0))
		{
			FAngelscriptManager::Throw("GetAllActors must take a TArray of actors as its out argument.");
			return;
		}

		UClass* ActorClass = (UClass*)ActorTypeInfo->plainUserData;
		if (!ActorClass->IsChildOf<AActor>())
		{
			FAngelscriptManager::Throw("GetAllActors must take a TArray of actors as its out argument.");
			return;
		}

		UGameplayStatics::GetAllActorsOfClassWithTag(FAngelscriptManager::CurrentWorldContext, ActorClass, Tag, OutActors);
	});
#endif

	FAngelscriptBinds::BindGlobalFunction(
	  "AActor SpawnActor(const TSubclassOf<AActor>& Class, const FVector& Location = FVector::ZeroVector, const FRotator& Rotation = FRotator::ZeroRotator, const FName& Name = NAME_None, bool bDeferredSpawn = false, ULevel Level = nullptr)",
		FUNC(FAngelscriptActorBinds::SpawnActor));
	FAngelscriptBinds::SetPreviousBindArgumentDeterminesOutputType(0);

	FAngelscriptBinds::BindGlobalFunction(
	  "void FinishSpawningActor(AActor Actor)",
		FUNC(FAngelscriptActorBinds::FinishSpawningActor));

	FAngelscriptBinds::BindGlobalFunction(
		"void FinishSpawningActor(AActor Actor, const FTransform& SpawnTransform)",
		FUNC(FAngelscriptActorBinds::FinishSpawningActor_Transform));

	// SpawnPersistentActor is a special case for player-tied things that should always stick around
	FAngelscriptBinds::BindGlobalFunction(
	  "AActor SpawnPersistentActor(const TSubclassOf<AActor>& Class, const FVector& Location = FVector::ZeroVector, const FRotator& Rotation = FRotator::ZeroRotator, const FName& Name = NAME_None, bool bDeferredSpawn = false)",
		FUNC(FAngelscriptActorBinds::SpawnPersistentActor));
	FAngelscriptBinds::SetPreviousBindArgumentDeterminesOutputType(0);

});

#pragma once
#include "CoreMinimal.h"
#include "AngelscriptObject.generated.h"

UCLASS()
class ANGELSCRIPTCODE_API UAngelscriptObject : public UObject
{
	GENERATED_BODY()

public:
	UAngelscriptObject();

	//virtual void BeginPlay() override;

	//virtual void Tick(float DeltaTime) override;
	virtual void ProcessEvent(UFunction* Function, void* Parameters) override;
};
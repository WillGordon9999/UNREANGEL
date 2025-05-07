#include "AngelscriptObject.h"
#include "ClassGenerator/ASClass.h"

UAngelscriptObject::UAngelscriptObject()
{

}

//void UAngelscriptObject::BeginPlay()
//{
//	Super::BeginPlay();
//}
//
//void UAngelscriptObject::Tick(float DeltaTime)
//{
//	Super::Tick(DeltaTime);
//}

void UAngelscriptObject::ProcessEvent(UFunction* Function, void* Parameters)
{
	Super::ProcessEvent(Function, Parameters);

	UASFunction* ASFunction = (UASFunction*)Function;

	if (ASFunction != nullptr) //TEMP this would probably not be safe
	{
		// If the RPC has a validation function in Angelscript we need to call that first
		if (Function->FunctionFlags & FUNC_NetValidate)
		{
			UFunction* ValidateFunction = ASFunction->GetRuntimeValidateFunction();
			if (ValidateFunction)
			{
				// Allocate new params for the validation function in this scope. This should also contain space for the return value.
				FStructOnScope ValidateFunctionParms(ValidateFunction);
				uint8* ValidateFunctionParmsPtr = ValidateFunctionParms.GetStructMemory();

				TFieldIterator<FProperty> FunctionPropertyIt(Function);
				TFieldIterator<FProperty> ValidateFunctionPropertyIt(ValidateFunction);

				// Then copy the parameter values from our parent function in there.
				for (int32 ParamIdx = 0; ParamIdx < Function->NumParms; ParamIdx++)
				{
					if (!FunctionPropertyIt)
					{
						break;
					}

					check(ValidateFunctionPropertyIt);

					FProperty* SourceProp = *FunctionPropertyIt;
					FProperty* TargetProp = *ValidateFunctionPropertyIt;

					// Copy parameters, but not return values.
					if (SourceProp && TargetProp
						&& ((SourceProp->PropertyFlags & CPF_Parm) != 0)
						&& ((SourceProp->PropertyFlags & CPF_ReturnParm) == 0))
					{
						check(SourceProp->SameType(TargetProp));

						const uint8* SrcPtr = SourceProp->ContainerPtrToValuePtr<uint8>(Parameters);
						uint8* DestPtr = TargetProp->ContainerPtrToValuePtr<uint8>(ValidateFunctionParmsPtr);

						SourceProp->CopyCompleteValue(DestPtr, SrcPtr);
					}

					++FunctionPropertyIt;
					++ValidateFunctionPropertyIt;
				}

				// Now invoke the validation function with our validation parameters.
				UASFunction* ASValidate = (UASFunction*)ValidateFunction;
				//ValidateFunction->RuntimeCallEvent(this, ValidateFunctionParmsPtr);
				ASValidate->RuntimeCallEvent(this, ValidateFunctionParmsPtr);
				void* RetPtr = (void*)((SIZE_T)ValidateFunctionParmsPtr + ValidateFunction->ReturnValueOffset);

				// Check return value of _Validate function
				if (*(uint8*)RetPtr != 0)
				{
					//Function->RuntimeCallEvent(this, Parameters);
					ASFunction->RuntimeCallEvent(this, Parameters);
				}
				else
				{
					RPC_ValidateFailed(*ValidateFunction->GetName());
				}
			}
		}
		else
		{
			ASFunction->RuntimeCallEvent(this, Parameters);
		}
	}
}
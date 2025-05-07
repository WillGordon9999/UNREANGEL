#include "AngelscriptBinds/Bind_TSet.h"
#include "AngelscriptBinds.h"
#include "AngelscriptManager.h"

#include "ClassGenerator/ASClass.h"

#include "Containers/Set.h"
#include "UObject/UnrealType.h"
//#include "UObject/GarbageCollectionSchema.h"
#include "UObject/GarbageCollection.h"

#include "StartAngelscriptHeaders.h"
//#include "as_context.h"
//#include "as_scriptengine.h"
//#include "as_scriptfunction.h"
#include "source/as_context.h"
#include "source/as_scriptengine.h"
#include "source/as_scriptfunction.h"
#include "EndAngelscriptHeaders.h"

#if AS_ITERATOR_DEBUGGING
static thread_local TArray<void*, TInlineAllocator<16>> GSetsBeingIterated;

static bool CheckSetIteratorDebug(FScriptSet& Set)
{
	if (GSetsBeingIterated.Contains(&Set))
	{
		FAngelscriptManager::Throw("TSet is being modified during for loop iteration");
		return false;
	}

	return true;
}

void FSetOperations::MarkSetBeingIterated(FScriptSet& Set)
{
	GSetsBeingIterated.Add(&Set);
}

void FSetOperations::UnmarkSetBeingIterated(FScriptSet& Set)
{
	GSetsBeingIterated.RemoveSingle(&Set);
}
#endif

#if AS_REFERENCE_DEBUGGING
static void InvalidateReferencesToSet(FScriptSet& Set, FSetOperations* Ops)
{
	asCContext* Context = (asCContext*)asGetActiveContext();
	if (Context != nullptr)
	{
		Context->InvalidateReferencesToMemoryBlock(Set.GetData(0, Ops->Layout), Set.GetMaxIndex() * Ops->Layout.SparseArrayLayout.Size);
	}
}
#endif

struct FAngelscriptSetType : public FAngelscriptType
{
	FString GetAngelscriptTypeName() const override
	{
		return TEXT("TSet");
	}

	virtual bool CanQueryPropertyType() const { return false; }
	virtual bool CanBeTemplateSubType() const override { return false; }

	bool HasReferences(const FAngelscriptTypeUsage& Usage) const override
	{
		if (Usage.SubTypes.Num() != 1)
			return false;
		return Usage.SubTypes[0].HasReferences();
	}

	void EmitReferenceInfo(const FAngelscriptTypeUsage& Usage, FGCReferenceParams& Params) const override
	{
		check(HasReferences(Usage));

		auto SetLayout = FScriptSet::GetScriptLayout(Usage.SubTypes[0].GetValueSize(), Usage.SubTypes[0].GetValueAlignment());

		UE::GC::FSchemaBuilder InnerSchema(SetLayout.Size);
		{
			FGCReferenceParams InnerParams = Params;
			InnerParams.Schema = &InnerSchema;
			InnerParams.AtOffset = 0;
			Usage.SubTypes[0].EmitReferenceInfo(InnerParams);
		}

		Params.Schema->Add(UE::GC::DeclareMember(Params.Names.Top(), Params.AtOffset, UE::GC::EMemberType::SparseStructArray, InnerSchema.Build()));
	}

	bool CanCreateProperty(const FAngelscriptTypeUsage& Usage) const override
	{
		if (Usage.SubTypes.Num() != 1)
			return false;
		return Usage.SubTypes[0].CanCreateProperty() && Usage.SubTypes[0].CanHashValue();
	}

	FProperty* CreateProperty(const FAngelscriptTypeUsage& Usage, const FPropertyParams& Params) const override
	{
		auto* SetProp = new FSetProperty(Params.Outer, Params.PropertyName, RF_Public);

		FPropertyParams InnerParams = Params;
		InnerParams.Outer = SetProp;
		InnerParams.PropertyName = *(Params.PropertyName.ToString() + TEXT("_Element"));

		SetProp->SetLayout = FScriptSet::GetScriptLayout(Usage.SubTypes[0].GetValueSize(), Usage.SubTypes[0].GetValueAlignment());
		SetProp->ElementProp = Usage.SubTypes[0].CreateProperty(InnerParams);

		return SetProp;
	}

	bool MatchesProperty(const FAngelscriptTypeUsage& Usage, const FProperty* Property, EPropertyMatchType MatchType) const override
	{
		if (Usage.SubTypes.Num() != 1)
			return false;

		const FSetProperty* SetProp = CastField<FSetProperty>(Property);
		if (SetProp == nullptr)
			return false;

		return Usage.SubTypes[0].MatchesProperty(SetProp->ElementProp, FAngelscriptType::EPropertyMatchType::InContainer);
	}

	bool CanCopy(const FAngelscriptTypeUsage& Usage) const override
	{
		return Usage.SubTypes.Num() == 1 && Usage.SubTypes[0].CanCopy()
			&& Usage.SubTypes[0].CanConstruct() && Usage.SubTypes[0].CanDestruct();
	}
	bool NeedCopy(const FAngelscriptTypeUsage& Usage) const override { return true; }
	void CopyValue(const FAngelscriptTypeUsage& Usage, void* SourcePtr, void* DestinationPtr) const override
	{
		FScriptSet& Source = *(FScriptSet*)SourcePtr;
		FScriptSet& Destination = *(FScriptSet*)DestinationPtr;

		FSetOperations Ops(Usage.SubTypes[0]);
		Ops.Empty(Destination, Source.Num());
		for (int32 i = 0, Num = Source.GetMaxIndex(); i < Num; ++i)
		{
			if (Source.IsValidIndex(i))
				Ops.Add(Destination, Ops.GetElement(Source, i));
		}
	}

	bool CanConstruct(const FAngelscriptTypeUsage& Usage) const override
	{
		return Usage.SubTypes.Num() == 1 && Usage.SubTypes[0].CanConstruct();
	}
	bool NeedConstruct(const FAngelscriptTypeUsage& Usage) const override { return true; }
	void ConstructValue(const FAngelscriptTypeUsage& Usage, void* DestinationPtr) const override
	{
		new(DestinationPtr) FScriptSet();
	}

	bool CanDestruct(const FAngelscriptTypeUsage& Usage) const override
	{
		return Usage.SubTypes.Num() == 1 && Usage.SubTypes[0].CanDestruct();
	}
	bool NeedDestruct(const FAngelscriptTypeUsage& Usage) const override { return true; }
	void DestructValue(const FAngelscriptTypeUsage& Usage, void* DestinationPtr) const override
	{
		FScriptSet& Destination = *(FScriptSet*)DestinationPtr;

		FSetOperations Ops(Usage.SubTypes[0]);
		Ops.Empty(Destination, 0);

		Destination.~FScriptSet();
	}

	int32 GetValueSize(const FAngelscriptTypeUsage& Usage) const override
	{
		return sizeof(FScriptSet);
	}

	int32 GetValueAlignment(const FAngelscriptTypeUsage& Usage) const
	{
		return alignof(FScriptSet);
	}

	bool CanBeArgument(const FAngelscriptTypeUsage& Usage) const override { return true; }
	void SetArgument(const FAngelscriptTypeUsage& Usage, int32 ArgumentIndex, class asIScriptContext* Context, struct FFrame& Stack, const FArgData& Data) const override
	{
		FScriptSet* Arg = (FScriptSet*)Data.StackPtr;
		new(Arg) FScriptSet();

		if (Usage.bIsReference)
		{
			FScriptSet& Ref = Stack.StepCompiledInRef<FSetProperty,FScriptSet>(Arg);
			Context->SetArgAddress(ArgumentIndex, &Ref);
		}
		else
		{
			Stack.StepCompiledIn<FSetProperty>(Arg);
			Context->SetArgObject(ArgumentIndex, Arg);
		}
	}

	bool CanBeReturned(const FAngelscriptTypeUsage& Usage) const override
	{
		return true;
	}

	void GetReturnValue(const FAngelscriptTypeUsage& Usage, class asIScriptContext* Context, void* Destination) const override
	{
		if (Usage.bIsReference)
		{
			*(void**)Destination = Context->GetReturnAddress();
		}
		else
		{
			void* ReturnedObject = Context->GetReturnObject();
			if (ReturnedObject == nullptr)
				return;
			CopyValue(Usage, ReturnedObject, Destination);
		}
	}

	bool GetDebuggerValue(const FAngelscriptTypeUsage& Usage, void* Address, struct FDebuggerValue& Value) const override
	{
		if (Usage.SubTypes.Num() != 1)
			return false;

		const FAngelscriptTypeUsage& SubType = Usage.SubTypes[0];
		FScriptSet& Set = Usage.ResolvePrimitive<FScriptSet>(Address);

		Value.Usage = Usage;
		Value.Address = Address;
		Value.bHasMembers = true;
		Value.Type = Usage.GetAngelscriptDeclaration();

		int32 Num = Set.Num();
		if (Num == 0)
		{
			Value.Value = TEXT("Empty");
		}
		else
		{
			Value.Value = FString::Printf(TEXT("Num = %d"), Num);
		}

		return true;
	}

	bool GetDebuggerScope(const FAngelscriptTypeUsage& Usage, void* Address, struct FDebuggerScope& Scope) const override
	{
		if (Usage.SubTypes.Num() != 1)
			return false;

		const FAngelscriptTypeUsage& SubType = Usage.SubTypes[0];
		FScriptSet& Set = Usage.ResolvePrimitive<FScriptSet>(Address);

		FSetOperations Ops(SubType);
		for (int32 i = 0, Num = Set.GetMaxIndex(); i < Num; ++i)
		{
			if (!Set.IsValidIndex(i))
				continue;

			void* ElemPtr = Ops.GetElement(Set, i);

			FDebuggerValue ElemValue;
			if (SubType.GetDebuggerValue(ElemPtr, ElemValue))
			{
				ElemValue.Name = FString::Printf(TEXT("[%d]"), i);
				Scope.Values.Add(MoveTemp(ElemValue));
			}
		}

		{
			FDebuggerValue NumValue;
			NumValue.Name = TEXT("Num");
			NumValue.Type = TEXT("int");
			NumValue.Value = LexToString(Set.Num());
			Scope.Values.Add(MoveTemp(NumValue));
		}

		return true;
	}

	bool GetDebuggerMember(const FAngelscriptTypeUsage& Usage, void* Address, const FString& Member, struct FDebuggerValue& Value) const override
	{
		if (Usage.SubTypes.Num() != 1)
			return false;

		const FAngelscriptTypeUsage& SubType = Usage.SubTypes[0];
		FScriptSet& Set = Usage.ResolvePrimitive<FScriptSet>(Address);

		if (Member.StartsWith(TEXT("[")) && Member.EndsWith(TEXT("]")))
		{
			int32 Index = -1;
			LexFromString(Index, *Member.Mid(1, Member.Len() - 2));

			if (!Set.IsValidIndex(Index))
				return false;

			FSetOperations Ops(SubType);
			void* ElemPtr = Ops.GetElement(Set, Index);

			if (SubType.GetDebuggerValue(ElemPtr, Value))
			{
				Value.Name = FString::Printf(TEXT("[%d]"), Index);
				return true;
			}

			return false;
		}
		else if (Member == TEXT("Num"))
		{
			Value.Name = TEXT("Num");
			Value.Type = TEXT("int");
			Value.Value = LexToString(Set.Num());
			return true;
		}

		return true;
	}

	bool GetCppForm(const FAngelscriptTypeUsage& Usage, FCppForm& OutCppForm) const override
	{
		if (Usage.SubTypes.Num() != 1)
			return false;

		FCppForm CppInner;
		if (Usage.SubTypes[0].GetCppForm(CppInner))
		{
			if (CppInner.CppType.Len() != 0 && !CppInner.bDisallowNativeNest)
			{
				OutCppForm.CppType = FString::Printf(TEXT("TSet<%s>"), *CppInner.CppType);
				OutCppForm.CppHeader = CppInner.CppHeader;
			}

			if (CppInner.CppGenericType.Len() != 0)
			{
				OutCppForm.CppGenericType = FString::Printf(TEXT("TSet<%s>"), *CppInner.CppGenericType);
			}
		}

		OutCppForm.TemplateObjectForm = TEXT("FScriptSet");
		return true;
	}

	bool CanCompare(const FAngelscriptTypeUsage& Usage) const override
	{
		return Usage.SubTypes.Num() == 1 && Usage.SubTypes[0].CanCompare();
	}

	bool IsValueEqual(const FAngelscriptTypeUsage& Usage, void* SourcePtr, void* DestinationPtr) const override
	{
		const FAngelscriptTypeUsage& SubType = Usage.SubTypes[0];
		FScriptSet& SourceSet = *(FScriptSet*)SourcePtr;
		FScriptSet& DestSet = *(FScriptSet*)DestinationPtr;

		check(SubType.CanCompare());

		FSetOperations Ops(SubType);
		return Ops.IsPermutation(SourceSet, DestSet);
	}
};

bool ValidateSetOperations(asITypeInfo* TemplateType, asCString* ErrorMessage);

void FAngelscriptSetBinds::Destruct(FScriptSet& Set, asCObjectType* Meta)
{
   auto* Ops = FSetOperations::GetSetOperations(Meta);
   if (Ops->bNeedDestruct)
   {
   	int32 Count = Set.GetMaxIndex();
   	for (int32 i = 0; i < Count; ++i)
   	{
   		if (Set.IsValidIndex(i))
   			Ops->Type.DestructValue(Ops->GetElement(Set, i));
   	}
   }
   Set.~FScriptSet();
}

void FAngelscriptSetBinds::Add(FScriptSet& Set, asCObjectType* Meta, void* Value)
{
#if AS_ITERATOR_DEBUGGING
	if (!CheckSetIteratorDebug(Set))
		return;
#endif

	auto* Ops = FSetOperations::GetSetOperations(Meta);

#if AS_REFERENCE_DEBUGGING
	InvalidateReferencesToSet(Set, Ops);
#endif

	Ops->Add(Set, Value);
}

void FAngelscriptSetBinds::AppendArray(FScriptSet& Set, asCObjectType* Meta, FScriptArray& SourceArray)
{
#if AS_ITERATOR_DEBUGGING
	if (!CheckSetIteratorDebug(Set))
		return;
#endif

	auto* Ops = FSetOperations::GetSetOperations(Meta);

#if AS_REFERENCE_DEBUGGING
	InvalidateReferencesToSet(Set, Ops);
#endif

	for (int i = 0, Count = SourceArray.Num(); i < Count; ++i)
	{
		void* Value = (void*)((SIZE_T)SourceArray.GetData() + (i*Ops->ValueSize));
		Ops->Add(Set, Value);
	}
}

void FAngelscriptSetBinds::AppendSet(FScriptSet& Set, asCObjectType* Meta, FScriptSet& SourceSet)
{
#if AS_ITERATOR_DEBUGGING
	if (!CheckSetIteratorDebug(Set))
		return;
#endif

	auto* Ops = FSetOperations::GetSetOperations(Meta);

#if AS_REFERENCE_DEBUGGING
	InvalidateReferencesToSet(Set, Ops);
#endif

	for (int32 i = 0, Num = SourceSet.GetMaxIndex(); i < Num; ++i)
	{
		if (SourceSet.IsValidIndex(i))
			Ops->Add(Set, Ops->GetElement(SourceSet, i));
	}
}

bool FAngelscriptSetBinds::Contains(FScriptSet& Set, asCObjectType* Meta, void* Value)
{
	auto* Ops = FSetOperations::GetSetOperations(Meta);
	int32 Index = Ops->FindIndex(Set, Value);
	return Index != INDEX_NONE;
}

bool FAngelscriptSetBinds::Remove(FScriptSet& Set, asCObjectType* Meta, void* Value)
{
#if AS_ITERATOR_DEBUGGING
	if (!CheckSetIteratorDebug(Set))
		return false;
#endif

	auto* Ops = FSetOperations::GetSetOperations(Meta);

#if AS_REFERENCE_DEBUGGING
	InvalidateReferencesToSet(Set, Ops);
#endif

	int32 Index = Ops->FindIndex(Set, Value);
	if (Index == INDEX_NONE)
		return false;

	Ops->RemoveAt(Set, Index);
	return true;
}

FScriptSet& FAngelscriptSetBinds::Assign(FScriptSet& DestinationSet, asCObjectType* Meta, FScriptSet& SourceSet)
{
#if AS_ITERATOR_DEBUGGING
	if (!CheckSetIteratorDebug(DestinationSet))
		return DestinationSet;
#endif

	auto* Ops = FSetOperations::GetSetOperations(Meta);

#if AS_REFERENCE_DEBUGGING
	InvalidateReferencesToSet(DestinationSet, Ops);
#endif

	Ops->Empty(DestinationSet, SourceSet.Num());

	for (int32 i = 0, Num = SourceSet.GetMaxIndex(); i < Num; ++i)
	{
		if (SourceSet.IsValidIndex(i))
			Ops->Add(DestinationSet, Ops->GetElement(SourceSet, i));
	}

	return DestinationSet;
}

void FAngelscriptSetBinds::Empty(FScriptSet& Set, asCObjectType* Meta, int32 Slack)
{
#if AS_ITERATOR_DEBUGGING
	if (!CheckSetIteratorDebug(Set))
		return;
#endif

	auto* Ops = FSetOperations::GetSetOperations(Meta);

#if AS_REFERENCE_DEBUGGING
	InvalidateReferencesToSet(Set, Ops);
#endif

	Ops->Empty(Set, Slack);
}

void FAngelscriptSetBinds::Reset(FScriptSet& Set, asCObjectType* Meta)
{
#if AS_ITERATOR_DEBUGGING
	if (!CheckSetIteratorDebug(Set))
		return;
#endif

	auto* Ops = FSetOperations::GetSetOperations(Meta);

#if AS_REFERENCE_DEBUGGING
	InvalidateReferencesToSet(Set, Ops);
#endif

	Ops->Empty(Set, Set.Num());
}

struct FAngelscriptSetIteratorType : public FAngelscriptType
{
	FString GetAngelscriptTypeName() const override
	{
		return TEXT("TSetIterator");
	}

	virtual bool CanQueryPropertyType() const override { return false; }

	bool GetCppForm(const FAngelscriptTypeUsage& Usage, FCppForm& OutCppForm) const override
	{
		OutCppForm.TemplateObjectForm = TEXT("FSetIterator");
		return true;
	}
};

struct FAngelscriptSetConstIteratorType : public FAngelscriptType
{
	FString GetAngelscriptTypeName() const override
	{
		return TEXT("TSetConstIterator");
	}

	virtual bool CanQueryPropertyType() const override { return false; }

	bool GetCppForm(const FAngelscriptTypeUsage& Usage, FCppForm& OutCppForm) const override
	{
		OutCppForm.TemplateObjectForm = TEXT("FSetIterator");
		return true;
	}
};

AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_TSet((int32)FAngelscriptBinds::EOrder::Early+1, []
{
	FBindFlags Flags;
	Flags.bTemplate = true;
	Flags.TemplateType = "<T>";
	Flags.ExtraFlags = asOBJ_TEMPLATE_SUBTYPE_COVARIANT;

	auto TSet_ = FAngelscriptBinds::ValueClass<FScriptSet>("TSet<class T>", Flags);
	TSet_.Constructor("void f()", FUNC_TRIVIAL(FAngelscriptSetBinds::Construct));

	TSet_.Destructor("void f()", &FAngelscriptSetBinds::Destruct);
	SCRIPT_NATIVE_TEMPLATED_CALL(TSet_, "FAngelscriptSetBinds::Destruct", false);
	FAngelscriptBinds::PreviousBindPassScriptObjectTypeAsFirstParam();

	TSet_.TemplateCallback("bool f(int&in Type, int&out ErrorMessage)",
	[](asITypeInfo* TemplateType, asCString* ErrorMessage) -> bool
	{
		return ValidateSetOperations(TemplateType, ErrorMessage);
	});

	TSet_.Method("void Add(const T&in if_handle_then_const Value)", &FAngelscriptSetBinds::Add);
	SCRIPT_NATIVE_TEMPLATED_CALL_NEEDSCOPY_NEEDSCOMPARE(TSet_, "FAngelscriptSetBinds::Add", false);
	FAngelscriptBinds::PreviousBindPassScriptObjectTypeAsFirstParam();

	TSet_.Method("void Append(const TArray<T>& Array)", &FAngelscriptSetBinds::AppendArray);
	SCRIPT_NATIVE_TEMPLATED_CALL_NEEDSCOPY_NEEDSCOMPARE(TSet_, "FAngelscriptSetBinds::AppendArray", false);
	FAngelscriptBinds::PreviousBindPassScriptObjectTypeAsFirstParam();

	TSet_.Method("void Append(const TSet<T>& Set)", &FAngelscriptSetBinds::AppendSet);
	SCRIPT_NATIVE_TEMPLATED_CALL_NEEDSCOPY_NEEDSCOMPARE(TSet_, "FAngelscriptSetBinds::AppendSet", false);
	FAngelscriptBinds::PreviousBindPassScriptObjectTypeAsFirstParam();

	TSet_.Method("bool Contains(const T&in if_handle_then_const Value) const", &FAngelscriptSetBinds::Contains);
	SCRIPT_NATIVE_TEMPLATED_CALL_NEEDSCOMPARE(TSet_, "FAngelscriptSetBinds::Contains", true);
	FAngelscriptBinds::PreviousBindPassScriptObjectTypeAsFirstParam();

	TSet_.Method("bool Remove(const T&in if_handle_then_const Value)", &FAngelscriptSetBinds::Remove);
	SCRIPT_NATIVE_TEMPLATED_CALL_NEEDSCOPY_NEEDSCOMPARE(TSet_, "FAngelscriptSetBinds::Remove", false);
	FAngelscriptBinds::PreviousBindPassScriptObjectTypeAsFirstParam();

	TSet_.Method("TSet<T>& opAssign(const TSet<T>& Other)", &FAngelscriptSetBinds::Assign);
	SCRIPT_NATIVE_TEMPLATED_CALL_NEEDSCOPY_NEEDSCOMPARE(TSet_, "FAngelscriptSetBinds::Assign", false);
	FAngelscriptBinds::PreviousBindPassScriptObjectTypeAsFirstParam();

	TSet_.Method("void Empty(int32 Slack = 0)", &FAngelscriptSetBinds::Empty);
	SCRIPT_NATIVE_TEMPLATED_CALL(TSet_, "FAngelscriptSetBinds::Empty", false);
	FAngelscriptBinds::PreviousBindPassScriptObjectTypeAsFirstParam();

	TSet_.Method("void Reset()", &FAngelscriptSetBinds::Reset);
	SCRIPT_NATIVE_TEMPLATED_CALL(TSet_, "FAngelscriptSetBinds::Reset", false);
	FAngelscriptBinds::PreviousBindPassScriptObjectTypeAsFirstParam();

	TSet_.Method("int32 Num() const", FUNC_TRIVIAL(FAngelscriptSetBinds::Num));
	TSet_.Method("bool IsEmpty() const", FUNC_TRIVIAL(FAngelscriptSetBinds::IsEmpty));

	auto SetType = MakeShared<FAngelscriptSetType>();
	FAngelscriptType::Register(SetType);

	FAngelscriptType::RegisterTypeFinder([SetType](FProperty* Property, FAngelscriptTypeUsage& Usage) -> bool
	{
		FSetProperty* SetProp = CastField<FSetProperty>(Property);
		if (SetProp == nullptr)
			return false;

		FAngelscriptTypeUsage InnerUsage = FAngelscriptTypeUsage::FromProperty(SetProp->ElementProp);
		if (!InnerUsage.IsValid())
			return false;

		Usage.Type = SetType;
		Usage.SubTypes.Add(InnerUsage);
		return true;
	});

	auto SetIteratorType = MakeShared<FAngelscriptSetIteratorType>();
	FAngelscriptType::Register(SetIteratorType);

	FBindFlags ItFlags;
	ItFlags.bTemplate = true;
	ItFlags.TemplateType = "<T>";

	auto TSetIterator_ = FAngelscriptBinds::ValueClass<FSetIterator>("TSetIterator<class T>", ItFlags);
	TSetIterator_.Constructor("void f(const TSetIterator<T>& Other)", FUNC_TRIVIAL(FSetIterator::CopyConstruct));

#if AS_ITERATOR_DEBUGGING
	TSetIterator_.Destructor("void f()", &FSetIterator::Destruct);
#endif

	TSetIterator_.Method("TSetIterator<T>& opAssign(const TSetIterator<T>& Other)", METHOD_TRIVIAL(FSetIterator, Assignment));
	TSetIterator_.Property("bool CanProceed", &FSetIterator::bCanProceed);
	TSetIterator_.Method("const T& Proceed()", METHOD(FSetIterator, Proceed));

	auto SetConstIteratorType = MakeShared<FAngelscriptSetConstIteratorType>();
	FAngelscriptType::Register(SetConstIteratorType);

	auto TSetConstIterator_ = FAngelscriptBinds::ValueClass<FSetIterator>("TSetConstIterator<class T>", ItFlags);
	TSetConstIterator_.Constructor("void f(const TSetConstIterator<T>& Other)", FUNC_TRIVIAL(FSetIterator::CopyConstruct));

#if AS_ITERATOR_DEBUGGING
	TSetConstIterator_.Destructor("void f()", &FSetIterator::Destruct);
#endif

	TSetConstIterator_.Method("TSetConstIterator<T>& opAssign(const TSetConstIterator<T>& Other)", METHOD_TRIVIAL(FSetIterator, Assignment));
	TSetConstIterator_.Property("bool CanProceed", &FSetIterator::bCanProceed);
	TSetConstIterator_.Method("const T& Proceed()", METHOD(FSetIterator, Proceed));

	TSet_.Method("TSetIterator<T> Iterator()", FUNC_TRIVIAL(FSetIterator::Create));
	FAngelscriptBinds::PreviousBindPassScriptObjectTypeAsFirstParam();

	TSet_.Method("TSetConstIterator<T> Iterator() const", FUNC_TRIVIAL(FSetIterator::Create));
	FAngelscriptBinds::PreviousBindPassScriptObjectTypeAsFirstParam();
});

bool ValidateSetOperations(asITypeInfo* TemplateType, asCString* ErrorMessage)
{
	FSetOperations* Ops = (FSetOperations*)TemplateType->GetUserData();
	if (Ops != nullptr)
		return Ops->bValid;
		
	int32 SubTypeId = TemplateType->GetSubTypeId(0);
	auto Type = FAngelscriptTypeUsage::FromTypeId(SubTypeId);

	// We don't allow containers of templated types,
	if (!Type.CanBeTemplateSubType())
	{
		if (ErrorMessage != nullptr)
			*ErrorMessage = "Containers cannot be nested in other containers";
		return false;
	}

	Ops = new FSetOperations(Type);

	bool bCanHash = Type.CanHashValue();
	if (!bCanHash)
	{
		if (asCTypeInfo* SubType = (asCTypeInfo*)TemplateType->GetSubType(0))
		{
			auto* ObjectType = CastToObjectType(SubType);
			if (ObjectType != nullptr && ObjectType->GetFirstMethod("Hash") != nullptr)
			{
				const FString HashDecl = TEXT("uint32 Hash() const");
				Ops->HashFunction = SubType->GetMethodByDecl(TCHAR_TO_ANSI(*HashDecl));
				bCanHash = Ops->HashFunction != nullptr;
			}
		}
	}

	Ops->bValid = Type.CanConstruct() && Type.CanDestruct() && Type.CanCopy() && Type.CanCompare() && bCanHash;
	TemplateType->SetUserData(Ops);

	if (!Ops->bValid && ErrorMessage != nullptr)
	{
		if (bCanHash)
		{
			*ErrorMessage = "Key type does not have a hash function defined";
		}
		else
		{
			*ErrorMessage = "Subtype cannot be constructed or copied";
		}
	}

	return Ops->bValid;
}

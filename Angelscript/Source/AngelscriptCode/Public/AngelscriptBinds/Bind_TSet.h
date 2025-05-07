#pragma once
#include "CoreMinimal.h"
#include "UObject/UnrealType.h"
#include "Containers/Set.h"
#include "AngelscriptType.h"
#include "AngelscriptManager.h"

#include "StartAngelscriptHeaders.h"
//#include "as_context.h"
//#include "as_scriptfunction.h"
//#include "as_objecttype.h"
#include "source/as_context.h"
#include "source/as_scriptfunction.h"
#include "source/as_objecttype.h"
#include "EndAngelscriptHeaders.h"

struct ANGELSCRIPTCODE_API FSetOperations
{
	bool bValid;
	FAngelscriptTypeUsage Type;
	FScriptSetLayout Layout;
	int32 ValueSize;

	bool bNeedConstruct;
	bool bNeedDestruct;
	bool bNeedCopy;
	
	asIScriptFunction* HashFunction;

	FSetOperations()
	{
		bValid = false;
		HashFunction = nullptr;
	}

	FSetOperations(const FAngelscriptTypeUsage& Usage)
	{
		Type = Usage;
		ValueSize = Type.GetValueSize();
		Layout = FScriptSet::GetScriptLayout(ValueSize, Type.GetValueAlignment());

		bNeedConstruct = Type.NeedConstruct();
		bNeedDestruct = Type.NeedDestruct();
		bNeedCopy = Type.NeedCopy();
		
		HashFunction = nullptr;
	}

	static FORCEINLINE FSetOperations* GetSetOperations(asCObjectType* Meta)
	{
		return (FSetOperations*)Meta->plainUserData;
	}

#if AS_ITERATOR_DEBUGGING
	static void MarkSetBeingIterated(FScriptSet& Set);
	static void UnmarkSetBeingIterated(FScriptSet& Set);
#endif

	FORCEINLINE uint32 GetHash(const void* Ptr) const
	{
		if (this->HashFunction != nullptr)
		{
			return this->InvokeHashFunction(Ptr);
		}

		// If we don't have a hashing function, GetHash must be valid if this is being called.
		return this->Type.GetHash(Ptr);
	}

	uint32 InvokeHashFunction(const void* Object) const
	{
		ensure(HashFunction != nullptr);
		FAngelscriptContext Context;
		Context->Prepare(HashFunction);
		Context->SetObject(const_cast<void*>(Object));
		Context->Execute();
		return Context->GetReturnDWord();
	}

	FORCEINLINE void* GetElement(FScriptSet& Set, int32 Index)
	{
		return Set.GetData(Index, Layout);
	}

	FORCEINLINE_DEBUGGABLE int32 FindIndex(FScriptSet& Set, void* Value)
	{
		auto GetKeyHash = [this](const void* Ptr) -> uint32
		{
			return GetHash(Ptr);
		};

		auto Compare = [this](const void* A, const void* B) -> bool
		{
			return Type.IsValueEqual((void*)A, (void*)B);
		};

		return Set.FindIndex(Value, Layout, GetKeyHash, Compare);
	}

	FORCEINLINE_DEBUGGABLE void Empty(FScriptSet& Set, int32 Slack)
	{
		if (bNeedDestruct)
		{
			int32 Count = Set.GetMaxIndex();
			for (int32 i = 0; i < Count; ++i)
			{
				if (Set.IsValidIndex(i))
					Type.DestructValue(GetElement(Set, i));
			}
		}
		Set.Empty(Slack, Layout);
	}

	FORCEINLINE_DEBUGGABLE void RemoveAt(FScriptSet& Set, int32 Index)
	{
		if (bNeedDestruct)
			Type.DestructValue(GetElement(Set, Index));
		Set.RemoveAt(Index, Layout);
	}

	FORCEINLINE_DEBUGGABLE void Add(FScriptSet& Set, void* Value)
	{
		auto GetKeyHash = [this](const void* Ptr) -> uint32
		{
			return GetHash(Ptr);
		};

		auto Compare = [this](const void* A, const void* B) -> bool
		{
			return Type.IsValueEqual((void*)A, (void*)B);
		};

		auto Construct = [this, Value](void* Ptr)
		{
			if (bNeedConstruct)
				Type.ConstructValue(Ptr);

			if (bNeedCopy)
				Type.CopyValue(Value, Ptr);
			else
				FMemory::Memcpy(Ptr, Value, ValueSize);
		};

		auto Destruct = [this](void* Ptr)
		{
			if (bNeedDestruct)
				Type.DestructValue(Ptr);
		};

		Set.Add(Value, Layout, GetKeyHash, Compare, Construct, Destruct);
	}

	FORCEINLINE_DEBUGGABLE int32 FindNextIndex(FScriptSet& Set, int32 Index)
	{
		int32 MaxIndex = Set.GetMaxIndex();
		for (int32 NextIndex = Index + 1; NextIndex < MaxIndex; ++NextIndex)
		{
			if (Set.IsValidIndex(NextIndex))
				return NextIndex;
		}
		return -1;
	}

	bool AnyEqual(FScriptSet& Set, int32 Index, int32 Num, void* ElementToCompare)
	{
		for (; Num; --Num)
		{
			while (!Set.IsValidIndex(Index))
				++Index;

			if (Type.IsValueEqual(GetElement(Set, Index), ElementToCompare))
				return true;

			++Index;
		}
		return false;
	}

	bool RangesContainSameAmountsOfVal(FScriptSet& SetA, int32 IndexA, FScriptSet& SetB, int32 IndexB, int32 Num, void* ElementToCompare)
	{
		int32 CountA = 0;
		int32 CountB = 0;
		for (;;)
		{
			if (Num == 0)
				return CountA == CountB;

			while (!SetA.IsValidIndex(IndexA))
				++IndexA;

			while (!SetB.IsValidIndex(IndexB))
				++IndexB;

			void* ElementA = GetElement(SetA, IndexA);
			void* ElementB = GetElement(SetB, IndexB);
			if (Type.IsValueEqual(ElementA, ElementToCompare))
				++CountA;

			if (Type.IsValueEqual(ElementB, ElementToCompare))
				++CountB;

			++IndexA;
			++IndexB;
			--Num;
		}
	}

	bool IsPermutation(FScriptSet& SetA, FScriptSet& SetB)
	{
		int32 Num = SetA.Num();
		if (Num != SetB.Num())
			return false;

		// Skip over common initial sequence
		int32 IndexA = 0;
		int32 IndexB = 0;
		for (;;)
		{
			if (Num == 0)
				return true;

			while (!SetA.IsValidIndex(IndexA))
				++IndexA;

			while (!SetB.IsValidIndex(IndexB))
				++IndexB;

			void* ElementA = GetElement(SetA, IndexA);
			void* ElementB = GetElement(SetB, IndexB);
			if (!Type.IsValueEqual(ElementA, ElementB))
				break;

			++IndexA;
			++IndexB;
			--Num;
		}

		int32 FirstIndexA = IndexA;
		int32 FirstIndexB = IndexB;
		int32 FirstNum    = Num;
		for (;;)
		{
			void* ElementA = GetElement(SetA, IndexA);
			if (!AnyEqual(SetA, FirstIndexA, FirstNum - Num, ElementA) && !RangesContainSameAmountsOfVal(SetA, IndexA, SetB, IndexB, Num, ElementA))
				return false;

			--Num;
			if (Num == 0)
				return true;

			while (!SetA.IsValidIndex(IndexA))
				++IndexA;

			while (!SetB.IsValidIndex(IndexB))
				++IndexB;
		}
	}
};

struct ANGELSCRIPTCODE_API FAngelscriptSetBinds
{
	static void Construct(FScriptSet* Address)
	{
		new(Address) FScriptSet();
	}

	static void Destruct(FScriptSet& Set, asCObjectType* Meta);

	template<typename T>
	static void Destruct_Template(TSet<T>& Set)
	{
		Set.~TSet<T>();
	}

	static void Add(FScriptSet& Set, asCObjectType* Meta, void* Value);

	template<typename T>
	static void Add_Template(TSet<T>& Set, void* Value)
	{
		Set.Add(*(T*)Value);
	}

	static void AppendArray(FScriptSet& Set, asCObjectType* Meta, FScriptArray& SourceArray);

	template<typename T>
	static void AppendArray_Template(TSet<T>& Set, FScriptArray& SourceArray)
	{
		Set.Append((TArray<T>&)SourceArray);
	}

	static void AppendSet(FScriptSet& Set, asCObjectType* Meta, FScriptSet& SourceSet);

	template<typename T>
	static void AppendSet_Template(TSet<T>& Set, FScriptSet& SourceSet)
	{
		Set.Append((TSet<T>&)SourceSet);
	}

	static bool Contains(FScriptSet& Set, asCObjectType* Meta, void* Value);

	template<typename T>
	static bool Contains_Template(TSet<T>& Set, void* Value)
	{
		return Set.Contains(*(T*)Value);
	}

	static bool Remove(FScriptSet& Set, asCObjectType* Meta, void* Value);

	template<typename T>
	static bool Remove_Template(TSet<T>& Set, void* Value)
	{
		int32 RemovedCount = Set.Remove(*(T*)Value);
		return RemovedCount != 0;
	}

	static FScriptSet& Assign(FScriptSet& DestinationSet, asCObjectType* Meta, FScriptSet& SourceSet);

	template<typename T>
	static FScriptSet& Assign_Template(TSet<T>& DestinationSet, FScriptSet& SourceSet)
	{
		DestinationSet = *(TSet<T>*)&SourceSet;
		return *(FScriptSet*)&DestinationSet;
	}

	static void Empty(FScriptSet& Set, asCObjectType* Meta, int32 Slack);

	template<typename T>
	static void Empty_Template(TSet<T>& Set, int32 Slack)
	{
		Set.Empty(Slack);
	}

	static void Reset(FScriptSet& Set, asCObjectType* Meta);

	template<typename T>
	static void Reset_Template(TSet<T>& Set)
	{
		Set.Reset();
	}

	static int32 Num(FScriptSet& Set)
	{
		return Set.Num();
	}

	static bool IsEmpty(FScriptSet& Set)
	{
		return Set.IsEmpty();
	}
};

struct FSetIterator
{
	FScriptSet* Set;
	FSetOperations* Ops;

	int32 Index;
	int32 NextIndex;
	bool bCanProceed;

	FSetIterator() {}
	FSetIterator& operator=(const FSetIterator& Other) = default;

	static FSetIterator Create(FScriptSet& Set, asCObjectType* Meta)
	{
		auto* Ops = FSetOperations::GetSetOperations(Meta);

		FSetIterator It;
		It.Set = &Set;
		It.Ops = Ops;
		It.Index = -1;
		It.NextIndex = It.Ops->FindNextIndex(Set, -1);
		It.bCanProceed = It.NextIndex != -1;

#if AS_ITERATOR_DEBUGGING
		FSetOperations::MarkSetBeingIterated(Set);
#endif

		return It;
	}

	static void CopyConstruct(FSetIterator& Iterator, const FSetIterator& Other)
	{
#if AS_ITERATOR_DEBUGGING
		if (Other.Set != nullptr)
			FSetOperations::MarkSetBeingIterated(*Other.Set);
#endif

		Iterator = Other;
	}

#if AS_ITERATOR_DEBUGGING
	static void Destruct(FSetIterator& Iterator)
	{
		if (Iterator.Set != nullptr)
			FSetOperations::UnmarkSetBeingIterated(*Iterator.Set);
	}
#endif

	FSetIterator& Assignment(const FSetIterator& Other)
	{
#if AS_ITERATOR_DEBUGGING
		if (Set != nullptr)
			FSetOperations::UnmarkSetBeingIterated(*Set);
		if (Other.Set != nullptr)
			FSetOperations::MarkSetBeingIterated(*Other.Set);
#endif

		*this = Other;
		return *this;
	}

	void* Proceed()
	{
		if (NextIndex == -1)
		{
			FAngelscriptManager::Throw("Iterator out of bounds.");
			return nullptr;
		}

		Index = NextIndex;
		NextIndex = Ops->FindNextIndex(*Set, Index);
		bCanProceed = NextIndex != -1;

		return Ops->GetElement(*Set, Index);
	}
};

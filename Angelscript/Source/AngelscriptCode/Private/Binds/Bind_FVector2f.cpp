#include "Misc/DefaultValueHelper.h"

#include "AngelscriptBinds.h"
#include "AngelscriptManager.h"

#include "Helper_StructType.h"
#include "Helper_ToString.h"

struct FVector2fType : TAngelscriptVariantStructType<FVector2f>
{
	FString GetAngelscriptTypeName() const override
	{
		return TEXT("FVector2f");
	}

	void ConstructValue(const FAngelscriptTypeUsage& Usage, void* DestinationPtr) const override
	{
		new(DestinationPtr) FVector2f(0.f, 0.f);
	}

	bool NeedConstruct(const FAngelscriptTypeUsage& Usage) const override { return false; }
	bool NeedDestruct(const FAngelscriptTypeUsage& Usage) const override { return false; }

	bool DefaultValue_UnrealToAngelscript(const FAngelscriptTypeUsage& Usage, const FString& InValue, FString& OutValue) const override
	{
		if (InValue.IsEmpty())
		{
			OutValue = TEXT("FVector2f()");
			return true;
		}
		FVector2f Value;
		if (FDefaultValueHelper::ParseVector2D(InValue, Value))
		{
			OutValue = FString::Printf(TEXT("FVector2f(%f,%f)"), Value.X, Value.Y);
			return true;
		}
		return false;
	}

	bool DefaultValue_AngelscriptToUnreal(const FAngelscriptTypeUsage& Usage, const FString& CppForm, FString& OutForm) const override
	{
		FString Parameters;
		if(FDefaultValueHelper::Is( CppForm, TEXT("FVector2f :: ZeroVector") ))
		{
			return true;
		}
		else if(FDefaultValueHelper::Is(CppForm, TEXT("FVector2f :: UnitVector")))
		{
			OutForm = FString::Printf(TEXT("%f,%f"),
				FVector2f::UnitVector.X, FVector2f::UnitVector.Y);
		}
		else if( FDefaultValueHelper::GetParameters(CppForm, TEXT("FVector2f"), Parameters) )
		{
			FVector2f Vector;
			float Value;
			if (FDefaultValueHelper::ParseVector2D(Parameters, Vector))
			{
				OutForm = FString::Printf(TEXT("%f,%f"),
					Vector.X, Vector.Y);
			}
			else if (FDefaultValueHelper::ParseFloat(Parameters, Value))
			{
				OutForm = FString::Printf(TEXT("%f,%f"),
					Value, Value);
			}
		}

		return !OutForm.IsEmpty();
	}

	bool GetCppForm(const FAngelscriptTypeUsage& Usage, FCppForm& OutCppForm) const override
	{
		OutCppForm.CppType = GetAngelscriptTypeName();
		return true;
	}
};

AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_FVector2f(FAngelscriptBinds::EOrder::Early, []
{
	FBindFlags Flags;
	Flags.bPOD = true;
	Flags.ExtraFlags |= asOBJ_BASICMATHTYPE;

	auto FVector2f_ = FAngelscriptBinds::ValueClass<FVector2f>("FVector2f", Flags);
	FAngelscriptType::Register(MakeShared<FVector2fType>());

	FVector2f_.Constructor("void f(float32 X, float32 Y)", [](FVector2f* Address, float X, float Y)
	{
		new(Address) FVector2f(X, Y);
	});
	FAngelscriptBinds::SetPreviousBindNoDiscard(true);
	SCRIPT_TRIVIAL_NATIVE_CONSTRUCTOR(FVector2f_, "FVector2f");

	FVector2f_.Constructor("void f()", [](FVector2f* Address)
	{
		new(Address) FVector2f(0.f, 0.f);
	});
	FAngelscriptBinds::SetPreviousBindNoDiscard(true);
	SCRIPT_TRIVIAL_NATIVE_CONSTRUCTOR_CUSTOMFORM(FVector2f_, "FVector2f", "0.f, 0.f");

	FVector2f_.Constructor("void f(const FVector2f& Other)", [](FVector2f* Address, const FVector2f& Other)
	{
		new(Address) FVector2f(Other);
	});
	FAngelscriptBinds::SetPreviousBindNoDiscard(true);
	SCRIPT_TRIVIAL_NATIVE_CONSTRUCTOR(FVector2f_, "FVector2f");

	FVector2f_.Property("float32 X", &FVector2f::X);
	FVector2f_.Property("float32 Y", &FVector2f::Y);

	FVector2f_.Method("FVector2f& opAssign(const FVector2f& Other)", METHODPR_TRIVIAL(FVector2f&, FVector2f, operator=, (const FVector2f&)));

	FVector2f_.Method("FVector2f opAdd(const FVector2f& Other) const", METHODPR_TRIVIAL(FVector2f, FVector2f, operator+, (const FVector2f&) const));
	FVector2f_.Method("FVector2f opSub(const FVector2f& Other) const", METHODPR_TRIVIAL(FVector2f, FVector2f, operator-, (const FVector2f&) const));

	FVector2f_.Method("FVector2f opMul(const FVector2f& Other) const", METHODPR_TRIVIAL(FVector2f, FVector2f, operator*, (const FVector2f&) const));
	FVector2f_.Method("FVector2f opDiv(const FVector2f& Other) const", METHODPR_TRIVIAL(FVector2f, FVector2f, operator/, (const FVector2f&) const));

	FVector2f_.Method("FVector2f opMul(float32 Scale) const", METHODPR_TRIVIAL(FVector2f, FVector2f, operator*, (float) const));
	FVector2f_.Method("FVector2f opDiv(float32 Scale) const", METHODPR_TRIVIAL(FVector2f, FVector2f, operator/, (float) const));

	FVector2f_.Method("FVector2f opAdd(float32 Bias) const", METHODPR_TRIVIAL(FVector2f, FVector2f, operator+, (float) const));
	FVector2f_.Method("FVector2f opSub(float32 Bias) const", METHODPR_TRIVIAL(FVector2f, FVector2f, operator-, (float) const));

	FVector2f_.Method("FVector2f opNeg() const", METHODPR_TRIVIAL(FVector2f, FVector2f, operator-, () const));

	FVector2f_.Method("FVector2f opMulAssign(float32 Scale)", METHODPR_TRIVIAL(FVector2f, FVector2f, operator*=, (float)));
	FVector2f_.Method("FVector2f opDivAssign(float32 Scale)", METHODPR_TRIVIAL(FVector2f, FVector2f, operator/=, (float)));

	FVector2f_.Method("FVector2f opMulAssign(const FVector2f& Other)", METHODPR_TRIVIAL(FVector2f, FVector2f, operator*=, (const FVector2f&)));
	FVector2f_.Method("FVector2f opDivAssign(const FVector2f& Other)", METHODPR_TRIVIAL(FVector2f, FVector2f, operator/=, (const FVector2f&)));

	FVector2f_.Method("FVector2f opAddAssign(const FVector2f& Other)", METHODPR_TRIVIAL(FVector2f, FVector2f, operator+=, (const FVector2f&)));
	FVector2f_.Method("FVector2f opSubAssign(const FVector2f& Other)", METHODPR_TRIVIAL(FVector2f, FVector2f, operator-=, (const FVector2f&)));

	FVector2f_.Method("float32& opIndex(int32 Index)", METHODPR_TRIVIAL(float&, FVector2f, operator[], (int32)));
	FVector2f_.Method("float32 opIndex(int32 Index) const", METHODPR_TRIVIAL(float, FVector2f, operator[], (int32) const));

	FVector2f_.Method("bool opEquals(const FVector2f& Other) const", METHODPR_TRIVIAL(bool, FVector2f, operator==, (const FVector2f&) const));
	FVector2f_.Method("bool Equals(const FVector2f& Other, float32 Tolerance = __KINDA_SMALL_NUMBER_flt) const", METHOD_TRIVIAL(FVector2f, Equals));

	FVector2f_.Method("float32 CrossProduct(const FVector2f& Other) const", FUNC_TRIVIAL(FVector2f::CrossProduct));
	FVector2f_.Method("float32 DotProduct(const FVector2f& Other) const", FUNC_TRIVIAL(FVector2f::DotProduct));

	FVector2f_.Method("float32 GetMax() const", METHOD_TRIVIAL(FVector2f, GetMax));
	FVector2f_.Method("float32 GetAbsMax() const", METHOD_TRIVIAL(FVector2f, GetAbsMax));
	FVector2f_.Method("float32 GetMin() const", METHOD_TRIVIAL(FVector2f, GetMin));

	FVector2f_.Method("FVector2f GetAbs() const", METHOD_TRIVIAL(FVector2f, GetAbs));
	FVector2f_.Method("float32 Size() const", METHOD_TRIVIAL(FVector2f, Size));
	FVector2f_.Method("float32 SizeSquared() const", METHOD_TRIVIAL(FVector2f, SizeSquared));

	FVector2f_.Method("bool IsNearlyZero(float32 Tolerance = __KINDA_SMALL_NUMBER_flt) const", METHOD_TRIVIAL(FVector2f, IsNearlyZero));
	FVector2f_.Method("bool IsZero() const", METHOD_TRIVIAL(FVector2f, IsZero));

	FVector2f_.Method("void Normalize(float32 Tolerance = __SMALL_NUMBER_flt)", METHOD_TRIVIAL(FVector2f, Normalize));
	FVector2f_.Method("FVector2f GetSafeNormal(float32 Tolerance = __SMALL_NUMBER_flt) const", METHOD_TRIVIAL(FVector2f, GetSafeNormal));

	FVector2f_.Method("bool ContainsNaN() const", METHOD_TRIVIAL(FVector2f, ContainsNaN));
	FVector2f_.Method("FVector2f GetSignVector() const", METHOD_TRIVIAL(FVector2f, GetSignVector));

	FVector2f_.Method("float32 Distance(const FVector2f& Other) const", FUNC_TRIVIAL(FVector2f::Distance));
	FVector2f_.Method("float32 DistSquared(const FVector2f& Other) const", FUNC_TRIVIAL(FVector2f::DistSquared));

	FVector2f_.Method("FVector2f GetClampedToMaxSize(float32 Max) const", [](FVector2f& Vector, float MaxSize) -> FVector2f
	{
		if (MaxSize < KINDA_SMALL_NUMBER)
		{
			return FVector2f(0.f, 0.f);
		}

		const float VSq = Vector.SizeSquared();
		if (VSq > FMath::Square(MaxSize))
		{
			const float Scale = MaxSize * FMath::InvSqrt(VSq);
			return FVector2f(Vector.X*Scale, Vector.Y*Scale);
		}
		else
		{
			return Vector;
		}
	});

	{
		FAngelscriptBinds::FNamespace ns("FVector2f");
		FAngelscriptBinds::BindGlobalVariable("const FVector2f ZeroVector", &FVector2f::ZeroVector);
		FAngelscriptBinds::BindGlobalVariable("const FVector2f UnitVector", &FVector2f::UnitVector);
	}

	FToStringHelper::Register(TEXT("FVector2f"), [](void* Ptr, FString& Str)
	{
		Str += ((FVector2f*)Ptr)->ToString();
	});
});

AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_FVector2f_Conversion(FAngelscriptBinds::EOrder::Late, []
{
	auto FVector2f_ = FAngelscriptBinds::ExistingClass("FVector2f");
	FVector2f_.Constructor("void f(const FVector3f& Other)", [](FVector2f* Address, const FVector3f& Other)
	{
		new(Address) FVector2f(Other);
	});
	FAngelscriptBinds::SetPreviousBindNoDiscard(true);
	SCRIPT_TRIVIAL_NATIVE_CONSTRUCTOR(FVector2f_, "FVector2f");

	FVector2f_.Constructor("void f(const FVector2D& Other)", [](FVector2f* Address, const FVector2D& Other)
	{
		new(Address) FVector2f(Other);
	});
	FAngelscriptBinds::SetPreviousBindNoDiscard(true);
	SCRIPT_TRIVIAL_NATIVE_CONSTRUCTOR(FVector2f_, "FVector2f");

	FVector2f_.Method("bool InitFromString(const FString& SourceString)", METHOD_TRIVIAL(FVector2f, InitFromString));
});
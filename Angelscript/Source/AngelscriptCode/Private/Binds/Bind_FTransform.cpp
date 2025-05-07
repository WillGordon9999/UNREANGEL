#include "AngelscriptBinds.h"
#include "AngelscriptManager.h"

#include "Helper_StructType.h"
#include "Helper_ToString.h"
#include "AngelscriptDocs.h"

struct FTransformType : TAngelscriptBaseStructType<FTransform>
{
	FString GetAngelscriptTypeName() const override
	{
		return TEXT("FTransform");
	}

	bool CanCompare(const FAngelscriptTypeUsage& Usage) const override { return false; }
	bool IsValueEqual(const FAngelscriptTypeUsage& Usage, void* SourcePtr, void* DestinationPtr) const override
	{
		return false;
	}

	bool GetCppForm(const FAngelscriptTypeUsage& Usage, FCppForm& OutCppForm) const override
	{
		OutCppForm.CppType = GetAngelscriptTypeName();
		return true;
	}
};

AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_FTransform(FAngelscriptBinds::EOrder::Early, []
{
	FBindFlags Flags;
	Flags.bPOD = true;
	Flags.ExtraFlags |= asOBJ_BASICMATHTYPE;

	auto FTransform_ = FAngelscriptBinds::ValueClass<FTransform>("FTransform", Flags);
	FAngelscriptType::Register(MakeShared<FTransformType>());

	FTransform_.Constructor("void f()", [](FTransform* Address)
	{
		new(Address) FTransform();
	});
	FAngelscriptBinds::SetPreviousBindNoDiscard(true);
	SCRIPT_TRIVIAL_NATIVE_CONSTRUCTOR(FTransform_, "FTransform");

	FTransform_.Constructor("void f(const FTransform& Other)", [](FTransform* Address, const FTransform& Other)
	{
		new(Address) FTransform(Other);
	});
	FAngelscriptBinds::SetPreviousBindNoDiscard(true);
	SCRIPT_TRIVIAL_NATIVE_CONSTRUCTOR(FTransform_, "FTransform");

	{
		FAngelscriptBinds::FNamespace ns("FTransform");
		FAngelscriptBinds::BindGlobalVariable("const FTransform Identity", &FTransform::Identity);
	}

	FToStringHelper::Register(TEXT("FTransform"), [](void* Ptr, FString& Str)
	{
		Str += ((FTransform*)Ptr)->ToString();
	});
});

AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_FTransform_Interactions(FAngelscriptBinds::EOrder::Late, []
{
	auto FTransform_ = FAngelscriptBinds::ExistingClass("FTransform");

	FTransform_.Constructor("void f(const FVector& InTranslation)", [](FTransform* Address, const FVector& InTranslation)
	{
		new(Address) FTransform(InTranslation);
	});
	FAngelscriptBinds::SetPreviousBindNoDiscard(true);
	SCRIPT_TRIVIAL_NATIVE_CONSTRUCTOR(FTransform_, "FTransform");

	FTransform_.Constructor("void f(const FQuat& InRotation)", [](FTransform* Address, const FQuat& InRotation)
	{
		new(Address) FTransform(InRotation);
	});
	FAngelscriptBinds::SetPreviousBindNoDiscard(true);
	SCRIPT_TRIVIAL_NATIVE_CONSTRUCTOR(FTransform_, "FTransform");

	FTransform_.Constructor("void f(const FRotator& InRotation)", [](FTransform* Address, const FRotator& InRotation)
	{
		new(Address) FTransform(InRotation);
	});
	FAngelscriptBinds::SetPreviousBindNoDiscard(true);
	SCRIPT_TRIVIAL_NATIVE_CONSTRUCTOR(FTransform_, "FTransform");

	FTransform_.Constructor("void f(const FQuat& InRotation, const FVector& InTranslation, const FVector& InScale3D = FVector::OneVector)",
	[](FTransform* Address, const FQuat& InRotation, const FVector& InTranslation, const FVector& InScale3D)
	{
		new(Address) FTransform(InRotation, InTranslation, InScale3D);
	});
	FAngelscriptBinds::SetPreviousBindNoDiscard(true);
	SCRIPT_TRIVIAL_NATIVE_CONSTRUCTOR(FTransform_, "FTransform");

	FTransform_.Constructor("void f(const FRotator& InRotation, const FVector& InTranslation, const FVector& InScale3D = FVector::OneVector)",
	[](FTransform* Address, const FRotator& InRotation, const FVector& InTranslation, const FVector& InScale3D)
	{
		new(Address) FTransform(InRotation, InTranslation, InScale3D);
	});
	FAngelscriptBinds::SetPreviousBindNoDiscard(true);
	SCRIPT_TRIVIAL_NATIVE_CONSTRUCTOR(FTransform_, "FTransform");

	FTransform_.Constructor("void f(const FVector& InX, const FVector& InY, const FVector& InZ, const FVector& InTranslation)",
	[](FTransform* Address, const FVector& InX, const FVector& InY, const FVector& InZ, const FVector& InTranslation)
	{
		new(Address) FTransform(InX, InY, InZ, InTranslation);
	});
	FAngelscriptBinds::SetPreviousBindNoDiscard(true);
	SCRIPT_TRIVIAL_NATIVE_CONSTRUCTOR(FTransform_, "FTransform");

	FTransform_.Constructor("void f(const FTransform3f& Transform)", [](FTransform* Address, const FTransform3f& Transform)
	{
		new(Address) FTransform(Transform);
	});
	FAngelscriptBinds::SetPreviousBindNoDiscard(true);
	SCRIPT_TRIVIAL_NATIVE_CONSTRUCTOR(FTransform_, "FTransform");

	FTransform_.Method("FTransform& opAssign(const FTransform& Other)", METHODPR_TRIVIAL(FTransform&, FTransform, operator=, (const FTransform&)));
	FTransform_.Method("FTransform Inverse() const", METHOD_TRIVIAL(FTransform, Inverse));

	FTransform_.Method("void Blend(const FTransform& Atom1, const FTransform& Atom2, float32 Alpha)", METHODPR_TRIVIAL(void, FTransform, Blend, (const FTransform&,const FTransform&,float)));
	FTransform_.Method("void BlendWith(const FTransform& OtherAtom, float32 Alpha)", METHODPR_TRIVIAL(void, FTransform, BlendWith, (const FTransform&, float)));

	FTransform_.Method("FTransform opMul(const FTransform& Other) const", METHODPR_TRIVIAL(FTransform, FTransform, operator*, (const FTransform&)const));
	FTransform_.Method("FTransform opMul(const FQuat& Other) const", METHODPR_TRIVIAL(FTransform, FTransform, operator*, (const FQuat&)const));

	FTransform_.Method("void opMulAssign(const FTransform& Other)", METHODPR_TRIVIAL(void, FTransform, operator*=, (const FTransform&)));
	FTransform_.Method("void opMulAssign(const FQuat& Other)", METHODPR_TRIVIAL(void, FTransform, operator*=, (const FQuat&)));

	FTransform_.Method("void ScaleTranslation(const FVector& InScale3D)", METHODPR_TRIVIAL(void, FTransform, ScaleTranslation, (const FVector&)));
	FTransform_.Method("void ScaleTranslation(const float64& Scale)", METHODPR_TRIVIAL(void, FTransform, ScaleTranslation, (const double&)));
	FTransform_.Method("void RemoveScaling(float64 Tolerance=SMALL_NUMBER)", METHODPR_TRIVIAL(void, FTransform, RemoveScaling, (double)));

	FTransform_.Method("void SetToRelativeTransform(const FTransform& Other)", METHOD_TRIVIAL(FTransform, SetToRelativeTransform));
	
	FTransform_.Method("void Accumulate(const FTransform& SourceAtom)", METHODPR_TRIVIAL(void, FTransform, Accumulate, (const FTransform&)));
	SCRIPT_BIND_DOCUMENTATION(
	"Accumulates another transform with this one\n"
    "Rotation is accumulated multiplicatively (Rotation = SourceAtom.Rotation * Rotation)\n"
    "Translation is accumulated additively (Translation += SourceAtom.Translation)\n"
    "Scale3D is accumulated multiplicatively (Scale3D *= SourceAtom.Scale3D)\n"
    "@param SourceAtom The other transform to accumulate into this one"
     );

	FTransform_.Method("float32 GetMaximumAxisScale() const", METHODPR_TRIVIAL(float, FTransform, GetMaximumAxisScale, () const));
	FTransform_.Method("float32 GetMinimumAxisScale() const", METHODPR_TRIVIAL(float, FTransform, GetMinimumAxisScale, () const));

	FTransform_.Method("FTransform GetRelativeTransform(const FTransform& Other) const", METHOD_TRIVIAL(FTransform, GetRelativeTransform));
	FTransform_.Method("FTransform GetRelativeTransformReverse(const FTransform& Other) const", METHOD_TRIVIAL(FTransform, GetRelativeTransformReverse));

	FTransform_.Method("FVector TransformPosition(const FVector& V) const", METHOD_TRIVIAL(FTransform, TransformPosition));
	FTransform_.Method("FVector TransformPositionNoScale(const FVector& V) const", METHOD_TRIVIAL(FTransform, TransformPositionNoScale));

	FTransform_.Method("FVector InverseTransformPosition(const FVector& V) const", METHOD_TRIVIAL(FTransform, InverseTransformPosition));
	FTransform_.Method("FVector InverseTransformPositionNoScale(const FVector& V) const", METHOD_TRIVIAL(FTransform, InverseTransformPositionNoScale));

	FTransform_.Method("FVector TransformVector(const FVector& V) const", METHOD_TRIVIAL(FTransform, TransformVector));
	FTransform_.Method("FVector TransformVectorNoScale(const FVector& V) const", METHOD_TRIVIAL(FTransform, TransformVectorNoScale));

	FTransform_.Method("FVector InverseTransformVector(const FVector& V) const", METHOD_TRIVIAL(FTransform, InverseTransformVector));
	FTransform_.Method("FVector InverseTransformVectorNoScale(const FVector& V) const", METHOD_TRIVIAL(FTransform, InverseTransformVectorNoScale));

	FTransform_.Method("FQuat TransformRotation(const FQuat& Q) const", METHOD_TRIVIAL(FTransform, TransformRotation));
	FTransform_.Method("FQuat InverseTransformRotation(const FQuat& Q) const", METHOD_TRIVIAL(FTransform, InverseTransformRotation));

	FTransform_.Method("FVector SubtractTranslations(const FTransform& B) const", FUNC_TRIVIAL(FTransform::SubtractTranslations));

	FTransform_.Method("void NormalizeRotation()", METHOD_TRIVIAL(FTransform, NormalizeRotation));

	FTransform_.Method("bool IsRotationNormalized() const", METHOD_TRIVIAL(FTransform, IsRotationNormalized));

	FTransform_.Method("bool TranslationEquals(const FTransform& Other, float64 Tolerance = KINDA_SMALL_NUMBER) const", METHODPR_TRIVIAL(bool, FTransform, TranslationEquals, (const FTransform&, double) const));

	FTransform_.Method("bool EqualsNoScale(const FTransform& Other, float64 Tolerance = KINDA_SMALL_NUMBER) const", METHODPR_TRIVIAL(bool, FTransform, EqualsNoScale, (const FTransform&, double) const));

	FTransform_.Method("bool Equals(const FTransform& Other, float64 Tolerance = KINDA_SMALL_NUMBER) const", METHODPR_TRIVIAL(bool, FTransform, Equals, (const FTransform&, double) const));

	FTransform_.Method("FVector GetLocation() const", METHOD_TRIVIAL(FTransform, GetLocation));

	FTransform_.Method("bool ContainsNaN() const", METHOD_TRIVIAL(FTransform, ContainsNaN));
	FTransform_.Method("bool IsValid() const", METHOD_TRIVIAL(FTransform, IsValid));

	FTransform_.Method("float64 GetDeterminant() const", METHODPR_TRIVIAL(double, FTransform, GetDeterminant, () const));
	FTransform_.Method("FRotator Rotator() const", METHOD_TRIVIAL(FTransform, Rotator));

	FTransform_.Method("FVector GetTranslation() const", METHOD_TRIVIAL(FTransform, GetTranslation));
	FTransform_.Method("FVector GetScale3D() const", METHOD_TRIVIAL(FTransform, GetScale3D));
	FTransform_.Method("FQuat GetRotation() const", METHOD_TRIVIAL(FTransform, GetRotation));
	
	FTransform_.Method("FMatrix ToMatrixWithScale() const", METHOD_TRIVIAL(FTransform, ToMatrixWithScale));
	FTransform_.Method("FMatrix ToMatrixNoScale() const", METHOD_TRIVIAL(FTransform, ToMatrixNoScale));
	FTransform_.Method("FMatrix ToInverseMatrixWithScale() const", METHOD_TRIVIAL(FTransform, ToInverseMatrixWithScale));

	FTransform_.Method("void SetLocation(const FVector& Origin)", METHOD_TRIVIAL(FTransform, SetLocation));
	FTransform_.Method("void SetTranslation(const FVector& Origin)", METHOD_TRIVIAL(FTransform, SetTranslation));

	FTransform_.Method("void AddToTranslation(const FVector& Origin)", METHOD_TRIVIAL(FTransform, AddToTranslation));

	FTransform_.Method("void ConcatenateRotation(const FQuat& DeltaRotation)", METHOD_TRIVIAL(FTransform, ConcatenateRotation));
	SCRIPT_BIND_DOCUMENTATION(
	"Concatenates another rotation to this transformation\n"
    "@param DeltaRotation The rotation to concatenate in the following fashion: Rotation = Rotation * DeltaRotation"
    );

	FTransform_.Method("void SetRotation(const FQuat& NewRotation)", METHOD_TRIVIAL(FTransform, SetRotation));
	
	FTransform_.Method("void SetScale3D(const FVector& NewScale3D)", METHOD_TRIVIAL(FTransform, SetScale3D));
	FTransform_.Method("void SetTranslationAndScale3D(const FVector& NewTranslation, const FVector& NewScale3D)", METHOD_TRIVIAL(FTransform, SetTranslationAndScale3D));
	FTransform_.Method("bool InitFromString(const FString& SourceString)", METHOD_TRIVIAL(FTransform, InitFromString));
});
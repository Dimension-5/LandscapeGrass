// Copyright Epic Games, Inc. All Rights Reserved.

#ifdef NNE_USE_DIRECTML

#include "NNEDmlOperator.h"
#include "NNEDmlOperatorUtils.h"

namespace UE::NNERuntimeRDG::Private::Dml
{

class FOperatorDmlUnsqueeze : public FOperatorDml
{

public:

	static FOperatorDml* Create()
	{
		return new FOperatorDmlUnsqueeze();
	}

	static bool Validate(const NNE::FAttributeMap& AttributeMap, TConstArrayView<ENNETensorDataType> InputTypes, TConstArrayView<NNE::FSymbolicTensorShape> InputShapes)
	{
		//TODO
		return true;
	}

	//
	//
	//
	virtual bool Initialize(IDMLDevice* Device, TArrayView<const NNE::Internal::FTensor> InputTensors, TArrayView<const NNE::Internal::FTensor> OutputTensors, const NNE::FAttributeMap& Attributes) override
	{
		check(InputTensors.Num() == 1);
		check(OutputTensors.Num() == 1);

		const FNNEAttributeValue* AxesAttr = Attributes.GetAttributeValue(TEXT("axes"));
		if (!AxesAttr)
		{
			UE_LOG(LogNNE, Warning, TEXT("Missing axes attribute for operator Unsqueeze"));
			return false;
		}

		TArray<int32>			Axes = AxesAttr->GetValue<TArray<int32>>();
		TConstArrayView<uint32>	InputShape = InputTensors[0].GetShape().GetData();
		const int32_t			OutShapeRank = InputShape.Num() + Axes.Num();

		HandleNegativeAxes(Axes, OutShapeRank);
		Axes.Sort();

		Util::FSmallUIntArray	OutputShape;

		OutputShape.Reserve(OutShapeRank);
		OutputShape.Append(InputShape);
		
		for (int32 Idx = 0; Idx < Axes.Num(); ++Idx)
		{
			if (Axes[Idx] >= OutShapeRank)
			{
				UE_LOG(LogNNE, Warning, TEXT("Unsqueeze operator does not support axes greater than the number of dimensions of the resulting tensor shape"));
				return false;
			}

			OutputShape.Insert(1, Axes[Idx]);
		}

		FTensorDescDml DmlTensorDesc;
		
		if (!DmlTensorDesc
				.SetFromTensor(OutputTensors[0])
				.SetShape(OutputShape)
				.Validate())
		{
			UE_LOG(LogNNE, Warning, TEXT("Failed to initialize Unsqueeze's output tensor for DML inference"));
			return false;
		}

		DML_ELEMENT_WISE_IDENTITY_OPERATOR_DESC DmlIdentityOpDesc{};

		DmlIdentityOpDesc.InputTensor = DmlTensorDesc.GetDmlDesc();
		DmlIdentityOpDesc.OutputTensor = DmlTensorDesc.GetDmlDesc();

		return CreateOperator(Device, DML_OPERATOR_DESC{ DML_OPERATOR_ELEMENT_WISE_IDENTITY, &DmlIdentityOpDesc });
	}
};

// Register Reshape operator on Module startup
NNE_DML_REGISTER_OP(Unsqueeze)

} // namespace UE::NNERuntimeRDG::Private::Dml

#endif // NNE_USE_DIRECTML

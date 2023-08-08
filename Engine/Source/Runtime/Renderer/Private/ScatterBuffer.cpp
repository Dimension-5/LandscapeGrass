#include "ScatterBuffer.h"
#include "CoreMinimal.h"
#include "HAL/ThreadSafeCounter.h"
#include "Stats/Stats.h"
#include "RHI.h"
#include "PrecomputedVolumetricLightmap.h"
#include "Components/LightComponent.h"
#include "Components/ReflectionCaptureComponent.h"
#include "Rendering/MotionVectorSimulation.h"
#include "LightMapRendering.h"
#include "ScenePrivate.h"
#include "Components/WindDirectionalSourceComponent.h"
#if RHI_RAYTRACING
#include "RayTracingDynamicGeometryCollection.h"
#endif
#include "ClearQuad.h"

template<EScatterUploadBufferType UploadBufferType>
class FGVDBScatterUploadCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FGVDBScatterUploadCS, Global);
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), GVDB_THREAD_COUNT);
		switch (UploadBufferType)
		{
		case EScatterUploadBufferType::E_SUBT_FLOAT_BUFFER:
			OutEnvironment.SetDefine(TEXT("FLOAT_BUFFER"), 1);
			break;
		case EScatterUploadBufferType::E_SUBT_FLOAT2_BUFFER:
			OutEnvironment.SetDefine(TEXT("FLOAT2_BUFFER"), 1);
			break;
		case EScatterUploadBufferType::E_SUBT_FLOAT3_BUFFER:
			OutEnvironment.SetDefine(TEXT("FLOAT3_BUFFER"), 1);
			break;
		case EScatterUploadBufferType::E_SUBT_FLOAT4_BUFFER:
			OutEnvironment.SetDefine(TEXT("FLOAT4_BUFFER"), 1);
			break;
		case EScatterUploadBufferType::E_SUBT_SINT_BUFFER:
			OutEnvironment.SetDefine(TEXT("SINT_BUFFER"), 1);
			break;
		case EScatterUploadBufferType::E_SUBT_SINT4_BUFFER:
			OutEnvironment.SetDefine(TEXT("SINT4_BUFFER"), 1);
			break;
		case EScatterUploadBufferType::E_SUBT_UINT_BUFFER:
			OutEnvironment.SetDefine(TEXT("UINT_BUFFER"), 1);
			break;
		default:
			check(false);
			break;
		}
	}

	FGVDBScatterUploadCS()
	{

	}

	/** Initialization constructor. */
	explicit FGVDBScatterUploadCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		NumScatters.Bind(Initializer.ParameterMap, TEXT("NumScatters"));
		ScatterBuffer.Bind(Initializer.ParameterMap, TEXT("ScatterBuffer"));
		Src.Bind(Initializer.ParameterMap, TEXT("Src"));
		Dst.Bind(Initializer.ParameterMap, TEXT("Dst"));
	}

	/**
	* Set input parameters.
	*/
	void SetParameters(
		FRHICommandList& RHICmdList
		, uint32 InNumScatters
		, FRHIShaderResourceView* InScatterBufferSRV
		, FRHIShaderResourceView* InSRV
		, FRHIUnorderedAccessView* OutDst
	)
	{
		FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();
		SetShaderValue(RHICmdList, ComputeShaderRHI, NumScatters, InNumScatters);
		SetSRVParameter(RHICmdList, ComputeShaderRHI, ScatterBuffer, InScatterBufferSRV);
		SetSRVParameter(RHICmdList, ComputeShaderRHI, Src, InSRV);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, Dst, OutDst);
	}

	/**
	* Unbinds any buffers that have been bound.
	*/
	void UnbindBuffers(FRHICommandList& RHICmdList)
	{
		FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();
		SetUAVParameter(RHICmdList, ComputeShaderRHI, Dst, nullptr);
	}

	static const TCHAR* GetSourceFilename()
	{
		return TEXT("/Engine/Private/GVDBScatterUpload.usf");
	}

	static const TCHAR* GetFunctionName()
	{
		return TEXT("GVDBScatterUploadCS");
	}

private:
	LAYOUT_FIELD(FShaderParameter, NumScatters);
	LAYOUT_FIELD(FShaderResourceParameter, ScatterBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, Src);
	LAYOUT_FIELD(FShaderResourceParameter, Dst);
};
IMPLEMENT_SHADER_TYPE2(FGVDBScatterUploadCS<EScatterUploadBufferType::E_SUBT_FLOAT_BUFFER>, SF_Compute);
IMPLEMENT_SHADER_TYPE2(FGVDBScatterUploadCS<EScatterUploadBufferType::E_SUBT_FLOAT2_BUFFER>, SF_Compute);
IMPLEMENT_SHADER_TYPE2(FGVDBScatterUploadCS<EScatterUploadBufferType::E_SUBT_FLOAT3_BUFFER>, SF_Compute);
IMPLEMENT_SHADER_TYPE2(FGVDBScatterUploadCS<EScatterUploadBufferType::E_SUBT_FLOAT4_BUFFER>, SF_Compute);
IMPLEMENT_SHADER_TYPE2(FGVDBScatterUploadCS<EScatterUploadBufferType::E_SUBT_SINT_BUFFER>, SF_Compute);
IMPLEMENT_SHADER_TYPE2(FGVDBScatterUploadCS<EScatterUploadBufferType::E_SUBT_SINT4_BUFFER>, SF_Compute);
IMPLEMENT_SHADER_TYPE2(FGVDBScatterUploadCS<EScatterUploadBufferType::E_SUBT_UINT_BUFFER>, SF_Compute);

template<EScatterUploadBufferType UploadBufferType>
void GVDBScatterUpload(
	FRHICommandList& RHICmdList,
	uint32 NumScatters,
	uint32 ThreadGroupSize,
	FRHIShaderResourceView* ScatterBufferSRV,
	FRHIShaderResourceView* UploadBufferSRV,
	FRHIUnorderedAccessView* DstBufferUAV)
{
	TShaderMapRef<FGVDBScatterUploadCS<UploadBufferType>> UploadCS(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	SetComputePipelineState(RHICmdList, UploadCS.GetComputeShader());
	UploadCS->SetParameters(RHICmdList, NumScatters, ScatterBufferSRV, UploadBufferSRV, DstBufferUAV);
	DispatchComputeShader(
		RHICmdList,
		UploadCS,
		FMath::DivideAndRoundUp(NumScatters, ThreadGroupSize), 1, 1);
	UploadCS->UnbindBuffers(RHICmdList);
}

FGVDBScatterUploadBuffer::FGVDBScatterUploadBuffer()
{
	ScatterData = nullptr;
	UploadData = nullptr;

	NumScatters = 0;
	MaxScatters = 0;
	NumScattersAllocated = 0;
	BytePerElement = 0;
	ElementOffset = 0;
	Format = EPixelFormat::PF_Unknown;
}

void FGVDBScatterUploadBuffer::Init(
	uint32 InNumBytesPerElement,
	uint32 InNumElements,
	EPixelFormat InFormat,
	const TCHAR* DebugName,
	uint32 InElementOffset)
{
	check(ScatterData == nullptr);
	check(UploadData == nullptr);

	NumScatters = 0;
	MaxScatters = InNumElements;
	NumScattersAllocated = FMath::RoundUpToPowerOfTwo(InNumElements);
	ElementOffset = InElementOffset;

	if ((NumScattersAllocated * sizeof(uint32)) > ScatterBuffer.NumBytes)
	{
		ScatterBuffer.Release();
		ScatterBuffer.Initialize(DebugName ? DebugName : TEXT("GVDBScatterBuffer"), GPixelFormats[PF_R32_UINT].BlockBytes,
			NumScattersAllocated, PF_R32_UINT, BUF_Dynamic);
	}

	if (Format != InFormat || InNumBytesPerElement != BytePerElement || (NumScattersAllocated * BytePerElement) > UploadBuffer.NumBytes)
	{
		BytePerElement = InNumBytesPerElement;
		Format = InFormat;
		check(BytePerElement == GPixelFormats[Format].BlockBytes);

		UploadBuffer.Release();
		UploadBuffer.Initialize(DebugName ? DebugName : TEXT("GVDBUploadBuffer"), GPixelFormats[Format].BlockBytes,
			NumScattersAllocated, Format, BUF_Dynamic);
	}
	
	ScatterData = (uint32*)RHILockBuffer(ScatterBuffer.Buffer, 0, ScatterBuffer.NumBytes, RLM_WriteOnly);
	UploadData = (uint8*)RHILockBuffer(UploadBuffer.Buffer, 0, UploadBuffer.NumBytes, RLM_WriteOnly);
}

void FGVDBScatterUploadBuffer::Add(uint32 Index, const void* Data, uint32 Num)
{
	for (uint32 i = 0; i < Num; i++)
	{
		ScatterData[i] = Index + i + ElementOffset;
	}
	void* Result = UploadData;
	ScatterData += Num;
	UploadData += Num * BytePerElement;
	NumScatters += Num;
	FMemory::Memcpy(Result, Data, Num * BytePerElement);
}

void FGVDBScatterUploadBuffer::ResourceUploadTo(
	FRHICommandList& RHICmdList, FRWBuffer& DstBuffer, EScatterUploadBufferType BufferType, bool bFlush/* = false*/)
{
	RHIUnlockBuffer(ScatterBuffer.Buffer);
	RHIUnlockBuffer(UploadBuffer.Buffer);

	ScatterData = nullptr;
	UploadData = nullptr;

	if (NumScatters == 0)
	{
		return;
	}

	constexpr uint32 ThreadGroupSize = GVDB_THREAD_COUNT;

	switch (BufferType)
	{
	case EScatterUploadBufferType::E_SUBT_FLOAT_BUFFER:
		GVDBScatterUpload<EScatterUploadBufferType::E_SUBT_FLOAT_BUFFER>(
			RHICmdList,
			NumScatters,
			ThreadGroupSize,
			ScatterBuffer.SRV,
			UploadBuffer.SRV,
			DstBuffer.UAV);
		break;
	case EScatterUploadBufferType::E_SUBT_FLOAT2_BUFFER:
		GVDBScatterUpload<EScatterUploadBufferType::E_SUBT_FLOAT2_BUFFER>(
			RHICmdList,
			NumScatters,
			ThreadGroupSize,
			ScatterBuffer.SRV,
			UploadBuffer.SRV,
			DstBuffer.UAV);
		break;
	case EScatterUploadBufferType::E_SUBT_FLOAT3_BUFFER:
		GVDBScatterUpload<EScatterUploadBufferType::E_SUBT_FLOAT3_BUFFER>(
			RHICmdList,
			NumScatters,
			ThreadGroupSize,
			ScatterBuffer.SRV,
			UploadBuffer.SRV,
			DstBuffer.UAV);
		break;
	case EScatterUploadBufferType::E_SUBT_FLOAT4_BUFFER:
		GVDBScatterUpload<EScatterUploadBufferType::E_SUBT_FLOAT4_BUFFER>(
			RHICmdList,
			NumScatters,
			ThreadGroupSize,
			ScatterBuffer.SRV,
			UploadBuffer.SRV,
			DstBuffer.UAV);
		break;
	case EScatterUploadBufferType::E_SUBT_SINT_BUFFER:
		GVDBScatterUpload<EScatterUploadBufferType::E_SUBT_SINT_BUFFER>(
			RHICmdList,
			NumScatters,
			ThreadGroupSize,
			ScatterBuffer.SRV,
			UploadBuffer.SRV,
			DstBuffer.UAV);
		break;
	case EScatterUploadBufferType::E_SUBT_SINT4_BUFFER:
		GVDBScatterUpload<EScatterUploadBufferType::E_SUBT_SINT4_BUFFER>(
			RHICmdList,
			NumScatters,
			ThreadGroupSize,
			ScatterBuffer.SRV,
			UploadBuffer.SRV,
			DstBuffer.UAV);
		break;
	case EScatterUploadBufferType::E_SUBT_UINT_BUFFER:
		GVDBScatterUpload<EScatterUploadBufferType::E_SUBT_UINT_BUFFER>(
			RHICmdList,
			NumScatters,
			ThreadGroupSize,
			ScatterBuffer.SRV,
			UploadBuffer.SRV,
			DstBuffer.UAV);
		break;
	default:
		check(false);
		break;
	}

	if (bFlush)
	{
		FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush(
			EImmediateFlushType::DispatchToRHIThread);
	}
}

FGVDBScatterDoubleBufferUploader::FGVDBScatterDoubleBufferUploader()
	:CurrendUsedBufferIndex(INDEX_NONE)
{

}

bool FGVDBScatterDoubleBufferUploader::IsReady(int32 FenceIndex) const
{
	return !Fence[FenceIndex] || (Fence[FenceIndex]->NumPendingWriteCommands.GetValue() == 0 && Fence[FenceIndex]->Poll());
}
bool FGVDBScatterDoubleBufferUploader::Init(
	uint32 InNumBytesPerElement,
	uint32 InNumElements,
	EPixelFormat InFormat,
	const TCHAR* DebugName,
	uint32 InElementOffset)
{
	FString FenceAName = FString(DebugName) + FString(TEXT("FenceA"));
	FString FenceBName = FString(DebugName) + FString(TEXT("FenceB"));

	CurrendUsedBufferIndex = INDEX_NONE;
	uint32 CurrentFrame = GFrameNumberRenderThread;
	if (IsReady(0))
	{
		if (!Fence[0].IsValid())
		{
			Fence[0] = RHICreateGPUFence(*FenceAName);
		}
		CurrendUsedBufferIndex = 0;
	}
	else if (IsReady(1))
	{
		if (!Fence[1].IsValid())
		{
			Fence[1] = RHICreateGPUFence(*FenceBName);
		}
		CurrendUsedBufferIndex = 1;
	}

	if (CurrendUsedBufferIndex != INDEX_NONE)
	{
		Fence[CurrendUsedBufferIndex]->Clear();
		UploadBuffer[CurrendUsedBufferIndex].Init(
			InNumBytesPerElement,
			InNumElements,
			InFormat,
			CurrendUsedBufferIndex == 0 ? (*FenceAName) : (*FenceBName), InElementOffset);
	}
	return CurrendUsedBufferIndex != INDEX_NONE;
}

void FGVDBScatterDoubleBufferUploader::Add(uint32 Index, const void* Data, uint32 Num)
{
	if (CurrendUsedBufferIndex != INDEX_NONE)
	{
		UploadBuffer[CurrendUsedBufferIndex].Add(Index, Data, Num);
	}
}

void FGVDBScatterDoubleBufferUploader::ResourceUploadTo(
	FRHICommandList& RHICmdList,
	FRWBuffer& DstBuffer,
	EScatterUploadBufferType BufferType,
	bool bFlush/* = false*/)
{
	if (CurrendUsedBufferIndex != INDEX_NONE)
	{
		UploadBuffer[CurrendUsedBufferIndex].ResourceUploadTo(
			RHICmdList, DstBuffer, BufferType, bFlush);
		RHICmdList.WriteGPUFence(Fence[CurrendUsedBufferIndex]);

		CurrendUsedBufferIndex = INDEX_NONE;
	}
}


void FGVDBScatterDoubleBufferUploader::Release()
{
	Fence[0].SafeRelease();
	Fence[1].SafeRelease();
	UploadBuffer[0].Release();
	UploadBuffer[1].Release();
}

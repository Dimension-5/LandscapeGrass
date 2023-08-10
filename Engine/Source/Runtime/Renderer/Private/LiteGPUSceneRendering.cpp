#include "Components/LiteGPUSceneComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "RHIGPUReadback.h"
#include "DrawDebugHelpers.h"
#include "RHICommandList.h"
#include "../Private/SceneRendering.h"
#include "../Private/PostProcess/SceneRenderTargets.h"
#include "../Private/ScenePrivate.h"

DEFINE_LOG_CATEGORY_STATIC(LogLiteGPUSceneRendering, Log, All);

#define GPUFOLIAGE_COMPUTE_THREAD_NUM 512

static float sLiteGPUSceneMaxScreenSize = 1.0f;
static FAutoConsoleVariableRef CVarLiteGPUSceneMaxScreenSize(
	TEXT("r.LiteGPUScene.MaxScreenSize"),
	sLiteGPUSceneMaxScreenSize,
	TEXT("Max ScreenSize for lite gpu scene"),
	ECVF_Scalability
);
static float sLiteGPUSceneScreenSizeBias = 0.0f;
static FAutoConsoleVariableRef CVarLiteGPUSceneScreenSizeBias(
	TEXT("r.LiteGPUScene.ScreenSizeBias"),
	sLiteGPUSceneScreenSizeBias,
	TEXT("ScreenSize Bias for instanced gpudriven foliage"),
	ECVF_Scalability
);

static float sLiteGPUSceneLODDistanceScale = 1.0f;
static FAutoConsoleVariableRef CVarGPUDrivenFoliageLODDistanceScale(
	TEXT("r.LiteGPUScene.LODDistanceScale"),
	sLiteGPUSceneLODDistanceScale,
	TEXT("Scale factor for the distance used in computing discrete LOD for LiteGPUScene. (defaults to 1)\n")
	TEXT("(higher values make LODs transition earlier, e.g., 2 is twice as fast / half the distance)"),
	ECVF_Scalability
);

DECLARE_CYCLE_STAT(TEXT("GPUDriven Generate Indirect Draw Buffer"), STAT_GenerateIndirecDrawBuffer, STATGROUP_LiteGPUScene);

class FClearVisibleInstanceCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FClearVisibleInstanceCS, Global);
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters,
		FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), GPUFOLIAGE_COMPUTE_THREAD_NUM);
		OutEnvironment.SetDefine(TEXT("ENABLE_LITE_GPU_SCENE_DEBUG"), ENABLE_LITE_GPU_SCENE_DEBUG);
	}

	FClearVisibleInstanceCS()
	{
	}

	/** Initialization constructor. */
	explicit FClearVisibleInstanceCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		AllPatchNum.Bind(Initializer.ParameterMap, TEXT("AllPatchNum"));
		AllInstanceIndexNum.Bind(Initializer.ParameterMap, TEXT("AllInstanceIndexNum"));
		GPUUnCulledInstanceNum.Bind(Initializer.ParameterMap, TEXT("GPUUnCulledInstanceNum"));
		RWIndirectDrawDispatchIndiretBuffer.Bind(Initializer.ParameterMap, TEXT("RWIndirectDrawDispatchIndiretBuffer"));
		RWInstanceIndiceBuffer.Bind(Initializer.ParameterMap, TEXT("RWInstanceIndiceBuffer"));
		RWPatchCountBuffer.Bind(Initializer.ParameterMap, TEXT("RWPatchCountBuffer"));
		RWPatchCountCopyBuffer.Bind(Initializer.ParameterMap, TEXT("RWPatchCountCopyBuffer"));
		RWPatchCountOffsetBuffer.Bind(Initializer.ParameterMap, TEXT("RWPatchCountOffsetBuffer"));
		RWNextPatchCountOffsetBuffer.Bind(Initializer.ParameterMap, TEXT("RWNextPatchCountOffsetBuffer"));
		//For Debugging
		RWDrawedTriangleCountBuffer.Bind(Initializer.ParameterMap, TEXT("RWDrawedTriangleCountBuffer"));
	}

	void SetParameters(
		FRHICommandList& RHICmdList
		, const FLiteGPUSceneInstanceData* InstanceData
		, FLiteGPUSceneProxyVisibilityData* VisibilityData
	)
	{
		FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();

		SetShaderValue(RHICmdList, ComputeShaderRHI, AllPatchNum, InstanceData->CulledAllPatchNum);
		SetShaderValue(RHICmdList, ComputeShaderRHI, AllInstanceIndexNum, InstanceData->AllInstanceIndexNum);
		
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWInstanceIndiceBuffer, VisibilityData->RWInstanceIndiceBuffer->UAV);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWPatchCountBuffer, InstanceData->RWPatchCountBuffer->UAV);
		SetSRVParameter(RHICmdList, ComputeShaderRHI, GPUUnCulledInstanceNum,
			VisibilityData->RWGPUUnCulledInstanceNum->SRV);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWIndirectDrawDispatchIndiretBuffer,
			VisibilityData->RWIndirectDrawDispatchIndiretBuffer->UAV);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWPatchCountCopyBuffer,
			InstanceData->RWPatchCountCopyBuffer->UAV);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWPatchCountOffsetBuffer,
			InstanceData->RWPatchCountOffsetBuffer->UAV);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWNextPatchCountOffsetBuffer,
			InstanceData->RWNextPatchCountOffsetBuffer->UAV);

#if ENABLE_LITE_GPU_SCENE_DEBUG
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWDrawedTriangleCountBuffer,
			InstanceData->RWDrawedTriangleCountBuffer->UAV);
#else
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWDrawedTriangleCountBuffer, nullptr);
#endif
	}

	void UnbindBuffers(FRHICommandList& RHICmdList)
	{
		FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();

		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWIndirectDrawDispatchIndiretBuffer, nullptr);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWInstanceIndiceBuffer, nullptr);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWPatchCountBuffer, nullptr);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWPatchCountCopyBuffer, nullptr);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWPatchCountOffsetBuffer, nullptr);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWNextPatchCountOffsetBuffer, nullptr);
		// For Debugging
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWDrawedTriangleCountBuffer, nullptr);
	}

private:
	LAYOUT_FIELD(FShaderParameter, AllPatchNum);
	LAYOUT_FIELD(FShaderParameter, AllInstanceIndexNum);
	//UAV
	LAYOUT_FIELD(FShaderResourceParameter, GPUUnCulledInstanceNum);
	LAYOUT_FIELD(FShaderResourceParameter, RWIndirectDrawDispatchIndiretBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, RWInstanceIndiceBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, RWPatchCountBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, RWPatchCountCopyBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, RWPatchCountOffsetBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, RWNextPatchCountOffsetBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, RWDrawedTriangleCountBuffer);

};
IMPLEMENT_SHADER_TYPE(, FClearVisibleInstanceCS, TEXT("/Engine/Private/LiteGPUSceneIndirectDrawGenerator.usf"), TEXT("ClearVisibleInstanceCS"), SF_Compute);


template <bool bCountingPass>
class TGenerateInstanceIndiceCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(TGenerateInstanceIndiceCS, Global);
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters,
		FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), GPUFOLIAGE_COMPUTE_THREAD_NUM);
		OutEnvironment.SetDefine(TEXT("Generate_Instance_Count_Pass"), bCountingPass ? 1 : 0);
		OutEnvironment.SetDefine(TEXT("ENABLE_LITE_GPU_SCENE_DEBUG"), ENABLE_LITE_GPU_SCENE_DEBUG);
	}

	TGenerateInstanceIndiceCS()
	{
	}

	/** Initialization constructor. */
	explicit TGenerateInstanceIndiceCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		ProjMatrix.Bind(Initializer.ParameterMap, TEXT("ProjMatrix"));
		FoliageScreenSizeBias.Bind(Initializer.ParameterMap, TEXT("FoliageScreenSizeBias"));
		FoliageScreenSizeScale.Bind(Initializer.ParameterMap, TEXT("FoliageScreenSizeScale"));
		FoliageMaxScreenSize.Bind(Initializer.ParameterMap, TEXT("FoliageMaxScreenSize"));
		AllPatchNum.Bind(Initializer.ParameterMap, TEXT("AllPatchNum"));

		// SRV
		UnCulledInstanceBuffer.Bind(Initializer.ParameterMap, TEXT("UnCulledInstanceBuffer"));
		GPUUnCulledInstanceNum.Bind(Initializer.ParameterMap, TEXT("GPUUnCulledInstanceNum"));
		GPUUnCulledInstanceScreenSize.Bind(Initializer.ParameterMap, TEXT("GPUUnCulledInstanceScreenSize"));

		ViewLocation.Bind(Initializer.ParameterMap, TEXT("ViewLocation"));
		ViewForward.Bind(Initializer.ParameterMap, TEXT("ViewForward"));

		InstancePatchNumBuffer.Bind(Initializer.ParameterMap, TEXT("InstancePatchNumBuffer"));
		InstancePatchIDsBuffer.Bind(Initializer.ParameterMap, TEXT("InstancePatchIDsBuffer"));
		AllPatchInfoBuffer.Bind(Initializer.ParameterMap, TEXT("AllPatchInfoBuffer"));

		PatchCountOffsetBuffer.Bind(Initializer.ParameterMap, TEXT("PatchCountOffsetBuffer"));
		// UAV
		RWPatchCountBuffer.Bind(Initializer.ParameterMap, TEXT("RWPatchCountBuffer"));
		RWPatchCountCopyBuffer.Bind(Initializer.ParameterMap, TEXT("RWPatchCountCopyBuffer"));
		RWInstanceIndiceBuffer.Bind(Initializer.ParameterMap, TEXT("RWInstanceIndiceBuffer"));
		PerPatchMaxNum.Bind(Initializer.ParameterMap, TEXT("PerPatchMaxNum"));

		RWInstanceTypeBuffer.Bind(Initializer.ParameterMap, TEXT("RWInstanceTypeBuffer"));
		AABBPatchBuffer.Bind(Initializer.ParameterMap, TEXT("AABBPatchBuffer"));
		RWInstanceScaleBuffer.Bind(Initializer.ParameterMap, TEXT("RWInstanceScaleBuffer"));
		RWInstanceTransformBuffer.Bind(Initializer.ParameterMap, TEXT("RWInstanceTransformBuffer"));
	}

	void SetParameters(
		FRHICommandList& RHICmdList
		, const FSceneView& View
		, const FLiteGPUSceneInstanceData* InstanceData
		, FLiteGPUSceneProxyVisibilityData* VisibilityData
	)
	{
		FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();
		SetShaderValue(RHICmdList, ComputeShaderRHI, PerPatchMaxNum, InstanceData->PerPatchMaxNum);
		SetShaderValue(RHICmdList, ComputeShaderRHI, ProjMatrix, FMatrix44f(View.ViewMatrices.GetProjectionMatrix()));
		SetShaderValue(RHICmdList, ComputeShaderRHI, ViewLocation, FVector3f(View.ViewMatrices.GetViewOrigin()));
		SetShaderValue(RHICmdList, ComputeShaderRHI, ViewForward, FVector3f(View.GetViewDirection()));
		SetShaderValue(RHICmdList, ComputeShaderRHI, FoliageScreenSizeBias, sLiteGPUSceneScreenSizeBias);
		SetShaderValue(RHICmdList, ComputeShaderRHI, FoliageScreenSizeScale, sLiteGPUSceneLODDistanceScale * View.LODDistanceFactor);
		SetShaderValue(RHICmdList, ComputeShaderRHI, FoliageMaxScreenSize, sLiteGPUSceneMaxScreenSize);
		SetShaderValue(RHICmdList, ComputeShaderRHI, AllPatchNum, VisibilityData->TotalPatchNum);
		
		SetSRVParameter(RHICmdList, ComputeShaderRHI, UnCulledInstanceBuffer,
			VisibilityData->RWGPUUnCulledInstanceBuffer->SRV);
		SetSRVParameter(RHICmdList, ComputeShaderRHI, GPUUnCulledInstanceNum,
			VisibilityData->RWGPUUnCulledInstanceNum->SRV);
		SetSRVParameter(RHICmdList, ComputeShaderRHI, GPUUnCulledInstanceScreenSize,
			VisibilityData->RWGPUUnCulledInstanceScreenSize->SRV);
		SetSRVParameter(RHICmdList, ComputeShaderRHI, InstancePatchNumBuffer,
			InstanceData->RWInstancePatchNumBuffer->SRV);
		SetSRVParameter(RHICmdList, ComputeShaderRHI, InstancePatchIDsBuffer,
			InstanceData->RWInstancePatchIDsBuffer->SRV);
		SetSRVParameter(RHICmdList, ComputeShaderRHI, AllPatchInfoBuffer, InstanceData->AllPatchInfoBuffer->SRV);
		SetSRVParameter(RHICmdList, ComputeShaderRHI, RWInstanceTypeBuffer,
			InstanceData->RWInstanceTypeBuffer->SRV);
		SetSRVParameter(RHICmdList, ComputeShaderRHI, AABBPatchBuffer, InstanceData->MeshAABBBuffer->SRV);

		// UAV
		if (bCountingPass)
		{
			SetUAVParameter(RHICmdList, ComputeShaderRHI, RWPatchCountCopyBuffer,
				InstanceData->RWPatchCountCopyBuffer->UAV);
		}
		else
		{
			SetSRVParameter(RHICmdList, ComputeShaderRHI, PatchCountOffsetBuffer,
				InstanceData->RWPatchCountOffsetBuffer->SRV);
			SetUAVParameter(RHICmdList, ComputeShaderRHI, RWPatchCountBuffer, InstanceData->RWPatchCountBuffer->UAV);
			SetUAVParameter(RHICmdList, ComputeShaderRHI, RWInstanceIndiceBuffer,
				VisibilityData->RWInstanceIndiceBuffer->UAV);
		}
		SetSRVParameter(RHICmdList, ComputeShaderRHI, RWInstanceScaleBuffer, InstanceData->RWInstanceScaleBuffer->SRV);
		SetSRVParameter(RHICmdList, ComputeShaderRHI, RWInstanceTransformBuffer,
			InstanceData->RWInstanceTransformBuffer->SRV);
	}

	void UnbindBuffers(FRHICommandList& RHICmdList)
	{
		FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWPatchCountBuffer, nullptr);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWPatchCountCopyBuffer, nullptr);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWInstanceIndiceBuffer, nullptr);
	}

private:
	LAYOUT_FIELD(FShaderParameter, PerPatchMaxNum);
	LAYOUT_FIELD(FShaderParameter, ProjMatrix);
	LAYOUT_FIELD(FShaderParameter, FoliageScreenSizeBias);
	LAYOUT_FIELD(FShaderParameter, FoliageScreenSizeScale);
	LAYOUT_FIELD(FShaderParameter, FoliageMaxScreenSize);
	LAYOUT_FIELD(FShaderParameter, AllPatchNum);
	LAYOUT_FIELD(FShaderParameter, ViewLocation);
	LAYOUT_FIELD(FShaderParameter, ViewForward);
	// SRV
	LAYOUT_FIELD(FShaderResourceParameter, UnCulledInstanceBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, GPUUnCulledInstanceNum);
	LAYOUT_FIELD(FShaderResourceParameter, GPUUnCulledInstanceScreenSize);

	LAYOUT_FIELD(FShaderResourceParameter, InstancePatchNumBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, InstancePatchIDsBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, AllPatchInfoBuffer);

	LAYOUT_FIELD(FShaderResourceParameter, RWInstanceTransformBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, RWInstanceTypeBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, AABBPatchBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, RWInstanceScaleBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, PatchCountOffsetBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, RWPatchCountBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, RWPatchCountCopyBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, RWInstanceIndiceBuffer);
};

IMPLEMENT_SHADER_TYPE(template<>, TGenerateInstanceIndiceCS<true>,
	TEXT("/Engine/Private/LiteGPUSceneIndirectDrawGenerator.usf"), TEXT("GenerateInstanceIndiceCS"),
	SF_Compute);
IMPLEMENT_SHADER_TYPE(template<>, TGenerateInstanceIndiceCS<false>,
	TEXT("/Engine/Private/LiteGPUSceneIndirectDrawGenerator.usf"), TEXT("GenerateInstanceIndiceCS"),
	SF_Compute);

class FComputeInstancePatchOffsetCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FComputeInstancePatchOffsetCS, Global);
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters,
		FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), GPUFOLIAGE_COMPUTE_THREAD_NUM);
		OutEnvironment.SetDefine(TEXT("ENABLE_LITE_GPU_SCENE_DEBUG"), ENABLE_LITE_GPU_SCENE_DEBUG);
	}

	FComputeInstancePatchOffsetCS()
	{
	}

	/** Initialization constructor. */
	explicit FComputeInstancePatchOffsetCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		AllPatchNum.Bind(Initializer.ParameterMap, TEXT("AllPatchNum"));
		AllPatchInfoBuffer.Bind(Initializer.ParameterMap, TEXT("AllPatchInfoBuffer"));
		// UAV
		PatchCountCopyBuffer.Bind(Initializer.ParameterMap, TEXT("PatchCountCopyBuffer"));
		RWPatchCountOffsetBuffer.Bind(Initializer.ParameterMap, TEXT("RWPatchCountOffsetBuffer"));
		RWNextPatchCountOffsetBuffer.Bind(Initializer.ParameterMap, TEXT("RWNextPatchCountOffsetBuffer"));

		RWIndirectDrawBuffer.Bind(Initializer.ParameterMap, TEXT("RWIndirectDrawBuffer"));
		// For Debugging
		RWDrawedTriangleCountBuffer.Bind(Initializer.ParameterMap, TEXT("RWDrawedTriangleCountBuffer"));
	}

	void SetParameters(
		FRHICommandList& RHICmdList
		, const FLiteGPUSceneInstanceData* InstanceData
		, FLiteGPUSceneProxyVisibilityData* VisibilityData
	)
	{
		FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();
		SetShaderValue(RHICmdList, ComputeShaderRHI, AllPatchNum, VisibilityData->TotalPatchNum);
		SetSRVParameter(RHICmdList, ComputeShaderRHI, PatchCountCopyBuffer, InstanceData->RWPatchCountCopyBuffer->SRV);
		// UAV
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWPatchCountOffsetBuffer,
			InstanceData->RWPatchCountOffsetBuffer->UAV);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWNextPatchCountOffsetBuffer,
			InstanceData->RWNextPatchCountOffsetBuffer->UAV);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWIndirectDrawBuffer, VisibilityData->RWIndirectDrawBuffer->UAV);
		SetSRVParameter(RHICmdList, ComputeShaderRHI, AllPatchInfoBuffer, InstanceData->AllPatchInfoBuffer->SRV);
#if ENABLE_LITE_GPU_SCENE_DEBUG
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWDrawedTriangleCountBuffer,
			InstanceData->RWDrawedTriangleCountBuffer->UAV);
#else
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWDrawedTriangleCountBuffer,
			nullptr);
#endif
	}

	void UnbindBuffers(FRHICommandList& RHICmdList)
	{
		FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWPatchCountOffsetBuffer, nullptr);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWNextPatchCountOffsetBuffer, nullptr);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWIndirectDrawBuffer, nullptr);
		//For Debugging
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWDrawedTriangleCountBuffer, nullptr);
	}

private:
	LAYOUT_FIELD(FShaderParameter, AllPatchNum);
	//SRV
	LAYOUT_FIELD(FShaderResourceParameter, AllPatchInfoBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, PatchCountCopyBuffer);
	// UAV
	LAYOUT_FIELD(FShaderResourceParameter, RWPatchCountOffsetBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, RWNextPatchCountOffsetBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, RWIndirectDrawBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, RWDrawedTriangleCountBuffer);
};

IMPLEMENT_SHADER_TYPE(, FComputeInstancePatchOffsetCS,
	TEXT("/Engine/Private/LiteGPUSceneIndirectDrawGenerator.usf"),
	TEXT("ComputeInstancePatchOffsetCS"), SF_Compute);

void FScene::InitializeLiteGPUSceneBufferManager()
{
	if (pLiteGPUSceneBufferManager == nullptr)
	{
		pLiteGPUSceneBufferManager = new FLiteGPUSceneBufferManager(FeatureLevel);
		pLiteGPUSceneBufferManager->InitResource();
	}
}

FLiteGPUSceneBufferManager::FLiteGPUSceneBufferManager(ERHIFeatureLevel::Type InFeatureLevel)
{
	check(IsInRenderingThread());
	bInitialized = false;
	RWInstanceInstanceIndicesBufferUploader = nullptr;
	RWInstancedAABBBufferUploader = nullptr;

	RWInstanceLocationBufferUploader = nullptr;
	RWInstanceScaleBufferUploader = nullptr;
	RWInstanceTransformBufferUploader = nullptr;
	RWInstanceTypeBufferUploader = nullptr;

	RWInstancePatchNumBufferUploader = nullptr;
	RWInstancePatchStartBufferUploader = nullptr;
	RWInstancePatchIDsBufferUploader = nullptr;
}

void FLiteGPUSceneBufferManager::InitRHI(FRHICommandListBase& RHICmdList)
{
	InitRenderResource();
	FRenderResource::InitRHI(RHICmdList);
}

void FLiteGPUSceneBufferManager::ReleaseRHI()
{
	ReleaseRenderResource();
	FRenderResource::ReleaseRHI();
}

void FLiteGPUSceneBufferManager::UpdateUploader(FRHICommandList& RHICmdList, class FLiteGPUSceneInstanceData* InstanceDataPtr, int32 DirtyCount)
{
	SCOPE_CYCLE_COUNTER(STAT_ResourceUpload);
	if (!InstanceDataPtr)
	{
		return;
	}

	int32 DirtyBufferNum = DirtyCount;
	const TArray<uint32>& InDirtyBuffer = InstanceDataPtr->UpdateDirtyInstanceIndices;

	if (DirtyBufferNum <= 0)
	{
		return;
	}
	if (RWInstanceTransformBufferUploader)
	{
		uint32 UpdateNum = DirtyBufferNum * 4;
		const bool UploaderReady = RWInstanceTransformBufferUploader->Init(
			GPixelFormats[PF_A32B32G32R32F].BlockBytes,
			UpdateNum,
			PF_A32B32G32R32F,
			TEXT("RWInstanceLocationBufferUploader"));

		if (UploaderReady && InstanceDataPtr->UploadInstanceTransformData.GetData() != nullptr)
		{
			for (int32 Index = 0; Index < DirtyBufferNum; Index++)
			{
				int32 IndexDirty = InDirtyBuffer[Index];
				int32 InstanceUploadOffset = IndexDirty * 4;
				check(InstanceDataPtr->UploadInstanceTransformData.Num() >= InstanceUploadOffset + 4);
				FVector4f* addressA = InstanceDataPtr->UploadInstanceTransformData.GetData() + InstanceUploadOffset;
				RWInstanceTransformBufferUploader->Add(InstanceUploadOffset, (void*)addressA, 4);
			}

			RWInstanceTransformBufferUploader->ResourceUploadTo(
				RHICmdList,
				*InstanceDataPtr->RWInstanceTransformBuffer,
				EScatterUploadBufferType::E_SUBT_FLOAT4_BUFFER,
				false);
		}
	}
	if (RWInstanceScaleBufferUploader)
	{

		uint32 UpdateNum = DirtyBufferNum;
		const bool UploaderReady = RWInstanceScaleBufferUploader->Init(
			GPixelFormats[PF_A32B32G32R32F].BlockBytes,
			UpdateNum,
			PF_A32B32G32R32F,
			TEXT("RWInstanceScaleBufferUploader"));

		if (UploaderReady && InstanceDataPtr->UploadInstanceScaleData.GetData() != nullptr)
		{
			for (int32 Index = 0; Index < DirtyBufferNum; Index++)
			{
				int32 IndexDirty = InDirtyBuffer[Index];
				int32 InstanceUploadOffset = IndexDirty;
				check(InstanceDataPtr->UploadInstanceScaleData.Num() >= InstanceUploadOffset + 1);
				FVector4f* addressA = InstanceDataPtr->UploadInstanceScaleData.GetData() + InstanceUploadOffset;
				RWInstanceScaleBufferUploader->Add(InstanceUploadOffset, (void*)addressA, 1);
			}

			RWInstanceScaleBufferUploader->ResourceUploadTo(
				RHICmdList,
				*InstanceDataPtr->RWInstanceScaleBuffer,
				EScatterUploadBufferType::E_SUBT_FLOAT4_BUFFER,
				false);
		}
	}

	if (RWInstanceTypeBufferUploader)
	{
		uint32 UpdateNum = DirtyBufferNum;
		const bool UploaderReady = RWInstanceTypeBufferUploader->Init(
			GPixelFormats[PF_R8_UINT].BlockBytes,
			UpdateNum,
			PF_R8_UINT,
			TEXT("RWInstanceFoliageTypeBufferUploader"));

		if (UploaderReady && InstanceDataPtr->UploadInstanceFoliageTypeData.GetData() != nullptr)
		{
			for (int32 Index = 0; Index < DirtyBufferNum; Index++)
			{
				int32 IndexDirty = InDirtyBuffer[Index];
				int32 InstanceUploadOffset = IndexDirty;
				check(InstanceDataPtr->UploadInstanceFoliageTypeData.Num() >= InstanceUploadOffset + 1);
				uint8* addressA = InstanceDataPtr->UploadInstanceFoliageTypeData.GetData() + InstanceUploadOffset;
				RWInstanceTypeBufferUploader->Add(InstanceUploadOffset, (void*)addressA, 1);
			}

			RWInstanceTypeBufferUploader->ResourceUploadTo(
				RHICmdList,
				*InstanceDataPtr->RWInstanceTypeBuffer,
				EScatterUploadBufferType::E_SUBT_UINT_BUFFER,
				false);
		}
	}
	if (RWInstancePatchNumBufferUploader)
	{
		uint32 UpdateNum = DirtyBufferNum;
		const bool UploaderReady = RWInstancePatchNumBufferUploader->Init(
			GPixelFormats[PF_R8_UINT].BlockBytes,
			UpdateNum,
			PF_R8_UINT,
			TEXT("RWInstancePatchNumBufferUploader"));

		if (UploaderReady && InstanceDataPtr->UploadInstancePatchNum.GetData() != nullptr)
		{
			for (int32 Index = 0; Index < DirtyBufferNum; Index++)
			{
				int32 IndexDirty = InDirtyBuffer[Index];
				int32 InstanceUploadOffset = IndexDirty;
				check(InstanceDataPtr->UploadInstancePatchNum.Num() >= InstanceUploadOffset + 1);
				uint8* addressA = InstanceDataPtr->UploadInstancePatchNum.GetData() + InstanceUploadOffset;
				RWInstancePatchNumBufferUploader->Add(InstanceUploadOffset, (void*)addressA, 1);
			}

			RWInstancePatchNumBufferUploader->ResourceUploadTo(
				RHICmdList,
				*InstanceDataPtr->RWInstancePatchNumBuffer,
				EScatterUploadBufferType::E_SUBT_UINT_BUFFER,
				false);
		}
	}
	if (RWInstancePatchIDsBufferUploader)
	{

		uint32 UpdateNum = DirtyBufferNum * InstanceDataPtr->PerPatchMaxNum;
		const bool UploaderReady = RWInstancePatchIDsBufferUploader->Init(
			GPixelFormats[PF_R8_UINT].BlockBytes,
			UpdateNum,
			PF_R8_UINT,
			TEXT("RWInstancePatchIDsBufferUploader"));

		if (UploaderReady && InstanceDataPtr->UploadInstancePatchIDs.GetData() != nullptr)
		{
			for (int32 Index = 0; Index < DirtyBufferNum; Index++)
			{
				int32 IndexDirty = InDirtyBuffer[Index];
				int32 InstanceUploadOffset = IndexDirty * InstanceDataPtr->PerPatchMaxNum;
				check(InstanceDataPtr->UploadInstancePatchIDs.Num() >= InstanceUploadOffset + InstanceDataPtr->PerPatchMaxNum);
				uint8* addressA = InstanceDataPtr->UploadInstancePatchIDs.GetData() + InstanceUploadOffset;
				RWInstancePatchIDsBufferUploader->Add(InstanceUploadOffset, (void*)addressA, InstanceDataPtr->PerPatchMaxNum);
			}

			RWInstancePatchIDsBufferUploader->ResourceUploadTo(
				RHICmdList,
				*InstanceDataPtr->RWInstancePatchIDsBuffer,
				EScatterUploadBufferType::E_SUBT_UINT_BUFFER,
				false);
		}
	}
	InstanceDataPtr->CullingInstancedNum = InstanceDataPtr->InstanceNum;
}

void FLiteGPUSceneBufferManager::InitRenderResource()
{
	if (RWInstanceInstanceIndicesBufferUploader == nullptr)
	{
		RWInstanceInstanceIndicesBufferUploader = new FGVDBScatterDoubleBufferUploader();
	}
	if (RWInstancedAABBBufferUploader == nullptr)
	{
		RWInstancedAABBBufferUploader = new FGVDBScatterDoubleBufferUploader();
	}
	if (RWInstanceLocationBufferUploader == nullptr)
	{
		RWInstanceLocationBufferUploader = new FGVDBScatterDoubleBufferUploader();
	}
	if (RWInstanceScaleBufferUploader == nullptr)
	{
		RWInstanceScaleBufferUploader = new FGVDBScatterDoubleBufferUploader();
	}
	if (RWInstanceTransformBufferUploader == nullptr)
	{
		RWInstanceTransformBufferUploader = new FGVDBScatterDoubleBufferUploader();
	}
	if (RWInstanceTypeBufferUploader == nullptr)
	{
		RWInstanceTypeBufferUploader = new FGVDBScatterDoubleBufferUploader();
	}
	if (RWInstancePatchNumBufferUploader == nullptr)
	{
		RWInstancePatchNumBufferUploader = new FGVDBScatterDoubleBufferUploader();
	}
	if (RWInstancePatchIDsBufferUploader == nullptr)
	{
		RWInstancePatchIDsBufferUploader = new FGVDBScatterDoubleBufferUploader();
	}
	if (RWInstancePatchStartBufferUploader == nullptr)
	{
		RWInstancePatchStartBufferUploader = new FGVDBScatterDoubleBufferUploader();
	}
	bInitialized = true;
	UE_LOG(LogLiteGPUSceneRendering, Log, TEXT("call FLiteGPUSceneBufferManager::InitRenderResource"));
}

void FLiteGPUSceneBufferManager::ReleaseRenderResource()
{
	if (RWInstanceInstanceIndicesBufferUploader)
	{
		RWInstanceInstanceIndicesBufferUploader->Release();
		delete RWInstanceInstanceIndicesBufferUploader;
		RWInstanceInstanceIndicesBufferUploader = nullptr;
	}

	if (RWInstancedAABBBufferUploader)
	{
		RWInstancedAABBBufferUploader->Release();
		delete RWInstancedAABBBufferUploader;
		RWInstancedAABBBufferUploader = nullptr;
	}

	if (RWInstanceLocationBufferUploader)
	{
		RWInstanceLocationBufferUploader->Release();
		delete RWInstanceLocationBufferUploader;
		RWInstanceLocationBufferUploader = nullptr;
	}
	if (RWInstanceScaleBufferUploader)
	{
		RWInstanceScaleBufferUploader->Release();
		delete RWInstanceScaleBufferUploader;
		RWInstanceScaleBufferUploader = nullptr;
	}
	if (RWInstanceTransformBufferUploader)
	{
		RWInstanceTransformBufferUploader->Release();
		delete RWInstanceTransformBufferUploader;
		RWInstanceTransformBufferUploader = nullptr;
	}
	if (RWInstanceTypeBufferUploader)
	{
		RWInstanceTypeBufferUploader->Release();
		delete RWInstanceTypeBufferUploader;
		RWInstanceTypeBufferUploader = nullptr;
	}

	if (RWInstancePatchNumBufferUploader)
	{
		RWInstancePatchNumBufferUploader->Release();
		delete RWInstancePatchNumBufferUploader;
		RWInstancePatchNumBufferUploader = nullptr;
	}
	if (RWInstancePatchIDsBufferUploader)
	{
		RWInstancePatchIDsBufferUploader->Release();
		delete RWInstancePatchIDsBufferUploader;
		RWInstancePatchIDsBufferUploader = nullptr;
	}
	if (RWInstancePatchIDsBufferUploader)
	{
		RWInstancePatchIDsBufferUploader->Release();
		delete RWInstancePatchIDsBufferUploader;
		RWInstancePatchIDsBufferUploader = nullptr;
	}
}

void FLiteGPUSceneProxy::GenerateIndirectDrawBuffer(const FSceneView* View, const FSceneViewFamily& ViewFamily,
	FLiteGPUSceneProxyVisibilityData* ResVisibilityData,
	FLiteGPUSceneInstanceData* PerInstanceData) const
{
	SCOPE_CYCLE_COUNTER(STAT_GenerateIndirecDrawBuffer);
	
	if (!ResVisibilityData->bGPUCulling || !ResVisibilityData->bFirstGPUCullinged)
	{
		return;
	}
	
	FRHICommandListImmediate& RHICmdList = GRHICommandList.GetImmediateCommandList();
	RHICmdList.Transition(FRHITransitionInfo(SharedMainVisibilityData->RWInstanceIndiceBuffer->UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute));
	SCOPED_DRAW_EVENT(RHICmdList, GenerateIndirectDrawBufferPhase);
	{
		// 0 st Clear
		SCOPED_DRAW_EVENT(RHICmdList, ClearVisibleInstanceCS);
		const FRHITransitionInfo ComputeToComputeUAVs[] =
		{
			// resources to write
			FRHITransitionInfo(ResVisibilityData->RWInstanceIndiceBuffer->UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
			FRHITransitionInfo(SharedPerInstanceData->RWPatchCountBuffer->UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
			FRHITransitionInfo(SharedPerInstanceData->RWPatchCountCopyBuffer->UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
			FRHITransitionInfo(SharedPerInstanceData->RWPatchCountOffsetBuffer->UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
			FRHITransitionInfo(ResVisibilityData->RWIndirectDrawDispatchIndiretBuffer->UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
			FRHITransitionInfo(SharedPerInstanceData->RWNextPatchCountOffsetBuffer->UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
			FRHITransitionInfo(ResVisibilityData->RWInstanceIndiceBuffer->UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
#if ENABLE_LITE_GPU_SCENE_DEBUG
			FRHITransitionInfo(SharedPerInstanceData->RWDrawedTriangleCountBuffer->UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute)
#endif
			//resources to read
			FRHITransitionInfo(ResVisibilityData->RWGPUUnCulledInstanceNum->UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute)
		};
		RHICmdList.Transition(MakeArrayView(ComputeToComputeUAVs, UE_ARRAY_COUNT(ComputeToComputeUAVs)));
		TShaderMapRef<FClearVisibleInstanceCS> ClearPassCS(GetGlobalShaderMap(ProxyFeatureLevel));
		SetComputePipelineState(RHICmdList, ClearPassCS.GetComputeShader());
		ClearPassCS->SetParameters(RHICmdList, SharedPerInstanceData.Get(), ResVisibilityData);
		DispatchComputeShader(RHICmdList, ClearPassCS.GetShader(),
			FMath::DivideAndRoundUp<uint32>(
				FMath::Max((int32)PerInstanceData->CullingInstancedNum,
					SharedPerInstanceData->AllInstanceIndexNum),
				GPUFOLIAGE_COMPUTE_THREAD_NUM), 1, 1);
		ClearPassCS->UnbindBuffers(RHICmdList);
	}
	{
		// 3 rd
		SCOPED_DRAW_EVENT(RHICmdList, GenerateInstanceIndiceCS_FirstPass);
		const FRHITransitionInfo ComputeToComputeUAVs[] =
		{
			// resources to read
			FRHITransitionInfo(ResVisibilityData->RWGPUUnCulledInstanceBuffer->UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
			FRHITransitionInfo(ResVisibilityData->RWGPUUnCulledInstanceScreenSize->UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
			FRHITransitionInfo(SharedPerInstanceData->RWInstancePatchNumBuffer->UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
			FRHITransitionInfo(SharedPerInstanceData->RWInstanceTypeBuffer->UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
			FRHITransitionInfo(SharedPerInstanceData->RWInstanceTransformBuffer->UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
			//resources to write
			FRHITransitionInfo(SharedPerInstanceData->RWPatchCountCopyBuffer->UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
			FRHITransitionInfo(SharedPerInstanceData->RWPatchCountOffsetBuffer->UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
			FRHITransitionInfo(SharedPerInstanceData->RWPatchCountBuffer->UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
			FRHITransitionInfo(ResVisibilityData->RWInstanceIndiceBuffer->UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute)
		};
		RHICmdList.Transition(MakeArrayView(ComputeToComputeUAVs, UE_ARRAY_COUNT(ComputeToComputeUAVs)));
		TShaderMapRef<TGenerateInstanceIndiceCS<true>> GenerateCS(GetGlobalShaderMap(ProxyFeatureLevel));
		SetComputePipelineState(RHICmdList, GenerateCS.GetComputeShader());
		GenerateCS->SetParameters(RHICmdList, *View, SharedPerInstanceData.Get(), ResVisibilityData);
		DispatchIndirectComputeShader(RHICmdList, GenerateCS.GetShader(),
			ResVisibilityData->RWIndirectDrawDispatchIndiretBuffer->Buffer, 0);
		GenerateCS->UnbindBuffers(RHICmdList);
	}
	{
		SCOPED_DRAW_EVENT(RHICmdList, ComputeInstancePatchOffsetCS);
		const FRHITransitionInfo ComputeToComputeUAVs[] =
		{
			//Resources to read
			FRHITransitionInfo(SharedPerInstanceData->RWPatchCountCopyBuffer->UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
			//Resources to write
			FRHITransitionInfo(SharedPerInstanceData->RWNextPatchCountOffsetBuffer->UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
			FRHITransitionInfo(SharedPerInstanceData->RWPatchCountOffsetBuffer->UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
			FRHITransitionInfo(ResVisibilityData->RWIndirectDrawBuffer->UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
#if ENABLE_LITE_GPU_SCENE_DEBUG
			FRHITransitionInfo(SharedPerInstanceData->RWDrawedTriangleCountBuffer->UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute)
#endif
		};
		RHICmdList.Transition(MakeArrayView(ComputeToComputeUAVs, UE_ARRAY_COUNT(ComputeToComputeUAVs)));
		TShaderMapRef<FComputeInstancePatchOffsetCS> ComputeOffsetCS(GetGlobalShaderMap(ProxyFeatureLevel));
		SetComputePipelineState(RHICmdList, ComputeOffsetCS.GetComputeShader());
		ComputeOffsetCS->SetParameters(RHICmdList, SharedPerInstanceData.Get(), ResVisibilityData);
		DispatchComputeShader(RHICmdList, ComputeOffsetCS.GetShader(),
			FMath::DivideAndRoundUp<uint32>(SharedPerInstanceData->AllPatchNum,
				GPUFOLIAGE_COMPUTE_THREAD_NUM), 1, 1);

		ComputeOffsetCS->UnbindBuffers(RHICmdList);
	}
	{
		SCOPED_DRAW_EVENT(RHICmdList, GenerateInstanceIndiceCS_SecondPass);
		const FRHITransitionInfo ComputeToComputeUAVs[] =
		{
			// resources to read
			FRHITransitionInfo(ResVisibilityData->RWGPUUnCulledInstanceBuffer->UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
			FRHITransitionInfo(ResVisibilityData->RWGPUUnCulledInstanceScreenSize->UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
			FRHITransitionInfo(SharedPerInstanceData->RWInstanceTypeBuffer->UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
			FRHITransitionInfo(SharedPerInstanceData->RWInstanceTransformBuffer->UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
			//resources to write
			FRHITransitionInfo(SharedPerInstanceData->RWPatchCountCopyBuffer->UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
			FRHITransitionInfo(SharedPerInstanceData->RWPatchCountOffsetBuffer->UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
			FRHITransitionInfo(SharedPerInstanceData->RWPatchCountBuffer->UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
			FRHITransitionInfo(ResVisibilityData->RWInstanceIndiceBuffer->UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute)
		};
		RHICmdList.Transition(MakeArrayView(ComputeToComputeUAVs, UE_ARRAY_COUNT(ComputeToComputeUAVs)));
		TShaderMapRef<TGenerateInstanceIndiceCS<false>> GenerateCS(GetGlobalShaderMap(ProxyFeatureLevel));
		SetComputePipelineState(RHICmdList, GenerateCS.GetComputeShader());
		GenerateCS->SetParameters(RHICmdList, *View, SharedPerInstanceData.Get(), ResVisibilityData);
		DispatchIndirectComputeShader(RHICmdList, GenerateCS.GetShader(),
			ResVisibilityData->RWIndirectDrawDispatchIndiretBuffer->Buffer, 0);

		GenerateCS->UnbindBuffers(RHICmdList);
	}
	RHICmdList.Transition(FRHITransitionInfo(SharedMainVisibilityData->RWInstanceIndiceBuffer->UAV, ERHIAccess::UAVCompute, ERHIAccess::VertexOrIndexBuffer));
}

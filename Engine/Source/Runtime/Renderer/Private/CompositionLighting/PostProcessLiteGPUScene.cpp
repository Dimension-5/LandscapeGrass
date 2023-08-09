#include "CompositionLighting/PostProcessLiteGPUScene.h"
#include "SceneUtils.h"
#include "ScenePrivate.h"
#include "ClearQuad.h"
#include "PipelineStateCache.h"
#include "GlobalShader.h"
#include "RHICommandList.h"
#include "RHIUtilities.h"
#include "UniformBuffer.h"
#include "ShaderParameterUtils.h"
#include "ShadowRendering.h"
#include "Components/LiteGPUSceneComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogLiteGPUSceneCulling, Log, All);
DECLARE_GPU_STAT_NAMED(LiteGPUSceneCulling, TEXT("GPUDriven LiteGPUScene Culling"));
DECLARE_CYCLE_STAT(TEXT("GPUDriven Culling"), STAT_GPUDrivenCulling, STATGROUP_LiteGPUScene);

static int32 GEnableDebugLiteGPUSceneBounds = 0;
static FAutoConsoleVariableRef CVarEnableDebugBounds(
	TEXT("r.LiteGPUScene.DebugBounds"),
	GEnableDebugLiteGPUSceneBounds,
	TEXT("Enable to debug lite gpu scene bounds(default = 1 (enabled))"));

static float sLiteGPUSceneMinScreenSize = 0.012f;
static FAutoConsoleVariableRef CVarLiteGPUSceneMinScreenSize(
	TEXT("r.LiteGPUScene.MinScreenSize"),
	sLiteGPUSceneMinScreenSize,
	TEXT("Var for lite gpu scene screen size culling"),
	ECVF_Scalability
);

static int32 GLiteGPUSceneCullingUseWaveOps = 1;
static FAutoConsoleVariableRef CVarLiteGPUSceneCullingUseWaveOps(
	TEXT("r.LiteGPUScene.UseWaveOps"),
	GLiteGPUSceneCullingUseWaveOps,
	TEXT("Use wave intrisics in DX12. (default = 1)"));

static int32 sLiteGPUSceneHZBMinMipLevel = 0;
static FAutoConsoleVariableRef CVarLiteGPUSceneMinMipLevel(
	TEXT("r.LiteGPUScene.HZBMinMipLevel"),
	sLiteGPUSceneHZBMinMipLevel,
	TEXT(""));

static int32 sLiteGPUSceneHZBMaxMipLevel = 9;
static FAutoConsoleVariableRef CVarLiteGPUSceneMaxMipLevel(
	TEXT("r.LiteGPUScene.HZBMaxMipLevel"),
	sLiteGPUSceneHZBMaxMipLevel,
	TEXT(""));

static int32 sLiteGPUSceneEnableDepthDrawTest =0;
static FAutoConsoleVariableRef CVarLiteGPUSceneEnableDepthDrawTest(
	TEXT("r.LiteGPUScene.EnableDepthDrawTest"),
	sLiteGPUSceneEnableDepthDrawTest,
	TEXT("Defalut to 0, 1 means use DepthDrawTest to help Clear occlusion Result"));

class FClearLiteGPUSceneResCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FClearLiteGPUSceneResCS);

public:
	class FEnableDepthDrawTest : SHADER_PERMUTATION_BOOL("ENABLE_DEPTHDRAWTEST");
	using FPermutationDomain = TShaderPermutationDomain<FEnableDepthDrawTest>;
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters,
	                                         FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), LITE_GPU_SCENE_COMPUTE_THREAD_NUM);
		OutEnvironment.SetDefine(TEXT("ENABLE_LITE_GPU_SCENE_DEBUG"), ENABLE_LITE_GPU_SCENE_DEBUG);
	}

	FClearLiteGPUSceneResCS()
	{
	}
	/** Initialization constructor. */
	explicit FClearLiteGPUSceneResCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		NumInstances.Bind(Initializer.ParameterMap, TEXT("NumInstances"));
		RWUnCulledInstanceBuffer.Bind(Initializer.ParameterMap, TEXT("RWUnCulledInstanceBuffer"));
		RWGPUUnCulledInstanceScreenSize.Bind(Initializer.ParameterMap, TEXT("RWGPUUnCulledInstanceScreenSize"));
		RWGPUUnCulledInstanceNum.Bind(Initializer.ParameterMap, TEXT("RWGPUUnCulledInstanceNum"));
		RWGPUUnCulledInstanceIndirectParameters.Bind(Initializer.ParameterMap,
		TEXT("RWGPUUnCulledInstanceIndirectParameters"));
		RWDepthDrawTestResultBuffer.Bind(Initializer.ParameterMap, TEXT("RWDepthDrawTestResultBuffer"));

		// For Debugging
		RWCulledInstanceBuffer.Bind(Initializer.ParameterMap, TEXT("RWCulledInstanceBuffer"));
		RWGPUCulledInstanceDebugIndirectParameters.Bind(Initializer.ParameterMap, TEXT("RWGPUCulledInstanceDebugIndirectParameters"));
	}

	void SetParameters(
		FRHICommandList& RHICmdList
		, const FLiteGPUSceneInstanceData* InstanceData
		, FLiteGPUSceneProxyVisibilityData* VisibilityData
	)
	{
		FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();
		SetShaderValue(RHICmdList, ComputeShaderRHI, NumInstances, InstanceData->CullingInstancedNum);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWUnCulledInstanceBuffer,
		                VisibilityData->RWGPUUnCulledInstanceBuffer->UAV);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWGPUUnCulledInstanceScreenSize,
		VisibilityData->RWGPUUnCulledInstanceScreenSize->UAV);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWDepthDrawTestResultBuffer,
						VisibilityData->RWDepthDrawTestResultBuffer->UAV);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWGPUUnCulledInstanceNum,
		                VisibilityData->RWGPUUnCulledInstanceNum->UAV);
		if(sLiteGPUSceneEnableDepthDrawTest || ENABLE_LITE_GPU_SCENE_DEBUG)
		{
			SetUAVParameter(RHICmdList, ComputeShaderRHI, RWGPUUnCulledInstanceIndirectParameters,
							VisibilityData->RWGPUUnCulledInstanceIndirectParameters->UAV);
		}else
		{
			SetUAVParameter(RHICmdList, ComputeShaderRHI, RWGPUUnCulledInstanceIndirectParameters,nullptr);
		}

#if ENABLE_LITE_GPU_SCENE_DEBUG
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWCulledInstanceBuffer,
						VisibilityData->RWGPUCulledInstanceBuffer->UAV);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWGPUCulledInstanceDebugIndirectParameters,
						VisibilityData->RWGPUCulledInstanceDebugIndirectParameters->UAV);
#else
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWCulledInstanceBuffer, nullptr);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWGPUCulledInstanceDebugIndirectParameters, nullptr);
#endif
	}

	void UnbindBuffers(FRHICommandList& RHICmdList)
	{
		FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();
		SetShaderValue(RHICmdList, ComputeShaderRHI, NumInstances, nullptr);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWUnCulledInstanceBuffer, nullptr);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWGPUUnCulledInstanceNum, nullptr);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWGPUUnCulledInstanceScreenSize, nullptr);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWDepthDrawTestResultBuffer, nullptr);
		// For Debugging and DepthDrawTest
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWGPUUnCulledInstanceIndirectParameters, nullptr);
		// For Debugging
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWCulledInstanceBuffer, nullptr);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWGPUCulledInstanceDebugIndirectParameters, nullptr);
	}
private:
	LAYOUT_FIELD(FShaderParameter, NumInstances);
	LAYOUT_FIELD(FShaderResourceParameter, RWUnCulledInstanceBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, RWGPUUnCulledInstanceNum);
	LAYOUT_FIELD(FShaderResourceParameter, RWGPUUnCulledInstanceScreenSize);
	LAYOUT_FIELD(FShaderResourceParameter, RWDepthDrawTestResultBuffer);
	// For Debugging
	LAYOUT_FIELD(FShaderResourceParameter, RWCulledInstanceBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, RWGPUCulledInstanceDebugIndirectParameters);
	// For Debug and DepthDrawTest
	LAYOUT_FIELD(FShaderResourceParameter, RWGPUUnCulledInstanceIndirectParameters);
};
IMPLEMENT_GLOBAL_SHADER(FClearLiteGPUSceneResCS, "/Engine/Private/LiteGPUSceneCulling.usf", "ClearGPUCullingResCS", SF_Compute);

class FLiteGPUSceneDepthDrawTestUpdateDataCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLiteGPUSceneDepthDrawTestUpdateDataCS)
public:
	class FUpdatePass : SHADER_PERMUTATION_BOOL("UpdatePass");
	
	class FWaveOps : SHADER_PERMUTATION_BOOL("DIM_WAVE_OPS");
	
	using FPermutationDomain = TShaderPermutationDomain<FUpdatePass,FWaveOps>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters,
	                                         FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), LITE_GPU_SCENE_COMPUTE_THREAD_NUM);
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FWaveOps>())
		{
			OutEnvironment.CompilerFlags.Add(CFLAG_WaveOperations);
		}
	}

	FLiteGPUSceneDepthDrawTestUpdateDataCS()
	{
	}

	/** Initialization constructor. */
	explicit FLiteGPUSceneDepthDrawTestUpdateDataCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		NumInstances.Bind(Initializer.ParameterMap, TEXT("NumInstances"));
		RWUnCulledInstanceBuffer.Bind(Initializer.ParameterMap, TEXT("RWUnCulledInstanceBuffer"));
		RWGPUUnCulledInstanceNum.Bind(Initializer.ParameterMap, TEXT("RWGPUUnCulledInstanceNum"));
		RWDepthDrawTestResultBuffer.Bind(Initializer.ParameterMap, TEXT("RWDepthDrawTestResultBuffer"));
	}

	void SetParameters(
		FRHICommandList& RHICmdList
		, const FLiteGPUSceneInstanceData* InstanceData
		, FLiteGPUSceneProxyVisibilityData* VisibilityData,
		bool bFirstPass = false
	)
	{
		FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();
		if(!bFirstPass)
		{
			SetShaderValue(RHICmdList, ComputeShaderRHI, NumInstances, InstanceData->CullingInstancedNum);
			SetUAVParameter(RHICmdList, ComputeShaderRHI, RWUnCulledInstanceBuffer,
							VisibilityData->RWGPUUnCulledInstanceBuffer->UAV);
			SetUAVParameter(RHICmdList, ComputeShaderRHI, RWDepthDrawTestResultBuffer,
			VisibilityData->RWDepthDrawTestResultBuffer->UAV);
		}
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWGPUUnCulledInstanceNum,
		                VisibilityData->RWGPUUnCulledInstanceNum->UAV);
	}

	void UnbindBuffers(FRHICommandList& RHICmdList,bool bFirstPass = false)
	{
		FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();
		if(!bFirstPass)
		{
			SetShaderValue(RHICmdList, ComputeShaderRHI, NumInstances, nullptr);
			SetUAVParameter(RHICmdList, ComputeShaderRHI, RWUnCulledInstanceBuffer, nullptr);
			SetUAVParameter(RHICmdList, ComputeShaderRHI, RWDepthDrawTestResultBuffer, nullptr);
		}
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWGPUUnCulledInstanceNum, nullptr);
	}
private:
	LAYOUT_FIELD(FShaderParameter, NumInstances);
	LAYOUT_FIELD(FShaderResourceParameter, RWUnCulledInstanceBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, RWGPUUnCulledInstanceNum);
	LAYOUT_FIELD(FShaderResourceParameter, RWDepthDrawTestResultBuffer);
};
IMPLEMENT_GLOBAL_SHADER(FLiteGPUSceneDepthDrawTestUpdateDataCS, "/Engine/Private/LiteGPUSceneCulling.usf", "DepthDrawTestUpdateInstanceNumAndIndicesCS",SF_Compute);

class FLiteGPUSceneDepthDrawTestVS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FLiteGPUSceneDepthDrawTestVS, Global);
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters,
	                                         FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	FLiteGPUSceneDepthDrawTestVS()
	{
	}

	FLiteGPUSceneDepthDrawTestVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PreviewTranslation.Bind(Initializer.ParameterMap, TEXT("PreviewTranslation"));
		TranslatedViewProjectionMatrix.Bind(Initializer.ParameterMap, TEXT("TranslatedViewProjectionMatrix"));
		UnCulledInstanceBuffer.Bind(Initializer.ParameterMap, TEXT("UnCulledInstanceBuffer"));
		RWInstanceTypeBuffer.Bind(Initializer.ParameterMap, TEXT("RWInstanceTypeBuffer"));
		RWInstanceTransformBuffer.Bind(Initializer.ParameterMap, TEXT("RWInstanceTransformBuffer"));
		AABBPatchBuffer.Bind(Initializer.ParameterMap, TEXT("AABBPatchBuffer"));
	}

	void SetParameters(
		FRHICommandList& RHICmdList
		, const FViewInfo& View
		, const FLiteGPUSceneInstanceData* InstanceData
		, FLiteGPUSceneProxyVisibilityData* VisibilityData
	)
	{
		FRHIVertexShader* ShaderRHI = RHICmdList.GetBoundVertexShader();
		
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);
		FVector3f PreviewTranslationVector = FVector3f(View.ViewMatrices.GetPreViewTranslation());
		SetShaderValue(RHICmdList, ShaderRHI, PreviewTranslation,
		               FVector4f(PreviewTranslationVector.X, PreviewTranslationVector.Y, PreviewTranslationVector.Z, 0));
		SetShaderValue(RHICmdList, ShaderRHI, TranslatedViewProjectionMatrix, FMatrix44f(View.ViewMatrices.GetTranslatedViewProjectionMatrix()));
		SetSRVParameter(RHICmdList, ShaderRHI, UnCulledInstanceBuffer,VisibilityData->RWGPUUnCulledInstanceBuffer->SRV);
		SetSRVParameter(RHICmdList, ShaderRHI, RWInstanceTypeBuffer, InstanceData->RWInstanceTypeBuffer->SRV);
		SetSRVParameter(RHICmdList, ShaderRHI, RWInstanceTransformBuffer, InstanceData->RWInstanceTransformBuffer->SRV);
		SetSRVParameter(RHICmdList, ShaderRHI, AABBPatchBuffer, InstanceData->MeshAABBBuffer->SRV);
	}
	void ResetParameters(FRHICommandList& RHICmdList)
	{
		FRHIVertexShader* ShaderRHI = RHICmdList.GetBoundVertexShader();
		SetSRVParameter(RHICmdList, ShaderRHI, UnCulledInstanceBuffer,nullptr);
		SetSRVParameter(RHICmdList, ShaderRHI, RWInstanceTypeBuffer,nullptr);
		SetSRVParameter(RHICmdList, ShaderRHI, RWInstanceTransformBuffer, nullptr);
		SetSRVParameter(RHICmdList, ShaderRHI, AABBPatchBuffer, nullptr);
	}
private:
	LAYOUT_FIELD(FShaderParameter, PreviewTranslation);
	LAYOUT_FIELD(FShaderParameter, TranslatedViewProjectionMatrix);
	LAYOUT_FIELD(FShaderResourceParameter, UnCulledInstanceBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, AABBPatchBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, RWInstanceTypeBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, RWInstanceTransformBuffer);
};
IMPLEMENT_SHADER_TYPE(, FLiteGPUSceneDepthDrawTestVS, TEXT("/Engine/Private/LiteGPUSceneCulling.usf"),
                        TEXT("DepthDrawTestVS"), SF_Vertex);

struct FLiteGPUSceneDepthDrawTestPS : FGlobalShader
{
	DECLARE_SHADER_TYPE(FLiteGPUSceneDepthDrawTestPS, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	FLiteGPUSceneDepthDrawTestPS() {}
public:
	LAYOUT_FIELD(FShaderResourceParameter, RWDepthDrawTestResultBuffer)

	FLiteGPUSceneDepthDrawTestPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		RWDepthDrawTestResultBuffer.Bind( Initializer.ParameterMap, TEXT("RWDepthDrawTestResultBuffer") );
	}

	void SetParameters(
		FRHICommandList& RHICmdList,
		FRHIUnorderedAccessView* InResultsUAV)
	{
		FRHIPixelShader* ShaderRHI = RHICmdList.GetBoundPixelShader();
		SetUAVParameter(RHICmdList, ShaderRHI, RWDepthDrawTestResultBuffer, InResultsUAV);
	}

	void ResetParameters(FRHICommandList& RHICmdList)
	{
		FRHIPixelShader* ShaderRHI = RHICmdList.GetBoundPixelShader();
		SetUAVParameter(RHICmdList, ShaderRHI, RWDepthDrawTestResultBuffer, nullptr);
	}
};
IMPLEMENT_SHADER_TYPE(,FLiteGPUSceneDepthDrawTestPS, TEXT("/Engine/Private/LiteGPUSceneCulling.usf"), TEXT("DepthDrawTestPS"), SF_Pixel);

class FDebugLiteGPUSceneVS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FDebugLiteGPUSceneVS, Global);
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters,
	                                         FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("ENABLE_LITE_GPU_SCENE_DEBUG"), ENABLE_LITE_GPU_SCENE_DEBUG);
	}

	FDebugLiteGPUSceneVS()
	{
	}

	FDebugLiteGPUSceneVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PreviewTranslation.Bind(Initializer.ParameterMap, TEXT("PreviewTranslation"));
		TranslatedViewProjectionMatrix.Bind(Initializer.ParameterMap, TEXT("TranslatedViewProjectionMatrix"));
		CulledLeafInstanceBuffer.Bind(Initializer.ParameterMap, TEXT("CulledLeafInstanceBuffer"));
		RWInstanceTypeBuffer.Bind(Initializer.ParameterMap, TEXT("RWInstanceTypeBuffer"));
		RWInstanceTransformBuffer.Bind(Initializer.ParameterMap, TEXT("RWInstanceTransformBuffer"));
		RWInstanceScaleBuffer.Bind(Initializer.ParameterMap, TEXT("RWInstanceScaleBuffer"));
		AABBPatchBuffer.Bind(Initializer.ParameterMap, TEXT("AABBPatchBuffer"));
	}

	void SetParameters(
		FRHICommandList& RHICmdList
		, const FViewInfo& View
		, const FLiteGPUSceneInstanceData* InstanceData
		, FLiteGPUSceneProxyVisibilityData* VisibilityData
		, bool bShowCulled
	)
	{
		FRHIVertexShader* ShaderRHI = RHICmdList.GetBoundVertexShader();
		{
			FVector3f PreviewTranslationVector = FVector3f(View.ViewMatrices.GetPreViewTranslation());
			FVector3f ViewLoc = FVector3f(View.ViewMatrices.GetViewOrigin());
			SetShaderValue(RHICmdList, ShaderRHI, PreviewTranslation,
			               FVector4f(PreviewTranslationVector.X, PreviewTranslationVector.Y, PreviewTranslationVector.Z, 0));
			SetShaderValue(RHICmdList, ShaderRHI, TranslatedViewProjectionMatrix, FMatrix44f(View.ViewMatrices.GetTranslatedViewProjectionMatrix()));
#if ENABLE_LITE_GPU_SCENE_DEBUG
			SetSRVParameter(RHICmdList, ShaderRHI, CulledLeafInstanceBuffer,
				bShowCulled ? VisibilityData->RWGPUCulledInstanceBuffer->SRV : VisibilityData->RWGPUUnCulledInstanceIndicesBuffer->SRV);
#endif
		}
		SetSRVParameter(RHICmdList, ShaderRHI, RWInstanceTypeBuffer, InstanceData->RWInstanceTypeBuffer->SRV);
		SetSRVParameter(RHICmdList, ShaderRHI, RWInstanceTransformBuffer, InstanceData->RWInstanceTransformBuffer->SRV);
		SetSRVParameter(RHICmdList, ShaderRHI, RWInstanceScaleBuffer, InstanceData->RWInstanceScaleBuffer->SRV);
		SetSRVParameter(RHICmdList, ShaderRHI, AABBPatchBuffer, InstanceData->MeshAABBBuffer->SRV);
	}

private:
	LAYOUT_FIELD(FShaderParameter, PreviewTranslation);
	LAYOUT_FIELD(FShaderParameter, TranslatedViewProjectionMatrix);
	LAYOUT_FIELD(FShaderResourceParameter, CulledLeafInstanceBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, AABBPatchBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, RWInstanceScaleBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, RWInstanceTypeBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, RWInstanceTransformBuffer);
};

IMPLEMENT_SHADER_TYPE(, FDebugLiteGPUSceneVS, TEXT("/Engine/Private/LiteGPUSceneCulling.usf"),
                        TEXT("DebugLiteGPUSceneVS"), SF_Vertex);

template <bool bDrawCulled>
class FDebugLiteGPUScenePS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FDebugLiteGPUScenePS, Global);
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters,
	                                         FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("DRAW_CULLED"), bDrawCulled);
	}

	FDebugLiteGPUScenePS()
	{
	}

	FDebugLiteGPUScenePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
	}
};

IMPLEMENT_SHADER_TYPE(template<>, FDebugLiteGPUScenePS<true>, TEXT("/Engine/Private/LiteGPUSceneCulling.usf"),
                      TEXT("DebugLiteGPUScenePS"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>, FDebugLiteGPUScenePS<false>, TEXT("/Engine/Private/LiteGPUSceneCulling.usf"),
                      TEXT("DebugLiteGPUScenePS"), SF_Pixel);

class FLiteGPUSceneCullingCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLiteGPUSceneCullingCS)
	class FWaveOps : SHADER_PERMUTATION_BOOL("DIM_WAVE_OPS");
	
	class FEnableDepthDrawTest : SHADER_PERMUTATION_BOOL("ENABLE_DEPTHDRAWTEST");

	using FPermutationDomain = TShaderPermutationDomain<FWaveOps,FEnableDepthDrawTest>;

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters,
	                                         FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), LITE_GPU_SCENE_COMPUTE_THREAD_NUM);
		OutEnvironment.SetDefine(TEXT("ENABLE_LITE_GPU_SCENE_DEBUG"), ENABLE_LITE_GPU_SCENE_DEBUG);

		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FWaveOps>())
		{
			OutEnvironment.CompilerFlags.Add(CFLAG_WaveOperations);
		}
	}

	FLiteGPUSceneCullingCS()
	{
	}

	/** Initialization constructor. */
	explicit FLiteGPUSceneCullingCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		NumInstances.Bind(Initializer.ParameterMap, TEXT("NumInstances"));
		ViewLocation.Bind(Initializer.ParameterMap, TEXT("ViewLocation"));
		ViewForward.Bind(Initializer.ParameterMap, TEXT("ViewForward"));
		FrustumPermutedPlanes0.Bind(Initializer.ParameterMap, TEXT("FrustumPermutedPlanes0"));
		FrustumPermutedPlanes1.Bind(Initializer.ParameterMap, TEXT("FrustumPermutedPlanes1"));
		ProjMatrix.Bind(Initializer.ParameterMap, TEXT("ProjMatrix"));
		PreviewTranslation.Bind(Initializer.ParameterMap, TEXT("PreviewTranslation"));
		LiteGPUSceneMinScreenSize.Bind(Initializer.ParameterMap, TEXT("LiteGPUSceneMinScreenSize"));
		TranslatedViewProjectionMatrix.Bind(Initializer.ParameterMap, TEXT("TranslatedViewProjectionMatrix"));
		HZBSize.Bind(Initializer.ParameterMap, TEXT("HZBSize"));
		HZBUvFactor.Bind(Initializer.ParameterMap, TEXT("HZBUvFactor"));

		HZBTexture.Bind(Initializer.ParameterMap, TEXT("HZBTexture"));
		HZBSampler.Bind(Initializer.ParameterMap, TEXT("HZBSampler"));
		RWUnCulledInstanceBuffer.Bind(Initializer.ParameterMap, TEXT("RWUnCulledInstanceBuffer"));
		RWGPUUnCulledInstanceNum.Bind(Initializer.ParameterMap, TEXT("RWGPUUnCulledInstanceNum"));
		RWGPUUnCulledInstanceScreenSize.Bind(Initializer.ParameterMap, TEXT("RWGPUUnCulledInstanceScreenSize"));

		AABBPatchBuffer.Bind(Initializer.ParameterMap, TEXT("AABBPatchBuffer"));
		RWInstanceTypeBuffer.Bind(Initializer.ParameterMap, TEXT("RWInstanceTypeBuffer"));
		RWInstanceTransformBuffer.Bind(Initializer.ParameterMap, TEXT("RWInstanceTransformBuffer"));
		RWGPUUnCulledInstanceIndirectParameters.Bind(Initializer.ParameterMap, TEXT("RWGPUUnCulledInstanceIndirectParameters"));

		// For Debugging
		RWCulledInstanceBuffer.Bind(Initializer.ParameterMap, TEXT("RWCulledInstanceBuffer"));
		RWGPUCulledInstanceDebugIndirectParameters.Bind(Initializer.ParameterMap, TEXT("RWGPUCulledInstanceDebugIndirectParameters"));
	}

	void SetParameters(
		FRHICommandList& RHICmdList
		, const FViewInfo& View
		, const FLiteGPUSceneInstanceData* InstanceData
		, FLiteGPUSceneProxyVisibilityData* VisibilityData
	)
	{
		FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();


		FVector3f PreviewTranslationVector = FVector3f(View.ViewMatrices.GetPreViewTranslation());
		FVector3f ViewLoc = FVector3f(View.ViewMatrices.GetViewOrigin());

		SetShaderValue(RHICmdList, ComputeShaderRHI, NumInstances, InstanceData->CullingInstancedNum);
		SetShaderValue(RHICmdList, ComputeShaderRHI, ViewLocation, ViewLoc);
		SetShaderValue(RHICmdList, ComputeShaderRHI, ViewForward, FVector3f(View.GetViewDirection()));
		SetShaderValue(RHICmdList, ComputeShaderRHI, PreviewTranslation,
		               FVector4f(PreviewTranslationVector.X, PreviewTranslationVector.Y, PreviewTranslationVector.Z, 0));
		SetShaderValue(RHICmdList, ComputeShaderRHI, TranslatedViewProjectionMatrix, FMatrix44f(View.ViewMatrices.GetTranslatedViewProjectionMatrix()));
		SetShaderValue(RHICmdList, ComputeShaderRHI, LiteGPUSceneMinScreenSize, sLiteGPUSceneMinScreenSize);

		FConvexVolume ViewFrustumLocal;
		GetViewFrustumBounds(ViewFrustumLocal, View.ViewMatrices.GetViewProjectionMatrix(), true);
		if (ViewFrustumLocal.PermutedPlanes.Num() >= 8)
		{
			FMatrix PermutedPlanesMatrix0 = FMatrix(ViewFrustumLocal.PermutedPlanes[0],
			                                        ViewFrustumLocal.PermutedPlanes[1],
			                                        ViewFrustumLocal.PermutedPlanes[2],
			                                        ViewFrustumLocal.PermutedPlanes[3]);
			FMatrix PermutedPlanesMatrix1 = FMatrix(ViewFrustumLocal.PermutedPlanes[4],
			                                        ViewFrustumLocal.PermutedPlanes[5],
			                                        ViewFrustumLocal.PermutedPlanes[6],
			                                        ViewFrustumLocal.PermutedPlanes[7]);

			SetShaderValue(RHICmdList, ComputeShaderRHI, FrustumPermutedPlanes0, FMatrix44f(PermutedPlanesMatrix0));
			SetShaderValue(RHICmdList, ComputeShaderRHI, FrustumPermutedPlanes1, FMatrix44f(PermutedPlanesMatrix1));
		}
		FMatrix InputProj = View.ViewMatrices.GetProjectionMatrix();
		SetShaderValue(RHICmdList, ComputeShaderRHI, ProjMatrix, FMatrix44f(InputProj));
		// Occulusion culling
		{
			const float HZBMipmapCounts = FMath::Log2((float)FMath::Max(View.HZBMipmap0Size.X, View.HZBMipmap0Size.Y));
			const float HZBMinMip = FMath::Clamp((float)sLiteGPUSceneHZBMinMipLevel,0.0f,HZBMipmapCounts - 1);
			const float HZBMaxMip = FMath::Clamp((float)sLiteGPUSceneHZBMaxMipLevel,0.0f,HZBMipmapCounts - 1);
			const FVector4f HZBUvFactorValue(
				float(View.ViewRect.Width()) / float(2 * View.HZBMipmap0Size.X),
				float(View.ViewRect.Height()) / float(2 * View.HZBMipmap0Size.Y),
				HZBMinMip,
				HZBMaxMip
			);
			const FVector4f HZBSizeValue(
				View.HZBMipmap0Size.X,
				View.HZBMipmap0Size.Y,
				1.0f / float(View.HZBMipmap0Size.X),
				1.0f / float(View.HZBMipmap0Size.Y)
			);
			SetShaderValue(RHICmdList, ComputeShaderRHI, HZBUvFactor, HZBUvFactorValue);
			SetShaderValue(RHICmdList, ComputeShaderRHI, HZBSize, HZBSizeValue);
			SetTextureParameter(
				RHICmdList,
				ComputeShaderRHI,
				HZBTexture,
				HZBSampler,
				TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(),
				View.HZB->GetRHI()
			);
		}

		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWUnCulledInstanceBuffer, VisibilityData->RWGPUUnCulledInstanceBuffer->UAV);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWGPUUnCulledInstanceScreenSize, VisibilityData->RWGPUUnCulledInstanceScreenSize->UAV);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWGPUUnCulledInstanceNum, VisibilityData->RWGPUUnCulledInstanceNum->UAV);
		SetSRVParameter(RHICmdList, ComputeShaderRHI, AABBPatchBuffer, InstanceData->MeshAABBBuffer->SRV);
		SetSRVParameter(RHICmdList, ComputeShaderRHI, RWInstanceTypeBuffer, InstanceData->RWInstanceTypeBuffer->SRV);
		SetSRVParameter(RHICmdList, ComputeShaderRHI, RWInstanceTransformBuffer, InstanceData->RWInstanceTransformBuffer->SRV);
		if(sLiteGPUSceneEnableDepthDrawTest||ENABLE_LITE_GPU_SCENE_DEBUG)
		{
			SetUAVParameter(RHICmdList, ComputeShaderRHI, RWGPUUnCulledInstanceIndirectParameters,
							VisibilityData->RWGPUUnCulledInstanceIndirectParameters->UAV);
		}
		else
		{
			SetUAVParameter(RHICmdList, ComputeShaderRHI, RWGPUUnCulledInstanceIndirectParameters,nullptr);
		}
#if ENABLE_LITE_GPU_SCENE_DEBUG
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWCulledInstanceBuffer,
						VisibilityData->RWGPUCulledInstanceBuffer->UAV);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWGPUCulledInstanceDebugIndirectParameters,
						VisibilityData->RWGPUCulledInstanceDebugIndirectParameters->UAV);
#else
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWCulledInstanceBuffer,
						nullptr);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWGPUCulledInstanceDebugIndirectParameters,
						nullptr);
#endif
	}

	void UnbindBuffers(FRHICommandList& RHICmdList)
	{
		FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWUnCulledInstanceBuffer, nullptr);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWGPUUnCulledInstanceNum, nullptr);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWGPUUnCulledInstanceScreenSize, nullptr);
		// For Debugging and DepthDrawTest
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWGPUUnCulledInstanceIndirectParameters, nullptr);
		// For Debugging
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWCulledInstanceBuffer, nullptr);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, RWGPUCulledInstanceDebugIndirectParameters, nullptr);
	}

private:
	LAYOUT_FIELD(FShaderParameter, NumInstances);
	LAYOUT_FIELD(FShaderParameter, ViewLocation);
	LAYOUT_FIELD(FShaderParameter, ViewForward);
	LAYOUT_FIELD(FShaderParameter, FrustumPermutedPlanes0);
	LAYOUT_FIELD(FShaderParameter, FrustumPermutedPlanes1);
	LAYOUT_FIELD(FShaderParameter, ProjMatrix);
	LAYOUT_FIELD(FShaderParameter, PreviewTranslation);
	LAYOUT_FIELD(FShaderParameter, TranslatedViewProjectionMatrix);
	LAYOUT_FIELD(FShaderParameter, HZBSize);
	LAYOUT_FIELD(FShaderParameter, HZBUvFactor);
	LAYOUT_FIELD(FShaderParameter, LiteGPUSceneMinScreenSize);

	LAYOUT_FIELD(FShaderResourceParameter, HZBTexture);
	LAYOUT_FIELD(FShaderResourceParameter, HZBSampler);
	LAYOUT_FIELD(FShaderResourceParameter, AABBPatchBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, RWInstanceTransformBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, RWInstanceTypeBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, RWUnCulledInstanceBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, RWGPUUnCulledInstanceNum);
	LAYOUT_FIELD(FShaderResourceParameter, RWGPUUnCulledInstanceScreenSize);
	// For Debug and DepthDrawTest
	LAYOUT_FIELD(FShaderResourceParameter, RWGPUUnCulledInstanceIndirectParameters);
	// For Debugging
	LAYOUT_FIELD(FShaderResourceParameter, RWCulledInstanceBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, RWGPUCulledInstanceDebugIndirectParameters);
};

IMPLEMENT_GLOBAL_SHADER(FLiteGPUSceneCullingCS, "/Engine/Private/LiteGPUSceneCulling.usf", "LiteGPUSceneCullingCS", SF_Compute);

namespace Detail
{
	void Cull(FRHICommandList& RHICmdList
		, const FViewInfo& View
		, class FLiteGPUSceneProxy* CullingProxy
		, class FLiteGPUSceneProxyVisibilityData* ResVisibilityData)
	{

	}	
	
	void DepthDrawTest(FRHICommandList& RHICmdList
		, const FViewInfo& View
		, class FLiteGPUSceneProxy* CullingProxy
		, class FLiteGPUSceneProxyVisibilityData* ResVisibilityData)
	{

	}	

	void Debug(FRHICommandList& RHICmdList
		, const FViewInfo& View
		, class FLiteGPUSceneProxy* CullingProxy
		, class FLiteGPUSceneProxyVisibilityData* ResVisibilityData)
	{

	}
};

void AddLiteGPUSceneCullingPass(FRDGBuilder& GraphBuilder, const FViewInfo& View)
{
	static auto CVarEnableLiteGPUScene = IConsoleManager::Get().FindConsoleVariable(TEXT("r.LiteGPUScene.Display"));
	bool bLiteGPUScene = CVarEnableLiteGPUScene && CVarEnableLiteGPUScene->GetInt() > 0;
	if (!bLiteGPUScene)
	{
		return;
	}
	check(IsInRenderingThread());
	if (View.bIsSceneCapture || View.bIsReflectionCapture || View.bIsPlanarReflection)
	{
		return;
	}

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("LiteGPUScene::Culling"),
		ERDGPassFlags::Compute,
		[&View](FRHICommandList& RHICmdList)
		{
			const FSceneViewFamily& ViewFamily = *(View.Family);
			FScene& Scene = *(FScene*)ViewFamily.Scene;
			for (auto Proxy : Scene.CachedLiteGPUScene)
			{
				if (Proxy && Proxy->IsInitialized())
				{
					Detail::Cull(RHICmdList, View, Proxy,
						Proxy->SharedMainVisibilityData.Get());
					if (sLiteGPUSceneEnableDepthDrawTest > 0)
					{
						Detail::DepthDrawTest(RHICmdList, View, Proxy,
							Proxy->SharedMainVisibilityData.Get());
					}

		#if ENABLE_LITE_GPU_SCENE_DEBUG
					if (GEnableDebugLiteGPUSceneBounds)
					{
						Detail::Debug(
							RHICmdList, View, View.ViewRect, Proxy,
							Proxy->SharedMainVisibilityData.Get());
					}
		#endif
					Proxy->GenerateIndirectDrawBuffer(&View, ViewFamily,
						Proxy->SharedMainVisibilityData.Get(),
						Proxy->SharedPerInstanceData.Get());
				}
			}
		});
}
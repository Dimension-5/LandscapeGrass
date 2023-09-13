#include "CompositionLighting/PostProcessLiteGPUScene.h"
#include "LiteGPUSceneSubsystem.h"
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

class FClearLiteGPUSceneResCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FClearLiteGPUSceneResCS);
	SHADER_USE_PARAMETER_STRUCT(FClearLiteGPUSceneResCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumInstances)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, RWUnCulledInstanceBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, RWGPUUnCulledInstanceNum)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, RWGPUUnCulledInstanceScreenSize)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), LITE_GPU_SCENE_COMPUTE_THREAD_NUM);
		// OutEnvironment.SetDefine(TEXT("ENABLE_LITE_GPU_SCENE_DEBUG"), ENABLE_LITE_GPU_SCENE_DEBUG);
	}
};
IMPLEMENT_GLOBAL_SHADER(FClearLiteGPUSceneResCS, "/Engine/Private/LiteGPUSceneCulling.usf", "ClearGPUCullingResCS", SF_Compute);

class FLiteGPUSceneCullingCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLiteGPUSceneCullingCS);
	SHADER_USE_PARAMETER_STRUCT(FLiteGPUSceneCullingCS, FGlobalShader);
	class FWaveOps : SHADER_PERMUTATION_BOOL("DIM_WAVE_OPS");
	using FPermutationDomain = TShaderPermutationDomain<FWaveOps>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumInstances)
		SHADER_PARAMETER(FVector3f, ViewLocation)
		SHADER_PARAMETER(FMatrix44f, FrustumPermutedPlanes0)
		SHADER_PARAMETER(FMatrix44f, FrustumPermutedPlanes1)
		SHADER_PARAMETER(FMatrix44f, ProjMatrix)
		SHADER_PARAMETER(FVector4f, PreviewTranslation)
		SHADER_PARAMETER(FMatrix44f, TranslatedViewProjectionMatrix)
		SHADER_PARAMETER(FVector4f, HZBSize)
		SHADER_PARAMETER(FVector4f, HZBUvFactor)
		SHADER_PARAMETER(float, LiteGPUSceneMinScreenSize)
		SHADER_PARAMETER_SAMPLER(SamplerState, HZBSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HZBTexture)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, AABBBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, InstanceTransformBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, InstanceTypeBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, RWUnCulledInstanceBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, RWGPUUnCulledInstanceNum)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, RWGPUUnCulledInstanceScreenSize)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), LITE_GPU_SCENE_COMPUTE_THREAD_NUM);
		// OutEnvironment.SetDefine(TEXT("ENABLE_LITE_GPU_SCENE_DEBUG"), ENABLE_LITE_GPU_SCENE_DEBUG);

		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FWaveOps>())
		{
			OutEnvironment.CompilerFlags.Add(CFLAG_WaveOperations);
		}
	}
};
IMPLEMENT_GLOBAL_SHADER(FLiteGPUSceneCullingCS, "/Engine/Private/LiteGPUSceneCulling.usf", "LiteGPUSceneCullingCS", SF_Compute);

namespace Detail
{
	void Cull(FRHICommandList& RHICmdList
		, const FViewInfo& View
		, class FLiteGPUSceneProxy* CullingProxy)
	{

	}

	void DepthDrawTest(FRHICommandList& RHICmdList
		, const FViewInfo& View
		, class FLiteGPUSceneProxy* CullingProxy)
	{

	}

	void Debug(FRHICommandList& RHICmdList
		, const FViewInfo& View
		, class FLiteGPUSceneProxy* CullingProxy)
	{

	}
};

void AddLiteGPUSceneCullingPass(FRDGBuilder& GraphBuilder, const FViewInfo& View)
{
	static auto CVarEnableLiteGPUScene = IConsoleManager::Get().FindConsoleVariable(TEXT("r.LiteGPUScene.Enable"));
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
		{},
		ERDGPassFlags::NeverParallel,
		[&View](FRHICommandList& RHICmdList)
		{
			const FSceneViewFamily& ViewFamily = *(View.Family);
			FScene& Scene = *(FScene*)ViewFamily.Scene;
			for (auto Proxy : Scene.CachedLiteGPUScene)
			{
				if (Proxy && Proxy->IsInitialized())
				{
					Detail::Cull(RHICmdList, View, Proxy);
				}
			}
		});
}
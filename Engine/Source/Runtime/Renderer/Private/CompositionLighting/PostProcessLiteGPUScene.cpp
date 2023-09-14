#include "CompositionLighting/PostProcessLiteGPUScene.h"
#include "LiteGPUSceneSubsystem.h"
#include "SceneUtils.h"
#include "ScenePrivate.h"
#include "ClearQuad.h"
#include "PipelineStateCache.h"
#include "GlobalShader.h"
#include "RHICommandList.h"
#include "RHIUtilities.h"
#include "RenderGraphUtils.h"
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
		SHADER_PARAMETER(FVector3f, ViewForward)
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
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, RWUnCulledInstanceNum)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, RWUnCulledInstanceScreenSize)
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
	void Clear(FRDGBuilder& GraphBuilder
		, const FViewInfo& View
		, class FLiteGPUSceneProxy* CullingProxy)
	{
		const auto InstanceNum = CullingProxy->Scene->GetInstanceNum();

		// FILL PARAMETERS
		auto ViewBufferState = CullingProxy->Scene->GetViewBufferState();
		auto ClearParameters = GraphBuilder.AllocParameters<FClearLiteGPUSceneResCS::FParameters>();
		ClearParameters->NumInstances = InstanceNum;
		ClearParameters->RWUnCulledInstanceBuffer = GraphBuilder.CreateUAV(ViewBufferState.UnCulledInstanceBuffer);
		ClearParameters->RWGPUUnCulledInstanceScreenSize = GraphBuilder.CreateUAV(ViewBufferState.UnCulledInstanceScreenSize);
		ClearParameters->RWGPUUnCulledInstanceNum = GraphBuilder.CreateUAV(ViewBufferState.UnCulledInstanceNum);

		// GET COMPUTE SHADER
		const auto& ShaderMap = GetGlobalShaderMap(View.FeatureLevel);
		TShaderMapRef<FClearLiteGPUSceneResCS> ClearShader(ShaderMap);

		// CALCULATE AND DISPATCH
		const auto NumThreadGroups = FMath::DivideAndRoundUp<uint32>(InstanceNum, LITE_GPU_SCENE_COMPUTE_THREAD_NUM);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("LiteGPUScene::ClearBuffers"),
			ERDGPassFlags::Compute,
			ClearShader,
			ClearParameters,
			FIntVector(NumThreadGroups, 1, 1)
		);
	}

	void Cull(FRDGBuilder& GraphBuilder
		, const FViewInfo& View
		, class FLiteGPUSceneProxy* CullingProxy)
	{
		const auto InstanceNum = CullingProxy->Scene->GetInstanceNum();
		FVector3f PreviewTranslationVector = FVector3f(View.ViewMatrices.GetPreViewTranslation());
		FVector3f ViewLoc = FVector3f(View.ViewMatrices.GetViewOrigin());

		// FILL PARAMETERS
		auto ViewBufferState = CullingProxy->Scene->GetViewBufferState();
		auto SceneBufferState = CullingProxy->Scene->GetSceneBufferState();
		auto CullParameters = GraphBuilder.AllocParameters<FLiteGPUSceneCullingCS::FParameters>();
		CullParameters->NumInstances = InstanceNum;
		CullParameters->ViewLocation = ViewLoc;
		CullParameters->ViewForward = FVector3f(View.GetViewDirection());
		CullParameters->PreviewTranslation = FVector4f(PreviewTranslationVector.X, PreviewTranslationVector.Y, PreviewTranslationVector.Z, 0);
		CullParameters->TranslatedViewProjectionMatrix = FMatrix44f(View.ViewMatrices.GetTranslatedViewProjectionMatrix());
		CullParameters->LiteGPUSceneMinScreenSize = sLiteGPUSceneMinScreenSize;
		// Frustum
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

			CullParameters->FrustumPermutedPlanes0 = FMatrix44f(PermutedPlanesMatrix0);
			CullParameters->FrustumPermutedPlanes1 = FMatrix44f(PermutedPlanesMatrix1);
		}
		FMatrix InputProj = View.ViewMatrices.GetProjectionMatrix();
		CullParameters->ProjMatrix = FMatrix44f(InputProj);
		// Occulusion culling
		{
			const float HZBMipmapCounts = FMath::Log2((float)FMath::Max(View.HZBMipmap0Size.X, View.HZBMipmap0Size.Y));
			const float HZBMinMip = FMath::Clamp((float)sLiteGPUSceneHZBMinMipLevel, 0.0f, HZBMipmapCounts - 1);
			const float HZBMaxMip = FMath::Clamp((float)sLiteGPUSceneHZBMaxMipLevel, 0.0f, HZBMipmapCounts - 1);
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
			CullParameters->HZBUvFactor = HZBUvFactorValue;
			CullParameters->HZBSize = HZBSizeValue;
			CullParameters->HZBSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			CullParameters->HZBTexture = View.HZB;
		}
		CullParameters->RWUnCulledInstanceBuffer = GraphBuilder.CreateUAV(ViewBufferState.UnCulledInstanceBuffer);
		CullParameters->RWUnCulledInstanceScreenSize = GraphBuilder.CreateUAV(ViewBufferState.UnCulledInstanceScreenSize);
		CullParameters->RWUnCulledInstanceNum = GraphBuilder.CreateUAV(ViewBufferState.UnCulledInstanceNum);
		CullParameters->AABBBuffer = GraphBuilder.CreateSRV(SceneBufferState.MeshAABBBuffer);
		CullParameters->InstanceTypeBuffer = GraphBuilder.CreateSRV(SceneBufferState.InstanceAttributeBuffer);
		CullParameters->InstanceTransformBuffer = GraphBuilder.CreateSRV(SceneBufferState.InstanceTransformBuffer);

		// GET COMPUTE SHADER
		FLiteGPUSceneCullingCS::FPermutationDomain PermutationVector;
		const bool bUseWaveOps = GRHISupportsWaveOperations && GRHIMinimumWaveSize >= 32 && GLiteGPUSceneCullingUseWaveOps;
		PermutationVector.Set<FLiteGPUSceneCullingCS::FWaveOps>(bUseWaveOps);
		const auto& ShaderMap = GetGlobalShaderMap(View.FeatureLevel);
		TShaderMapRef<FLiteGPUSceneCullingCS> CullingShader(ShaderMap, PermutationVector);

		// CALCULATE AND DISPATCH
		const auto NumThreadGroups = FMath::DivideAndRoundUp<uint32>(InstanceNum, LITE_GPU_SCENE_COMPUTE_THREAD_NUM);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("LiteGPUScene::Cull"),
			ERDGPassFlags::Compute,
			CullingShader,
			CullParameters,
			FIntVector(NumThreadGroups, 1, 1)
		);
	}

	void DepthDrawTest(FRDGBuilder& GraphBuilder
		, const FViewInfo& View
		, class FLiteGPUSceneProxy* CullingProxy)
	{
		// TODO
	}

	void Debug(FRDGBuilder& GraphBuilder
		, const FViewInfo& View
		, class FLiteGPUSceneProxy* CullingProxy)
	{
		// TODO
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

	const FSceneViewFamily& ViewFamily = *(View.Family);
	FScene& Scene = *(FScene*)ViewFamily.Scene;
	for (auto Proxy : Scene.CachedLiteGPUScene)
	{
		if (Proxy && Proxy->IsInitialized())
		{
			Detail::Clear(GraphBuilder, View, Proxy);
			Detail::Cull(GraphBuilder, View, Proxy);
			Detail::DepthDrawTest(GraphBuilder, View, Proxy);
			Detail::Debug(GraphBuilder, View, Proxy);
		}
	}
}
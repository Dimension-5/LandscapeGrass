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

	class FUseScreenSize : SHADER_PERMUTATION_BOOL("SCREEN_SIZE_CULLING");
	class FUseDistance : SHADER_PERMUTATION_BOOL("DISTANCE_CULLING");
	using FPermutationDomain = TShaderPermutationDomain<FUseScreenSize, FUseDistance>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumInstances)
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
		if (PermutationVector.Get<FUseScreenSize>())
		{
			OutEnvironment.SetDefine(TEXT("LITE_GPU_SCENE_SCREEN_SIZE_CULLING"), 1);
		}		
		else
		{
			OutEnvironment.SetDefine(TEXT("LITE_GPU_SCENE_SCREEN_SIZE_CULLING"), 0);
		}
		if (PermutationVector.Get<FUseDistance>())
		{
			OutEnvironment.SetDefine(TEXT("LITE_GPU_SCENE_DISTANCE_CULLING"), 1);
		}
		else
		{
			OutEnvironment.SetDefine(TEXT("LITE_GPU_SCENE_DISTANCE_CULLING"), 0);
		}
	}
};
IMPLEMENT_GLOBAL_SHADER(FClearLiteGPUSceneResCS, "/Engine/Private/LiteGPUSceneCulling.usf", "ClearGPUCullingResCS", SF_Compute);

class FLiteGPUSceneCullingCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLiteGPUSceneCullingCS);
	SHADER_USE_PARAMETER_STRUCT(FLiteGPUSceneCullingCS, FGlobalShader);
	class FWaveOps : SHADER_PERMUTATION_BOOL("DIM_WAVE_OPS");
	class FUseScreenSize : SHADER_PERMUTATION_BOOL("SCREEN_SIZE_CULLING");
	class FUseDistance : SHADER_PERMUTATION_BOOL("DISTANCE_CULLING");
	using FPermutationDomain = TShaderPermutationDomain<FWaveOps, FUseScreenSize, FUseDistance>;

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
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, CullDistanceBuffer)
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
		if (PermutationVector.Get<FUseScreenSize>())
		{
			OutEnvironment.SetDefine(TEXT("LITE_GPU_SCENE_SCREEN_SIZE_CULLING"), 1);
		}
		else
		{
			OutEnvironment.SetDefine(TEXT("LITE_GPU_SCENE_SCREEN_SIZE_CULLING"), 0);
		}
		if (PermutationVector.Get<FUseDistance>())
		{
			OutEnvironment.SetDefine(TEXT("LITE_GPU_SCENE_DISTANCE_CULLING"), 1);
		}
		else
		{
			OutEnvironment.SetDefine(TEXT("LITE_GPU_SCENE_DISTANCE_CULLING"), 0);
		}
	}
};
IMPLEMENT_GLOBAL_SHADER(FLiteGPUSceneCullingCS, "/Engine/Private/LiteGPUSceneCulling.usf", "LiteGPUSceneCullingCS", SF_Compute);


class FClearVisibleInstanceCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FClearVisibleInstanceCS);
	SHADER_USE_PARAMETER_STRUCT(FClearVisibleInstanceCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, AllSectionNum)
		SHADER_PARAMETER(uint32, AllInstanceIndexNum)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, UnCulledInstanceNum)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, RWIndirectDrawDispatchIndiretBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, RWInstanceIndiceBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, RWSectionCountBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, RWSectionCountCopyBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, RWSectionCountOffsetBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, RWNextSectionCountOffsetBuffer)
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
IMPLEMENT_GLOBAL_SHADER(FClearVisibleInstanceCS, "/Engine/Private/LiteGPUSceneIndirectDrawGenerator.usf", "ClearVisibleInstanceCS", SF_Compute);

class FCountingInstanceIndiceCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCountingInstanceIndiceCS);
	SHADER_USE_PARAMETER_STRUCT(FCountingInstanceIndiceCS, FGlobalShader);
	class FUseScreenSize : SHADER_PERMUTATION_BOOL("SCREEN_SIZE_CULLING");
	using FPermutationDomain = TShaderPermutationDomain<FUseScreenSize>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(int32, PerSectionMaxNum)
		SHADER_PARAMETER(FMatrix44f, ProjMatrix)
		SHADER_PARAMETER(float, InstanceMaxScreenSize)
		SHADER_PARAMETER(float, InstanceScreenSizeBias)
		SHADER_PARAMETER(float, InstanceScreenSizeScale)
		SHADER_PARAMETER(uint32, AllSectionNum)
		SHADER_PARAMETER(FVector3f, ViewLocation)
		SHADER_PARAMETER(FVector3f, ViewForward)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, UnCulledInstanceScreenSize)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, UnCulledInstanceBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, UnCulledInstanceNum)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, InstanceTypeBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, InstanceSectionNumBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, InstanceSectionIDBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, AllSectionInfoBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, InstanceTransformBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, AABBSectionBuffer)

		//------------------------------------//

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, RWSectionCountCopyBuffer)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), LITE_GPU_SCENE_COMPUTE_THREAD_NUM);
		OutEnvironment.SetDefine(TEXT("Generate_Instance_Count_Pass"), LITE_GPU_SCENE_COMPUTE_THREAD_NUM);

		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FUseScreenSize>())
		{
			OutEnvironment.SetDefine(TEXT("LITE_GPU_SCENE_SCREEN_SIZE_CULLING"), 1);
		}
		else
		{
			OutEnvironment.SetDefine(TEXT("LITE_GPU_SCENE_SCREEN_SIZE_CULLING"), 0);
		}
		// OutEnvironment.SetDefine(TEXT("ENABLE_LITE_GPU_SCENE_DEBUG"), ENABLE_LITE_GPU_SCENE_DEBUG);
	}
};
IMPLEMENT_GLOBAL_SHADER(FCountingInstanceIndiceCS, "/Engine/Private/LiteGPUSceneIndirectDrawGenerator.usf", "GenerateInstanceIndiceCS", SF_Compute);

class FGenerateInstanceIndiceCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGenerateInstanceIndiceCS);
	SHADER_USE_PARAMETER_STRUCT(FGenerateInstanceIndiceCS, FGlobalShader);
	class FUseScreenSize : SHADER_PERMUTATION_BOOL("SCREEN_SIZE_CULLING");
	using FPermutationDomain = TShaderPermutationDomain<FUseScreenSize>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(int32, PerSectionMaxNum)
		SHADER_PARAMETER(FMatrix44f, ProjMatrix)
		SHADER_PARAMETER(float, InstanceMaxScreenSize)
		SHADER_PARAMETER(float, InstanceScreenSizeBias)
		SHADER_PARAMETER(float, InstanceScreenSizeScale)
		SHADER_PARAMETER(uint32, AllSectionNum)
		SHADER_PARAMETER(FVector3f, ViewLocation)
		SHADER_PARAMETER(FVector3f, ViewForward)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, UnCulledInstanceScreenSize)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, UnCulledInstanceBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, UnCulledInstanceNum)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, InstanceTypeBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, InstanceSectionNumBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, InstanceSectionIDBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, AllSectionInfoBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, InstanceTransformBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, AABBSectionBuffer)

		//------------------------------------//

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, SectionCountOffsetBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, RWSectionCountBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, RWInstanceIndiceBuffer)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), LITE_GPU_SCENE_COMPUTE_THREAD_NUM);

		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FUseScreenSize>())
		{
			OutEnvironment.SetDefine(TEXT("LITE_GPU_SCENE_SCREEN_SIZE_CULLING"), 1);
		}
		else
		{
			OutEnvironment.SetDefine(TEXT("LITE_GPU_SCENE_SCREEN_SIZE_CULLING"), 0);
		}
		// OutEnvironment.SetDefine(TEXT("ENABLE_LITE_GPU_SCENE_DEBUG"), ENABLE_LITE_GPU_SCENE_DEBUG);
	}
};
IMPLEMENT_GLOBAL_SHADER(FGenerateInstanceIndiceCS, "/Engine/Private/LiteGPUSceneIndirectDrawGenerator.usf", "GenerateInstanceIndiceCS", SF_Compute);

class FGenerateDrawArgumentsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGenerateDrawArgumentsCS);
	SHADER_USE_PARAMETER_STRUCT(FGenerateDrawArgumentsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, AllSectionNum)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, AllSectionInfoBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, SectionCountCopyBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, RWSectionCountOffsetBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, RWNextSectionCountOffsetBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, RWIndirectDrawBuffer)
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
IMPLEMENT_GLOBAL_SHADER(FGenerateDrawArgumentsCS, "/Engine/Private/LiteGPUSceneIndirectDrawGenerator.usf", "GenerateDrawArgumentsCS", SF_Compute);

namespace LiteGPUScene::Detail
{
	struct BufferBlackboard
	{
		FRDGBufferRef RWSectionCountBuffer = nullptr;
		FRDGBufferRef RWSectionCountCopyBuffer = nullptr;
		FRDGBufferRef RWSectionCountOffsetBuffer = nullptr;
		FRDGBufferRef RWNextSectionCountOffsetBuffer = nullptr;

		FRDGBufferRef RWUnCulledInstanceScreenSize = nullptr;
		FRDGBufferRef RWUnCulledInstanceBuffer = nullptr;
		FRDGBufferRef RWUnCulledInstanceNum = nullptr;
		FRDGBufferRef RWIndirectDrawDispatchIndiretBuffer = nullptr;
		FRDGBufferRef RWInstanceIndiceBuffer = nullptr;
		FRDGBufferRef RWIndirectDrawBuffer = nullptr;
		FRDGBufferRef RWUnCulledInstanceIndirectParameters = nullptr;

		FRDGBufferRef SectionInfoBuffer = nullptr;
		FRDGBufferRef MeshAABBBuffer = nullptr;
		FRDGBufferRef MeshCullDistanceBuffer = nullptr;
		FRDGBufferRef InstanceTypeBuffer = nullptr;
		FRDGBufferRef InstanceSectionNumBuffer = nullptr;
		FRDGBufferRef InstanceSectionIDBuffer = nullptr;
		FRDGBufferRef InstanceTransformBuffer;
	};

	void Clear(FRDGBuilder& GraphBuilder
		, const FViewInfo& View
		, class FLiteGPUSceneProxy* CullingProxy
		, const BufferBlackboard& Buffers)
	{
		const auto InstanceNum = CullingProxy->Scene->GetInstanceNum();

		// FILL PARAMETERS
		auto ClearParameters = GraphBuilder.AllocParameters<FClearLiteGPUSceneResCS::FParameters>();
		ClearParameters->NumInstances = InstanceNum;
		ClearParameters->RWUnCulledInstanceBuffer = GraphBuilder.CreateUAV(Buffers.RWUnCulledInstanceBuffer, PF_R32_SINT);
		if (Buffers.RWUnCulledInstanceScreenSize)
		{
			ClearParameters->RWUnCulledInstanceScreenSize = GraphBuilder.CreateUAV(Buffers.RWUnCulledInstanceScreenSize, PF_R32_FLOAT);
		}
		ClearParameters->RWUnCulledInstanceNum = GraphBuilder.CreateUAV(Buffers.RWUnCulledInstanceNum, PF_R32_UINT);

		// GET COMPUTE SHADER
		const auto& ShaderMap = GetGlobalShaderMap(View.FeatureLevel);
		FClearLiteGPUSceneResCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FClearLiteGPUSceneResCS::FUseScreenSize>((bool)Buffers.RWUnCulledInstanceScreenSize);
		PermutationVector.Set<FClearLiteGPUSceneResCS::FUseDistance>((bool)Buffers.MeshCullDistanceBuffer);
		TShaderMapRef<FClearLiteGPUSceneResCS> ClearShader(ShaderMap, PermutationVector);

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
		, class FLiteGPUSceneProxy* CullingProxy
		, const BufferBlackboard& Buffers)
	{
		const auto InstanceNum = CullingProxy->Scene->GetInstanceNum();
		FVector3f PreviewTranslationVector = FVector3f(View.ViewMatrices.GetPreViewTranslation());
		FVector3f ViewLoc = FVector3f(View.ViewMatrices.GetViewOrigin());

		// FILL PARAMETERS
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
		CullParameters->RWUnCulledInstanceBuffer = GraphBuilder.CreateUAV(Buffers.RWUnCulledInstanceBuffer, PF_R32_SINT);
		if (Buffers.RWUnCulledInstanceScreenSize)
		{
			CullParameters->RWUnCulledInstanceScreenSize = GraphBuilder.CreateUAV(Buffers.RWUnCulledInstanceScreenSize, PF_R32_FLOAT);
		}
		if (Buffers.MeshCullDistanceBuffer)
		{
			CullParameters->CullDistanceBuffer = GraphBuilder.CreateSRV(Buffers.MeshCullDistanceBuffer, PF_R32G32_UINT);
		}
		CullParameters->RWUnCulledInstanceNum = GraphBuilder.CreateUAV(Buffers.RWUnCulledInstanceNum, PF_R32_UINT);
		CullParameters->AABBBuffer = GraphBuilder.CreateSRV(Buffers.MeshAABBBuffer, PF_A32B32G32R32F);
		CullParameters->InstanceTypeBuffer = GraphBuilder.CreateSRV(Buffers.InstanceTypeBuffer, PF_R32_UINT);
		CullParameters->InstanceTransformBuffer = GraphBuilder.CreateSRV(Buffers.InstanceTransformBuffer, PF_A32B32G32R32F);

		// GET COMPUTE SHADER
		FLiteGPUSceneCullingCS::FPermutationDomain PermutationVector;
		const bool bUseWaveOps = GRHISupportsWaveOperations && GRHIMinimumWaveSize >= 32 && GLiteGPUSceneCullingUseWaveOps;
		PermutationVector.Set<FLiteGPUSceneCullingCS::FWaveOps>(bUseWaveOps);
		PermutationVector.Set<FLiteGPUSceneCullingCS::FUseScreenSize>((bool)Buffers.RWUnCulledInstanceScreenSize);
		PermutationVector.Set<FLiteGPUSceneCullingCS::FUseDistance>((bool)Buffers.MeshCullDistanceBuffer);
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
		, class FLiteGPUSceneProxy* CullingProxy
		, const BufferBlackboard& Buffers)
	{
		// TODO
	}

	TRefCountPtr<FRDGPooledBuffer> OutIndicesBuffer;

	void GenerateDrawArgs(FRDGBuilder& GraphBuilder
		, const FViewInfo& View
		, class FLiteGPUSceneProxy* CullingProxy
		, const BufferBlackboard& Buffers)
	{
		const auto InstanceNum = CullingProxy->Scene->GetInstanceNum();
		const auto AllSectionNum = CullingProxy->Scene->GetSectionNum();
		// 1 CLEAR DRAW ARGS
		{
			// FILL PARAMETERS
			auto ClearParameters = GraphBuilder.AllocParameters<FClearVisibleInstanceCS::FParameters>();
			ClearParameters->AllSectionNum = AllSectionNum;
			ClearParameters->AllInstanceIndexNum = InstanceNum;
			ClearParameters->UnCulledInstanceNum = GraphBuilder.CreateSRV(Buffers.RWUnCulledInstanceNum, PF_R32_UINT);
			ClearParameters->RWIndirectDrawDispatchIndiretBuffer = GraphBuilder.CreateUAV(Buffers.RWIndirectDrawDispatchIndiretBuffer, PF_R32_UINT);
			ClearParameters->RWInstanceIndiceBuffer = GraphBuilder.CreateUAV(Buffers.RWInstanceIndiceBuffer, PF_R32_FLOAT);
			ClearParameters->RWSectionCountBuffer = GraphBuilder.CreateUAV(Buffers.RWSectionCountBuffer, PF_R32_UINT);
			ClearParameters->RWSectionCountCopyBuffer = GraphBuilder.CreateUAV(Buffers.RWSectionCountCopyBuffer, PF_R32_UINT);
			ClearParameters->RWSectionCountOffsetBuffer = GraphBuilder.CreateUAV(Buffers.RWSectionCountOffsetBuffer, PF_R32_UINT);
			ClearParameters->RWNextSectionCountOffsetBuffer = GraphBuilder.CreateUAV(Buffers.RWNextSectionCountOffsetBuffer, PF_R32_UINT);

			// GET COMPUTE SHADER
			const auto& ShaderMap = GetGlobalShaderMap(View.FeatureLevel);
			TShaderMapRef<FClearVisibleInstanceCS> ClearShader(ShaderMap);

			// CALCULATE AND DISPATCH
			const auto NumThreadGroups = FMath::DivideAndRoundUp<uint32>(InstanceNum, LITE_GPU_SCENE_COMPUTE_THREAD_NUM);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("LiteGPUScene::ClearDrawArgs"),
				ERDGPassFlags::Compute,
				ClearShader,
				ClearParameters,
				FIntVector(NumThreadGroups, 1, 1)
			);
		}
		// 2 COUNT INDICES
		{
			// FILL PARAMETERS
			auto Parameters = GraphBuilder.AllocParameters<FCountingInstanceIndiceCS::FParameters>();
			Parameters->PerSectionMaxNum = CullingProxy->Scene->GetPerSectionMaxNum();
			Parameters->ProjMatrix = FMatrix44f(View.ViewMatrices.GetProjectionMatrix());
			Parameters->ViewLocation = FVector3f(View.ViewMatrices.GetViewOrigin());
			Parameters->ViewForward = FVector3f(View.GetViewDirection());
			Parameters->InstanceScreenSizeBias = sLiteGPUSceneScreenSizeBias;
			Parameters->InstanceScreenSizeScale = sLiteGPUSceneLODDistanceScale * View.LODDistanceFactor;
			Parameters->InstanceMaxScreenSize = sLiteGPUSceneMaxScreenSize;
			Parameters->AllSectionNum = AllSectionNum;

			if (Buffers.RWUnCulledInstanceScreenSize)
			{
				Parameters->UnCulledInstanceScreenSize = GraphBuilder.CreateSRV(Buffers.RWUnCulledInstanceScreenSize, PF_R32_FLOAT);
			}
			Parameters->UnCulledInstanceBuffer = GraphBuilder.CreateSRV(Buffers.RWUnCulledInstanceBuffer, PF_R32_UINT);
			Parameters->UnCulledInstanceNum = GraphBuilder.CreateSRV(Buffers.RWUnCulledInstanceNum, PF_R32_UINT);
			
			Parameters->InstanceTypeBuffer = GraphBuilder.CreateSRV(Buffers.InstanceTypeBuffer, PF_R32_UINT);
			Parameters->InstanceSectionNumBuffer = GraphBuilder.CreateSRV(Buffers.InstanceSectionNumBuffer, PF_R32_UINT);
			Parameters->InstanceSectionIDBuffer = GraphBuilder.CreateSRV(Buffers.InstanceSectionIDBuffer, PF_R32_UINT);

			Parameters->AllSectionInfoBuffer = GraphBuilder.CreateSRV(Buffers.SectionInfoBuffer, PF_A32B32G32R32F);
			Parameters->InstanceTransformBuffer = GraphBuilder.CreateSRV(Buffers.InstanceTransformBuffer, PF_A32B32G32R32F);
			Parameters->AABBSectionBuffer = GraphBuilder.CreateSRV(Buffers.MeshAABBBuffer, PF_A32B32G32R32F);
			
			Parameters->RWSectionCountCopyBuffer = GraphBuilder.CreateUAV(Buffers.RWSectionCountCopyBuffer, PF_R32_UINT);

			// GET COMPUTE SHADER
			const auto& ShaderMap = GetGlobalShaderMap(View.FeatureLevel);
			FCountingInstanceIndiceCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FCountingInstanceIndiceCS::FUseScreenSize>((bool)Buffers.RWUnCulledInstanceScreenSize);
			TShaderMapRef<FCountingInstanceIndiceCS> CountShader(ShaderMap, PermutationVector);

			// CALCULATE AND DISPATCH
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("LiteGPUScene::CountIndices"),
				ERDGPassFlags::Compute,
				CountShader,
				Parameters,
				Buffers.RWIndirectDrawDispatchIndiretBuffer, 0
			);
		}
		// 3 GENERATE ARGUMENTS
		{
			// FILL PARAMETERS
			auto Parameters = GraphBuilder.AllocParameters<FGenerateDrawArgumentsCS::FParameters>();
			Parameters->AllSectionNum = AllSectionNum;
			Parameters->AllSectionInfoBuffer = GraphBuilder.CreateSRV(Buffers.SectionInfoBuffer, PF_A32B32G32R32F);
			Parameters->SectionCountCopyBuffer = GraphBuilder.CreateSRV(Buffers.RWSectionCountCopyBuffer, PF_R32_UINT);

			Parameters->RWSectionCountOffsetBuffer = GraphBuilder.CreateUAV(Buffers.RWSectionCountOffsetBuffer, PF_R32_UINT);
			Parameters->RWNextSectionCountOffsetBuffer = GraphBuilder.CreateUAV(Buffers.RWNextSectionCountOffsetBuffer, PF_R32_UINT);
			Parameters->RWIndirectDrawBuffer = GraphBuilder.CreateUAV(Buffers.RWIndirectDrawBuffer, PF_R32_UINT);

			// GET COMPUTE SHADER
			const auto& ShaderMap = GetGlobalShaderMap(View.FeatureLevel);
			TShaderMapRef<FGenerateDrawArgumentsCS> OffsetShader(ShaderMap);

			// CALCULATE AND DISPATCH
			const auto NumThreadGroups = FMath::DivideAndRoundUp<uint32>(AllSectionNum, LITE_GPU_SCENE_COMPUTE_THREAD_NUM);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("LiteGPUScene::GenerateArguments"),
				ERDGPassFlags::Compute,
				OffsetShader,
				Parameters,
				FIntVector(NumThreadGroups, 1, 1)
			);
		}
		// 4 GENERATE INDICES
		{
			// FILL PARAMETERS
			auto Parameters = GraphBuilder.AllocParameters<FGenerateInstanceIndiceCS::FParameters>();
			Parameters->PerSectionMaxNum = CullingProxy->Scene->GetPerSectionMaxNum();
			Parameters->ProjMatrix = FMatrix44f(View.ViewMatrices.GetProjectionMatrix());
			Parameters->ViewLocation = FVector3f(View.ViewMatrices.GetViewOrigin());
			Parameters->ViewForward = FVector3f(View.GetViewDirection());
			Parameters->InstanceScreenSizeBias = sLiteGPUSceneScreenSizeBias;
			Parameters->InstanceScreenSizeScale = sLiteGPUSceneLODDistanceScale * View.LODDistanceFactor;
			Parameters->InstanceMaxScreenSize = sLiteGPUSceneMaxScreenSize;
			Parameters->AllSectionNum = AllSectionNum;

			if (Buffers.RWUnCulledInstanceScreenSize)
			{
				Parameters->UnCulledInstanceScreenSize = GraphBuilder.CreateSRV(Buffers.RWUnCulledInstanceScreenSize, PF_R32_FLOAT);
			}
			Parameters->UnCulledInstanceBuffer = GraphBuilder.CreateSRV(Buffers.RWUnCulledInstanceBuffer, PF_R32_UINT);
			Parameters->UnCulledInstanceNum = GraphBuilder.CreateSRV(Buffers.RWUnCulledInstanceNum, PF_R32_UINT);

			Parameters->InstanceTypeBuffer = GraphBuilder.CreateSRV(Buffers.InstanceTypeBuffer, PF_R32_UINT);
			Parameters->InstanceSectionNumBuffer = GraphBuilder.CreateSRV(Buffers.InstanceSectionNumBuffer, PF_R32_UINT);
			Parameters->InstanceSectionIDBuffer = GraphBuilder.CreateSRV(Buffers.InstanceSectionIDBuffer, PF_R32_UINT);

			Parameters->AllSectionInfoBuffer = GraphBuilder.CreateSRV(Buffers.SectionInfoBuffer, PF_A32B32G32R32F);
			Parameters->InstanceTransformBuffer = GraphBuilder.CreateSRV(Buffers.InstanceTransformBuffer, PF_A32B32G32R32F);
			Parameters->AABBSectionBuffer = GraphBuilder.CreateSRV(Buffers.MeshAABBBuffer, PF_A32B32G32R32F);

			Parameters->SectionCountOffsetBuffer = GraphBuilder.CreateSRV(Buffers.RWSectionCountOffsetBuffer, PF_R32_UINT);
			Parameters->RWSectionCountBuffer = GraphBuilder.CreateUAV(Buffers.RWSectionCountBuffer, PF_R32_UINT);
			Parameters->RWInstanceIndiceBuffer = GraphBuilder.CreateUAV(Buffers.RWInstanceIndiceBuffer, PF_R32_FLOAT);

			// GET COMPUTE SHADER
			const auto& ShaderMap = GetGlobalShaderMap(View.FeatureLevel);
			FGenerateInstanceIndiceCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FGenerateInstanceIndiceCS::FUseScreenSize>((bool)Buffers.RWUnCulledInstanceScreenSize);
			TShaderMapRef<FGenerateInstanceIndiceCS> DrawGenShader(ShaderMap, PermutationVector);

			// CALCULATE AND DISPATCH
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("LiteGPUScene::GenerateIndices"),
				ERDGPassFlags::Compute,
				DrawGenShader,
				Parameters,
				Buffers.RWIndirectDrawDispatchIndiretBuffer, 0
			);
			GraphBuilder.QueueBufferExtraction(Buffers.RWInstanceIndiceBuffer, &OutIndicesBuffer, ERHIAccess::VertexOrIndexBuffer);
		}
	}

	void Debug(FRDGBuilder& GraphBuilder
		, const FViewInfo& View
		, class FLiteGPUSceneProxy* CullingProxy
		, const BufferBlackboard& Buffers)
	{
		// TODO
	}
};

void AddLiteGPUSceneCullingPass(FRDGBuilder& GraphBuilder, const FViewInfo& View)
{
	using namespace LiteGPUScene;

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
		if (Proxy->Scene->GetInstanceNum() == 0)
			continue;

		auto ViewBufferState = Proxy->Scene->GetViewBufferState();
		auto SceneBufferState = Proxy->Scene->GetSceneBufferState();
		auto CounterBufferState = Proxy->Scene->GetCounterBufferState();

		Detail::BufferBlackboard Buffers;
		if (ViewBufferState.RWUnCulledInstanceScreenSize)
		{
			Buffers.RWUnCulledInstanceScreenSize = GraphBuilder.RegisterExternalBuffer(ViewBufferState.RWUnCulledInstanceScreenSize);
		}
		if (SceneBufferState.MeshCullDistanceBuffer)
		{
			Buffers.MeshCullDistanceBuffer = GraphBuilder.RegisterExternalBuffer(SceneBufferState.MeshCullDistanceBuffer);
		}
		Buffers.RWUnCulledInstanceBuffer = GraphBuilder.RegisterExternalBuffer(ViewBufferState.RWUnCulledInstanceBuffer);
		Buffers.RWUnCulledInstanceNum = GraphBuilder.RegisterExternalBuffer(ViewBufferState.RWUnCulledInstanceNum);
		Buffers.RWIndirectDrawDispatchIndiretBuffer = GraphBuilder.RegisterExternalBuffer(ViewBufferState.RWIndirectDrawDispatchIndiretBuffer);
		Buffers.RWInstanceIndiceBuffer = GraphBuilder.RegisterExternalBuffer(ViewBufferState.RWInstanceIndiceBuffer);
		Buffers.RWIndirectDrawBuffer = GraphBuilder.RegisterExternalBuffer(ViewBufferState.RWIndirectDrawBuffer);
		Buffers.RWUnCulledInstanceIndirectParameters = GraphBuilder.RegisterExternalBuffer(ViewBufferState.RWUnCulledInstanceIndirectParameters);
		
		Buffers.SectionInfoBuffer = GraphBuilder.RegisterExternalBuffer(SceneBufferState.SectionInfoBuffer);
		Buffers.MeshAABBBuffer = GraphBuilder.RegisterExternalBuffer(SceneBufferState.MeshAABBBuffer);
		Buffers.InstanceTransformBuffer = GraphBuilder.RegisterExternalBuffer(SceneBufferState.InstanceTransformBuffer);
		Buffers.InstanceTypeBuffer = GraphBuilder.RegisterExternalBuffer(SceneBufferState.InstanceTypeBuffer);
		Buffers.InstanceSectionNumBuffer = GraphBuilder.RegisterExternalBuffer(SceneBufferState.InstanceSectionNumBuffer);
		Buffers.InstanceSectionIDBuffer = GraphBuilder.RegisterExternalBuffer(SceneBufferState.InstanceSectionIDBuffer);

		Buffers.RWSectionCountBuffer = GraphBuilder.RegisterExternalBuffer(CounterBufferState.RWSectionCountBuffer);
		Buffers.RWSectionCountCopyBuffer = GraphBuilder.RegisterExternalBuffer(CounterBufferState.RWSectionCountCopyBuffer);
		Buffers.RWSectionCountOffsetBuffer = GraphBuilder.RegisterExternalBuffer(CounterBufferState.RWSectionCountOffsetBuffer);
		Buffers.RWNextSectionCountOffsetBuffer = GraphBuilder.RegisterExternalBuffer(CounterBufferState.RWNextSectionCountOffsetBuffer);

		if (Proxy && Proxy->IsInitialized())
		{
			Detail::Clear(GraphBuilder, View, Proxy, Buffers);
			Detail::Cull(GraphBuilder, View, Proxy, Buffers);
			Detail::DepthDrawTest(GraphBuilder, View, Proxy, Buffers);
			Detail::GenerateDrawArgs(GraphBuilder, View, Proxy, Buffers);
			Detail::Debug(GraphBuilder, View, Proxy, Buffers);
		}
	}
}
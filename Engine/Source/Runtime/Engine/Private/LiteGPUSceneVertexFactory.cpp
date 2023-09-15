#include "LiteGPUScene.h"
#include "Components/LiteGPUSceneComponent.h"
#include "LiteGPUSceneSubsystem.h"
#include "MeshDrawShaderBindings.h"

FLiteGPUSceneVertexFactory::FLiteGPUSceneVertexFactory(ERHIFeatureLevel::Type InFeatureLevel)
	: FLocalVertexFactory(InFeatureLevel, "FLiteGPUSceneVertexFactory"), bInitialized(false)
{

}

void FLiteGPUSceneVertexFactoryShaderParameters::Bind(const FShaderParameterMap& ParameterMap)
{
	FLocalVertexFactoryShaderParametersBase::Bind(ParameterMap);
	VertexFetch_PerInstanceTransformAParameter.Bind(ParameterMap, TEXT("VertexFetch_PerInstanceTransformAParameter"));
	VertexFetch_PerInstanceTransformBParameter.Bind(ParameterMap, TEXT("VertexFetch_PerInstanceTransformBParameter"));
	VertexFetch_PerInstanceTransformCParameter.Bind(ParameterMap, TEXT("VertexFetch_PerInstanceTransformCParameter"));
	VertexFetch_PerInstanceTransformDParameter.Bind(ParameterMap, TEXT("VertexFetch_PerInstanceTransformDParameter"));
}

void FLiteGPUSceneVertexFactoryShaderParameters::GetElementShaderBindings(const class FSceneInterface* Scene
	, const FSceneView* View, const class FMeshMaterialShader* Shader, const EVertexInputStreamType InputStreamType
	, ERHIFeatureLevel::Type FeatureLevel
	, const FVertexFactory* VertexFactory
	, const FMeshBatchElement& BatchElement
	, class FMeshDrawSingleShaderBindings& ShaderBindings
	, FVertexInputStreamArray& VertexStreams
) const
{
	FLocalVertexFactoryShaderParameters::GetElementShaderBindings(Scene, View, Shader, InputStreamType, FeatureLevel, VertexFactory, BatchElement, ShaderBindings, VertexStreams);

	const FLiteGPUSceneVertexFactoryUserData* BatchElementParams = (const FLiteGPUSceneVertexFactoryUserData*)BatchElement.UserData;
	if (FLiteGPUSceneProxy* SceneProxy = BatchElementParams->SceneProxy)
	{
		const auto& BufferStates = SceneProxy->Scene->BufferState;
		auto TransformBufferASRV = BufferStates.InstanceTransformBufferA->GetSRV();
		auto TransformBufferBSRV = BufferStates.InstanceTransformBufferB->GetSRV();
		auto TransformBufferCSRV = BufferStates.InstanceTransformBufferC->GetSRV();
		auto TransformBufferDSRV = BufferStates.InstanceTransformBufferD->GetSRV();
		ShaderBindings.Add(VertexFetch_PerInstanceTransformAParameter, TransformBufferASRV);
		ShaderBindings.Add(VertexFetch_PerInstanceTransformBParameter, TransformBufferBSRV);
		ShaderBindings.Add(VertexFetch_PerInstanceTransformCParameter, TransformBufferCSRV);
		ShaderBindings.Add(VertexFetch_PerInstanceTransformDParameter, TransformBufferDSRV);
	}
}

IMPLEMENT_TYPE_LAYOUT(FLiteGPUSceneVertexFactoryShaderParameters);

void FLiteGPUSceneVertexFactory::InitVertexFactory(const FLiteGPUSceneMeshVertexBuffer* VertexBuffer, const FVertexBuffer* InstanceIndiceBuffer)
{
	if (IsInRenderingThread())
	{
		Init_RenderThread(VertexBuffer, InstanceIndiceBuffer);
	}
	else {
		FLiteGPUSceneVertexFactory* LVertexFactory = this;
		const FLiteGPUSceneMeshVertexBuffer* LVertexBuffer = VertexBuffer;
		const FVertexBuffer* LInstanceIndiceBuffer = InstanceIndiceBuffer;

		ENQUEUE_RENDER_COMMAND(InitFSingleInstancedSkeletalMeshVertexFactory)(
			[LVertexFactory, LVertexBuffer, LInstanceIndiceBuffer](FRHICommandList& RHICmdList)
			{
				// Initialize the vertex factory's stream components.
				LVertexFactory->Init_RenderThread(LVertexBuffer, LInstanceIndiceBuffer);
			}
		);
	}
}

void FLiteGPUSceneVertexFactory::InitRHI(FRHICommandListBase& RHICmdList)
{
	check(HasValidFeatureLevel());
	const bool bInstanced = true;

	FVertexDeclarationElementList Elements;
	if (Data.PositionComponent.VertexBuffer != NULL)
	{
		Elements.Add(AccessStreamComponent(Data.PositionComponent, 0));
	}

	// only tangent,normal are used by the stream. the binormal is derived in the shader
	uint8 TangentBasisAttributes[2] = { 1, 2 };
	for (int32 AxisIndex = 0; AxisIndex < 2; AxisIndex++)
	{
		if (Data.TangentBasisComponents[AxisIndex].VertexBuffer != NULL)
		{
			Elements.Add(AccessStreamComponent(Data.TangentBasisComponents[AxisIndex], TangentBasisAttributes[AxisIndex]));
		}
	}
	Elements.Add(AccessStreamComponent(Data.ColorComponent, 3));

	if (Data.TextureCoordinates.Num())
	{
		const int32 BaseTexCoordAttribute = 4;
		for (int32 CoordinateIndex = 0; CoordinateIndex < Data.TextureCoordinates.Num(); CoordinateIndex++)
		{
			Elements.Add(AccessStreamComponent(
				Data.TextureCoordinates[CoordinateIndex],
				BaseTexCoordAttribute + CoordinateIndex
			));
		}
	}
	if (bInstanced)
	{
		// Third texture coord optimization xxy
		Elements.Add(AccessStreamComponent(FLiteGPUSceneVertexFactory::Data.InstanceIndicesComponent, 6));
	}
	// we don't need per-vertex shadow or lightmap rendering
	InitDeclaration(Elements);
}

void FLiteGPUSceneVertexFactory::Init_RenderThread(const FLiteGPUSceneMeshVertexBuffer* VertexBuffer, const FVertexBuffer* InstanceIndiceBuffer)
{
	check(IsInRenderingThread());
	FLiteGPUSceneVertexFactoryDataType NewData;
	NewData.PositionComponent = STRUCTMEMBER_VERTEXSTREAMCOMPONENT(VertexBuffer, FLiteGPUSceneMeshVertex, Position, VET_Float3);
	// UV
	NewData.TextureCoordinates.Add(
		FVertexStreamComponent(VertexBuffer, STRUCT_OFFSET(FLiteGPUSceneMeshVertex, TextureFirstCoordinate), sizeof(FLiteGPUSceneMeshVertex), VET_Float2)
	);

	NewData.TextureCoordinates.Add(
		FVertexStreamComponent(VertexBuffer, STRUCT_OFFSET(FLiteGPUSceneMeshVertex, TextureSecondCoordinate), sizeof(FLiteGPUSceneMeshVertex), VET_Float2)
	);

	NewData.TangentBasisComponents[0] = STRUCTMEMBER_VERTEXSTREAMCOMPONENT(VertexBuffer, FLiteGPUSceneMeshVertex, TangentX, VET_PackedNormal);
	NewData.TangentBasisComponents[1] = STRUCTMEMBER_VERTEXSTREAMCOMPONENT(VertexBuffer, FLiteGPUSceneMeshVertex, TangentZ, VET_PackedNormal);
	NewData.ColorComponent = STRUCTMEMBER_VERTEXSTREAMCOMPONENT(VertexBuffer, FLiteGPUSceneMeshVertex, Color, VET_Color);

	NewData.InstanceIndicesComponent = FVertexStreamComponent(
		InstanceIndiceBuffer,
		0,
		sizeof(float),
		EVertexElementType::VET_Float1,
		EVertexStreamUsage::Instancing
	);
	SetData(NewData);
	bInitialized = true;
}

IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FLiteGPUSceneVertexFactory, SF_Vertex, FLiteGPUSceneVertexFactoryShaderParameters);
IMPLEMENT_VERTEX_FACTORY_TYPE(FLiteGPUSceneVertexFactory, "/Engine/Private/LiteGPUSceneVertexFactory.ush",
	EVertexFactoryFlags::UsedWithMaterials
	| EVertexFactoryFlags::SupportsStaticLighting
	| EVertexFactoryFlags::SupportsDynamicLighting
	| EVertexFactoryFlags::SupportsPositionOnly
);
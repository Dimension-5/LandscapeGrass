#include "Components/LiteGPUSceneComponent.h"
#include "LiteGPUSceneSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogLiteGPUScene, Log, All);

void ULiteGPUSceneComponent::ManagedTick(float DeltaTime)
{
	ENQUEUE_RENDER_COMMAND(UpdateLiteGPUSceneOps)(
		[this](FRHICommandListImmediate& RHICmdList)
		{
			/*if*/ while (!PendingOps.IsEmpty())
			{
				TSharedPtr<Op> Ops = nullptr;
				{
					FScopeLock LCK(&OpsMutex);
					PendingOps.Dequeue(Ops);
				}
				if (Ops != nullptr)
				{
					if (Ops->OP == OP_ADD)
					{
						Handler->OnAdd(this, Ops->Instances);
					}
					else if (Ops->OP == OP_REMOVE)
					{
						Handler->OnRemove(this, Ops->Instances);
					}
				}
			}
		});
}

TArray<int64> ULiteGPUSceneComponent::AddInstancesWS(const TArray<FTransform>& InstanceTransforms)
{
	TArray<int64> InstanceIDs;
	TArray64<FLiteGPUSceneInstance> NewInstances;
	InstanceIDs.Reserve(InstanceIDs.Num() + InstanceTransforms.Num());
	NewInstances.Reserve(NewInstances.Num() + InstanceTransforms.Num());
	// TODO: parallel for ?
	for (const auto& Transform : InstanceTransforms)
	{
		auto& NewInstance = PersistantInstances.AddZeroed_GetRef();
		NewInstance.IDWithinComponent = NextInstanceID++;
		NewInstance.Transform = UE::Math::TTransform<float>(Transform);
		check(IDToSlotMap.Find(NewInstance.IDWithinComponent) == nullptr);
		IDToSlotMap.Add(NewInstance.IDWithinComponent, PersistantInstances.Num() - 1);
		InstanceIDs.Add(NewInstance.IDWithinComponent);
		NewInstances.Add(NewInstance);
	}
	auto NewOp = MakeShared<Op>(NewInstances, OP_ADD);
	{
		FScopeLock LCK(&OpsMutex);
		PendingOps.Enqueue(NewOp);
	}
	UE_LOG(LogLiteGPUScene, Log, TEXT("Added %d Instances to LiteGPUScene."), InstanceIDs.Num());
	return InstanceIDs;
}

bool ULiteGPUSceneComponent::GetInstanceTransformWS(int64 InstanceIndex, FTransform& OutInstanceTransform) const
{
	if (InstanceIndex < 0 || InstanceIndex >= PersistantInstances.Num())
	{
		return false;
	}
	const auto Slot = IDToSlotMap.FindRef(InstanceIndex);
	const auto& Instance = PersistantInstances[Slot];
	const auto SectorX = Instance.Transform;
	return true;
}

bool ULiteGPUSceneComponent::RemoveInstances(const TArray<int64>& InstancesToRemove)
{
	TArray64<FLiteGPUSceneInstance> RemovedInstances;
	RemovedInstances.Reserve(InstancesToRemove.Num());
	for (auto ID : InstancesToRemove)
	{
		const auto Slot = IDToSlotMap.FindRef(ID);
		if (Slot != INDEX_NONE)
		{
			RemovedInstances.Add(PersistantInstances[Slot]);
			PersistantInstances[Slot] = PersistantInstances.Last();
			PersistantInstances.SetNum(PersistantInstances.Num() - 1, true);
			IDToSlotMap[PersistantInstances[Slot].IDWithinComponent] = Slot;
			IDToSlotMap.Remove(ID);
		}
	}
	auto NewOp = MakeShared<Op>(RemovedInstances, OP_REMOVE);
	{
		FScopeLock LCK(&OpsMutex);
		PendingOps.Enqueue(NewOp);
	}
	PersistantInstances.Shrink();
	return false;
}

void ULiteGPUSceneComponent::SetCullDistances(int32 Start, int32 End)
{
	StartCullDistance.store(Start);
	EndCullDistance.store(End);
}

bool ULiteGPUSceneComponent::ClearInstances()
{
	auto NewOp = MakeShared<Op>(PersistantInstances, OP_REMOVE);
	{
		FScopeLock LCK(&OpsMutex);
		PendingOps.Enqueue(NewOp);
	}
	PersistantInstances.SetNum(0, true);
	IDToSlotMap.Empty();
	IDToSlotMap.Shrink();
	return false;
}

void ULiteGPUSceneComponent::OnRegister()
{
	Super::OnRegister();
}

void ULiteGPUSceneComponent::OnUnregister()
{
	Super::OnUnregister();
}

// TODO: REMOVE THIS
FLiteGPUSceneProxy::FLiteGPUSceneProxy(ULiteGPUSceneRenderComponent* Component, ERHIFeatureLevel::Type InFeatureLevel)
	: FPrimitiveSceneProxy(Component, TEXT("LiteGPUScene")),
	Scene(Component->Scene)
{
	pVFUserData = new FLiteGPUSceneVertexFactoryUserData();
	pVFUserData->SceneProxy = this;

	FLiteGPUSceneProxy* pLiteGPUSceneProxy = this;
	FSceneInterface* SceneInterface = Component->GetWorld()->Scene;
	CachedSceneInterface = SceneInterface;
	ENQUEUE_RENDER_COMMAND(InitLiteGPUProxy)(
		[pLiteGPUSceneProxy, SceneInterface](FRHICommandList& RHICmdList)
		{
			pLiteGPUSceneProxy->Init_RenderingThread();
			SceneInterface->AddOrRemoveLiteGPUSceneProxy_RenderingThread(pLiteGPUSceneProxy, true);
		}
	);

}

FLiteGPUSceneProxy::~FLiteGPUSceneProxy()
{
	if (IsInRenderingThread())
	{
		if (CachedSceneInterface)
		{
			CachedSceneInterface->AddOrRemoveLiteGPUSceneProxy_RenderingThread(this, false);
		}

		/*
		if (nullptr != pGPUDrivenVertexFactory)
		{
			pGPUDrivenVertexFactory->ReleaseResource();
			delete pGPUDrivenVertexFactory;
			pGPUDrivenVertexFactory = nullptr;
		}
		*/

		if (pVFUserData)
		{
			delete pVFUserData;
			pVFUserData = nullptr;
		}
	}
}

FPrimitiveViewRelevance FLiteGPUSceneProxy::GetViewRelevance(const FSceneView* View) const
{
	FPrimitiveViewRelevance Result;
	Result.bDrawRelevance = IsShown(View);
	Result.bShadowRelevance = IsShadowCast(View);
	Result.bDynamicRelevance = true;
	Result.bStaticRelevance = false;
	Result.bRenderInMainPass = ShouldRenderInMainPass();
	Result.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
	Result.bRenderCustomDepth = ShouldRenderCustomDepth();
	Result.bTranslucentSelfShadow = bCastVolumetricTranslucentShadow;
	Result.bVelocityRelevance = DrawsVelocity() & Result.bOpaque & Result.bRenderInMainPass;
	return Result;
}

void FLiteGPUSceneProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
	const FSceneView* MainView = Views[0];
	static auto CVarEnable = IConsoleManager::Get().FindConsoleVariable(TEXT("r.LiteGPUScene.Enable"));
	bool bLiteGPUScene = CVarEnable && CVarEnable->GetInt() > 0;
	if (!bLiteGPUScene || !IsInitialized())
	{
		return;
	}
	if (MainView->bIsSceneCapture || MainView->bIsReflectionCapture)
	{
		return;
	}

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		if (VisibilityMap & (1 << ViewIndex))
		{
			const FSceneView* View = Views[ViewIndex];
			const FConvexVolume* ShadowFrustomView = View->GetDynamicMeshElementsShadowCullFrustum();
			if (ShadowFrustomView)
			{
				return;
			}
			if (Scene->BufferState.InstanceTransformBufferA != nullptr)
			{
				// DispatchCulling();
				// GenerateDrawcalls();
				DrawMeshBatches(ViewIndex, View, ViewFamily, Collector);
			}
		}
	}
	return;
}

struct LiteGPUSceneIndirectArguments
{
	static void Init(LiteGPUSceneIndirectArguments& ia)
	{
		ia.IndexCountPerInstance = 0;
		ia.InstanceCount = 1;
		ia.StartIndexLocation = 0;
		ia.BaseVertexLocation = 0;
		ia.StartInstanceLocation = 0;
	}
	uint32 IndexCountPerInstance;
	uint32 InstanceCount;
	uint32 StartIndexLocation;
	int32 BaseVertexLocation;
	uint32 StartInstanceLocation;
};

void FLiteGPUSceneProxy::DrawMeshBatches(int32 ViewIndex, const FSceneView* View, const FSceneViewFamily& ViewFamily, FMeshElementCollector& Collector) const
{
	if (Scene->CombinedBuffer.IndexBuffer->IndicesNum <= 0)
	{
		return;
	}

	TMap<FMaterialRenderProxy*, TArray<int32>> MaterialToSectionIDsMap;
	TArray<FLiteGPUSceneMeshSectionInfo> AllSections;
	{
		FScopeLock LCK(&Scene->CombinedData.CombinedDataMutex);
		for (auto& pair : Scene->CombinedData.SectionsMap)
		{
			int32 MappedMaterialIndex = pair.Key;
			UMaterialInterface* Mat = Scene->CombinedData.Materials[MappedMaterialIndex];
			if (Mat && Mat->GetRenderProxy())
			{
				FMaterialRenderProxy* MatProxy = Mat->GetRenderProxy();
				const TArray<FLiteGPUSceneMeshSectionInfo>& SectionInfoArray = pair.Value;
				for (const FLiteGPUSceneMeshSectionInfo& SectionInfo : SectionInfoArray)
				{
					int32 InsertSectionIndex = AllSections.Add(SectionInfo);
					TArray<int32>* pSectionIndiceArray = MaterialToSectionIDsMap.Find(MatProxy);
					if (pSectionIndiceArray)
					{
						pSectionIndiceArray->Add(InsertSectionIndex);
					}
					else
					{
						TArray<int32> SectionIndiceArray;
						SectionIndiceArray.Add(InsertSectionIndex);
						MaterialToSectionIDsMap.Add(MatProxy, SectionIndiceArray);
					}
				}
			}
		}
	}

	int32 IndirectDrawOffset = 0;
	if (auto VertexFactory = Scene->GetVertexFactory())
	{
		for (const auto& Var : MaterialToSectionIDsMap)
		{
			FMeshBatch& MeshBatch = Collector.AllocateMesh();
			MeshBatch.LODIndex = 0;
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			MeshBatch.VisualizeHLODIndex = 0;
#endif
			MeshBatch.bWireframe = false;
			MeshBatch.VertexFactory = VertexFactory;
			MeshBatch.MaterialRenderProxy = Var.Key;
			MeshBatch.Type = PT_TriangleList;
			MeshBatch.DepthPriorityGroup = SDPG_World;

			FMeshBatchElement& BatchElement = MeshBatch.Elements[0];
			BatchElement.IndexBuffer = Scene->CombinedBuffer.IndexBuffer;
			BatchElement.PrimitiveUniformBuffer = GetUniformBuffer();
			BatchElement.FirstIndex = 0;
			BatchElement.NumPrimitives = 0;
			BatchElement.MinVertexIndex = 0;
			BatchElement.MaxVertexIndex = Scene->CombinedBuffer.VertexBuffer->VerticesNum - 1;

			BatchElement.UserData = (void*)pVFUserData;
			BatchElement.bUserDataIsColorVertexBuffer = false;
			BatchElement.InstancedLODIndex = 0;
			BatchElement.UserIndex = 0;
			BatchElement.NumInstances = 1;

			BatchElement.IndirectArgsBuffer = Scene->ViewBufferState.RWIndirectDrawBuffer->GetRHI();
			check(BatchElement.IndirectArgsBuffer != nullptr);
			BatchElement.IndirectArgsOffset = IndirectDrawOffset * sizeof(LiteGPUSceneIndirectArguments);

			BatchElement.FirstInstance = 0;
			BatchElement.IndirectDrawCount = Var.Value.Num();
			check(BatchElement.IndirectDrawCount != 0);
			BatchElement.IndirectBufferStride = sizeof(LiteGPUSceneIndirectArguments);

			IndirectDrawOffset += Var.Value.Num();
			Collector.AddMesh(ViewIndex, MeshBatch);
		}
	}
}

bool FLiteGPUSceneProxy::IsInitialized() const
{
	return Scene.IsValid();
}

#if RHI_RAYTRACING
bool FLiteGPUSceneProxy::HasRayTracingRepresentation() const
{
	return true;
}

void FLiteGPUSceneProxy::GetDynamicRayTracingInstances(FRayTracingMaterialGatheringContext& Context, TArray<struct FRayTracingInstance>& OutRayTracingInstances)
{

}
#endif

void FLiteGPUSceneProxy::Init_RenderingThread()
{
	check(Scene->CombinedBuffer.bInitialized);
	UE_LOG(LogLiteGPUScene, Log, TEXT("Call FLiteGPUSceneProxy::Init_RenderingThread"));
}

ULiteGPUSceneRenderComponent::ULiteGPUSceneRenderComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bRenderInDepthPass = true;
	BodyInstance.bSimulatePhysics = false;
}

void ULiteGPUSceneRenderComponent::OnRegister()
{
	Super::OnRegister();
}

void ULiteGPUSceneRenderComponent::OnUnregister()
{
	Super::OnUnregister();
}

void ULiteGPUSceneRenderComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials /* = false */) const
{
	Super::GetUsedMaterials(OutMaterials, bGetDebugMaterials);

	if (Scene.IsValid())
	{
		FScopeLock LCK(&Scene->CombinedData.CombinedDataMutex);
		for (int32 MatIndex = 0; MatIndex < Scene->CombinedData.Materials.Num(); MatIndex++)
		{
			OutMaterials.Add(Scene->CombinedData.Materials[MatIndex]);
		}
	}
}

FBoxSphereBounds ULiteGPUSceneRenderComponent::CalcBounds(const FTransform& BoundTransform) const
{
	// infinite bounds. TODO: accumulate actual bounds? will result in unstable depth sorting...
	float f = 9999999.0f;
	return FBoxSphereBounds(FBox(-FVector(f, f, f), FVector(f, f, f)));
	// return Super::CalcBounds(BoundTransform);
	// return FBoxSphereBounds(BoundTransform.GetLocation(), FVector::ZeroVector, 0.f);
}

FPrimitiveSceneProxy* ULiteGPUSceneRenderComponent::CreateSceneProxy()
{
	return new FLiteGPUSceneProxy(this, GetWorld()->FeatureLevel);
}
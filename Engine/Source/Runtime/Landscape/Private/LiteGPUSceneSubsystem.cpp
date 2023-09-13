#include "LiteGPUSceneSubsystem.h"
#include "RenderGraphBuilder.h"
#include "Components/LiteGPUSceneComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"

static int sDisplayLiteGPUScene = 1;
static FAutoConsoleVariableRef CVarDrawLiteGPUScene(
	TEXT("r.LiteGPUScene.Enable"),
	sDisplayLiteGPUScene,
	TEXT("Display GPUDriven Scene.\n"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static float sTileDistanceX = 1000.f;
static FAutoConsoleVariableRef CVarTileDistanceX(
	TEXT("r.LiteGPUScene.TileDistanceX"),
	sTileDistanceX,
	TEXT("X Distance between tiles.\n"),
	ECVF_ReadOnly
);

static float sTileDistanceY = 1000.f;
static FAutoConsoleVariableRef CVarTileDistanceY(
	TEXT("r.LiteGPUScene.TileDistanceY"),
	sTileDistanceY,
	TEXT("Y Distance between tiles.\n"),
	ECVF_ReadOnly
);

DEFINE_LOG_CATEGORY_STATIC(LogLiteGPUScene, Log, All)
static TMap<UWorld*, ALiteGPUSceneManager*> GLiteGPUSceneManagers;

void FLiteGPUSceneSubsystemTickFunction::ExecuteTick(float DeltaTime, enum ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	Manager->DoTick(DeltaTime, TickType, CurrentThread, MyCompletionGraphEvent);
}

FString FLiteGPUSceneSubsystemTickFunction::DiagnosticMessage()
{
	static const FString Message(TEXT("FLiteGPUSceneSubsystemTickFunction"));
	return Message;
}

FName FLiteGPUSceneSubsystemTickFunction::DiagnosticContext(bool bDetailed)
{
	static const FName Context(TEXT("FLiteGPUSceneSubsystemTickFunction"));
	return Context;
}

void ULiteGPUSceneSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("BuildLiteGPUScenes"),
		TEXT("build lite gpu scenes data."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
			{
				auto Subsystem = GEngine->GetEngineSubsystem<ULiteGPUSceneSubsystem>();
				Subsystem->BuildLiteGPUScenes();
			}),
		ECVF_Cheat);
}

void ULiteGPUSceneSubsystem::Deinitialize()
{

}

void ULiteGPUSceneSubsystem::BuildLiteGPUScenes()
{
	for (const auto& Iter : GLiteGPUSceneManagers)
	{
		Iter.Value->BuildLiteGPUScene();
	}
}

FLiteGPUSceneProxy::FLiteGPUSceneProxy(ULiteGPUSceneRenderComponent* Component, ERHIFeatureLevel::Type InFeatureLevel)
	: FPrimitiveSceneProxy(Component, TEXT("LiteGPUScene")),
	  Scene(Component->Scene)
{
	// Build up a stats of <mat_proxy, patch_ids> map
	for (auto& pair : Scene->CombinedData.SectionsMap)
	{
		int32 MappedMaterialIndex = pair.Key;
		UMaterialInterface* Mat = Scene->CombinedData.Materials[MappedMaterialIndex];
		if (Mat && Mat->GetRenderProxy())
		{
			FMaterialRenderProxy* MatProxy = Mat->GetRenderProxy();
			const TArray<FLiteGPUSceneMeshSectionInfo>& PatchInfoArray = pair.Value;
			for (const FLiteGPUSceneMeshSectionInfo& PatchInfo : PatchInfoArray)
			{
				int32 InsertPatchIndex = AllSections.Add(PatchInfo);
				TArray<int32>* pPatchIndiceArray = MaterialToSectionIDsMap.Find(MatProxy);
				if (pPatchIndiceArray)
				{
					pPatchIndiceArray->Add(InsertPatchIndex);
				}
				else
				{
					TArray<int32> PatchIndiceArray;
					PatchIndiceArray.Add(InsertPatchIndex);
					MaterialToSectionIDsMap.Add(MatProxy, PatchIndiceArray);
				}
			}
		}
	}

	pGPUDrivenVertexFactory = new FLiteGPUSceneVertexFactory(InFeatureLevel);

	pVFUserData = new FLiteGPUSceneVertexFactoryUserData();
	pVFUserData->SceneProxy = this;

	FLiteGPUSceneProxy* pLiteGPUSceneProxy = this;
	FSceneInterface* SceneInterface = Component->GetWorld()->Scene;
	ENQUEUE_RENDER_COMMAND(InitLiteGPUProxy)(
		[pLiteGPUSceneProxy, SceneInterface](FRHICommandList& RHICmdList)
		{
			pLiteGPUSceneProxy->Init_RenderingThread();
		}
	);
	BeginInitResource(pGPUDrivenVertexFactory);
}

FLiteGPUSceneProxy::~FLiteGPUSceneProxy()
{
	if (IsInRenderingThread())
	{
		if (nullptr != pGPUDrivenVertexFactory)
		{
			pGPUDrivenVertexFactory->ReleaseResource();
			delete pGPUDrivenVertexFactory;
			pGPUDrivenVertexFactory = nullptr;
		}

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
	Result.bRenderCustomDepth = ShouldRenderCustomDepth();
	Result.bDistortion = true;
	Result.bNormalTranslucency = true;
	Result.bSeparateTranslucency = true;
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
			if (Scene->BufferState.InstanceTransformBufferSRV != nullptr)
			{
				// DispatchCulling();
				// GenerateDrawcalls();
				DrawMeshBatches(ViewIndex, View, ViewFamily, Collector);
			}
		}
	}
	return;
}

void FLiteGPUSceneProxy::PostUpdateBeforePresent(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily)
{

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
	int32 IndirectDrawOffset = 0;
	for (auto& Var : MaterialToSectionIDsMap)
	{
		const TArray<int32>& SectionIndices = Var.Value;

		FMeshBatch& MeshBatch = Collector.AllocateMesh();
		MeshBatch.bWireframe = false;
		MeshBatch.VertexFactory = pGPUDrivenVertexFactory;
		MeshBatch.MaterialRenderProxy = Var.Key;
		MeshBatch.LODIndex = 0;
		MeshBatch.Type = PT_TriangleList;
		MeshBatch.DepthPriorityGroup = SDPG_World;
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		MeshBatch.VisualizeHLODIndex = 0;
#endif

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

		BatchElement.IndirectArgsBuffer = Scene->ViewBufferState.IndirectDrawBuffer->GetRHI();
		BatchElement.IndirectArgsOffset = IndirectDrawOffset * sizeof(LiteGPUSceneIndirectArguments);

		BatchElement.FirstInstance = 0;
		BatchElement.IndirectDrawCount = MaterialToSectionIDsMap.Num();
		BatchElement.IndirectBufferStride = sizeof(LiteGPUSceneIndirectArguments);

		IndirectDrawOffset += SectionIndices.Num();
		Collector.AddMesh(ViewIndex, MeshBatch);
	}
}

bool FLiteGPUSceneProxy::IsInitialized() const
{
	return Scene.IsValid();
}

void FLiteGPUSceneProxy::Init_RenderingThread()
{
	check(Scene->CombinedBuffer.bInitialized);
	UE_LOG(LogLiteGPUScene, Log, TEXT("Call FLiteGPUSceneProxy::Init_RenderingThread"));
	pGPUDrivenVertexFactory->Init_RenderThread(Scene->CombinedBuffer.VertexBuffer, nullptr);// TODO: Scene->InstanceIndicesBuffer);
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
	
	for (int32 MatIndex = 0; MatIndex < Scene->CombinedData.Materials.Num(); MatIndex++)
	{
		OutMaterials.Add(Scene->CombinedData.Materials[MatIndex]);
	}
}

FPrimitiveSceneProxy* ULiteGPUSceneRenderComponent::CreateSceneProxy()
{
	return new FLiteGPUSceneProxy(this, GetWorld()->FeatureLevel);
}

ALiteGPUSceneManager* ALiteGPUSceneManager::Get(const UObject* WorldContext)
{
	if (WorldContext)
	{
		if (auto pManager = GLiteGPUSceneManagers.Find(WorldContext->GetWorld()))
			return *pManager;
	}
	return nullptr;
}

void ALiteGPUSceneManager::PreRegisterAllComponents()
{
	Super::PreRegisterAllComponents();

	GLiteGPUSceneManagers.FindOrAdd(GetWorld(), this);
	// Register Tick 
	TickFunction.Manager = this;
	TickFunction.bCanEverTick = true;
	TickFunction.bTickEvenWhenPaused = true;
	TickFunction.bStartWithTickEnabled = true;
	TickFunction.TickGroup = TG_DuringPhysics;
	TickFunction.bAllowTickOnDedicatedServer = false;
	TickFunction.RegisterTickFunction(GetWorld()->PersistentLevel);
}

void ALiteGPUSceneManager::UnregisterAllComponents(bool bForReregister)
{
	if (!bForReregister)
	{
		GLiteGPUSceneManagers.Remove(GetWorld());
	}

	Super::UnregisterAllComponents(bForReregister);
}

void ALiteGPUSceneManager::OnAdd(TObjectPtr<ULiteGPUSceneComponent> Comp, const TArrayView<FLiteGPUSceneInstance> Instances)
{
	if (Scene.IsValid())
	{
		static auto CVarX = IConsoleManager::Get().FindConsoleVariable(TEXT("r.LiteGPUScene.SectorDistanceX"));
		static auto CVarY = IConsoleManager::Get().FindConsoleVariable(TEXT("r.LiteGPUScene.SectorDistanceY"));
		const float SectorDistanceX = CVarX ? CVarX->GetFloat() : 1000.0f;
		const float SectorDistanceY = CVarY ? CVarY->GetFloat() : 1000.0f;

		auto& SceneData = Scene->SceneData;
		const auto MeshIndex = SceneData.SourceMeshes.Find(Comp->StaticMesh);

		const auto OldInstanceNum = SceneData.InstanceNum;
		SceneData.InstanceNum += Instances.Num();
		SceneData.InstanceCapacity += Instances.Num();

		for (int32 IndexOffset = 0; IndexOffset < Instances.Num(); IndexOffset++)
		{
			const auto& ToAdd = Instances[IndexOffset];
			// ADD TO SECTOR
			const auto SectorX = FMath::FloorToInt64(ToAdd.Transform.GetLocation().X / SectorDistanceX);
			const auto SectorY = FMath::FloorToInt64(ToAdd.Transform.GetLocation().Y / SectorDistanceY);
			const auto SectorXY = FInt64Vector2(SectorX, SectorY);
			uint32 ThisSectorID = 0;
			if (auto pSectorID = SceneData.SectorIDMap.Find(SectorXY))
			{
				ThisSectorID = *pSectorID;
				SceneData.SectorInfos[*pSectorID].InstanceCount += 1;
			}
			else
			{
				ThisSectorID = SceneData.NextSectorID;
				SceneData.SectorIDMap.Add(SectorXY, ThisSectorID);
				SceneData.SectorInfos.Add({ SectorXY, 1 });
				SceneData.NextSectorID += 1;
			}

			// ADD INSTANCE TRANSFORMS
			const int32 NewInstanceIndex = IndexOffset + OldInstanceNum;
			SceneData.InstanceTransforms.Add(ToAdd.Transform);
			SceneData.InstanceSectorIDs.Add(ThisSectorID);
			SceneData.InstanceTypes.Add(MeshIndex);
			check(CIDToPID.FindOrAdd(Comp).Find(ToAdd.IDWithinComponent) == nullptr);
			CIDToPID.FindOrAdd(Comp).Add(ToAdd.IDWithinComponent, NewInstanceIndex);
		}

		int32 SectionIndex = 0;
		SceneData.InstanceSectionNums.SetNum(OldInstanceNum + Instances.Num());
		SceneData.InstanceSectionIDs.SetNum(SceneData.InstanceSectionNums.Num() * SceneData.PerSectionMaxNum);
		for (auto& SectionInfo : SceneData.SectionInfos)
		{
			for (int32 IndexOffset = 0; IndexOffset < Instances.Num(); IndexOffset++)
			{
				const int32 NewInstanceIndex = IndexOffset + OldInstanceNum;
				const int32 InstanceSectionStart = NewInstanceIndex * SceneData.PerSectionMaxNum;
				if (SectionInfo.MeshIndex == MeshIndex)
				{
					int32 InsertIndex = SceneData.InstanceSectionNums[NewInstanceIndex];
					SceneData.InstanceSectionNums[NewInstanceIndex] += 1;
					SceneData.SectionInstanceNums[SectionIndex] += 1;
					SceneData.InstanceSectionIDs[InstanceSectionStart + InsertIndex] = SectionIndex;
				}
			}
			SectionIndex++;
		}

		FLiteGPUSceneUpdate Update;
		Update.InstanceNum = Instances.Num();
		for (int32 Index = 0; Index < Update.InstanceNum; Index++)
		{
			Update.InstanceIndices.Add(Index + OldInstanceNum);
		}
		Scene->EnqueueUpdates_TS(MoveTemp(Update));
	}
}

void ALiteGPUSceneManager::OnRemove(TObjectPtr<ULiteGPUSceneComponent> Comp, const TArrayView<FLiteGPUSceneInstance> Instances)
{
	if (Scene.IsValid())
	{
		FLiteGPUSceneUpdate Update;

		auto& SceneData = Scene->SceneData;
		const auto MeshIndex = SceneData.SourceMeshes.Find(Comp->StaticMesh);

		auto TailNum = SceneData.InstanceNum;
		SceneData.InstanceNum -= Instances.Num();
		SceneData.InstanceCapacity -= Instances.Num();

		for (int32 IndexOffset = 0; IndexOffset < Instances.Num(); IndexOffset++)
		{
			const auto LastIndex = TailNum - 1;
			const auto RemoveIndex = CIDToPID.FindOrAdd(Comp)[Instances[IndexOffset].IDWithinComponent];
			if (LastIndex != RemoveIndex)
			{
				// REMOVE FROM SECTOR
				auto& SectorInfo = SceneData.SectorInfos[SceneData.InstanceSectorIDs[RemoveIndex]];
				SectorInfo.InstanceCount -= 1;

				// REMOTE INSTANCE DATA
				SceneData.InstanceTransforms[RemoveIndex] = SceneData.InstanceTransforms[LastIndex];
				SceneData.InstanceSectorIDs[RemoveIndex] = SceneData.InstanceSectorIDs[LastIndex];
				SceneData.InstanceTypes[RemoveIndex] = SceneData.InstanceTypes[LastIndex];

				SceneData.InstanceSectionNums[RemoveIndex] = SceneData.InstanceSectionNums[LastIndex];
				SceneData.InstanceSectionNums[LastIndex] = 0;

				for (int32 i = 0; i < SceneData.PerSectionMaxNum; i++)
				{
					SceneData.InstanceSectionIDs[RemoveIndex * SceneData.PerSectionMaxNum + i] = SceneData.InstanceSectionIDs[LastIndex * SceneData.PerSectionMaxNum + i];
					SceneData.InstanceSectionIDs[LastIndex * SceneData.PerSectionMaxNum + i] = 0;
				}

				Update.InstanceNum++;
				Update.InstanceIndices.Add(LastIndex);
			}
			TailNum--;
			CIDToPID.FindOrAdd(Comp).Remove(Instances[IndexOffset].IDWithinComponent);
		}

		SceneData.InstanceSectorIDs.SetNum(SceneData.InstanceNum);
		SceneData.InstanceTypes.SetNum(SceneData.InstanceNum);
		SceneData.InstanceSectionNums.SetNum(SceneData.InstanceNum);
		SceneData.InstanceSectionIDs.SetNum(SceneData.PerSectionMaxNum * SceneData.InstanceNum);

		for (int32 pIndex = 0; pIndex < SceneData.SectionInstanceNums.Num(); pIndex++)
		{
			if (SceneData.SectionInfos[pIndex].MeshIndex == MeshIndex)
			{
				SceneData.SectionInstanceNums[pIndex] -= Instances.Num();
				break;
			}
		}

		Scene->EnqueueUpdates_TS(MoveTemp(Update));
	}
}

void ALiteGPUSceneManager::BuildLiteGPUScene()
{
	const auto StartTime = FDateTime::UtcNow();
	Scene = MakeShared<FLiteGPUScene>();

	// Glob all components
	Components.Empty();
	for (TObjectIterator<ULiteGPUSceneComponent> Itr; Itr; ++Itr)
	{
		auto Component = *Itr;
		if (!Component->IsTemplate())
		{
			Components.AddUnique(*Itr);
		}
	}

	// Build combined vb/ib data here
	TArray<TObjectPtr<UStaticMesh>> Meshes;
	for (auto Component : Components)
	{
		Meshes.AddUnique(Component->GetUnderlyingMesh());
	}

	Scene->BuildScene(Meshes);

	ENQUEUE_RENDER_COMMAND(UpdateLiteGPUScene)(
		[this](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList);
			Scene->UpdateSectionInfos(GraphBuilder);
			Scene->UpdateAABBData(GraphBuilder);
			GraphBuilder.Execute();
		});

	const auto Elapsed = FDateTime::UtcNow() - StartTime;
	UE_LOG(LogLiteGPUScene, Log, TEXT("Lite GPU scene build finished in %d milliseconds"), Elapsed.GetTotalMilliseconds());

	RenderComp = NewObject<ULiteGPUSceneRenderComponent>(this, TEXT("LiteGPUSceneRenderComponent"));
	RenderComp->Owner = this;
	RenderComp->Scene = Scene;
	RenderComp->RegisterComponent();
}

void ALiteGPUSceneManager::DoTick(float DeltaTime, enum ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ALiteGPUSceneManager::Tick);

	static auto CVarEnable = IConsoleManager::Get().FindConsoleVariable(TEXT("r.LiteGPUScene.Enable"));
	bool bEnableLiteGPUScene = CVarEnable ? CVarEnable->GetInt() > 0 : false;
	if (bEnableLiteGPUScene)
	{
		for (auto Component : Components)
		{
			Component->Handler = this;
			Component->ManagedTick(DeltaTime);
		}		

		if (Scene.IsValid())
		{
			ENQUEUE_RENDER_COMMAND(UpdateLiteGPUScene)(
				[this](FRHICommandListImmediate& RHICmdList)
				{
					FRDGBuilder GraphBuilder(RHICmdList);
					Scene->UpdateInstanceData(GraphBuilder);
					GraphBuilder.Execute();
				});
		}
	}
}
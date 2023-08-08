#include "LiteGPUSceneSubsystem.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"

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
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*, FOutputDevice& Ar)
			{
				ULiteGPUSceneSubsystem::Get().BuildLiteGPUScenes();
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

void ALiteGPUSceneManager::BuildLiteGPUScene()
{
	const auto StartTime = FDateTime::UtcNow();

	HISMArray FoliageMeshComps;
	for (TObjectIterator<HISMComponent> Itr; Itr; ++Itr)
	{
		HISMComponent* HISM = *Itr;
		if (HISM->GetWorld() && HISM->GetOwner() && 
			HISM->IsVisible() && !HISM->IsPendingKill())
		{
			UStaticMesh* SrcMesh = HISM->GetStaticMesh();
			if (SrcMesh)
			{
				FoliageMeshComps.Add(HISM);
			}
		}
	}
	BuildHSIMComponents(FoliageMeshComps);

	const auto Elapsed = FDateTime::UtcNow() - StartTime;
	UE_LOG(LogLiteGPUScene, Log, TEXT("Lite GPU scene build finished in %d milliseconds"), Elapsed.GetTotalMilliseconds());
}

void ALiteGPUSceneManager::BuildHSIMComponents(HISMArray& InMeshes)
{
	TArray<UStaticMesh*> LiteMeshes;
	for (auto& HISM : InMeshes)
	{
		LiteMeshes.Add(HISM->GetStaticMesh());
	}
	UE_LOG(LogLiteGPUScene, Display, TEXT("LiteGPUScene: Static Meshes Num %d"), LiteMeshes.Num());

	if (LiteGPUSceneComp != nullptr)
	{
		LiteGPUSceneComp->DestroyComponent();
		LiteGPUSceneComp = nullptr;
	}

	if (!LiteGPUSceneComp)
	{
		LiteGPUSceneComp = NewObject<ULiteGPUSceneComponent>(this, TEXT("LiteGPUSceneComp"));
		LiteGPUSceneComp->CastShadow = true;
		LiteGPUSceneComp->RegisterComponent();
		LiteGPUSceneComp->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::SnapToTargetIncludingScale);

		LiteGPUSceneComp->BuildLiteGPUScene(LiteMeshes);
		LiteGPUSceneComp->GetOutermost()->MarkPackageDirty();
	}

	for (auto& HISM : InMeshes)
	{
		LiteGPUSceneComp->AddInstanceGroup(HISM);
	}
}

void ALiteGPUSceneManager::DoTick(float DeltaTime, enum ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ALiteGPUSceneManager::Tick);

	static auto CVarEnable = IConsoleManager::Get().FindConsoleVariable(TEXT("r.LiteGPUScene.Enable"));
	bool bEnableLiteGPUScene = CVarEnable ? CVarEnable->GetInt() > 0 : false;
	if (bEnableLiteGPUScene)
	{
		if (!LiteGPUSceneComp || !LiteGPUSceneComp->IsInitialized())
		{
			return;
		}
		if (LiteGPUSceneComp->SharedPerInstanceData.IsValid())
		{
			LiteGPUSceneComp->SharedPerInstanceData->DirtyInstanceNum = 0;
		}
		if (LiteGPUSceneComp)
		{
			LiteGPUSceneComp->UpdateIncreasedGroup();
			LiteGPUSceneComp->UpdateRemovedGroup();
			LiteGPUSceneComp->UpdateRenderThread();
		}
	}
}
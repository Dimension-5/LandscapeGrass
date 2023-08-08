#include "LiteGPUSceneSubsystem.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogLiteGPUScene, Log, All)
static TMap<UWorld*, ALiteGPUSceneManager*> GLiteGPUSceneManagers;

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
	TArray<HISMComponent*> LiteMeshes = InMeshes;
	UE_LOG(LogLiteGPUScene, Display, TEXT("LiteGPUScene: Static Meshes Num %d"), LiteMeshes.Num());


}
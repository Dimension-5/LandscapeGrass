#include "LiteGPUSceneSubsystem.h"
#include "Components/LiteGPUSceneComponent.h"
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

	// Build combined vb/ib data here
	// Scene->ConstructCombinedVertices(Components);

	const auto Elapsed = FDateTime::UtcNow() - StartTime;
	UE_LOG(LogLiteGPUScene, Log, TEXT("Lite GPU scene build finished in %d milliseconds"), Elapsed.GetTotalMilliseconds());
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
			Component->ManagedTick(DeltaTime);
		}
	}
}
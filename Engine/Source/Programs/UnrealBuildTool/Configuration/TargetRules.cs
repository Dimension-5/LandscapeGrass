// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.Serialization;
using EpicGames.Core;
using Microsoft.Extensions.Logging;
using UnrealBuildBase;

namespace UnrealBuildTool
{
	/// <summary>
	/// The type of target
	/// </summary>
	[Serializable]
	public enum TargetType
	{
		/// <summary>
		/// Cooked monolithic game executable (GameName.exe).  Also used for a game-agnostic engine executable (UnrealGame.exe or RocketGame.exe)
		/// </summary>
		Game,

		/// <summary>
		/// Uncooked modular editor executable and DLLs (UnrealEditor.exe, UnrealEditor*.dll, GameName*.dll)
		/// </summary>
		Editor,

		/// <summary>
		/// Cooked monolithic game client executable (GameNameClient.exe, but no server code)
		/// </summary>
		Client,

		/// <summary>
		/// Cooked monolithic game server executable (GameNameServer.exe, but no client code)
		/// </summary>
		Server,

		/// <summary>
		/// Program (standalone program, e.g. ShaderCompileWorker.exe, can be modular or monolithic depending on the program)
		/// </summary>
		Program
	}

	/// <summary>
	/// Specifies how to link all the modules in this target
	/// </summary>
	[Serializable]
	public enum TargetLinkType
	{
		/// <summary>
		/// Use the default link type based on the current target type
		/// </summary>
		Default,

		/// <summary>
		/// Link all modules into a single binary
		/// </summary>
		Monolithic,

		/// <summary>
		/// Link modules into individual dynamic libraries
		/// </summary>
		Modular,
	}

	/// <summary>
	/// Specifies whether to share engine binaries and intermediates with other projects, or to create project-specific versions. By default,
	/// editor builds always use the shared build environment (and engine binaries are written to Engine/Binaries/Platform), but monolithic builds
	/// and programs do not (except in installed builds). Using the shared build environment prevents target-specific modifications to the build
	/// environment.
	/// </summary>
	[Serializable]
	public enum TargetBuildEnvironment
	{
		/// <summary>
		/// Engine binaries and intermediates are output to the engine folder. Target-specific modifications to the engine build environment will be ignored.
		/// </summary>
		Shared,

		/// <summary>
		/// Engine binaries and intermediates are specific to this target
		/// </summary>
		Unique,
	}

	/// <summary>
	/// Specifies how UnrealHeaderTool should enforce member pointer declarations in UCLASSes and USTRUCTs.  This should match (by name, not value) the
	/// EPointerMemberBehavior enum in BaseParser.h so that it can be interpreted correctly by UnrealHeaderTool.
	/// </summary>
	[Serializable]
	public enum PointerMemberBehavior
	{
		/// <summary>
		/// Pointer members of the associated type will be disallowed and result in an error emitted from UnrealHeaderTool.
		/// </summary>
		Disallow,

		/// <summary>
		/// Pointer members of the associated type will be allowed and not emit any messages to log or screen.
		/// </summary>
		AllowSilently,

		/// <summary>
		/// Pointer members of the associated type will be allowed but will emit messages to log.
		/// </summary>
		AllowAndLog,
	}

	/// <summary>
	/// Determines which version of the engine to take default build settings from. This allows for backwards compatibility as new options are enabled by default.
	/// </summary>
	public enum BuildSettingsVersion
	{
		/// <summary>
		/// Legacy default build settings for 4.23 and earlier.
		/// </summary>
		V1,

		/// <summary>
		/// New defaults for 4.24:
		/// * ModuleRules.PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs
		/// * ModuleRules.bLegacyPublicIncludePaths = false
		/// </summary>
		V2,

		/// <summary>
		/// New defaults for 5.2:
		/// * ModuleRules.bLegacyParentIncludePaths = false
		/// </summary>
		V3,

		/// <summary>
		/// New defaults for 5.3:
		/// * TargetRules.CppStandard = CppStandard.Default has changed from Cpp17 to Cpp20
		/// * TargetRules.WindowsPlatform.bStrictConformanceMode = true
		/// </summary>
		V4,

		// *** When adding new entries here, be sure to update GameProjectUtils::GetDefaultBuildSettingsVersion() to ensure that new projects are created correctly. ***

		/// <summary>
		/// Always use the defaults for the current engine version. Note that this may cause compatibility issues when upgrading.
		/// </summary>
		Latest = V4,
	}

	/// <summary>
	/// What version of include order to use when compiling. This controls which UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_X defines are enabled when compiling the target.
	/// The UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_X defines are used when removing implicit includes from Unreal Public headers.
	/// Specific versions can be used by licensees to avoid compile errors when integrating new versions of Unreal.
	/// Using Latest comes with a high risk of introducing compile errors in your code on newer Unreal versions.
	/// </summary>
	public enum EngineIncludeOrderVersion
	{
		/// <summary>
		/// Include order used in Unreal 5.0
		/// </summary>
		[Obsolete("The Unreal 5.0 include order is deprecated and will be removed in 5.4.")]
		Unreal5_0,

		/// <summary>
		/// Include order used in Unreal 5.1
		/// </summary>
		[Obsolete("The Unreal 5.1 include order is deprecated and will be removed in 5.4.")]
		Unreal5_1,

		/// <summary>
		/// Include order used in Unreal 5.2
		/// </summary>
		Unreal5_2,

		/// <summary>
		/// Include order used in Unreal 5.3
		/// </summary>
		Unreal5_3,

		// *** When adding new entries here, be sure to update UEBuildModuleCPP.CurrentIncludeOrderDefine to ensure that the correct guard is used. ***

		/// <summary>
		/// Always use the latest version of include order. This value is updated every Unreal release, use with caution if you intend to integrate newer Unreal releases.
		/// </summary>
		Latest = Unreal5_3,

		/// <summary>
		/// Contains the oldest version of include order that the engine supports.
		/// </summary>
#pragma warning disable CS0618 // Type or member is obsolete
		Oldest = Unreal5_0,
#pragma warning restore CS0618 // Type or member is obsolete
	}

	/// <summary>
	/// Which static analyzer to use
	/// </summary>
	public enum StaticAnalyzer
	{
		/// <summary>
		/// Do not perform static analysis
		/// </summary>
		None,

		/// <summary>
		/// Use the default static analyzer for the selected compiler, if it has one. For
		/// Visual Studio and Clang, this means using their built-in static analysis tools.
		/// Any compiler that doesn't support static analysis will ignore this option.
		/// </summary>
		Default,

		/// <summary>
		/// Use the built-in Visual C++ static analyzer
		/// </summary>
		VisualCpp = Default,

		/// <summary>
		/// Use PVS-Studio for static analysis
		/// </summary>
		PVSStudio,

		/// <summary>
		/// Use clang for static analysis. This forces the compiler to clang.
		/// </summary>
		Clang,
	}

	/// <summary>
	/// Output type for the static analyzer. This currently only works for the Clang static analyzer.
	/// The Clang static analyzer can do either Text, which prints the analysis to stdout, or
	/// html, where it writes out a navigable HTML page for each issue that it finds, per file.
	/// The HTML is output in the same directory as the object file that would otherwise have
	/// been generated. 
	/// All other analyzers default automatically to Text. 
	/// </summary>
	public enum StaticAnalyzerOutputType
	{
		/// <summary>
		/// Output the analysis to stdout.
		/// </summary>
		Text,

		/// <summary>
		/// Output the analysis to an HTML file in the object folder.
		/// </summary>
		Html,
	}

	/// <summary>
	/// Output type for the static analyzer. This currently only works for the Clang static analyzer.
	/// The Clang static analyzer can do a shallow quick analysis. However the default deep is recommended.
	/// </summary>
	public enum StaticAnalyzerMode
	{
		/// <summary>
		/// Default deep analysis.
		/// </summary>
		Deep,

		/// <summary>
		/// Quick analysis. Not recommended.
		/// </summary>
		Shallow,
	}

	/// <summary>
	/// Optimization mode for compiler settings
	/// </summary>
	public enum OptimizationMode
	{
		/// <summary>
		/// Favor speed
		/// </summary>
		Speed,

		/// <summary>
		/// Favor minimal code size
		/// </summary>
		Size,

		/// <summary>
		/// Somewhere between Speed and Size
		/// </summary>
		SizeAndSpeed
	}

	/// <summary>
	/// Determines how the Gameplay Debugger plugin will be activated.
	/// </summary>
	public enum GameplayDebuggerOverrideState
	{
		/// <summary>
		/// Default => Not overriden, default usage behavior.
		/// </summary>
		Default,

		/// <summary>
		/// Core => WITH_GAMEPLAY_DEBUGGER = 0 and WITH_GAMEPLAY_DEBUGGER_CORE = 1
		/// </summary>
		Core,

		/// <summary>
		/// Full => WITH_GAMEPLAY_DEBUGGER = 1 and WITH_GAMEPLAY_DEBUGGER_CORE = 1
		/// </summary>
		Full
	}

	/// <summary>
	/// Utility class for EngineIncludeOrderVersion defines
	/// </summary>
	public class EngineIncludeOrderHelper
	{
		private static string GetDeprecationDefine(EngineIncludeOrderVersion InVersion)
		{
			switch (InVersion)
			{
				default:
				case EngineIncludeOrderVersion.Unreal5_3:
					return "UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_3";
				case EngineIncludeOrderVersion.Unreal5_2:
					return "UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2";
#pragma warning disable CS0618 // Type or member is obsolete
				case EngineIncludeOrderVersion.Unreal5_1:
					return "UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_1";
				case EngineIncludeOrderVersion.Unreal5_0:
					return "UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_0";
#pragma warning restore CS0618 // Type or member is obsolete
			}
		}

		private static string GetDeprecationDefine(EngineIncludeOrderVersion InTestVersion, EngineIncludeOrderVersion InVersion)
		{
			return GetDeprecationDefine(InTestVersion) + "=" + (InVersion < InTestVersion ? "1" : "0");
		}

		/// <summary>
		/// Returns a list of every deprecation define available.
		/// </summary>
		/// <returns></returns>
		public static List<string> GetAllDeprecationDefines()
		{
			return new List<string>()
			{
#pragma warning disable CS0618 // Type or member is obsolete
				GetDeprecationDefine(EngineIncludeOrderVersion.Unreal5_1),
#pragma warning restore CS0618 // Type or member is obsolete
				GetDeprecationDefine(EngineIncludeOrderVersion.Unreal5_2),
				GetDeprecationDefine(EngineIncludeOrderVersion.Unreal5_3),
			};
		}

		/// <summary>
		/// Get a list of every deprecation define and their value for the specified engine include order.
		/// </summary>
		/// <param name="InVersion"></param>
		/// <returns></returns>
		public static List<string> GetDeprecationDefines(EngineIncludeOrderVersion InVersion)
		{
			return new List<string>()
			{
#pragma warning disable CS0618 // Type or member is obsolete
				GetDeprecationDefine(EngineIncludeOrderVersion.Unreal5_1, InVersion),
#pragma warning restore CS0618 // Type or member is obsolete
				GetDeprecationDefine(EngineIncludeOrderVersion.Unreal5_2, InVersion),
				GetDeprecationDefine(EngineIncludeOrderVersion.Unreal5_3, InVersion),
			};
		}

		/// <summary>
		/// Returns the latest deprecation define.
		/// </summary>
		/// <returns></returns>
		public static string GetLatestDeprecationDefine()
		{
			return GetDeprecationDefine(EngineIncludeOrderVersion.Latest);
		}
	}

	/// <summary>
	/// Attribute used to mark fields which must match between targets in the shared build environment
	/// </summary>
	[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property, AllowMultiple = false)]
	class RequiresUniqueBuildEnvironmentAttribute : Attribute
	{
	}

	/// <summary>
	/// Attribute used to mark configurable sub-objects
	/// </summary>
	[AttributeUsage(AttributeTargets.Field, AllowMultiple = false)]
	class ConfigSubObjectAttribute : Attribute
	{
	}

	/// <summary>
	/// TargetRules is a data structure that contains the rules for defining a target (application/executable)
	/// </summary>
	public abstract partial class TargetRules
	{
		/// <summary>
		/// Static class wrapping constants aliasing the global TargetType enum.
		/// </summary>
		public static class TargetType
		{
			/// <summary>
			/// Alias for TargetType.Game
			/// </summary>
			public const global::UnrealBuildTool.TargetType Game = global::UnrealBuildTool.TargetType.Game;

			/// <summary>
			/// Alias for TargetType.Editor
			/// </summary>
			public const global::UnrealBuildTool.TargetType Editor = global::UnrealBuildTool.TargetType.Editor;

			/// <summary>
			/// Alias for TargetType.Client
			/// </summary>
			public const global::UnrealBuildTool.TargetType Client = global::UnrealBuildTool.TargetType.Client;

			/// <summary>
			/// Alias for TargetType.Server
			/// </summary>
			public const global::UnrealBuildTool.TargetType Server = global::UnrealBuildTool.TargetType.Server;

			/// <summary>
			/// Alias for TargetType.Program
			/// </summary>
			public const global::UnrealBuildTool.TargetType Program = global::UnrealBuildTool.TargetType.Program;
		}

		/// <summary>
		/// The name of this target
		/// </summary>
		public string Name
		{
			get
			{
				if (!String.IsNullOrEmpty(NameOverride))
				{
					return NameOverride;
				}

				return DefaultName;
			}
			set => NameOverride = value;
		}

		/// <summary>
		/// If the Name of this target has been overriden
		/// </summary>
		public bool IsNameOverriden() { return !String.IsNullOrEmpty(NameOverride); }

		/// <summary>
		/// Return the original target name without overrides or adornments
		/// </summary>
		public string OriginalName => DefaultName;

		/// <summary>
		/// Override the name used for this target
		/// </summary>
		[CommandLine("-TargetNameOverride=")]
		private string? NameOverride;

		private readonly string DefaultName;

		/// <summary>
		/// Whether this is a low level tests target.
		/// </summary>
		public bool IsTestTarget => bIsTestTargetOverride;
		/// <summary>
		/// Override this boolean flag in inheriting classes for low level test targets.
		/// </summary>
		protected bool bIsTestTargetOverride;

		/// <summary>
		/// Whether this is a test target explicitly defined.
		/// Explicitley defined test targets always inherit from TestTargetRules and define their own tests.
		/// Implicit test targets are created from existing targets when building with -Mode=Test and they include tests from all dependencies.
		/// </summary>
		public bool ExplicitTestsTarget => GetType().IsSubclassOf(typeof(TestTargetRules));

		/// <summary>
		/// Controls the value of WITH_LOW_LEVEL_TESTS that dictates whether module-specific low level tests are compiled in or not.
		/// </summary>
		public bool WithLowLevelTests => (IsTestTarget && !ExplicitTestsTarget) || bWithLowLevelTestsOverride;
		/// <summary>
		/// Override the value of WithLowLevelTests by setting this to true in inherited classes.
		/// </summary>
		protected bool bWithLowLevelTestsOverride;

		/// <summary>
		/// File containing the general type for this target (not including platform/group)
		/// </summary>
		internal FileReference? File { get; set; }

		/// <summary>
		/// File containing the platform/group-specific type for this target
		/// </summary>
		internal FileReference? TargetSourceFile { get; set; }

		/// <summary>
		/// All target files that could be referenced by this target and will require updating the Makefile if changed
		/// </summary>
		internal HashSet<FileReference>? TargetFiles { get; set; }

		/// <summary>
		/// Logger for output relating to this target. Set before the constructor is run from <see cref="RulesCompiler"/>
		/// </summary>
		public ILogger Logger { get; internal set; }

		/// <summary>
		/// Generic nullable object so a user can set additional data in a project's TargetRule and access in a project's ModuleRule without needing to add new properties post-release.
		/// For example:
		/// * in .Target.cs:	AdditionalData = "data";
		/// * in .Build.cs:		if ((Target.AdditionalData as string) == "data") { ... }
		/// </summary>
		public object? AdditionalData { get; set; }

		/// <summary>
		/// Platform that this target is being built for.
		/// </summary>
		public readonly UnrealTargetPlatform Platform;

		/// <summary>
		/// The configuration being built.
		/// </summary>
		public readonly UnrealTargetConfiguration Configuration;

		/// <summary>
		/// Architecture that the target is being built for (or an empty string for the default).
		/// </summary>
		public readonly UnrealArchitectures Architectures;

		/// <summary>
		/// Gets the Architecture in the normal case where there is a single Architecture in Architectures
		/// (this will throw an exception if there is more than one architecture specified)
		/// </summary>
		public UnrealArch Architecture => Architectures.SingleArchitecture;

		/// <summary>
		/// Intermediate environment. Determines if the intermediates end up in a different folder than normal.
		/// </summary>
		public UnrealIntermediateEnvironment IntermediateEnvironment;

		/// <summary>
		/// Path to the project file for the project containing this target.
		/// </summary>
		public readonly FileReference? ProjectFile;

		/// <summary>
		/// The current build version
		/// </summary>
		public readonly ReadOnlyBuildVersion Version;

		/// <summary>
		/// The type of target.
		/// </summary>
		public global::UnrealBuildTool.TargetType Type = global::UnrealBuildTool.TargetType.Game;

		/// <summary>
		/// Specifies the engine version to maintain backwards-compatible default build settings with (eg. DefaultSettingsVersion.Release_4_23, DefaultSettingsVersion.Release_4_24). Specify DefaultSettingsVersion.Latest to always
		/// use defaults for the current engine version, at the risk of introducing build errors while upgrading.
		/// </summary>
		public BuildSettingsVersion DefaultBuildSettings
		{
			get => DefaultBuildSettingsPrivate ?? BuildSettingsVersion.V1;
			set => DefaultBuildSettingsPrivate = value;
		}
		private BuildSettingsVersion? DefaultBuildSettingsPrivate; // Cannot be initialized inline; potentially overridden before the constructor is called.

		/// <summary>
		/// Force the include order to a specific version. Overrides any Target and Module rules.
		/// </summary>
		[CommandLine("-ForceIncludeOrder=")]
		public EngineIncludeOrderVersion? ForcedIncludeOrder = null;

		/// <summary>
		/// What version of include order to use when compiling this target. Can be overridden via -ForceIncludeOrder on the command line. ModuleRules.IncludeOrderVersion takes precedence.
		/// </summary>
		public EngineIncludeOrderVersion IncludeOrderVersion
		{
			get
			{
				if (ForcedIncludeOrder != null)
				{
					return ForcedIncludeOrder.Value;
				}
				return IncludeOrderVersionPrivate ?? EngineIncludeOrderVersion.Oldest;
			}
			set => IncludeOrderVersionPrivate = value;
		}
		private EngineIncludeOrderVersion? IncludeOrderVersionPrivate;

		/// <summary>
		/// Path to the output file for the main executable, relative to the Engine or project directory.
		/// This setting is only typically useful for non-UE programs, since the engine uses paths relative to the executable to find other known folders (eg. Content).
		/// </summary>
		public string? OutputFile;

		/// <summary>
		/// Tracks a list of config values read while constructing this target
		/// </summary>
		internal readonly ConfigValueTracker ConfigValueTracker;

		/// <summary>
		/// Whether the target uses Steam.
		/// </summary>
		public bool bUsesSteam;

		/// <summary>
		/// Whether the target uses CEF3.
		/// </summary>
		public bool bUsesCEF3;

		/// <summary>
		/// Whether the project uses visual Slate UI (as opposed to the low level windowing/messaging, which is always available).
		/// </summary>
		public bool bUsesSlate = true;

		/// <summary>
		/// Forces linking against the static CRT. This is not fully supported across the engine due to the need for allocator implementations to be shared (for example), and TPS
		/// libraries to be consistent with each other, but can be used for utility programs.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public bool bUseStaticCRT = false;

		/// <summary>
		/// Enables the debug C++ runtime (CRT) for debug builds. By default we always use the release runtime, since the debug
		/// version isn't particularly useful when debugging Unreal Engine projects, and linking against the debug CRT libraries forces
		/// our third party library dependencies to also be compiled using the debug CRT (and often perform more slowly). Often
		/// it can be inconvenient to require a separate copy of the debug versions of third party static libraries simply
		/// so that you can debug your program's code.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bDebugBuildsActuallyUseDebugCRT = false;

		/// <summary>
		/// Whether the output from this target can be publicly distributed, even if it has dependencies on modules that are in folders
		/// with special restrictions (eg. CarefullyRedist, NotForLicensees, NoRedist).
		/// </summary>
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bLegalToDistributeBinary = false;

		/// <summary>
		/// Specifies the configuration whose binaries do not require a "-Platform-Configuration" suffix.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public UnrealTargetConfiguration UndecoratedConfiguration = UnrealTargetConfiguration.Development;

		/// <summary>
		/// Whether this target supports hot reload
		/// </summary>
		public bool bAllowHotReload
		{
			get => bAllowHotReloadOverride ?? (Type == TargetType.Editor && LinkType == TargetLinkType.Modular);
			set => bAllowHotReloadOverride = value;
		}
		private bool? bAllowHotReloadOverride;

		/// <summary>
		/// Build all the modules that are valid for this target type. Used for CIS and making installed engine builds.
		/// </summary>
		[CommandLine("-AllModules")]
		public bool bBuildAllModules = false;

		/// <summary>
		/// Set this to reference a VSTest run settings file from generated projects.
		/// </summary>
		public FileReference? VSTestRunSettingsFile;

		/// <summary>
		/// Additional plugins that are built for this target type but not enabled.
		/// </summary>
		[CommandLine("-BuildPlugin=", ListSeparator = '+')]
		public List<string> BuildPlugins = new List<string>();

		/// <summary>
		/// If this is true, then the BuildPlugins list will be used to populate RuntimeDependencies, rather than EnablePlugins
		/// </summary>
		public bool bRuntimeDependenciesComeFromBuildPlugins = false;

		/// <summary>
		/// A list of additional plugins which need to be included in this target. This allows referencing non-optional plugin modules
		/// which cannot be disabled, and allows building against specific modules in program targets which do not fit the categories
		/// in ModuleHostType.
		/// </summary>
		public List<string> AdditionalPlugins = new List<string>();

		/// <summary>
		/// Additional plugins that should be included for this target.
		/// </summary>
		[CommandLine("-EnablePlugin=", ListSeparator = '+')]
		public List<string> EnablePlugins = new List<string>();

		/// <summary>
		/// List of plugins to be disabled for this target. Note that the project file may still reference them, so they should be marked
		/// as optional to avoid failing to find them at runtime.
		/// </summary>
		[CommandLine("-DisablePlugin=", ListSeparator = '+')]
		public List<string> DisablePlugins = new List<string>();

		/// <summary>
		/// How to treat conflicts when a disabled plugin is being enabled by another plugin referencing it
		/// </summary>
		public WarningLevel DisablePluginsConflictWarningLevel = WarningLevel.Default;

		/// <summary>
		/// A list of Plugin names that are allowed to exist as dependencies without being defined in the uplugin descriptor
		/// </summary>
		public List<string> InternalPluginDependencies = new List<string>();

		/// <summary>
		/// Path to the set of pak signing keys to embed in the executable.
		/// </summary>
		public string PakSigningKeysFile = "";

		/// <summary>
		/// Allows a Program Target to specify it's own solution folder path.
		/// </summary>
		public string SolutionDirectory = String.Empty;

		/// <summary>
		/// Whether the target should be included in the default solution build configuration
		/// Setting this to false will skip building when running in the IDE
		/// </summary>
		public bool? bBuildInSolutionByDefault = null;

		/// <summary>
		/// Whether this target should be compiled as a DLL.  Requires LinkType to be set to TargetLinkType.Monolithic.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		[CommandLine("-CompileAsDll")]
		public bool bShouldCompileAsDLL = false;

		/// <summary>
		/// Extra subdirectory to load config files out of, for making multiple types of builds with the same platform
		/// This will get baked into the game executable as CUSTOM_CONFIG and used when staging to filter files and settings
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public string CustomConfig = String.Empty;

		/// <summary>
		/// Subfolder to place executables in, relative to the default location.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public string ExeBinariesSubFolder = String.Empty;

		/// <summary>
		/// Allow target module to override UHT code generation version.
		/// </summary>
		public EGeneratedCodeVersion GeneratedCodeVersion = EGeneratedCodeVersion.None;

		/// <summary>
		/// Whether to enable the mesh editor.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public bool bEnableMeshEditor = false;

		/// <summary>
		/// Whether to use the verse script interface.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		[CommandLine("-NoUseVerse", Value = "false")]
		[CommandLine("-UseVerse", Value = "true")]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bUseVerse = true;

		/// <summary>
		/// Whether to use the AutoRTFM Clang compiler.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		[CommandLine("-NoUseAutoRTFM", Value = "false")]
		[CommandLine("-UseAutoRTFM", Value = "true")]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bUseAutoRTFMCompiler = false;

		/// <summary>
		/// Whether to compile the Chaos physics plugin.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		[CommandLine("-NoCompileChaos", Value = "false")]
		[CommandLine("-CompileChaos", Value = "true")]
		[Obsolete("Deprecated in UE5.1 - No longer used as Chaos is always enabled.")]
		public bool bCompileChaos = true;

		/// <summary>
		/// Whether to use the Chaos physics interface. This overrides the physx flags to disable APEX and NvCloth
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		[CommandLine("-NoUseChaos", Value = "false")]
		[CommandLine("-UseChaos", Value = "true")]
		[Obsolete("Deprecated in UE5.1 - No longer used as Chaos is always enabled.")]
		public bool bUseChaos = true;

		/// <summary>
		/// Whether to compile in checked chaos features for debugging
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public bool bUseChaosChecked = false;

		/// <summary>
		/// Whether to compile in chaos memory tracking features
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public bool bUseChaosMemoryTracking = false;

		/// <summary>
		/// Whether to compile in Chaos Visual Debugger (CVD) support features to record the state of the physics simulation
		/// </summary>
		[XmlConfigFile(Name = "bCompileChaosVisualDebuggerSupport")]
		[CommandLine("-CompileChaosVisualDebuggerSupport")]
		[RequiresUniqueBuildEnvironment]
		public bool bCompileChaosVisualDebuggerSupport = false;

		/// <summary>
		/// Whether scene query acceleration is done by UE. The physx scene query structure is still created, but we do not use it.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		[Obsolete("Deprecated in UE5.1 - No longer used in engine.")]
		public bool bCustomSceneQueryStructure = false;

		/// <summary>
		/// Whether to include PhysX support.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		[Obsolete("Deprecated in UE5.1 - No longer used as Chaos is always enabled.")]
		public bool bCompilePhysX = false;

		/// <summary>
		/// Whether to include PhysX APEX support.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		[ConfigFile(ConfigHierarchyType.Engine, "/Script/BuildSettings.BuildSettings", "bCompileApex")]
		[Obsolete("Deprecated in UE5.1 - No longer used as Chaos is always enabled.")]
		public bool bCompileAPEX = false;

		/// <summary>
		/// Whether to include NvCloth.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		[Obsolete("Deprecated in UE5.1 - No longer used as Chaos is always enabled.")]
		public bool bCompileNvCloth = false;

		/// <summary>
		/// Whether to include ICU unicode/i18n support in Core.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		[ConfigFile(ConfigHierarchyType.Engine, "/Script/BuildSettings.BuildSettings", "bCompileICU")]
		public bool bCompileICU = true;

		/// <summary>
		/// Whether to compile CEF3 support.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		[ConfigFile(ConfigHierarchyType.Engine, "/Script/BuildSettings.BuildSettings", "bCompileCEF3")]
		public bool bCompileCEF3 = true;

		/// <summary>
		/// Whether to compile using ISPC.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public bool bCompileISPC = false;

		/// <summary>
		/// Whether to compile IntelMetricsDiscovery.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public bool bCompileIntelMetricsDiscovery = true;

		/// <summary>
		/// Whether to compile in python support
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public bool bCompilePython = true;

		/// <summary>
		/// Whether to compile with WITH_GAMEPLAY_DEBUGGER enabled with all Engine's default gameplay debugger categories.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public bool bUseGameplayDebugger
		{
			get
			{
				if (UseGameplayDebuggerOverride == GameplayDebuggerOverrideState.Default)
				{
					return (bBuildDeveloperTools || (Configuration != UnrealTargetConfiguration.Test && Configuration != UnrealTargetConfiguration.Shipping));
				}
				else
				{
					return UseGameplayDebuggerOverride == GameplayDebuggerOverrideState.Full;
				}
			}
			set
			{
				if (value)
				{
					UseGameplayDebuggerOverride = GameplayDebuggerOverrideState.Full;
				}
				else if (UseGameplayDebuggerOverride != GameplayDebuggerOverrideState.Core)
				{
					UseGameplayDebuggerOverride = GameplayDebuggerOverrideState.Default;
				}
			}
		}

		/// <summary>
		/// Set to true when bUseGameplayDebugger is false but GameplayDebugger's core parts are required.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public bool bUseGameplayDebuggerCore
		{
			get => (UseGameplayDebuggerOverride == GameplayDebuggerOverrideState.Core) || bUseGameplayDebugger;
			set
			{
				if (UseGameplayDebuggerOverride != GameplayDebuggerOverrideState.Full)
				{
					UseGameplayDebuggerOverride = value ? GameplayDebuggerOverrideState.Core : GameplayDebuggerOverrideState.Default;
				}
			}
		}
		GameplayDebuggerOverrideState UseGameplayDebuggerOverride;

		/// <summary>
		/// Whether to use Iris.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		[CommandLine("-NoUseIris", Value = "false")]
		[CommandLine("-UseIris", Value = "true")]
		public bool bUseIris = true;

		/// <summary>
		/// Whether we are compiling editor code or not. Prefer the more explicit bCompileAgainstEditor instead.
		/// </summary>
		public bool bBuildEditor
		{
			get => (Type == TargetType.Editor || bCompileAgainstEditor);
			set => Logger.LogWarning("Setting {Type}.bBuildEditor is deprecated. Set {Type}.Type instead.", GetType().Name);
		}

		/// <summary>
		/// Whether to compile code related to building assets. Consoles generally cannot build assets. Desktop platforms generally can.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public bool bBuildRequiresCookedData
		{
			get => bBuildRequiresCookedDataOverride ?? (Type == TargetType.Game || Type == TargetType.Client || Type == TargetType.Server);
			set => bBuildRequiresCookedDataOverride = value;
		}
		bool? bBuildRequiresCookedDataOverride;

		/// <summary>
		/// Whether to compile WITH_EDITORONLY_DATA disabled. Only Windows will use this, other platforms force this to false.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public bool bBuildWithEditorOnlyData
		{
			get => bBuildWithEditorOnlyDataOverride ?? (Type == TargetType.Editor || Type == TargetType.Program);
			set => bBuildWithEditorOnlyDataOverride = value;
		}
		private bool? bBuildWithEditorOnlyDataOverride;

		/// <summary>
		/// Manually specified value for bBuildDeveloperTools.
		/// </summary>
		bool? bBuildDeveloperToolsOverride;

		/// <summary>
		/// Whether to compile the developer tools.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public bool bBuildDeveloperTools
		{
			set => bBuildDeveloperToolsOverride = value;
			get => bBuildDeveloperToolsOverride ?? (bCompileAgainstEngine && (Type == TargetType.Editor || Type == TargetType.Program || (Configuration != UnrealTargetConfiguration.Test && Configuration != UnrealTargetConfiguration.Shipping)));
		}

		/// <summary>
		/// Manually specified value for bBuildTargetDeveloperTools.
		/// </summary>
		bool? bBuildTargetDeveloperToolsOverride;

		/// <summary>
		/// Whether to compile the developer tools that are for target platforms or connected devices (defaults to bBuildDeveloperTools)
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public bool bBuildTargetDeveloperTools
		{
			set => bBuildTargetDeveloperToolsOverride = value;
			get => bBuildTargetDeveloperToolsOverride ?? bBuildDeveloperTools;
		}

		/// <summary>
		/// Whether to force compiling the target platform modules, even if they wouldn't normally be built.
		/// </summary>
		public bool bForceBuildTargetPlatforms = false;

		/// <summary>
		/// Whether to force compiling shader format modules, even if they wouldn't normally be built.
		/// </summary>
		public bool bForceBuildShaderFormats = false;

		/// <summary>
		/// Override for including extra shader formats
		/// </summary>
		public bool? bNeedsExtraShaderFormatsOverride;

		/// <summary>
		/// Whether we should include any extra shader formats. By default this is only enabled for Program and Editor targets.
		/// </summary>
		public bool bNeedsExtraShaderFormats
		{
			set => bNeedsExtraShaderFormatsOverride = value;
			get => bNeedsExtraShaderFormatsOverride ?? (bForceBuildShaderFormats || bBuildDeveloperTools) && (Type == TargetType.Editor || Type == TargetType.Program);
		}

		/// <summary>
		/// Whether we should compile SQLite using the custom "Unreal" platform (true), or using the native platform (false).
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		[ConfigFile(ConfigHierarchyType.Engine, "/Script/BuildSettings.BuildSettings", "bCompileCustomSQLitePlatform")]
		public bool bCompileCustomSQLitePlatform = true;

		/// <summary>
		/// Whether to utilize cache freed OS allocs with MallocBinned
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		[ConfigFile(ConfigHierarchyType.Engine, "/Script/BuildSettings.BuildSettings", "bUseCacheFreedOSAllocs")]
		public bool bUseCacheFreedOSAllocs = true;

		/// <summary>
		/// Enabled for all builds that include the engine project.  Disabled only when building standalone apps that only link with Core.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public virtual bool bCompileAgainstEngine
		{
			get => bCompileAgainstEnginePrivate;
			set => bCompileAgainstEnginePrivate = value;
		}
		private bool bCompileAgainstEnginePrivate = true;

		/// <summary>
		/// Enabled for all builds that include the CoreUObject project.  Disabled only when building standalone apps that only link with Core.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public virtual bool bCompileAgainstCoreUObject
		{
			get => bCompileAgainstCoreUObjectPrivate;
			set => bCompileAgainstCoreUObjectPrivate = value;
		}
		private bool bCompileAgainstCoreUObjectPrivate = true;

		/// <summary>
		/// Enabled for builds that need to initialize the ApplicationCore module. Command line utilities do not normally need this.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public virtual bool bCompileAgainstApplicationCore
		{
			get => bCompileAgainstApplicationCorePrivate;
			set => bCompileAgainstApplicationCorePrivate = value;
		}
		private bool bCompileAgainstApplicationCorePrivate = true;

		/// <summary>
		/// Manually specified value for bCompileAgainstEditor.
		/// </summary>
		bool? bCompileAgainstEditorOverride;

		/// <summary>
		/// Enabled for editor builds (TargetType.Editor). Can be overridden for programs (TargetType.Program) that would need to compile against editor code. Not available for other target types.
		/// Mainly drives the value of WITH_EDITOR.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public virtual bool bCompileAgainstEditor
		{
			set => bCompileAgainstEditorOverride = value;
			get => bCompileAgainstEditorOverride ?? (Type == TargetType.Editor);
		}

		/// <summary>
		/// Whether to compile Recast navmesh generation.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		[ConfigFile(ConfigHierarchyType.Engine, "/Script/BuildSettings.BuildSettings", "bCompileRecast")]
		public bool bCompileRecast = true;

		/// <summary>
		/// Whether to compile with navmesh segment links.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public bool bCompileNavmeshSegmentLinks = true;

		/// <summary>
		/// Whether to compile with navmesh cluster links.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public bool bCompileNavmeshClusterLinks = true;

		/// <summary>
		/// Whether to compile SpeedTree support.
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Engine, "/Script/BuildSettings.BuildSettings", "bCompileSpeedTree")]
		bool? bOverrideCompileSpeedTree;

		/// <summary>
		/// Whether we should compile in support for SpeedTree or not.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public bool bCompileSpeedTree
		{
			set => bOverrideCompileSpeedTree = value;
			get => bOverrideCompileSpeedTree ?? Type == TargetType.Editor;
		}

		/// <summary>
		/// Enable exceptions for all modules.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public bool bForceEnableExceptions = false;

		/// <summary>
		/// Enable inlining for all modules.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bUseInlining = true;

		/// <summary>
		/// Enable exceptions for all modules.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public bool bForceEnableObjCExceptions = false;

		/// <summary>
		/// Enable RTTI for all modules.
		/// </summary>
		[CommandLine("-rtti")]
		[RequiresUniqueBuildEnvironment]
		public bool bForceEnableRTTI = false;

		/// <summary>
		/// Enable Position Independent Executable (PIE). Has an overhead cost
		/// </summary>
		[CommandLine("-pie")]
		public bool bEnablePIE = false;

		/// <summary>
		/// Enable Stack Protection. Has an overhead cost
		/// </summary>
		[CommandLine("-stack-protect")]
		public bool bEnableStackProtection = false;

		/// <summary>
		/// Compile server-only code.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public bool bWithServerCode
		{
			get => bWithServerCodeOverride ?? (Type != TargetType.Client);
			set => bWithServerCodeOverride = value;
		}
		private bool? bWithServerCodeOverride;

		/// <summary>
		/// Compile with FName storing the number part in the name table. 
		/// Saves memory when most names are not numbered and those that are are referenced multiple times.
		/// The game and engine must ensure they reuse numbered names similarly to name strings to avoid leaking memory.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public bool bFNameOutlineNumber
		{
			get => bFNameOutlineNumberOverride ?? false;
			set => bFNameOutlineNumberOverride = value;
		}
		private bool? bFNameOutlineNumberOverride;

		/// <summary>
		/// When enabled, Push Model Networking support will be compiled in.
		/// This can help reduce CPU overhead of networking, at the cost of more memory.
		/// Always enabled in editor builds.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public bool bWithPushModel
		{
			get => bWithPushModelOverride ?? (Type == TargetType.Editor);
			set => bWithPushModelOverride = value;
		}
		private bool? bWithPushModelOverride;

		/// <summary>
		/// Whether to include stats support even without the engine.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public bool bCompileWithStatsWithoutEngine = false;

		/// <summary>
		/// Whether to include plugin support.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		[ConfigFile(ConfigHierarchyType.Engine, "/Script/BuildSettings.BuildSettings", "bCompileWithPluginSupport")]
		public bool bCompileWithPluginSupport = false;

		/// <summary>
		/// Whether to allow plugins which support all target platforms.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public bool bIncludePluginsForTargetPlatforms
		{
			get => bIncludePluginsForTargetPlatformsOverride ?? (Type == TargetType.Editor);
			set => bIncludePluginsForTargetPlatformsOverride = value;
		}
		private bool? bIncludePluginsForTargetPlatformsOverride;

		/// <summary>
		/// Whether to allow accessibility code in both Slate and the OS layer.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public bool bCompileWithAccessibilitySupport = true;

		/// <summary>
		/// Whether to include PerfCounters support.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public bool bWithPerfCounters
		{
			get => bWithPerfCountersOverride ?? (Type == TargetType.Editor || Type == TargetType.Server);
			set => bWithPerfCountersOverride = value;
		}

		[ConfigFile(ConfigHierarchyType.Engine, "/Script/BuildSettings.BuildSettings", "bWithPerfCounters")]
		bool? bWithPerfCountersOverride;

		/// <summary>
		/// Whether to enable support for live coding
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public bool bWithLiveCoding
		{
			get => bWithLiveCodingPrivate ?? (Platform == UnrealTargetPlatform.Win64 && Architecture.bIsX64 && Configuration != UnrealTargetConfiguration.Shipping && Configuration != UnrealTargetConfiguration.Test && Type != TargetType.Program);
			set => bWithLiveCodingPrivate = value;
		}
		bool? bWithLiveCodingPrivate;

		/// <summary>
		/// Whether to enable support for live coding
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bUseDebugLiveCodingConsole = false;

		/// <summary>
		/// Whether to enable support for DirectX Math
		/// LWC_TODO: No longer supported. Needs removing.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public bool bWithDirectXMath = false;

		/// <summary>
		/// Whether to enable the support for FixedTimeStep in the engine
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public bool bWithFixedTimeStepSupport = true;

		/// <summary>
		/// Whether to turn on logging for test/shipping builds.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public bool bUseLoggingInShipping = false;

		/// <summary>
		/// Whether to turn on logging to memory for test/shipping builds.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public bool bLoggingToMemoryEnabled;

		/// <summary>
		/// Whether to check that the process was launched through an external launcher.
		/// </summary>
		public bool bUseLauncherChecks = false;

		/// <summary>
		/// Whether to turn on checks (asserts) for test/shipping builds.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public bool bUseChecksInShipping = false;

		/// <summary>
		/// Whether to turn on GPU markers for Test builds.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public bool bAllowProfileGPUInTest = false;

		/// <summary>
		/// Whether to turn on GPU markers for Shipping builds.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public bool bAllowProfileGPUInShipping = false;

		/// <summary>
		/// Whether to turn on UTF-8 mode, mapping TCHAR to UTF8CHAR.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public bool bTCHARIsUTF8 = false;

		/// <summary>
		/// Whether to use the EstimatedUtcNow or PlatformUtcNow.  EstimatedUtcNow is appropriate in
		/// cases where PlatformUtcNow can be slow.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public bool bUseEstimatedUtcNow = false;

		/// <summary>
		/// True if we need FreeType support.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		[ConfigFile(ConfigHierarchyType.Engine, "/Script/BuildSettings.BuildSettings", "bCompileFreeType")]
		public bool bCompileFreeType = true;

		/// <summary>
		/// Whether to turn allow exec commands for shipping builds.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public bool bUseExecCommandsInShipping = true;

		/// <summary>
		/// True if we want to favor optimizing size over speed.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		[Obsolete("Deprecated in UE5.1 - Please use OptimizationLevel instead.")]
		[ConfigFile(ConfigHierarchyType.Engine, "/Script/BuildSettings.BuildSettings", "bCompileForSize")]
		public bool bCompileForSize = false;

		/// <summary>
		/// Allows to fine tune optimizations level for speed and\or code size
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		[CommandLine("-OptimizationLevel=")]
		public OptimizationMode OptimizationLevel = OptimizationMode.Speed;

		/// <summary>
		/// Whether to compile development automation tests.
		/// </summary>
		public bool bForceCompileDevelopmentAutomationTests = false;

		/// <summary>
		/// Whether to compile performance automation tests.
		/// </summary>
		public bool bForceCompilePerformanceAutomationTests = false;

		/// <summary>
		/// Whether to override the defaults for automation tests (Debug/Development configs)
		/// </summary>
		public bool bForceDisableAutomationTests = false;

		/// <summary>
		/// If true, event driven loader will be used in cooked builds. @todoio This needs to be replaced by a runtime solution after async loading refactor.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public bool bEventDrivenLoader;

		/// <summary>
		/// Used to override the behavior controlling whether UCLASSes and USTRUCTs are allowed to have native pointer members, if disallowed they will be a UHT error and should be substituted with TObjectPtr members instead.
		/// </summary>
		public PointerMemberBehavior? NativePointerMemberBehaviorOverride = null;

		/// <summary>
		/// Whether the XGE controller worker and modules should be included in the engine build.
		/// These are required for distributed shader compilation using the XGE interception interface.
		/// </summary>
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bUseXGEController = true;

		/// <summary>
		/// Enables "include what you use" by default for modules in this target. Changes the default PCH mode for any module in this project to PCHUsageMode.UseExplicitOrSharedPCHs.
		/// </summary>
		public bool bIWYU { get => IntermediateEnvironment == UnrealIntermediateEnvironment.IWYU; }

		/// <summary>
		/// Adds header files in included modules to the build.
		/// </summary>
		[CommandLine("-IncludeHeaders")]
		public bool bIncludeHeaders
		{
			get => bIncludeHeadersPrivate || bIWYU;
			set => bIncludeHeadersPrivate = value;
		}
		private bool bIncludeHeadersPrivate = false;

		/// <summary>
		/// When used with -IncludeHeaders, only header files will be compiled.
		/// </summary>
		[CommandLine("-HeadersOnly")]
		public bool bHeadersOnly = false;

		/// <summary>
		/// Enforce "include what you use" rules; warns if monolithic headers (Engine.h, UnrealEd.h, etc...) are used, and checks that source files include their matching header first.
		/// </summary>
		public bool bEnforceIWYU = true;

		/// <summary>
		/// Emit a warning when an old-style monolithic header is included while compiling this target.
		/// </summary>
		public bool bWarnAboutMonolithicHeadersIncluded = false;

		/// <summary>
		/// Whether the final executable should export symbols.
		/// </summary>
		public bool bHasExports
		{
			get => bHasExportsOverride ?? (LinkType == TargetLinkType.Modular);
			set => bHasExportsOverride = value;
		}
		private bool? bHasExportsOverride;

		/// <summary>
		/// Make static libraries for all engine modules as intermediates for this target.
		/// </summary>
		[CommandLine("-Precompile")]
		public bool bPrecompile = false;

		/// <summary>
		/// Whether we should compile with support for OS X 10.9 Mavericks. Used for some tools that we need to be compatible with this version of OS X.
		/// </summary>
		public bool bEnableOSX109Support = false;

		/// <summary>
		/// True if this is a console application that's being built.
		/// </summary>
		public bool bIsBuildingConsoleApplication = false;

		/// <summary>
		/// If true, creates an additional console application. Hack for Windows, where it's not possible to conditionally inherit a parent's console Window depending on how
		/// the application is invoked; you have to link the same executable with a different subsystem setting.
		/// </summary>
		public bool bBuildAdditionalConsoleApp
		{
			get => bBuildAdditionalConsoleAppOverride ?? (Type == TargetType.Editor);
			set => bBuildAdditionalConsoleAppOverride = value;
		}
		private bool? bBuildAdditionalConsoleAppOverride;

		/// <summary>
		/// True if debug symbols that are cached for some platforms should not be created.
		/// </summary>
		public bool bDisableSymbolCache = true;

		/// <summary>
		/// Whether to unify C++ code into larger files for faster compilation.
		/// </summary>
		public bool bUseUnityBuild
		{
			get => bUseUnityBuildOverride ?? IntermediateEnvironment.IsUnity() && !bEnableCppModules;
			set => bUseUnityBuildOverride = value;
		}

		/// <summary>
		/// Whether to unify C++ code into larger files for faster compilation.
		/// </summary>
		bool? bUseUnityBuildOverride = null;

		/// <summary>
		/// Whether to force C++ source files to be combined into larger files for faster compilation.
		/// </summary>
		[CommandLine("-ForceUnity")]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bForceUnityBuild = false;

		/// <summary>
		/// Whether to merge module and generated unity files for faster compilation.
		/// </summary>
		[Obsolete("Deprecated in UE5.3; use DisableMergingModuleAndGeneratedFilesInUnityFiles instead")]
		[CommandLine("-DisableMergingUnityFiles", Value = "false")]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bMergeModuleAndGeneratedUnityFiles = true;

		/// <summary>
		/// List of modules that disables merging module and generated cpp files in the same unity files.
		/// </summary>
		[XmlConfigFile(Category = "ModuleConfiguration", Name = "DisableMergingModuleAndGeneratedFilesInUnityFiles")]
		public string[]? DisableMergingModuleAndGeneratedFilesInUnityFiles = null;

		/// <summary>
		/// Use a heuristic to determine which files are currently being iterated on and exclude them from unity blobs, result in faster
		/// incremental compile times. The current implementation uses the read-only flag to distinguish the working set, assuming that files will
		/// be made writable by the source control system if they are being modified. This is true for Perforce, but not for Git.
		/// </summary>
		[CommandLine("-DisableAdaptiveUnity", Value = "false")]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bUseAdaptiveUnityBuild = true;

		/// <summary>
		/// Disable optimization for files that are in the adaptive non-unity working set.
		/// </summary>
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bAdaptiveUnityDisablesOptimizations = false;

		/// <summary>
		/// Disables force-included PCHs for files that are in the adaptive non-unity working set.
		/// </summary>
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bAdaptiveUnityDisablesPCH = false;

		/// <summary>
		/// Backing storage for bAdaptiveUnityDisablesProjectPCH.
		/// </summary>
		[XmlConfigFile(Category = "BuildConfiguration")]
		bool? bAdaptiveUnityDisablesProjectPCHForProjectPrivate;

		/// <summary>
		/// Whether to disable force-included PCHs for project source files in the adaptive non-unity working set. Defaults to bAdaptiveUnityDisablesPCH;
		/// </summary>
		public bool bAdaptiveUnityDisablesPCHForProject
		{
			get => bAdaptiveUnityDisablesProjectPCHForProjectPrivate ?? bAdaptiveUnityDisablesPCH;
			set => bAdaptiveUnityDisablesProjectPCHForProjectPrivate = value;
		}

		/// <summary>
		/// Creates a dedicated PCH for each source file in the working set, allowing faster iteration on cpp-only changes.
		/// </summary>
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bAdaptiveUnityCreatesDedicatedPCH = false;

		/// <summary>
		/// Creates a dedicated PCH for each source file in the working set, allowing faster iteration on cpp-only changes.
		/// </summary>
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bAdaptiveUnityEnablesEditAndContinue = false;

		/// <summary>
		/// Creates a dedicated source file for each header file in the working set to detect missing includes in headers.
		/// </summary>
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bAdaptiveUnityCompilesHeaderFiles = false;

		/// <summary>
		/// The number of source files in a game module before unity build will be activated for that module.  This
		/// allows small game modules to have faster iterative compile times for single files, at the expense of slower full
		/// rebuild times.  This setting can be overridden by the bFasterWithoutUnity option in a module's Build.cs file.
		/// </summary>
		[XmlConfigFile(Category = "BuildConfiguration")]
		public int MinGameModuleSourceFilesForUnityBuild = 32;

		/// <summary>
		/// Default treatment of uncategorized warnings
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		public WarningLevel DefaultWarningLevel
		{
			get => (DefaultWarningLevelPrivate == WarningLevel.Default) ? (bWarningsAsErrors ? WarningLevel.Error : WarningLevel.Warning) : DefaultWarningLevelPrivate;
			set => DefaultWarningLevelPrivate = value;
		}

		/// <inheritdoc cref="DefaultWarningLevel"/>
		[XmlConfigFile(Category = "BuildConfiguration", Name = nameof(DefaultWarningLevel))]
		private WarningLevel DefaultWarningLevelPrivate;

		/// <summary>
		/// Level to report deprecation warnings as errors
		/// </summary>
		public WarningLevel DeprecationWarningLevel
		{
			get => (DeprecationWarningLevelPrivate == WarningLevel.Default) ? DefaultWarningLevel : DeprecationWarningLevelPrivate;
			set => DeprecationWarningLevelPrivate = value;
		}

		/// <inheritdoc cref="DeprecationWarningLevel"/>
		[XmlConfigFile(Category = "BuildConfiguration", Name = nameof(DeprecationWarningLevel))]
		private WarningLevel DeprecationWarningLevelPrivate;

		/// <summary>
		/// Forces shadow variable warnings to be treated as errors on platforms that support it.
		/// </summary>
		[CommandLine("-ShadowVariableErrors", Value = nameof(WarningLevel.Error))]
		[RequiresUniqueBuildEnvironment]
		public WarningLevel ShadowVariableWarningLevel = WarningLevel.Warning;

		/// <summary>
		/// Whether to enable all warnings as errors. UE enables most warnings as errors already, but disables a few (such as deprecation warnings).
		/// </summary>
		[XmlConfigFile(Category = "BuildConfiguration")]
		[CommandLine("-WarningsAsErrors")]
		[RequiresUniqueBuildEnvironment]
		public bool bWarningsAsErrors = false;

		/// <summary>
		/// Indicates what warning/error level to treat unsafe type casts as on platforms that support it (e.g., double->float or int64->int32)
		/// </summary>
		[XmlConfigFile(Category = "BuildConfiguration")]
		[RequiresUniqueBuildEnvironment]
		public WarningLevel UnsafeTypeCastWarningLevel = WarningLevel.Off;

		/// <summary>
		/// Forces the use of undefined identifiers in conditional expressions to be treated as errors.
		/// </summary>
		[XmlConfigFile(Category = "BuildConfiguration")]
		[RequiresUniqueBuildEnvironment]
		public bool bUndefinedIdentifierErrors = true;

		/// <summary>
		/// Indicates what warning/error level to treat potential PCH performance issues.
		/// </summary>
		[XmlConfigFile(Category = "BuildConfiguration")]
		[CommandLine("-PCHPerformanceIssueWarningLevel=")]
		public WarningLevel PCHPerformanceIssueWarningLevel = WarningLevel.Off;

		/// <summary>
		/// How to treat general module include path validation messages
		/// </summary>
		[XmlConfigFile(Category = "BuildConfiguration")]
		[CommandLine("-ModuleIncludePathWarningLevel=")]
		public WarningLevel ModuleIncludePathWarningLevel = WarningLevel.Off;

		/// <summary>
		/// How to treat private module include path validation messages, where a module is adding an include path that exposes private headers
		/// </summary>
		[XmlConfigFile(Category = "BuildConfiguration")]
		[CommandLine("-ModuleIncludePrivateWarningLevel=")]
		public WarningLevel ModuleIncludePrivateWarningLevel = WarningLevel.Off;

		/// <summary>
		/// How to treat unnecessary module sub-directory include path validation messages
		/// </summary>
		[XmlConfigFile(Category = "BuildConfiguration")]
		[CommandLine("-ModuleIncludeSubdirectoryWarningLevel=")]
		public WarningLevel ModuleIncludeSubdirectoryWarningLevel = WarningLevel.Off;

		/// <summary>
		/// Forces frame pointers to be retained this is usually required when you want reliable callstacks e.g. mallocframeprofiler
		/// </summary>
		public bool bRetainFramePointers
		{
			get =>
				// Default to disabled on Linux to maintain legacy behavior
				bRetainFramePointersOverride ?? Platform.IsInGroup(UnrealPlatformGroup.Linux) == false;
			set => bRetainFramePointersOverride = value;
		}

		/// <summary>
		/// Forces frame pointers to be retained this is usually required when you want reliable callstacks e.g. mallocframeprofiler
		/// </summary>
		[XmlConfigFile(Category = "BuildConfiguration", Name = "bRetainFramePointers")]
		[CommandLine("-RetainFramePointers", Value = "true")]
		[CommandLine("-NoRetainFramePointers", Value = "false")]
		public bool? bRetainFramePointersOverride = null;

		/// <summary>
		/// New Monolithic Graphics drivers have optional "fast calls" replacing various D3d functions
		/// </summary>
		[CommandLine("-FastMonoCalls", Value = "true")]
		[CommandLine("-NoFastMonoCalls", Value = "false")]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bUseFastMonoCalls = true;

		/// <summary>
		/// An approximate number of bytes of C++ code to target for inclusion in a single unified C++ file.
		/// </summary>
		[CommandLine("-BytesPerUnityCPP")]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public int NumIncludedBytesPerUnityCPP = 384 * 1024;

		/// <summary>
		/// Disables overrides that are set by the module
		/// </summary>
		[CommandLine("-DisableModuleNumIncludedBytesPerUnityCPPOverride", Value = "true")]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bDisableModuleNumIncludedBytesPerUnityCPPOverride = false;

		/// <summary>
		/// Whether to stress test the C++ unity build robustness by including all C++ files files in a project from a single unified file.
		/// </summary>
		[CommandLine("-StressTestUnity")]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bStressTestUnity = false;

		/// <summary>
		/// Whether to add additional information to the unity files, such as '_of_X' in the file name. Not recommended.
		/// </summary>
		[CommandLine("-DetailedUnityFiles", Value = "true")]
		[CommandLine("-NoDetailedUnityFiles", Value = "false")]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bDetailedUnityFiles = false;

		/// <summary>
		/// Whether to force debug info to be generated.
		/// </summary>
		[CommandLine("-ForceDebugInfo")]
		public bool bForceDebugInfo = false;

		/// <summary>
		/// Whether to globally disable debug info generation; see DebugInfoHeuristics.cs for per-config and per-platform options.
		/// </summary>
		[CommandLine("-NoDebugInfo")]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bDisableDebugInfo = false;

		/// <summary>
		/// Whether to disable debug info generation for generated files. This improves link times for modules that have a lot of generated glue code.
		/// </summary>
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bDisableDebugInfoForGeneratedCode = false;

		/// <summary>
		/// Whether to disable debug info on PC/Mac in development builds (for faster developer iteration, as link times are extremely fast with debug info disabled).
		/// </summary>
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bOmitPCDebugInfoInDevelopment = false;

		/// <summary>
		/// Whether PDB files should be used for Visual C++ builds.
		/// </summary>
		[CommandLine("-NoPDB", Value = "false")]
		[CommandLine("-UsePDB", Value = "true")]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bUsePDBFiles = false;

		/// <summary>
		/// Whether PCH files should be used.
		/// </summary>
		[CommandLine("-NoPCH", Value = "false")]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bUsePCHFiles = true;

		/// <summary>
		/// Set flags require for deterministic compiling and linking.
		/// Enabling deterministic mode for msvc disables codegen multithreading so compiling will be slower
		/// </summary>
		[CommandLine("-Deterministic", Value = "true")]
		[CommandLine("-NonDeterministic", Value = "false")]
		[XmlConfigFile(Category = "BuildConfiguration")]
		[RequiresUniqueBuildEnvironment]
		public bool bDeterministic
		{
			get => bDeterministicPrivate ?? (Configuration == UnrealTargetConfiguration.Shipping);
			set => bDeterministicPrivate = value;
		}
		private bool? bDeterministicPrivate;

		/// <summary>
		/// Whether PCHs should be chained when compiling with clang.
		/// </summary>
		[CommandLine("-NoPCHChain", Value = "false")]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bChainPCHs = true;

		/// <summary>
		/// Whether PCH headers should be force included for gen.cpp files when PCH is disabled.
		/// </summary>
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bForceIncludePCHHeadersForGenCppFilesWhenPCHIsDisabled = true;

		/// <summary>
		/// Whether to just preprocess source files for this target, and skip compilation
		/// </summary>
		[CommandLine("-Preprocess")]
		public bool bPreprocessOnly = false;

		/// <summary>
		/// Generate dependency files by preprocessing. This is only recommended when distributing builds as it adds additional overhead.
		/// </summary>
		[CommandLine("-PreprocessDepends")]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bPreprocessDepends = false;

		/// <summary>
		/// Whether to generate assembly data while compiling this target. Works exclusively on MSVC for now.
		/// </summary>
		[CommandLine("-WithAssembly")]
		public bool bWithAssembly = false;

		/// <summary>
		/// Whether static code analysis should be enabled.
		/// </summary>
		[CommandLine("-StaticAnalyzer")]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public StaticAnalyzer StaticAnalyzer = StaticAnalyzer.None;

		/// <summary>
		/// The output type to use for the static analyzer. This is only supported for Clang.
		/// </summary>
		[CommandLine("-StaticAnalyzerOutputType")]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public StaticAnalyzerOutputType StaticAnalyzerOutputType = StaticAnalyzerOutputType.Text;

		/// <summary>
		/// The mode to use for the static analyzer. This is only supported for Clang.
		/// Shallow mode completes quicker but is generally not recommended.
		/// </summary>
		[CommandLine("-StaticAnalyzerMode")]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public StaticAnalyzerMode StaticAnalyzerMode = StaticAnalyzerMode.Deep;

		/// <summary>
		/// The minimum number of files that must use a pre-compiled header before it will be created and used.
		/// </summary>
		[XmlConfigFile(Category = "BuildConfiguration")]
		public int MinFilesUsingPrecompiledHeader = 6;

		/// <summary>
		/// When enabled, a precompiled header is always generated for game modules, even if there are only a few source files
		/// in the module.  This greatly improves compile times for iterative changes on a few files in the project, at the expense of slower
		/// full rebuild times for small game projects.  This can be overridden by setting MinFilesUsingPrecompiledHeaderOverride in
		/// a module's Build.cs file.
		/// </summary>
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bForcePrecompiledHeaderForGameModules = true;

		/// <summary>
		/// Whether to use incremental linking or not. Incremental linking can yield faster iteration times when making small changes.
		/// Currently disabled by default because it tends to behave a bit buggy on some computers (PDB-related compile errors).
		/// </summary>
		[CommandLine("-IncrementalLinking")]
		[CommandLine("-NoIncrementalLinking", Value = "false")]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bUseIncrementalLinking = false;

		/// <summary>
		/// Whether to allow the use of link time code generation (LTCG).
		/// </summary>
		[CommandLine("-LTCG")]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bAllowLTCG = false;

		/// <summary>
		/// When Link Time Code Generation (LTCG) is enabled, whether to 
		/// prefer using the lighter weight version on supported platforms.
		/// </summary>
		[CommandLine("-ThinLTO")]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bPreferThinLTO = false;

		/// <summary>
		/// Directory where to put the ThinLTO cache on supported platforms.
		/// </summary>
		[CommandLine("-ThinLTOCacheDirectory")]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public string? ThinLTOCacheDirectory = null;

		/// <summary>
		/// Arguments that will be applied to prune the ThinLTO cache on supported platforms.
		/// Arguments will only be applied if ThinLTOCacheDirectory is set.
		/// </summary>
		[CommandLine("-ThinLTOCachePruningArguments")]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public string? ThinLTOCachePruningArguments = null;

		/// <summary>
		/// Whether to enable Profile Guided Optimization (PGO) instrumentation in this build.
		/// </summary>
		[CommandLine("-PGOProfile", Value = "true")]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bPGOProfile = false;

		/// <summary>
		/// Whether to optimize this build with Profile Guided Optimization (PGO).
		/// </summary>
		[CommandLine("-PGOOptimize", Value = "true")]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bPGOOptimize = false;

		/// <summary>
		/// Whether the target requires code coverage compilation and linking.
		/// </summary>
		[CommandLine("-CodeCoverage", Value = "true")]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bCodeCoverage;

		/// <summary>
		/// Whether to support edit and continue.
		/// </summary>
		[CommandLine("-SupportEditAndContinue")]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bSupportEditAndContinue = false;

		/// <summary>
		/// Whether to omit frame pointers or not. Disabling is useful for e.g. memory profiling on the PC.
		/// </summary>
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bOmitFramePointers = true;

		/// <summary>
		/// Whether to enable support for C++20 modules
		/// </summary>
		public bool bEnableCppModules = false;

		/// <summary>
		/// Whether to enable support for C++20 coroutines
		/// This option is provided to facilitate evaluation of the feature.
		/// Expect the name of the option to change. Use of coroutines in with UE is untested and unsupported.
		/// </summary>
		public bool bEnableCppCoroutinesForEvaluation = false;

		/// <summary>
		/// Whether to enable engine's ability to set process priority on runtime.
		/// This option requires some environment setup on Linux, that's why it's disabled by default.
		/// Project has to opt-in this feature as it has to guarantee correct setup.
		/// </summary>
		public bool bEnableProcessPriorityControl = false;

		/// <summary>
		/// If true, then enable memory profiling in the build (defines USE_MALLOC_PROFILER=1 and forces bOmitFramePointers=false).
		/// </summary>
		[Obsolete("Deprecated in UE5.3 - No longer used as MallocProfiler is removed in favor of UnrealInsights.")]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bUseMallocProfiler = false;

		/// <summary>
		/// If true, then enable Unreal Insights (utrace) profiling in the build for the Shader Compiler Worker (defines USE_SHADER_COMPILER_WORKER_TRACE=1).
		/// </summary>
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bShaderCompilerWorkerTrace = false;

		/// <summary>
		/// Enables "Shared PCHs", a feature which significantly speeds up compile times by attempting to
		/// share certain PCH files between modules that UBT detects is including those PCH's header files.
		/// </summary>
		[CommandLine("-NoSharedPCH", Value = "false")]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bUseSharedPCHs = true;

		/// <summary>
		/// True if Development and Release builds should use the release configuration of PhysX/APEX.
		/// </summary>
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bUseShippingPhysXLibraries = false;

		/// <summary>
		/// True if Development and Release builds should use the checked configuration of PhysX/APEX. if bUseShippingPhysXLibraries is true this is ignored.
		/// </summary>
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bUseCheckedPhysXLibraries = false;

		/// <summary>
		/// Tells the UBT to check if module currently being built is violating EULA.
		/// </summary>
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bCheckLicenseViolations = true;

		/// <summary>
		/// Tells the UBT to break build if module currently being built is violating EULA.
		/// </summary>
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bBreakBuildOnLicenseViolation = true;

		/// <summary>
		/// Whether to use the :FASTLINK option when building with /DEBUG to create local PDBs on Windows. Fast, but currently seems to have problems finding symbols in the debugger.
		/// </summary>
		[CommandLine("-FastPDB")]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool? bUseFastPDBLinking;

		/// <summary>
		/// Outputs a map file as part of the build.
		/// </summary>
		[CommandLine("-MapFile")]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bCreateMapFile = false;

		/// <summary>
		/// True if runtime symbols files should be generated as a post build step for some platforms.
		/// These files are used by the engine to resolve symbol names of callstack backtraces in logs.
		/// </summary>
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bAllowRuntimeSymbolFiles = true;

		/// <summary>
		/// Package full path (directory + filename) where to store input files used at link time 
		/// Normally used to debug a linker crash for platforms that support it
		/// </summary>
		[CommandLine("-PackagePath")]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public string? PackagePath = null;

		/// <summary>
		/// Directory where to put crash report files for platforms that support it
		/// </summary>
		[CommandLine("-CrashDiagnosticDirectory")]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public string? CrashDiagnosticDirectory = null;

		/// <summary>
		/// Bundle version for Mac apps.
		/// </summary>
		[CommandLine("-BundleVersion")]
		public string? BundleVersion = null;

		/// <summary>
		/// Whether to deploy the executable after compilation on platforms that require deployment.
		/// </summary>
		[CommandLine("-Deploy")]
		public bool bDeployAfterCompile = false;

		/// <summary>
		/// Whether to force skipping deployment for platforms that require deployment by default.
		/// </summary>
		[CommandLine("-SkipDeploy")]
		private bool bForceSkipDeploy = false;

		/// <summary>
		/// When enabled, allows XGE to compile pre-compiled header files on remote machines.  Otherwise, PCHs are always generated locally.
		/// </summary>
		public bool bAllowRemotelyCompiledPCHs = false;

		/// <summary>
		/// Whether headers in system paths should be checked for modification when determining outdated actions.
		/// </summary>
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bCheckSystemHeadersForModification;

		/// <summary>
		/// Whether to disable linking for this target.
		/// </summary>
		[CommandLine("-NoLink")]
		public bool bDisableLinking = false;

		/// <summary>
		/// Whether to ignore tracking build outputs for this target.
		/// </summary>
		public bool bIgnoreBuildOutputs = false;

		/// <summary>
		/// Indicates that this is a formal build, intended for distribution. This flag is automatically set to true when Build.version has a changelist set.
		/// The only behavior currently bound to this flag is to compile the default resource file separately for each binary so that the OriginalFilename field is set correctly.
		/// By default, we only compile the resource once to reduce build times.
		/// </summary>
		[CommandLine("-Formal")]
		public bool bFormalBuild = false;

		/// <summary>
		/// Whether to clean Builds directory on a remote Mac before building.
		/// </summary>
		[CommandLine("-FlushMac")]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bFlushBuildDirOnRemoteMac = false;

		/// <summary>
		/// Whether to write detailed timing info from the compiler and linker.
		/// </summary>
		[CommandLine("-Timing")]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bPrintToolChainTimingInfo = false;

		/// <summary>
		/// Whether to parse timing data into a tracing file compatible with chrome://tracing.
		/// </summary>
		[CommandLine("-Tracing")]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bParseTimingInfoForTracing = false;

		/// <summary>
		/// Whether to expose all symbols as public by default on POSIX platforms
		/// </summary>
		[CommandLine("-PublicSymbolsByDefault")]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public bool bPublicSymbolsByDefault = false;

		/// <summary>
		/// Disable supports for inlining gen.cpps
		/// </summary>
		[Obsolete("Deprecated in UE5.3; the engine code relies on inlining generated cpp files to compile")]
		[XmlConfigFile(Name = "bDisableInliningGenCpps")]
		[CommandLine("-DisableInliningGenCpps")]
		public bool bDisableInliningGenCpps = false;

		/// <summary>
		/// Allows overriding the toolchain to be created for this target. This must match the name of a class declared in the UnrealBuildTool assembly.
		/// </summary>
		[CommandLine("-ToolChain")]
		public string? ToolChainName = null;

		/// <summary>
		/// The weight(cpu/memory utilization) of a MSVC compile action
		/// </summary>
		[XmlConfigFile(Category = "BuildConfiguration")]
		public float MSVCCompileActionWeight = 1.0f;

		/// <summary>
		/// The weight(cpu/memory utilization) of a clang compile action
		/// </summary>
		[XmlConfigFile(Category = "BuildConfiguration")]
		public float ClangCompileActionWeight = 1.0f;

		/// <summary>
		/// Whether to allow engine configuration to determine if we can load unverified certificates.
		/// </summary>
		public bool bDisableUnverifiedCertificates = false;

		/// <summary>
		/// Whether to load generated ini files in cooked build, (GameUserSettings.ini loaded either way)
		/// </summary>
		public bool bAllowGeneratedIniWhenCooked = true;

		/// <summary>
		/// Whether to load non-ufs ini files in cooked build, (GameUserSettings.ini loaded either way)
		/// </summary>
		public bool bAllowNonUFSIniWhenCooked = true;

		/// <summary>
		/// Add all the public folders as include paths for the compile environment.
		/// </summary>
		public bool bLegacyPublicIncludePaths
		{
			get => bLegacyPublicIncludePathsPrivate ?? (DefaultBuildSettings < BuildSettingsVersion.V2);
			set => bLegacyPublicIncludePathsPrivate = value;
		}
		private bool? bLegacyPublicIncludePathsPrivate;

		/// <summary>
		/// Add all the parent folders as include paths for the compile environment.
		/// </summary>
		public bool bLegacyParentIncludePaths
		{
			get => bLegacyParentIncludePathsPrivate ?? (DefaultBuildSettings < BuildSettingsVersion.V3);
			set => bLegacyParentIncludePathsPrivate = value;
		}
		private bool? bLegacyParentIncludePathsPrivate;

		/// <summary>
		/// Which C++ standard to use for compiling this target (for engine modules)
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		[CommandLine("-CppStdEngine")]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public CppStandardVersion CppStandardEngine
		{
			get => CppStandardEnginePrivate ?? CppStandardVersion.EngineDefault;
			set => CppStandardEnginePrivate = value;
		}
		private CppStandardVersion? CppStandardEnginePrivate;

		/// <summary>
		/// Which C++ standard to use for compiling this target (for non-engine modules)
		/// </summary>
		[CommandLine("-CppStd")]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public CppStandardVersion CppStandard
		{
			get => CppStandardPrivate ?? (DefaultBuildSettings < BuildSettingsVersion.V4 ? CppStandardVersion.Cpp17 : CppStandardVersion.Default);
			set => CppStandardPrivate = value;
		}
		private CppStandardVersion? CppStandardPrivate;

		/// <summary>
		/// Which C standard to use for compiling this target
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		[CommandLine("-CStd")]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public CStandardVersion CStandard = CStandardVersion.Default;

		/// <summary>
		/// Direct the compiler to generate AVX instructions wherever SSE or AVX intrinsics are used, on the x64 platforms that support it.
		/// Note that by enabling this you are changing the minspec for the PC platform, and the resultant executable will crash on machines without AVX support.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		[CommandLine("-MinCpuArchX64")]
		[XmlConfigFile(Category = "BuildConfiguration")]
		public MinimumCpuArchitectureX64 MinCpuArchX64 = MinimumCpuArchitectureX64.Default;

		/// <summary>
		/// Do not allow manifest changes when building this target. Used to cause earlier errors when building multiple targets with a shared build environment.
		/// </summary>
		[CommandLine("-NoManifestChanges")]
		internal bool bNoManifestChanges = false;

		/// <summary>
		/// The build version string
		/// </summary>
		[CommandLine("-BuildVersion")]
		public string? BuildVersion;

		/// <summary>
		/// Specifies how to link modules in this target (monolithic or modular). This is currently protected for backwards compatibility. Call the GetLinkType() accessor
		/// until support for the deprecated ShouldCompileMonolithic() override has been removed.
		/// </summary>
		public TargetLinkType LinkType
		{
			get => (LinkTypePrivate != TargetLinkType.Default) ? LinkTypePrivate : ((Type == global::UnrealBuildTool.TargetType.Editor) ? TargetLinkType.Modular : TargetLinkType.Monolithic);
			set => LinkTypePrivate = value;
		}

		/// <summary>
		/// Backing storage for the LinkType property.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		[CommandLine("-Monolithic", Value = "Monolithic")]
		[CommandLine("-Modular", Value = "Modular")]
		TargetLinkType LinkTypePrivate = TargetLinkType.Default;

		/// <summary>
		/// Macros to define globally across the whole target.
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		[CommandLine("-Define:")]
		public List<string> GlobalDefinitions = new List<string>();

		/// <summary>
		/// Macros to define across all macros in the project.
		/// </summary>
		[CommandLine("-ProjectDefine:")]
		public List<string> ProjectDefinitions = new List<string>();

		/// <summary>
		/// Specifies the name of the launch module. For modular builds, this is the module that is compiled into the target's executable.
		/// </summary>
		public string? LaunchModuleName
		{
			get => (LaunchModuleNamePrivate == null && Type != global::UnrealBuildTool.TargetType.Program) ? "Launch" : LaunchModuleNamePrivate;
			set => LaunchModuleNamePrivate = value;
		}

		/// <summary>
		/// Backing storage for the LaunchModuleName property.
		/// </summary>
		private string? LaunchModuleNamePrivate;

		/// <summary>
		/// Specifies the path to write a header containing public definitions for this target. Useful when building a DLL to be consumed by external build processes.
		/// </summary>
		public string? ExportPublicHeader;

		/// <summary>
		/// List of additional modules to be compiled into the target.
		/// </summary>
		public List<string> ExtraModuleNames = new List<string>();

		/// <summary>
		/// Path to a manifest to output for this target
		/// </summary>
		[CommandLine("-Manifest")]
		public List<FileReference> ManifestFileNames = new List<FileReference>();

		/// <summary>
		/// Path to a list of dependencies for this target, when precompiling
		/// </summary>
		[CommandLine("-DependencyList")]
		public List<FileReference> DependencyListFileNames = new List<FileReference>();

		/// <summary>
		/// Backing storage for the BuildEnvironment property
		/// </summary>
		[CommandLine("-SharedBuildEnvironment", Value = "Shared")]
		[CommandLine("-UniqueBuildEnvironment", Value = "Unique")]
		private TargetBuildEnvironment? BuildEnvironmentOverride;

		/// <summary>
		/// Specifies the build environment for this target. See TargetBuildEnvironment for more information on the available options.
		/// </summary>
		public TargetBuildEnvironment BuildEnvironment
		{
			get
			{
				if (BuildEnvironmentOverride.HasValue)
				{
					return BuildEnvironmentOverride.Value;
				}
				if (Type == TargetType.Program && ProjectFile != null && File!.IsUnderDirectory(ProjectFile.Directory))
				{
					return TargetBuildEnvironment.Unique;
				}
				else if (Unreal.IsEngineInstalled() || LinkType != TargetLinkType.Monolithic)
				{
					return TargetBuildEnvironment.Shared;
				}
				else
				{
					return TargetBuildEnvironment.Unique;
				}
			}
			set => BuildEnvironmentOverride = value;
		}

		/// <summary>
		/// Whether to ignore violations to the shared build environment (eg. editor targets modifying definitions)
		/// </summary>
		[CommandLine("-OverrideBuildEnvironment")]
		public bool bOverrideBuildEnvironment = false;

		/// <summary>
		/// Specifies a list of targets which should be built before this target is built.
		/// </summary>
		public List<TargetInfo> PreBuildTargets = new List<TargetInfo>();

		/// <summary>
		/// Specifies a list of steps which should be executed before this target is built, in the context of the host platform's shell.
		/// The following variables will be expanded before execution:
		/// $(EngineDir), $(ProjectDir), $(TargetName), $(TargetPlatform), $(TargetConfiguration), $(TargetType), $(ProjectFile).
		/// </summary>
		public List<string> PreBuildSteps = new List<string>();

		/// <summary>
		/// Specifies a list of steps which should be executed after this target is built, in the context of the host platform's shell.
		/// The following variables will be expanded before execution:
		/// $(EngineDir), $(ProjectDir), $(TargetName), $(TargetPlatform), $(TargetConfiguration), $(TargetType), $(ProjectFile).
		/// </summary>
		public List<string> PostBuildSteps = new List<string>();

		/// <summary>
		/// Specifies additional build products produced as part of this target.
		/// </summary>
		public List<string> AdditionalBuildProducts = new List<string>();

		/// <summary>
		/// Additional arguments to pass to the compiler
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		[CommandLine("-CompilerArguments=")]
		public string? AdditionalCompilerArguments;

		/// <summary>
		/// Additional arguments to pass to the linker
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		[CommandLine("-LinkerArguments=")]
		public string? AdditionalLinkerArguments;

		/// <summary>
		/// Max amount of memory that each compile action may require. Used by ParallelExecutor to decide the maximum 
		/// number of parallel actions to start at one time.
		/// </summary>
		public double MemoryPerActionGB = 0.0;

		/// <summary>
		/// List of modules to disable unity builds for
		/// </summary>
		[XmlConfigFile(Category = "ModuleConfiguration", Name = "DisableUnityBuild")]
		public string[]? DisableUnityBuildForModules = null;

		/// <summary>
		///  List of modules to enable optimizations for
		/// </summary>
		[XmlConfigFile(Category = "ModuleConfiguration", Name = "EnableOptimizeCode")]
		public string[]? EnableOptimizeCodeForModules = null;

		/// <summary>
		/// List of modules to disable optimizations for
		/// </summary>
		[XmlConfigFile(Category = "ModuleConfiguration", Name = "DisableOptimizeCode")]
		public string[]? DisableOptimizeCodeForModules = null;

		/// <summary>
		///  List of modules to optimize for size. This allows the target to override module optimization level
		///  Note that this may disable PCH usage if a private PCH is not provided
		/// </summary>
		[XmlConfigFile(Category = "ModuleConfiguration", Name = "OptimizeForSize")]
		public string[]? OptimizeForSizeModules = null;

		/// <summary>
		///  List of modules to optimize for size and speed. This allows the target to override module optimization level
		///  Note that this may disable PCH usage if a private PCH is not provided
		/// </summary>
		[XmlConfigFile(Category = "ModuleConfiguration", Name = "OptimizeForSizeAndSpeed")]
		public string[]? OptimizeForSizeAndSpeedModules = null;

		/// <summary>
		/// When generating project files, specifies the name of the project file to use when there are multiple targets of the same type.
		/// </summary>
		public string? GeneratedProjectName;

		/// <summary>
		/// If this is non-null, then any platforms NOT listed will not be allowed to have modules in their directories be created
		/// </summary>
		public UnrealTargetPlatform[]? OptedInModulePlatforms = null;

		/// <summary>
		/// Android-specific target settings.
		/// </summary>
		[ConfigSubObject]
		public AndroidTargetRules AndroidPlatform = new AndroidTargetRules();

		/// <summary>
		/// IOS-specific target settings.
		/// </summary>
		[ConfigSubObject]
		public IOSTargetRules IOSPlatform = new IOSTargetRules();

		/// <summary>
		/// Linux-specific target settings.
		/// </summary>
		[ConfigSubObject]
		public LinuxTargetRules LinuxPlatform = new LinuxTargetRules();

		/// <summary>
		/// Mac-specific target settings.
		/// </summary>
		[ConfigSubObject]
		public MacTargetRules MacPlatform = new MacTargetRules();

		/// <summary>
		/// Windows-specific target settings.
		/// </summary>
		[ConfigSubObject]
		public WindowsTargetRules WindowsPlatform; // Requires 'this' parameter; initialized in constructor

		/// <summary>
		/// Create a TargetRules instance
		/// </summary>
		/// <param name="RulesType">Type to create</param>
		/// <param name="TargetInfo">Target info</param>
		/// <param name="BaseFile">Path to the file for the rules assembly</param>
		/// <param name="PlatformFile">Path to the platform specific rules file</param>
		/// <param name="TargetFiles">Path to all possible rules file</param>
		/// <param name="DefaultBuildSettings"></param>
		/// <param name="Logger">Logger for the new target rules</param>
		/// <returns>Target instance</returns>
		public static TargetRules Create(Type RulesType, TargetInfo TargetInfo, FileReference? BaseFile, FileReference? PlatformFile, IEnumerable<FileReference>? TargetFiles, BuildSettingsVersion? DefaultBuildSettings, ILogger Logger)
		{
			TargetRules Rules = (TargetRules)FormatterServices.GetUninitializedObject(RulesType);
			if (DefaultBuildSettings.HasValue)
			{
				Rules.DefaultBuildSettings = DefaultBuildSettings.Value;
			}

			// The base target file name: this affects where the resulting build product is created so the platform/group is not desired in this case.
			Rules.File = BaseFile;

			// The platform/group-specific target file name
			Rules.TargetSourceFile = PlatformFile;

			// All target files for this target that could cause invalidation
			Rules.TargetFiles = TargetFiles?.ToHashSet() ?? new();

			// Initialize the logger
			Rules.Logger = Logger;

			// Find the constructor
			ConstructorInfo? Constructor = RulesType.GetConstructor(new Type[] { typeof(TargetInfo) });
			if (Constructor == null)
			{
				throw new BuildException("No constructor found on {0} which takes an argument of type TargetInfo.", RulesType.Name);
			}

			// Invoke the regular constructor
			try
			{
				Constructor.Invoke(Rules, new object[] { TargetInfo });
			}
			catch (Exception Ex)
			{
				throw new BuildException(Ex, "Unable to instantiate instance of '{0}' object type from compiled assembly '{1}'.  Unreal Build Tool creates an instance of your module's 'Rules' object in order to find out about your module's requirements.  The CLR exception details may provide more information:  {2}", RulesType.Name, Path.GetFileNameWithoutExtension(RulesType.Assembly?.Location), Ex.ToString());
			}

			return Rules;
		}

		/// <summary>
		/// Constructor.
		/// </summary>
		/// <param name="Target">Information about the target being built</param>
		public TargetRules(TargetInfo Target)
		{
			DefaultName = Target.Name;
			Platform = Target.Platform;
			Configuration = Target.Configuration;
			Architectures = Target.Architectures;
			IntermediateEnvironment = Target.IntermediateEnvironment;
			ProjectFile = Target.ProjectFile;
			Version = Target.Version;
			WindowsPlatform = new WindowsTargetRules(this);

			// Make sure the logger was initialized by the caller
			if (Logger == null)
			{
				throw new NotSupportedException("Logger property must be initialized by the caller.");
			}

			// Read settings from config files
			Dictionary<ConfigDependencyKey, IReadOnlyList<string>?> ConfigValues = new Dictionary<ConfigDependencyKey, IReadOnlyList<string>?>();
			foreach (object ConfigurableObject in GetConfigurableObjects())
			{
				ConfigCache.ReadSettings(DirectoryReference.FromFile(ProjectFile), Platform, ConfigurableObject, ConfigValues, Target.Arguments);
				XmlConfig.ApplyTo(ConfigurableObject);
				if (Target.Arguments != null)
				{
					Target.Arguments.ApplyTo(ConfigurableObject);
				}
			}
			ConfigValueTracker = new ConfigValueTracker(ConfigValues);

			// If we've got a changelist set, set that we're making a formal build
			bFormalBuild = (Version.Changelist != 0 && Version.IsPromotedBuild);

			// Allow the build platform to set defaults for this target
			UEBuildPlatform.GetBuildPlatform(Platform).ResetTarget(this);
			bDeployAfterCompile = bForceSkipDeploy ? false : bDeployAfterCompile;

			// Determine intermediate environment overrides based on command line flags
			if (IntermediateEnvironment == UnrealIntermediateEnvironment.Default)
			{
				if (bIWYU)
				{
					IntermediateEnvironment = UnrealIntermediateEnvironment.IWYU;
				}
				else if (StaticAnalyzer != StaticAnalyzer.None)
				{
					IntermediateEnvironment = UnrealIntermediateEnvironment.Analyze;
				}
				else if (!bUseUnityBuild)
				{
					IntermediateEnvironment = UnrealIntermediateEnvironment.NonUnity;
				}
			}

			// Set the default build version
			if (String.IsNullOrEmpty(BuildVersion))
			{
				if (String.IsNullOrEmpty(Target.Version.BuildVersionString))
				{
					BuildVersion = String.Format("{0}-CL-{1}", Target.Version.BranchName, Target.Version.Changelist);
				}
				else
				{
					BuildVersion = Target.Version.BuildVersionString;
				}
			}

			// Get the directory to use for crypto settings. We can build engine targets (eg. UHT) with 
			// a project file, but we can't use that to determine crypto settings without triggering
			// constant rebuilds of UHT.
			DirectoryReference? CryptoSettingsDir = DirectoryReference.FromFile(ProjectFile);
			if (CryptoSettingsDir != null && File != null && !File.IsUnderDirectory(CryptoSettingsDir))
			{
				CryptoSettingsDir = null;
			}

			// Setup macros for signing and encryption keys
			EncryptionAndSigning.CryptoSettings CryptoSettings = EncryptionAndSigning.ParseCryptoSettings(CryptoSettingsDir, Platform, Logger);
			if (CryptoSettings.IsAnyEncryptionEnabled())
			{
				ProjectDefinitions.Add(String.Format("IMPLEMENT_ENCRYPTION_KEY_REGISTRATION()=UE_REGISTER_ENCRYPTION_KEY({0})", FormatHexBytes(CryptoSettings.EncryptionKey!.Key!)));
			}
			else
			{
				ProjectDefinitions.Add("IMPLEMENT_ENCRYPTION_KEY_REGISTRATION()=");
			}

			if (CryptoSettings.IsPakSigningEnabled())
			{
				ProjectDefinitions.Add(String.Format("IMPLEMENT_SIGNING_KEY_REGISTRATION()=UE_REGISTER_SIGNING_KEY(UE_LIST_ARGUMENT({0}), UE_LIST_ARGUMENT({1}))", FormatHexBytes(CryptoSettings.SigningKey!.PublicKey.Exponent!), FormatHexBytes(CryptoSettings.SigningKey.PublicKey.Modulus!)));
			}
			else
			{
				ProjectDefinitions.Add("IMPLEMENT_SIGNING_KEY_REGISTRATION()=");
			}
		}

		/// <summary>
		/// Formats an array of bytes as a sequence of values
		/// </summary>
		/// <param name="Data">The data to convert into a string</param>
		/// <returns>List of hexadecimal bytes</returns>
		private static string FormatHexBytes(byte[] Data)
		{
			return String.Join(",", Data.Select(x => String.Format("0x{0:X2}", x)));
		}

		/// <summary>
		/// Override any settings required for the selected target type
		/// </summary>
		internal void SetOverridesForTargetType()
		{
			if (Type == global::UnrealBuildTool.TargetType.Game)
			{
				GlobalDefinitions.Add("UE_GAME=1");
			}
			else if (Type == global::UnrealBuildTool.TargetType.Client)
			{
				GlobalDefinitions.Add("UE_GAME=1");
				GlobalDefinitions.Add("UE_CLIENT=1");
			}
			else if (Type == global::UnrealBuildTool.TargetType.Editor)
			{
				GlobalDefinitions.Add("UE_EDITOR=1");
			}
			else if (Type == global::UnrealBuildTool.TargetType.Server)
			{
				GlobalDefinitions.Add("UE_SERVER=1");
				GlobalDefinitions.Add("USE_NULL_RHI=1");
			}
		}

		/// <summary>
		/// Override settings that all cooked editor targets will want
		/// </summary>
		protected void SetDefaultsForCookedEditor(bool bIsCookedCooker, bool bIsForExternalUse)
		{
			LinkType = TargetLinkType.Monolithic;
			BuildEnvironment = TargetBuildEnvironment.Unique;

			if (!bIsCookedCooker)
			{
				bBuildAdditionalConsoleApp = false;
			}

			GlobalDefinitions.Add("ASSETREGISTRY_ENABLE_PREMADE_REGISTRY_IN_EDITOR=1");
			bUseLoggingInShipping = true;

			GlobalDefinitions.Add("UE_IS_COOKED_EDITOR=1");

			// remove some insecure things external users may not want
			if (bIsForExternalUse)
			{
				bWithServerCode = false;
				bBuildTargetDeveloperTools = false;
				GlobalDefinitions.Add("AUTOSDKS_ENABLED=0");
			}

			// this will allow shader compiling to work based on whether or not the shaders directory is present
			// to determine if we should allow shader compilation
			GlobalDefinitions.Add("UE_ALLOW_SHADER_COMPILING_BASED_ON_SHADER_DIRECTORY_EXISTENCE=1");
			// this setting can be used to compile out the shader compiler if that is important
			//GlobalDefinitions.Add("UE_ALLOW_SHADER_COMPILING=0");

			ConfigHierarchy ProjectGameIni = ConfigCache.ReadHierarchy(ConfigHierarchyType.Game, ProjectFile?.Directory, Platform);
			List<string>? DisabledPlugins;
			List<string> AllDisabledPlugins = new List<string>();

			if (ProjectGameIni.GetArray("CookedEditorSettings", "DisabledPlugins", out DisabledPlugins))
			{
				AllDisabledPlugins.AddRange(DisabledPlugins);
			}
			if (ProjectGameIni.GetArray("CookedEditorSettings" + (bIsCookedCooker ? "_CookedCooker" : "_CookedEditor"), "DisabledPlugins", out DisabledPlugins))
			{
				AllDisabledPlugins.AddRange(DisabledPlugins);
			}
			if (Configuration == UnrealTargetConfiguration.Shipping)
			{
				if (ProjectGameIni.GetArray("CookedEditorSettings", "DisabledPluginsInShipping", out DisabledPlugins))
				{
					AllDisabledPlugins.AddRange(DisabledPlugins);
				}
				if (ProjectGameIni.GetArray("CookedEditorSettings" + (bIsCookedCooker ? "_CookedCooker" : "_CookedEditor"), "DisabledPluginsInShipping", out DisabledPlugins))
				{
					AllDisabledPlugins.AddRange(DisabledPlugins);
				}
			}

			// disable them, and remove them from Enabled in case they were there
			foreach (string PluginName in AllDisabledPlugins)
			{
				DisablePlugins.Add(PluginName);
				EnablePlugins.Remove(PluginName);
			}
		}

		/// <summary>
		/// Gets a list of platforms that this target supports
		/// </summary>
		/// <returns>Array of platforms that the target supports</returns>
		internal UnrealTargetPlatform[] GetSupportedPlatforms()
		{
			// Take the SupportedPlatformsAttribute from the first type in the inheritance chain that supports it
			for (Type? CurrentType = GetType(); CurrentType != null; CurrentType = CurrentType.BaseType)
			{
				object[] Attributes = CurrentType.GetCustomAttributes(typeof(SupportedPlatformsAttribute), false);
				if (Attributes.Length > 0)
				{
					return Attributes.OfType<SupportedPlatformsAttribute>().SelectMany(x => x.Platforms).Distinct().ToArray();
				}
			}

			// Otherwise, get the default for the target type
			if (Type == TargetType.Program)
			{
				return Utils.GetPlatformsInClass(UnrealPlatformClass.Desktop);
			}
			else if (Type == TargetType.Editor)
			{
				return Utils.GetPlatformsInClass(UnrealPlatformClass.Editor);
			}
			else
			{
				return Utils.GetPlatformsInClass(UnrealPlatformClass.All);
			}
		}

		/// <summary>
		/// Gets a list of configurations that this target supports
		/// </summary>
		/// <returns>Array of configurations that the target supports</returns>
		internal UnrealTargetConfiguration[] GetSupportedConfigurations()
		{
			// Otherwise take the SupportedConfigurationsAttribute from the first type in the inheritance chain that supports it
			for (Type? CurrentType = GetType(); CurrentType != null; CurrentType = CurrentType.BaseType)
			{
				object[] Attributes = CurrentType.GetCustomAttributes(typeof(SupportedConfigurationsAttribute), false);
				if (Attributes.Length > 0)
				{
					return Attributes.OfType<SupportedConfigurationsAttribute>().SelectMany(x => x.Configurations).Distinct().ToArray();
				}
			}

			// Otherwise, get the default for the target type
			if (Type == TargetType.Editor)
			{
				return new[] { UnrealTargetConfiguration.Debug, UnrealTargetConfiguration.DebugGame, UnrealTargetConfiguration.Development };
			}
			else
			{
				return ((UnrealTargetConfiguration[])Enum.GetValues(typeof(UnrealTargetConfiguration))).Where(x => x != UnrealTargetConfiguration.Unknown).ToArray();
			}
		}

		/// <summary>
		/// Finds all the subobjects which can be configured by command line options and config files
		/// </summary>
		/// <returns>Sequence of objects</returns>
		internal IEnumerable<object> GetConfigurableObjects()
		{
			yield return this;

			foreach (FieldInfo Field in GetType().GetFields(BindingFlags.Public | BindingFlags.Instance))
			{
				if (Field.GetCustomAttribute<ConfigSubObjectAttribute>() != null)
				{
					object? Value = Field.GetValue(this);
					if (Value != null)
					{
						yield return Value;
					}
				}
			}
			foreach (UnrealTargetPlatform Platform in UnrealTargetPlatform.GetValidPlatforms())
			{
				UEBuildPlatform? BuildPlatform;
				if (UEBuildPlatform.TryGetBuildPlatform(Platform, out BuildPlatform))
				{
					yield return BuildPlatform.ArchitectureConfig;
				}
			}
		}

		/// <summary>
		/// Gets the host platform being built on
		/// </summary>
		public UnrealTargetPlatform HostPlatform => BuildHostPlatform.Current.Platform;

		/// <summary>
		/// Expose the bGenerateProjectFiles flag to targets, so we can modify behavior as appropriate for better intellisense
		/// </summary>
		public bool bGenerateProjectFiles => ProjectFileGenerator.bGenerateProjectFiles;

		/// <summary>
		/// Indicates whether target rules should be used to explicitly enable or disable plugins. Usually not needed for project generation unless project files indicate whether referenced plugins should be built or not.
		/// </summary>
		public bool bShouldTargetRulesTogglePlugins => ((ProjectFileGenerator.Current != null) && ProjectFileGenerator.Current.ShouldTargetRulesTogglePlugins())
					|| ((ProjectFileGenerator.Current == null) && !ProjectFileGenerator.bGenerateProjectFiles);

		/// <summary>
		/// Expose a setting for whether or not the engine is installed
		/// </summary>
		/// <returns>Flag for whether the engine is installed</returns>
		public bool bIsEngineInstalled => Unreal.IsEngineInstalled();

		/// <summary>
		/// Gets diagnostic messages about default settings which have changed in newer versions of the engine
		/// </summary>
		/// <param name="Diagnostics">List of diagnostic messages</param>
		internal void GetBuildSettingsInfo(List<string> Diagnostics)
		{
			// Resolve BuildSettingsVersion.Latest to the version it's assigned to
			BuildSettingsVersion LatestVersion = BuildSettingsVersion.Latest;
			foreach (BuildSettingsVersion Value in Enum.GetValues(typeof(BuildSettingsVersion)))
			{
				if ((int)Value == (int)BuildSettingsVersion.Latest)
				{
					LatestVersion = Value;
				}
			}

			if (DefaultBuildSettings < LatestVersion)
			{
				Diagnostics.Add("[Upgrade]");
				Diagnostics.Add("[Upgrade] Using backward-compatible build settings. The latest version of UE sets the following values by default, which may require code changes:");

				List<Tuple<string, string>> ModifiedSettings = new List<Tuple<string, string>>();
				if (BuildSettingsVersion.V2 <= LatestVersion && DefaultBuildSettings < BuildSettingsVersion.V2)
				{
					ModifiedSettings.Add(Tuple.Create(String.Format("{0} = false", nameof(bLegacyPublicIncludePaths)), "Omits subfolders from public include paths to reduce compiler command line length. (Previously: true)."));
					ModifiedSettings.Add(Tuple.Create(String.Format("{0} = WarningLevel.Error", nameof(ShadowVariableWarningLevel)), "Treats shadowed variable warnings as errors. (Previously: WarningLevel.Warning)."));
					ModifiedSettings.Add(Tuple.Create(String.Format("{0} = PCHUsageMode.UseExplicitOrSharedPCHs", nameof(ModuleRules.PCHUsage)), "Set in build.cs files to enables IWYU-style PCH model. See https://docs.unrealengine.com/en-US/Programming/BuildTools/UnrealBuildTool/IWYU/index.html. (Previously: PCHUsageMode.UseSharedPCHs)."));
				}

				if (BuildSettingsVersion.V3 <= LatestVersion && DefaultBuildSettings < BuildSettingsVersion.V3)
				{
					ModifiedSettings.Add(Tuple.Create(String.Format("{0} = false", nameof(bLegacyParentIncludePaths)), "Omits module parent folders from include paths to reduce compiler command line length. (Previously: true)."));
				}

				if (BuildSettingsVersion.V4 <= LatestVersion && DefaultBuildSettings < BuildSettingsVersion.V4)
				{
					ModifiedSettings.Add(Tuple.Create(String.Format("{0} = CppStandardVersion.Default", nameof(CppStandard)), "Updates C++ Standard to C++20 (Previously: CppStandardVersion.Cpp17)."));
					ModifiedSettings.Add(Tuple.Create(String.Format("{0}.{1} = true", nameof(WindowsPlatform), nameof(WindowsPlatform.bStrictConformanceMode)), "Updates MSVC strict conformance mode to true (Previously: false)."));
				}

				if (ModifiedSettings.Count > 0)
				{
					string FormatString = String.Format("[Upgrade]     {{0,-{0}}}   => {{1}}", ModifiedSettings.Max(x => x.Item1.Length));
					foreach (Tuple<string, string> ModifiedSetting in ModifiedSettings)
					{
						Diagnostics.Add(String.Format(FormatString, ModifiedSetting.Item1, ModifiedSetting.Item2));
					}
				}
				Diagnostics.Add(String.Format("[Upgrade] Suppress this message by setting 'DefaultBuildSettings = BuildSettingsVersion.{0};' in {1}, and explicitly overriding settings that differ from the new defaults.", LatestVersion, File!.GetFileName()));
				Diagnostics.Add("[Upgrade]");
			}

			if (IncludeOrderVersion <= (EngineIncludeOrderVersion)(EngineIncludeOrderVersion.Latest - 1) && ForcedIncludeOrder == null)
			{
				Diagnostics.Add("[Upgrade]");
				Diagnostics.Add("[Upgrade] Using backward-compatible include order. The latest version of UE has changed the order of includes, which may require code changes. The current setting is:");
				Diagnostics.Add(String.Format("[Upgrade]     IncludeOrderVersion = EngineIncludeOrderVersion.{0}", IncludeOrderVersion));
				Diagnostics.Add(String.Format("[Upgrade] Suppress this message by setting 'IncludeOrderVersion = EngineIncludeOrderVersion.{0};' in {1}.", EngineIncludeOrderVersion.Latest, File!.GetFileName()));
				Diagnostics.Add("[Upgrade] Alternatively you can set this to 'EngineIncludeOrderVersion.Latest' to always use the latest include order. This will potentially cause compile errors when integrating new versions of the engine.");
				Diagnostics.Add("[Upgrade]");
			}

			Logger.LogDebug("Using EngineIncludeOrderVersion.{Version} for target {Target}", IncludeOrderVersion, File!.GetFileName());
		}
	}

	/// <summary>
	/// Read-only wrapper around an existing TargetRules instance. This exposes target settings to modules without letting them to modify the global environment.
	/// </summary>
	public partial class ReadOnlyTargetRules
	{
		/// <summary>
		/// The writeable TargetRules instance
		/// </summary>
		TargetRules Inner;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Inner">The TargetRules instance to wrap around</param>
		public ReadOnlyTargetRules(TargetRules Inner)
		{
			this.Inner = Inner;
			AndroidPlatform = new ReadOnlyAndroidTargetRules(Inner.AndroidPlatform);
			IOSPlatform = new ReadOnlyIOSTargetRules(Inner.IOSPlatform);
			LinuxPlatform = new ReadOnlyLinuxTargetRules(Inner.LinuxPlatform);
			MacPlatform = new ReadOnlyMacTargetRules(Inner.MacPlatform);
			WindowsPlatform = new ReadOnlyWindowsTargetRules(Inner.WindowsPlatform);
		}

		/// <summary>
		/// Accessors for fields on the inner TargetRules instance
		/// </summary>
		#region Read-only accessor properties
#pragma warning disable CS1591

		public string Name => Inner.Name;

		public string OriginalName => Inner.OriginalName;

		internal FileReference File => Inner.File!;

		internal FileReference TargetSourceFile => Inner.TargetSourceFile!;

		internal IReadOnlySet<FileReference> TargetFiles => Inner.TargetFiles!;

		public object? AdditionalData => Inner.AdditionalData;

		public UnrealTargetPlatform Platform => Inner.Platform;

		public UnrealTargetConfiguration Configuration => Inner.Configuration;

		public UnrealArchitectures Architectures => Inner.Architectures;

		public UnrealIntermediateEnvironment IntermediateEnvironment => Inner.IntermediateEnvironment;

		public UnrealArch Architecture => Inner.Architecture;

		public FileReference? ProjectFile => Inner.ProjectFile;

		public ReadOnlyBuildVersion Version => Inner.Version;

		public TargetType Type => Inner.Type;

		/// <inheritdoc cref="TargetRules.Logger"/>
		public ILogger Logger => Inner.Logger;

		public BuildSettingsVersion DefaultBuildSettings => Inner.DefaultBuildSettings;

		public EngineIncludeOrderVersion? ForcedIncludeOrder => Inner.ForcedIncludeOrder;

		public EngineIncludeOrderVersion IncludeOrderVersion => Inner.IncludeOrderVersion;

		internal ConfigValueTracker ConfigValueTracker => Inner.ConfigValueTracker;

		public string? OutputFile => Inner.OutputFile;

		public bool bUsesSteam => Inner.bUsesSteam;

		public bool bUsesCEF3 => Inner.bUsesCEF3;

		public bool bUsesSlate => Inner.bUsesSlate;

		public bool bUseStaticCRT => Inner.bUseStaticCRT;

		public bool bDebugBuildsActuallyUseDebugCRT => Inner.bDebugBuildsActuallyUseDebugCRT;

		public bool bLegalToDistributeBinary => Inner.bLegalToDistributeBinary;

		public UnrealTargetConfiguration UndecoratedConfiguration => Inner.UndecoratedConfiguration;

		public bool bAllowHotReload => Inner.bAllowHotReload;

		public bool bBuildAllModules => Inner.bBuildAllModules;

		public IEnumerable<string> AdditionalPlugins => Inner.AdditionalPlugins;

		public IEnumerable<string> EnablePlugins => Inner.EnablePlugins;

		public IEnumerable<string> DisablePlugins => Inner.DisablePlugins;

		public WarningLevel DisablePluginsConflictWarningLevel => Inner.DisablePluginsConflictWarningLevel;

		public IEnumerable<string> BuildPlugins => Inner.BuildPlugins;

		public IEnumerable<string> InternalPluginDependencies => Inner.InternalPluginDependencies;
		public bool bRuntimeDependenciesComeFromBuildPlugins => Inner.bRuntimeDependenciesComeFromBuildPlugins;

		public string PakSigningKeysFile => Inner.PakSigningKeysFile;

		public string SolutionDirectory => Inner.SolutionDirectory;

		public string CustomConfig => Inner.CustomConfig;

		public bool? bBuildInSolutionByDefault => Inner.bBuildInSolutionByDefault;

		public string ExeBinariesSubFolder => Inner.ExeBinariesSubFolder;

		public EGeneratedCodeVersion GeneratedCodeVersion => Inner.GeneratedCodeVersion;
		public bool bEnableMeshEditor => Inner.bEnableMeshEditor;

		public bool bUseVerse => Inner.bUseVerse;

		public bool bUseAutoRTFMCompiler => Inner.bUseAutoRTFMCompiler;

		[Obsolete("Deprecated in UE5.1 - No longer used as Chaos is always enabled.")]
		public bool bCompileChaos => Inner.bCompileChaos;

		[Obsolete("Deprecated in UE5.1 - No longer used as Chaos is always enabled.")]
		public bool bUseChaos => Inner.bUseChaos;

		public bool bUseChaosMemoryTracking => Inner.bUseChaosMemoryTracking;

		public bool bCompileChaosVisualDebuggerSupport => Inner.bCompileChaosVisualDebuggerSupport;

		public bool bUseChaosChecked => Inner.bUseChaosChecked;

		[Obsolete("Deprecated in UE5.1 - No longer used in engine.")]
		public bool bCustomSceneQueryStructure => Inner.bCustomSceneQueryStructure;

		[Obsolete("Deprecated in UE5.1 - No longer used as Chaos is always enabled.")]
		public bool bCompilePhysX => Inner.bCompilePhysX;

		[Obsolete("Deprecated in UE5.1 - No longer used as Chaos is always enabled.")]
		public bool bCompileAPEX => Inner.bCompileAPEX;

		[Obsolete("Deprecated in UE5.1 - No longer used as Chaos is always enabled.")]
		public bool bCompileNvCloth => Inner.bCompileNvCloth;

		public bool bCompileICU => Inner.bCompileICU;

		public bool bCompileCEF3 => Inner.bCompileCEF3;

		public bool bCompileISPC => Inner.bCompileISPC;

		public bool bUseGameplayDebugger => Inner.bUseGameplayDebugger;

		public bool bUseGameplayDebuggerCore => Inner.bUseGameplayDebuggerCore;

		public bool bUseIris => Inner.bUseIris;

		public bool bCompileIntelMetricsDiscovery => Inner.bCompileIntelMetricsDiscovery;

		public bool bCompilePython => Inner.bCompilePython;

		public bool bBuildEditor => Inner.bBuildEditor;

		public bool bBuildRequiresCookedData => Inner.bBuildRequiresCookedData;

		public bool bBuildWithEditorOnlyData => Inner.bBuildWithEditorOnlyData;

		public bool bBuildDeveloperTools => Inner.bBuildDeveloperTools;

		public bool bBuildTargetDeveloperTools => Inner.bBuildTargetDeveloperTools;

		public bool bForceBuildTargetPlatforms => Inner.bForceBuildTargetPlatforms;

		public bool bForceBuildShaderFormats => Inner.bForceBuildShaderFormats;

		public bool bNeedsExtraShaderFormats => Inner.bNeedsExtraShaderFormats;

		public bool bCompileCustomSQLitePlatform => Inner.bCompileCustomSQLitePlatform;

		public bool bUseCacheFreedOSAllocs => Inner.bUseCacheFreedOSAllocs;

		public bool bCompileAgainstEngine => Inner.bCompileAgainstEngine;

		public bool bCompileAgainstCoreUObject => Inner.bCompileAgainstCoreUObject;

		public bool bCompileAgainstApplicationCore => Inner.bCompileAgainstApplicationCore;

		public bool bCompileAgainstEditor => Inner.bCompileAgainstEditor;

		public bool bCompileRecast => Inner.bCompileRecast;

		public bool bCompileNavmeshSegmentLinks => Inner.bCompileNavmeshSegmentLinks;

		public bool bCompileNavmeshClusterLinks => Inner.bCompileNavmeshClusterLinks;

		public bool bCompileSpeedTree => Inner.bCompileSpeedTree;

		public bool bForceEnableExceptions => Inner.bForceEnableExceptions;

		public bool bForceEnableObjCExceptions => Inner.bForceEnableObjCExceptions;

		public bool bForceEnableRTTI => Inner.bForceEnableRTTI;

		public bool bEnablePIE => Inner.bEnablePIE;

		public bool bEnableStackProtection => Inner.bEnableStackProtection;

		public bool bUseInlining => Inner.bUseInlining;

		public bool bWithServerCode => Inner.bWithServerCode;

		public bool bFNameOutlineNumber => Inner.bFNameOutlineNumber;

		public bool bWithPushModel => Inner.bWithPushModel;

		public bool bCompileWithStatsWithoutEngine => Inner.bCompileWithStatsWithoutEngine;

		public bool bCompileWithPluginSupport => Inner.bCompileWithPluginSupport;

		public bool bIncludePluginsForTargetPlatforms => Inner.bIncludePluginsForTargetPlatforms;

		public bool bCompileWithAccessibilitySupport => Inner.bCompileWithAccessibilitySupport;

		public bool bWithPerfCounters => Inner.bWithPerfCounters;

		public bool bWithLiveCoding => Inner.bWithLiveCoding;

		public bool bUseDebugLiveCodingConsole => Inner.bUseDebugLiveCodingConsole;

		public bool bWithDirectXMath => Inner.bWithDirectXMath;

		public bool bWithFixedTimeStepSupport => Inner.bWithFixedTimeStepSupport;

		public bool bUseLoggingInShipping => Inner.bUseLoggingInShipping;

		public bool bLoggingToMemoryEnabled => Inner.bLoggingToMemoryEnabled;

		public bool bUseLauncherChecks => Inner.bUseLauncherChecks;

		public bool bUseChecksInShipping => Inner.bUseChecksInShipping;

		public bool bAllowProfileGPUInTest => Inner.bAllowProfileGPUInTest;

		public bool bAllowProfileGPUInShipping => Inner.bAllowProfileGPUInShipping;

		public bool bTCHARIsUTF8 => Inner.bTCHARIsUTF8;

		public bool bUseEstimatedUtcNow => Inner.bUseEstimatedUtcNow;

		public bool bCompileFreeType => Inner.bCompileFreeType;

		public bool bUseExecCommandsInShipping => Inner.bUseExecCommandsInShipping;

		[Obsolete("Deprecated in UE5.1 - Please use OptimizationLevel instead.")]
		public bool bCompileForSize => Inner.bCompileForSize;

		public OptimizationMode OptimizationLevel => Inner.OptimizationLevel;

		public bool bRetainFramePointers => Inner.bRetainFramePointers;

		public bool bForceCompileDevelopmentAutomationTests => Inner.bForceCompileDevelopmentAutomationTests;

		public bool bForceCompilePerformanceAutomationTests => Inner.bForceCompilePerformanceAutomationTests;

		public bool bForceDisableAutomationTests => Inner.bForceDisableAutomationTests;

		public bool bUseXGEController => Inner.bUseXGEController;

		public bool bEventDrivenLoader => Inner.bEventDrivenLoader;

		public PointerMemberBehavior? NativePointerMemberBehaviorOverride => Inner.NativePointerMemberBehaviorOverride;

		public bool bIWYU => Inner.bIWYU;

		public bool bIncludeHeaders => Inner.bIncludeHeaders;

		public bool bHeadersOnly => Inner.bHeadersOnly;

		public bool bEnforceIWYU => Inner.bEnforceIWYU;

		public bool bWarnAboutMonolithicHeadersIncluded => Inner.bWarnAboutMonolithicHeadersIncluded;

		public bool bHasExports => Inner.bHasExports;

		public bool bPrecompile => Inner.bPrecompile;

		public bool bEnableOSX109Support => Inner.bEnableOSX109Support;

		public bool bIsBuildingConsoleApplication => Inner.bIsBuildingConsoleApplication;

		public bool bBuildAdditionalConsoleApp => Inner.bBuildAdditionalConsoleApp;

		public bool bDisableSymbolCache => Inner.bDisableSymbolCache;

		public bool bUseUnityBuild => Inner.bUseUnityBuild;

		public bool bForceUnityBuild => Inner.bForceUnityBuild;

		public IReadOnlyList<string>? DisableMergingModuleAndGeneratedFilesInUnityFiles => Inner.DisableMergingModuleAndGeneratedFilesInUnityFiles;

		public bool bAdaptiveUnityDisablesOptimizations => Inner.bAdaptiveUnityDisablesOptimizations;

		public bool bAdaptiveUnityDisablesPCH => Inner.bAdaptiveUnityDisablesPCH;

		public bool bAdaptiveUnityDisablesPCHForProject => Inner.bAdaptiveUnityDisablesPCHForProject;

		public bool bAdaptiveUnityCreatesDedicatedPCH => Inner.bAdaptiveUnityCreatesDedicatedPCH;

		public bool bAdaptiveUnityEnablesEditAndContinue => Inner.bAdaptiveUnityEnablesEditAndContinue;

		public bool bAdaptiveUnityCompilesHeaderFiles => Inner.bAdaptiveUnityCompilesHeaderFiles;

		public int MinGameModuleSourceFilesForUnityBuild => Inner.MinGameModuleSourceFilesForUnityBuild;

		public WarningLevel DefaultWarningLevel => Inner.DefaultWarningLevel;

		public WarningLevel DeprecationWarningLevel => Inner.DeprecationWarningLevel;

		public WarningLevel ShadowVariableWarningLevel => Inner.ShadowVariableWarningLevel;

		public WarningLevel UnsafeTypeCastWarningLevel => Inner.UnsafeTypeCastWarningLevel;

		public bool bUndefinedIdentifierErrors => Inner.bUndefinedIdentifierErrors;

		public WarningLevel PCHPerformanceIssueWarningLevel => Inner.PCHPerformanceIssueWarningLevel;

		public WarningLevel ModuleIncludePathWarningLevel => Inner.ModuleIncludePathWarningLevel;

		public WarningLevel ModuleIncludePrivateWarningLevel => Inner.ModuleIncludePrivateWarningLevel;

		public WarningLevel ModuleIncludeSubdirectoryWarningLevel => Inner.ModuleIncludeSubdirectoryWarningLevel;

		public bool bWarningsAsErrors => Inner.bWarningsAsErrors;

		public bool bUseFastMonoCalls => Inner.bUseFastMonoCalls;

		public int NumIncludedBytesPerUnityCPP => Inner.NumIncludedBytesPerUnityCPP;

		public bool bDisableModuleNumIncludedBytesPerUnityCPPOverride => Inner.bDisableModuleNumIncludedBytesPerUnityCPPOverride;

		public bool bStressTestUnity => Inner.bStressTestUnity;

		public bool bDetailedUnityFiles => Inner.bDetailedUnityFiles;

		public bool bDisableDebugInfo => Inner.bDisableDebugInfo;

		public bool bDisableDebugInfoForGeneratedCode => Inner.bDisableDebugInfoForGeneratedCode;

		public bool bOmitPCDebugInfoInDevelopment => Inner.bOmitPCDebugInfoInDevelopment;

		public bool bUsePDBFiles => Inner.bUsePDBFiles;

		public bool bUsePCHFiles => Inner.bUsePCHFiles;

		public bool bDeterministic => Inner.bDeterministic;
		public bool bChainPCHs => Inner.bChainPCHs;

		public bool bForceIncludePCHHeadersForGenCppFilesWhenPCHIsDisabled => Inner.bForceIncludePCHHeadersForGenCppFilesWhenPCHIsDisabled;

		public bool bPreprocessOnly => Inner.bPreprocessOnly;

		public bool bPreprocessDepends => Inner.bPreprocessDepends;

		public bool bWithAssembly => Inner.bWithAssembly;

		public StaticAnalyzer StaticAnalyzer => Inner.StaticAnalyzer;

		public StaticAnalyzerOutputType StaticAnalyzerOutputType => Inner.StaticAnalyzerOutputType;

		public StaticAnalyzerMode StaticAnalyzerMode => Inner.StaticAnalyzerMode;

		public int MinFilesUsingPrecompiledHeader => Inner.MinFilesUsingPrecompiledHeader;

		public bool bForcePrecompiledHeaderForGameModules => Inner.bForcePrecompiledHeaderForGameModules;

		public bool bUseIncrementalLinking => Inner.bUseIncrementalLinking;

		public bool bAllowLTCG => Inner.bAllowLTCG;

		public bool bPreferThinLTO => Inner.bPreferThinLTO;

		public bool bPGOProfile => Inner.bPGOProfile;

		public bool bPGOOptimize => Inner.bPGOOptimize;

		public bool bSupportEditAndContinue => Inner.bSupportEditAndContinue;

		public bool bCodeCoverage => Inner.bCodeCoverage;

		public bool bOmitFramePointers => Inner.bOmitFramePointers;

		public bool bEnableCppModules => Inner.bEnableCppModules;

		public bool bEnableCppCoroutinesForEvaluation => Inner.bEnableCppCoroutinesForEvaluation;

		public bool bEnableProcessPriorityControl => Inner.bEnableProcessPriorityControl;

		[Obsolete("Deprecated in UE5.3 - No longer used as MallocProfiler is removed in favor of UnrealInsights.")]
		public bool bUseMallocProfiler => Inner.bUseMallocProfiler;

		public bool bShaderCompilerWorkerTrace => Inner.bShaderCompilerWorkerTrace;

		public bool bUseSharedPCHs => Inner.bUseSharedPCHs;

		public bool bUseShippingPhysXLibraries => Inner.bUseShippingPhysXLibraries;

		public bool bUseCheckedPhysXLibraries => Inner.bUseCheckedPhysXLibraries;

		public bool bCheckLicenseViolations => Inner.bCheckLicenseViolations;

		public bool bBreakBuildOnLicenseViolation => Inner.bBreakBuildOnLicenseViolation;

		public bool? bUseFastPDBLinking => Inner.bUseFastPDBLinking;

		public bool bCreateMapFile => Inner.bCreateMapFile;

		public bool bAllowRuntimeSymbolFiles => Inner.bAllowRuntimeSymbolFiles;

		public string? PackagePath => Inner.PackagePath;

		public string? CrashDiagnosticDirectory => Inner.CrashDiagnosticDirectory;

		public string? ThinLTOCacheDirectory => Inner.ThinLTOCacheDirectory;

		public string? ThinLTOCachePruningArguments => Inner.ThinLTOCachePruningArguments;

		public string? BundleVersion => Inner.BundleVersion;

		public bool bDeployAfterCompile => Inner.bDeployAfterCompile;

		public bool bAllowRemotelyCompiledPCHs => Inner.bAllowRemotelyCompiledPCHs;

		public bool bCheckSystemHeadersForModification => Inner.bCheckSystemHeadersForModification;

		public bool bDisableLinking => Inner.bDisableLinking;

		public bool bIgnoreBuildOutputs => Inner.bIgnoreBuildOutputs;

		public bool bFormalBuild => Inner.bFormalBuild;

		public bool bUseAdaptiveUnityBuild => Inner.bUseAdaptiveUnityBuild;

		public bool bFlushBuildDirOnRemoteMac => Inner.bFlushBuildDirOnRemoteMac;

		public bool bPrintToolChainTimingInfo => Inner.bPrintToolChainTimingInfo;

		public bool bParseTimingInfoForTracing => Inner.bParseTimingInfoForTracing;

		public bool bPublicSymbolsByDefault => Inner.bPublicSymbolsByDefault;

		public string? ToolChainName => Inner.ToolChainName;

		public float MSVCCompileActionWeight => Inner.MSVCCompileActionWeight;

		public float ClangCompileActionWeight => Inner.ClangCompileActionWeight;

		public bool bLegacyPublicIncludePaths => Inner.bLegacyPublicIncludePaths;

		public bool bLegacyParentIncludePaths => Inner.bLegacyParentIncludePaths;

		public CppStandardVersion CppStandardEngine => Inner.CppStandardEngine;

		public CppStandardVersion CppStandard => Inner.CppStandard;

		public CStandardVersion CStandard => Inner.CStandard;

		public MinimumCpuArchitectureX64 MinCpuArchX64 => Inner.MinCpuArchX64;

		internal bool bNoManifestChanges => Inner.bNoManifestChanges;

		public string? BuildVersion => Inner.BuildVersion;

		public TargetLinkType LinkType => Inner.LinkType;

		public IReadOnlyList<string> GlobalDefinitions => Inner.GlobalDefinitions.AsReadOnly();

		public IReadOnlyList<string> ProjectDefinitions => Inner.ProjectDefinitions.AsReadOnly();

		public string? LaunchModuleName => Inner.LaunchModuleName;

		public string? ExportPublicHeader => Inner.ExportPublicHeader;

		public IReadOnlyList<string> ExtraModuleNames => Inner.ExtraModuleNames.AsReadOnly();

		public IReadOnlyList<FileReference> ManifestFileNames => Inner.ManifestFileNames.AsReadOnly();

		public IReadOnlyList<FileReference> DependencyListFileNames => Inner.DependencyListFileNames.AsReadOnly();

		public TargetBuildEnvironment BuildEnvironment => Inner.BuildEnvironment;

		public bool bOverrideBuildEnvironment => Inner.bOverrideBuildEnvironment;

		public IReadOnlyList<TargetInfo> PreBuildTargets => Inner.PreBuildTargets;

		public IReadOnlyList<string> PreBuildSteps => Inner.PreBuildSteps;

		public IReadOnlyList<string> PostBuildSteps => Inner.PostBuildSteps;

		public IReadOnlyList<string> AdditionalBuildProducts => Inner.AdditionalBuildProducts;

		public string? AdditionalCompilerArguments => Inner.AdditionalCompilerArguments;

		public string? AdditionalLinkerArguments => Inner.AdditionalLinkerArguments;

		public double MemoryPerActionGB => Inner.MemoryPerActionGB;

		public string? GeneratedProjectName => Inner.GeneratedProjectName;

		public ReadOnlyAndroidTargetRules AndroidPlatform
		{
			get;
			private set;
		}

		public ReadOnlyLinuxTargetRules LinuxPlatform
		{
			get;
			private set;
		}

		public ReadOnlyIOSTargetRules IOSPlatform
		{
			get;
			private set;
		}

		public ReadOnlyMacTargetRules MacPlatform
		{
			get;
			private set;
		}

		public ReadOnlyWindowsTargetRules WindowsPlatform
		{
			get;
			private set;
		}

		public bool bShouldCompileAsDLL => Inner.bShouldCompileAsDLL;

		public bool bGenerateProjectFiles => Inner.bGenerateProjectFiles;

		public bool bIsEngineInstalled => Inner.bIsEngineInstalled;

		public IReadOnlyList<string>? DisableUnityBuildForModules => Inner.DisableUnityBuildForModules;

		public IReadOnlyList<string>? EnableOptimizeCodeForModules => Inner.EnableOptimizeCodeForModules;

		public IReadOnlyList<string>? DisableOptimizeCodeForModules => Inner.DisableOptimizeCodeForModules;

		public IReadOnlyList<string>? OptimizeForSizeModules => Inner.OptimizeForSizeModules;

		public IReadOnlyList<string>? OptimizeForSizeAndSpeedModules => Inner.OptimizeForSizeAndSpeedModules;

		public IReadOnlyList<UnrealTargetPlatform>? OptedInModulePlatforms => Inner.OptedInModulePlatforms;

		public TestTargetRules? InnerTestTargetRules => Inner as TestTargetRules;

#pragma warning restore C1591
		#endregion

		/// <summary>
		/// Provide access to the RelativeEnginePath property for code referencing ModuleRules.BuildConfiguration.
		/// </summary>
		public string RelativeEnginePath => Unreal.EngineDirectory.MakeRelativeTo(DirectoryReference.GetCurrentDirectory()) + Path.DirectorySeparatorChar;

		/// <summary>
		/// Provide access to the UEThirdPartySourceDirectory property for code referencing ModuleRules.UEBuildConfiguration.
		/// </summary>
		public string UEThirdPartySourceDirectory => "ThirdParty/";

		/// <summary>
		/// Provide access to the UEThirdPartyBinariesDirectory property for code referencing ModuleRules.UEBuildConfiguration.
		/// </summary>
		public string UEThirdPartyBinariesDirectory => "../Binaries/ThirdParty/";

		/// <summary>
		/// Whether this is a low level tests target.
		/// </summary>
		public bool IsTestTarget => Inner.IsTestTarget;

		/// <summary>
		/// Whether this is a test target explicitly defined.
		/// Explicitley defined test targets always inherit from TestTargetRules and define their own tests.
		/// Implicit test targets are created from existing targets when building with -Mode=Test and they include tests from all dependencies.
		/// </summary>
		public bool ExplicitTestsTarget => Inner.ExplicitTestsTarget;

		/// <summary>
		/// Controls the value of WITH_LOW_LEVEL_TESTS that dictates whether module-specific low level tests are compiled in or not.
		/// </summary>
		public bool WithLowLevelTests => Inner.WithLowLevelTests;

		/// <summary>
		/// Checks if current platform is part of a given platform group
		/// </summary>
		/// <param name="Group">The platform group to check</param>
		/// <returns>True if current platform is part of a platform group</returns>
		public bool IsInPlatformGroup(UnrealPlatformGroup Group)
		{
			return UEBuildPlatform.IsPlatformInGroup(Platform, Group);
		}

		/// <summary>
		/// Gets diagnostic messages about default settings which have changed in newer versions of the engine
		/// </summary>
		/// <param name="Diagnostics">List of messages to be appended to</param>
		internal void GetBuildSettingsInfo(List<string> Diagnostics)
		{
			Inner.GetBuildSettingsInfo(Diagnostics);
		}

		public bool IsPlatformOptedIn(UnrealTargetPlatform Platform)
		{
			return Inner.OptedInModulePlatforms == null || Inner.OptedInModulePlatforms.Contains(Platform);
		}

		/// <summary>
		/// Determines if the automation tests should be compiled based on the current configuration and optional forced settings.
		/// If either the development tests or performance tests are compiled (unless explicitly disabled), this property returns true.
		/// </summary>
		private bool? bWithAutomationTestsPrivate = null;
		public bool WithAutomationTests
		{
			get
			{
				if (!bWithAutomationTestsPrivate.HasValue)
				{
					bool bCompileDevTests = Configuration != UnrealTargetConfiguration.Shipping;
					bool bCompilePerfTests = bCompileDevTests;

					if (bForceCompileDevelopmentAutomationTests)
					{
						bCompileDevTests = true;
					}
					if (bForceCompilePerformanceAutomationTests)
					{
						bCompilePerfTests = true;
					}
					if (bForceDisableAutomationTests)
					{
						bCompileDevTests = bCompilePerfTests = false;
					}

					bWithAutomationTestsPrivate = bCompileDevTests || bCompilePerfTests;
				}

				return bWithAutomationTestsPrivate.Value;
			}
		}
	}
}

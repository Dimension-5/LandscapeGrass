// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Linq;
using System.Text;
using EpicGames.Core;
using Microsoft.Extensions.Logging;
using UnrealBuildBase;

namespace UnrealBuildTool
{
	class MakefileProjectFile : ProjectFile
	{
		public MakefileProjectFile(FileReference InitFilePath, DirectoryReference BaseDir)
			: base(InitFilePath, BaseDir)
		{
		}
	}

	/// <summary>
	/// Makefile project file generator implementation
	/// </summary>
	class MakefileGenerator : ProjectFileGenerator
	{
		/// True if intellisense data should be generated (takes a while longer)
		/// Now this is needed for project target generation.
		bool bGenerateIntelliSenseData = true;

		/// Default constructor
		public MakefileGenerator(FileReference? InOnlyGameProject)
			: base(InOnlyGameProject)
		{
		}

		/// True if we should include IntelliSense data in the generated project files when possible
		public override bool ShouldGenerateIntelliSenseData()
		{
			return bGenerateIntelliSenseData;
		}

		/// File extension for project files we'll be generating (e.g. ".vcxproj")
		public override string ProjectFileExtension => ".mk";

		protected override bool WritePrimaryProjectFile(ProjectFile? UBTProject, PlatformProjectGeneratorCollection PlatformProjectGenerators, ILogger Logger)
		{
			bool bSuccess = true;
			return bSuccess;
		}

		private bool WriteMakefile(ILogger Logger)
		{
			string GameProjectFile = "";
			string BuildCommand = "";
			string ProjectBuildCommand = "";

			string MakeGameProjectFile = "";

			string UnrealRootPath = Unreal.RootDirectory.FullName;

			string EditorTarget = "UnrealEditor";

			if (!String.IsNullOrEmpty(GameProjectName))
			{
				GameProjectFile = OnlyGameProject!.FullName;
				MakeGameProjectFile = "GAMEPROJECTFILE =" + GameProjectFile + "\n";
				ProjectBuildCommand = $"PROJECTBUILD = \"$(UNREALROOTPATH)/Engine/{Unreal.RelativeDotnetDirectory}/dotnet\" \"$(UNREALROOTPATH)/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll\"\n";
				EditorTarget = GameProjectName + "Editor";
			}

			BuildCommand = "BUILD = bash \"$(UNREALROOTPATH)/Engine/Build/BatchFiles/Linux/Build.sh\"\n";

			string FileName = "Makefile"; // PrimaryProjectName + ".mk";
			StringBuilder MakefileContent = new StringBuilder();
			MakefileContent.Append(
				"# Makefile generated by MakefileGenerator.cs\n" +
				"# *DO NOT EDIT*\n\n" +
				"UNREALROOTPATH = " + UnrealRootPath + "\n" +
				MakeGameProjectFile + "\n" +
				"TARGETS ="
			);
			String MakeProjectCmdArg = "";
			String MakeBuildCommand = "";
			foreach (ProjectFile Project in GeneratedProjectFiles)
			{
				foreach (ProjectTarget TargetFile in Project.ProjectTargets.OfType<ProjectTarget>())
				{
					if (TargetFile.TargetFilePath == null)
					{
						continue;
					}

					string TargetFileName = TargetFile.TargetFilePath.GetFileNameWithoutExtension();
					string Basename = TargetFileName.Substring(0, TargetFileName.LastIndexOf(".Target", StringComparison.InvariantCultureIgnoreCase));

					foreach (UnrealTargetConfiguration CurConfiguration in (UnrealTargetConfiguration[])Enum.GetValues(typeof(UnrealTargetConfiguration)))
					{
						if (CurConfiguration != UnrealTargetConfiguration.Unknown && CurConfiguration != UnrealTargetConfiguration.Development)
						{
							if (InstalledPlatformInfo.IsValidConfiguration(CurConfiguration, EProjectType.Code))
							{
								string Confname = Enum.GetName(typeof(UnrealTargetConfiguration), CurConfiguration)!;
								MakefileContent.Append(String.Format(" \\\n\t{0}-Linux-{1} ", Basename, Confname));
							}
						}
					}
					MakefileContent.Append(" \\\n\t" + Basename);
				}
			}
			MakefileContent.Append("\\\n\tconfigure");

			MakefileContent.Append("\n\n" + BuildCommand + ProjectBuildCommand + "\n" +
				"all: StandardSet\n\n" +
				"RequiredTools: CrashReportClient-Linux-Shipping CrashReportClientEditor-Linux-Shipping ShaderCompileWorker UnrealLightmass EpicWebHelper-Linux-Shipping\n\n" +
				$"StandardSet: RequiredTools UnrealFrontend {EditorTarget} UnrealInsights\n\n" +
				$"DebugSet: RequiredTools UnrealFrontend-Linux-Debug {EditorTarget}-Linux-Debug\n\n"
			);

			foreach (ProjectFile Project in GeneratedProjectFiles)
			{
				foreach (ProjectTarget TargetFile in Project.ProjectTargets.OfType<ProjectTarget>())
				{
					if (TargetFile.TargetFilePath == null)
					{
						continue;
					}

					string TargetFileName = TargetFile.TargetFilePath.GetFileNameWithoutExtension();
					string Basename = TargetFileName.Substring(0, TargetFileName.LastIndexOf(".Target", StringComparison.InvariantCultureIgnoreCase));

					if (Basename == GameProjectName || Basename == (GameProjectName + "Editor"))
					{
						MakeProjectCmdArg = " -project=\"$(GAMEPROJECTFILE)\"";
						MakeBuildCommand = "$(PROJECTBUILD)";
					}
					else
					{
						MakeBuildCommand = "$(BUILD)";
					}

					foreach (UnrealTargetConfiguration CurConfiguration in (UnrealTargetConfiguration[])Enum.GetValues(typeof(UnrealTargetConfiguration)))
					{
						if (Basename == GameProjectName || Basename == (GameProjectName + "Editor"))
						{
							MakeProjectCmdArg = " -project=\"$(GAMEPROJECTFILE)\"";
							MakeBuildCommand = "$(PROJECTBUILD)";
						}
						else
						{
							MakeBuildCommand = "$(BUILD)";
						}

						if (CurConfiguration != UnrealTargetConfiguration.Unknown && CurConfiguration != UnrealTargetConfiguration.Development)
						{
							if (InstalledPlatformInfo.IsValidConfiguration(CurConfiguration, EProjectType.Code))
							{
								string Confname = Enum.GetName(typeof(UnrealTargetConfiguration), CurConfiguration)!;
								MakefileContent.Append(String.Format("\n{1}-Linux-{2}:\n\t {0} {1} Linux {2} {3} $(ARGS)\n", MakeBuildCommand, Basename, Confname, MakeProjectCmdArg));
							}
						}
					}
					MakefileContent.Append(String.Format("\n{1}:\n\t {0} {1} Linux Development {2} $(ARGS)\n", MakeBuildCommand, Basename, MakeProjectCmdArg));
				}
			}

			MakefileContent.Append("\nconfigure:\n");
			if (!String.IsNullOrEmpty(GameProjectName))
			{
				// Make sure UBT is updated.
				MakefileContent.Append("\txbuild /property:Configuration=Development /verbosity:quiet /nologo ");
				MakefileContent.Append("\"$(UNREALROOTPATH)/Engine/Source/Programs/UnrealBuildTool/UnrealBuildTool.csproj\"\n");
				MakefileContent.Append("\t$(PROJECTBUILD) -projectfiles -project=\"\\\"$(GAMEPROJECTFILE)\\\"\" -game -engine \n");
			}
			else
			{
				MakefileContent.Append("\tbash \"$(UNREALROOTPATH)/GenerateProjectFiles.sh\" \n");
			}

			MakefileContent.Append("\n.PHONY: $(TARGETS)\n");
			FileReference FullFileName = FileReference.Combine(PrimaryProjectPath, FileName);
			return WriteFileIfChanged(FullFileName.FullName, MakefileContent.ToString(), Logger);
		}

		/// ProjectFileGenerator interface
		//protected override bool WritePrimaryProjectFile( ProjectFile UBTProject )
		protected override bool WriteProjectFiles(PlatformProjectGeneratorCollection PlatformProjectGenerators, ILogger Logger)
		{
			return WriteMakefile(Logger);
		}

		/// ProjectFileGenerator interface
		/// <summary>
		/// Allocates a generator-specific project file object
		/// </summary>
		/// <param name="InitFilePath">Path to the project file</param>
		/// <param name="BaseDir">The base directory for files within this project</param>
		/// <returns>The newly allocated project file object</returns>
		protected override ProjectFile AllocateProjectFile(FileReference InitFilePath, DirectoryReference BaseDir)
		{
			return new MakefileProjectFile(InitFilePath, BaseDir);
		}

		/// ProjectFileGenerator interface
		public override void CleanProjectFiles(DirectoryReference InPrimaryProjectDirectory, string InPrimaryProjectName, DirectoryReference InIntermediateProjectFilesDirectory, ILogger Logger)
		{
		}
	}
}

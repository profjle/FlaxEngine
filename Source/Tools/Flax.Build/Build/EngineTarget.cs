// Copyright (c) 2012-2023 Wojciech Figat. All rights reserved.

using System;
using System.IO;
using Flax.Build.Graph;
using Flax.Build.NativeCpp;

namespace Flax.Build
{
    /// <summary>
    /// The build target that builds the engine (eg. as game player or editor).
    /// </summary>
    /// <seealso cref="Flax.Build.ProjectTarget" />
    public class EngineTarget : ProjectTarget
    {
        private static Version _engineVersion;

        /// <summary>
        /// Gets the engine project.
        /// </summary>
        public static ProjectInfo EngineProject => ProjectInfo.Load(Path.Combine(Globals.EngineRoot, "Flax.flaxproj"));

        /// <summary>
        /// Gets the engine version.
        /// </summary>
        public static Version EngineVersion
        {
            get
            {
                if (_engineVersion == null)
                {
                    _engineVersion = EngineProject.Version;
                    Log.Verbose(string.Format("Engine build version: {0}", _engineVersion));
                }
                return _engineVersion;
            }
        }

        /// <inheritdoc />
        public override void Init()
        {
            base.Init();

            // Merge all modules from engine source into a single executable and single C# library
            LinkType = TargetLinkType.Monolithic;

            Modules.Add("Main");
            Modules.Add("Engine");
        }

        /// <inheritdoc />
        public override string GetOutputFilePath(BuildOptions options, TargetOutputType? outputType)
        {
            // If building engine executable for platform doesn't support referencing it when linking game shared libraries
            if (outputType == null && UseSeparateMainExecutable(options))
            {
                // Build into shared library
                outputType = TargetOutputType.Library;
            }

            return base.GetOutputFilePath(options, outputType);
        }

        /// <inheritdoc />
        public override void SetupTargetEnvironment(BuildOptions options)
        {
            base.SetupTargetEnvironment(options);

            // If building engine executable for platform doesn't support referencing it when linking game shared libraries
            if (UseSeparateMainExecutable(options))
            {
                // Build into shared library
                options.LinkEnv.Output = LinkerOutput.SharedLibrary;
            }
        }

        /// <inheritdoc />
        public override void PreBuild(TaskGraph graph, BuildOptions buildOptions)
        {
            // If building engine executable for platform doesn't support referencing it when linking game shared libraries
            if (UseSeparateMainExecutable(buildOptions))
            {
                // Don't link Main module into shared library
                Modules.Remove("Main");
            }

            base.PreBuild(graph, buildOptions);
        }

        /// <inheritdoc />
        public override void PostBuild(TaskGraph graph, BuildOptions buildOptions)
        {
            base.PostBuild(graph, buildOptions);

            // If building engine executable for platform doesn't support referencing it when linking game shared libraries
            if (UseSeparateMainExecutable(buildOptions))
            {
                // Build additional executable with Main module only that uses shared library
                using (new ProfileEventScope("BuildExecutable"))
                {
                    BuildMainExecutable(graph, buildOptions);
                }

                // Restore state from PreBuild
                Modules.Add("Main");
            }

            // Mono on Linux is using dynamic linking and needs additional link files
            if (buildOptions.Platform.Target == TargetPlatform.Linux && Platform.BuildTargetPlatform == TargetPlatform.Linux && !IsPreBuilt)
            {
                var task = graph.Add<Task>();
                task.PrerequisiteFiles.Add(Path.Combine(buildOptions.OutputFolder, "libmonosgen-2.0.so"));
                task.ProducedFiles.Add(Path.Combine(buildOptions.OutputFolder, "libmonosgen-2.0.so.1"));
                task.WorkingDirectory = buildOptions.OutputFolder;
                task.CommandPath = "ln";
                task.CommandArguments = "-s -f libmonosgen-2.0.so libmonosgen-2.0.so.1";
                task = graph.Add<Task>();
                task.PrerequisiteFiles.Add(Path.Combine(buildOptions.OutputFolder, "libmonosgen-2.0.so"));
                task.ProducedFiles.Add(Path.Combine(buildOptions.OutputFolder, "libmonosgen-2.0.so.1.0.0"));
                task.WorkingDirectory = buildOptions.OutputFolder;
                task.CommandPath = "ln";
                task.CommandArguments = "-s -f libmonosgen-2.0.so libmonosgen-2.0.so.1.0.0";
            }
        }

        private bool UseSeparateMainExecutable(BuildOptions buildOptions)
        {
            return UseSymbolsExports && OutputType == TargetOutputType.Executable && !buildOptions.Platform.HasExecutableFileReferenceSupport && !Configuration.BuildBindingsOnly;
        }

        private void BuildMainExecutable(TaskGraph graph, BuildOptions buildOptions)
        {
            if (IsPreBuilt)
                return;
            var outputPath = Path.Combine(buildOptions.OutputFolder, buildOptions.Platform.GetLinkOutputFileName(OutputName, LinkerOutput.Executable));
            var exeBuildOptions = Builder.GetBuildOptions(this, buildOptions.Platform, buildOptions.Toolchain, buildOptions.Architecture, buildOptions.Configuration, buildOptions.WorkingDirectory);
            exeBuildOptions.LinkEnv.Output = LinkerOutput.Executable;
            var rules = Builder.GenerateRulesAssembly();
            var buildData = new Builder.BuildData
            {
                Rules = rules,
                Target = this,
                Graph = graph,
                TargetOptions = exeBuildOptions,
                Platform = buildOptions.Platform,
                Toolchain = buildOptions.Toolchain,
                Architecture = buildOptions.Architecture,
                Configuration = buildOptions.Configuration,
            };

            // Build Main module
            var mainModule = rules.GetModule("Main");
            var mainModuleOutputPath = Path.Combine(exeBuildOptions.IntermediateFolder, mainModule.Name);
            if (!Directory.Exists(mainModuleOutputPath))
                Directory.CreateDirectory(mainModuleOutputPath);
            var mainModuleOptions = new BuildOptions
            {
                Target = this,
                Platform = buildOptions.Platform,
                Toolchain = buildOptions.Toolchain,
                Architecture = buildOptions.Architecture,
                Configuration = buildOptions.Configuration,
                CompileEnv = (CompileEnvironment)exeBuildOptions.CompileEnv.Clone(),
                LinkEnv = (LinkEnvironment)exeBuildOptions.LinkEnv.Clone(),
                IntermediateFolder = mainModuleOutputPath,
                OutputFolder = mainModuleOutputPath,
                WorkingDirectory = exeBuildOptions.WorkingDirectory,
                HotReloadPostfix = exeBuildOptions.HotReloadPostfix,
            };
            mainModuleOptions.SourcePaths.Add(mainModule.FolderPath);
            mainModule.Setup(mainModuleOptions);
            mainModuleOptions.MergeSourcePathsIntoSourceFiles();
            mainModuleOptions.CompileEnv.PreprocessorDefinitions.Add("FLAXENGINE_API=" + buildOptions.Toolchain.DllImport);
            Builder.BuildModuleInner(buildData, mainModule, mainModuleOptions, false);

            // Link executable
            exeBuildOptions.LinkEnv.InputLibraries.Add(Path.Combine(buildOptions.OutputFolder, buildOptions.Platform.GetLinkOutputFileName(OutputName, LinkerOutput.SharedLibrary)));
            exeBuildOptions.LinkEnv.InputFiles.AddRange(mainModuleOptions.OutputFiles);
            exeBuildOptions.DependencyFiles.AddRange(mainModuleOptions.DependencyFiles);
            exeBuildOptions.OptionalDependencyFiles.AddRange(mainModuleOptions.OptionalDependencyFiles);
            exeBuildOptions.Libraries.AddRange(mainModuleOptions.Libraries);
            exeBuildOptions.DelayLoadLibraries.AddRange(mainModuleOptions.DelayLoadLibraries);
            exeBuildOptions.ScriptingAPI.Add(mainModuleOptions.ScriptingAPI);
            exeBuildOptions.ExternalModules.AddRange(mainModuleOptions.ExternalModules);
            buildOptions.Toolchain.LinkFiles(graph, exeBuildOptions, outputPath);
        }
    }
}

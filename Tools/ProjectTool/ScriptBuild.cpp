#include "ScriptBuild.h"
#include "ProjectFiles.h"
#include <Windows.h>
#include <algorithm>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace ProjectTool
{
    namespace
    {
        using Hashes = std::map<Path, std::string>;

        std::wstring Upper(std::wstring value)
        {
            if (value.empty())
            {
                return value;
            }
            const auto length = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, value.data(),
                                              static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr, 0);
            if (!length)
            {
                throw std::runtime_error("Cannot normalize script build identity.");
            }
            std::wstring result(static_cast<size_t>(length), L'\0');
            if (!LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, value.data(), static_cast<int>(value.size()),
                               result.data(), length, nullptr, nullptr, 0))
            {
                throw std::runtime_error("Cannot normalize script build identity.");
            }
            return result;
        }

        std::vector<Path> TrackedFiles(const Path& folder)
        {
            std::vector<Path> files;
            if (!std::filesystem::exists(folder))
            {
                return files;
            }
            for (const auto& file : std::filesystem::recursive_directory_iterator(folder))
            {
                const auto extension = Upper(file.path().extension().wstring());
                if (file.is_regular_file() && (extension == L".CPP" || extension == L".H" || extension == L".HPP"))
                {
                    files.push_back(file.path());
                }
            }
            std::sort(files.begin(), files.end());
            return files;
        }

        Hashes CaptureHashes(const std::vector<Path>& files)
        {
            Hashes hashes;
            for (const auto& file : files)
            {
                hashes.emplace(file, Sha256(file));
            }
            return hashes;
        }

        std::vector<Path> SdkInputs(const Path& root)
        {
            std::set<Path> files;
            const std::set<std::wstring> extensions{L".H", L".HPP", L".INL", L".C", L".CPP", L".TARGETS", L".PROPS"};
            for (const auto* folder :
                 {"Engine/include", "Engine/src", "Engine/build", "Engine/externals", "SceneRuntime/include",
                  "SceneRuntime/src", "SceneRuntime/build", "Tools/ProjectTool", "generated/dependencies/jolt-src/Jolt",
                  "generated/dependencies/assimp-src/include", "generated/dependencies/assimp-src/code",
                  "generated/dependencies/assimp-src/contrib", "generated/dependencies/directxtex-src/DirectXTex",
                  "generated/dependencies/recast-src/Recast", "generated/dependencies/recast-src/Detour"})
            {
                const auto path = root / folder;
                if (!std::filesystem::exists(path))
                {
                    continue;
                }
                for (const auto& file : std::filesystem::recursive_directory_iterator(path))
                {
                    if (file.is_regular_file() && extensions.contains(Upper(file.path().extension().wstring())))
                    {
                        files.insert(file.path());
                    }
                }
            }
            for (const auto* relative :
                 {"Engine/Engine.vcxproj", "Engine/EngineCore.vcxproj", "Engine/EngineAssets.vcxproj",
                  "Engine/EnginePlatform.vcxproj", "SceneRuntime/SceneRuntime.vcxproj",
                  "SceneRuntime/SceneData.vcxproj", "SceneRuntime/SceneRendering.vcxproj",
                  "SceneRuntime/SceneTitle.vcxproj", "SceneRuntime/SceneSimulation.vcxproj",
                  "SceneRuntime/SceneStorage.vcxproj", "SceneRuntime/SceneRuntimeCore.vcxproj",
                  "Tools/ProjectTool/ProjectTool.vcxproj", "scripts/GenerateEngineIdentity.ps1",
                  "scripts/EngineBuildInputs.ps1", "scripts/BuildAssimp.ps1", "scripts/BuildJolt.ps1",
                  "scripts/BuildDirectXTex.ps1", "scripts/BuildRecast.ps1"})
            {
                const auto path = root / relative;
                if (std::filesystem::is_regular_file(path))
                {
                    files.insert(path);
                }
            }
            return {files.begin(), files.end()};
        }

        std::vector<std::wstring> BuildArguments(const Path& project, const std::string& configuration)
        {
            return {project.wstring(),
                    L"/p:Configuration=" + Wide(configuration),
                    L"/p:Platform=x64",
                    L"/p:PreferredToolArchitecture=x64",
                    L"/m:1",
                    L"/nr:false",
                    L"/v:minimal",
                    L"/nologo"};
        }

        std::string AggregateSource(const std::vector<Path>& sources)
        {
            std::ostringstream source;
            source << "#include <SceneRuntime/Scripts/ScriptModuleApi.h>\r\n";
            for (const auto& file : sources)
            {
                source << "bool Register_" << Utf8(file.stem()) << "(void*,SceneRuntime::RegisterModuleScript);\r\n";
            }
            source
                << "extern \"C\" __declspec(dllexport) bool Wp1RegisterScripts("
                   "const SceneRuntime::ScriptModuleAbi* abi,void* context,SceneRuntime::RegisterModuleScript add)\r\n"
                   "{\r\n    if (!abi || !add || *abi!=SceneRuntime::ScriptModuleAbi{}) return false;\r\n";
            for (const auto& file : sources)
            {
                source << "    if (!Register_" << Utf8(file.stem()) << "(context,add)) return false;\r\n";
            }
            source << "    return true;\r\n}\r\n";
            return source.str();
        }

        std::string ScriptProject(const Options& options, const Path& workspace, const std::vector<Path>& sources,
                                  const Path& engineLibrary, const Path& sceneLibrary, const Path& coreLibrary,
                                  const Path& assetsLibrary, const Path& platformLibrary)
        {
            const bool debug = options.configuration == "Debug";
            std::string definitions = debug ? "_DEBUG;_WINDOWS" : "NDEBUG;_WINDOWS";
            if (options.configuration == "Development")
            {
                definitions += ";ENGINE_DEVELOPMENT";
            }
            const auto escaped = [](const Path& path)
            {
                return XmlEscape(Utf8(path));
            };
            std::ostringstream project;
            project
                << "<Project DefaultTargets=\"Build\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\r\n"
                   "  <ItemGroup Label=\"ProjectConfigurations\"><ProjectConfiguration Include=\""
                << options.configuration << "|x64\"><Configuration>" << options.configuration
                << "</Configuration><Platform>x64</Platform></ProjectConfiguration></ItemGroup>\r\n"
                   "  <PropertyGroup Label=\"Globals\"><VCProjectVersion>18.0</VCProjectVersion>"
                   "<WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion></PropertyGroup>\r\n"
                   "  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.Default.props\" />\r\n"
                   "  <PropertyGroup Label=\"Configuration\"><ConfigurationType>DynamicLibrary</ConfigurationType>"
                   "<PlatformToolset>v145</PlatformToolset><UseDebugLibraries>"
                << (debug ? "true" : "false")
                << "</UseDebugLibraries></PropertyGroup>\r\n"
                   "  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.props\" />\r\n"
                   "  <PropertyGroup><OutDir>"
                << escaped(workspace / "bin") << "/</OutDir><IntDir>" << escaped(workspace / "obj")
                << "/</IntDir><TargetName>GameScripts-" << options.runId
                << "</TargetName><LinkIncremental>false</LinkIncremental></PropertyGroup>\r\n"
                   "  <ItemDefinitionGroup><ClCompile><LanguageStandard>stdcpp20</LanguageStandard>"
                   "<ExceptionHandling>Sync</ExceptionHandling><DebugInformationFormat>ProgramDatabase</DebugInformationFormat>"
                   "<ConformanceMode>true</ConformanceMode><RuntimeLibrary>"
                << (debug ? "MultiThreadedDebugDLL" : "MultiThreadedDLL")
                << "</RuntimeLibrary><PreprocessorDefinitions>" << definitions
                << ";%(PreprocessorDefinitions)</PreprocessorDefinitions><AdditionalIncludeDirectories>"
                << escaped(options.engine / "SceneRuntime/include") << ";" << escaped(options.engine / "Engine/include")
                << ";" << escaped(options.engine / "Engine/externals/imgui")
                << ";%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>"
                   "<AdditionalOptions>/utf-8 /bigobj %(AdditionalOptions)</AdditionalOptions>"
                   "<WarningLevel>Level4</WarningLevel><TreatWarningAsError>true</TreatWarningAsError></ClCompile>"
                   "<Link><AdditionalDependencies>"
                << escaped(sceneLibrary) << ";" << escaped(engineLibrary) << ";" << escaped(coreLibrary) << ";"
                << escaped(assetsLibrary) << ";" << escaped(platformLibrary)
                << ";%(AdditionalDependencies)</AdditionalDependencies><GenerateDebugInformation>true</GenerateDebugInformation>"
                   "<TreatLinkerWarningAsErrors>true</TreatLinkerWarningAsErrors></Link></ItemDefinitionGroup>\r\n"
                   "  <ItemGroup><ClCompile Include=\""
                << escaped(workspace / "ScriptModule.cpp") << "\" />";
            for (const auto& file : sources)
            {
                project << "<ClCompile Include=\"" << escaped(file) << "\" />";
            }
            project
                << "</ItemGroup>\r\n  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.targets\" />\r\n</Project>\r\n";
            return project.str();
        }
    }

    ScriptBuildSession::ScriptBuildSession(const Path& output, std::string_view configuration, unsigned timeoutMs)
    {
        auto identity = Upper(std::filesystem::absolute(output).lexically_normal().wstring());
        while (!identity.empty() && (identity.back() == L'\\' || identity.back() == L'/'))
        {
            identity.pop_back();
        }
        identity += L"|" + Wide(configuration);
        const auto name = L"Local\\WP1.ScriptBuild." + Wide(Sha256Text(Utf8(Path(identity))));
        const auto mutex = CreateMutexW(nullptr, FALSE, name.c_str());
        if (!mutex)
        {
            throw std::runtime_error("Cannot create script build session.");
        }
        const auto state = WaitForSingleObject(mutex, timeoutMs);
        if (state != WAIT_OBJECT_0 && state != WAIT_ABANDONED)
        {
            CloseHandle(mutex);
            throw std::runtime_error(state == WAIT_TIMEOUT
                                         ? "Timed out waiting for another script build for this output."
                                         : "Cannot enter script build session.");
        }
        mutex_ = mutex;
    }

    ScriptBuildSession::~ScriptBuildSession()
    {
        if (mutex_)
        {
            ReleaseMutex(mutex_);
            CloseHandle(mutex_);
        }
    }

    void PublishScriptManifest(const Path& path, const Json& manifest)
    {
        WriteJsonAtomic(path, manifest);
    }

    void BuildScripts(const Options& input)
    {
        auto options = input;
        if (options.runId.empty())
        {
            options.runId = NewId();
        }
        if (!std::regex_match(options.runId, std::regex("^[a-zA-Z0-9_-]{1,64}$")))
        {
            throw std::runtime_error("Invalid script build run ID.");
        }
        if (options.configuration != "Debug" && options.configuration != "Development" &&
            options.configuration != "Release")
        {
            throw std::runtime_error("Invalid script build configuration.");
        }
        options.engine = std::filesystem::absolute(options.engine).lexically_normal();
        options.content = std::filesystem::absolute(options.content).lexically_normal();
        options.output =
            options.output.empty() ? options.content : std::filesystem::absolute(options.output).lexically_normal();
        options.work =
            options.work.empty() ? options.engine : std::filesystem::absolute(options.work).lexically_normal();
        const auto buildRoot = options.work / "generated/script-builds";
        const auto workspace = buildRoot / options.runId;
        const auto resultPath = buildRoot / (options.runId + ".json");
        const auto logPath = buildRoot / (options.runId + ".log");
        std::filesystem::create_directories(workspace);
        WriteText(logPath, "Script module build started.\r\n");
        try
        {
            if (std::filesystem::exists(options.content / "WP1.project.json"))
            {
                LoadProject(options.content, options.engine);
            }
            const ScriptBuildSession scriptSession(options.output, options.configuration);
            const ScriptBuildSession nativeSession(options.engine, options.configuration + "-native");
            Progress(options, true, 0, "ゲーム処理ソースを確認中");
            const auto sourceRoot = options.content / "Assets/Scripts";
            const auto tracked = TrackedFiles(sourceRoot);
            std::vector<Path> sources;
            std::set<std::wstring> names;
            for (const auto& file : tracked)
            {
                if (Upper(file.extension().wstring()) != L".CPP")
                {
                    continue;
                }
                if (!std::regex_match(Utf8(file.stem()), std::regex("^[a-zA-Z_][a-zA-Z0-9_]{0,63}$")))
                {
                    throw std::runtime_error("Script filename must be a C++ identifier: " + Utf8(file.filename()));
                }
                if (!names.emplace(Upper(file.stem().wstring())).second)
                {
                    throw std::runtime_error("Script source base names must be unique across folders.");
                }
                sources.push_back(file);
            }
            if (sources.size() > 256)
            {
                throw std::runtime_error("At most 256 script source files are supported.");
            }
            const auto before = CaptureHashes(tracked);
            const auto outputFolder = options.output / "Assets/Scripts/Bin" / options.configuration;
            std::filesystem::create_directories(outputFolder);
            const auto manifestPath = outputFolder / "module.json";
            if (sources.empty())
            {
                PublishScriptManifest(manifestPath, {{"version", 1},
                                                     {"file", ""},
                                                     {"configuration", options.configuration},
                                                     {"sources", Json::object()}});
            }
            else
            {
                const auto engineLibrary =
                    options.engine / "generated/outputs/x64" / options.configuration / "Engine/Engine.lib";
                const auto sceneLibrary =
                    options.engine / "generated/outputs/x64" / options.configuration / "SceneRuntime/SceneRuntime.lib";
                const auto coreLibrary =
                    options.engine / "generated/outputs/x64" / options.configuration / "EngineCore/EngineCore.lib";
                const auto assetsLibrary =
                    options.engine / "generated/outputs/x64" / options.configuration / "EngineAssets/EngineAssets.lib";
                const auto platformLibrary = options.engine / "generated/outputs/x64" / options.configuration /
                                             "EnginePlatform/EnginePlatform.lib";
                const auto msbuild = FindMsbuild();
                if (RunProcess(
                        msbuild,
                        BuildArguments(options.engine / "SceneRuntime/SceneRuntime.vcxproj", options.configuration),
                        options.engine, logPath) != 0)
                {
                    throw std::runtime_error("Native SDK dependency build failed; the previous module was retained.");
                }
                const auto nativeBefore =
                    CaptureHashes({engineLibrary, sceneLibrary, coreLibrary, assetsLibrary, platformLibrary});
                const auto sdkBefore = CaptureHashes(SdkInputs(options.engine));
                WriteText(workspace / "ScriptModule.cpp", AggregateSource(sources));
                const auto projectPath = workspace / "GameScripts.vcxproj";
                WriteText(projectPath, ScriptProject(options, workspace, sources, engineLibrary, sceneLibrary,
                                                     coreLibrary, assetsLibrary, platformLibrary));
                Progress(options, true, 1, "C++ゲーム処理をコンパイル中");
                if (RunProcess(msbuild, BuildArguments(projectPath, options.configuration), options.engine, logPath) !=
                    0)
                {
                    throw std::runtime_error("Script compilation failed; see " + Utf8(logPath));
                }
                if (before != CaptureHashes(TrackedFiles(sourceRoot)))
                {
                    throw std::runtime_error(
                        "Script sources changed during compilation; the previous module was retained.");
                }
                if (nativeBefore !=
                    CaptureHashes({engineLibrary, sceneLibrary, coreLibrary, assetsLibrary, platformLibrary}))
                {
                    throw std::runtime_error(
                        "Native SDK libraries changed during script compilation; the previous module was retained.");
                }
                if (sdkBefore != CaptureHashes(SdkInputs(options.engine)))
                {
                    throw std::runtime_error(
                        "Native SDK inputs changed during script compilation; the previous module was retained.");
                }
                const auto filename = "GameScripts-" + options.runId + ".dll";
                Progress(options, true, 3, "コンパイル済み処理を保存中");
                // 同じ実行IDの成果物を上書きせず、読み込み済みDLLと公開済み世代を保護します。
                std::filesystem::copy_file(workspace / "bin" / filename, outputFolder / filename);
                const auto symbols = workspace / "bin" / ("GameScripts-" + options.runId + ".pdb");
                if (std::filesystem::is_regular_file(symbols))
                {
                    std::filesystem::copy_file(symbols, outputFolder / symbols.filename());
                }
                Json publicHashes = Json::object();
                for (const auto& [path, hash] : before)
                {
                    publicHashes[Utf8(path.lexically_relative(sourceRoot).generic_wstring())] = hash;
                }
                Json nativeHashes = Json::object();
                for (const auto& [path, hash] : nativeBefore)
                {
                    nativeHashes[Utf8(path.filename())] = hash;
                }
                PublishScriptManifest(manifestPath, {{"version", 1},
                                                     {"file", filename},
                                                     {"configuration", options.configuration},
                                                     {"sources", publicHashes},
                                                     {"nativeLibraries", nativeHashes}});
            }
            WriteJsonAtomic(resultPath, {{"success", true},
                                         {"configuration", options.configuration},
                                         {"manifest", Utf8(manifestPath)},
                                         {"log", Utf8(logPath)}});
            Progress(options, true, 4, "コンパイル完了");
        }
        catch (const std::exception& error)
        {
            std::ofstream log(logPath, std::ios::binary | std::ios::app);
            log << error.what() << "\r\n";
            WriteJsonAtomic(resultPath, {{"success", false}, {"error", error.what()}, {"log", Utf8(logPath)}});
            throw;
        }
    }
}

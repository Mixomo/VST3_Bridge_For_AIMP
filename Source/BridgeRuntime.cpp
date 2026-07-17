#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "BridgeRuntime.h"

#include <set>

namespace bridge
{
namespace
{
    juce::String normalise(const juce::String& value)
    {
        return value.replaceCharacter('/', '\\').trimCharactersAtEnd("\\");
    }

    bool isWithin(const juce::File& child, const juce::File& parent)
    {
        const auto childPath = normalise(child.getFullPathName());
        const auto parentPath = normalise(parent.getFullPathName());
        return childPath.equalsIgnoreCase(parentPath)
            || childPath.startsWithIgnoreCase(parentPath + "\\");
    }

    juce::String relativeTo(const juce::File& child, const juce::File& parent)
    {
        return normalise(child.getRelativePathFrom(parent));
    }

    bool canWriteDirectory(const juce::File& directory)
    {
        if (!directory.createDirectory())
            return false;
        const auto probe = directory.getNonexistentChildFile(".vst3_bridge_write_test", ".tmp", false);
        if (!probe.replaceWithText("ok"))
            return false;
        return probe.deleteFile();
    }

    juce::String storageModeName(StorageMode mode)
    {
        if (mode == StorageMode::portable) return "portable";
        if (mode == StorageMode::userProfile) return "userProfile";
        return "automatic";
    }

    StorageMode parseStorageMode(const juce::String& value)
    {
        if (value == "portable") return StorageMode::portable;
        if (value == "userProfile") return StorageMode::userProfile;
        return StorageMode::automatic;
    }

    juce::var folderToVar(const ScanFolder& folder)
    {
        auto* object = new juce::DynamicObject();
        object->setProperty("location", folder.location.toVar());
        object->setProperty("enabled", folder.enabled);
        object->setProperty("recursive", folder.recursive);
        return juce::var(object);
    }

    ScanFolder folderFromVar(const juce::var& value)
    {
        ScanFolder result;
        if (auto* object = value.getDynamicObject())
        {
            result.location = PathReference::fromVar(object->getProperty("location"));
            result.enabled = static_cast<bool>(object->getProperty("enabled"));
            result.recursive = static_cast<bool>(object->getProperty("recursive"));
        }
        return result;
    }

    juce::var pluginToVar(const PluginRecord& plugin)
    {
        auto* object = new juce::DynamicObject();
        object->setProperty("location", plugin.location.toVar());
        object->setProperty("canonicalPath", plugin.canonicalPath);
        object->setProperty("name", plugin.name);
        object->setProperty("descriptiveName", plugin.descriptiveName);
        object->setProperty("manufacturer", plugin.manufacturer);
        object->setProperty("version", plugin.version);
        object->setProperty("category", plugin.category);
        object->setProperty("uid", plugin.uid);
        object->setProperty("architecture", architectureName(plugin.architecture));
        object->setProperty("fingerprint", plugin.fingerprint);
        object->setProperty("status", plugin.status);
        object->setProperty("lastError", plugin.lastError);
        object->setProperty("scannerQuarantined", plugin.scannerQuarantined);
        object->setProperty("runtimeQuarantined", plugin.runtimeQuarantined);
        object->setProperty("instrument", plugin.instrument);
        object->setProperty("numInputChannels", plugin.numInputChannels);
        object->setProperty("numOutputChannels", plugin.numOutputChannels);
        object->setProperty("sharedContainer", plugin.sharedContainer);
        object->setProperty("araExtension", plugin.araExtension);
        object->setProperty("lastScanTime", plugin.lastScanTime);
        object->setProperty("state", plugin.state);
        return juce::var(object);
    }

    PluginRecord pluginFromVar(const juce::var& value)
    {
        PluginRecord result;
        if (auto* object = value.getDynamicObject())
        {
            result.location = PathReference::fromVar(object->getProperty("location"));
            result.canonicalPath = object->getProperty("canonicalPath").toString();
            result.name = object->getProperty("name").toString();
            result.descriptiveName = object->getProperty("descriptiveName").toString();
            result.manufacturer = object->getProperty("manufacturer").toString();
            result.version = object->getProperty("version").toString();
            result.category = object->getProperty("category").toString();
            result.uid = object->getProperty("uid").toString();
            result.architecture = parseArchitecture(object->getProperty("architecture").toString());
            result.fingerprint = object->getProperty("fingerprint").toString();
            result.status = object->getProperty("status").toString();
            result.lastError = object->getProperty("lastError").toString();
            result.scannerQuarantined = static_cast<bool>(object->getProperty("scannerQuarantined"));
            result.runtimeQuarantined = static_cast<bool>(object->getProperty("runtimeQuarantined"));
            result.instrument = static_cast<bool>(object->getProperty("instrument"));
            result.numInputChannels = static_cast<int>(object->getProperty("numInputChannels"));
            result.numOutputChannels = static_cast<int>(object->getProperty("numOutputChannels"));
            result.sharedContainer = static_cast<bool>(object->getProperty("sharedContainer"));
            result.araExtension = static_cast<bool>(object->getProperty("araExtension"));
            result.lastScanTime = static_cast<std::int64_t>(object->getProperty("lastScanTime"));
            result.state = object->getProperty("state").toString();
        }
        return result;
    }

    void enumerateFolder(const juce::File& folder, bool recursive, juce::Array<juce::File>& output,
                         std::set<juce::String>& visited, std::atomic_bool* cancelled)
    {
        if (cancelled != nullptr && cancelled->load()) return;
        const auto canonical = canonicalPath(folder).toLowerCase();
        if (canonical.isEmpty() || !visited.insert(canonical).second) return;

        juce::Array<juce::File> children;
        folder.findChildFiles(children, juce::File::findFilesAndDirectories, false);
        for (const auto& child : children)
        {
            if (cancelled != nullptr && cancelled->load()) return;
            if (child.hasFileExtension("vst3"))
            {
                output.addIfNotAlreadyThere(child);
                continue;
            }
            if (recursive && child.isDirectory() && !child.isSymbolicLink())
                enumerateFolder(child, true, output, visited, cancelled);
        }
    }
}

juce::String architectureName(Architecture value)
{
    if (value == Architecture::x86) return "x86";
    if (value == Architecture::x64) return "x64";
    if (value == Architecture::multi) return "multi";
    return "unknown";
}

Architecture parseArchitecture(const juce::String& value)
{
    if (value == "x86") return Architecture::x86;
    if (value == "x64") return Architecture::x64;
    if (value == "multi") return Architecture::multi;
    return Architecture::unknown;
}

Architecture currentArchitecture()
{
    return sizeof(void*) == 8 ? Architecture::x64 : Architecture::x86;
}

Architecture detectBundleArchitecture(const juce::File& bundle)
{
    juce::Array<juce::File> files;
    if (bundle.isDirectory())
        bundle.findChildFiles(files, juce::File::findFiles, true, "*.dll;*.vst3");
    else
        files.add(bundle);

    bool found32 = false, found64 = false;
    for (const auto& file : files)
    {
        juce::FileInputStream stream(file);
        if (!stream.openedOk() || stream.getTotalLength() < 64) continue;
        if (stream.readShort() != 0x5a4d) continue;
        stream.setPosition(0x3c);
        const auto peOffset = static_cast<uint32_t>(stream.readInt());
        if (peOffset + 6 >= static_cast<uint64_t>(stream.getTotalLength())) continue;
        stream.setPosition(peOffset);
        if (static_cast<uint32_t>(stream.readInt()) != 0x00004550) continue;
        const auto machine = static_cast<uint16_t>(stream.readShort());
        found32 |= machine == 0x014c;
        found64 |= machine == 0x8664;
    }
    if (found32 && found64) return Architecture::multi;
    if (found64) return Architecture::x64;
    if (found32) return Architecture::x86;
    return Architecture::unknown;
}

RuntimePaths RuntimePaths::detect(const juce::File& executablePath, const juce::File& packageHint,
                                  const juce::File& profileHint, StorageMode requestedMode)
{
    RuntimePaths result;
    result.executable = executablePath;
    result.binaryDirectory = executablePath.getParentDirectory();
    result.packageRoot = packageHint;
    if (result.packageRoot == juce::File())
    {
        const auto leaf = result.binaryDirectory.getFileName();
        result.packageRoot = (leaf.equalsIgnoreCase("x64") || leaf.equalsIgnoreCase("x86") || leaf.equalsIgnoreCase("bin"))
                           ? result.binaryDirectory.getParentDirectory() : result.binaryDirectory;
    }
    result.aimpExecutable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    result.aimpRoot = result.aimpExecutable.getParentDirectory();
    result.aimpProfile = profileHint;
    if (result.aimpProfile == juce::File())
    {
        const auto localProfile = result.aimpRoot.getChildFile("Profile");
        if (localProfile.isDirectory()) result.aimpProfile = localProfile;
    }
    result.portableConfig = result.packageRoot.getChildFile("dsp_vst3_bridge_config.json");
    result.userConfig = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Mixomo").getChildFile("VST3 Bridge").getChildFile(makeInstanceId(result.packageRoot))
        .getChildFile("dsp_vst3_bridge_config.json");
    result.storageMode = requestedMode;
    const bool portableTree = result.aimpProfile != juce::File() && isWithin(result.packageRoot, result.aimpProfile);
    const bool wantsPortable = requestedMode == StorageMode::portable
        || (requestedMode == StorageMode::automatic && (result.portableConfig.existsAsFile()
            || result.packageRoot.getChildFile("dsp_vst3_bridge.portable").existsAsFile() || portableTree));
    if (wantsPortable && canWriteDirectory(result.packageRoot))
    {
        result.activeConfig = result.portableConfig;
        result.storageMode = StorageMode::portable;
    }
    else
    {
        result.activeConfig = result.userConfig;
        result.storageMode = StorageMode::userProfile;
        result.portableFallback = wantsPortable;
    }
    result.logFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("dsp_vst3_bridge_" + makeInstanceId(result.packageRoot) + "_"
                      + architectureName(currentArchitecture()) + "_" + juce::String::toHexString((int)GetCurrentProcessId()) + ".log");
    return result;
}

juce::File RuntimePaths::helper(Architecture architecture) const
{
    const auto suffix = architecture == Architecture::x86 ? "32" : "64";
    auto file = packageRoot.getChildFile("bin").getChildFile("VST3BridgeHost" + juce::String(suffix) + ".exe");
    if (file.existsAsFile()) return file;
    return binaryDirectory.getChildFile("VST3BridgeHost.exe");
}

juce::File RuntimePaths::scanner(Architecture architecture) const
{
    const auto suffix = architecture == Architecture::x86 ? "32" : "64";
    auto file = packageRoot.getChildFile("bin").getChildFile("VST3BridgeScanner" + juce::String(suffix) + ".exe");
    if (file.existsAsFile()) return file;
    return binaryDirectory.getChildFile("VST3BridgeScanner.exe");
}

juce::var PathReference::toVar() const
{
    auto* object = new juce::DynamicObject();
    object->setProperty("base", base);
    object->setProperty("path", normalise(path));
    return juce::var(object);
}

PathReference PathReference::fromVar(const juce::var& value)
{
    if (auto* object = value.getDynamicObject())
        return { object->getProperty("base").toString(), normalise(object->getProperty("path").toString()) };
    return { "absolute", normalise(value.toString()) };
}

PathReference PathReference::fromFile(const juce::File& file, const RuntimePaths& paths)
{
    if (isWithin(file, paths.packageRoot)) return { "bridge", relativeTo(file, paths.packageRoot) };
    const auto relativeBridge = relativeTo(file, paths.packageRoot);
    if (file.getVolumeSerialNumber() == paths.packageRoot.getVolumeSerialNumber()
        && relativeBridge.startsWith("..\\") && !relativeBridge.startsWith("..\\..\\"))
        return { "bridge", relativeBridge };
    if (paths.aimpProfile != juce::File() && isWithin(file, paths.aimpProfile)) return { "profile", relativeTo(file, paths.aimpProfile) };
    if (isWithin(file, paths.aimpRoot)) return { "aimp", relativeTo(file, paths.aimpRoot) };
    return { "absolute", normalise(file.getFullPathName()) };
}

juce::File PathReference::resolve(const RuntimePaths& paths) const
{
    if (base == "bridge") return paths.packageRoot.getChildFile(path);
    if (base == "profile" && paths.aimpProfile != juce::File()) return paths.aimpProfile.getChildFile(path);
    if (base == "aimp") return paths.aimpRoot.getChildFile(path);
    return juce::File(path);
}

ConfigStore::ConfigStore(RuntimePaths paths) : runtimePaths(std::move(paths)) {}

BridgeSettings ConfigStore::load()
{
    BridgeSettings settings;
    auto file = runtimePaths.activeConfig;
    if (!file.existsAsFile())
    {
        const auto legacyUser = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Mixomo").getChildFile("VST3 Bridge").getChildFile("dsp_vst3_bridge_config.json");
        const auto legacyLocal = runtimePaths.binaryDirectory.getChildFile("dsp_vst3_config.json");
        if (legacyUser.existsAsFile()) file = legacyUser;
        else if (legacyLocal.existsAsFile()) file = legacyLocal;
    }
    auto root = juce::JSON::parse(file);
    if (root.getDynamicObject() == nullptr)
        root = juce::JSON::parse(runtimePaths.activeConfig.getSiblingFile(runtimePaths.activeConfig.getFileName() + ".bak"));
    auto* object = root.getDynamicObject();
    if (object == nullptr)
    {
        settings.scanFolders.add({ PathReference::fromFile(runtimePaths.packageRoot, runtimePaths), true, false });
        settings.scanFolders.add({ PathReference::fromFile(runtimePaths.packageRoot.getChildFile("VST3"), runtimePaths), true, true });
        return settings;
    }
    const auto loadedSchemaVersion = static_cast<int>(object->getProperty("schemaVersion"));

    settings.requestedStorageMode = parseStorageMode(object->getProperty("storageMode").toString());
    settings.startupMode = object->getProperty("startupMode").toString();
    if (settings.startupMode.isEmpty()) settings.startupMode = "restoreLast";
    auto legacyStartupPlugin = PathReference::fromVar(object->getProperty("startupPlugin"));
    settings.activePlugin = PathReference::fromVar(object->hasProperty("activePlugin")
        ? object->getProperty("activePlugin") : object->getProperty("activePluginPath"));
    settings.pluginState = object->getProperty("pluginState").toString();
    settings.scanOnStartup = false;
    settings.scanBridgeFolder = object->hasProperty("scanBridgeFolder") ? static_cast<bool>(object->getProperty("scanBridgeFolder")) : true;
    settings.scanSystemFolders = static_cast<bool>(object->getProperty("scanSystemFolders"));
    settings.removeMissing = object->hasProperty("removeMissing") ? static_cast<bool>(object->getProperty("removeMissing")) : true;
    settings.retryQuarantined = static_cast<bool>(object->getProperty("retryQuarantined"));
    const auto configuredTimeout = static_cast<int>(object->getProperty("scanTimeoutSeconds"));
    settings.scanTimeoutSeconds = juce::jlimit(1, 120,
        loadedSchemaVersion < 7 && configuredTimeout == 15 ? 20 : configuredTimeout > 0 ? configuredTimeout : 20);
    settings.openEditorOnStart = static_cast<bool>(object->getProperty("openEditorOnStart"));
    settings.visualizerMode = static_cast<bool>(object->getProperty("visualizerMode"));
    settings.alwaysOnTop = static_cast<bool>(object->getProperty("alwaysOnTop"));
    settings.fullscreen = static_cast<bool>(object->hasProperty("fullscreen")
        ? object->getProperty("fullscreen") : object->getProperty("fullscreenOnStart"));
    settings.rememberWindow = object->hasProperty("rememberWindow") ? static_cast<bool>(object->getProperty("rememberWindow")) : true;
    settings.savedDpi = juce::jmax(96, static_cast<int>(object->getProperty("savedDpi")));
    settings.monitorName = object->getProperty("monitorName").toString();
    if (auto* bounds = object->getProperty("logicalWindowBounds").getArray(); bounds != nullptr && bounds->size() == 4)
        settings.logicalWindowBounds = { static_cast<int>((*bounds)[0]), static_cast<int>((*bounds)[1]), static_cast<int>((*bounds)[2]), static_cast<int>((*bounds)[3]) };
    else if (auto* oldBounds = object->getProperty("windowBounds").getArray(); oldBounds != nullptr && oldBounds->size() == 4)
    {
        const auto oldDpi = juce::jmax(96, static_cast<int>(object->getProperty("savedDpi")));
        settings.logicalWindowBounds = { MulDiv(static_cast<int>((*oldBounds)[0]), 96, oldDpi),
                                         MulDiv(static_cast<int>((*oldBounds)[1]), 96, oldDpi),
                                         MulDiv(static_cast<int>((*oldBounds)[2]), 96, oldDpi),
                                         MulDiv(static_cast<int>((*oldBounds)[3]), 96, oldDpi) };
    }
    if (auto* folders = object->getProperty("scanFolders").getArray()) for (const auto& item : *folders) settings.scanFolders.add(folderFromVar(item));
    if (auto* plugins = object->getProperty("plugins").getArray()) for (const auto& item : *plugins) settings.plugins.add(pluginFromVar(item));
    if (settings.plugins.isEmpty())
    {
        if (auto* legacyPlugins = object->getProperty("scannedPlugins").getArray())
        {
            for (const auto& item : *legacyPlugins)
            {
                auto* legacy = item.getDynamicObject();
                if (legacy == nullptr) continue;
                PluginRecord record;
                const juce::File plugin(legacy->getProperty("path").toString());
                record.location = PathReference::fromFile(plugin, runtimePaths);
                record.canonicalPath = canonicalPath(plugin);
                record.name = legacy->getProperty("name").toString();
                record.manufacturer = legacy->getProperty("manufacturer").toString();
                record.version = legacy->getProperty("version").toString();
                record.category = legacy->getProperty("category").toString();
                record.uid = legacy->getProperty("uid").toString();
                record.instrument = static_cast<bool>(legacy->getProperty("isInstrument"));
                record.architecture = detectBundleArchitecture(plugin);
                record.fingerprint = fingerprintBundle(plugin);
                record.status = plugin.exists() ? "Compatible" : "Missing";
                settings.plugins.add(record);
            }
        }
    }

    // VST3 hosts may report either the bundle or its platform binary. Persist the bundle.
    for (auto& plugin : settings.plugins)
    {
        const auto bundle = vst3BundleRoot(plugin.location.resolve(runtimePaths));
        plugin.location = PathReference::fromFile(bundle, runtimePaths);
        plugin.canonicalPath = canonicalPath(bundle);
        if (loadedSchemaVersion < 4)
        {
            if (plugin.status == "Runtime failed") plugin.status = "Compatible";
            plugin.runtimeQuarantined = false;
            if (plugin.lastError == "Plugin failed during runtime processing") plugin.lastError.clear();
        }
    }
    if (settings.activePlugin.path.isNotEmpty())
        settings.activePlugin = PathReference::fromFile(vst3BundleRoot(settings.activePlugin.resolve(runtimePaths)), runtimePaths);
    if (legacyStartupPlugin.path.isNotEmpty())
        legacyStartupPlugin = PathReference::fromFile(vst3BundleRoot(legacyStartupPlugin.resolve(runtimePaths)), runtimePaths);
    for (int i = 0; i < settings.plugins.size(); ++i)
        for (int j = settings.plugins.size(); --j > i;)
            if (settings.plugins.getReference(i).canonicalPath.equalsIgnoreCase(settings.plugins.getReference(j).canonicalPath))
                settings.plugins.remove(j);

    if (settings.startupMode == "alwaysSelected")
    {
        if (legacyStartupPlugin.path.isNotEmpty()) settings.activePlugin = legacyStartupPlugin;
        settings.startupMode = "restoreLast";
    }
    if (loadedSchemaVersion < 6 && settings.pluginState.isNotEmpty())
    {
        const auto active = canonicalPath(settings.activePlugin.resolve(runtimePaths));
        for (auto& plugin : settings.plugins)
            if (plugin.canonicalPath.equalsIgnoreCase(active)) { plugin.state = settings.pluginState; break; }
    }

    if (settings.scanFolders.isEmpty())
    {
        settings.scanFolders.add({ PathReference::fromFile(runtimePaths.packageRoot, runtimePaths), true, false });
        settings.scanFolders.add({ PathReference::fromFile(runtimePaths.packageRoot.getChildFile("VST3"), runtimePaths), true, true });
    }
    return settings;
}

bool ConfigStore::save(const BridgeSettings& settings, juce::String* error)
{
    auto* object = new juce::DynamicObject();
    object->setProperty("schemaVersion", configSchemaVersion);
    object->setProperty("storageMode", storageModeName(settings.requestedStorageMode));
    object->setProperty("startupMode", settings.startupMode);
    object->setProperty("activePlugin", settings.activePlugin.toVar());
    object->setProperty("pluginState", settings.pluginState);
    object->setProperty("scanOnStartup", settings.scanOnStartup);
    object->setProperty("scanBridgeFolder", settings.scanBridgeFolder);
    object->setProperty("scanSystemFolders", settings.scanSystemFolders);
    object->setProperty("removeMissing", settings.removeMissing);
    object->setProperty("retryQuarantined", settings.retryQuarantined);
    object->setProperty("scanTimeoutSeconds", settings.scanTimeoutSeconds);
    object->setProperty("openEditorOnStart", settings.openEditorOnStart);
    object->setProperty("visualizerMode", settings.visualizerMode);
    object->setProperty("alwaysOnTop", settings.alwaysOnTop);
    object->setProperty("fullscreen", settings.fullscreen);
    object->setProperty("rememberWindow", settings.rememberWindow);
    object->setProperty("savedDpi", settings.savedDpi);
    object->setProperty("monitorName", settings.monitorName);
    juce::Array<juce::var> bounds { settings.logicalWindowBounds[0], settings.logicalWindowBounds[1], settings.logicalWindowBounds[2], settings.logicalWindowBounds[3] };
    object->setProperty("logicalWindowBounds", bounds);
    juce::Array<juce::var> folders; for (const auto& folder : settings.scanFolders) folders.add(folderToVar(folder));
    juce::Array<juce::var> plugins; for (const auto& plugin : settings.plugins) plugins.add(pluginToVar(plugin));
    object->setProperty("scanFolders", folders);
    object->setProperty("plugins", plugins);

    const auto target = runtimePaths.activeConfig;
    target.getParentDirectory().createDirectory();
    juce::InterProcessLock lock("VST3BridgeConfig_" + makeInstanceId(target));
    if (!lock.enter(3000)) { if (error) *error = "Configuration is locked by another instance"; return false; }
    const auto temp = target.getSiblingFile(target.getFileName() + ".tmp");
    const auto backup = target.getSiblingFile(target.getFileName() + ".bak");
    if (!temp.replaceWithText(juce::JSON::toString(juce::var(object), true))) { if (error) *error = "Cannot write temporary configuration"; return false; }
    if (target.existsAsFile()) target.copyFileTo(backup);
    if (!MoveFileExW(temp.getFullPathName().toWideCharPointer(), target.getFullPathName().toWideCharPointer(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        if (error) *error = "Cannot replace configuration atomically";
        return false;
    }
    return true;
}

bool ConfigStore::changeStorageMode(StorageMode mode, BridgeSettings& settings, juce::String* error)
{
    settings.requestedStorageMode = mode;
    auto next = RuntimePaths::detect(runtimePaths.executable, runtimePaths.packageRoot, runtimePaths.aimpProfile, mode);
    const auto previous = runtimePaths;
    runtimePaths = next;
    if (save(settings, error)) return true;
    runtimePaths = previous;
    return false;
}

juce::Array<juce::File> discoverVst3Candidates(const juce::Array<ScanFolder>& folders,
                                                const RuntimePaths& paths, std::atomic_bool* cancelled)
{
    juce::Array<juce::File> result;
    std::set<juce::String> visited;
    for (const auto& entry : folders)
    {
        if (!entry.enabled) continue;
        const auto folder = entry.location.resolve(paths);
        if (folder.hasFileExtension("vst3")) result.addIfNotAlreadyThere(folder);
        else if (folder.isDirectory()) enumerateFolder(folder, entry.recursive, result, visited, cancelled);
    }
    return result;
}

juce::String fingerprintBundle(const juce::File& bundle)
{
    std::int64_t size = 0, latest = 0, count = 0;
    juce::Array<juce::File> files;
    if (bundle.isDirectory()) bundle.findChildFiles(files, juce::File::findFiles, true);
    else files.add(bundle);
    for (const auto& file : files)
    {
        size += file.getSize(); latest = juce::jmax(latest, file.getLastModificationTime().toMilliseconds()); ++count;
    }
    return juce::String::toHexString(size) + "-" + juce::String::toHexString(latest) + "-" + juce::String(count);
}

juce::String canonicalPath(const juce::File& file)
{
    wchar_t buffer[32768] {};
    const auto path = file.getFullPathName().toWideCharPointer();
    const DWORD length = GetFullPathNameW(path, static_cast<DWORD>(std::size(buffer)), buffer, nullptr);
    return length > 0 && length < std::size(buffer) ? normalise(juce::String(buffer)) : normalise(file.getFullPathName());
}

juce::File vst3BundleRoot(juce::File file)
{
    if (file.isDirectory() && file.hasFileExtension("vst3")) return file;
    for (auto parent = file.getParentDirectory(); parent != juce::File(); parent = parent.getParentDirectory())
    {
        if (parent.isDirectory() && parent.hasFileExtension("vst3")) return parent;
        if (parent == parent.getParentDirectory()) break;
    }
    return file;
}

std::array<int, 2> comfortableWindowPhysicalSize(std::array<int, 2> displaySize)
{
    if (displaySize[0] <= 0 || displaySize[1] <= 0) return { 1280, 720 };
    constexpr int tiers[] { 360, 480, 540, 720, 1080, 1440, 2160, 2880, 4320 };
    int targetHeight = juce::jmax(360, juce::roundToInt(displaySize[1] * 0.75));
    for (const auto tier : tiers)
        if (tier < displaySize[1]) targetHeight = tier;
    return { juce::roundToInt(static_cast<double>(displaySize[0]) * targetHeight / displaySize[1]), targetHeight };
}

juce::String makeInstanceId(const juce::File& packageRoot)
{
    return juce::String::toHexString(static_cast<std::int64_t>(normalise(packageRoot.getFullPathName()).toLowerCase().hashCode64()));
}
}

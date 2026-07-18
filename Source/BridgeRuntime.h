#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <array>
#include <cstdint>
#include <functional>

namespace bridge
{
constexpr int configSchemaVersion = 9;
constexpr int maxRackSlots = 10;
constexpr const char* bridgeVersion = "2.0.0";
constexpr const char* aimpSdkVersion = "5.40 build 2650";

enum class Architecture { unknown, x86, x64, multi };
enum class StorageMode { automatic, portable, userProfile };

juce::String architectureName(Architecture value);
Architecture parseArchitecture(const juce::String& value);
Architecture currentArchitecture();
Architecture detectBundleArchitecture(const juce::File& bundle);

struct RuntimePaths
{
    juce::File executable;
    juce::File aimpExecutable;
    juce::File aimpRoot;
    juce::File aimpProfile;
    juce::File packageRoot;
    juce::File binaryDirectory;
    juce::File portableConfig;
    juce::File userConfig;
    juce::File activeConfig;
    juce::File logFile;
    StorageMode storageMode = StorageMode::automatic;
    bool portableFallback = false;

    static RuntimePaths detect(const juce::File& executable,
                               const juce::File& packageHint = {},
                               const juce::File& profileHint = {},
                               StorageMode requestedMode = StorageMode::automatic);
    juce::File helper(Architecture architecture) const;
    juce::File scanner(Architecture architecture) const;
};

struct PathReference
{
    juce::String base = "absolute";
    juce::String path;

    juce::var toVar() const;
    static PathReference fromVar(const juce::var& value);
    static PathReference fromFile(const juce::File& file, const RuntimePaths& paths);
    juce::File resolve(const RuntimePaths& paths) const;
};

struct ScanFolder
{
    PathReference location;
    bool enabled = true;
    bool recursive = true;
};

struct PluginRecord
{
    PathReference location;
    juce::String canonicalPath;
    juce::String name;
    juce::String descriptiveName;
    juce::String manufacturer;
    juce::String version;
    juce::String category;
    juce::String uid;
    Architecture architecture = Architecture::unknown;
    juce::String fingerprint;
    juce::String status = "Compatible";
    juce::String lastError;
    bool scannerQuarantined = false;
    bool runtimeQuarantined = false;
    bool instrument = false;
    int numInputChannels = 0;
    int numOutputChannels = 0;
    bool sharedContainer = false;
    bool araExtension = false;
    std::int64_t lastScanTime = 0;
    juce::String state;
};

struct RackSlot
{
    PathReference plugin;
    juce::String state;
    bool muted = false;
    bool solo = false;
    int editorHeight = 0;
};

struct BridgeSettings
{
    StorageMode requestedStorageMode = StorageMode::automatic;
    juce::String startupMode = "restoreLast";
    PathReference activePlugin;
    juce::String pluginState;
    bool scanOnStartup = false;
    bool openRackOnStartup = false;
    bool scanBridgeFolder = true;
    bool scanSystemFolders = false;
    bool removeMissing = true;
    bool retryQuarantined = false;
    int scanTimeoutSeconds = 20;
    juce::Array<ScanFolder> scanFolders;
    juce::Array<PluginRecord> plugins;
    juce::Array<RackSlot> rack;
};

class ConfigStore
{
public:
    explicit ConfigStore(RuntimePaths paths);
    const RuntimePaths& getPaths() const { return runtimePaths; }
    RuntimePaths& getPaths() { return runtimePaths; }
    BridgeSettings load();
    bool save(const BridgeSettings& settings, juce::String* error = nullptr);
    bool changeStorageMode(StorageMode mode, BridgeSettings& settings, juce::String* error = nullptr);

private:
    RuntimePaths runtimePaths;
};

juce::Array<juce::File> discoverVst3Candidates(const juce::Array<ScanFolder>& folders,
                                                const RuntimePaths& paths,
                                                std::atomic_bool* cancelled = nullptr);
juce::String fingerprintBundle(const juce::File& bundle);
juce::String canonicalPath(const juce::File& file);
juce::File vst3BundleRoot(juce::File file);
std::array<int, 2> comfortableWindowPhysicalSize(std::array<int, 2> displaySize);
bool rackNeedsPreparation(bool prepared, double currentSampleRate, int currentChannels, int currentBlockSize,
                          double sampleRate, int channels, int blockSize);
juce::String makeInstanceId(const juce::File& packageRoot);
}

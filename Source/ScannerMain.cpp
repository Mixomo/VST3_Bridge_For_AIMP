#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include "BridgeRuntime.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

namespace
{
    juce::File pluginFile;
    juce::File resultFile;

    juce::String argument(int argc, wchar_t** argv, const wchar_t* name)
    {
        for (int i = 1; i + 1 < argc; ++i)
            if (wcscmp(argv[i], name) == 0)
                return juce::String(argv[i + 1]);
        return {};
    }

    int writeResult(const juce::var& value, int exitCode)
    {
        if (resultFile != juce::File())
            resultFile.replaceWithText(juce::JSON::toString(value, true));
        return exitCode;
    }

    juce::var failure(const juce::String& status, const juce::String& message,
                      const juce::File& plugin, bridge::Architecture architecture)
    {
        auto* object = new juce::DynamicObject();
        object->setProperty("success", false);
        object->setProperty("status", status);
        object->setProperty("message", message);
        object->setProperty("canonicalPath", bridge::canonicalPath(plugin));
        object->setProperty("architecture", bridge::architectureName(architecture));
        return juce::var(object);
    }

    int scan(const juce::File& plugin)
    {
        const auto architecture = bridge::detectBundleArchitecture(plugin);
        if (!plugin.exists())
            return writeResult(failure("missing", "VST3 bundle does not exist", plugin, architecture), 2);
        if (architecture != bridge::Architecture::unknown && architecture != bridge::Architecture::multi
            && architecture != bridge::currentArchitecture())
            return writeResult(failure("wrong architecture", "Scanner architecture does not match plugin", plugin, architecture), 3);

        juce::ScopedJuceInitialiser_GUI juceInitialiser;
        juce::VST3PluginFormat format;
        juce::OwnedArray<juce::PluginDescription> descriptions;
        format.findAllTypesForFile(descriptions, plugin.getFullPathName());
        if (descriptions.isEmpty())
            return writeResult(failure("no compatible VST3 class", "No effect class was discovered", plugin, architecture), 4);

        const auto& description = *descriptions[0];
        auto* object = new juce::DynamicObject();
        object->setProperty("success", true);
        object->setProperty("status", "compatible");
        object->setProperty("message", "");
        object->setProperty("name", description.name);
        object->setProperty("descriptiveName", description.descriptiveName);
        object->setProperty("manufacturer", description.manufacturerName);
        object->setProperty("version", description.version);
        object->setProperty("category", description.category);
        object->setProperty("uid", description.createIdentifierString());
        object->setProperty("instrument", description.isInstrument);
        object->setProperty("numInputChannels", description.numInputChannels);
        object->setProperty("numOutputChannels", description.numOutputChannels);
        object->setProperty("sharedContainer", description.hasSharedContainer);
        object->setProperty("araExtension", description.hasARAExtension);
        object->setProperty("architecture", bridge::architectureName(architecture == bridge::Architecture::multi
            ? bridge::currentArchitecture() : architecture));
        object->setProperty("canonicalPath", bridge::canonicalPath(plugin));
        object->setProperty("fingerprint", bridge::fingerprintBundle(plugin));
        return writeResult(juce::var(object), 0);
    }

    int scanSafely()
    {
        int result = 0;
        __try
        {
            result = scan(pluginFile);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            result = -1;
        }
        return result;
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    int argc = 0;
    auto** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) return 1;
    pluginFile = juce::File(argument(argc, argv, L"--plugin"));
    resultFile = juce::File(argument(argc, argv, L"--result"));
    LocalFree(argv);
    if (pluginFile == juce::File() || resultFile == juce::File()) return 1;
    const auto result = scanSafely();
    return result >= 0 ? result
        : writeResult(failure("exception or crash", "Scanner caught a Windows exception", pluginFile,
                              bridge::detectBundleArchitecture(pluginFile)), 5);
}

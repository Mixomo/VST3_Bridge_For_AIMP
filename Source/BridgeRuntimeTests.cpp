#include "BridgeRuntime.h"
#include "OutProcProtocol.h"

#include <cassert>
#include <cstddef>
#include <iostream>

int main()
{
    using namespace bridge;
    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("vst3_rack_runtime_test", {}, false);
    const auto binary = root.getChildFile("x64").getChildFile("VST3RackHost64.exe");
    const auto profile = root.getChildFile("Profile");
    root.createDirectory();
    profile.createDirectory();
    const auto paths = RuntimePaths::detect(binary, root, profile, StorageMode::portable);
    const auto plugin = root.getChildFile("VST3").getChildFile(juce::String::fromUTF8("Prueba á.vst3"));
    const auto reference = PathReference::fromFile(plugin, paths);
    assert(reference.base == "bridge");
    assert(reference.resolve(paths).getFullPathName().equalsIgnoreCase(plugin.getFullPathName()));
    const auto sibling = root.getSiblingFile("Shared VST3").getChildFile(juce::String::fromUTF8("Analizador ñ.vst3"));
    const auto siblingReference = PathReference::fromFile(sibling, paths);
    assert(siblingReference.base == "bridge");
    assert(siblingReference.path.startsWith("..\\"));
    assert(siblingReference.resolve(paths).getFullPathName().equalsIgnoreCase(sibling.getFullPathName()));
    const auto bundle = root.getChildFile("Nested.vst3");
    bundle.createDirectory();
    const auto nestedBinary = bundle.getChildFile("Contents").getChildFile("x86_64-win").getChildFile("Nested.vst3");
    assert(vst3BundleRoot(nestedBinary) == bundle);
    auto makePlugin = [](const juce::File& file)
    {
        auto* pluginObject = new juce::DynamicObject();
        pluginObject->setProperty("location", PathReference { "absolute", file.getFullPathName() }.toVar());
        pluginObject->setProperty("canonicalPath", canonicalPath(file));
        pluginObject->setProperty("name", "Nested");
        pluginObject->setProperty("architecture", architectureName(currentArchitecture()));
        pluginObject->setProperty("status", "Runtime failed");
        pluginObject->setProperty("lastError", "Plugin failed during runtime processing");
        pluginObject->setProperty("runtimeQuarantined", true);
        return juce::var(pluginObject);
    };
    auto* oldConfig = new juce::DynamicObject();
    oldConfig->setProperty("schemaVersion", 3);
    oldConfig->setProperty("storageMode", "portable");
    oldConfig->setProperty("scanOnStartup", true);
    oldConfig->setProperty("startupMode", "alwaysSelected");
    oldConfig->setProperty("startupPlugin", PathReference { "absolute", nestedBinary.getFullPathName() }.toVar());
    oldConfig->setProperty("pluginState", "c3RhdGU=");
    oldConfig->setProperty("activePlugin", PathReference { "absolute", nestedBinary.getFullPathName() }.toVar());
    juce::Array<juce::var> oldPlugins { makePlugin(nestedBinary), makePlugin(bundle) };
    oldConfig->setProperty("plugins", oldPlugins);
    paths.activeConfig.replaceWithText(juce::JSON::toString(juce::var(oldConfig)));
    const auto migrated = ConfigStore(paths).load();
    assert(migrated.plugins.size() == 1);
    assert(migrated.plugins[0].status == "Compatible");
    assert(!migrated.plugins[0].runtimeQuarantined);
    assert(migrated.activePlugin.resolve(paths) == bundle);
    assert(migrated.startupMode == "restoreLast");
    assert(migrated.plugins[0].state == "c3RhdGU=");
    assert(!migrated.scanOnStartup);
    assert(migrated.scanTimeoutSeconds == 20);

    auto* currentConfig = new juce::DynamicObject();
    currentConfig->setProperty("schemaVersion", configSchemaVersion);
    currentConfig->setProperty("storageMode", "portable");
    currentConfig->setProperty("activePlugin", PathReference { "absolute", nestedBinary.getFullPathName() }.toVar());
    currentConfig->setProperty("plugins", juce::Array<juce::var> { makePlugin(nestedBinary) });
    paths.activeConfig.replaceWithText(juce::JSON::toString(juce::var(currentConfig)));
    const auto current = ConfigStore(paths).load();
    assert(current.plugins.size() == 1);
    assert(current.plugins[0].location.resolve(paths) == bundle);
    assert(current.plugins[0].canonicalPath.equalsIgnoreCase(canonicalPath(bundle)));
    assert(current.activePlugin.resolve(paths) == bundle);
    assert(current.rack.size() == 1);
    assert(current.rack[0].plugin.resolve(paths) == bundle);
    auto rackSettings = current;
    rackSettings.rack.clear();
    rackSettings.rack.add({ PathReference::fromFile(bundle, paths), "c2xvdC0x", true, false, 540 });
    rackSettings.rack.add({ PathReference::fromFile(bundle, paths), "c2xvdC0y", false, true, 720 });
    rackSettings.openRackOnStartup = true;
    assert(ConfigStore(paths).save(rackSettings));
    const auto rackRoundTrip = ConfigStore(paths).load();
    assert(rackRoundTrip.rack.size() == 2);
    assert(rackRoundTrip.rack[0].state == "c2xvdC0x");
    assert(rackRoundTrip.rack[1].state == "c2xvdC0y");
    assert(rackRoundTrip.rack[0].muted && !rackRoundTrip.rack[0].solo);
    assert(!rackRoundTrip.rack[1].muted && rackRoundTrip.rack[1].solo);
    assert(rackRoundTrip.rack[0].editorHeight == 540 && rackRoundTrip.rack[1].editorHeight == 720);
    assert(rackRoundTrip.openRackOnStartup);
    assert((comfortableWindowPhysicalSize({ 1920, 1080 }) == std::array<int, 2>({ 1280, 720 })));
    assert((comfortableWindowPhysicalSize({ 2560, 1440 }) == std::array<int, 2>({ 1920, 1080 })));
    assert((comfortableWindowPhysicalSize({ 3840, 2160 }) == std::array<int, 2>({ 2560, 1440 })));
    assert((comfortableWindowPhysicalSize({ 3440, 1440 }) == std::array<int, 2>({ 2580, 1080 })));
    assert(!rackNeedsPreparation(true, 48000.0, 2, 512, 48000.0, 2, 512));
    assert(rackNeedsPreparation(false, 48000.0, 2, 512, 48000.0, 2, 512));
    assert(rackNeedsPreparation(true, 48000.0, 2, 512, 96000.0, 2, 512));
    assert(currentArchitecture() == (sizeof(void*) == 8 ? Architecture::x64 : Architecture::x86));
    assert(sizeof(VST3BridgeIpcSlot) == 40);
    assert(offsetof(VST3BridgeIpcSlot, sequence) == 32);
    static_assert(sizeof(VST3BridgeIpcHeader) == 192);
    root.deleteRecursively();
    std::cout << "BridgeRuntimeTests passed\n";
    return 0;
}

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "HostWindow.h"
#include "VST3HostEngine.h"

namespace
{
    constexpr auto background = 0xff1e1e1e;
    constexpr auto panel = 0xff2d2d2d;
    constexpr int compactControlsHeight = 132;

    juce::String monitorNameForWindow(HWND window)
    {
        MONITORINFOEXW info {};
        info.cbSize = sizeof(info);
        return GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info)
            ? juce::String(info.szDevice) : juce::String();
    }

    class FolderManagerComponent final : public juce::Component,
                                         private juce::ListBoxModel,
                                         private juce::Button::Listener,
                                         private juce::Timer
    {
    public:
        FolderManagerComponent()
        {
            title.setText("VST3 Search Folders", juce::dontSendNotification);
            title.setFont(juce::FontOptions(20.0f));
            list.setModel(this);
            list.setRowHeight(36);
            list.setColour(juce::ListBox::backgroundColourId, juce::Colour(panel));
            title.setTooltip("Manual VST3 discovery locations. The bridge never scans these folders automatically.");
            title.setColour(juce::Label::textColourId, juce::Colours::white);
            for (auto* component : std::initializer_list<juce::Component*> { &title, &list, &enabled, &recursive,
                                     &add, &remove, &open, &rescan, &scanAll, &cancel })
                addAndMakeVisible(component);
            enabled.setButtonText("Enabled"); recursive.setButtonText("Recursive");
            add.setButtonText("Add Folder..."); remove.setButtonText("Remove"); open.setButtonText("Open Folder");
            rescan.setButtonText("Rescan Selected"); scanAll.setButtonText("Scan All"); cancel.setButtonText("Cancel Scan");
            enabled.setTooltip("Include this folder the next time Scan All is pressed. Disabling it keeps the folder saved but skips it.");
            recursive.setTooltip("Also search every subfolder. Disable this to inspect only VST3 bundles directly inside the selected folder.");
            add.setTooltip("Choose another folder and add it to the manual VST3 search list.");
            remove.setTooltip("Forget this search location. Already discovered plugins remain in the plugin dropdown.");
            open.setTooltip("Open the selected search location in Windows File Explorer.");
            rescan.setTooltip("Scan only the selected folder now and refresh its known VST3 plugins.");
            scanAll.setTooltip("Scan every enabled folder now. No automatic scan is performed at startup.");
            cancel.setTooltip("Request cancellation of the current manual scan after the plugin currently being checked finishes.");
            for (auto* button : std::initializer_list<juce::Button*> { &enabled, &recursive, &add, &remove, &open, &rescan, &scanAll, &cancel }) button->addListener(this);
            refresh();
            timerCallback();
            startTimer(250);
            setSize(900, 500);
        }

        int getNumRows() override { return settings.scanFolders.size(); }
        void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(background)); }
        void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override
        {
            if (selected) g.fillAll(juce::Colour(0xff3c5368));
            if (!juce::isPositiveAndBelow(row, settings.scanFolders.size())) return;
            const auto& folder = settings.scanFolders.getReference(row);
            const auto resolved = folder.location.resolve(VST3HostEngine::getInstance().getRuntimePaths());
            const auto text = juce::String(folder.enabled ? "[Enabled]  " : "[Disabled] ")
                + resolved.getFullPathName() + "  |  " + (folder.recursive ? "Recursive" : "Top level")
                + "  |  " + folder.location.base + "  |  " + (resolved.isDirectory() ? "Available" : "Missing");
            g.setColour(juce::Colours::whitesmoke);
            g.drawFittedText(text, 8, 0, width - 16, height, juce::Justification::centredLeft, 1);
        }

        void selectedRowsChanged(int row) override
        {
            const bool valid = juce::isPositiveAndBelow(row, settings.scanFolders.size());
            enabled.setEnabled(valid); recursive.setEnabled(valid); remove.setEnabled(valid); open.setEnabled(valid);
            rescan.setEnabled(valid && !VST3HostEngine::getInstance().isScanning());
            if (valid)
            {
                enabled.setToggleState(settings.scanFolders.getReference(row).enabled, juce::dontSendNotification);
                recursive.setToggleState(settings.scanFolders.getReference(row).recursive, juce::dontSendNotification);
            }
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced(18);
            title.setBounds(area.removeFromTop(32));
            area.removeFromTop(6);
            auto bottom = area.removeFromBottom(88);
            auto toggles = bottom.removeFromTop(32);
            enabled.setBounds(toggles.removeFromLeft(125));
            toggles.removeFromLeft(10);
            recursive.setBounds(toggles.removeFromLeft(135));
            bottom.removeFromTop(10);
            juce::FlexBox buttons;
            buttons.flexDirection = juce::FlexBox::Direction::row;
            buttons.justifyContent = juce::FlexBox::JustifyContent::spaceBetween;
            for (auto* button : { &add, &remove, &open, &rescan, &scanAll, &cancel })
                buttons.items.add(juce::FlexItem(*button).withMinWidth(130.0f).withHeight(36.0f));
            buttons.performLayout(bottom.toFloat());
            list.setBounds(area);
        }

    private:
        void timerCallback() override
        {
            const bool scanning = VST3HostEngine::getInstance().isScanning();
            cancel.setEnabled(scanning);
            cancel.setButtonText("Cancel Scan");
            scanAll.setEnabled(!scanning);
            scanAll.setButtonText(scanning ? "Scanning..." : "Scan All");
            rescan.setEnabled(!scanning && juce::isPositiveAndBelow(list.getSelectedRow(), settings.scanFolders.size()));
        }

        void refresh()
        {
            settings = VST3HostEngine::getInstance().getSettingsSnapshot();
            list.updateContent();
            selectedRowsChanged(list.getSelectedRow());
        }

        void commit()
        {
            VST3HostEngine::getInstance().updateSettings(settings);
            list.repaint();
        }

        void buttonClicked(juce::Button* button) override
        {
            const int row = list.getSelectedRow();
            if (button == &enabled && juce::isPositiveAndBelow(row, settings.scanFolders.size()))
            { settings.scanFolders.getReference(row).enabled = enabled.getToggleState(); commit(); }
            else if (button == &recursive && juce::isPositiveAndBelow(row, settings.scanFolders.size()))
            { settings.scanFolders.getReference(row).recursive = recursive.getToggleState(); commit(); }
            else if (button == &remove && juce::isPositiveAndBelow(row, settings.scanFolders.size()))
            { settings.scanFolders.remove(row); commit(); refresh(); }
            else if (button == &open && juce::isPositiveAndBelow(row, settings.scanFolders.size()))
                settings.scanFolders.getReference(row).location.resolve(VST3HostEngine::getInstance().getRuntimePaths()).startAsProcess();
            else if (button == &rescan && juce::isPositiveAndBelow(row, settings.scanFolders.size()))
                VST3HostEngine::getInstance().scanFolder(settings.scanFolders.getReference(row));
            else if (button == &scanAll) VST3HostEngine::getInstance().scanAll(true);
            else if (button == &cancel) VST3HostEngine::getInstance().cancelScan();
            else if (button == &add)
            {
                chooser = std::make_unique<juce::FileChooser>("Add VST3 search folder", juce::File(), "*");
                auto safe = juce::Component::SafePointer<FolderManagerComponent>(this);
                chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                    [safe](const juce::FileChooser& selected)
                    {
                        if (safe == nullptr) return;
                        const auto folder = selected.getResult();
                        if (!folder.isDirectory()) return;
                        const auto paths = VST3HostEngine::getInstance().getRuntimePaths();
                        const auto canonical = bridge::canonicalPath(folder);
                        for (const auto& existing : safe->settings.scanFolders)
                            if (bridge::canonicalPath(existing.location.resolve(paths)).equalsIgnoreCase(canonical)) return;
                        safe->settings.scanFolders.add({ bridge::PathReference::fromFile(folder, paths), true, true });
                        safe->commit(); safe->refresh();
                    });
            }
        }

        juce::Label title;
        juce::ListBox list;
        juce::ToggleButton enabled, recursive;
        juce::TextButton add, remove, open, rescan, scanAll, cancel;
        bridge::BridgeSettings settings;
        std::unique_ptr<juce::FileChooser> chooser;
    };

    class SettingsComponent final : public juce::Component, private juce::Button::Listener, private juce::ComboBox::Listener
    {
    public:
        SettingsComponent()
        {
            settings = VST3HostEngine::getInstance().getSettingsSnapshot();
            storage.addItemList({ "Automatic", "Portable", "User profile" }, 1);
            startup.addItemList({ "Restore last active plugin", "Start with no plugin" }, 1);
            storage.setSelectedId(settings.requestedStorageMode == bridge::StorageMode::portable ? 2 : settings.requestedStorageMode == bridge::StorageMode::userProfile ? 3 : 1);
            startup.setSelectedId(settings.startupMode == "none" ? 2 : 1);
            storage.addListener(this); startup.addListener(this);
            configure(scanBridge, "Scan bridge package folder", settings.scanBridgeFolder);
            configure(scanSystem, "Scan standard system VST3 folders", settings.scanSystemFolders);
            configure(removeMissing, "Remove missing plugins automatically", settings.removeMissing);
            configure(retryQuarantine, "Retry quarantined plugins automatically", settings.retryQuarantined);
            configure(autoEditor, "Open VST3 editor automatically when DSP starts", settings.openEditorOnStart);
            configure(rememberWindow, "Remember window position and size", settings.rememberWindow);
            openLog.setButtonText("Open Log"); openLogFolder.setButtonText("Open Log Folder"); openConfigFolder.setButtonText("Open Config Folder"); copyDiagnostics.setButtonText("Copy Diagnostics"); resetCache.setButtonText("Reset Scan Cache"); forgetAll.setButtonText("Forget All Plugins");
            openLog.setTooltip("Open the current diagnostic log in its associated text editor.");
            openLogFolder.setTooltip("Open the Windows Temp folder containing the bridge diagnostic log.");
            openConfigFolder.setTooltip("Open the folder containing the active JSON configuration file. The location follows the selected Automatic, Portable or User profile storage mode.");
            copyDiagnostics.setTooltip("Copy bridge version, architecture, DPI, paths, window state and plugin diagnostics to the clipboard.");
            resetCache.setTooltip("Invalidate cached scan fingerprints and clear scan/runtime quarantine flags. Remembered plugins, their saved states and scan folders are kept; no scan starts automatically.");
            forgetAll.setTooltip("Remove all plugins and their saved VST3 states from the dropdown. Scan folders and bridge settings are kept.");
            scanBridge.setTooltip("Include the bridge package folder and its portable VST3 directory when you manually scan folders.");
            scanSystem.setTooltip("Include the standard Windows Common Files VST3 locations when you manually start a scan.");
            removeMissing.setTooltip("Remove dropdown entries whose VST3 bundle no longer exists when scan results are refreshed.");
            retryQuarantine.setTooltip("Let a manual scan retry plugins previously isolated after a scan or runtime failure.");
            autoEditor.setTooltip("Ask the out-of-process host to show the bridge window when AIMP starts this DSP. If startup recovery fails, it opens safely in bypass.");
            rememberWindow.setTooltip("Restore bridge position, dimensions, monitor, DPI, visualizer/fullscreen state and always-on-top preference. Switching plugins does not force a new bridge size.");
            for (auto* button : { &openLog, &openLogFolder, &openConfigFolder, &copyDiagnostics, &resetCache, &forgetAll }) { button->addListener(this); addAndMakeVisible(button); }
            timeout.setRange(1, 120, 1); timeout.setValue(settings.scanTimeoutSeconds); timeout.setTextValueSuffix(" seconds scan timeout");
            for (auto* c : std::initializer_list<juce::Component*> { &storageLabel, &storage, &startupLabel, &startup, &timeout }) addAndMakeVisible(c);
            storageLabel.setText("Configuration storage", juce::dontSendNotification);
            startupLabel.setText("Startup plugin", juce::dontSendNotification);
            storageLabel.setTooltip("Where the bridge configuration, plugin list and per-plugin VST3 states are stored.");
            storage.setTooltip("Automatic chooses a writable portable location when possible, otherwise the user profile. Portable stays with the bridge; User profile stays in Windows AppData.");
            startupLabel.setTooltip("Choose whether AIMP restores the last active VST3 or starts the bridge in bypass.");
            startup.setTooltip("Restore last active plugin reloads its saved VST3 state. Start with no plugin always opens in bypass; you can select a plugin later.");
            storage.setColour(juce::ComboBox::backgroundColourId, juce::Colour(panel));
            startup.setColour(juce::ComboBox::backgroundColourId, juce::Colour(panel));
            storageLabel.setColour(juce::Label::textColourId, juce::Colours::white);
            startupLabel.setColour(juce::Label::textColourId, juce::Colours::white);
            configureHeading(discoveryTitle, "Discovery and safety");
            configureHeading(windowTitle, "Window and startup");
            configureHeading(maintenanceTitle, "Maintenance and diagnostics");
            timeoutLabel.setText("Per-plugin scan timeout", juce::dontSendNotification);
            timeoutHelp.setText("Maximum time allowed for one isolated scanner process\nbefore it is stopped and marked as timed out.", juce::dontSendNotification);
            timeout.setTextValueSuffix(" s");
            timeout.setTooltip("Maximum seconds allowed for each isolated scanner process. Increase it only for plugins that initialise slowly; a timeout keeps a frozen scanner from blocking AIMP.");
            timeoutLabel.setTooltip(timeout.getTooltip());
            timeoutHelp.setTooltip(timeout.getTooltip());
            timeout.setSliderStyle(juce::Slider::LinearHorizontal);
            timeout.setTextBoxStyle(juce::Slider::TextBoxRight, false, 84, 30);
            for (auto* label : { &timeoutLabel, &timeoutHelp })
            {
                label->setColour(juce::Label::textColourId, label == &timeoutHelp ? juce::Colours::lightgrey : juce::Colours::white);
                label->setJustificationType(juce::Justification::centredLeft);
                addAndMakeVisible(label);
            }
            timeout.onDragEnd = [this] { commit(); };
            setSize(980, 620);
        }
        ~SettingsComponent() override { commit(); }
        void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(background)); }
        void resized() override
        {
            auto area = getLocalBounds().reduced(24);
            auto row = area.removeFromTop(36);
            storageLabel.setBounds(row.removeFromLeft(190));
            storage.setBounds(row);
            area.removeFromTop(10);
            row = area.removeFromTop(36);
            startupLabel.setBounds(row.removeFromLeft(190));
            startup.setBounds(row);
            area.removeFromTop(18);

            auto maintenance = area.removeFromBottom(82);
            auto columns = area;
            const auto columnWidth = (columns.getWidth() - 24) / 2;
            auto left = columns.removeFromLeft(columnWidth);
            columns.removeFromLeft(24);
            auto right = columns;

            discoveryTitle.setBounds(left.removeFromTop(30));
            left.removeFromTop(4);
            for (auto* c : { &scanBridge, &scanSystem, &removeMissing, &retryQuarantine })
                c->setBounds(left.removeFromTop(34));
            left.removeFromTop(8);
            timeoutLabel.setBounds(left.removeFromTop(24));
            timeout.setBounds(left.removeFromTop(36));
            timeoutHelp.setBounds(left.removeFromTop(48));

            windowTitle.setBounds(right.removeFromTop(30));
            right.removeFromTop(4);
            for (auto* c : { &autoEditor, &rememberWindow })
                c->setBounds(right.removeFromTop(34));

            maintenanceTitle.setBounds(maintenance.removeFromTop(28));
            maintenance.removeFromTop(6);
            juce::FlexBox buttons;
            buttons.flexDirection = juce::FlexBox::Direction::row;
            buttons.justifyContent = juce::FlexBox::JustifyContent::spaceBetween;
            for (auto* button : { &openLog, &openLogFolder, &openConfigFolder, &copyDiagnostics, &resetCache, &forgetAll })
                buttons.items.add(juce::FlexItem(*button).withMinWidth(145.0f).withHeight(36.0f));
            buttons.performLayout(maintenance.toFloat());
        }
    private:
        void configure(juce::ToggleButton& button, const juce::String& text, bool value)
        { button.setButtonText(text); button.setToggleState(value, juce::dontSendNotification); button.addListener(this); addAndMakeVisible(button); }
        void configureHeading(juce::Label& label, const juce::String& text)
        {
            label.setText(text, juce::dontSendNotification);
            label.setFont(juce::FontOptions(17.0f));
            label.setTooltip(text + " settings");
            label.setColour(juce::Label::textColourId, juce::Colour(0xff76d7c4));
            addAndMakeVisible(label);
        }
        void buttonClicked(juce::Button* button) override
        {
            auto& engine = VST3HostEngine::getInstance();
            if (button == &openLog) engine.getLogFile().startAsProcess();
            else if (button == &openLogFolder) engine.getLogFile().getParentDirectory().startAsProcess();
            else if (button == &openConfigFolder) engine.getConfigFile().getParentDirectory().startAsProcess();
            else if (button == &copyDiagnostics) juce::SystemClipboard::copyTextToClipboard(engine.getDiagnostics());
            else if (button == &resetCache) { engine.resetScanCache(); settings = engine.getSettingsSnapshot(); }
            else if (button == &forgetAll) { engine.forgetAllPlugins(); settings = engine.getSettingsSnapshot(); }
            else commit();
        }
        void comboBoxChanged(juce::ComboBox*) override { commit(); }
        void commit()
        {
            settings.requestedStorageMode = storage.getSelectedId() == 2 ? bridge::StorageMode::portable : storage.getSelectedId() == 3 ? bridge::StorageMode::userProfile : bridge::StorageMode::automatic;
            settings.startupMode = startup.getSelectedId() == 2 ? "none" : "restoreLast";
            settings.scanOnStartup = false; settings.removeMissing = removeMissing.getToggleState();
            settings.scanBridgeFolder = scanBridge.getToggleState(); settings.scanSystemFolders = scanSystem.getToggleState();
            settings.retryQuarantined = retryQuarantine.getToggleState(); settings.openEditorOnStart = autoEditor.getToggleState();
            settings.rememberWindow = rememberWindow.getToggleState();
            settings.scanTimeoutSeconds = static_cast<int>(timeout.getValue());
            VST3HostEngine::getInstance().updateSettings(settings);
        }
        bridge::BridgeSettings settings;
        juce::Label storageLabel, startupLabel, discoveryTitle, windowTitle, maintenanceTitle, timeoutLabel, timeoutHelp;
        juce::ComboBox storage, startup;
        juce::ToggleButton scanBridge, scanSystem, removeMissing, retryQuarantine, autoEditor, rememberWindow;
        juce::TextButton openLog, openLogFolder, openConfigFolder, copyDiagnostics, resetCache, forgetAll;
        juce::Slider timeout;
    };
}

HostContentComponent::HostContentComponent()
{
    VST3HostEngine::getInstance().writeLog("Bridge content construction started");
    pluginLabel.setText("Active VST3 Plugin:", juce::dontSendNotification);
    pluginLabel.setTooltip("The VST3 currently processing AIMP audio. Each remembered plugin keeps its own parameters, opaque VST3 state and GUI-scale state when the plugin serialises it.");
    noEditorLabel.setText("Select a compatible VST3 plugin.", juce::dontSendNotification);
    noEditorLabel.setTooltip("Choose a compatible plugin from the dropdown or use Load VST. Bypass passes audio through unchanged.");
    noEditorLabel.setJustificationType(juce::Justification::centred);
    for (auto* text : { &statusLabel, &detailLabel })
    {
        text->setReadOnly(true);
        text->setMultiLine(false, false);
        text->setScrollbarsShown(false);
        text->setCaretVisible(false);
        text->setPopupMenuEnabled(false);
        text->setColour(juce::TextEditor::backgroundColourId, juce::Colour(panel));
        text->setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    }
    detailLabel.setColour(juce::TextEditor::textColourId, juce::Colours::lightgrey);
    detailLabel.setTooltip("Full plugin name, company, version and normalised VST3 bundle location for the selected entry.");
    statusLabel.setTooltip("Current scan, startup, loading or recovery status. Detailed diagnostics are also written to the bridge log.");
    const auto startupWarning = VST3HostEngine::getInstance().getStartupWarning();
    if (startupWarning.isNotEmpty())
    {
        statusLabel.setText(startupWarning, false);
        noEditorLabel.setText(startupWarning, juce::dontSendNotification);
    }
    pluginComboBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour(panel));
    pluginComboBox.setTooltip("Switch the active VST3 or choose None - Bypass. The current plugin state is saved before switching and the selected plugin restores its own last state. The bridge window size is not reset automatically.");
    pluginComboBox.addListener(this);
    for (auto* component : std::initializer_list<juce::Component*> { &pluginLabel, &pluginComboBox, &loadButton, &foldersButton,
                             &removeButton, &settingsButton, &alwaysOnTopButton,
                             &visualizerButton, &fullscreenButton, &statusLabel, &detailLabel, &noEditorLabel })
        addAndMakeVisible(component);
    for (auto* button : { &loadButton, &foldersButton, &removeButton, &settingsButton,
                          &alwaysOnTopButton, &visualizerButton, &fullscreenButton })
        button->addListener(this);
    loadButton.setTooltip("Choose one VST3 bundle, validate it in isolation, add it to the dropdown and load it.");
    foldersButton.setTooltip("Manage manual search locations, then scan one selected folder or every enabled folder. No automatic scan runs at startup.");
    removeButton.setTooltip("Open actions for the selected plugin. Reset Parameters recreates it without saved state, including non-parameter state such as loaded FIR data when supported. Reset Size changes only the bridge window using the current monitor resolution, aspect ratio and Windows DPI scale. You can also retry, forget, or open the folder containing the VST3 bundle.");
    settingsButton.setTooltip("Open compact bridge settings for storage, startup recovery, manual discovery, window restoration and diagnostics.");
    alwaysOnTopButton.setTooltip("Toggle whether the bridge stays above ordinary windows. This preference is saved. It does not force focus over modal dialogs.");
    visualizerButton.setTooltip("Toggle Visualizer Mode: hide bridge controls and keep a minimal movable title bar. Press Escape to return to normal mode.");
    fullscreenButton.setTooltip("Toggle true fullscreen visualizer mode. Shortcut: F11 enters or exits; Escape returns to the normal bordered window.");
    updateWindowModeButtons();
    addMouseListener(this, true);
    VST3HostEngine::getInstance().writeLog("Bridge content controls created");
    updatePluginList();
    lastActivePath = VST3HostEngine::getInstance().getActivePluginPath();
    VST3HostEngine::getInstance().writeLog("Bridge content plugin list populated");
    startTimer(350);
    editorInitialised = true;
    VST3HostEngine::getInstance().writeLog("VST3 editor creation deferred until bridge is shown");
}

HostContentComponent::~HostContentComponent() { clearEditorComponent(); }
void HostContentComponent::paint(juce::Graphics& g) { g.fillAll(juce::Colour(background)); }

void HostContentComponent::resized()
{
    auto area = getLocalBounds();
    if (!visualizerMode)
    {
        auto controls = area.removeFromTop(compactControlsHeight).reduced(12, 8);
        auto selector = controls.removeFromTop(30);
        pluginLabel.setBounds(selector.removeFromLeft(138));
        selector.removeFromLeft(8);
        pluginComboBox.setBounds(selector);
        controls.removeFromTop(6);
        auto commands = controls.removeFromTop(32);
        juce::TextButton* commandButtons[] = { &loadButton, &foldersButton, &removeButton, &settingsButton,
                                               &alwaysOnTopButton, &visualizerButton, &fullscreenButton };
        constexpr int gap = 6;
        const int buttonWidth = (commands.getWidth() - gap * 6) / 7;
        for (auto* button : commandButtons)
        {
            button->setBounds(commands.removeFromLeft(buttonWidth).withHeight(30));
            commands.removeFromLeft(gap);
        }
        controls.removeFromTop(6);
        auto info = controls;
        detailLabel.setBounds(info.removeFromLeft(juce::roundToInt(info.getWidth() * 0.65f)));
        info.removeFromLeft(8);
        statusLabel.setBounds(info);
    }
    if (activeEditor)
    {
        auto editorArea = area;
        const auto* window = findParentComponentOfClass<HostWindow>();
        const bool fullscreen = window != nullptr && window->isFullScreen();
        if (!fullscreen && preferredEditorSize.x > 0 && preferredEditorSize.y > 0)
            editorArea = area.withSizeKeepingCentre(juce::jmin(area.getWidth(), preferredEditorSize.x),
                                                    juce::jmin(area.getHeight(), preferredEditorSize.y));
        activeEditor->setBounds(editorArea);
    }
    else noEditorLabel.setBounds(area);
}

void HostContentComponent::setVisualizerMode(bool enabled)
{
    if (visualizerMode == enabled) return;
    visualizerMode = enabled;
    for (auto* c : std::initializer_list<juce::Component*> { &pluginLabel, &pluginComboBox, &loadButton, &foldersButton,
                     &removeButton, &settingsButton, &alwaysOnTopButton,
                     &visualizerButton, &fullscreenButton, &statusLabel, &detailLabel }) c->setVisible(!enabled);
    resized(); repaint();
}

void HostContentComponent::updateWindowModeButtons()
{
    const auto settings = VST3HostEngine::getInstance().getSettingsSnapshot();
    alwaysOnTopButton.setToggleState(settings.alwaysOnTop, juce::dontSendNotification);
    visualizerButton.setToggleState(settings.visualizerMode, juce::dontSendNotification);
    fullscreenButton.setToggleState(settings.fullscreen, juce::dontSendNotification);
}

void HostContentComponent::mouseDoubleClick(const juce::MouseEvent& event)
{
    const auto y = event.getEventRelativeTo(this).y;
    if (!visualizerMode && y >= 196 && y <= 282)
        if (auto* window = findParentComponentOfClass<HostWindow>()) window->toggleMaximized();
}

bool HostContentComponent::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    if (auto* window = findParentComponentOfClass<HostWindow>()) return window->keyPressed(key);
    return false;
}

void HostContentComponent::comboBoxChanged(juce::ComboBox*)
{
    if (changingPlugin) return;
    const int index = pluginComboBox.getSelectedId() - 2;
    if (index < 0)
    {
        clearEditorComponent();
        VST3HostEngine::getInstance().acceptExplicitBypass();
        VST3HostEngine::getInstance().unloadPlugin();
        VST3HostEngine::getInstance().saveState();
        return;
    }
    if (!juce::isPositiveAndBelow(index, records.size())) return;
    const auto& record = records.getReference(index);
    if (record.status != "Compatible" || record.scannerQuarantined || record.runtimeQuarantined)
    { statusLabel.setText(record.status + ": " + record.lastError, false); return; }
    if ((record.architecture == bridge::Architecture::x86 || record.architecture == bridge::Architecture::x64)
        && record.architecture != bridge::currentArchitecture())
    {
        statusLabel.setText("Switching to the " + bridge::architectureName(record.architecture) + " helper...", false);
        VST3HostEngine::getInstance().requestHostArchitecture(record);
        return;
    }
    clearEditorComponent();
    if (VST3HostEngine::getInstance().loadPlugin(record.location.resolve(VST3HostEngine::getInstance().getRuntimePaths()).getFullPathName()))
    {
        refreshEditorComponent();
        lastActivePath = VST3HostEngine::getInstance().getActivePluginPath();
        updatePluginList();
        VST3HostEngine::getInstance().saveState();
    }
}

void HostContentComponent::buttonClicked(juce::Button* button)
{
    if (button == &alwaysOnTopButton || button == &visualizerButton)
    {
        auto settings = VST3HostEngine::getInstance().getSettingsSnapshot();
        if (button == &alwaysOnTopButton) settings.alwaysOnTop = !settings.alwaysOnTop;
        else settings.visualizerMode = !settings.visualizerMode;
        VST3HostEngine::getInstance().updateSettings(settings);
        if (auto* window = findParentComponentOfClass<HostWindow>()) window->applyWindowSettings();
    }
    else if (button == &fullscreenButton)
    {
        if (auto* window = findParentComponentOfClass<HostWindow>()) window->toggleFullscreen();
    }
    else if (button == &loadButton)
    {
        fileChooser = std::make_unique<juce::FileChooser>("Select VST3 plug-in", juce::File(), "*.vst3");
        auto safe = juce::Component::SafePointer<HostContentComponent>(this);
        fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe](const juce::FileChooser& chooser) { if (safe != nullptr && chooser.getResult().exists()) VST3HostEngine::getInstance().scanFileAndLoad(chooser.getResult()); });
    }
    else if (button == &foldersButton) openFolderManager();
    else if (button == &removeButton)
    {
        const int index = pluginComboBox.getSelectedId() - 2;
        if (juce::isPositiveAndBelow(index, records.size()))
        {
            const auto record = records.getReference(index);
            const bool failed = record.scannerQuarantined || record.runtimeQuarantined || record.status != "Compatible";
            juce::PopupMenu menu;
            if (failed) menu.addItem(1, "Retry and reset quarantine");
            const auto resolved = record.location.resolve(VST3HostEngine::getInstance().getRuntimePaths());
            const bool active = resolved.getFullPathName().equalsIgnoreCase(VST3HostEngine::getInstance().getActivePluginPath());
            menu.addItem(4, "Reset Parameters", active);
            menu.addItem(5, "Reset Size", active);
            menu.addItem(2, "Forget");
            menu.addItem(3, "Open file location");
            auto safe = juce::Component::SafePointer<HostContentComponent>(this);
            menu.showMenuAsync({}, [record, safe](int result)
            {
                auto& engine = VST3HostEngine::getInstance();
                if (result == 1) engine.retryPlugin(record.canonicalPath);
                else if (result == 2) engine.removePluginFromList(record.canonicalPath);
                else if (result == 3) bridge::vst3BundleRoot(record.location.resolve(engine.getRuntimePaths())).getParentDirectory().startAsProcess();
                else if (result == 4 && safe != nullptr) safe->resetPluginParameters();
                else if (result == 5 && safe != nullptr) safe->resetEditorSize();
            });
        }
    }
    else if (button == &settingsButton)
    {
        if (settingsWindow != nullptr && settingsWindow->getPeer() == nullptr) settingsWindow.reset();
        if (settingsWindow == nullptr)
        {
            juce::DialogWindow::LaunchOptions options; options.dialogTitle = "VST3 Bridge Settings";
            options.content.setOwned(new SettingsComponent());
            options.useNativeTitleBar = true; options.resizable = true;
            settingsWindow.reset(options.create());
        }
        settingsWindow->setVisible(true);
        settingsWindow->setEnabled(true);
        settingsWindow->toFront(true);
    }
}

void HostContentComponent::timerCallback()
{
    VST3HostEngine::getInstance().flushPendingStateSave();
    const auto revision = VST3HostEngine::getInstance().getEditorStateRevision();
    if (revision != lastEditorStateRevision)
    {
        lastEditorStateRevision = revision;
        if (activeEditor)
        {
            refreshEditorComponent();
            preferredEditorSize = initialEditorSize;
            resized();
        }
    }
    if (activeEditor)
    {
        auto canvas = getLocalBounds();
        if (!visualizerMode) canvas.removeFromTop(compactControlsHeight);
        auto expected = canvas;
        const auto* window = findParentComponentOfClass<HostWindow>();
        const bool fullscreen = window != nullptr && window->isFullScreen();
        if (!fullscreen && preferredEditorSize.x > 0 && preferredEditorSize.y > 0)
            expected = canvas.withSizeKeepingCentre(juce::jmin(canvas.getWidth(), preferredEditorSize.x),
                                                    juce::jmin(canvas.getHeight(), preferredEditorSize.y));
        if (activeEditor->getBounds() != expected)
        {
            if (!fullscreen)
                preferredEditorSize = { activeEditor->getWidth(), activeEditor->getHeight() };
            resized();
        }
    }
    if (editorLayoutPassesRemaining > 0)
    {
        resized();
        if (activeEditor) activeEditor->resized();
        --editorLayoutPassesRemaining;
    }
    const auto active = VST3HostEngine::getInstance().getActivePluginPath();
    if (records.size() != VST3HostEngine::getInstance().getPluginRecords().size() || active != lastActivePath || VST3HostEngine::getInstance().isScanning()) updatePluginList();
    if (active != lastActivePath)
    {
        lastActivePath = active;
        refreshEditorComponent();
    }
    const auto progress = VST3HostEngine::getInstance().getScanProgress();
    if (progress.isNotEmpty())
    {
        statusLabel.setText(progress, false);
        statusLabel.setTooltip(progress);
    }
    updateWindowModeButtons();
}

void HostContentComponent::updatePluginList()
{
    records = VST3HostEngine::getInstance().getPluginRecords();
    const auto active = VST3HostEngine::getInstance().getActivePluginPath();
    const juce::ScopedValueSetter<bool> guard(changingPlugin, true);
    pluginComboBox.clear(juce::dontSendNotification); pluginComboBox.addItem("[None - Bypass]", 1);
    int selected = 1;
    for (int i = 0; i < records.size(); ++i)
    {
        const auto& r = records.getReference(i);
        auto label = (r.name.isNotEmpty() ? r.name : juce::File(r.canonicalPath).getFileNameWithoutExtension())
            + (r.manufacturer.isNotEmpty() ? " - " + r.manufacturer : juce::String())
            + (r.category.isNotEmpty() ? " (" + r.category + ")" : juce::String())
            + " [" + bridge::architectureName(r.architecture)
            + (r.status == "Compatible" ? "]" : ", " + r.status + "]");
        pluginComboBox.addItem(label, i + 2);
        if (r.canonicalPath.equalsIgnoreCase(bridge::canonicalPath(bridge::vst3BundleRoot(juce::File(active))))) selected = i + 2;
    }
    pluginComboBox.setSelectedId(selected, juce::dontSendNotification);
    if (selected > 1)
    {
        const auto& r = records.getReference(selected - 2);
        removeButton.setButtonText("Plugin Actions...");
        juce::StringArray details;
        details.add("Name: " + (r.descriptiveName.isNotEmpty() ? r.descriptiveName
            : r.name.isNotEmpty() ? r.name : juce::File(r.canonicalPath).getFileNameWithoutExtension()));
        if (r.manufacturer.isNotEmpty()) details.add("Company: " + r.manufacturer);
        if (r.version.isNotEmpty()) details.add("Version: " + r.version);
        details.add("Path: " + r.canonicalPath);
        const auto text = details.joinIntoString(" | ");
        detailLabel.setText(text, false);
        detailLabel.setTooltip(text);
    }
    else
    {
        removeButton.setButtonText("Plugin Actions...");
        detailLabel.setText("Bypass active | No VST3 is processing audio", false);
        detailLabel.setTooltip("Bypass is active. Audio passes through the bridge without VST3 processing.");
    }
}

void HostContentComponent::clearEditorComponent()
{
    editorLayoutPassesRemaining = 0;
    initialEditorSize = {};
    preferredEditorSize = {};
    if (activeEditor)
    {
        activeEditor->removeKeyListener(this);
        activeEditor->removeMouseListener(this);
        removeChildComponent(activeEditor.get());
        activeEditor.reset();
    }
}

void HostContentComponent::resetPluginParameters()
{
    clearEditorComponent();
    VST3HostEngine::getInstance().resetActivePluginParameters();
    refreshEditorComponent();
}

void HostContentComponent::resetEditorSize()
{
    preferredEditorSize = {};
    if (auto* window = findParentComponentOfClass<HostWindow>()) window->resetToComfortableSize();
}

void HostContentComponent::refreshEditorComponent()
{
    editorInitialised = false;
    clearEditorComponent();
    auto& engine = VST3HostEngine::getInstance();
    const bool restoringState = engine.hasPendingPluginState();
    engine.applyPendingPluginState();
    auto* plugin = engine.getPluginInstance();
    if (plugin != nullptr && plugin->hasEditor())
    {
        VST3HostEngine::getInstance().writeLog("Creating VST3 editor");
        activeEditor.reset(plugin->createEditorAndMakeActive());
        VST3HostEngine::getInstance().writeLog("VST3 editor creation returned");
        if (activeEditor)
        {
            initialEditorSize = { activeEditor->getWidth(), activeEditor->getHeight() };
            if (restoringState) preferredEditorSize = initialEditorSize;
            activeEditor->addKeyListener(this);
            activeEditor->addMouseListener(this, true);
            addAndMakeVisible(activeEditor.get());
            editorLayoutPassesRemaining = 2;
        }
    }
    noEditorLabel.setVisible(activeEditor == nullptr);
    if (activeEditor == nullptr) editorLayoutPassesRemaining = 0;
    editorInitialised = true;
    lastEditorStateRevision = engine.getEditorStateRevision();
    resized();
    auto safe = juce::Component::SafePointer<HostContentComponent>(this);
    juce::MessageManager::callAsync([safe]
    {
        if (safe == nullptr || safe->activeEditor == nullptr) return;
        safe->resized();
        safe->activeEditor->resized();
    });
}

void HostContentComponent::ensureEditorInitialised()
{
    if (activeEditor == nullptr && VST3HostEngine::getInstance().getPluginInstance() != nullptr)
        refreshEditorComponent();
}

void HostContentComponent::openFolderManager()
{
    if (foldersWindow != nullptr && foldersWindow->getPeer() == nullptr) foldersWindow.reset();
    if (foldersWindow == nullptr)
    {
        juce::DialogWindow::LaunchOptions options; options.dialogTitle = "VST3 Search Folders";
        options.content.setOwned(new FolderManagerComponent()); options.useNativeTitleBar = true; options.resizable = true;
        foldersWindow.reset(options.create());
    }
    foldersWindow->setVisible(true);
    foldersWindow->setEnabled(true);
    foldersWindow->toFront(true);
}

HostWindow::HostWindow(bool preloadHidden)
    : DocumentWindow("AIMP VST3 Host Bridge", juce::Colour(background), DocumentWindow::allButtons)
{
    VST3HostEngine::getInstance().writeLog("Bridge DocumentWindow base created");
    bridgeLookAndFeel.setColourScheme(juce::LookAndFeel_V4::getDarkColourScheme());
    setLookAndFeel(&bridgeLookAndFeel);
    setUsingNativeTitleBar(true);
    VST3HostEngine::getInstance().writeLog("Bridge native peer configured");
    setContentOwned(new HostContentComponent(), true);
    VST3HostEngine::getInstance().writeLog("Bridge content attached");
    setResizable(true, true);
    setResizeLimits(1050, 520, 8192, 8192);
    setWantsKeyboardFocus(true);
    applyWindowSettings();
    if (preloadHidden) setAlpha(0.0f);
    setVisible(true);
    applyWindowSettings();
}

HostWindow::~HostWindow() { persistWindowState(); clearContentComponent(); setLookAndFeel(nullptr); }
bool HostWindow::isEditorInitialised() const
{
    const auto* content = dynamic_cast<const HostContentComponent*>(getContentComponent());
    return content != nullptr && content->isEditorInitialised();
}

void HostWindow::finishPreload(bool show)
{
    setVisible(false);
    setAlpha(1.0f);
    if (show) setVisible(true);
}

void HostWindow::closeButtonPressed()
{
    VST3HostEngine::getInstance().saveState();
    persistWindowState();
    setVisible(false);
}

void HostWindow::minimiseButtonPressed() { setMinimised(true); }
void HostWindow::maximiseButtonPressed() { toggleMaximized(); }

bool HostWindow::keyPressed(const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::F11Key) { toggleFullscreen(); return true; }
    if (key.getKeyCode() == juce::KeyPress::escapeKey)
    {
        const auto settings = VST3HostEngine::getInstance().getSettingsSnapshot();
        if (settings.fullscreen || settings.visualizerMode)
        {
            exitPresentationModes();
            return true;
        }
    }
    return false;
}

void HostWindow::moved() { DocumentWindow::moved(); schedulePersistence(); }
void HostWindow::resized()
{
    DocumentWindow::resized();
    if (!applyingState && !isFullScreen()) windowedBounds = getBounds();
    schedulePersistence();
}

void HostWindow::schedulePersistence() { if (!applyingState) startTimer(500); }
void HostWindow::timerCallback() { stopTimer(); persistWindowState(); }

void HostWindow::applyDecorations(bool hidden, bool minimal)
{
    setResizable(!hidden, !hidden);
    setDropShadowEnabled(!hidden);
    setUsingNativeTitleBar(!hidden && !minimal);
    setTitleBarButtonsRequired(hidden ? 0 : DocumentWindow::allButtons, false);
    setTitleBarHeight(hidden ? 0 : minimal ? 24 : 28);
    if (minimal) setColour(DocumentWindow::textColourId, juce::Colours::white);
}

void HostWindow::applyWindowSettings()
{
    const juce::ScopedValueSetter<bool> guard(applyingState, true);
    const auto settings = VST3HostEngine::getInstance().getSettingsSnapshot();
    setAlwaysOnTop(settings.alwaysOnTop);
    if (auto* content = dynamic_cast<HostContentComponent*>(getContentComponent()))
    {
        content->updateWindowModeButtons();
        content->setVisualizerMode(settings.visualizerMode);
    }
    if (!restoredWindowState)
    {
        juce::Rectangle<int> wanted(settings.logicalWindowBounds[0], settings.logicalWindowBounds[1], settings.logicalWindowBounds[2], settings.logicalWindowBounds[3]);
        const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(wanted);
        if (display == nullptr) display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
        if (display != nullptr) wanted = wanted.constrainedWithin(display->userBounds.toNearestInt());
        setBounds(wanted);
        windowedBounds = wanted;
        restoredWindowState = true;
    }
    if (isOnDesktop() && settings.fullscreen && !isFullScreen())
    {
        windowedBounds = getBounds();
        applyDecorations(true);
        setFullScreen(true);
        if (const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(windowedBounds))
            setBounds(display->logicalBounds.toNearestInt());
    }
    else if (isOnDesktop() && !settings.fullscreen && isFullScreen())
    {
        setFullScreen(false);
        setBounds(windowedBounds);
        applyDecorations(false, settings.visualizerMode);
    }
    else
        applyDecorations(settings.fullscreen, settings.visualizerMode);
}

void HostWindow::toggleFullscreen()
{
    auto settings = VST3HostEngine::getInstance().getSettingsSnapshot();
    const bool enter = !(settings.fullscreen || settings.visualizerMode);
    settings.fullscreen = enter;
    settings.visualizerMode = enter;
    VST3HostEngine::getInstance().updateSettings(settings);
    applyWindowSettings();
}

void HostWindow::toggleMaximized()
{
    if (isFullScreen()) { toggleFullscreen(); return; }
    if (auto* peer = getPeer())
    {
        const auto window = static_cast<HWND>(peer->getNativeHandle());
        ShowWindow(window, IsZoomed(window) ? SW_RESTORE : SW_MAXIMIZE);
    }
}

void HostWindow::exitPresentationModes()
{
    auto settings = VST3HostEngine::getInstance().getSettingsSnapshot();
    settings.fullscreen = false;
    settings.visualizerMode = false;
    VST3HostEngine::getInstance().updateSettings(settings);
    applyWindowSettings();
}

void HostWindow::bringToFrontOrFlash()
{
    if (auto* content = dynamic_cast<HostContentComponent*>(getContentComponent()))
        content->ensureEditorInitialised();
    setMinimised(false);
    setVisible(true);
    toFront(true);
    if (auto* peer = getPeer())
    {
        const auto window = static_cast<HWND>(peer->getNativeHandle());
        if (!SetForegroundWindow(window))
        {
            FLASHWINFO info { sizeof(FLASHWINFO), window, FLASHW_TRAY | FLASHW_TIMERNOFG, 3, 0 };
            FlashWindowEx(&info);
        }
    }
}

void HostWindow::resetToComfortableSize()
{
    const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(getBounds());
    if (display == nullptr) return;
    const auto available = display->userBounds.toNearestInt();
    const auto physical = bridge::comfortableWindowPhysicalSize({ display->physicalBounds.getWidth(), display->physicalBounds.getHeight() });
    const auto scale = juce::jmax(0.25, display->scale);
    const auto minimumWidth = juce::jmin(1050, available.getWidth());
    const auto minimumHeight = juce::jmin(520, available.getHeight());
    const auto width = juce::jlimit(minimumWidth, available.getWidth(), juce::roundToInt(physical[0] / scale));
    const auto height = juce::jlimit(minimumHeight, available.getHeight(), juce::roundToInt(physical[1] / scale));
    setResizeLimits(minimumWidth, minimumHeight, 8192, 8192);
    setBounds(available.withSizeKeepingCentre(width, height));
    windowedBounds = getBounds();
    persistWindowState();
}

void HostWindow::persistWindowState()
{
    if (applyingState || !isOnDesktop()) return;
    auto settings = VST3HostEngine::getInstance().getSettingsSnapshot();
    if (!settings.rememberWindow) return;
    const auto bounds = isFullScreen() ? windowedBounds : getBounds();
    settings.logicalWindowBounds = { bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight() };
    if (auto* peer = getPeer())
    {
        const auto window = static_cast<HWND>(peer->getNativeHandle());
        settings.savedDpi = static_cast<int>(GetDpiForWindow(window));
        settings.monitorName = monitorNameForWindow(window);
    }
    VST3HostEngine::getInstance().updateSettings(settings);
}

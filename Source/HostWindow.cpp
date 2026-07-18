#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "HostWindow.h"
#include "VST3HostEngine.h"

namespace
{
    constexpr auto background = 0xff292929;
    constexpr auto surface = 0xff343434;
    constexpr auto surfaceRaised = 0xff414141;
    constexpr auto activeSurface = 0xff5a3217;
    constexpr auto accent = 0xffff8500;
    constexpr auto mutedColour = 0xffc86400;
    constexpr auto soloColour = 0xffffa12b;
    constexpr int headerHeight = 70;
    constexpr int cardHeaderHeight = 92;
    constexpr int cardGap = 10;

    class FolderManagerComponent final : public juce::Component,
                                         private juce::ListBoxModel,
                                         private juce::Button::Listener,
                                         private juce::Timer
    {
    public:
        FolderManagerComponent()
        {
            list.setModel(this);
            list.setRowHeight(34);
            list.setColour(juce::ListBox::backgroundColourId, juce::Colour(surface));
            for (auto* component : std::initializer_list<juce::Component*> { &list, &add, &remove, &scan, &cancel })
                addAndMakeVisible(component);
            add.setButtonText("Add Folder"); remove.setButtonText("Remove");
            scan.setButtonText("Scan All"); cancel.setButtonText("Cancel");
            add.setTooltip("Add a folder containing VST3 plug-ins to the saved scan locations.");
            remove.setTooltip("Remove the selected scan folder; already discovered VST3 plug-ins remain available.");
            scan.setTooltip("Scan every saved and enabled folder for VST3 plug-ins.");
            cancel.setTooltip("Stop the current scan after the VST3 currently being inspected finishes.");
            list.setTooltip("Saved folders used by Scan All. Select a row to remove it.");
            for (auto* button : { &add, &remove, &scan, &cancel }) button->addListener(this);
            refresh(); startTimer(250); setSize(820, 460);
        }

        int getNumRows() override { return settings.scanFolders.size(); }
        void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(background)); }
        void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override
        {
            if (selected) g.fillAll(juce::Colour(0xff304357));
            if (!juce::isPositiveAndBelow(row, settings.scanFolders.size())) return;
            const auto& folder = settings.scanFolders.getReference(row);
            const auto path = folder.location.resolve(VST3HostEngine::getInstance().getRuntimePaths()).getFullPathName();
            g.setColour(juce::Colours::whitesmoke);
            g.drawFittedText(path, 10, 0, width - 20, height, juce::Justification::centredLeft, 1);
        }
        void resized() override
        {
            auto area = getLocalBounds().reduced(16);
            auto buttons = area.removeFromBottom(38);
            const auto buttonWidth = buttons.getWidth() / 4;
            for (auto* button : { &add, &remove, &scan, &cancel })
                button->setBounds(buttons.removeFromLeft(buttonWidth).reduced(3));
            list.setBounds(area);
        }

    private:
        void refresh()
        {
            settings = VST3HostEngine::getInstance().getSettingsSnapshot();
            list.updateContent();
        }
        void timerCallback() override
        {
            const auto scanning = VST3HostEngine::getInstance().isScanning();
            scan.setEnabled(!scanning); cancel.setEnabled(scanning);
        }
        void buttonClicked(juce::Button* button) override
        {
            auto& engine = VST3HostEngine::getInstance();
            if (button == &scan) engine.scanAll(true);
            else if (button == &cancel) engine.cancelScan();
            else if (button == &remove)
            {
                const auto row = list.getSelectedRow();
                if (juce::isPositiveAndBelow(row, settings.scanFolders.size()))
                { settings.scanFolders.remove(row); engine.updateSettings(settings); refresh(); }
            }
            else if (button == &add)
            {
                chooser = std::make_unique<juce::FileChooser>("Add VST3 scan folder", juce::File(), "*");
                auto safe = juce::Component::SafePointer<FolderManagerComponent>(this);
                chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                    [safe](const juce::FileChooser& selected)
                    {
                        if (safe == nullptr || !selected.getResult().isDirectory()) return;
                        const auto paths = VST3HostEngine::getInstance().getRuntimePaths();
                        safe->settings.scanFolders.add({ bridge::PathReference::fromFile(selected.getResult(), paths), true, true });
                        VST3HostEngine::getInstance().updateSettings(safe->settings);
                        safe->refresh();
                    });
            }
        }

        juce::ListBox list;
        juce::TextButton add, remove, scan, cancel;
        bridge::BridgeSettings settings;
        std::unique_ptr<juce::FileChooser> chooser;
    };

    class SettingsComponent final : public juce::Component,
                                    private juce::Button::Listener,
                                    private juce::ComboBox::Listener
    {
    public:
        explicit SettingsComponent(std::function<void()> forgetCallbackIn)
            : forgetCallback(std::move(forgetCallbackIn))
        {
            settings = VST3HostEngine::getInstance().getSettingsSnapshot();
            storage.addItemList({ "Automatic", "Portable", "User profile" }, 1);
            startup.addItemList({ "Restore rack", "Start with empty rack" }, 1);
            storage.setSelectedId(settings.requestedStorageMode == bridge::StorageMode::portable ? 2
                : settings.requestedStorageMode == bridge::StorageMode::userProfile ? 3 : 1);
            startup.setSelectedId(settings.startupMode == "none" ? 2 : 1);
            storage.addListener(this); startup.addListener(this);
            storage.setTooltip("Choose where this rack stores its independent configuration and VST3 states.");
            startup.setTooltip("Restore the saved rack on startup or begin with an empty rack.");
            storageLabel.setText("Configuration storage", juce::dontSendNotification);
            startupLabel.setText("Rack startup", juce::dontSendNotification);
            storageLabel.setTooltip(storage.getTooltip()); startupLabel.setTooltip(startup.getTooltip());
            configure(scanBridge, "Include rack folder when scanning", settings.scanBridgeFolder,
                      "Include the installed rack folder and its VST3 subfolder in Scan All.");
            configure(scanSystem, "Include system VST3 folders", settings.scanSystemFolders,
                      "Include the standard Windows VST3 folders in Scan All.");
            configure(openWithAimp, "Open rack window with AIMP", settings.openRackOnStartup,
                      "Show the rack window automatically when AIMP starts this DSP.");
            openLog.setButtonText("Open Log"); openConfig.setButtonText("Open Config Folder");
            diagnostics.setButtonText("Copy Diagnostics"); resetCache.setButtonText("Reset Scan Cache");
            forgetAll.setButtonText("Forget All Plugins");
            openLog.setTooltip("Open the current VST3 Bridge Rack For AIMP diagnostic log.");
            openConfig.setTooltip("Open the folder containing this rack's independent configuration.");
            diagnostics.setTooltip("Copy rack, architecture, paths and plug-in diagnostics to the clipboard.");
            resetCache.setTooltip("Clear scan fingerprints and quarantines while keeping the rack and saved states.");
            forgetAll.setTooltip("Clear the rack and forget every scanned VST3 entry. Plug-in files are not deleted.");
            for (auto* component : std::initializer_list<juce::Component*> { &storageLabel, &storage, &startupLabel, &startup,
                     &scanBridge, &scanSystem, &openWithAimp, &openLog, &openConfig, &diagnostics, &resetCache, &forgetAll }) addAndMakeVisible(component);
            for (auto* button : { &openLog, &openConfig, &diagnostics, &resetCache, &forgetAll }) button->addListener(this);
            setSize(720, 365);
        }
        ~SettingsComponent() override { commit(); }
        void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(background)); }
        void resized() override
        {
            auto area = getLocalBounds().reduced(24);
            auto row = area.removeFromTop(36); storageLabel.setBounds(row.removeFromLeft(190)); storage.setBounds(row);
            area.removeFromTop(10); row = area.removeFromTop(36); startupLabel.setBounds(row.removeFromLeft(190)); startup.setBounds(row);
            area.removeFromTop(18); scanBridge.setBounds(area.removeFromTop(34)); scanSystem.setBounds(area.removeFromTop(34));
            openWithAimp.setBounds(area.removeFromTop(34));
            area.removeFromTop(20); row = area.removeFromTop(38);
            const auto buttonWidth = row.getWidth() / 5;
            for (auto* button : { &openLog, &openConfig, &diagnostics, &resetCache, &forgetAll })
                button->setBounds(row.removeFromLeft(buttonWidth).reduced(3));
        }
    private:
        void configure(juce::ToggleButton& button, const juce::String& text, bool value, const juce::String& tooltip)
        { button.setButtonText(text); button.setToggleState(value, juce::dontSendNotification); button.setTooltip(tooltip); button.addListener(this); }
        void comboBoxChanged(juce::ComboBox*) override { commit(); }
        void buttonClicked(juce::Button* button) override
        {
            auto& engine = VST3HostEngine::getInstance();
            if (button == &openLog) engine.getLogFile().startAsProcess();
            else if (button == &openConfig) engine.getConfigFile().getParentDirectory().startAsProcess();
            else if (button == &diagnostics) juce::SystemClipboard::copyTextToClipboard(engine.getDiagnostics());
            else if (button == &resetCache) engine.resetScanCache();
            else if (button == &forgetAll)
            {
                if (forgetCallback) forgetCallback();
                settings = engine.getSettingsSnapshot();
            }
            else commit();
        }
        void commit()
        {
            settings.requestedStorageMode = storage.getSelectedId() == 2 ? bridge::StorageMode::portable
                : storage.getSelectedId() == 3 ? bridge::StorageMode::userProfile : bridge::StorageMode::automatic;
            settings.startupMode = startup.getSelectedId() == 2 ? "none" : "restoreLast";
            settings.openRackOnStartup = openWithAimp.getToggleState();
            settings.scanBridgeFolder = scanBridge.getToggleState();
            settings.scanSystemFolders = scanSystem.getToggleState();
            VST3HostEngine::getInstance().updateSettings(settings);
        }
        bridge::BridgeSettings settings;
        juce::Label storageLabel, startupLabel;
        juce::ComboBox storage, startup;
        juce::ToggleButton scanBridge, scanSystem, openWithAimp;
        juce::TextButton openLog, openConfig, diagnostics, resetCache, forgetAll;
        std::function<void()> forgetCallback;
    };
}

class DetachedEditorWindow final : public juce::DocumentWindow, private juce::KeyListener, private juce::Timer
{
    class EditorBackdrop final : public juce::Component
    {
    public:
        void attach(juce::AudioProcessorEditor& editorIn, juce::Colour colourIn)
        {
            editor = &editorIn;
            colour = colourIn;
            naturalSize = { editor->getWidth(), editor->getHeight() };
            growable = editor->isResizable();
            if (growable)
                if (auto* sizeConstrainer = editor->getConstrainer())
                {
                    auto candidate = juce::Rectangle<int>(0, 0, naturalSize.x + 256, naturalSize.y + 256);
                    sizeConstrainer->checkBounds(candidate, editor->getBounds(), { 0, 0, 8192, 8192 },
                                                 false, false, true, true);
                    growable = candidate.getWidth() > naturalSize.x || candidate.getHeight() > naturalSize.y;
                }
            setSize(naturalSize.x, naturalSize.y);
            setOpaque(true);
            addAndMakeVisible(editor);
        }
        void paint(juce::Graphics& g) override { g.fillAll(colour); }
        void resized() override
        {
            if (editor == nullptr) return;
            auto target = getLocalBounds();
            if (!growable)
                target = target.withSizeKeepingCentre(naturalSize.x, naturalSize.y);
            editor->setBounds(target);
        }
        void syncEditorSize()
        {
            if (editor == nullptr) return;
            const juce::Point<int> current { editor->getWidth(), editor->getHeight() };
            if (!growable && current.x > 0 && current.y > 0 && current != naturalSize)
            {
                naturalSize = current;
                resized();
            }
        }
        bool isGrowable() const { return growable; }
        juce::Point<int> getNaturalSize() const { return naturalSize; }
    private:
        juce::AudioProcessorEditor* editor = nullptr;
        juce::Point<int> naturalSize;
        juce::Colour colour;
        bool growable = false;
    };

public:
    DetachedEditorWindow(int slot, bool fullScreen, juce::Rectangle<int> sourceBounds)
        : DocumentWindow(pluginName(slot), fullScreen ? juce::Colour(0xff1e1e1e) : juce::Colour(surface),
                         DocumentWindow::closeButton)
    {
        lookAndFeel.setColourScheme(juce::LookAndFeel_V4::getDarkColourScheme());
        setLookAndFeel(&lookAndFeel);
        setUsingNativeTitleBar(false);
        setTitleBarHeight(fullScreen ? 0 : 30);
        setTitleBarTextCentred(true);
        setColour(DocumentWindow::textColourId, juce::Colours::white);
        setTitleBarButtonsRequired(DocumentWindow::closeButton, false);
        if (auto* processor = VST3HostEngine::getInstance().getRackPluginInstance(slot))
            editor.reset(processor->createEditorAndMakeActive());
        if (editor != nullptr)
        {
            const auto backdropColour = fullScreen ? juce::Colour(0xff1e1e1e) : juce::Colour(surface);
            editor->setColour(juce::ResizableWindow::backgroundColourId, backdropColour);
            editor->addKeyListener(this);
            backdrop.attach(*editor, backdropColour);
            usesBackdrop = fullScreen || backdrop.isGrowable();
            setContentNonOwned(usesBackdrop ? static_cast<juce::Component*>(&backdrop)
                                            : static_cast<juce::Component*>(editor.get()), true);
            if (fullScreen || !backdrop.isGrowable())
                setResizable(false, false);
            else
            {
                setResizable(backdrop.isGrowable(), true);
                setResizeLimits(1, 1, 8192, 8192);
            }
        }
        addKeyListener(this);
        const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(sourceBounds);
        if (display != nullptr)
        {
            const auto available = display->userBounds.toNearestInt();
            if (!fullScreen && editor != nullptr && backdrop.isGrowable())
            {
                const auto physical = bridge::comfortableWindowPhysicalSize(
                    { display->physicalBounds.getWidth(), display->physicalBounds.getHeight() });
                const auto scale = juce::jmax(0.25, display->scale);
                const auto minimumWidth = juce::jmin(1050, available.getWidth());
                const auto minimumHeight = juce::jmin(520, available.getHeight());
                const auto width = juce::jlimit(minimumWidth, available.getWidth(),
                                                 juce::roundToInt(physical[0] / scale));
                const auto height = juce::jlimit(minimumHeight, available.getHeight(),
                                                  juce::roundToInt(physical[1] / scale));
                setBounds(available.withSizeKeepingCentre(width, height));
            }
            else if (!fullScreen && editor != nullptr)
            {
                const auto natural = backdrop.getNaturalSize();
                setBounds(available.withSizeKeepingCentre(natural.x, natural.y + getTitleBarHeight()));
            }
            else
                setBounds(available.withSizeKeepingCentre(getWidth(), getHeight()));
        }
        else
        {
            const auto natural = backdrop.getNaturalSize();
            centreWithSize(!fullScreen && !usesBackdrop ? natural.x : getWidth(),
                           !fullScreen && !usesBackdrop ? natural.y + getTitleBarHeight() : getHeight());
        }
        setVisible(true);
        toFront(true);
        if (fullScreen) juce::Desktop::getInstance().setKioskModeComponent(this, false);
        startTimer(35);
    }

    ~DetachedEditorWindow() override
    {
        if (juce::Desktop::getInstance().getKioskModeComponent() == this)
            juce::Desktop::getInstance().setKioskModeComponent(nullptr);
        if (editor != nullptr) editor->removeKeyListener(this);
        clearContentComponent();
        setLookAndFeel(nullptr);
    }

    void closeButtonPressed() override { exitMode(); }
    bool keyPressed(const juce::KeyPress& key) override { return handleKey(key); }

private:
    static juce::String pluginName(int slot)
    {
        if (auto* plugin = VST3HostEngine::getInstance().getRackPluginInstance(slot)) return plugin->getName();
        return "VST3";
    }

    bool keyPressed(const juce::KeyPress& key, juce::Component*) override { return handleKey(key); }
    void timerCallback() override
    {
        if (usesBackdrop)
        {
            backdrop.syncEditorSize();
            backdrop.resized();
        }
        if (!isActiveWindow()) return;
        const auto down = juce::KeyPress::isKeyCurrentlyDown(juce::KeyPress::escapeKey)
                       || juce::KeyPress::isKeyCurrentlyDown(juce::KeyPress::F11Key);
        if (!shortcutArmed) { shortcutArmed = !down; return; }
        if (down) exitMode();
    }
    bool handleKey(const juce::KeyPress& key)
    {
        if (key.getKeyCode() != juce::KeyPress::escapeKey && key.getKeyCode() != juce::KeyPress::F11Key) return false;
        exitMode();
        return true;
    }
    void exitMode()
    {
        if (closing) return;
        closing = true;
        stopTimer();
        if (juce::Desktop::getInstance().getKioskModeComponent() == this)
            juce::Desktop::getInstance().setKioskModeComponent(nullptr);
        setVisible(false);
    }

    juce::LookAndFeel_V4 lookAndFeel;
    EditorBackdrop backdrop;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    bool usesBackdrop = false;
    bool closing = false;
    bool shortcutArmed = false;
};

class RackCardComponent final : public juce::Component,
                                private juce::Button::Listener,
                                private juce::KeyListener,
                                private juce::Timer
{
public:
    RackCardComponent(HostContentComponent& ownerIn, int indexIn, bool expandedIn, bool activeIn)
        : owner(ownerIn), index(indexIn), active(activeIn)
    {
        auto& engine = VST3HostEngine::getInstance();
        const auto paths = engine.getRackPluginPaths();
        const auto path = juce::isPositiveAndBelow(index, paths.size()) ? paths[index] : juce::String();
        auto* processor = engine.getRackPluginInstance(index);
        juce::String company = "Unknown company", category = "Unclassified";
        const auto canonical = bridge::canonicalPath(bridge::vst3BundleRoot(juce::File(path)));
        for (const auto& record : engine.getPluginRecords())
            if (record.canonicalPath.equalsIgnoreCase(canonical))
            {
                if (record.manufacturer.isNotEmpty()) company = record.manufacturer;
                if (record.category.isNotEmpty()) category = record.category;
                break;
            }
        number.setText(juce::String(index + 1).paddedLeft('0', 2), juce::dontSendNotification);
        name.setText(processor != nullptr ? processor->getName() : juce::File(path).getFileNameWithoutExtension(), juce::dontSendNotification);
        info.setText(company + "  |  " + category + "  |  " + path, juce::dontSendNotification);
        number.setTooltip("Rack position " + juce::String(index + 1) + ". Drag this card to reorder the chain.");
        name.setTooltip("VST3 in rack slot " + juce::String(index + 1) + ": " + path);
        info.setTooltip("Company: " + company + " | Class: " + category + " | VST3 bundle: " + path);
        number.setInterceptsMouseClicks(false, false); name.setInterceptsMouseClicks(false, false); info.setInterceptsMouseClicks(false, false);
        name.setFont(juce::FontOptions(18.0f).withStyle("Bold"));
        number.setFont(juce::FontOptions(17.0f));
        info.setColour(juce::Label::textColourId, juce::Colours::lightgrey);

        configure(expand, expandedIn ? "^" : "v",
                  "Show or hide this VST3 GUI inside its rack card.");
        configure(mute, "M", "Mute this slot so audio skips this VST3.");
        configure(solo, "S", "Solo this slot so audio skips every non-solo rack slot.");
        configure(up, "^", "Move this card one position up.");
        configure(down, "v", "Move this card one position down.");
        configure(add, "+", "Open the VST3 menu and insert a new slot below this card.");
        configure(remove, "-", "Remove this slot from the rack.");
        configure(clone, "Clone", "Clone this VST3, including its state, above or below this card.");
        configure(replace, "Replace...", "Replace this card with a scanned VST3 or choose Add VST to scan one file.");
        configure(actions, "Actions...", "Reload, reset or forget this VST3, reveal its file, or open Detached/Full Screen mode.");
        mute.setClickingTogglesState(true); solo.setClickingTogglesState(true);
        mute.setToggleState(engine.isRackSlotMuted(index), juce::dontSendNotification);
        solo.setToggleState(engine.isRackSlotSolo(index), juce::dontSendNotification);
        setExpanded(expandedIn);
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    }

    ~RackCardComponent() override { stopTimer(); clearEditor(); }

    int preferredHeight() const { return cardHeaderHeight + (editor != nullptr ? editorHeight + 28 : 0); }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(juce::Colour(active ? activeSurface : surfaceRaised)); g.fillRoundedRectangle(bounds, 10.0f);
        g.setColour(solo.getToggleState() ? juce::Colour(soloColour)
                    : mute.getToggleState() ? juce::Colour(mutedColour) : juce::Colour(0xff686868));
        g.drawRoundedRectangle(bounds, 10.0f, 1.5f);
        g.setColour(juce::Colour(accent));
        g.fillRoundedRectangle(juce::Rectangle<float>(8.0f, 15.0f, 4.0f, 62.0f), 2.0f);
        if (dropEdge != 0)
        {
            g.setColour(juce::Colour(accent));
            const auto y = dropEdge < 0 ? 1.0f : static_cast<float>(getHeight() - 5);
            g.fillRoundedRectangle(16.0f, y, static_cast<float>(getWidth() - 32), 4.0f, 2.0f);
        }
    }

    void resized() override
    {
        auto header = getLocalBounds().removeFromTop(cardHeaderHeight).reduced(14, 10);
        number.setBounds(header.removeFromLeft(38));
        const auto identityWidth = juce::jlimit(180, 380,
            juce::GlyphArrangement::getStringWidthInt(name.getFont(), name.getText()) + 32);
        auto identity = header.removeFromLeft(identityWidth);
        name.setBounds(identity.removeFromTop(38));
        expand.setBounds(identity.removeFromLeft(38).reduced(3));
        mute.setBounds(header.removeFromLeft(50).reduced(2));
        solo.setBounds(header.removeFromLeft(50).reduced(2));
        header.removeFromLeft(12);
        auto actionArea = header.removeFromRight(520);
        for (auto* button : { &up, &down, &add, &remove }) button->setBounds(actionArea.removeFromLeft(48).reduced(2));
        clone.setBounds(actionArea.removeFromLeft(84).reduced(2));
        replace.setBounds(actionArea.removeFromLeft(116).reduced(2));
        actions.setBounds(actionArea.reduced(2));
        info.setBounds(header.reduced(4, 2));
        if (editor != nullptr)
        {
            auto editorArea = getLocalBounds().withTrimmedTop(cardHeaderHeight).reduced(12, 6);
            resizeHandle.setBounds(editorArea.removeFromBottom(16));
            if (editorResizable) editor->setBounds(editorArea);
            else editor->setBounds(editorArea.withSizeKeepingCentre(naturalEditorWidth, naturalEditorHeight));
        }
    }

    void mouseDown(const juce::MouseEvent& event) override
    { owner.activateSlot(index); dragOrigin = event.getPosition(); dragging = false; }
    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (!dragging && event.getDistanceFromDragStart() > 6)
        {
            dragging = true;
            owner.startDragging(juce::var(index), this);
        }
    }

    void setDropIndicator(int edge) { if (dropEdge != edge) { dropEdge = edge; repaint(); } }
    void setActive(bool shouldBeActive)
    {
        if (active == shouldBeActive) return;
        active = shouldBeActive;
        updateEditorBackground();
        repaint();
    }

private:
    void configure(juce::TextButton& button, const juce::String& text, const juce::String& tooltip)
    { button.setButtonText(text); button.setTooltip(tooltip); button.addListener(this); addAndMakeVisible(button); }
    void clearEditor()
    {
        if (editor != nullptr) { editor->removeKeyListener(this); removeChildComponent(editor.get()); editor.reset(); }
    }
    void updateEditorBackground()
    {
        if (editor == nullptr) return;
        editor->setColour(juce::ResizableWindow::backgroundColourId,
                          juce::Colour(active ? activeSurface : surfaceRaised));
        editor->repaint();
    }
    void setExpanded(bool shouldExpand)
    {
        clearEditor();
        for (auto* component : std::initializer_list<juce::Component*> { &number, &name, &info }) addAndMakeVisible(component);
        if (!shouldExpand) return;
        auto& engine = VST3HostEngine::getInstance();
        engine.selectRackSlot(index); engine.applyPendingPluginState();
        auto* processor = engine.getRackPluginInstance(index);
        if (processor == nullptr || !processor->hasEditor()) return;
        editor.reset(processor->createEditorAndMakeActive());
        if (editor != nullptr)
        {
            naturalEditorWidth = editor->getWidth();
            naturalEditorHeight = editor->getHeight();
            editorResizable = editor->isResizable();
            safeMinimumEditorHeight = naturalEditorHeight;
            if (editorResizable)
                if (auto* constrainer = editor->getConstrainer())
                {
                    auto candidate = juce::Rectangle<int>(0, 0, naturalEditorWidth, 1);
                    constrainer->checkBounds(candidate, editor->getBounds(), { 0, 0, 4096, 4096 },
                                             false, false, true, false);
                    const auto reportedMinimum = juce::jmax(constrainer->getMinimumHeight(), candidate.getHeight());
                    if (reportedMinimum >= 120) safeMinimumEditorHeight = reportedMinimum;
                }
            safeMinimumEditorHeight = juce::jlimit(120, 2400, safeMinimumEditorHeight);
            const auto cachedHeight = engine.getRackSlotEditorHeight(index);
            editorHeight = juce::jlimit(safeMinimumEditorHeight, 2400,
                cachedHeight > 0 ? cachedHeight : naturalEditorHeight);
            if (cachedHeight != editorHeight) engine.setRackSlotEditorHeight(index, editorHeight);
            resizeHandle.setMinimumHeight(safeMinimumEditorHeight);
            editor->addKeyListener(this);
            updateEditorBackground();
            addAndMakeVisible(editor.get());
            addAndMakeVisible(resizeHandle);
            pendingEditorRelayouts = 3;
            startTimer(300);
        }
    }
    void timerCallback() override
    {
        if (editor != nullptr && pendingEditorRelayouts > 0)
        {
            --pendingEditorRelayouts;
            resized();
            editor->repaint();
            return;
        }
        if (editorResizable && editor != nullptr && editor->getHeight() != editorHeight)
        {
            editorHeight = juce::jlimit(safeMinimumEditorHeight, 2400, editor->getHeight());
            VST3HostEngine::getInstance().setRackSlotEditorHeight(index, editorHeight);
            owner.resized();
        }
    }
    bool keyPressed(const juce::KeyPress& key, juce::Component*) override
    {
        if (key.getKeyCode() == juce::KeyPress::F11Key)
        {
            auto safeOwner = juce::Component::SafePointer<HostContentComponent>(&owner);
            const auto slot = index;
            juce::MessageManager::callAsync([safeOwner, slot]
            { if (safeOwner != nullptr) safeOwner->openDetachedSlot(slot, true); });
            return true;
        }
        if (key.getKeyCode() == juce::KeyPress::leftKey || key.getKeyCode() == juce::KeyPress::upKey)
        { owner.selectAdjacentGui(-1); return true; }
        if (key.getKeyCode() == juce::KeyPress::rightKey || key.getKeyCode() == juce::KeyPress::downKey)
        { owner.selectAdjacentGui(1); return true; }
        return false;
    }
    void buttonClicked(juce::Button* button) override
    {
        owner.activateSlot(index);
        if (button == &expand) owner.toggleSlotEditor(index);
        else if (button == &mute) owner.setSlotMuted(index, mute.getToggleState());
        else if (button == &solo) owner.setSlotSolo(index, solo.getToggleState());
        else if (button == &up) owner.moveSlot(index, index - 1);
        else if (button == &down) owner.moveSlot(index, index + 1);
        else if (button == &add) owner.showAddMenu(index + 1);
        else if (button == &remove) owner.removeSlot(index);
        else if (button == &clone)
        {
            juce::PopupMenu menu; menu.addItem(1, "Clone above"); menu.addItem(2, "Clone below");
            auto safe = juce::Component::SafePointer<RackCardComponent>(this);
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&clone), [safe](int result)
            { if (safe != nullptr && result > 0) safe->owner.cloneSlot(safe->index, result == 1); });
        }
        else if (button == &replace) owner.showReplaceMenu(index);
        else if (button == &actions)
        {
            juce::PopupMenu menu;
            menu.addItem(1, "Reset plugin parameters");
            menu.addItem(6, "Reload plugin");
            menu.addItem(2, "Open plugin location");
            menu.addSeparator();
            menu.addItem(3, "Detached mode");
            menu.addItem(4, "Full Screen (F11)");
            menu.addSeparator();
            menu.addItem(5, "Forget current plugin");
            auto safe = juce::Component::SafePointer<RackCardComponent>(this);
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&actions), [safe](int result)
            {
                if (safe == nullptr) return;
                if (result == 1) safe->owner.resetSlotParameters(safe->index);
                else if (result == 6)
                {
                    auto safeOwner = juce::Component::SafePointer<HostContentComponent>(&safe->owner);
                    const auto slot = safe->index;
                    juce::MessageManager::callAsync([safeOwner, slot]
                    { if (safeOwner != nullptr) safeOwner->reloadSlot(slot); });
                }
                else if (result == 2) safe->owner.openSlotLocation(safe->index);
                else if (result == 3) safe->owner.openDetachedSlot(safe->index, false);
                else if (result == 4) safe->owner.openDetachedSlot(safe->index, true);
                else if (result == 5) safe->owner.forgetPlugin(safe->index);
            });
        }
    }

    class EditorResizeHandle final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit EditorResizeHandle(RackCardComponent& cardIn) : card(cardIn)
        { setMouseCursor(juce::MouseCursor::UpDownResizeCursor); }
        void setMinimumHeight(int minimum)
        { setTooltip("Drag to resize this VST3 GUI. Safe minimum: " + juce::String(minimum) + " px."); }
        void paint(juce::Graphics& g) override
        {
            g.setColour(juce::Colour(accent));
            g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(80.0f, 3.0f), 2.0f);
        }
        void mouseDown(const juce::MouseEvent&) override { startHeight = card.editorHeight; }
        void mouseDrag(const juce::MouseEvent& event) override
        {
            card.editorHeight = juce::jlimit(card.safeMinimumEditorHeight, 2400,
                                             startHeight + event.getDistanceFromDragStartY());
            card.owner.resized();
        }
        void mouseUp(const juce::MouseEvent&) override
        { VST3HostEngine::getInstance().setRackSlotEditorHeight(card.index, card.editorHeight); }
    private:
        RackCardComponent& card;
        int startHeight = 0;
    };

    HostContentComponent& owner;
    int index;
    int editorHeight = 0;
    int naturalEditorWidth = 0;
    int naturalEditorHeight = 0;
    int safeMinimumEditorHeight = 240;
    int pendingEditorRelayouts = 0;
    bool dragging = false;
    bool active = false;
    bool editorResizable = false;
    int dropEdge = 0;
    juce::Point<int> dragOrigin;
    juce::Label number, name, info;
    juce::TextButton expand, mute, solo, up, down, add, remove, clone, replace, actions;
    EditorResizeHandle resizeHandle { *this };
    std::unique_ptr<juce::AudioProcessorEditor> editor;
};

void RackCanvas::paint(juce::Graphics& g) { g.fillAll(juce::Colour(background)); }
void RackViewport::paint(juce::Graphics& g) { g.fillAll(juce::Colour(background)); }

HostContentComponent::HostContentComponent()
{
    title.setText("VST3 Bridge Rack For AIMP", juce::dontSendNotification);
    title.setFont(juce::FontOptions(23.0f).withStyle("Bold"));
    title.setColour(juce::Label::textColourId, juce::Colour(accent));
    title.setTooltip("Ordered VST3 processing rack. Audio flows from slot 01 downward.");
    for (auto* button : { &addButton, &cardStatesButton, &foldersButton, &settingsButton }) { button->addListener(this); addAndMakeVisible(button); }
    addButton.setTooltip("Open all scanned VST3 plug-ins or choose Add VST to scan one file, then append it to the rack.");
    foldersButton.setTooltip("Manage VST3 search folders and run a scan.");
    settingsButton.setTooltip("Open rack storage, startup, scan and diagnostic settings.");
    cardStatesButton.setTooltip("Expand every card, collapse every card, or remove all rack cards.");
    statusLabel.setReadOnly(true); statusLabel.setMultiLine(false, false); statusLabel.setScrollbarsShown(false);
    statusLabel.setCaretVisible(false); statusLabel.setPopupMenuEnabled(false);
    statusLabel.setColour(juce::TextEditor::backgroundColourId, juce::Colour(surface));
    statusLabel.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    statusLabel.setTooltip("Rack status, scanning progress and load errors.");
    viewport.setViewedComponent(&rackContent, false);
    viewport.setScrollBarsShown(true, false);
    viewport.setScrollBarThickness(12);
    viewport.getVerticalScrollBar().setColour(juce::ScrollBar::backgroundColourId, juce::Colour(background));
    viewport.getVerticalScrollBar().setColour(juce::ScrollBar::trackColourId, juce::Colour(surface));
    viewport.getVerticalScrollBar().setColour(juce::ScrollBar::thumbColourId, juce::Colour(accent));
    viewport.setTooltip("Scroll vertically through the rack cards. Cards process audio from top to bottom.");
    setOpaque(true); viewport.setOpaque(true); rackContent.setOpaque(true);
    for (auto* component : std::initializer_list<juce::Component*> { &title, &statusLabel, &viewport }) addAndMakeVisible(component);
    refreshCards(); startTimer(300); editorInitialised = true;
}

HostContentComponent::~HostContentComponent()
{
    stopTimer(); detachedWindow.reset(); cards.clear(); viewport.setViewedComponent(nullptr, false);
}

void HostContentComponent::paint(juce::Graphics& g) { g.fillAll(juce::Colour(background)); }

void HostContentComponent::resized()
{
    auto area = getLocalBounds().reduced(14);
    auto header = area.removeFromTop(headerHeight);
    title.setBounds(header.removeFromLeft(310));
    addButton.setBounds(header.removeFromLeft(130).reduced(4, 14));
    cardStatesButton.setBounds(header.removeFromLeft(130).reduced(4, 14));
    foldersButton.setBounds(header.removeFromLeft(130).reduced(4, 14));
    settingsButton.setBounds(header.removeFromLeft(110).reduced(4, 14));
    statusLabel.setBounds(header.reduced(8, 14));
    viewport.setBounds(area);
    layoutCards();
}

void HostContentComponent::refreshCards()
{
    auto& engine = VST3HostEngine::getInstance();
    const auto paths = engine.getRackPluginPaths();
    if (keyboardSlot < 0) keyboardSlot = engine.getSelectedRackSlot();
    cards.clear(); rackContent.removeAllChildren();
    for (int i = 0; i < paths.size(); ++i)
    {
        auto* card = cards.add(new RackCardComponent(*this, i, expandedSlots.contains(i), i == keyboardSlot));
        rackContent.addAndMakeVisible(card);
    }
    if (juce::isPositiveAndBelow(keyboardSlot, paths.size())) engine.selectRackSlot(keyboardSlot);
    lastRackPaths = paths; layoutCards();
}

void HostContentComponent::layoutCards()
{
    const auto width = juce::jmax(720, viewport.getWidth() - viewport.getScrollBarThickness());
    int y = cardGap;
    for (auto* card : cards)
    {
        const auto height = card->preferredHeight();
        card->setBounds(cardGap, y, width - cardGap * 2, height);
        y += height + cardGap;
    }
    rackContent.setSize(width, juce::jmax(viewport.getHeight(), y));
}

void HostContentComponent::toggleSlotEditor(int index)
{
    if (expandedSlots.contains(index)) expandedSlots.removeFirstMatchingValue(index);
    else expandedSlots.add(index);
    keyboardSlot = index;
    refreshCards();
    if (juce::isPositiveAndBelow(index, cards.size()))
        viewport.getVerticalScrollBar().setCurrentRangeStart(cards[index]->getY());
}

void HostContentComponent::activateSlot(int index)
{
    if (!juce::isPositiveAndBelow(index, cards.size())) return;
    keyboardSlot = index;
    VST3HostEngine::getInstance().selectRackSlot(index);
    for (int i = 0; i < cards.size(); ++i) cards[i]->setActive(i == index);
}

void HostContentComponent::selectAdjacentGui(int delta)
{
    const auto count = VST3HostEngine::getInstance().getRackSize();
    if (count == 0) return;
    keyboardSlot = (juce::jmax(0, keyboardSlot) + delta + count) % count;
    expandedSlots.clear();
    expandedSlots.add(keyboardSlot);
    refreshCards();
    viewport.getVerticalScrollBar().setCurrentRangeStart(cards[keyboardSlot]->getY());
}

void HostContentComponent::moveSlot(int from, int to)
{
    const auto count = VST3HostEngine::getInstance().getRackSize();
    if (!juce::isPositiveAndBelow(from, count) || !juce::isPositiveAndBelow(to, count) || from == to) return;
    VST3HostEngine::getInstance().moveRackSlot(from, to);
    const auto remap = [from, to](int index)
    {
        if (index == from) return to;
        if (from < to && index > from && index <= to) return index - 1;
        if (from > to && index >= to && index < from) return index + 1;
        return index;
    };
    for (int i = 0; i < expandedSlots.size(); ++i) expandedSlots.set(i, remap(expandedSlots[i]));
    if (keyboardSlot >= 0) keyboardSlot = remap(keyboardSlot);
    refreshCards();
}

void HostContentComponent::removeSlot(int index)
{
    cards.clear();
    VST3HostEngine::getInstance().removeRackSlot(index);
    expandedSlots.removeFirstMatchingValue(index);
    for (int i = 0; i < expandedSlots.size(); ++i)
        if (expandedSlots[i] > index) expandedSlots.set(i, expandedSlots[i] - 1);
    if (keyboardSlot == index) keyboardSlot = -1;
    else if (keyboardSlot > index) --keyboardSlot;
    refreshCards();
}

void HostContentComponent::cloneSlot(int index, bool above)
{
    auto& engine = VST3HostEngine::getInstance();
    if (engine.getRackSize() >= bridge::maxRackSlots)
    { statusLabel.setText("Rack full: maximum 10 slots.", false); return; }
    const auto target = above ? index : index + 1;
    if (engine.cloneRackSlot(index, target))
    {
        for (int i = 0; i < expandedSlots.size(); ++i)
            if (expandedSlots[i] >= target) expandedSlots.set(i, expandedSlots[i] + 1);
        expandedSlots.addIfNotAlreadyThere(target);
        keyboardSlot = target;
        refreshCards();
    }
}

void HostContentComponent::reloadSlot(int index)
{
    auto& engine = VST3HostEngine::getInstance();
    if (!beginReplacement(index)) return;
    const auto path = replacedPluginPath;
    if (!engine.loadPlugin(path, false))
    {
        rollbackReplacement();
        return;
    }
    if (auto* plugin = engine.getRackPluginInstance(engine.getRackSize() - 1))
        plugin->setStateInformation(replacedPluginState.getData(), static_cast<int>(replacedPluginState.getSize()));
    finishReplacement();
    statusLabel.setText("Reloaded VST3: " + juce::File(path).getFileNameWithoutExtension(), false);
}

void HostContentComponent::setSlotMuted(int index, bool muted) { VST3HostEngine::getInstance().setRackSlotMuted(index, muted); }
void HostContentComponent::setSlotSolo(int index, bool solo) { VST3HostEngine::getInstance().setRackSlotSolo(index, solo); }

void HostContentComponent::showAddMenu(int insertAt)
{
    auto& engine = VST3HostEngine::getInstance();
    if (engine.getRackSize() >= bridge::maxRackSlots)
    { statusLabel.setText("Rack full: maximum 10 slots.", false); return; }
    records = engine.getPluginRecords();
    juce::PopupMenu menu;
    menu.addItem(1, "Add VST...");
    if (!records.isEmpty()) menu.addSeparator();
    for (int i = 0; i < records.size(); ++i)
    {
        const auto& record = records.getReference(i);
        const bool compatible = record.status == "Compatible" && !record.scannerQuarantined && !record.runtimeQuarantined;
        const auto label = (record.name.isNotEmpty() ? record.name : juce::File(record.canonicalPath).getFileNameWithoutExtension())
            + " | " + (record.manufacturer.isNotEmpty() ? record.manufacturer : "Unknown company")
            + " | " + (record.category.isNotEmpty() ? record.category : "Unclassified")
            + " | " + bridge::architectureName(record.architecture);
        menu.addItem(1000 + i, label, compatible);
    }
    auto safe = juce::Component::SafePointer<HostContentComponent>(this);
    const auto mouse = juce::Desktop::getInstance().getMainMouseSource().getScreenPosition().roundToInt();
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea({ mouse.x, mouse.y, 1, 1 }), [safe, insertAt](int result)
    {
        if (safe == nullptr || result == 0) return;
        if (result == 1)
        {
            safe->fileChooser = std::make_unique<juce::FileChooser>("Add VST3 plug-in", juce::File(), "*.vst3");
            auto nestedSafe = juce::Component::SafePointer<HostContentComponent>(safe.getComponent());
            safe->fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [nestedSafe, insertAt](const juce::FileChooser& chooser)
                {
                    if (nestedSafe == nullptr || !chooser.getResult().exists()) return;
                    nestedSafe->pendingInsertAt = insertAt;
                    nestedSafe->pendingRackSize = VST3HostEngine::getInstance().getRackSize();
                    VST3HostEngine::getInstance().scanFileAndLoad(chooser.getResult());
                });
        }
        else safe->addPluginRecord(result - 1000, insertAt);
    });
}

void HostContentComponent::showReplaceMenu(int index)
{
    auto& engine = VST3HostEngine::getInstance();
    if (!juce::isPositiveAndBelow(index, engine.getRackSize())) return;
    records = engine.getPluginRecords();
    juce::PopupMenu menu;
    menu.addItem(1, "Add VST...");
    if (!records.isEmpty()) menu.addSeparator();
    for (int i = 0; i < records.size(); ++i)
    {
        const auto& record = records.getReference(i);
        const bool compatible = record.status == "Compatible" && !record.scannerQuarantined
                             && !record.runtimeQuarantined && record.architecture == bridge::currentArchitecture();
        const auto label = (record.name.isNotEmpty() ? record.name : juce::File(record.canonicalPath).getFileNameWithoutExtension())
            + " | " + (record.manufacturer.isNotEmpty() ? record.manufacturer : "Unknown company")
            + " | " + (record.category.isNotEmpty() ? record.category : "Unclassified")
            + " | " + bridge::architectureName(record.architecture);
        menu.addItem(1000 + i, label, compatible);
    }
    auto safe = juce::Component::SafePointer<HostContentComponent>(this);
    const auto mouse = juce::Desktop::getInstance().getMainMouseSource().getScreenPosition().roundToInt();
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea({ mouse.x, mouse.y, 1, 1 }), [safe, index](int result)
    {
        if (safe == nullptr || result == 0) return;
        if (result == 1)
        {
            safe->fileChooser = std::make_unique<juce::FileChooser>("Replace with VST3 plug-in", juce::File(), "*.vst3");
            auto nestedSafe = juce::Component::SafePointer<HostContentComponent>(safe.getComponent());
            safe->fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [nestedSafe, index](const juce::FileChooser& chooser)
                {
                    if (nestedSafe == nullptr || !chooser.getResult().exists()) return;
                    auto& nestedEngine = VST3HostEngine::getInstance();
                    if (nestedEngine.isScanning())
                    { nestedSafe->statusLabel.setText("Wait for the current VST3 scan to finish.", false); return; }
                    if (!nestedSafe->beginReplacement(index)) return;
                    nestedSafe->pendingRackSize = nestedEngine.getRackSize();
                    nestedSafe->pendingReplaceScan = true;
                    nestedSafe->pendingReplaceIdleTicks = 0;
                    nestedEngine.scanFileAndLoad(chooser.getResult());
                });
        }
        else safe->replacePluginRecord(result - 1000, index);
    });
}

bool HostContentComponent::beginReplacement(int index)
{
    auto& engine = VST3HostEngine::getInstance();
    if (!juce::isPositiveAndBelow(index, engine.getRackSize()) || pendingReplaceAt >= 0) return false;
    auto* plugin = engine.getRackPluginInstance(index);
    const auto paths = engine.getRackPluginPaths();
    if (plugin == nullptr || !juce::isPositiveAndBelow(index, paths.size())) return false;
    replacedPluginPath = paths[index];
    replacedPluginState.reset();
    {
        const juce::ScopedLock callbackLock(plugin->getCallbackLock());
        plugin->getStateInformation(replacedPluginState);
    }
    replacedPluginMuted = engine.isRackSlotMuted(index);
    replacedPluginSolo = engine.isRackSlotSolo(index);
    replacedPluginEditorHeight = engine.getRackSlotEditorHeight(index);
    pendingReplaceAt = index;
    detachedWindow.reset();
    cards.clear();
    rackContent.removeAllChildren();
    engine.removeRackSlot(index);
    return true;
}

void HostContentComponent::finishReplacement()
{
    auto& engine = VST3HostEngine::getInstance();
    if (pendingReplaceAt < 0 || engine.getRackSize() == 0) return;
    const auto last = engine.getRackSize() - 1;
    const auto target = juce::jlimit(0, last, pendingReplaceAt);
    if (last != target) engine.moveRackSlot(last, target);
    engine.setRackSlotMuted(target, replacedPluginMuted);
    engine.setRackSlotSolo(target, replacedPluginSolo);
    engine.setRackSlotEditorHeight(target, replacedPluginEditorHeight);
    keyboardSlot = target;
    expandedSlots.addIfNotAlreadyThere(target);
    pendingReplaceAt = pendingRackSize = -1;
    pendingReplaceScan = false;
    pendingReplaceIdleTicks = 0;
    replacedPluginPath.clear();
    replacedPluginState.reset();
    engine.saveState();
    refreshCards();
}

void HostContentComponent::rollbackReplacement()
{
    auto& engine = VST3HostEngine::getInstance();
    const auto target = pendingReplaceAt;
    const auto oldPath = replacedPluginPath;
    const auto oldState = replacedPluginState;
    const auto oldMuted = replacedPluginMuted;
    const auto oldSolo = replacedPluginSolo;
    const auto oldHeight = replacedPluginEditorHeight;
    pendingReplaceAt = pendingRackSize = -1;
    pendingReplaceScan = false;
    pendingReplaceIdleTicks = 0;
    if (target < 0 || oldPath.isEmpty() || !engine.loadPlugin(oldPath, false))
    {
        statusLabel.setText("Replacement failed and the previous VST3 could not be restored. See the rack log.", false);
        refreshCards();
        return;
    }
    const auto last = engine.getRackSize() - 1;
    if (auto* plugin = engine.getRackPluginInstance(last))
        plugin->setStateInformation(oldState.getData(), static_cast<int>(oldState.getSize()));
    const auto restored = juce::jlimit(0, last, target);
    if (last != restored) engine.moveRackSlot(last, restored);
    engine.setRackSlotMuted(restored, oldMuted);
    engine.setRackSlotSolo(restored, oldSolo);
    engine.setRackSlotEditorHeight(restored, oldHeight);
    replacedPluginPath.clear();
    replacedPluginState.reset();
    statusLabel.setText("Could not load the replacement; the previous VST3 was restored.", false);
    engine.saveState();
    refreshCards();
}

void HostContentComponent::replacePluginRecord(int recordIndex, int index)
{
    if (!juce::isPositiveAndBelow(recordIndex, records.size())) return;
    auto& engine = VST3HostEngine::getInstance();
    const auto& record = records.getReference(recordIndex);
    if (record.architecture != bridge::currentArchitecture())
    { statusLabel.setText("Replacement must use the rack helper architecture.", false); return; }
    const auto path = record.location.resolve(engine.getRuntimePaths()).getFullPathName();
    if (!beginReplacement(index)) return;
    if (!engine.loadPlugin(path, false)) { rollbackReplacement(); return; }
    finishReplacement();
}

void HostContentComponent::addPluginRecord(int recordIndex, int insertAt)
{
    if (!juce::isPositiveAndBelow(recordIndex, records.size())) return;
    auto& engine = VST3HostEngine::getInstance();
    const auto& record = records.getReference(recordIndex);
    if ((record.architecture == bridge::Architecture::x86 || record.architecture == bridge::Architecture::x64)
        && record.architecture != bridge::currentArchitecture())
    {
        if (engine.getRackSize() > 0)
        { statusLabel.setText("A rack cannot mix x86 and x64 VST3 helpers.", false); return; }
        engine.requestHostArchitecture(record); return;
    }
    if (!engine.loadPlugin(record.location.resolve(engine.getRuntimePaths()).getFullPathName()))
    { statusLabel.setText("Could not load " + record.name + ". See the rack log for details.", false); return; }
    const auto last = engine.getRackSize() - 1;
    const auto target = juce::jlimit(0, last, insertAt);
    if (last != target) engine.moveRackSlot(last, target);
    for (int i = 0; i < expandedSlots.size(); ++i)
        if (expandedSlots[i] >= target) expandedSlots.set(i, expandedSlots[i] + 1);
    expandedSlots.addIfNotAlreadyThere(target);
    keyboardSlot = target;
    engine.saveState(); refreshCards();
}

void HostContentComponent::buttonClicked(juce::Button* button)
{
    if (button == &addButton) showAddMenu(VST3HostEngine::getInstance().getRackSize());
    else if (button == &cardStatesButton) showCardStatesMenu();
    else if (button == &foldersButton) openFolderManager();
    else if (button == &settingsButton) openSettings();
}

void HostContentComponent::timerCallback()
{
    auto& engine = VST3HostEngine::getInstance();
    if (detachedWindow != nullptr && !detachedWindow->isVisible()) detachedWindow.reset();
    engine.flushPendingStateSave();
    if (pendingReplaceScan)
    {
        if (engine.getRackSize() > pendingRackSize)
            finishReplacement();
        else if (engine.isScanning())
            pendingReplaceIdleTicks = 0;
        else if (++pendingReplaceIdleTicks >= 3)
            rollbackReplacement();
    }
    if (pendingRackSize >= 0 && engine.getRackSize() > pendingRackSize)
    {
        const auto last = engine.getRackSize() - 1;
        const auto target = juce::jlimit(0, last, pendingInsertAt);
        if (last != target) engine.moveRackSlot(last, target);
        for (int i = 0; i < expandedSlots.size(); ++i)
            if (expandedSlots[i] >= target) expandedSlots.set(i, expandedSlots[i] + 1);
        expandedSlots.addIfNotAlreadyThere(target);
        keyboardSlot = target;
        pendingRackSize = pendingInsertAt = -1;
    }
    if (engine.getRackPluginPaths() != lastRackPaths) refreshCards();
    const auto progress = engine.getScanProgress();
    if (progress.isNotEmpty()) statusLabel.setText(progress, false);
}

bool HostContentComponent::isInterestedInDragSource(const SourceDetails& details) { return details.description.isInt(); }

void HostContentComponent::itemDragEnter(const SourceDetails& details) { updateDropIndicator(details); }
void HostContentComponent::itemDragMove(const SourceDetails& details) { updateDropIndicator(details); }

void HostContentComponent::itemDragExit(const SourceDetails& details)
{
    if (details.sourceComponent != nullptr) details.sourceComponent->setAlpha(1.0f);
    clearDropIndicator();
}

void HostContentComponent::itemDropped(const SourceDetails& details)
{
    const auto from = static_cast<int>(details.description);
    if (details.sourceComponent != nullptr) details.sourceComponent->setAlpha(1.0f);
    const auto insertion = dropInsertion;
    clearDropIndicator();
    if (insertion < 0 || cards.isEmpty()) return;
    const auto target = juce::jlimit(0, cards.size() - 1, insertion > from ? insertion - 1 : insertion);
    moveSlot(from, target);
}

void HostContentComponent::updateDropIndicator(const SourceDetails& details)
{
    if (details.sourceComponent != nullptr) details.sourceComponent->setAlpha(0.55f);
    const auto rackY = details.localPosition.y - viewport.getY() + viewport.getViewPositionY();
    int insertion = cards.size();
    for (int i = 0; i < cards.size(); ++i)
        if (rackY < cards[i]->getBounds().getCentreY()) { insertion = i; break; }
    for (auto* card : cards) card->setDropIndicator(0);
    if (!cards.isEmpty())
    {
        if (insertion == cards.size()) cards.getLast()->setDropIndicator(1);
        else cards[insertion]->setDropIndicator(-1);
    }
    dropInsertion = insertion;
}

void HostContentComponent::clearDropIndicator()
{
    for (auto* card : cards) card->setDropIndicator(0);
    dropInsertion = -1;
}

void HostContentComponent::showCardStatesMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "Expand all cards");
    menu.addItem(2, "Collapse all cards");
    menu.addSeparator();
    menu.addItem(3, "Remove all cards", !cards.isEmpty());
    auto safe = juce::Component::SafePointer<HostContentComponent>(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&cardStatesButton), [safe](int result)
    {
        if (safe == nullptr || result == 0) return;
        if (result == 1)
        {
            safe->expandedSlots.clear();
            for (int i = 0; i < VST3HostEngine::getInstance().getRackSize(); ++i) safe->expandedSlots.add(i);
            safe->refreshCards();
        }
        else if (result == 2)
        {
            safe->expandedSlots.clear();
            safe->refreshCards();
        }
        else if (result == 3)
        {
            safe->cards.clear();
            safe->expandedSlots.clear();
            safe->keyboardSlot = -1;
            VST3HostEngine::getInstance().clearRack();
            safe->refreshCards();
        }
    });
}

void HostContentComponent::resetSlotParameters(int index)
{
    auto& engine = VST3HostEngine::getInstance();
    if (!juce::isPositiveAndBelow(index, engine.getRackSize())) return;
    const auto wasMuted = engine.isRackSlotMuted(index);
    const auto wasSolo = engine.isRackSlotSolo(index);
    const auto editorHeight = engine.getRackSlotEditorHeight(index);
    cards.clear();
    engine.selectRackSlot(index);
    engine.resetActivePluginParameters();
    if (juce::isPositiveAndBelow(index, engine.getRackSize()))
    {
        engine.setRackSlotMuted(index, wasMuted);
        engine.setRackSlotSolo(index, wasSolo);
        if (editorHeight > 0) engine.setRackSlotEditorHeight(index, editorHeight);
        expandedSlots.addIfNotAlreadyThere(index);
        keyboardSlot = index;
    }
    refreshCards();
}

void HostContentComponent::openSlotLocation(int index)
{
    const auto paths = VST3HostEngine::getInstance().getRackPluginPaths();
    if (!juce::isPositiveAndBelow(index, paths.size())) return;
    bridge::vst3BundleRoot(juce::File(paths[index])).revealToUser();
}

void HostContentComponent::openDetachedSlot(int index, bool fullScreen)
{
    auto& engine = VST3HostEngine::getInstance();
    if (!juce::isPositiveAndBelow(index, engine.getRackSize())) return;
    detachedWindow.reset();
    expandedSlots.removeFirstMatchingValue(index);
    keyboardSlot = index;
    refreshCards();
    const auto sourceBounds = getTopLevelComponent() != nullptr ? getTopLevelComponent()->getBounds() : getScreenBounds();
    detachedWindow = std::make_unique<DetachedEditorWindow>(index, fullScreen, sourceBounds);
}

void HostContentComponent::forgetAllPlugins()
{
    detachedWindow.reset();
    cards.clear();
    expandedSlots.clear();
    keyboardSlot = -1;
    records.clear();
    VST3HostEngine::getInstance().forgetAllPlugins();
    refreshCards();
    statusLabel.setText("Rack cleared and all scanned VST3 entries forgotten.", false);
}

void HostContentComponent::forgetPlugin(int index)
{
    const auto paths = VST3HostEngine::getInstance().getRackPluginPaths();
    if (!juce::isPositiveAndBelow(index, paths.size())) return;
    const auto name = juce::File(paths[index]).getFileNameWithoutExtension();
    detachedWindow.reset();
    cards.clear();
    expandedSlots.clear();
    keyboardSlot = -1;
    VST3HostEngine::getInstance().removePluginFromList(paths[index]);
    refreshCards();
    statusLabel.setText(name + " removed from the rack and scanned VST3 list.", false);
}

void HostContentComponent::openFolderManager()
{
    if (foldersWindow != nullptr && foldersWindow->getPeer() == nullptr) foldersWindow.reset();
    if (foldersWindow == nullptr)
    {
        juce::DialogWindow::LaunchOptions options; options.dialogTitle = "VST3 Bridge Rack For AIMP - Scan Folders";
        options.content.setOwned(new FolderManagerComponent()); options.useNativeTitleBar = true; options.resizable = true;
        foldersWindow.reset(options.create());
    }
    foldersWindow->setVisible(true); foldersWindow->toFront(true);
}

void HostContentComponent::openSettings()
{
    if (settingsWindow != nullptr && settingsWindow->getPeer() == nullptr) settingsWindow.reset();
    if (settingsWindow == nullptr)
    {
        juce::DialogWindow::LaunchOptions options; options.dialogTitle = "VST3 Bridge Rack For AIMP - Settings";
        auto safe = juce::Component::SafePointer<HostContentComponent>(this);
        options.content.setOwned(new SettingsComponent([safe] { if (safe != nullptr) safe->forgetAllPlugins(); }));
        options.useNativeTitleBar = true; options.resizable = false;
        settingsWindow.reset(options.create());
    }
    settingsWindow->setVisible(true); settingsWindow->toFront(true);
}

void HostContentComponent::ensureEditorInitialised()
{
    if (cards.isEmpty() && VST3HostEngine::getInstance().getRackSize() > 0) refreshCards();
}

HostWindow::HostWindow(bool preloadHidden)
    : DocumentWindow("VST3 Bridge Rack For AIMP", juce::Colour(background), DocumentWindow::allButtons)
{
    rackLookAndFeel.setColourScheme(juce::LookAndFeel_V4::getDarkColourScheme());
    setLookAndFeel(&rackLookAndFeel); setUsingNativeTitleBar(true);
    setContentOwned(new HostContentComponent(), true);
    setResizable(true, true); setResizeLimits(1100, 620, 3840, 2400);
    centreWithSize(1600, 900); setWantsKeyboardFocus(true);
    if (preloadHidden) setAlpha(0.0f);
    setVisible(true);
}

HostWindow::~HostWindow() { clearContentComponent(); setLookAndFeel(nullptr); }
void HostWindow::closeButtonPressed() { VST3HostEngine::getInstance().saveState(); setVisible(false); }

bool HostWindow::keyPressed(const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::F11Key)
    {
        if (auto* content = dynamic_cast<HostContentComponent*>(getContentComponent()))
            content->openDetachedSlot(VST3HostEngine::getInstance().getSelectedRackSlot(), true);
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::leftKey || key.getKeyCode() == juce::KeyPress::upKey
        || key.getKeyCode() == juce::KeyPress::rightKey || key.getKeyCode() == juce::KeyPress::downKey)
    {
        if (auto* content = dynamic_cast<HostContentComponent*>(getContentComponent()))
            content->selectAdjacentGui(key.getKeyCode() == juce::KeyPress::leftKey || key.getKeyCode() == juce::KeyPress::upKey ? -1 : 1);
        return true;
    }
    return false;
}

bool HostWindow::isEditorInitialised() const
{
    const auto* content = dynamic_cast<const HostContentComponent*>(getContentComponent());
    return content != nullptr && content->isEditorInitialised();
}

void HostWindow::finishPreload(bool show)
{
    setVisible(false); setAlpha(1.0f); if (show) setVisible(true);
}

void HostWindow::bringToFrontOrFlash()
{
    if (auto* content = dynamic_cast<HostContentComponent*>(getContentComponent())) content->ensureEditorInitialised();
    setMinimised(false); setVisible(true); toFront(true);
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

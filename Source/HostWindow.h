#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "BridgeRuntime.h"

class RackCardComponent;
class DetachedEditorWindow;
class RackViewport : public juce::Viewport, public juce::SettableTooltipClient { public: void paint(juce::Graphics&) override; };
class RackCanvas : public juce::Component { public: void paint(juce::Graphics&) override; };

class HostContentComponent : public juce::Component,
                             public juce::DragAndDropContainer,
                             public juce::DragAndDropTarget,
                             private juce::Button::Listener,
                             private juce::Timer
{
public:
    HostContentComponent();
    ~HostContentComponent() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool isEditorInitialised() const { return editorInitialised; }
    void ensureEditorInitialised();
    void selectAdjacentGui(int delta);

    void toggleSlotEditor(int index);
    void activateSlot(int index);
    void moveSlot(int from, int to);
    void removeSlot(int index);
    void cloneSlot(int index, bool above);
    void reloadSlot(int index);
    void showAddMenu(int insertAt);
    void showReplaceMenu(int index);
    void setSlotMuted(int index, bool muted);
    void setSlotSolo(int index, bool solo);
    void resetSlotParameters(int index);
    void openSlotLocation(int index);
    void openDetachedSlot(int index, bool fullScreen);
    void forgetAllPlugins();
    void forgetPlugin(int index);

private:
    void buttonClicked(juce::Button*) override;
    void timerCallback() override;
    bool isInterestedInDragSource(const SourceDetails&) override;
    void itemDragEnter(const SourceDetails&) override;
    void itemDragMove(const SourceDetails&) override;
    void itemDragExit(const SourceDetails&) override;
    void itemDropped(const SourceDetails&) override;
    void refreshCards();
    void layoutCards();
    void addPluginRecord(int recordIndex, int insertAt);
    void replacePluginRecord(int recordIndex, int index);
    bool beginReplacement(int index);
    void finishReplacement();
    void rollbackReplacement();
    void openFolderManager();
    void openSettings();
    void showCardStatesMenu();
    void updateDropIndicator(const SourceDetails&);
    void clearDropIndicator();

    juce::Label title;
    juce::TextButton addButton { "+  Add VST" };
    juce::TextButton foldersButton { "Scan Folders" };
    juce::TextButton settingsButton { "Settings" };
    juce::TextButton cardStatesButton { "Card States..." };
    juce::TextEditor statusLabel;
    RackViewport viewport;
    RackCanvas rackContent;
    juce::OwnedArray<RackCardComponent> cards;
    juce::Array<bridge::PluginRecord> records;
    juce::StringArray lastRackPaths;
    std::unique_ptr<juce::FileChooser> fileChooser;
    std::unique_ptr<juce::DialogWindow> settingsWindow;
    std::unique_ptr<juce::DialogWindow> foldersWindow;
    std::unique_ptr<DetachedEditorWindow> detachedWindow;
    juce::Array<int> expandedSlots;
    int keyboardSlot = -1;
    int dropInsertion = -1;
    int pendingInsertAt = -1;
    int pendingRackSize = -1;
    int pendingReplaceAt = -1;
    juce::String replacedPluginPath;
    juce::MemoryBlock replacedPluginState;
    bool replacedPluginMuted = false;
    bool replacedPluginSolo = false;
    int replacedPluginEditorHeight = 0;
    bool pendingReplaceScan = false;
    int pendingReplaceIdleTicks = 0;
    bool editorInitialised = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HostContentComponent)
};

class HostWindow : public juce::DocumentWindow
{
public:
    explicit HostWindow(bool preloadHidden = false);
    ~HostWindow() override;
    void closeButtonPressed() override;
    bool keyPressed(const juce::KeyPress&) override;
    void bringToFrontOrFlash();
    bool isEditorInitialised() const;
    void finishPreload(bool show);

private:
    juce::LookAndFeel_V4 rackLookAndFeel;
    juce::TooltipWindow tooltipWindow { nullptr, 450 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HostWindow)
};

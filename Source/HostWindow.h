#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "BridgeRuntime.h"

class HostContentComponent : public juce::Component,
                             private juce::ComboBox::Listener,
                             private juce::Button::Listener,
                             private juce::KeyListener,
                             private juce::Timer
{
public:
    HostContentComponent();
    ~HostContentComponent() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void setVisualizerMode(bool enabled);
    void updateWindowModeButtons();
    bool isVisualizerMode() const { return visualizerMode; }
    bool isEditorInitialised() const { return editorInitialised; }
    void ensureEditorInitialised();
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    void comboBoxChanged(juce::ComboBox*) override;
    void buttonClicked(juce::Button*) override;
    bool keyPressed(const juce::KeyPress&, juce::Component*) override;
    void timerCallback() override;
    void updatePluginList();
    void clearEditorComponent();
    void refreshEditorComponent();
    void resetPluginParameters();
    void resetEditorSize();
    void openFolderManager();

    juce::Label pluginLabel;
    juce::ComboBox pluginComboBox;
    juce::TextButton loadButton { "Load VST..." };
    juce::TextButton foldersButton { "Scan Folders..." };
    juce::TextButton removeButton { "Plugin Actions..." };
    juce::TextButton settingsButton { "Settings..." };
    juce::TextButton alwaysOnTopButton { "Always on Top" };
    juce::TextButton visualizerButton { "Visualizer Mode" };
    juce::TextButton fullscreenButton { "Fullscreen Mode" };
    juce::TextEditor statusLabel;
    juce::TextEditor detailLabel;
    juce::Label noEditorLabel;
    std::unique_ptr<juce::AudioProcessorEditor> activeEditor;
    juce::Array<bridge::PluginRecord> records;
    std::unique_ptr<juce::FileChooser> fileChooser;
    std::unique_ptr<juce::DialogWindow> settingsWindow;
    std::unique_ptr<juce::DialogWindow> foldersWindow;
    bool changingPlugin = false;
    bool visualizerMode = false;
    bool editorInitialised = false;
    int editorLayoutPassesRemaining = 0;
    juce::Point<int> initialEditorSize;
    juce::Point<int> preferredEditorSize;
    juce::uint32 lastEditorStateRevision = 0;
    juce::String lastActivePath;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HostContentComponent)
};

class HostWindow : public juce::DocumentWindow, private juce::Timer
{
public:
    explicit HostWindow(bool preloadHidden = false);
    ~HostWindow() override;
    void closeButtonPressed() override;
    void minimiseButtonPressed() override;
    void maximiseButtonPressed() override;
    bool keyPressed(const juce::KeyPress&) override;
    void moved() override;
    void resized() override;
    void applyWindowSettings();
    void toggleFullscreen();
    void toggleMaximized();
    void exitPresentationModes();
    void bringToFrontOrFlash();
    bool isEditorInitialised() const;
    void finishPreload(bool show);
    void resetToComfortableSize();

private:
    void timerCallback() override;
    void applyDecorations(bool hidden, bool minimal = false);
    void schedulePersistence();
    void persistWindowState();
    bool applyingState = false;
    bool restoredWindowState = false;
    bool maximized = false;
    juce::Rectangle<int> windowedBounds;
    juce::LookAndFeel_V4 bridgeLookAndFeel;
    juce::TooltipWindow tooltipWindow { nullptr, 500 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HostWindow)
};

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

class HostContentComponent : public juce::Component,
                             public juce::ComboBox::Listener,
                             public juce::Button::Listener
{
public:
    HostContentComponent();
    ~HostContentComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void comboBoxChanged(juce::ComboBox* comboBoxThatHasChanged) override;
    void buttonClicked(juce::Button* button) override;

    void updatePluginList();
    void loadSelectedPlugin();
    void resizeToFitEditor();

private:
    void clearEditorComponent();
    void refreshEditorComponent();

    juce::ComboBox pluginComboBox;
    juce::TextButton loadButton;
    juce::TextButton removeButton;
    juce::Label pluginLabel;
    
    std::unique_ptr<juce::AudioProcessorEditor> activeEditor;
    juce::Label noEditorLabel;

    juce::Array<juce::PluginDescription> availablePlugins;
    std::unique_ptr<juce::FileChooser> fileChooser;
    bool isChangingPlugin = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HostContentComponent)
};

class HostWindow : public juce::DocumentWindow
{
public:
    HostWindow();
    ~HostWindow() override;

    void closeButtonPressed() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HostWindow)
};

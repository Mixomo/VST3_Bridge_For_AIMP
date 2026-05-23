#include "HostWindow.h"
#include "VST3HostEngine.h"

HostContentComponent::HostContentComponent()
{
    // Configure Label
    pluginLabel.setText("Active VST3 Plugin:", juce::dontSendNotification);
    pluginLabel.setColour(juce::Label::textColourId, juce::Colours::whitesmoke);
    addAndMakeVisible(pluginLabel);

    // Configure ComboBox
    pluginComboBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff2d2d2d));
    pluginComboBox.setColour(juce::ComboBox::textColourId, juce::Colours::whitesmoke);
    pluginComboBox.setColour(juce::ComboBox::arrowColourId, juce::Colours::lightgrey);
    addAndMakeVisible(pluginComboBox);
    pluginComboBox.addListener(this);

    loadButton.setButtonText("Load VST3...");
    loadButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3c3c3c));
    loadButton.setColour(juce::TextButton::textColourOffId, juce::Colours::whitesmoke);
    addAndMakeVisible(loadButton);
    loadButton.addListener(this);

    removeButton.setButtonText("Remove");
    removeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3c3c3c));
    removeButton.setColour(juce::TextButton::textColourOffId, juce::Colours::whitesmoke);
    addAndMakeVisible(removeButton);
    removeButton.addListener(this);

    // Configure fallback Label
    noEditorLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    noEditorLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(noEditorLabel);

    // Update the list of VST3 plugins
    updatePluginList();
    
    // Refresh GUI to show the editor of the active VST3 if one is loaded
    refreshEditorComponent();
}

HostContentComponent::~HostContentComponent()
{
    clearEditorComponent();
}

void HostContentComponent::paint(juce::Graphics& g)
{
    // Draw modern dark gradient background
    g.fillAll(juce::Colour(0xff1e1e1e));
    
    // Draw divider line between control panel and editor
    g.setColour(juce::Colour(0xff323232));
    g.drawHorizontalLine(45, 0.0f, (float)getWidth());
}

void HostContentComponent::resized()
{
    auto bounds = getLocalBounds();
    auto topRow = bounds.removeFromTop(45).reduced(10, 8);

    pluginLabel.setBounds(topRow.removeFromLeft(120));
    loadButton.setBounds(topRow.removeFromRight(115));
    topRow.removeFromRight(5);
    removeButton.setBounds(topRow.removeFromRight(85));
    topRow.removeFromLeft(5); // gap
    pluginComboBox.setBounds(topRow);

    if (activeEditor != nullptr)
    {
        activeEditor->setBounds(bounds);
    }
    else
    {
        noEditorLabel.setBounds(bounds);
    }
}

void HostContentComponent::comboBoxChanged(juce::ComboBox* comboBoxThatHasChanged)
{
    if (comboBoxThatHasChanged == &pluginComboBox && !isChangingPlugin)
    {
        const juce::ScopedValueSetter<bool> changeGuard(isChangingPlugin, true);
        int selectedId = pluginComboBox.getSelectedId();
        clearEditorComponent();
        loadButton.setEnabled(false);
        removeButton.setEnabled(false);
        pluginComboBox.setEnabled(false);
        
        if (selectedId == 1) // "None"
        {
            VST3HostEngine::getInstance().unloadPlugin();
            VST3HostEngine::getInstance().saveState();
            refreshEditorComponent();
        }
        else if (selectedId > 1)
        {
            int index = selectedId - 2;
            if (index >= 0 && index < availablePlugins.size())
            {
                juce::String path = availablePlugins[index].fileOrIdentifier;

                if (!VST3HostEngine::getInstance().loadPlugin(path))
                {
                    VST3HostEngine::getInstance().saveState();
                    updatePluginList();
                    pluginComboBox.setSelectedId(1, juce::dontSendNotification);
                    if (auto* dw = findParentComponentOfClass<juce::DocumentWindow>())
                        dw->setVisible(false);
                }
                else
                {
                    VST3HostEngine::getInstance().saveState();
                    refreshEditorComponent();
                }
            }
        }

        pluginComboBox.setEnabled(true);
        loadButton.setEnabled(true);
        removeButton.setEnabled(true);
    }
}

void HostContentComponent::buttonClicked(juce::Button* button)
{
    if (button == &loadButton && !isChangingPlugin)
    {
        loadButton.setEnabled(false);
        auto safeThis = juce::Component::SafePointer<HostContentComponent>(this);
        fileChooser = std::make_unique<juce::FileChooser>(
            "Select VST3 plug-in",
            juce::File("C:\\Program Files\\Common Files\\VST3"),
            "*.vst3");

        constexpr int chooserFlags = juce::FileBrowserComponent::openMode
                                   | juce::FileBrowserComponent::canSelectFiles;

        fileChooser->launchAsync(chooserFlags, [safeThis](const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr)
                return;

            const juce::ScopedValueSetter<bool> changeGuard(safeThis->isChangingPlugin, true);
            auto pluginFile = chooser.getResult();
            if (pluginFile == juce::File{})
            {
                safeThis->fileChooser.reset();
                safeThis->loadButton.setEnabled(true);
                safeThis->removeButton.setEnabled(true);
                return;
            }

            safeThis->clearEditorComponent();
            safeThis->pluginComboBox.setEnabled(false);
            safeThis->removeButton.setEnabled(false);

            if (!VST3HostEngine::getInstance().loadPluginFromFile(pluginFile))
            {
                VST3HostEngine::getInstance().saveState();
                safeThis->updatePluginList();
                safeThis->pluginComboBox.setSelectedId(1, juce::dontSendNotification);
                if (auto* dw = safeThis->findParentComponentOfClass<juce::DocumentWindow>())
                    dw->setVisible(false);
            }
            else
            {
                VST3HostEngine::getInstance().saveState();
                safeThis->updatePluginList();
                safeThis->refreshEditorComponent();
            }

            safeThis->fileChooser.reset();
            safeThis->pluginComboBox.setEnabled(true);
            safeThis->loadButton.setEnabled(true);
            safeThis->removeButton.setEnabled(true);
        });
    }
    else if (button == &removeButton && !isChangingPlugin)
    {
        const int selectedId = pluginComboBox.getSelectedId();
        const int index = selectedId - 2;
        if (index >= 0 && index < availablePlugins.size())
        {
            const auto path = availablePlugins[index].fileOrIdentifier;
            clearEditorComponent();
            VST3HostEngine::getInstance().removePluginFromList(path);
            updatePluginList();
            refreshEditorComponent();
        }
    }
}

void HostContentComponent::updatePluginList()
{
    pluginComboBox.clear(juce::dontSendNotification);
    pluginComboBox.addItem("[ None - Bypass ]", 1);
    
    availablePlugins = VST3HostEngine::getInstance().getAvailablePlugins();
    
    int activeId = 1;
    juce::String activePath = VST3HostEngine::getInstance().getActivePluginPath();
    
    for (int i = 0; i < availablePlugins.size(); ++i)
    {
        const auto& desc = availablePlugins[i];
        juce::String displayName = desc.name;
        if (desc.manufacturerName.isNotEmpty())
        {
            displayName += " (" + desc.manufacturerName + ")";
        }

        int itemID = i + 2;
        pluginComboBox.addItem(displayName, itemID);
        
        if (desc.fileOrIdentifier == activePath)
        {
            activeId = itemID;
        }
    }
    
    pluginComboBox.setSelectedId(activeId, juce::dontSendNotification);
}

void HostContentComponent::refreshEditorComponent()
{
    clearEditorComponent();
    
    auto* instance = VST3HostEngine::getInstance().getPluginInstance();

    if (instance != nullptr)
    {
        if (instance->hasEditor())
        {
            activeEditor.reset(instance->createEditorIfNeeded());
            if (activeEditor != nullptr)
            {
                addAndMakeVisible(activeEditor.get());
                noEditorLabel.setVisible(false);
                resizeToFitEditor();
                return;
            }
        }
        
        noEditorLabel.setText("This plugin has no graphical editor.", juce::dontSendNotification);
    }
    else
    {
        noEditorLabel.setText("Please select a VST3 plugin from the list.", juce::dontSendNotification);
    }
    
    noEditorLabel.setVisible(true);
    setSize(760, 180);
    
    if (auto* dw = findParentComponentOfClass<juce::DocumentWindow>())
    {
        dw->setContentNonOwned(this, true);
    }
}

void HostContentComponent::clearEditorComponent()
{
    if (activeEditor != nullptr)
    {
        removeChildComponent(activeEditor.get());
        activeEditor.reset();
    }
}

void HostContentComponent::resizeToFitEditor()
{
    if (activeEditor != nullptr)
    {
        int editorWidth = juce::jmax(760, activeEditor->getWidth());
        int editorHeight = activeEditor->getHeight();
        
        // Add 45px for top control bar
        setSize(editorWidth, editorHeight + 45);
        
        if (auto* dw = findParentComponentOfClass<juce::DocumentWindow>())
        {
            dw->setContentNonOwned(this, true);
        }
    }
}

// ==============================================================================
// HostWindow Implementation
// ==============================================================================

HostWindow::HostWindow()
    : DocumentWindow("AIMP VST3 Host Bridge",
                     juce::Colour(0xff1e1e1e),
                     DocumentWindow::closeButton)
{
    setUsingNativeTitleBar(true);
    
    auto* content = new HostContentComponent();
    setContentOwned(content, true);
    
    setResizable(true, true);
    setResizeLimits(760, 180, 2400, 1600);
    
    // Center on screen
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
}

HostWindow::~HostWindow()
{
    clearContentComponent();
}

void HostWindow::closeButtonPressed()
{
    VST3HostEngine::getInstance().saveState();
    // Close the window gracefully
    setVisible(false);
}

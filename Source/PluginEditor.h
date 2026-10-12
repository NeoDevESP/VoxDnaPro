#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "UI/Pages.h"

// ============================================================================
//  VOX DNA PRO v3.0 — editor "Urban Vocal Laboratory"
//  Base 1536 x 1024 escalada (50-150 %): cabecera, navegador de presets,
//  5 vistas (CREATE / MIX / ROUTING / SEQUENCE / ANALYZE).
// ============================================================================
namespace pv::ui
{
class PresetBrowser : public juce::Component, private juce::ListBoxModel
{
public:
    explicit PresetBrowser (UiContext& c);
    void paint (juce::Graphics&) override;
    void resized() override;
    void rebuild();
    void selectCurrent();
    std::function<void()> onSaveRequest, onImportExport;
private:
    struct Entry { PresetInfo info; };
    int getNumRows() override { return (int) shown.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    juce::Image avatarFor (const PresetInfo&);

    UiContext& ctx;
    juce::TextEditor search;
    IconButton close { IconButton::close, "Limpiar busqueda y filtros" };
    juce::OwnedArray<juce::TextButton> chips;
    juce::ListBox list;
    juce::TextButton create { "+   Crear preset" }, importExport { "Importar / Exportar" };
    std::vector<Entry> all, shown;
    juce::String genre;
    std::map<juce::String, juce::Image> avatars;
};

class TabBar : public juce::Component
{
public:
    TabBar();
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    int current = 0, hover = -1;
    float beat = 0.0f;
    std::function<void (int)> onChange;
private:
    juce::Rectangle<float> tabBounds (int i) const;
};
} // namespace pv::ui

class VoxDnaEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VoxDnaEditor (VoxDnaProcessor&);
    ~VoxDnaEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    static constexpr int baseW = 1536, baseH = 1024;
    void setView (int v);   // también lo usan las pruebas (capturas)
    void tickForTest() { timerCallback(); }   // las pruebas avanzan la animación frame a frame

private:
    void timerCallback() override;
    void paintHeader (juce::Graphics&);
    void showPresetMenu();
    void showMoreMenu();
    void showSettingsMenu();
    void showImageMenu();
    void savePresetDialog();
    void exportPreset();
    void importPreset();
    void setScale (float s);
    void applyMotionMode();
    juce::Rectangle<int> logoArea() const { return juce::Rectangle<int> (0, 0, 330, 66).transformedBy (juce::AffineTransform::scale ((float) getWidth() / (float) baseW)).expanded (2); }
    void presetChanged();

    VoxDnaProcessor& proc;
    pv::NeonLookAndFeel lnf;
    std::unique_ptr<juce::TooltipWindow> tooltips;
    pv::ui::UiContext ctx;

    juce::Component content;
    pv::ui::PresetBrowser browser;
    pv::ui::TabBar tabs;
    pv::ui::CreatePage createPage;
    pv::ui::MixPage mixPage;
    pv::ui::RoutingPage routingPage;
    pv::ui::SequencePage sequencePage;
    pv::ui::AnalyzePage analyzePage;
    int view = 0;

    pv::ui::IconButton undoButton { pv::ui::IconButton::undo, "Deshacer" }, redoButton { pv::ui::IconButton::redo, "Rehacer" };
    pv::ui::IconButton heartButton { pv::ui::IconButton::heart, "Favorito" }, exportButton { pv::ui::IconButton::download, "Exportar preset (.vdna)" };
    pv::ui::IconButton moreButton { pv::ui::IconButton::more, "Mas opciones" }, gearButton { pv::ui::IconButton::gear, "Ajustes" };
    juce::TextButton titleButton, aButton { "A" }, bButton { "B" };
    juce::ComboBox qualityBox, scaleBox;
    std::unique_ptr<APVTS::ComboBoxAttachment> qualityAttachment;
    std::unique_ptr<juce::FileChooser> chooser;
    pv::ui::PresetInfo info;
    int lastSerial = -1;
    juce::String lastName;
    int repaintCounter = 0;
    juce::uint32 frameCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxDnaEditor)
};

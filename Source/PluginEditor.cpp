#include "PluginEditor.h"

using namespace pv;
using namespace pv::ui;

namespace
{
const char* genres[] { "ALL", "URBAN", "TRAP", "R&B", "POP" };
}

// ============================================================================
//  NAVEGADOR DE PRESETS
// ============================================================================
PresetBrowser::PresetBrowser (UiContext& c) : ctx (c)
{
    search.setTextToShowWhenEmpty ("Buscar presets...", Pal::dim);
    search.setFont (font (16.0f));
    search.setIndents (32, 6);
    search.onTextChange = [this] { rebuild(); };
    addAndMakeVisible (search);
    close.onClick = [this]
    {
        search.clear();
        genre.clear();
        for (int i = 0; i < chips.size(); ++i) chips[i]->setToggleState (i == 0, juce::dontSendNotification);
        rebuild();
    };
    addAndMakeVisible (close);
    for (int i = 0; i < 5; ++i)
    {
        auto* b = chips.add (new juce::TextButton (genres[i]));
        b->setClickingTogglesState (true);
        b->setRadioGroupId (901);
        b->getProperties().set ("fontSize", 14.0f);
        b->onClick = [this, i] { genre = i == 0 ? juce::String() : juce::String (genres[i]); rebuild(); };
        addAndMakeVisible (b);
    }
    chips[0]->setToggleState (true, juce::dontSendNotification);
    list.setModel (this);
    list.setRowHeight (51);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.getViewport()->setScrollBarThickness (8);
    addAndMakeVisible (list);
    create.getProperties().set ("fontSize", 16.0f);
    importExport.getProperties().set ("fontSize", 15.0f);
    create.onClick = [this] { if (onSaveRequest) onSaveRequest(); };
    importExport.onClick = [this] { if (onImportExport) onImportExport(); };
    addAndMakeVisible (create);
    addAndMakeVisible (importExport);
    rebuild();
}

void PresetBrowser::rebuild()
{
    all.clear();
    const auto& fp = getFactoryPresets();
    for (int i = 0; i < (int) fp.size(); ++i) all.push_back ({ UiContext::factoryInfo (i) });
    for (auto& f : ctx.proc.getUserPresets())
    {
        PresetInfo info;
        info.factory = false;
        info.artist = "USER";
        info.title = f.getFileNameWithoutExtension().toUpperCase();
        info.key = "U:" + f.getFileNameWithoutExtension();
        info.genre = "USER";
        info.tags.add ("USER");
        info.userFile = f;
        all.push_back ({ info });
    }
    shown.clear();
    const auto q = search.getText().trim().toLowerCase();
    for (auto& e : all)
    {
        if (genre.isNotEmpty() && e.info.genre != genre) continue;
        const auto hay = (e.info.artist + " " + e.info.title + " " + e.info.tags.joinIntoString (" ") + " " + e.info.genre).toLowerCase();
        if (q.isNotEmpty() && ! hay.contains (q)) continue;
        shown.push_back (e);
    }
    std::stable_sort (shown.begin(), shown.end(), [this] (const Entry& a, const Entry& b)
                      { return ctx.isFavourite (a.info.key) && ! ctx.isFavourite (b.info.key); });
    list.updateContent();
    selectCurrent();
    list.repaint();
}

void PresetBrowser::selectCurrent()
{
    const auto key = ctx.currentPreset().key;
    for (int i = 0; i < (int) shown.size(); ++i)
        if (shown[(size_t) i].info.key == key) { list.selectRow (i, true, true); return; }
    list.deselectAllRows();
}

juce::Image PresetBrowser::avatarFor (const PresetInfo& info)
{
    auto user = ctx.userImage (info.key);
    const juce::String k = info.key + (user.isValid() ? "#u" : "");
    auto it = avatars.find (k);
    if (it != avatars.end()) return it->second;
    juce::Image img (juce::Image::ARGB, 80, 80, true);
    {
        juce::Graphics g (img);
        juce::Path clip;
        clip.addRoundedRectangle (0.0f, 0.0f, 80.0f, 80.0f, 10.0f);
        g.reduceClipRegion (clip);
        if (user.isValid())
            g.drawImageWithin (user, 0, 0, 80, 80, juce::RectanglePlacement::centred | juce::RectanglePlacement::fillDestination);
        else
        {
            art::paint (g, { 0, 0, 80, 80 }, info.key, info.tags, info.genre, true);
            g.setFont (display (30.0f));
            g.setColour (juce::Colours::white.withAlpha (0.92f));
            g.drawText (info.title.substring (0, 1), juce::Rectangle<int> (0, 0, 80, 80), juce::Justification::centred);
        }
    }
    avatars[k] = img;
    return img;
}

void PresetBrowser::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (! juce::isPositiveAndBelow (row, (int) shown.size())) return;
    const auto& info = shown[(size_t) row].info;
    auto r = juce::Rectangle<float> (2.0f, 2.0f, (float) w - 6.0f, (float) h - 4.0f);
    if (selected)
    {
        g.setGradientFill (juce::ColourGradient (Pal::violet.withAlpha (0.35f), r.getX(), 0, Pal::violet.withAlpha (0.08f), r.getRight(), 0, false));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (Pal::violet.withAlpha (0.85f));
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
    }
    g.drawImage (avatarFor (info), juce::Rectangle<float> (r.getX() + 4.0f, r.getY() + 3.0f, 40.0f, 40.0f));
    const float tx = r.getX() + 52.0f;
    g.setFont (font (16.5f, true));
    g.setColour (Pal::text);
    g.drawText (info.artist + " - " + info.title, juce::Rectangle<float> (tx, r.getY() + 2.0f, r.getRight() - tx - 18.0f, 22.0f), juce::Justification::centredLeft, true);
    float px = tx;
    g.setFont (font (12.0f, true));
    for (int i = 0; i < juce::jmin (2, info.tags.size()); ++i)
    {
        const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), info.tags[i]) + 14.0f;
        auto pill = juce::Rectangle<float> (px, r.getY() + 25.0f, tw, 16.0f);
        g.setColour (Pal::panel2.brighter (0.15f));
        g.fillRoundedRectangle (pill, 3.0f);
        g.setColour (Pal::border.brighter (0.3f));
        g.drawRoundedRectangle (pill, 3.0f, 0.8f);
        g.setColour (Pal::text.withAlpha (0.75f));
        g.drawText (info.tags[i], pill, juce::Justification::centred);
        px += tw + 6.0f;
    }
    if (ctx.isFavourite (info.key))
        drawIcon (g, IconButton::heart, juce::Rectangle<float> (r.getRight() - 16.0f, r.getY() + 6.0f, 12.0f, 12.0f), Pal::magenta, true);
}

void PresetBrowser::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (! juce::isPositiveAndBelow (row, (int) shown.size())) return;
    const auto info = shown[(size_t) row].info;
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m;
        m.addItem (1, ctx.isFavourite (info.key) ? "Quitar de favoritos" : juce::String::fromUTF8 ("A\xc3\xb1" "adir a favoritos"));
        juce::Component::SafePointer<PresetBrowser> safe (this);
        m.showMenuAsync ({}, [safe, info] (int r) { if (safe != nullptr && r == 1) { safe->ctx.toggleFavourite (info.key); safe->rebuild(); } });
        return;
    }
    if (info.factory) ctx.proc.loadFactoryPreset (info.factoryIndex);
    else ctx.proc.loadUserPreset (info.userFile);
}

void PresetBrowser::resized()
{
    close.setBounds (getWidth() - 30, 8, 22, 22);
    search.setBounds (8, 38, getWidth() - 16, 30);
    const float cw = (float) (getWidth() - 16 - 4 * 6) / 5.0f;
    for (int i = 0; i < 5; ++i) chips[i]->setBounds (juce::roundToInt (8.0f + (cw + 6.0f) * (float) i), 78, juce::roundToInt (cw), 26);
    create.setBounds (8, getHeight() - 92, getWidth() - 16, 32);
    importExport.setBounds (8, getHeight() - 48, getWidth() - 16, 32);
    list.setBounds (4, 112, getWidth() - 8, create.getY() - 120);
}

void PresetBrowser::paint (juce::Graphics& g)
{
    drawPanel (g, getLocalBounds().toFloat());
    g.setFont (font (19.0f, true));
    g.setColour (Pal::text);
    drawSpaced (g, "PRESETS", { 12.0f, 6.0f, 160.0f, 26.0f }, 1.0f, juce::Justification::centredLeft);
    drawIcon (g, IconButton::search, juce::Rectangle<float> (18.0f, 46.0f, 14.0f, 14.0f), Pal::dim);
}

// ============================================================================
//  PESTAÑAS
// ============================================================================
TabBar::TabBar() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

juce::Rectangle<float> TabBar::tabBounds (int i) const
{
    const float gap = 6.0f, w = ((float) getWidth() - gap * 4.0f) / 5.0f;
    return { (w + gap) * (float) i, 0.0f, w, (float) getHeight() };
}

void TabBar::paint (juce::Graphics& g)
{
    static const char* names[] { "CREATE", "MIX", "ROUTING", "SEQUENCE", "ANALYZE" };
    static const char* subs[] { "R\xc3\x81PIDO \xc2\xb7 PRESETS \xc2\xb7 MACROS", "DETALLE \xc2\xb7 M\xc3\x93" "DULOS \xc2\xb7 CAPAS",
                                "CADENA \xc2\xb7 PARALELOS \xc2\xb7 ENV\xc3\x8D" "OS", "AUTOMATIZACI\xc3\x93N \xc2\xb7 MOVIMIENTO",
                                "AN\xc3\x81LISIS LOCAL \xc2\xb7 REFERENCIA \xc2\xb7 MATCH" };
    for (int i = 0; i < 5; ++i)
    {
        auto r = tabBounds (i);
        const bool on = i == current, hv = i == hover;
        if (on)
        {
            g.setGradientFill (juce::ColourGradient (Pal::violet.withAlpha (0.32f), r.getX(), r.getY(), Pal::magenta.withAlpha (0.12f), r.getRight(), r.getBottom(), false));
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (Pal::violet.brighter (0.4f));
            g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.3f);
            g.setColour (Pal::lilac.withAlpha (0.2f + 0.35f * beat));
            g.drawRoundedRectangle (r.expanded (1.5f), 7.0f, 2.0f + 2.0f * beat);
        }
        else
        {
            g.setColour (Pal::panel.withAlpha (0.95f));
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour ((hv ? Pal::violet : Pal::border).withAlpha (0.9f));
            g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
        }
        g.setFont (font (23.0f, true));
        g.setColour (on ? juce::Colours::white : Pal::text.withAlpha (0.88f));
        drawSpaced (g, names[i], r.withTrimmedBottom (r.getHeight() * 0.42f).withTrimmedTop (6.0f), 1.2f, juce::Justification::centred);
        g.setFont (font (13.0f, true));
        g.setColour (on ? Pal::lilac : Pal::dim);
        drawSpaced (g, juce::String::fromUTF8 (subs[i]), r.withTrimmedTop (r.getHeight() * 0.56f).withTrimmedBottom (6.0f), 0.8f, juce::Justification::centred);
    }
}

void TabBar::mouseMove (const juce::MouseEvent& e)
{
    int h = -1;
    for (int i = 0; i < 5; ++i) if (tabBounds (i).contains (e.position)) h = i;
    if (h != hover) { hover = h; repaint(); }
}

void TabBar::mouseUp (const juce::MouseEvent& e)
{
    for (int i = 0; i < 5; ++i)
        if (tabBounds (i).contains (e.position)) { current = i; repaint(); if (onChange) onChange (i); }
}

// ============================================================================
//  EDITOR
// ============================================================================
VoxDnaEditor::VoxDnaEditor (VoxDnaProcessor& p)
    : AudioProcessorEditor (&p), proc (p), ctx (p), browser (ctx), createPage (ctx), mixPage (ctx), routingPage (ctx),
      sequencePage (ctx), analyzePage (ctx)
{
    setLookAndFeel (&lnf);
    tooltips = std::make_unique<juce::TooltipWindow> (this, 700);
    tooltips->setLookAndFeel (&lnf);
    tooltips->setEnabled (ctx.settings.tooltips);

    addAndMakeVisible (content);
    content.setInterceptsMouseClicks (false, true);
    content.addAndMakeVisible (browser);
    content.addAndMakeVisible (tabs);
    for (juce::Component* pg : std::initializer_list<juce::Component*> { &createPage, &mixPage, &routingPage, &sequencePage, &analyzePage })
        content.addChildComponent (pg);
    tabs.onChange = [this] (int v) { setView (v); };

    browser.onSaveRequest = [this] { savePresetDialog(); };
    browser.onImportExport = [this]
    {
        juce::PopupMenu m;
        m.addItem (1, "Importar preset (.vdna)...");
        m.addItem (2, "Exportar preset actual (.vdna)...");
        m.addItem (3, "Abrir carpeta de presets");
        juce::Component::SafePointer<VoxDnaEditor> safe (this);
        m.showMenuAsync ({}, [safe] (int r)
        {
            if (safe == nullptr) return;
            if (r == 1) safe->importPreset();
            if (r == 2) safe->exportPreset();
            if (r == 3) { VoxDnaProcessor::getUserPresetFolder().createDirectory(); VoxDnaProcessor::getUserPresetFolder().startAsProcess(); }
        });
    };
    createPage.onImageMenu = [this] { showImageMenu(); };
    analyzePage.onReferenceChanged = [this] { createPage.hero.rebuildReferenceList(); };

    // ---- cabecera ----
    for (auto* b : { &undoButton, &redoButton, &heartButton, &exportButton, &moreButton, &gearButton }) content.addAndMakeVisible (b);
    gearButton.frame = true;
    undoButton.onClick = [this] { proc.undoManager.undo(); };
    redoButton.onClick = [this] { proc.undoManager.redo(); };
    heartButton.onClick = [this]
    {
        ctx.toggleFavourite (info.key);
        heartButton.setToggleState (ctx.isFavourite (info.key), juce::dontSendNotification);
        browser.rebuild();
    };
    exportButton.onClick = [this] { exportPreset(); };
    moreButton.onClick = [this] { showMoreMenu(); };
    gearButton.onClick = [this] { showSettingsMenu(); };

    titleButton.getProperties().set ("ghost", true);
    titleButton.getProperties().set ("fontSize", 21.0f);
    titleButton.setTooltip ("Lista completa de presets");
    titleButton.onClick = [this] { showPresetMenu(); };
    content.addAndMakeVisible (titleButton);

    for (auto* b : { &aButton, &bButton })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (202);
        b->getProperties().set ("fontSize", 20.0f);
        b->setTooltip ("Comparador A/B: dos ajustes completos en memoria (copiar en el menu ...)");
        content.addAndMakeVisible (b);
    }
    aButton.setToggleState (proc.getABSlot() == 0, juce::dontSendNotification);
    bButton.setToggleState (proc.getABSlot() == 1, juce::dontSendNotification);
    aButton.onClick = [this] { proc.selectAB (0); presetChanged(); };
    bButton.onClick = [this] { proc.selectAB (1); presetChanged(); };

    qualityBox.addItemList ({ "MONITOR", "MIX", "RENDER HQ" }, 1);
    qualityBox.setTooltip (tip ("quality"));
    qualityBox.getProperties().set ("flat", true);
    qualityAttachment = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "quality", qualityBox);
    content.addAndMakeVisible (qualityBox);
    for (int s : { 50, 65, 80, 90, 100, 115, 125, 150 }) scaleBox.addItem (juce::String (s) + "%", s);
    scaleBox.setTooltip ("Escala de la interfaz");
    scaleBox.getProperties().set ("flat", true);
    scaleBox.onChange = [this] { setScale ((float) scaleBox.getSelectedId() / 100.0f); };
    content.addAndMakeVisible (scaleBox);

    // ---- layout fijo a tamaño base ----
    content.setBounds (0, 0, baseW, baseH);
    browser.setBounds (8, 72, 230, 944);
    tabs.setBounds (246, 70, 1282, 56);
    for (juce::Component* pg : std::initializer_list<juce::Component*> { &createPage, &mixPage, &routingPage, &sequencePage, &analyzePage })
        pg->setBounds (246, 136, 1282, 880);
    undoButton.setBounds (494, 13, 30, 30);
    redoButton.setBounds (530, 13, 30, 30);
    titleButton.setBounds (568, 11, 366, 34);
    heartButton.setBounds (942, 13, 30, 30);
    exportButton.setBounds (978, 13, 30, 30);
    moreButton.setBounds (1018, 13, 30, 30);
    aButton.setBounds (1178, 10, 40, 38);
    bButton.setBounds (1222, 10, 40, 38);
    qualityBox.setBounds (1306, 11, 96, 36);
    scaleBox.setBounds (1402, 11, 66, 36);
    gearButton.setBounds (1478, 10, 42, 38);

    setView (0);
    presetChanged();

    setResizable (true, true);
    setResizeLimits (baseW / 2, baseH / 2, (int) (baseW * 1.5f), (int) (baseH * 1.5f));
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) baseW / (double) baseH);
    scaleBox.setSelectedId (juce::roundToInt (ctx.settings.uiScale * 100.0f), juce::dontSendNotification);
    setSize (juce::roundToInt (baseW * ctx.settings.uiScale), juce::roundToInt (baseH * ctx.settings.uiScale));
    applyMotionMode();
}

// 60 fps con animaciones completas, 30 fps en suaves o desactivadas
void VoxDnaEditor::applyMotionMode()
{
    motionMode() = ctx.settings.motion;
    startTimerHz (motionMode() == 2 ? 60 : 30);
}

VoxDnaEditor::~VoxDnaEditor()
{
    stopTimer();
    tooltips->setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void VoxDnaEditor::setScale (float s)
{
    ctx.settings.uiScale = juce::jlimit (0.5f, 1.5f, s);
    ctx.settings.save();
    setSize (juce::roundToInt (baseW * ctx.settings.uiScale), juce::roundToInt (baseH * ctx.settings.uiScale));
}

void VoxDnaEditor::setView (int v)
{
    const int previous = view;
    view = juce::jlimit (0, 4, v);
    tabs.current = view;
    tabs.repaint();
    juce::Component* pages[] { &createPage, &mixPage, &routingPage, &sequencePage, &analyzePage };
    for (int i = 0; i < 5; ++i) pages[i]->setVisible (i == view);
    // transición: la vista nueva entra con fundido y un pequeño desplazamiento (v3.1)
    auto& animator = juce::Desktop::getInstance().getAnimator();
    if (motionMode() > 0 && previous != view && isShowing())
    {
        auto* pg = pages[view];
        const auto target = juce::Rectangle<int> (246, 136, 1282, 880);
        animator.cancelAnimation (pg, false);
        pg->setBounds (target.translated (previous < view ? 24 : -24, 0));
        pg->setAlpha (0.0f);
        animator.animateComponent (pg, target, 1.0f, motionMode() == 2 ? 220 : 140, false, 0.4, 0.0);
    }
    else
    {
        pages[view]->setAlpha (1.0f);
        pages[view]->setBounds (246, 136, 1282, 880);
    }
}

void VoxDnaEditor::presetChanged()
{
    info = ctx.currentPreset();
    titleButton.setButtonText (info.artist + " - " + info.title);
    heartButton.setToggleState (ctx.isFavourite (info.key), juce::dontSendNotification);
    createPage.presetChanged();
    browser.selectCurrent();
    repaint();
}

void VoxDnaEditor::timerCallback()
{
    const bool fast = motionMode() == 2;   // 60 Hz: datos cada 2 frames, movimiento cada frame
    ctx.motion.update (proc, fast ? 1.0f / 60.0f : 1.0f / 30.0f);
    ++frameCounter;
    if (motionMode() > 0)
    {
        if (view == 0) createPage.animate();
        tabs.beat = ctx.motion.beat;
        repaint (logoArea());
        if (ctx.motion.beat > 0.02f) tabs.repaint();
    }
    if (fast && (frameCounter & 1) == 1) return;

    ctx.live.pull (proc);
    ctx.live.updateBands();
    if (proc.presetSerial.load() != lastSerial || proc.getPresetName() != lastName)
    {
        lastSerial = proc.presetSerial.load();
        lastName = proc.getPresetName();
        presetChanged();
    }
    switch (view)
    {
        case 0: createPage.refresh(); break;
        case 1: mixPage.refresh(); break;
        case 2: routingPage.refresh(); break;
        case 3: sequencePage.refresh(); break;
        default: analyzePage.refresh(); break;
    }
    if (! juce::Desktop::getInstance().getMainMouseSource().isDragging())
        proc.undoManager.beginNewTransaction();
    undoButton.setEnabled (proc.undoManager.canUndo());
    redoButton.setEnabled (proc.undoManager.canRedo());
    aButton.setToggleState (proc.getABSlot() == 0, juce::dontSendNotification);
    bButton.setToggleState (proc.getABSlot() == 1, juce::dontSendNotification);
    if (++repaintCounter % 15 == 0) repaint (0, 0, getWidth(), juce::roundToInt (70.0f * (float) getWidth() / (float) baseW));
}

void VoxDnaEditor::paint (juce::Graphics& g)
{
    using namespace juce;
    g.addTransform (AffineTransform::scale ((float) getWidth() / (float) baseW));
    const auto b = Rectangle<float> (0, 0, (float) baseW, (float) baseH);
    g.setGradientFill (ColourGradient (Pal::bg2, 0, 0, Pal::bg, 0, b.getBottom(), false));
    g.fillRect (b);
    g.setGradientFill (ColourGradient (Pal::violet.withAlpha (0.16f), 300, -60, Pal::violet.withAlpha (0.0f), 900, 400, true));
    g.fillRect (b);
    g.setGradientFill (ColourGradient (Pal::magenta.withAlpha (0.07f), b.getRight() - 80, b.getBottom(), Pal::magenta.withAlpha (0.0f), b.getRight() - 700, b.getBottom() - 500, true));
    g.fillRect (b);
    paintHeader (g);
}

void VoxDnaEditor::paintHeader (juce::Graphics& g)
{
    using namespace juce;
    // ---- logo (con un brillo que lo recorre cada pocos segundos) ----
    g.setFont (display (44.0f));
    {
        ColourGradient lg (Colours::white, 18.0f, 8.0f, Pal::lilac, 260.0f, 52.0f, false);
        if (motionMode() > 0)
        {
            const double cyc = std::fmod (ctx.motion.time, 5.0) / 1.2;   // barrido de 1.2 s cada 5 s
            if (cyc < 1.0)
            {
                const double p = 0.05 + 0.9 * cyc;
                lg.addColour (jlimit (0.01, 0.99, p - 0.06), Pal::lilac.interpolatedWith (Colours::white, 0.5f));
                lg.addColour (jlimit (0.01, 0.99, p), Pal::cyan.brighter (0.6f));
                lg.addColour (jlimit (0.01, 0.99, p + 0.06), Pal::lilac.interpolatedWith (Colours::white, 0.5f));
            }
        }
        g.setGradientFill (lg);
    }
    drawSpaced (g, "VOX DNA", { 16.0f, 2.0f, 300.0f, 50.0f }, 2.0f, Justification::centredLeft);
    g.setFont (display (17.0f, false));
    g.setColour (Pal::lilac.interpolatedWith (Pal::magenta, motionMode() > 0 ? ctx.motion.beat : 0.0f));
    drawSpaced (g, "PRO", { 262.0f, 4.0f, 60.0f, 22.0f }, 1.5f, Justification::centredLeft);
    g.setFont (font (13.0f, true));
    g.setColour (Pal::dim);
    drawSpaced (g, "URBAN  VOCAL  LABORATORY", { 18.0f, 46.0f, 300.0f, 16.0f }, 4.5f, Justification::centredLeft);

    // ---- barra de preset ----
    auto bar = Rectangle<float> (487.0f, 8.0f, 572.0f, 40.0f);
    g.setColour (Pal::panel.withAlpha (0.95f));
    g.fillRoundedRectangle (bar, 6.0f);
    g.setColour (Pal::border);
    g.drawRoundedRectangle (bar.reduced (0.5f), 6.0f, 1.0f);
    g.drawVerticalLine (564, bar.getY() + 6.0f, bar.getBottom() - 6.0f);
    g.drawVerticalLine (937, bar.getY() + 6.0f, bar.getBottom() - 6.0f);
    StringArray line;
    if (info.genre.isNotEmpty()) line.add (info.genre);
    line.addArray (info.tags);
    if (info.evidence.isNotEmpty()) line.add (info.evidence);
    g.setFont (font (13.0f, true));
    g.setColour (Pal::lilac.withAlpha (0.85f));
    drawSpaced (g, line.joinIntoString (String::fromUTF8 ("  \xc2\xb7  ")), { 487.0f, 50.0f, 572.0f, 16.0f }, 0.8f, Justification::centred);

    auto qb = Rectangle<float> (1302.0f, 8.0f, 170.0f, 42.0f);
    g.setColour (Pal::panel.withAlpha (0.95f));
    g.fillRoundedRectangle (qb, 6.0f);
    g.setColour (Pal::border);
    g.drawRoundedRectangle (qb.reduced (0.5f), 6.0f, 1.0f);
    const int lat = proc.reportedLatency.load();
    const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
    g.setFont (font (12.5f, true));
    g.setColour (Pal::dim);
    g.drawText ("LAT " + String (lat * 1000.0 / sr, 1) + " ms", Rectangle<float> (1302.0f, 52.0f, 216.0f, 14.0f), Justification::centredRight);
}

void VoxDnaEditor::resized()
{
    content.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) baseW));
}

// ============================================================================
void VoxDnaEditor::showPresetMenu()
{
    juce::PopupMenu menu;
    const auto& fp = getFactoryPresets();
    juce::String cat;
    juce::PopupMenu sub;
    for (int i = 0; i < (int) fp.size(); ++i)
    {
        if (fp[(size_t) i].category != cat)
        {
            if (cat.isNotEmpty()) menu.addSubMenu (cat, sub);
            sub = {};
            cat = fp[(size_t) i].category;
        }
        sub.addItem (i + 1, displayName (fp[(size_t) i]), true, proc.getLoadedFactoryIndex() == i && fp[(size_t) i].name == proc.getPresetName());
    }
    if (cat.isNotEmpty()) menu.addSubMenu (cat, sub);
    const auto users = proc.getUserPresets();
    if (! users.isEmpty())
    {
        juce::PopupMenu um;
        for (int i = 0; i < users.size(); ++i) um.addItem (5000 + i, users[i].getFileNameWithoutExtension());
        menu.addSubMenu ("USER", um);
    }
    juce::Component::SafePointer<VoxDnaEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&titleButton), [safe, users] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (r >= 5000) safe->proc.loadUserPreset (users[r - 5000]);
        else safe->proc.loadFactoryPreset (r - 1);
    });
}

void VoxDnaEditor::showMoreMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Guardar como preset de usuario...");
    m.addItem (2, "Exportar preset (.vdna)...");
    m.addItem (3, "Importar preset (.vdna)...");
    m.addSeparator();
    m.addItem (4, juce::String ("Copiar ajuste actual a ") + (proc.getABSlot() == 0 ? "B" : "A"));
    m.addItem (5, "Imagen propia para este preset...");
    m.addSeparator();
    m.addItem (7, "Abrir carpeta de presets");
    juce::Component::SafePointer<VoxDnaEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&moreButton), [safe] (int r)
    {
        if (safe == nullptr) return;
        switch (r)
        {
            case 1: safe->savePresetDialog(); break;
            case 2: safe->exportPreset(); break;
            case 3: safe->importPreset(); break;
            case 4: safe->proc.copyCurrentToOtherSlot(); break;
            case 5: safe->showImageMenu(); break;
            case 7: VoxDnaProcessor::getUserPresetFolder().createDirectory(); VoxDnaProcessor::getUserPresetFolder().startAsProcess(); break;
            default: break;
        }
    });
}

void VoxDnaEditor::showImageMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("IMAGEN DEL PRESET");
    m.addItem (1, "Elegir imagen propia (PNG/JPG)...");
    m.addItem (2, "Volver al arte generativo", ctx.userImage (info.key).isValid());
    juce::Component::SafePointer<VoxDnaEditor> safe (this);
    m.showMenuAsync ({}, [safe] (int r)
    {
        if (safe == nullptr) return;
        if (r == 2) { safe->ctx.clearUserImage (safe->info.key); safe->presetChanged(); safe->browser.rebuild(); }
        if (r != 1) return;
        safe->chooser = std::make_unique<juce::FileChooser> ("Imagen para " + safe->info.title, juce::File(), "*.png;*.jpg;*.jpeg");
        safe->chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
        {
            if (safe == nullptr || ! fc.getResult().existsAsFile()) return;
            safe->ctx.setUserImage (safe->info.key, fc.getResult());
            safe->presetChanged();
            safe->browser.rebuild();
        });
    });
}

void VoxDnaEditor::showSettingsMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("VOX DNA PRO " JucePlugin_VersionString);
    m.addItem (1, "Mostrar ayudas (tooltips)", true, ctx.settings.tooltips);
    juce::PopupMenu anim;
    anim.addItem (21, "Completas (60 fps)", true, ctx.settings.motion == 2);
    anim.addItem (22, "Suaves (30 fps, menos particulas)", true, ctx.settings.motion == 1);
    anim.addItem (23, "Desactivadas", true, ctx.settings.motion == 0);
    m.addSubMenu ("Animaciones", anim);
    m.addItem (2, "Reiniciar medidores (I / LRA / TP)");
    m.addItem (3, "Abrir carpeta de datos (presets, referencias, imagenes)");
    m.addSeparator();
    const int lat = proc.reportedLatency.load();
    const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
    m.addItem (99, "Latencia reportada: " + juce::String (lat) + " muestras (" + juce::String (lat * 1000.0 / sr, 1) + " ms)", false);
    m.addItem (98, "Fuentes: Rajdhani y VoxDisplay (SIL OFL 1.1)", false);
    juce::Component::SafePointer<VoxDnaEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&gearButton), [safe] (int r)
    {
        if (safe == nullptr) return;
        if (r == 1)
        {
            safe->ctx.settings.tooltips = ! safe->ctx.settings.tooltips;
            safe->ctx.settings.save();
            safe->tooltips->setEnabled (safe->ctx.settings.tooltips);
        }
        if (r >= 21 && r <= 23)
        {
            safe->ctx.settings.motion = 23 - r;
            safe->ctx.settings.save();
            safe->applyMotionMode();
            safe->repaint();
        }
        if (r == 2) safe->proc.resetMeters();
        if (r == 3) { VoxDnaProcessor::getDataFolder().createDirectory(); VoxDnaProcessor::getDataFolder().startAsProcess(); }
    });
}

void VoxDnaEditor::savePresetDialog()
{
    auto* aw = new juce::AlertWindow ("GUARDAR PRESET", "Nombre y etiqueta (objetivo musical):", juce::MessageBoxIconType::NoIcon, this);
    aw->setLookAndFeel (&lnf);
    aw->addTextEditor ("name", info.factory ? info.title.toLowerCase() + " (mi version)" : proc.getPresetName());
    aw->addComboBox ("tag", presetTags(), "Etiqueta");
    aw->addButton ("GUARDAR", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("CANCELAR", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<VoxDnaEditor> safe (this);
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([safe, aw] (int result)
    {
        std::unique_ptr<juce::AlertWindow> owner (aw);
        const auto name = aw->getTextEditorContents ("name").trim();
        juce::String tag;
        if (auto* cb = aw->getComboBoxComponent ("tag")) tag = cb->getText();
        aw->setLookAndFeel (nullptr);
        if (result == 1 && name.isNotEmpty() && safe != nullptr && safe->proc.saveUserPreset (name, tag))
            safe->browser.rebuild();
    }), false);
}

void VoxDnaEditor::exportPreset()
{
    chooser = std::make_unique<juce::FileChooser> ("Exportar preset", juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                                                       .getChildFile (juce::File::createLegalFileName (info.artist + " - " + info.title) + ".vdna"), "*.vdna");
    juce::Component::SafePointer<VoxDnaEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [safe] (const juce::FileChooser& fc)
    {
        if (safe == nullptr || fc.getResult() == juce::File()) return;
        safe->proc.exportPreset (fc.getResult().withFileExtension ("vdna"), safe->info.artist + " - " + safe->info.title);
    });
}

void VoxDnaEditor::importPreset()
{
    chooser = std::make_unique<juce::FileChooser> ("Importar preset", juce::File(), "*.vdna");
    juce::Component::SafePointer<VoxDnaEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
    {
        if (safe == nullptr || ! fc.getResult().existsAsFile()) return;
        auto dest = VoxDnaProcessor::getUserPresetFolder().getChildFile (fc.getResult().getFileName());
        VoxDnaProcessor::getUserPresetFolder().createDirectory();
        if (dest != fc.getResult()) fc.getResult().copyFileTo (dest);
        if (safe->proc.loadUserPreset (dest)) safe->browser.rebuild();
    });
}

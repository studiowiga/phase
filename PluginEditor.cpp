#include "PluginEditor.h"
#include "BinaryData.h"

using namespace juce;

static Image loadImg (const void* d, int n) { return ImageFileFormat::loadFrom (d, (size_t) n); }

//==============================================================================
Phase90Editor::Phase90Editor (Phase90Processor& p)
    : AudioProcessorEditor (p), proc (p)
{
    background = loadImg (BinaryData::background_jpg, BinaryData::background_jpgSize);
    glowTicks  = loadImg (BinaryData::glow_ticks_png, BinaryData::glow_ticks_pngSize);
    glowFoot   = loadImg (BinaryData::glow_foot_png, BinaryData::glow_foot_pngSize);
    indexImg   = loadImg (BinaryData::knob_index_png, BinaryData::knob_index_pngSize);
    markImg    = loadImg (BinaryData::knob_mark_png, BinaryData::knob_mark_pngSize);
    labelFace  = Typeface::createSystemTypefaceFor (BinaryData::LiberationSansRegular_ttf, BinaryData::LiberationSansRegular_ttfSize);

    {   // encre réelle du "H" : hauteur et position des capitales
        GlyphArrangement ga;
        ga.addLineOfText (Font (FontOptions (labelFace).withHeight (100.0f)), "H", 0, 0);
        Path h; ga.createPath (h);
        const auto b = h.getBounds();
        if (b.getHeight() > 1.0f) { capRatio = b.getHeight() / 100.0f; capTopRatio = b.getY() / 100.0f; }
    }

    // taille : identique à Small Tone / Holyverb, jamais plus petite que la taille par défaut
    const double aspect = (double) art::W / (double) art::H;
    const int h0 = jlimit (p9ui::heightDefault, p9ui::heightMax, proc.uiHeight.load());   // lu avant setResizeLimits (qui appelle resized)
    setResizable (true, true);
    setResizeLimits (roundToInt (p9ui::heightDefault * aspect), p9ui::heightDefault,
                     roundToInt (p9ui::heightMax * aspect), p9ui::heightMax);
    getConstrainer()->setFixedAspectRatio (aspect);
    setSize (roundToInt (h0 * aspect), h0);

    tickLevel = footLevel = proc.isOn() ? 1.0f : 0.0f;
    startTimerHz (60);
}

Phase90Editor::~Phase90Editor() { stopTimer(); }

//==============================================================================
void Phase90Editor::resized()
{
    const int minW = roundToInt (p9ui::heightDefault * (double) art::W / (double) art::H);
    if (getHeight() < p9ui::heightDefault || getWidth() < minW) { setSize (minW, p9ui::heightDefault); return; }
    proc.uiHeight.store (getHeight());
    rebuildScaledImages();
}

void Phase90Editor::setWindowHeight (int h)
{
    h = jlimit (p9ui::heightDefault, p9ui::heightMax, h);
    setSize (roundToInt ((double) h * art::W / art::H), h);
}

void Phase90Editor::rebuildScaledImages()
{
    // images pré-redimensionnées une seule fois (affichage fluide à 60 i/s)
    const float s = scale();
    auto rs = [s] (const Image& im) -> Image
    {
        if (! im.isValid()) return {};
        const int w = jmax (1, roundToInt (im.getWidth() * s)), h = jmax (1, roundToInt (im.getHeight() * s));
        return im.rescaled (w, h, Graphics::highResamplingQuality);
    };
    if (background.isValid()) sBackground = background.rescaled (getWidth(), getHeight(), Graphics::highResamplingQuality);
    sGlowTicks = rs (glowTicks); sGlowFoot = rs (glowFoot);
}

//==============================================================================
String Phase90Editor::speedText() const
{
    if (proc.syncState() > 0) return "SYNC " + Phase90Processor::syncDivisionName (proc.syncState());
    const double hz = proc.speedHz();
    return String (hz, hz < 1.0 ? 2 : 1) + " HZ";
}

String Phase90Editor::speedStatus() const
{
    if (proc.syncState() > 0) return speedText() + "  -  SPEED INACTIVE";
    return "SPEED " + speedText() + "  -  " + (proc.isBlock() ? "BLOCK" : "SCRIPT");
}

String Phase90Editor::syncStatus() const
{
    const int sd = proc.syncState();
    if (sd == 0) { const double hz = proc.speedHz(); return "SYNC OFF  -  SPEED " + String (hz, hz < 1.0 ? 2 : 1) + " HZ"; }
    return "SYNC " + Phase90Processor::syncDivisionName (sd) + "  -  " + String (roundToInt (proc.hostBpm.load())) + " BPM";
}

// la ligne d'état n'apparaît que pendant un réglage (puis 1,5 s) et pendant un MIDI Learn
String Phase90Editor::statusText() const
{
    if (proc.learnTarget.load() >= 0) return "MIDI LEARN...";
    if (Time::getMillisecondCounterHiRes() * 0.001 < flashUntil + art::statusFade) return flashText;
    return {};
}

float Phase90Editor::statusAlpha() const
{
    if (proc.learnTarget.load() >= 0) return 1.0f;
    const double t = Time::getMillisecondCounterHiRes() * 0.001;
    return (float) jlimit (0.0, 1.0, (flashUntil + art::statusFade - t) / art::statusFade);
}

void Phase90Editor::flash (const String& t, double seconds)
{
    flashText = t;
    flashUntil = Time::getMillisecondCounterHiRes() * 0.001 + seconds;
    repaint();
}

void Phase90Editor::setParam (const String& id, float plain)
{
    if (auto* p = proc.apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
        p->endChangeGesture();
    }
}

void Phase90Editor::setSpeed (float v)
{
    if (auto* p = proc.apvts.getParameter ("speed"))
        p->setValueNotifyingHost (jlimit (0.0f, 1.0f, v));
    proc.presetName = {};
}

//==============================================================================
void Phase90Editor::timerCallback()
{
    const bool on = proc.isOn();
    const bool follow = proc.apvts.getRawParameterValue ("ticklfo")->load() > 0.5f;
    const float lfo = proc.lfoVisual.load();                  // 0 = haut du balayage, 1 = bas
    const float tickTarget = on ? (follow ? 0.40f + 0.60f * (1.0f - lfo) : 1.0f) : 0.0f;
    const float k = 0.35f;   // LED : allumage rapide mais pas instantané
    tickLevel += (tickTarget - tickLevel) * k;
    footLevel += ((on ? 1.0f : 0.0f) - footLevel) * k;

    // bouton SYNC : l'anneau s'allume et clignote une fois par division (plein sur le temps, atténué entre deux)
    const int sd = proc.syncState();
    const float ph = proc.lfoPhaseVisual.load();
    const float syncTarget = sd > 0 ? 0.30f + 0.70f * std::pow (1.0f - ph, 2.2f) : 0.0f;
    syncLevel += (syncTarget - syncLevel) * 0.5f;

    // un réglage a changé (souris, MIDI, automation du DAW, preset) -> la ligne d'état apparaît
    const float sp = proc.getSpeed();
    const int bl = proc.isBlock() ? 1 : 0, onI = on ? 1 : 0;
    if (lastOn >= 0)
    {
        if (std::abs (sp - lastSpeed) > 1.0e-5f) flash (speedStatus());
        if (bl != lastBlock)  flash (bl ? "BLOCK (R28 ON)" : "SCRIPT '74 (R28 OFF)");
        if (onI != lastOn)    flash (onI ? "ON" : "BYPASS", 1.0);
        if (sd != lastSync)   flash (syncStatus());
        if (proc.presetName != lastPreset && proc.presetName.isNotEmpty() && ! flashText.contains (proc.presetName.toUpperCase()))
            flash (proc.presetName.toUpperCase(), 2.0);
    }
    lastSpeed = sp; lastBlock = bl; lastOn = onI; lastSync = sd; lastPreset = proc.presetName;

    const int learned = proc.lastLearned.exchange (-1);
    if (learned >= 0)
    {
        const int cc = learned == 0 ? proc.ccFoot.load() : learned == 1 ? proc.ccSpeed.load() : learned == 2 ? proc.ccVoice.load() : proc.ccSync.load();
        flash (String (learned == 0 ? "FOOTSWITCH" : learned == 1 ? "SPEED" : learned == 2 ? "SCRIPT/BLOCK" : "SYNC") + " = CC " + String (cc), 2.0);
    }

    // on ne redessine que ce qui bouge (graduations + potard, footswitch, ligne d'état)
    const float s = scale();
    repaint (Rectangle<float> (art::ticksX * s, art::ticksY * s, 580.0f * s, 580.0f * s).toNearestInt().expanded (2));
    repaint (Rectangle<float> (art::footX * s, art::footY * s, 400.0f * s, 400.0f * s).toNearestInt().expanded (2));
    repaint (toWin ({ art::statusX - 360.0f, art::statusY - 40.0f, 720.0f, 80.0f }).toNearestInt());
    repaint (toWin ({ art::sync.x - 110.0f, art::sync.y - 90.0f, 220.0f, 200.0f }).toNearestInt());
}

//==============================================================================
void Phase90Editor::paint (Graphics& g)
{
    g.fillAll (Colour (7, 7, 7));
    if (sBackground.isValid()) g.drawImageAt (sBackground, 0, 0);
    const float s = scale();

    auto drawOverlay = [&] (const Image& im, float x, float y, float alpha)
    {
        if (alpha <= 0.003f || ! im.isValid()) return;
        g.setOpacity (jlimit (0.0f, 1.0f, alpha));
        g.drawImageAt (im, roundToInt (x * s), roundToInt (y * s));
        g.setOpacity (1.0f);
    };

    // lumières (calques allumés du visuel, fondus selon l'état)
    drawOverlay (sGlowTicks, art::ticksX, art::ticksY, tickLevel);
    drawOverlay (sGlowFoot, art::footX, art::footY, footLevel);

    // footswitch enfoncé : le chapeau descend légèrement dans l'ombre
    if (footDown)
    {
        const float r = 78.0f * s;
        const Point<float> c (art::foot.x * s, art::foot.y * s + 1.5f * s);
        ColourGradient sh (Colours::black.withAlpha (0.38f), c.x, c.y - r, Colours::black.withAlpha (0.12f), c.x, c.y + r, false);
        g.setGradientFill (sh);
        g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
    }

    paintKnobPointer (g);
    paintSyncButton (g);
    paintStatusLine (g);
}

void Phase90Editor::paintSyncButton (Graphics& g) const
{
    // petit bouton-poussoir dessiné en code (net à toutes les tailles) : même lumière venant du haut que le visuel
    const float s = scale();
    const Point<float> c (art::sync.x * s, art::sync.y * s);
    const float R = art::sync.r * s, ringR = R * 1.16f, r2 = R * 0.80f;
    auto circle = [] (Point<float> cc, float r) { Path p; p.addEllipse (cc.x - r, cc.y - r, 2 * r, 2 * r); return p; };

    // ombre portée
    DropShadow (Colours::black.withAlpha (0.70f), jmax (1, roundToInt (9.0f * s)), { 0, jmax (1, roundToInt (5.0f * s)) })
        .drawForPath (g, circle (c, R * 1.06f));

    // anneau LED (diffuseur) : gris éteint, orange + halo quand SYNC est actif
    const float lv = jlimit (0.0f, 1.0f, syncLevel);
    if (lv > 0.003f)
        for (int i = 6; i >= 1; --i)
        {
            g.setColour (Colour (255, 110, 20).withAlpha (0.30f * lv * (1.0f - (float) i / 7.0f)));
            g.strokePath (circle (c, ringR), PathStrokeType ((3.2f + 3.6f * (float) i) * s));
        }
    g.setColour (Colour (70, 62, 58).interpolatedWith (Colour (255, 196, 116), lv));
    g.strokePath (circle (c, ringR), PathStrokeType (3.2f * s));

    // jupe chromée
    g.setGradientFill (ColourGradient (Colour (226, 226, 230), c.x, c.y - R, Colour (78, 78, 82), c.x, c.y + R, false));
    g.fillPath (circle (c, R));
    // chapeau noir + reflet en haut
    g.setGradientFill (ColourGradient (Colour (64, 64, 66), c.x, c.y - r2, Colour (10, 10, 11), c.x, c.y + r2, false));
    g.fillPath (circle (c, r2));
    {
        ColourGradient hl (Colours::white.withAlpha (0.30f), c.x, c.y - r2 * 0.55f, Colours::white.withAlpha (0.0f), c.x + r2 * 0.75f, c.y - r2 * 0.55f, true);
        g.setGradientFill (hl);
        g.fillEllipse (c.x - r2 * 0.75f, c.y - r2 * 0.85f, r2 * 1.5f, r2 * 0.62f);
    }
    g.setColour (Colour (15, 15, 16));
    g.strokePath (circle (c, r2), PathStrokeType (1.2f * s));

    // inscription : "SYNC" (gris) ou "SYNC 1/4" (orange)
    const int sd = proc.syncState();
    paintText (g, sd > 0 ? "SYNC " + Phase90Processor::syncDivisionName (sd) : String ("SYNC"),
               art::sync.x, art::syncLabelY, art::syncLabelCapH, 210.0f,
               sd > 0 ? Colour (255, 150, 60) : Colour (214, 214, 218).withAlpha (0.92f));
}

void Phase90Editor::paintKnobPointer (Graphics& g) const
{
    // Seuls l'index peint et le repère de la jupe tournent : les reflets du métal brossé
    // (lumière venant du haut) restent fixes, et l'index s'assombrit un peu quand il passe
    // du côté de l'ombre, comme sur un vrai potard éclairé par le dessus.
    const float s = scale();
    const float a = (proc.getSpeed() - 0.5f) * 2.0f * art::knobSweep;
    const float light = 0.80f + 0.20f * std::cos (a);
    Graphics::ScopedSaveState ss (g);
    g.setImageResamplingQuality (Graphics::highResamplingQuality);

    auto drawRot = [&] (const Image& im, float x0, float y0, float alpha)
    {
        if (! im.isValid()) return;
        const float px = art::knob.x - x0, py = art::knob.y - y0;        // axe dans l'image
        const auto tr = AffineTransform::translation (-px, -py).rotated (a)
                          .translated (art::knob.x, art::knob.y).scaled (s);
        // ombre portée fixe (lumière du haut) : légèrement vers le bas
        g.setColour (Colours::black.withAlpha (0.30f * alpha));
        g.drawImageTransformed (im, tr.translated (0.0f, 2.2f * s), true);
        g.setOpacity (alpha);
        g.drawImageTransformed (im, tr, false);
    };
    drawRot (indexImg, art::indexX, art::indexY, light);
    drawRot (markImg, art::markX, art::markY, 0.70f + 0.30f * (0.5f + 0.5f * std::cos (a)));
    g.setOpacity (1.0f);
}

void Phase90Editor::paintStatusLine (Graphics& g) const
{
    const String t = statusText();
    const float a = statusAlpha();
    if (t.isEmpty() || a <= 0.003f) return;
    paintText (g, t, art::statusX, art::statusY, art::statusCapH, art::statusMaxW, Colour (214, 214, 218).withAlpha (0.92f * a));
}

void Phase90Editor::paintText (Graphics& g, const String& t, float cx, float cy, float capHArt, float maxWArt, Colour col) const
{
    // même police que "SPEED" ; texte centré sur (cx, cy) = centre des capitales
    const float s = scale();
    float fontH = capHArt / capRatio * s;
    Font f (FontOptions (labelFace).withHeight (fontH));
    f.setExtraKerningFactor (0.03f);
    GlyphArrangement ga;
    ga.addLineOfText (f, t, 0, 0);
    Path p; ga.createPath (p);
    auto bb = p.getBounds();
    const float maxW = maxWArt * s;
    if (bb.getWidth() > maxW)
    {
        fontH *= maxW / bb.getWidth();
        f = f.withHeight (fontH);
        ga.clear(); ga.addLineOfText (f, t, 0, 0);
        p.clear(); ga.createPath (p);
        bb = p.getBounds();
    }
    const float capH = fontH * capRatio;
    const float dx = cx * s - bb.getCentreX();
    const float dy = cy * s - capH * 0.5f - capTopRatio * fontH;
    g.setColour (Colours::black.withAlpha (0.6f * col.getFloatAlpha()));
    g.fillPath (p, AffineTransform::translation (dx, dy + 1.2f * s));
    g.setColour (col);
    g.fillPath (p, AffineTransform::translation (dx, dy));
}

//==============================================================================
Phase90Editor::Hit Phase90Editor::hitAt (Point<float> a) const
{
    if (a.getDistanceFrom ({ art::knob.x, art::knob.y }) <= art::knob.r) return Hit::knob;
    if (art::phaseBox.contains (a)) return Hit::phase;
    if (a.getDistanceFrom ({ art::sync.x, art::sync.y }) <= art::sync.r * 1.5f
        || Rectangle<float> (art::sync.x - 90.0f, art::syncLabelY - 22.0f, 180.0f, 44.0f).contains (a)) return Hit::sync;
    if (a.getDistanceFrom ({ art::foot.x, art::foot.y }) <= art::foot.r) return Hit::foot;
    return Hit::none;
}

void Phase90Editor::mouseMove (const MouseEvent& e)
{
    const auto h = hitAt (toArt (e.position));
    setMouseCursor (h == Hit::none ? MouseCursor::NormalCursor
                  : h == Hit::knob ? MouseCursor::UpDownResizeCursor : MouseCursor::PointingHandCursor);
}

void Phase90Editor::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { showContextMenu(); return; }
    const auto h = hitAt (toArt (e.position));
    dragging = Hit::none;
    switch (h)
    {
        case Hit::knob:
            dragging = Hit::knob; dragStart = e.position; dragStartSpeed = proc.getSpeed();
            if (auto* p = proc.apvts.getParameter ("speed")) p->beginChangeGesture();
            flash (speedStatus());
            break;
        case Hit::sync:
            proc.cycleSync();                     // OFF -> 1/4 -> 1/8 -> 1/2 -> OFF
            proc.presetName = {};
            break;
        case Hit::phase:
        {
            const bool block = ! proc.isBlock();
            setParam ("block", block ? 1.0f : 0.0f);
            proc.presetName = {};
            break;
        }
        case Hit::foot:
            footDown = true;
            setParam ("on", proc.isOn() ? 0.0f : 1.0f);
            break;
        case Hit::none: break;
    }
    repaint();
}

void Phase90Editor::mouseDrag (const MouseEvent& e)
{
    if (dragging != Hit::knob) return;
    const auto d = e.position - dragStart;
    const float range = 260.0f * scale() * (e.mods.isShiftDown() ? 5.0f : 1.0f);
    setSpeed (dragStartSpeed + (d.x - d.y) / range);
}

void Phase90Editor::mouseUp (const MouseEvent&)
{
    if (dragging == Hit::knob)
        if (auto* p = proc.apvts.getParameter ("speed")) p->endChangeGesture();
    dragging = Hit::none;
    if (footDown) { footDown = false; repaint(); }
}

void Phase90Editor::mouseDoubleClick (const MouseEvent& e)
{
    if (hitAt (toArt (e.position)) == Hit::knob) setSpeed (0.5f);
}

void Phase90Editor::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    if (hitAt (toArt (e.position)) != Hit::knob) return;
    const float step = (std::abs (w.deltaY) > 1.0e-6f ? w.deltaY : w.deltaX) * (w.isReversed ? -1.0f : 1.0f);
    setSpeed (proc.getSpeed() + step * (e.mods.isShiftDown() ? 0.02f : 0.08f));
}

//==============================================================================
void Phase90Editor::askUserPresetName (int slot)
{
    nameDialog = std::make_unique<AlertWindow> ("Save user preset " + String (slot), "Preset name:", MessageBoxIconType::NoIcon, this);
    nameDialog->addTextEditor ("name", proc.presetName.isNotEmpty() ? proc.presetName : "User " + String (slot));
    nameDialog->addButton ("Save", 1, KeyPress (KeyPress::returnKey));
    nameDialog->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    nameDialog->enterModalState (true, ModalCallbackFunction::create ([this, slot] (int r)
    {
        if (r == 1 && nameDialog != nullptr)
        {
            auto n = nameDialog->getTextEditorContents ("name").trim();
            if (n.isEmpty()) n = "User " + String (slot);
            proc.saveUserPreset (slot, n);
            flash ("SAVED : " + n.toUpperCase(), 2.0);
        }
        nameDialog.reset();
    }), false);
}

void Phase90Editor::showContextMenu()
{
    PopupMenu factory;
    const auto& fp = Phase90Processor::factoryPresets();
    for (int i = 0; i < (int) fp.size(); ++i)
        factory.addItem (100 + i, fp[(size_t) i].name, true, proc.presetName == fp[(size_t) i].name);

    PopupMenu load, save;
    for (int s = 1; s <= 9; ++s)
    {
        const auto n = proc.userPresetName (s);
        load.addItem (200 + s, "User " + String (s) + (n.isNotEmpty() ? "  -  " + n : String ("  (empty)")), n.isNotEmpty());
        save.addItem (300 + s, "User " + String (s) + (n.isNotEmpty() ? "  -  " + n + "  (overwrite)" : String()));
    }

    PopupMenu size;
    const int h = getHeight();
    size.addItem (401, "Default (" + String (p9ui::heightDefault) + " px)", true, std::abs (h - p9ui::heightDefault) < 8);
    size.addItem (402, "60 %", true, std::abs (h - p9ui::height60) < 8);
    size.addItem (403, "75 %", true, std::abs (h - p9ui::height75) < 8);
    size.addItem (404, "100 %", true, std::abs (h - p9ui::height100) < 8);

    auto flag = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load() > 0.5f; };
    PopupMenu voice;
    voice.addItem (520, "Script '74 (no R28 - smooth, vintage)", true, ! proc.isBlock());
    voice.addItem (521, "Block '77 (R28 feedback - more mids, M101)", true, proc.isBlock());

    PopupMenu opt;
    PopupMenu syncM;
    for (int i = 0; i < Phase90Processor::numSyncStates; ++i)
        syncM.addItem (530 + i, i == 0 ? String ("Off (SPEED in Hz)") : "Sync " + Phase90Processor::syncDivisionName (i), true, proc.syncState() == i);
    opt.addSubMenu ("Tempo sync - " + String (proc.hostBpm.load(), 1) + " BPM (SYNC button)", syncM);
    opt.addItem (502, "Stereo spread (right channel LFO inverted)", true, flag ("stereo"));
    opt.addItem (504, "Speed ring pulses with the LFO", true, flag ("ticklfo"));

    PopupMenu midi, chan;
    const int ch = (int) proc.apvts.getRawParameterValue ("midichan")->load();
    chan.addItem (600, "Omni", true, ch == 0);
    for (int c = 1; c <= 16; ++c) chan.addItem (600 + c, String (c), true, ch == c);
    midi.addSubMenu ("Channel", chan);
    midi.addSeparator();
    midi.addItem (701, "Learn: Footswitch (CC " + String (proc.ccFoot.load()) + ")");
    midi.addItem (702, "Learn: Speed (CC " + String (proc.ccSpeed.load()) + ")");
    midi.addItem (703, "Learn: Script / Block (CC " + String (proc.ccVoice.load()) + ")");
    midi.addItem (705, "Learn: Sync button (CC " + String (proc.ccSync.load()) + ")");
    midi.addItem (704, "Reset MIDI assignments (CC 80-83)");
    midi.addSeparator();
    const bool mom = flag ("ccmomentary");
    midi.addItem (710, "Footswitch: latching pedal (127 = on, 0 = bypass)", true, ! mom);
    midi.addItem (711, "Footswitch: momentary pedal (each press toggles)", true, mom);
    midi.addItem (-1, "Program Change n = factory preset n+1", false, false);

    PopupMenu m;
    m.addSectionHeader ("Phase 90 v" JucePlugin_VersionString " - STUDIO WIGA");
    m.addSubMenu ("Factory presets", factory);
    m.addSubMenu ("Load user preset", load);
    m.addSubMenu ("Save user preset", save);
    m.addSeparator();
    m.addSubMenu ("Voicing", voice);
    m.addSubMenu ("Window size", size);
    m.addSubMenu ("Options", opt);
    m.addSubMenu ("MIDI", midi);

    m.showMenuAsync (PopupMenu::Options().withMousePosition().withTargetComponent (this), [this, flag] (int r)
    {
        auto toggleFlag = [this] (const char* id) { setParam (id, proc.apvts.getRawParameterValue (id)->load() > 0.5f ? 0.0f : 1.0f); };
        if (r >= 100 && r < 100 + (int) Phase90Processor::factoryPresets().size())
        {
            proc.setCurrentProgram (r - 100);
            proc.updateHostDisplay (AudioProcessor::ChangeDetails().withProgramChanged (true));
        }
        else if (r > 200 && r < 210) { if (proc.loadUserPreset (r - 200)) flash ("LOADED : " + proc.presetName.toUpperCase()); }
        else if (r > 300 && r < 310) askUserPresetName (r - 300);
        else if (r == 401) setWindowHeight (p9ui::heightDefault);
        else if (r == 402) setWindowHeight (p9ui::height60);
        else if (r == 403) setWindowHeight (p9ui::height75);
        else if (r == 404) setWindowHeight (p9ui::height100);
        else if (r == 520 || r == 521)
        {
            setParam ("block", r == 521 ? 1.0f : 0.0f);
            proc.presetName = {};
        }
        else if (r >= 530 && r < 530 + Phase90Processor::numSyncStates) { setParam ("syncdiv", (float) (r - 530)); proc.presetName = {}; }
        else if (r == 502) { toggleFlag ("stereo"); flash (flag ("stereo") ? "STEREO SPREAD" : "MONO (ORIGINAL)"); }
        else if (r == 504) toggleFlag ("ticklfo");
        else if (r >= 600 && r <= 616) setParam ("midichan", (float) (r - 600));
        else if (r >= 701 && r <= 703) proc.learnTarget.store (r - 701);
        else if (r == 705) proc.learnTarget.store (3);
        else if (r == 704) { proc.ccFoot = 80; proc.ccSpeed = 81; proc.ccVoice = 82; proc.ccSync = 83; flash ("CC 80 / 81 / 82 / 83"); }
        else if (r == 710 || r == 711) setParam ("ccmomentary", r == 711 ? 1.0f : 0.0f);
        repaint();
    });
}

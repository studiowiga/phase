#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

//==============================================================================
// Géométrie du visuel (pixels de background.jpg : 1520 x 1916, même format que Small Tone / Holyverb)
namespace art
{
constexpr float W = 1520.0f, H = 1916.0f;
struct Circle { float x, y, r; };
constexpr Circle knob   { 756.0f, 386.0f, 215.0f };      // potard SPEED (axe de rotation + zone cliquable)
constexpr float  knobSweep = 2.6180f;                     // ±150° (graduations de la sérigraphie)
constexpr Circle foot   { 760.0f, 1373.0f, 150.0f };      // footswitch + anneau lumineux
// calques (position du coin haut-gauche dans le visuel)
constexpr float ticksX = 471.0f, ticksY = 110.0f;         // graduations orange allumées
constexpr float footX  = 560.0f, footY  = 1173.0f;        // anneau du footswitch allumé
constexpr float indexX = 712.0f, indexY = 200.0f;         // index blanc du chapeau (tourne)
constexpr float markX  = 730.0f, markY  = 172.0f;         // repère sur la jupe du potard (tourne)
const juce::Rectangle<float> phaseBox { 488.0f, 818.0f, 546.0f, 194.0f };   // cartouche "PHASE" : SCRIPT / BLOCK
constexpr float statusX = 760.0f, statusY = 1110.0f;      // ligne d'état (zone noire sous "PHASE")
constexpr float statusCapH = 33.0f;                       // hauteur des capitales (style "SPEED")
constexpr float statusMaxW = 660.0f;
constexpr Circle sync   { 1036.0f, 606.0f, 30.0f };       // bouton SYNC (en bas à droite du potard SPEED)
constexpr float syncLabelY = 673.0f, syncLabelCapH = 19.5f;   // inscription "SYNC" / "SYNC 1/4" (centre des capitales)
constexpr float statusHold = 1.5f, statusFade = 0.4f;    // ligne d'état : visible 1,5 s après le dernier réglage puis fondu
}

//==============================================================================
class Phase90Editor : public juce::AudioProcessorEditor,
                      private juce::Timer
{
public:
    explicit Phase90Editor (Phase90Processor&);
    ~Phase90Editor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override;
    void visibilityChanged() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void timerCallback() override;
    void useSoftwareRenderer();
    float scale() const { return (float) getWidth() / art::W; }
    juce::Point<float> toArt (juce::Point<float> p) const { return p / scale(); }
    juce::Rectangle<float> toWin (juce::Rectangle<float> r) const { const float s = scale(); return { r.getX() * s, r.getY() * s, r.getWidth() * s, r.getHeight() * s }; }
    enum class Hit { none, knob, phase, foot, sync };
    Hit hitAt (juce::Point<float> artPos) const;

    void setParam (const juce::String& id, float plain);
    void setSpeed (float v01);
    void flash (const juce::String& text, double seconds = art::statusHold);
    float statusAlpha() const;
    juce::String speedStatus() const;
    juce::String syncStatus() const;
    void paintSyncButton (juce::Graphics&) const;
    void paintText (juce::Graphics&, const juce::String&, float centreX, float capCentreY, float capH, float maxW, juce::Colour) const;
    juce::String statusText() const;
    juce::String speedText() const;
    void showContextMenu (Hit hit = Hit::none);
    void setWindowHeight (int h);
    void askUserPresetName (int slot);
    void rebuildScaledImages();
    void paintKnobPointer (juce::Graphics&) const;
    void paintStatusLine (juce::Graphics&) const;

    Phase90Processor& proc;
    juce::Image background, glowTicks, glowFoot, indexImg, markImg;
    juce::Image sBackground, sGlowTicks, sGlowFoot;
    // anti-gel v2 : caches refaits par le timer seulement, suivi de la fenêtre hôte et des redessins
    int cacheW = 0, cacheH = 0;
    float cacheScale = 0.0f;
    int resizeSettle = 0;
    void* lastPeer = nullptr;
    double lastPaintMs = 0.0, repaintAskedMs = 0.0, lastScaleCheckMs = 0.0, lastRendererCheckMs = 0.0;
    float physicalScale() const;
    void askRepaint (juce::Rectangle<int> r);
    juce::Typeface::Ptr labelFace;
    float capRatio = 0.72f, capTopRatio = -0.72f;    // capitales : hauteur et haut (y) / hauteur de police

    float tickLevel = 1.0f, footLevel = 1.0f, syncLevel = 0.0f;
    // dernières valeurs vues : un changement (souris, MIDI, automation, preset) fait apparaître la ligne d'état
    float lastSpeed = -1.0f; int lastBlock = -1, lastOn = -1, lastSync = -1; juce::String lastPreset;
    bool footDown = false;
    Hit dragging = Hit::none;
    float dragStartSpeed = 0.0f;
    juce::Point<float> dragStart;
    juce::String flashText;
    double flashUntil = 0.0;

    // dernier état réellement dessiné : on ne redessine une zone que si elle a changé
    float drawnTick = -1.0f, drawnSpeed = -1.0f, drawnFoot = -1.0f, drawnSyncLevel = -1.0f, drawnStatusAlpha = -1.0f;
    int drawnSyncState = -1;
    bool drawnFootDown = false;
    juce::String drawnStatus;

    std::unique_ptr<juce::AlertWindow> nameDialog;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Phase90Editor)
};

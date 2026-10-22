#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Phase90Engine.h"

namespace p9ui
{
// tailles de fenêtre identiques à Small Tone / Holyverb (et Freeze Shimmer / Tuna) :
// 100 % = 986 px de haut, ouverture par défaut = 493 px (= taille minimale)
constexpr int heightDefault = 493, height60 = 592, height75 = 740, height100 = 986, heightMax = 1479;
}

struct FactoryPreset { const char* name; double hz; bool block; };

class Phase90Processor : public juce::AudioProcessor,
                         private juce::AsyncUpdater
{
public:
    Phase90Processor();
    ~Phase90Processor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Phase 90"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.01; }

    // presets usine = programmes VST3 (menu déroulant de presets de la fenêtre du DAW)
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // ----- presets
    static const std::vector<FactoryPreset>& factoryPresets();
    juce::String presetName;                 // nom affiché sur la ligne d'état (vide = réglage libre)
    void applyPreset (float speed, bool block, const juce::String& name);
    // presets utilisateur 1 … 9 (communs à toutes les instances)
    static juce::File userPresetFile();
    juce::String userPresetName (int slot) const;          // "" si vide
    bool loadUserPreset (int slot);
    void saveUserPreset (int slot, const juce::String& name);

    // ----- MIDI
    std::atomic<int> ccFoot { 80 }, ccSpeed { 81 }, ccVoice { 82 };
    std::atomic<int> learnTarget { -1 };      // 0 foot, 1 speed, 2 script/block
    std::atomic<int> lastLearned { -1 };      // pour l'affichage
    std::atomic<int> uiHeight { p9ui::heightDefault };

    // ----- infos pour l'interface
    std::atomic<float> lfoVisual { 0.5f }, sweepVisual { 1000.0f };
    std::atomic<double> hostBpm { 120.0 };
    static juce::String syncDivisionName (int i);
    static double syncDivisionBeats (int i);
    static int syncDivisionIndex (float speed01);
    static constexpr int numSyncDivisions = 9;

    bool isOn() const      { return apvts.getRawParameterValue ("on")->load() > 0.5f; }
    bool isBlock() const   { return apvts.getRawParameterValue ("block")->load() > 0.5f; }
    float getSpeed() const { return apvts.getRawParameterValue ("speed")->load(); }
    double speedHz() const { return phase90::Engine::speedHz (getSpeed(), isBlock()); }

private:
    void handleAsyncUpdate() override;
    void setParam (const juce::String& id, float plainValue);

    phase90::Engine engine;
    int currentProgram = 0;

    // changements venant du MIDI (thread audio) appliqués sur le thread de l'interface
    std::atomic<float> pendingSpeed { -1.0f };
    std::atomic<int> pendingVoice { -1 }, pendingFootToggle { 0 }, pendingFootSet { -1 }, pendingProgram { -1 };
    bool footCcWasHigh = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Phase90Processor)
};

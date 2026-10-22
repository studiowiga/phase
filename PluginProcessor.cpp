#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

//==============================================================================
const std::vector<FactoryPreset>& Phase90Processor::factoryPresets()
{
    // vitesse du LFO en Hz (convertie en position du potard SPEED) / BLOCK (R28) ou SCRIPT
    static const std::vector<FactoryPreset> p {
        { "Classic Block",    0.92, true,  0 },   // potard à midi, le Phase 90 actuel (M101)
        { "Script '74",       0.70, false, 0 },   // le son doux et rond des premiers modèles
        { "Eruption",         0.28, false, 0 },   // balayage lent sur un son saturé
        { "Brown Sound",      0.40, true,  0 },   // rythmique crunch, R28 pour le mordant
        { "Slow Jet",         0.12, true,  0 },   // effet "avion" très lent
        { "Funk Rhythm",      2.20, true,  0 },   // rythmique funk en clean
        { "Fast Warble",      4.40, false, 0 },   // vitesse maxi Script
        { "Rotary Fast",      5.10, true,  0 },   // vitesse maxi Block, façon haut-parleur tournant
        { "Bass Swirl",       0.50, false, 0 },   // pour la basse (Script = moins de creux)
        { "Tempo 1/4",        0.92, true,  1 },   // un balayage par temps, calé sur le DAW
        { "Tempo 1/8 Funk",   2.20, true,  2 },   // deux balayages par temps
        { "Tempo 1/2 Swell",  0.40, false, 3 },   // un balayage tous les 2 temps
    };
    return p;
}

//==============================================================================
AudioProcessorValueTreeState::ParameterLayout Phase90Processor::createLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "speed", 1 }, "Speed",
        NormalisableRange<float> (0.0f, 1.0f), 0.5f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return String (phase90::Engine::speedHz (v, true), 2) + " Hz";
        })));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "block", 1 }, "Voicing (Script / Block)", true,
        AudioParameterBoolAttributes().withStringFromValueFunction ([] (bool v, int) { return v ? String ("BLOCK") : String ("SCRIPT"); })));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "on", 1 }, "Footswitch (On)", true));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "syncdiv", 1 }, "Tempo sync (SYNC button)",
                                                          StringArray { "Off", "1/4", "1/8", "1/2" }, 0));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "stereo", 1 }, "Stereo spread", false));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "ticklfo", 1 }, "Speed ring follows LFO", true));
    p.push_back (std::make_unique<AudioParameterInt>  (ParameterID { "midichan", 1 }, "MIDI channel (0 = omni)", 0, 16, 0));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "ccmomentary", 1 }, "Footswitch CC momentary", false));
    return { p.begin(), p.end() };
}

Phase90Processor::Phase90Processor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  AudioChannelSet::stereo(), true)
                        .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Phase90State", createLayout())
{
    presetName = factoryPresets()[0].name;
}

Phase90Processor::~Phase90Processor() { cancelPendingUpdate(); }

bool Phase90Processor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    if (out != AudioChannelSet::mono() && out != AudioChannelSet::stereo()) return false;
    return in == out || (in == AudioChannelSet::mono() && out == AudioChannelSet::stereo());
}

void Phase90Processor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
}

//==============================================================================
String Phase90Processor::syncDivisionName (int i)
{
    static const char* n[] = { "OFF", "1/4", "1/8", "1/2" };
    return n[jlimit (0, numSyncStates - 1, i)];
}
double Phase90Processor::syncDivisionBeats (int i)
{
    static const double b[] = { 0.0, 1.0, 0.5, 2.0 };       // durée d'un cycle, en temps (noires)
    return b[jlimit (0, numSyncStates - 1, i)];
}
void Phase90Processor::cycleSync()
{
    setParam ("syncdiv", (float) ((syncState() + 1) % numSyncStates));
}

//==============================================================================
void Phase90Processor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int inCh = getTotalNumInputChannels(), outCh = getTotalNumOutputChannels();
    for (int c = inCh; c < outCh; ++c) buffer.copyFrom (c, 0, buffer, 0, 0, n);   // mono -> stéréo

    // ---- MIDI : learn, CC, program change
    const int chan = (int) apvts.getRawParameterValue ("midichan")->load();
    const bool momentary = apvts.getRawParameterValue ("ccmomentary")->load() > 0.5f;
    bool triggerUi = false;
    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
        if (chan != 0 && msg.getChannel() != chan) continue;
        if (msg.isController())
        {
            const int cc = msg.getControllerNumber(), v = msg.getControllerValue();
            const int lt = learnTarget.load();
            if (lt >= 0)
            {
                (lt == 0 ? ccFoot : lt == 1 ? ccSpeed : lt == 2 ? ccVoice : ccSync).store (cc);
                lastLearned.store (lt); learnTarget.store (-1); triggerUi = true;
                continue;
            }
            if (cc == ccFoot.load())
            {
                const bool high = v >= 64;
                if (momentary) { if (high && ! footCcWasHigh) { pendingFootToggle.fetch_add (1); triggerUi = true; } }
                else { pendingFootSet.store (high ? 1 : 0); triggerUi = true; }
                footCcWasHigh = high;
            }
            if (cc == ccSpeed.load()) { pendingSpeed.store (v / 127.0f); triggerUi = true; }
            if (cc == ccVoice.load()) { pendingVoice.store (v >= 64 ? 1 : 0); triggerUi = true; }
            if (cc == ccSync.load())                             // chaque appui = état suivant (comme un clic)
            {
                const bool high = v >= 64;
                if (high && ! syncCcWasHigh) { pendingSyncStep.fetch_add (1); triggerUi = true; }
                syncCcWasHigh = high;
            }
        }
        else if (msg.isProgramChange())
        {
            pendingProgram.store (msg.getProgramChangeNumber()); triggerUi = true;
        }
    }
    midi.clear();
    if (triggerUi) triggerAsyncUpdate();

    // ---- tempo et position du DAW
    double bpm = hostBpm.load();
    double ppq = -1.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (pos->getIsPlaying())
                if (auto q = pos->getPpqPosition()) ppq = *q;
        }
    hostBpm.store (bpm);

    phase90::Engine::Settings st;
    st.speed = getSpeed();
    st.block = isBlock();
    st.on = isOn();
    st.stereoSpread = apvts.getRawParameterValue ("stereo")->load() > 0.5f;
    if (const int sd = syncState(); sd > 0)
    {
        const double beats = syncDivisionBeats (sd);
        st.syncHz = jlimit (0.02, 20.0, bpm / 60.0 / beats);
        if (ppq >= 0.0) st.syncPhase = ppq / beats;          // lecture en cours : balayage calé sur la grille
    }

    float* io[2] { buffer.getWritePointer (0), buffer.getWritePointer (jmin (1, outCh - 1)) };
    engine.process (io, jmin (2, outCh), n, st);

    lfoVisual.store ((float) engine.lfoNormalised());
    sweepVisual.store ((float) engine.sweepHz());
    lfoPhaseVisual.store ((float) engine.lfoPhase());
}

//==============================================================================
void Phase90Processor::setParam (const String& id, float plain)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
        p->endChangeGesture();
    }
}

void Phase90Processor::handleAsyncUpdate()
{
    const float r = pendingSpeed.exchange (-1.0f);
    if (r >= 0.0f) { setParam ("speed", r); presetName = {}; }
    const int d = pendingVoice.exchange (-1);
    if (d >= 0) { setParam ("block", (float) d); presetName = {}; }
    const int ss = pendingSyncStep.exchange (0);
    for (int i = 0; i < ss; ++i) cycleSync();
    const int fs = pendingFootSet.exchange (-1);
    if (fs >= 0) setParam ("on", (float) fs);
    const int ft = pendingFootToggle.exchange (0);
    if (ft % 2 == 1) setParam ("on", isOn() ? 0.0f : 1.0f);
    const int pg = pendingProgram.exchange (-1);
    if (pg >= 0 && pg < getNumPrograms()) setCurrentProgram (pg);
}

//==============================================================================
int Phase90Processor::getNumPrograms() { return (int) factoryPresets().size(); }

const String Phase90Processor::getProgramName (int i)
{
    if (isPositiveAndBelow (i, (int) factoryPresets().size())) return factoryPresets()[(size_t) i].name;
    return {};
}

void Phase90Processor::setCurrentProgram (int i)
{
    if (! isPositiveAndBelow (i, (int) factoryPresets().size())) return;
    currentProgram = i;
    const auto& p = factoryPresets()[(size_t) i];
    applyPreset ((float) phase90::Engine::posForHz (p.hz, p.block), p.block, p.sync, p.name);
}

void Phase90Processor::applyPreset (float speed, bool block, int sync, const String& name)
{
    setParam ("speed", speed);
    setParam ("block", block ? 1.0f : 0.0f);
    setParam ("syncdiv", (float) jlimit (0, numSyncStates - 1, sync));
    setParam ("on", 1.0f);
    presetName = name;
}

//==============================================================================
File Phase90Processor::userPresetFile()
{
    return File::getSpecialLocation (File::userApplicationDataDirectory)
             .getChildFile ("Studio Wiga").getChildFile ("Phase 90").getChildFile ("user_presets.xml");
}

static std::unique_ptr<XmlElement> readUserPresets()
{
    auto f = Phase90Processor::userPresetFile();
    if (f.existsAsFile())
        if (auto x = parseXML (f)) if (x->hasTagName ("Phase90UserPresets")) return x;
    return std::make_unique<XmlElement> ("Phase90UserPresets");
}

String Phase90Processor::userPresetName (int slot) const
{
    auto x = readUserPresets();
    for (auto* e : x->getChildIterator())
        if (e->getIntAttribute ("slot") == slot) return e->getStringAttribute ("name", "User " + String (slot));
    return {};
}

bool Phase90Processor::loadUserPreset (int slot)
{
    auto x = readUserPresets();
    for (auto* e : x->getChildIterator())
        if (e->getIntAttribute ("slot") == slot)
        {
            applyPreset ((float) e->getDoubleAttribute ("speed", 0.5), e->getBoolAttribute ("block", true),
                         e->getIntAttribute ("sync", 0), e->getStringAttribute ("name", "User " + String (slot)));
            return true;
        }
    return false;
}

void Phase90Processor::saveUserPreset (int slot, const String& name)
{
    auto x = readUserPresets();
    for (int i = x->getNumChildElements(); --i >= 0;)
        if (x->getChildElement (i)->getIntAttribute ("slot") == slot) x->removeChildElement (x->getChildElement (i), true);
    auto* e = x->createNewChildElement ("Preset");
    e->setAttribute ("slot", slot);
    e->setAttribute ("name", name);
    e->setAttribute ("speed", (double) getSpeed());
    e->setAttribute ("block", isBlock());
    e->setAttribute ("sync", syncState());
    auto f = userPresetFile();
    f.getParentDirectory().createDirectory();
    x->writeTo (f);
    presetName = name;
}

//==============================================================================
void Phase90Processor::getStateInformation (MemoryBlock& dest)
{
    auto state = apvts.copyState();
    std::unique_ptr<XmlElement> xml (state.createXml());
    xml->setAttribute ("ccFoot", ccFoot.load());
    xml->setAttribute ("ccSpeed", ccSpeed.load());
    xml->setAttribute ("ccVoice", ccVoice.load());
    xml->setAttribute ("ccSync", ccSync.load());
    xml->setAttribute ("uiHeight", uiHeight.load());
    xml->setAttribute ("presetName", presetName);
    xml->setAttribute ("program", currentProgram);
    copyXmlToBinary (*xml, dest);
}

void Phase90Processor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            ccFoot = xml->getIntAttribute ("ccFoot", 80);
            ccSpeed = xml->getIntAttribute ("ccSpeed", 81);
            ccVoice = xml->getIntAttribute ("ccVoice", 82);
            ccSync = xml->getIntAttribute ("ccSync", 83);
            uiHeight = jlimit (p9ui::heightDefault, p9ui::heightMax, xml->getIntAttribute ("uiHeight", p9ui::heightDefault));
            presetName = xml->getStringAttribute ("presetName");
            currentProgram = xml->getIntAttribute ("program", 0);
            apvts.replaceState (ValueTree::fromXml (*xml));
        }
    }
}

AudioProcessorEditor* Phase90Processor::createEditor() { return new Phase90Editor (*this); }

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Phase90Processor(); }

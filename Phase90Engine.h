#pragma once
// ============================================================================
//  Phase 90 (Studio Wiga) — moteur DSP (C++17 pur, sans JUCE : testé seul par dsp_test.cpp)
//
//  Modèle "composant par composant" du MXR Phase 90 :
//
//   ENTRÉE : buffer à gain unité, polarisé sur la référence 5.1 V (Zener D2) ;
//            liaison C5 0.01 µF / 470k -> passe-haut ≈ 34 Hz.
//   DÉPHASAGE : 4 cellules passe-tout identiques (ampli-op inverseur 10k/10k,
//            réseau RC sur l'entrée +) : C = 47 nF, R = 24k en parallèle avec la
//            résistance drain-source d'un JFET 2N5952 (résistance variable).
//            H(s) = -(1 - sRC)/(1 + sRC) : 0 à -180° par cellule, 720° au total
//            -> 2 encoches (déphasages de 180° et 540°) à 0.414·fc et 2.414·fc.
//            JFET coupé (VGS < VP) : R = 24k -> fc = 141 Hz, encoches 58 Hz / 340 Hz.
//            JFET conducteur : rDS ≈ Ron / (1 - VGS/VP) -> fc monte à ~5.5 kHz.
//   LFO : trigger de Schmitt + intégrateur, C7 = 0.01 µF, potard SPEED 470k
//            (courbe log) : triangle ≈ 0.1 Hz … 4.5 Hz (Script) / 5.2 Hz (Block).
//            Le triangle est appliqué aux grilles (source à 5.1 V) ; comme rDS est
//            hyperbolique en VGS, la fréquence de coupure suit une courbe
//            asymétrique : long passage dans les aigus, plongée rapide dans les graves
//            (le "swoosh" typique du Phase 90).
//   R28 (BLOCK, 1977+) : 24k de la sortie de la 4e cellule vers l'entrée de la 2e
//            (gain 10k / 24k) -> réinjection : résonance, bosse de ~ +4 dB dans les
//            médiums entre les encoches, balayage plus "agressif". Absente sur le SCRIPT (1974).
//   MIX : sec + déphasé à parts égales (R8 = R16 = 150k), sortie PNP, passe-haut 22 Hz.
// ============================================================================
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace phase90
{
constexpr double kPi = 3.14159265358979323846;

//------------------------------------------------------------------------------
struct OnePoleHP          // passe-haut du 1er ordre (liaisons capacitives)
{
    double a = 0, x1 = 0, y1 = 0;
    void set (double fs, double fc) { const double rc = 1.0 / (2 * kPi * fc); a = rc / (rc + 1.0 / fs); }
    inline double process (double x) { const double y = a * (y1 + x - x1); x1 = x; y1 = y; return y; }
    void reset() { x1 = y1 = 0; }
};

//------------------------------------------------------------------------------
// JFET 2N5952 en résistance variable (région ohmique), coupure adoucie près de VP
struct Jfet
{
    double ron = 400.0;     // rDS à VGS = 0
    double vp  = -2.4;      // tension de pincement
    inline double rds (double vgs) const
    {
        const double u = 1.0 - vgs / vp;                 // 1 à VGS = 0, 0 au pincement
        constexpr double k = 28.0;                       // arrondi du coude de pincement
        const double us = u > 0.4 ? u : std::log1p (std::exp (k * u)) / k;
        return ron / std::max (us, 1.0e-9);
    }
};

//------------------------------------------------------------------------------
// Cellule passe-tout (TPT / transformée bilinéaire avec pré-distorsion, R variable à chaque échantillon)
//   y = x - 2·LP(x - inject)   (inject = a·f : réinjection éventuelle de R28 vers le nœud +)
struct AllpassStage
{
    double s = 0;
    inline double process (double x, double g, double inject = 0.0)
    {
        const double G = g / (1.0 + g);
        const double in = x - inject;
        const double v = (in - s) * G;
        const double lp = v + s;
        s = lp + v;
        return x - 2.0 * lp;        // inverseur : -1 en continu, +1 en haut du spectre
    }
    void reset() { s = 0; }
};

//------------------------------------------------------------------------------
// Potard SPEED 470k log + résistance série -> fréquence du LFO
inline double speedPotResistance (double pos01)
{
    pos01 = std::clamp (pos01, 0.0, 1.0);
    // piste "audio" (log 10 %) parcourue à l'envers : sens horaire = plus rapide
    const double p = 1.0 - pos01;
    return 470.0e3 * (std::pow (10.0, 2.0 * p) - 1.0) / 99.0;
}

class Engine
{
public:
    struct Settings
    {
        double speed = 0.5;         // position du potard 0 … 1
        bool block = true;          // true = BLOCK (R28 présente), false = SCRIPT '74
        bool on = true;             // footswitch
        bool stereoSpread = false;  // LFO du canal droit en opposition (option, l'original est mono)
        double syncHz = 0.0;        // > 0 : fréquence du LFO imposée (synchro tempo)
        double syncPhase = -1.0;    // >= 0 : phase du LFO (0 … 1) calée sur la position du DAW en début de bloc
        double lfoHold = -1.0;      // >= 0 : LFO figé à cette valeur (tests / mesures)
    };

    static constexpr double C = 47.0e-9, Rpar = 24.0e3, Rf = 24.0e3;
    static constexpr double VgsTop = -0.80, VgsBottom = -2.55;     // excursion du triangle sur les grilles (trimmer réglé)
    static constexpr double kSeriesScript = 10.9e3, kSeriesBlock = 9.3e3;
    static constexpr double kLfoK = 0.1 * (470.0e3 + 10.9e3);      // Hz·ohm (C7 = 0.01 µF + seuils du trigger)

    static double speedHz (double pos, bool block)
    {
        return kLfoK / ((block ? kSeriesBlock : kSeriesScript) + speedPotResistance (pos));
    }
    // inverse (pour les presets exprimés en Hz)
    static double posForHz (double hz, bool block)
    {
        const double R = kLfoK / hz - (block ? kSeriesBlock : kSeriesScript);
        const double v = std::clamp (R / 470.0e3 * 99.0 + 1.0, 1.0, 100.0);
        return std::clamp (1.0 - std::log10 (v) / 2.0, 0.0, 1.0);
    }

    void prepare (double sampleRate, int /*maxBlock*/)
    {
        fs = sampleRate;
        for (auto& ch : chans)
        {
            ch.hpIn.set (fs, 34.0); ch.hpOut.set (fs, 22.0);
            ch.hpIn.reset(); ch.hpOut.reset();
            for (auto& st : ch.ap) st.reset();
            ch.fb = 0;
        }
        // 4 JFET non appariés (dispersion de fabrication, comme une vraie pédale)
        const double ronK[4] { 1.00, 1.06, 0.95, 1.03 };
        const double vpK[4]  { -2.40, -2.33, -2.47, -2.42 };
        for (int i = 0; i < 4; ++i) { fets[i].ron = 400.0 * ronK[i]; fets[i].vp = vpK[i]; }
        mix = 1.0; phase = 0.25; hz = speedHz (0.5, true); fbAmt = 1.0;
    }

    // fréquence de coupure d'une cellule pour une tension de grille donnée
    double cutoffHz (int stage, double vgs) const
    {
        const double r = fets[stage].rds (vgs);
        const double R = r * Rpar / (r + Rpar);
        return 1.0 / (2 * kPi * R * C);
    }
    // triangle (0 … 1) -> VGS
    static double vgsFromLfo (double t01) { return VgsTop + (VgsBottom - VgsTop) * t01; }

    void process (float* const* io, int nCh, int n, const Settings& st)
    {
        const double targetHz = st.syncHz > 0.0 ? st.syncHz : speedHz (st.speed, st.block);
        const double hCoef = std::exp (-1.0 / (0.030 * fs));          // potard : pas de saut de vitesse
        const double fbTarget = st.block ? 1.0 : 0.0;
        const double fbCoef = std::exp (-1.0 / (0.020 * fs));
        const double mixTarget = st.on ? 1.0 : 0.0;
        const double mixStep = 1.0 / (0.008 * fs);
        const double gMaxArg = kPi * 0.49;                             // garde-fou sous Nyquist
        if (st.syncHz > 0.0 && st.syncPhase >= 0.0)
        {
            phase = st.syncPhase - std::floor (st.syncPhase);          // le balayage suit la grille du DAW
            hz = targetHz;
        }

        for (int i = 0; i < n; ++i)
        {
            hz = targetHz + (hz - targetHz) * hCoef;
            fbAmt = fbTarget + (fbAmt - fbTarget) * fbCoef;
            drift += (rnd() * 2.0 - 1.0) * 0.0004; drift *= 0.9995;
            // triangle légèrement asymétrique (seuils du trigger de Schmitt)
            phase += hz * (1.0 + drift * 0.02) / fs;
            if (phase >= 1.0) phase -= 1.0;
            const double triL = st.lfoHold >= 0.0 ? st.lfoHold : tri (phase);
            const double triR = st.lfoHold >= 0.0 ? st.lfoHold : (st.stereoSpread ? tri (std::fmod (phase + 0.5, 1.0)) : triL);
            lfoVis = triL;

            if (mix < mixTarget) mix = std::min (mixTarget, mix + mixStep);
            else if (mix > mixTarget) mix = std::max (mixTarget, mix - mixStep);

            for (int c = 0; c < nCh && c < 2; ++c)
            {
                auto& ch = chans[c];
                const double vgs = vgsFromLfo (c == 0 ? triL : triR);
                double g[4];
                for (int k = 0; k < 4; ++k)
                {
                    const double r = fets[k].rds (vgs);
                    const double R = r * Rpar / (r + Rpar);
                    const double fc = 1.0 / (2 * kPi * R * C);
                    g[k] = std::tan (std::min (kPi * fc / fs, gMaxArg));
                }
                if (c == 0) fcVis = 1.0 / (2 * kPi * C * (fets[0].rds (vgs) * Rpar / (fets[0].rds (vgs) + Rpar)));

                const double x = io[c][i];
                const double dry = ch.hpIn.process (x);
                double y = ch.ap[0].process (dry, g[0]);
                y = opamp (y);
                // R28 (24k) : la sortie revient sur l'entrée inverseuse de la 2e cellule (10k / 24k)
                y = ch.ap[1].process (y, g[1]) + kFbSign * fbAmt * (10.0e3 / Rf) * ch.fb;
                y = opamp (y);
                y = opamp (ch.ap[2].process (y, g[2]));
                y = opamp (ch.ap[3].process (y, g[3]));
                const double m = 0.5 * (dry + y);                         // R8 = R16 : 50 / 50
                ch.fb = y;                                               // R28 prise en sortie de la 4e cellule
                double out = ch.hpOut.process (m) * (st.block ? kGainBlock : kGainScript);
                io[c][i] = (float) (x + (out - x) * mix);
            }
        }
    }

    // infos pour l'interface
    double lfoNormalised() const { return lfoVis; }          // 0 = haut du balayage, 1 = bas
    double sweepHz() const { return fcVis; }
    double currentSpeedHz() const { return hz; }
    double lfoPhase() const { return phase; }                 // 0 … 1, 0 = début d'un cycle (temps du DAW en synchro)

    static constexpr double kGainScript = 1.38, kGainBlock = 1.40;
    static constexpr double kFbSign = -1.0;

private:
    struct Channel
    {
        OnePoleHP hpIn, hpOut;
        AllpassStage ap[4];
        double fb = 0;
    };

    static inline double tri (double ph)       // 0 … 1, montée 48 % / descente 52 %
    {
        constexpr double up = 0.48;
        return ph < up ? ph / up : 1.0 - (ph - up) / (1.0 - up);
    }
    static inline double opamp (double x)     // TL062 : saturation douce bien au-dessus du niveau guitare
    {
        constexpr double t = 2.2;
        const double a = std::abs (x);
        if (a <= t) return x;
        return std::copysign (t + 0.8 * std::tanh ((a - t) / 0.8), x);
    }
    inline double rnd() { seed = seed * 1664525u + 1013904223u; return (double) (seed >> 8) / 16777216.0; }

    double fs = 48000.0;
    Channel chans[2];
    Jfet fets[4];
    double phase = 0.25, hz = 1.0, mix = 1.0, fbAmt = 1.0, drift = 0;
    double lfoVis = 0, fcVis = 1000;
    uint32_t seed = 0x2468ACEu;
};
} // namespace phase90

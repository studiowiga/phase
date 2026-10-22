// Test du moteur Phase 90 (compilé et lancé par GitHub Actions sur Linux)
//   g++ -O2 -std=c++17 -I. dsp_test.cpp -o dsp_test && ./dsp_test
#include "Phase90Engine.h"
#include <cstdio>
#include <cmath>
#include <vector>

using namespace phase90;
static int fails = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf ("  ok   "); } else { std::printf ("  FAIL "); ++fails; } std::printf (__VA_ARGS__); std::printf ("\n"); } while (0)

// fréquence du LFO mesurée en comptant les sommets du triangle
static double measureLfoHz (double pos, bool block, double fs, double syncHz = 0)
{
    Engine e; e.prepare (fs, 1);
    Engine::Settings st; st.speed = pos; st.block = block; st.syncHz = syncHz;
    float l = 0, r = 0; float* one[2] { &l, &r };
    const double seconds = pos < 0.2 ? 45.0 : 20.0;
    double last = e.lfoNormalised(); bool rising = false; int peaks = 0; double t0 = -1, t1 = 0;
    for (long i = 0; i < (long) (fs * seconds); ++i)
    {
        l = r = 0; e.process (one, 2, 1, st);
        const double v = e.lfoNormalised(), t = i / fs;
        if (rising && v < last && t > 1.0) { if (t0 < 0) t0 = t; t1 = t; ++peaks; }
        rising = v > last; last = v;
    }
    return peaks > 1 ? (peaks - 1) / (t1 - t0) : 0.0;
}

// gain (dB) d'un sinus de fréquence f, LFO figé
static double gainAt (double f, double hold, bool block, double fs = 48000)
{
    Engine e; e.prepare (fs, 256);
    Engine::Settings st; st.lfoHold = hold; st.block = block;
    std::vector<float> l (256), r (256);
    double ph = 0, si = 0, so = 0;
    const int blocks = (int) (fs * 1.5 / 256);
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < 256; ++i) { l[(size_t) i] = r[(size_t) i] = (float) (0.1 * std::sin (ph)); ph += 2 * kPi * f / fs; }
        std::vector<float> in = l;
        float* io[2] { l.data(), r.data() };
        e.process (io, 2, 256, st);
        if (b > blocks / 2) for (int i = 0; i < 256; ++i) { si += in[(size_t) i] * in[(size_t) i]; so += l[(size_t) i] * l[(size_t) i]; }
    }
    return 10 * std::log10 (so / si);
}

static double findNotch (double lo, double hi, double hold, bool block)
{
    double best = 1e9, bf = lo;
    for (double f = lo; f < hi; f *= 1.02) { const double g = gainAt (f, hold, block); if (g < best) { best = g; bf = f; } }
    return bf;
}

int main()
{
    std::printf ("Phase 90 - test du moteur\n\n[LFO / SPEED]\n");
    const double fMin = measureLfoHz (0.0, false, 48000), fMid = measureLfoHz (0.5, false, 48000), fMax = measureLfoHz (1.0, false, 48000);
    std::printf ("  SCRIPT : min %.3f Hz | milieu %.3f Hz | max %.3f Hz  (formule %.3f / %.3f / %.3f)\n",
                 fMin, fMid, fMax, Engine::speedHz (0, false), Engine::speedHz (0.5, false), Engine::speedHz (1, false));
    CHECK (fMin > 0.08 && fMin < 0.12, "SPEED minimum ~0.1 Hz");
    CHECK (fMax > 3.8 && fMax < 5.0, "SPEED maximum ~4.4 Hz (Script)");
    CHECK (std::abs (fMid - Engine::speedHz (0.5, false)) / fMid < 0.03, "mesure = formule du circuit");
    const double fMaxB = measureLfoHz (1.0, true, 48000);
    CHECK (fMaxB > fMax * 1.08, "BLOCK un peu plus rapide en haut de course (%.2f Hz)", fMaxB);
    const double f96 = measureLfoHz (0.5, false, 96000);
    CHECK (std::abs (f96 - fMid) / fMid < 0.02, "LFO indépendant de la fréquence d'échantillonnage (96 kHz : %.3f Hz)", f96);
    const double fSync = measureLfoHz (0.5, false, 48000, 2.0);
    CHECK (std::abs (fSync - 2.0) < 0.04, "synchro tempo : 2.000 Hz demandé, %.3f Hz mesuré", fSync);
    CHECK (std::abs (Engine::speedHz (Engine::posForHz (1.0, true), true) - 1.0) < 1e-6, "conversion Hz -> position du potard");

    std::printf ("\n[Encoches (SCRIPT, LFO figé)]\n");
    {
        Engine e; e.prepare (48000, 1);
        for (double hold : { 0.25, 0.75 })
        {
            const double fc = e.cutoffHz (0, Engine::vgsFromLfo (hold));
            const double n1 = findNotch (fc * 0.2, fc * 0.8, hold, false);
            const double depth = gainAt (n1, hold, false), flat = gainAt (fc * 8, hold, false);
            std::printf ("  LFO %.2f : fc %.0f Hz -> 1re encoche %.0f Hz (théorie %.0f), profondeur %.1f dB\n", hold, fc, n1, fc * 0.414, depth);
            CHECK (std::abs (n1 / (fc * 0.414) - 1.0) < 0.12, "1re encoche à 0.414·fc (4 cellules)");
            CHECK (depth < -18.0, "encoche profonde (%.1f dB)", depth);
            CHECK (std::abs (flat) < 2.5, "hors encoches ≈ 0 dB (%.2f dB)", flat);
        }
        const double top = e.cutoffHz (0, Engine::vgsFromLfo (0)) * 0.414, bot = e.cutoffHz (0, Engine::vgsFromLfo (1)) * 0.414;
        std::printf ("  balayage de la 1re encoche : %.0f Hz -> %.0f Hz\n", top, bot);
        CHECK (top > 1500 && top < 3500 && bot > 50 && bot < 120, "balayage ≈ 2.4 kHz … 80 Hz");
    }

    std::printf ("\n[BLOCK : R28]\n");
    {
        double maxS = -99, maxB = -99;
        for (double f = 100; f < 8000; f *= 1.06) { maxS = std::max (maxS, gainAt (f, 0.75, false)); maxB = std::max (maxB, gainAt (f, 0.75, true)); }
        std::printf ("  pic de réponse : SCRIPT %.2f dB | BLOCK %.2f dB\n", maxS, maxB);
        CHECK (maxB > maxS + 1.0, "BLOCK : résonance plus marquée (bosse de médiums)");
    }

    std::printf ("\n[Audio]\n");
    for (bool block : { false, true })
    {
        const double fs = 48000;
        Engine e; e.prepare (fs, 512);
        Engine::Settings st; st.block = block; st.speed = 0.5;
        std::vector<float> l (512), r (512);
        double sumIn = 0, sumOut = 0, peak = 0; bool finite = true;
        uint32_t sd = 1; double lp = 0;
        for (int blk = 0; blk < 600; ++blk)
        {
            for (int i = 0; i < 512; ++i)
            {
                sd = sd * 1664525u + 1013904223u; const double wn = ((sd >> 8) / 16777216.0 - 0.5);
                lp += (wn - lp) * 0.08;  // bruit "guitare" (grave)
                const float s = (float) (lp * 2.0); l[(size_t) i] = r[(size_t) i] = s; sumIn += s * s;
            }
            float* io[2] { l.data(), r.data() };
            e.process (io, 2, 512, st);
            for (int i = 0; i < 512; ++i) { finite &= std::isfinite (l[(size_t) i]); sumOut += l[(size_t) i] * l[(size_t) i]; peak = std::max (peak, (double) std::abs (l[(size_t) i])); }
        }
        const double g = 10 * std::log10 (sumOut / sumIn);
        CHECK (finite, "%s : aucun NaN / inf", block ? "BLOCK" : "SCRIPT");
        CHECK (g > -2.5 && g < 2.5, "%s : niveau effet ≈ niveau bypass (%.2f dB)", block ? "BLOCK" : "SCRIPT", g);
        (void) peak;

        st.on = false;
        double err = 0, ph = 0;
        for (int blk = 0; blk < 20; ++blk)
        {
            for (int i = 0; i < 512; ++i) { const float s = (float) (0.3 * std::sin (ph)); ph += 2 * kPi * 330.0 / fs; l[(size_t) i] = r[(size_t) i] = s; }
            std::vector<float> ref = l;
            float* io[2] { l.data(), r.data() };
            e.process (io, 2, 512, st);
            if (blk > 2) for (int i = 0; i < 512; ++i) err = std::max (err, (double) std::abs (l[(size_t) i] - ref[(size_t) i]));
        }
        CHECK (err < 1e-6, "bypass true-bypass transparent (écart %.2e)", err);
    }
    for (double fs : { 44100.0, 88200.0, 192000.0 })
    {
        Engine e; e.prepare (fs, 2048); Engine::Settings st; st.stereoSpread = true; st.speed = 1.0;
        std::vector<float> l (2048), r (2048); bool ok = true; double ph = 0;
        for (int k = 0; k < 80; ++k)
        {
            for (int i = 0; i < 2048; ++i) { l[(size_t) i] = (float) (0.9 * std::sin (ph)); r[(size_t) i] = -l[(size_t) i]; ph += 2 * kPi * 110.0 / fs; }
            float* io[2] { l.data(), r.data() }; e.process (io, 2, 2048, st);
            for (float v : l) ok &= std::isfinite (v) && std::abs (v) < 3;
        }
        CHECK (ok, "stable à %.0f Hz (BLOCK, stéréo, niveau fort)", fs);
    }

    std::printf ("\n%s (%d échec(s))\n", fails == 0 ? "TOUT EST OK" : "ÉCHEC", fails);
    return fails == 0 ? 0 : 1;
}

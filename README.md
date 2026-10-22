# Phase 90 — Studio Wiga

Phaser analogique **VST3** (Windows 64 bits + macOS universel Apple Silicon/Intel) au format pédale simple,
modélisé composant par composant sur le **MXR Phase 90** : 4 cellules passe-tout à JFET 2N5952,
LFO triangle (trigger de Schmitt + intégrateur), mélange sec/traité 50/50, résistance de réinjection R28
(versions *Block*). Même taille de pédale et de fenêtre que **Small Tone** / **Holyverb**.

## Compiler avec GitHub
1. Mets **tous les fichiers à la racine du dépôt** (avec le dossier `.github/workflows`).
2. Un push lance l'onglet **Actions** → workflow *Build Phase 90 VST3 (Windows + macOS)*.
3. À la fin, télécharge les artefacts :
   - `Phase90-VST3-Windows` → copie `Phase 90.vst3` dans `C:\Program Files\Common Files\VST3\`
   - `Phase90-VST3-macOS` → copie `Phase 90.vst3` dans `~/Library/Audio/Plug-Ins/VST3/` puis lance une fois
     `xattr -cr ~/Library/Audio/Plug-Ins/VST3/"Phase 90.vst3"`
4. Dans Reaper : re-scanne les plug-ins. Le plugin apparaît comme **Phase 90 (Studio Wiga)**.

Le job *Test du moteur phaser* compile `dsp_test.cpp` (plage du SPEED, position et profondeur des encoches,
effet de R28, niveaux, bypass, stabilité de 44,1 à 192 kHz). S'il échoue, le build est signalé en rouge.

## Commandes
| Commande | Rôle |
|---|---|
| **SPEED** | vitesse du LFO, ≈ 0,10 Hz → 4,4 Hz (Script) / 5,2 Hz (Block), potard 470k log comme l'original (midi ≈ 0,9 Hz). Tirer haut/bas ou gauche/droite, molette, **Maj** = réglage fin, double-clic = midi |
| Cartouche **PHASE** | clic = bascule **SCRIPT '74** (sans R28, doux) / **BLOCK** (R28, plus de médiums, le M101 actuel) |
| Footswitch | effet / bypass (true bypass) — l'anneau du footswitch et les graduations orange s'allument |
| Ligne d'état (sous « PHASE ») | preset, vitesse en Hz, version, ou BYPASS |

Les graduations orange « respirent » avec le LFO (plus lumineuses quand le balayage est en haut) — désactivable.

## Presets
Menu des presets de la fenêtre du DAW (programmes VST3) : Classic Block, Script '74, Eruption, Brown Sound,
Slow Jet, Funk Rhythm, Fast Warble, Rotary Fast, Bass Swirl. Clic droit → 9 presets utilisateur
communs à toutes les instances (`%APPDATA%\Studio Wiga\Phase 90\user_presets.xml`).

## Clic droit (n'importe où)
- **Voicing** : Script '74 / Block '77.
- **Window size** : défaut (493 px de haut = taille minimale, comme Small Tone / Holyverb), 60 %, 75 %, 100 % — ou tirer le coin.
- **Options** : SPEED synchronisé au tempo du DAW (4 mesures → 1/16), stéréo élargie (LFO du canal droit
  en opposition ; l'original est mono), graduations qui pulsent avec le LFO.
- **MIDI** : canal, *Learn* — par défaut Footswitch = CC 80, Speed = CC 81, Script/Block = CC 82 ;
  pédale à verrouillage ou momentanée ; Program Change n = preset n+1.

## Fonctionnement (résumé)
- **Cellules** : ampli-op inverseur 10k/10k, 47 nF + (24k // rDS du JFET) sur l'entrée + :
  H(s) = −(1 − sRC)/(1 + sRC). 4 cellules = 720° → 2 encoches à 0,414·fc et 2,414·fc.
- **JFET** : rDS ≈ Ron / (1 − VGS/VP) (Ron ≈ 400 Ω, VP ≈ −2,4 V, 4 JFET légèrement différents comme sur une vraie
  pédale). JFET coupé : fc = 141 Hz (encoches 58 / 340 Hz, valeurs ElectroSmash).
- **Balayage** : 1re encoche ≈ 2,4 kHz → 80 Hz. rDS étant hyperbolique, la descente dans les graves est rapide
  et le passage dans les aigus plus long : le « swoosh » asymétrique du Phase 90.
- **R28 (Block)** : 24k de la 4e cellule vers l'entrée de la 2e → bosse de ~ +4 dB dans les médiums, encoches moins
  creuses, son plus « agressif ». Le Script n'a pas cette résistance.
- **Entrée / sortie** : passe-haut 34 Hz (0,01 µF / 470k) et 22 Hz, saturation douce des TL062 bien au-dessus du niveau guitare.

Police Liberation Sans (licence SIL OFL, `OFL-Liberation.txt`).

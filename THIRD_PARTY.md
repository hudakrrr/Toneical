# Third-party components

Toneical itself is licensed under the **GNU AGPL v3.0 or later** (see `LICENSE`).
It builds on and ships the following, each under its own licence:

| Component | Where | Licence |
|---|---|---|
| [JUCE](https://juce.com) 9 framework | downloaded at build time | AGPLv3 (or commercial JUCE licence) |
| [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore) | downloaded at build time | MIT |
| [Eigen](https://eigen.tuxfamily.org) | downloaded at build time | MPL 2.0 |
| nlohmann/json (vendored by NeuralAmpModelerCore) | downloaded at build time | MIT |
| Amp captures from the [NAM community collection](https://github.com/pelennor2170/NAM_models) | `assets/nam/` | GPL v3 - authors listed in `assets/nam/README.txt` |
| Pedal captures from the NAM community collection and the [GuitarML ToneLibrary](https://github.com/GuitarML/ToneLibrary) | `assets/pedals/` | GPL v3 - authors listed in `assets/pedals/README.txt` |
| Cabinet impulse responses, BestPlugins Mega Pack 2 by David Fau Casquel (via Guitarix) | `assets/irs/` | GPL v2 or later - see `assets/irs/README.txt` |
| [Arizonia](https://fonts.google.com/specimen/Arizonia) font (logo) | `assets/fonts/` | SIL Open Font License 1.1 |
| Freeverb algorithm (Jezar at Dreampoint) | `Source/modules/Effects.h` | public domain |
| Tone stack model after Yeh & Smith, DAFx 2006 | `Source/dsp/DspHelpers.h` | own implementation of a published method |

Product and brand names (Marshall, Fender, Vox, Peavey, Boss, Ibanez, MXR, Klon, ...) appear only to
say which real devices were captured or what a design was inspired by. They belong to their owners,
who are not affiliated with Toneical. Toneical's own pedal and preset names are deliberately different.

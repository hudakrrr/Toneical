// Toneical - guitar rig for Linux
// Copyright (C) 2026 hudakrrr and contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include "Module.h"

namespace pg
{
//==============================================================================
// Drive, distortion and fuzz pedals made from neural captures of the real pedals:
//  * NAM captures (WaveNet, 48 kHz) from the community collection pelennor2170/NAM_models
//  * GuitarML Proteus captures (LSTM, 44.1 kHz - run through a sample-rate bridge), some
//    of which learned the pedal's own Drive knob, so that knob is the real thing
// Both collections are GPL v3; see assets/pedals/README.txt for who captured what.
//
// A capture is one setting of the real pedal. Where the real knob wasn't captured, the
// Drive knob changes how hard the capture is hit (like a hotter or weaker pickup, with
// level compensation), and where several settings were captured it also switches between
// them. Tone knobs are a filter after the capture.
std::vector<ModulePtr (*)()> capturedPedalFactories();

} // namespace pg

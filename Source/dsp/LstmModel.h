// Toneical - guitar rig for Linux
// Copyright (C) 2026 hudakrrr and contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include "dsp/DspHelpers.h"

namespace pg
{
//==============================================================================
// The neural network inside GuitarML Proteus / NeuralPi pedal captures:
// a single-layer LSTM (PyTorch layout, gates i, f, g, o) + one dense output, with the
// input added back on ("skip"). Conditioned captures take a second input: the
// pedal's knob (0..1), so the Drive knob of the real pedal is part of the model.
class LstmModel
{
public:
    // weights straight from the .json "state_dict"
    bool load (int inputSize, int hidden, bool skip,
               const std::vector<std::vector<float>>& wih, const std::vector<std::vector<float>>& whh,
               const std::vector<float>& bih, const std::vector<float>& bhh,
               const std::vector<float>& linW, float linB)
    {
        if (hidden <= 0 || hidden > maxHidden || inputSize < 1 || inputSize > 2) return false;
        H = hidden; In = inputSize; useSkip = skip;
        const int G = 4 * H;
        if ((int) wih.size() != G || (int) whh.size() != G || (int) bih.size() != G || (int) bhh.size() != G || (int) linW.size() != H)
            return false;
        Wx.assign ((size_t) G * 2, 0.0f);
        Wh.assign ((size_t) G * (size_t) H, 0.0f);   // stored transposed: [j][g], so the hot loop runs over g (vectorises)
        b.assign ((size_t) G, 0.0f);
        for (int g = 0; g < G; ++g)
        {
            for (int i = 0; i < In; ++i) Wx[(size_t) g * 2 + (size_t) i] = wih[(size_t) g][(size_t) i];
            for (int j = 0; j < H; ++j)  Wh[(size_t) (j * G + g)] = whh[(size_t) g][(size_t) j];
            b[(size_t) g] = bih[(size_t) g] + bhh[(size_t) g];
        }
        Wo = linW; bo = linB;
        reset();
        return true;
    }

    int inputSize() const { return In; }
    void reset() { std::fill (h, h + maxHidden, 0.0f); std::fill (c, c + maxHidden, 0.0f); }

    void process (float* x, int n, float cond) noexcept
    {
        const int G = 4 * H;
        float gates[4 * maxHidden];
        for (int s = 0; s < n; ++s)
        {
            const float in = x[s];
            for (int g = 0; g < G; ++g)
                gates[g] = b[(size_t) g] + Wx[(size_t) g * 2] * in + Wx[(size_t) g * 2 + 1] * cond;
            for (int j = 0; j < H; ++j)
            {
                const float hj = h[j];
                const float* w = Wh.data() + (size_t) j * (size_t) G;
                for (int g = 0; g < G; ++g) gates[g] += w[g] * hj;
            }
            float y = bo;
            for (int j = 0; j < H; ++j)
            {
                const float ig = sigm (gates[j]), fg = sigm (gates[H + j]);
                const float gg = fastTanh (gates[2 * H + j]), og = sigm (gates[3 * H + j]);
                c[j] = fg * c[j] + ig * gg;
                h[j] = og * fastTanh (c[j]);
                y += Wo[(size_t) j] * h[j];
            }
            x[s] = useSkip ? y + in : y;
        }
    }

private:
    static constexpr int maxHidden = 64;
    static inline float sigm (float v) noexcept { return 0.5f * (fastTanh (0.5f * v) + 1.0f); }

    int H = 0, In = 1;
    bool useSkip = true;
    std::vector<float> Wx, Wh, b, Wo;
    float bo = 0.0f;
    float h[maxHidden] {}, c[maxHidden] {};
};

} // namespace pg

/*
 * GatewayPi appliance layer for NAMix Linux
 * Copyright (C) 2026
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace gatewaypi {

// Pitch detector using the McLeod Pitch Method (normalised square difference
// + parabolic interpolation) over a 4x-decimated input stream.  Pure C++,
// no JUCE dependencies — unit-testable on any host.  Runs entirely on the
// message thread from samples tapped off the audio thread via a lock-free
// FIFO.
class TunerEngine {
public:
  struct Reading {
    bool valid = false;
    float frequency = 0.0f; // Hz
    int midiNote = 0;       // nearest MIDI note
    float cents = 0.0f;     // -50..+50 relative to midiNote
  };

  explicit TunerEngine(double referenceA = 440.0) : mReferenceA(referenceA) {}

  // Feed native-rate samples; keeps an internal decimated rolling window.
  void push(const float *samples, int n, double sampleRate) {
    mDecimatedRate = sampleRate / kDecimation;
    for (int i = 0; i < n; ++i) {
      mAccum += samples[i];
      if (++mAccumCount == kDecimation) {
        // Boxcar average as a crude anti-alias lowpass; plenty for pitch
        // work (we only care about content below ~1.5 kHz).
        mWindow[(size_t)mWritePos] = mAccum / (float)kDecimation;
        mWritePos = (mWritePos + 1) % kWindow;
        mFilled = std::min(mFilled + 1, kWindow);
        mAccum = 0.0f;
        mAccumCount = 0;
      }
    }
  }

  // Analyse the current window.  Cheap enough for a 20 Hz UI timer.
  Reading detect() {
    Reading out;
    if (mFilled < kWindow)
      return out;

    // Unroll the ring buffer into analysis order.
    std::vector<float> x((size_t)kWindow);
    for (int i = 0; i < kWindow; ++i)
      x[(size_t)i] = mWindow[(size_t)((mWritePos + i) % kWindow)];

    // Level gate: don't chase noise between notes.
    double rms = 0.0;
    for (float v : x)
      rms += (double)v * v;
    rms = std::sqrt(rms / kWindow);
    if (rms < 1.0e-3) { // ~-60 dBFS
      mSmoothedFreq = 0.0f;
      return out;
    }

    // NSDF (normalised square difference, the heart of MPM):
    //   nsdf[tau] = 2 * sum(x[i] x[i+tau]) / sum(x[i]^2 + x[i+tau]^2)
    const int maxTau = kWindow / 2; // 600 @12 kHz -> 20 Hz floor
    const int minTau = (int)(mDecimatedRate / 800.0); // 800 Hz ceiling
    for (int tau = minTau; tau < maxTau; ++tau) {
      double acf = 0.0, m = 0.0;
      for (int i = 0; i + tau < kWindow; ++i) {
        const double a = x[(size_t)i], b = x[(size_t)(i + tau)];
        acf += a * b;
        m += a * a + b * b;
      }
      mNsdf[(size_t)tau] = m > 0.0 ? (float)(2.0 * acf / m) : 0.0f;
    }

    // Peak picking: collect local maxima between zero crossings, then take
    // the first peak above 0.85 * the global peak (McLeod's rule — prefers
    // the fundamental over stronger low-octave candidates).
    struct Peak {
      int tau;
      float value;
    };
    std::vector<Peak> peaks;
    float globalMax = 0.0f;
    int tau = minTau;
    while (tau < maxTau - 1 && mNsdf[(size_t)tau] > 0.0f)
      ++tau; // skip the initial lobe around tau=0
    while (tau < maxTau - 1) {
      while (tau < maxTau - 1 && mNsdf[(size_t)tau] <= 0.0f)
        ++tau;
      Peak best{0, 0.0f};
      while (tau < maxTau - 1 && mNsdf[(size_t)tau] > 0.0f) {
        if (mNsdf[(size_t)tau] > best.value)
          best = {tau, mNsdf[(size_t)tau]};
        ++tau;
      }
      if (best.tau > 0) {
        peaks.push_back(best);
        globalMax = std::max(globalMax, best.value);
      }
    }
    if (peaks.empty() || globalMax < 0.5f)
      return out;

    int chosen = 0;
    for (const auto &p : peaks)
      if (p.value >= 0.85f * globalMax) {
        chosen = p.tau;
        break;
      }

    // Parabolic interpolation around the chosen lag for sub-sample precision.
    double t = chosen;
    if (chosen > minTau && chosen < maxTau - 1) {
      const double a = mNsdf[(size_t)(chosen - 1)], b = mNsdf[(size_t)chosen],
                   c = mNsdf[(size_t)(chosen + 1)];
      const double denom = a - 2.0 * b + c;
      if (std::abs(denom) > 1.0e-12)
        t = chosen + 0.5 * (a - c) / denom;
    }

    const float freq = (float)(mDecimatedRate / t);
    if (freq < 25.0f || freq > 900.0f)
      return out;

    // Light smoothing; reset on a jump (new note) so response stays snappy.
    if (mSmoothedFreq > 0.0f &&
        std::abs(freq - mSmoothedFreq) / mSmoothedFreq < 0.06f)
      mSmoothedFreq = 0.7f * mSmoothedFreq + 0.3f * freq;
    else
      mSmoothedFreq = freq;

    const double midi =
        69.0 + 12.0 * std::log2((double)mSmoothedFreq / mReferenceA);
    out.valid = true;
    out.frequency = mSmoothedFreq;
    out.midiNote = (int)std::lround(midi);
    out.cents = (float)((midi - out.midiNote) * 100.0);
    return out;
  }

private:
  static constexpr int kDecimation = 4;
  static constexpr int kWindow = 1200; // @12 kHz = 100 ms

  double mReferenceA;
  double mDecimatedRate = 12000.0;
  float mAccum = 0.0f;
  int mAccumCount = 0;
  std::vector<float> mWindow = std::vector<float>(kWindow, 0.0f);
  int mWritePos = 0;
  int mFilled = 0;
  std::vector<float> mNsdf = std::vector<float>(kWindow / 2, 0.0f);
  float mSmoothedFreq = 0.0f;
};

} // namespace gatewaypi

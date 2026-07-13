/*
 * GatewayPi appliance layer for NAMix Linux
 * Copyright (C) 2026
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 */

#include "Tuner.h"

#include <cmath>

namespace gatewaypi {

// ---------------------------------------------------------------------------
// TunerOverlay
// ---------------------------------------------------------------------------

void TunerOverlay::timerCallback() {
  const double sr = mProcessor.getSampleRate() > 0 ? mProcessor.getSampleRate()
                                                   : 48000.0;
  for (;;) {
    const int n = mProcessor.gpReadTunerSamples(mReadBuf, 2048);
    if (n <= 0)
      break;
    mEngine.push(mReadBuf, n, sr);
  }
  mReading = mEngine.detect();
  repaint();
}

void TunerOverlay::paint(juce::Graphics &g) {
  g.fillAll(juce::Colour(0xee101113));

  static const char *names[12] = {"C",  "C#", "D",  "D#", "E",  "F",
                                  "F#", "G",  "G#", "A",  "A#", "B"};
  const auto area = getLocalBounds().toFloat();
  const bool inTune = mReading.valid && std::abs(mReading.cents) <= 3.0f;
  const juce::Colour accent{0xffe29a3a}, good{0xff5abf6e}, dim{0xff5a5c60};

  // Note name (with octave), centred in the top half.
  g.setColour(mReading.valid ? (inTune ? good : juce::Colours::white) : dim);
  g.setFont(juce::FontOptions((float)getHeight() * 0.28f, juce::Font::bold));
  const juce::String note =
      mReading.valid
          ? juce::String(names[((mReading.midiNote % 12) + 12) % 12]) +
                juce::String(mReading.midiNote / 12 - 1)
          : juce::String::fromUTF8("\xe2\x80\x94"); // em dash
  g.drawText(note, area.withTrimmedBottom(area.getHeight() * 0.45f),
             juce::Justification::centred);

  // Cents meter: -50 .. +50 with a centre tick and a needle.
  const auto meter = area.withTrimmedTop(area.getHeight() * 0.58f)
                         .reduced(area.getWidth() * 0.12f, 0.0f)
                         .withHeight(area.getHeight() * 0.16f);
  g.setColour(juce::Colour(0xff2a2c30));
  g.fillRoundedRectangle(meter, 6.0f);
  g.setColour(dim);
  g.fillRect(meter.getCentreX() - 1.5f, meter.getY(), 3.0f, meter.getHeight());
  for (int c = -40; c <= 40; c += 10) {
    const float px = meter.getX() + meter.getWidth() * (float)(c + 50) / 100.0f;
    g.fillRect(px - 0.5f, meter.getBottom() - meter.getHeight() * 0.3f, 1.0f,
               meter.getHeight() * 0.3f);
  }
  if (mReading.valid) {
    const float clamped = juce::jlimit(-50.0f, 50.0f, mReading.cents);
    const float px = meter.getX() + meter.getWidth() * (clamped + 50.0f) / 100.0f;
    g.setColour(inTune ? good : accent);
    g.fillRoundedRectangle(px - 4.0f, meter.getY() - 8.0f, 8.0f,
                           meter.getHeight() + 16.0f, 4.0f);
  }

  // Frequency + hint line.
  g.setColour(dim);
  g.setFont(juce::FontOptions((float)getHeight() * 0.045f));
  const juce::String info =
      (mReading.valid
           ? juce::String(mReading.frequency, 1) + " Hz   " +
                 juce::String(mReading.cents > 0 ? "+" : "") +
                 juce::String(mReading.cents, 1) + " cents   "
           : juce::String("play a string   ")) +
      juce::String::fromUTF8("output muted \xc2\xb7 TUNE or hold C to resume");
  g.drawText(info, area.withTrimmedTop(area.getHeight() * 0.82f),
             juce::Justification::centred);
}

} // namespace gatewaypi

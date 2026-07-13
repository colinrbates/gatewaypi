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
// TunerOverlay — drawn as a vintage needle tuner: brushed charcoal panel,
// ivory dial with an arc scale, a ballistic needle, flat/tune/sharp lamps
// and an amber LCD note window.
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

  // Ballistic needle: ease toward the target like a damped meter movement.
  // Resting position is hard left when no note is sounding.
  const float target = mReading.valid ? juce::jlimit(-50.0f, 50.0f, mReading.cents)
                                      : -55.0f;
  mNeedle += (target - mNeedle) * 0.28f;
  repaint();
}

void TunerOverlay::paint(juce::Graphics &g) {
  const auto full = getLocalBounds().toFloat();

  // ---- Panel: dark brushed metal with a vignette and corner screws ----
  {
    juce::ColourGradient grad(juce::Colour(0xff34302a), full.getCentreX(), 0.0f,
                              juce::Colour(0xff191713), full.getCentreX(),
                              full.getBottom(), false);
    grad.addColour(0.5, juce::Colour(0xff262320));
    g.setGradientFill(grad);
    g.fillAll();

    // Faint horizontal brushing.
    g.setColour(juce::Colours::white.withAlpha(0.018f));
    for (float y = 0.0f; y < full.getHeight(); y += 3.0f)
      g.drawHorizontalLine((int)y, 0.0f, full.getWidth());

    // Corner screws.
    const float m = juce::jmin(full.getWidth(), full.getHeight()) * 0.045f;
    const float sr2 = m * 0.32f;
    for (auto p : {juce::Point<float>(m, m),
                   juce::Point<float>(full.getWidth() - m, m),
                   juce::Point<float>(m, full.getHeight() - m),
                   juce::Point<float>(full.getWidth() - m, full.getHeight() - m)}) {
      juce::ColourGradient screw(juce::Colour(0xff8a8578), p.x - sr2 * 0.4f,
                                 p.y - sr2 * 0.4f, juce::Colour(0xff3a372f),
                                 p.x + sr2, p.y + sr2, true);
      g.setGradientFill(screw);
      g.fillEllipse(p.x - sr2, p.y - sr2, sr2 * 2.0f, sr2 * 2.0f);
      g.setColour(juce::Colour(0xff23211c));
      g.drawLine(p.x - sr2 * 0.6f, p.y - sr2 * 0.35f, p.x + sr2 * 0.6f,
                 p.y + sr2 * 0.35f, sr2 * 0.28f);
    }
  }

  // Layout regions.
  const auto content = full.reduced(full.getWidth() * 0.08f,
                                    full.getHeight() * 0.07f);
  auto dial = content.withTrimmedBottom(content.getHeight() * 0.42f);
  auto lower = content.withTrimmedTop(content.getHeight() * 0.62f);

  const bool inTune = mReading.valid && std::abs(mReading.cents) <= 3.0f;
  const bool flat = mReading.valid && mReading.cents < -3.0f;
  const bool sharp = mReading.valid && mReading.cents > 3.0f;

  // ---- Dial face: warm ivory with a bezel and inner shadow ----
  {
    const auto bezel = dial.expanded(6.0f);
    g.setColour(juce::Colour(0xff0e0d0b));
    g.fillRoundedRectangle(bezel, 10.0f);
    g.setColour(juce::Colour(0xff4c473d));
    g.drawRoundedRectangle(bezel, 10.0f, 2.0f);

    juce::ColourGradient face(juce::Colour(0xfff3ead2), dial.getCentreX(),
                              dial.getY(), juce::Colour(0xffd9cba6),
                              dial.getCentreX(), dial.getBottom(), false);
    g.setGradientFill(face);
    g.fillRoundedRectangle(dial, 6.0f);
    g.setColour(juce::Colours::black.withAlpha(0.18f));
    g.drawRoundedRectangle(dial.reduced(1.0f), 6.0f, 2.0f);
  }

  // Needle geometry: pivot below the dial's bottom edge, sweep ±42°.
  const juce::Point<float> pivot(dial.getCentreX(),
                                 dial.getBottom() + dial.getHeight() * 0.55f);
  const float radius = dial.getHeight() * 1.18f;
  auto angleFor = [&](float cents) {
    return juce::degreesToRadians(cents / 50.0f * 42.0f);
  };
  auto pointAt = [&](float cents, float r) {
    const float a = angleFor(cents);
    return juce::Point<float>(pivot.x + r * std::sin(a),
                              pivot.y - r * std::cos(a));
  };

  // ---- Scale: ticks and labels on the ivory face ----
  {
    g.saveState();
    g.reduceClipRegion(dial.reduced(2.0f).toNearestInt());

    // Green in-tune wedge behind the centre.
    juce::Path wedge;
    wedge.startNewSubPath(pivot);
    wedge.lineTo(pointAt(-3.0f, radius));
    wedge.addCentredArc(pivot.x, pivot.y, radius, radius, 0.0f, angleFor(-3.0f),
                        angleFor(3.0f));
    wedge.lineTo(pivot);
    g.setColour(juce::Colour(0x2f2f7d3c));
    g.fillPath(wedge);

    const float tickOuter = radius * 0.985f;
    for (int c = -50; c <= 50; c += 5) {
      const bool major = c % 25 == 0;
      const bool mid = c % 10 == 0;
      const float len = dial.getHeight() * (major ? 0.16f : mid ? 0.11f : 0.06f);
      g.setColour(juce::Colour(0xff4a4436).withAlpha(major ? 1.0f : 0.75f));
      g.drawLine({pointAt((float)c, tickOuter - len), pointAt((float)c, tickOuter)},
                 major ? 2.4f : 1.2f);
    }
    // Centre marker triangle.
    {
      juce::Path tri;
      const auto tip = pointAt(0.0f, tickOuter - dial.getHeight() * 0.19f);
      tri.addTriangle(tip.x, tip.y, tip.x - 6.0f, tip.y - 12.0f, tip.x + 6.0f,
                      tip.y - 12.0f);
      g.setColour(juce::Colour(0xff2f7d3c));
      g.fillPath(tri);
    }
    // Labels.
    g.setColour(juce::Colour(0xff4a4436));
    g.setFont(juce::FontOptions(dial.getHeight() * 0.11f, juce::Font::bold));
    for (int c = -50; c <= 50; c += 25) {
      const auto p = pointAt((float)c, tickOuter - dial.getHeight() * 0.28f);
      const juce::String txt = c == 0 ? "0" : juce::String(c > 0 ? "+" : "") + juce::String(c);
      g.drawText(txt, (int)p.x - 24, (int)p.y - 10, 48, 20,
                 juce::Justification::centred);
    }
    g.setFont(juce::FontOptions(dial.getHeight() * 0.085f));
    g.drawText("CENTS", dial.toNearestInt().withTrimmedTop((int)(dial.getHeight() * 0.72f)),
               juce::Justification::centred);

    // ---- Needle: dark red with a drop shadow, clipped to the dial ----
    {
      const auto tip = pointAt(mNeedle, radius * 0.96f);
      const auto base = pointAt(mNeedle, radius * 0.30f);
      g.setColour(juce::Colours::black.withAlpha(0.25f));
      g.drawLine(base.x + 3.0f, base.y + 3.0f, tip.x + 3.0f, tip.y + 3.0f, 3.5f);
      g.setColour(juce::Colour(0xff8e2f23));
      g.drawLine({base, tip}, 3.5f);
    }

    // Glass highlight across the top of the dial.
    juce::ColourGradient glass(juce::Colours::white.withAlpha(0.16f),
                               dial.getCentreX(), dial.getY(),
                               juce::Colours::white.withAlpha(0.0f),
                               dial.getCentreX(), dial.getCentreY(), false);
    g.setGradientFill(glass);
    g.fillRoundedRectangle(dial.withHeight(dial.getHeight() * 0.5f), 6.0f);
    g.restoreState();
  }

  // ---- Lamps: flat / in-tune / sharp jewels under the dial ----
  {
    const float ly = dial.getBottom() + content.getHeight() * 0.075f;
    const float lr = juce::jmin(content.getWidth(), content.getHeight()) * 0.035f;
    struct Lamp { float x; bool lit; juce::Colour on; const char *label; };
    const Lamp lamps[3] = {
        {dial.getCentreX() - dial.getWidth() * 0.22f, flat,
         juce::Colour(0xffd98c2b), "FLAT"},
        {dial.getCentreX(), inTune, juce::Colour(0xff43c256), "TUNE"},
        {dial.getCentreX() + dial.getWidth() * 0.22f, sharp,
         juce::Colour(0xffd98c2b), "SHARP"}};
    g.setFont(juce::FontOptions(lr * 1.15f, juce::Font::bold));
    for (const auto &l : lamps) {
      if (l.lit) { // glow
        juce::ColourGradient glow(l.on.withAlpha(0.55f), l.x, ly,
                                  l.on.withAlpha(0.0f), l.x + lr * 3.0f, ly, true);
        g.setGradientFill(glow);
        g.fillEllipse(l.x - lr * 3.0f, ly - lr * 3.0f, lr * 6.0f, lr * 6.0f);
      }
      juce::ColourGradient jewel(
          l.lit ? l.on.brighter(0.6f) : juce::Colour(0xff3b382f), l.x - lr * 0.4f,
          ly - lr * 0.4f, l.lit ? l.on.darker(0.4f) : juce::Colour(0xff211f1a),
          l.x + lr, ly + lr, true);
      g.setGradientFill(jewel);
      g.fillEllipse(l.x - lr, ly - lr, lr * 2.0f, lr * 2.0f);
      g.setColour(juce::Colour(0xff0e0d0b));
      g.drawEllipse(l.x - lr, ly - lr, lr * 2.0f, lr * 2.0f, 1.5f);
      g.setColour(juce::Colour(0xff9a917c));
      g.drawText(l.label, (int)(l.x - lr * 4.0f), (int)(ly + lr * 1.5f),
                 (int)(lr * 8.0f), (int)(lr * 2.0f), juce::Justification::centred);
    }
  }

  // ---- LCD note window ----
  {
    auto lcd = lower.withTrimmedTop(lower.getHeight() * 0.28f)
                   .withSizeKeepingCentre(lower.getWidth() * 0.5f,
                                          lower.getHeight() * 0.62f);
    g.setColour(juce::Colour(0xff0e0d0b));
    g.fillRoundedRectangle(lcd.expanded(5.0f), 8.0f);
    g.setColour(juce::Colour(0xff4c473d));
    g.drawRoundedRectangle(lcd.expanded(5.0f), 8.0f, 2.0f);
    juce::ColourGradient screen(juce::Colour(0xff1d1509), lcd.getCentreX(),
                                lcd.getY(), juce::Colour(0xff120d06),
                                lcd.getCentreX(), lcd.getBottom(), false);
    g.setGradientFill(screen);
    g.fillRoundedRectangle(lcd, 4.0f);

    static const char *names[12] = {"C",  "C#", "D",  "D#", "E",  "F",
                                    "F#", "G",  "G#", "A",  "A#", "B"};
    const juce::Colour lit = inTune ? juce::Colour(0xff6fe07f)
                                    : juce::Colour(0xffe8a33c);
    g.setColour(mReading.valid ? lit : lit.withAlpha(0.25f));
    g.setFont(juce::FontOptions(lcd.getHeight() * 0.72f, juce::Font::bold));
    const juce::String note =
        mReading.valid
            ? juce::String(names[((mReading.midiNote % 12) + 12) % 12]) +
                  juce::String(mReading.midiNote / 12 - 1)
            : juce::String::fromUTF8("\xe2\x80\x94");
    g.drawText(note, lcd.toNearestInt(), juce::Justification::centred);

    // Hz readout to the right of the window.
    g.setColour(juce::Colour(0xff9a917c));
    g.setFont(juce::FontOptions(lcd.getHeight() * 0.2f));
    if (mReading.valid)
      g.drawText(juce::String(mReading.frequency, 1) + " Hz",
                 (int)lcd.getRight() + 10, (int)lcd.getCentreY() - 10, 120, 20,
                 juce::Justification::centredLeft);
  }

  // ---- Hint line ----
  g.setColour(juce::Colour(0xff77705f));
  g.setFont(juce::FontOptions(full.getHeight() * 0.028f));
  g.drawText(mReading.valid
                 ? juce::String("output muted") + juce::String::fromUTF8(" \xc2\xb7 ") +
                       "TUNE or hold C to resume"
                 : juce::String("play a string") + juce::String::fromUTF8(" \xc2\xb7 ") +
                       "output muted",
             getLocalBounds().withTrimmedTop((int)(full.getHeight() * 0.94f)),
             juce::Justification::centred);
}

} // namespace gatewaypi

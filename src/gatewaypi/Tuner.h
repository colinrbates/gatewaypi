/*
 * GatewayPi appliance layer for NAMix Linux
 * Copyright (C) 2026
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 */

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "TunerEngine.h"

namespace gatewaypi {

// Overlay covering the amp panel while the output is muted (silent tuning):
// big note name, cents needle, frequency readout.
class TunerOverlay : public juce::Component, private juce::Timer {
public:
  explicit TunerOverlay(NAMixAudioProcessor &proc, double referenceA = 440.0)
      : mProcessor(proc), mEngine(referenceA) {
    setInterceptsMouseClicks(false, false);
  }

  void visibilityChanged() override {
    if (isVisible())
      startTimerHz(20);
    else
      stopTimer();
  }

  void paint(juce::Graphics &g) override;

private:
  void timerCallback();

  NAMixAudioProcessor &mProcessor;
  TunerEngine mEngine;
  TunerEngine::Reading mReading;
  float mNeedle = -55.0f; // displayed needle position (cents; damped)
  float mReadBuf[2048];
};

} // namespace gatewaypi

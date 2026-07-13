/*
 * GatewayPi appliance layer for NAMix Linux
 * Copyright (C) 2026
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 */

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "GatewayPiConfig.h"
#include "MidiEngine.h"
#include "PresetManager.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace gatewaypi {

// Touch-first strip shown above the stock Gateway/NAMix panel: four preset
// buttons (one per Chocolate footswitch), bank navigation, bypass/mute,
// save, audio settings and shutdown.
class PresetBar : public juce::Component {
public:
  explicit PresetBar(PresetManager &presets);

  void refresh(); // re-read names/selection/bypass/mute from the PresetManager
  void resized() override;
  void paint(juce::Graphics &g) override;

  std::function<void()> onOpenAudioSettings;
  std::function<void()> onShutdown;

private:
  PresetManager &mPresets;
  std::array<juce::TextButton, kSlotsPerBank> mSlotButtons;
  juce::TextButton mBankDown{"<"}, mBankUp{">"};
  juce::Label mBankLabel;
  juce::TextButton mBypass{"BYP"}, mMute{"MUTE"}, mSave{"SAVE"};
  juce::TextButton mSettings{"AUDIO"}, mPower{"OFF"};
};

// The standalone appliance editor: PresetBar on top, the untouched
// NAMix editor below (scaled to fill the touchscreen).  Owns the
// PresetManager and MidiEngine, so foot control works for the lifetime of
// the app window — which, in kiosk mode, is the lifetime of the app.
class KioskShell : public juce::AudioProcessorEditor, private juce::Timer {
public:
  explicit KioskShell(NAMixAudioProcessor &proc);
  ~KioskShell() override;

  void resized() override;
  void paint(juce::Graphics &g) override;

private:
  void timerCallback() override;
  void refresh();
  void recreateInnerEditor();
  void configureAudioDevice();
  void openAudioSettings();
  void requestShutdown();

  NAMixAudioProcessor &mProcessor;
  Config mConfig;
  std::shared_ptr<PresetManager> mPresets;
  std::unique_ptr<MidiEngine> mMidi;
  PresetBar mBar;
  std::unique_ptr<NAMixAudioProcessorEditor> mInner;

  // Last-seen model/IR paths — the inner editor is rebuilt when these move
  // underneath it (it reads processor state in its constructor).
  juce::String mLastModelPath, mLastIRPath;

  bool mAudioDeviceConfigured = false;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KioskShell)
};

} // namespace gatewaypi

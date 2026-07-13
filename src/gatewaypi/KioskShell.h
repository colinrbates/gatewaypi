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
#include "Tuner.h"

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
  std::function<void()> onOpenMidiLearn;
  std::function<void()> onShutdown;

private:
  PresetManager &mPresets;
  std::array<juce::TextButton, kSlotsPerBank> mSlotButtons;
  juce::TextButton mBankDown{"<"}, mBankUp{">"};
  juce::Label mBankLabel;
  juce::TextButton mBypass{"BYP"}, mMute{"TUNE"}, mSave{"SAVE"};
  juce::TextButton mLearn{"LEARN"}, mSettings{"AUDIO"}, mPower{"OFF"};
};

// Touch overlay for binding footswitches in-app: tap an action, press a
// footswitch, done — the binding is stored in config.json.  No pedal-side
// (CubeSuite) programming required.
class MidiLearnOverlay : public juce::Component {
public:
  explicit MidiLearnOverlay(MidiEngine &midi);

  void refreshBindings();
  void resized() override;
  void paint(juce::Graphics &g) override;

  std::function<void()> onClose;

private:
  void armAction(MidiAction a);

  MidiEngine &mMidi;
  static constexpr int kNumActions = 8;
  std::array<juce::TextButton, kNumActions> mActionButtons;
  juce::Label mTitle, mStatus;
  juce::TextButton mDone{"DONE"};
};

// A full-screen confirm overlay (child component, so touch works under the
// cage kiosk — native AlertWindows do not get pointer input there).
class ConfirmOverlay : public juce::Component {
public:
  ConfirmOverlay(const juce::String &title, const juce::String &yesLabel);
  void resized() override;
  void paint(juce::Graphics &g) override;
  std::function<void()> onYes;
  std::function<void()> onCancel;

private:
  juce::String mTitle;
  juce::TextButton mYes, mCancel{"CANCEL"};
};

// In-window audio settings (child component — the native device dialog and
// any ComboBox popup are separate windows that get no touch under cage, so
// everything here is plain buttons).  Lets you pick the hardware interface,
// buffer size and sample rate, and shows whether the input is actually live.
class AudioPanel : public juce::Component, private juce::Timer {
public:
  explicit AudioPanel(const Config &config);
  void resized() override;
  void paint(juce::Graphics &g) override;
  void refreshDevices();
  std::function<void()> onClose;

private:
  void timerCallback() override;
  void rebuildDeviceButtons();
  void openDevice(const juce::String &name);
  void applyRateBuffer();

  const Config &mConfig;
  juce::Label mTitle, mStatus;
  juce::OwnedArray<juce::TextButton> mDeviceButtons;
  juce::Label mDeviceHeader, mBufHeader, mRateHeader;
  std::array<juce::TextButton, 3> mBufButtons;
  std::array<juce::TextButton, 2> mRateButtons;
  juce::TextButton mClose{"CLOSE"};
  juce::String mSelectedName;
};

// Static, self-contained audio-device configuration used by both the
// watchdog and the AudioPanel.  Returns the number of live input channels
// after the attempt (0 = input failed).  Standalone builds only.
int gpTryOpenDevice(const juce::String &deviceNameContains, double sampleRate,
                    int bufferSize);
juce::StringArray gpListDuplexDevices();

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
  void openMidiLearn();
  void requestShutdown();

  NAMixAudioProcessor &mProcessor;
  Config mConfig;
  std::shared_ptr<PresetManager> mPresets;
  std::unique_ptr<MidiEngine> mMidi;
  PresetBar mBar;
  TunerOverlay mTuner;
  std::unique_ptr<MidiLearnOverlay> mLearnOverlay;
  std::unique_ptr<AudioPanel> mAudioPanel;
  std::unique_ptr<ConfirmOverlay> mConfirm;
  std::unique_ptr<NAMixAudioProcessorEditor> mInner;

  // Last-seen model/IR paths — the inner editor is rebuilt when these move
  // underneath it (it reads processor state in its constructor).
  juce::String mLastModelPath, mLastIRPath;

  bool mAudioDeviceConfigured = false;
  int mConfigureAttempts = 0;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KioskShell)
};

} // namespace gatewaypi

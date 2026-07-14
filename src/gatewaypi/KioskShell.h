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

// Bigger, bolder button/label text than JUCE's defaults — the stock cap is
// ~16 px, which is hard to read on a small touchscreen at arm's length.
// Applied to the kiosk chrome only; the amp panel keeps its own look.
struct GatewayPiLookAndFeel : juce::LookAndFeel_V4 {
  juce::Font getTextButtonFont(juce::TextButton &, int buttonHeight) override {
    return juce::Font(juce::FontOptions(
        juce::jlimit(20.0f, 44.0f, buttonHeight * 0.62f), juce::Font::bold));
  }
};

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
  std::function<void(int)> onRenameSlot; // long-press a slot -> rename it

private:
  // A preset slot button that also reports a long-press (hold ~0.5 s), used
  // to open the rename keyboard.  Plain tap still selects the preset.
  class SlotButton : public juce::TextButton, private juce::Timer {
  public:
    std::function<void()> onLongPress;
    void mouseDown(const juce::MouseEvent &e) override {
      mFired = false;
      startTimer(500);
      juce::TextButton::mouseDown(e);
    }
    void mouseUp(const juce::MouseEvent &e) override {
      stopTimer();
      juce::TextButton::mouseUp(e);
    }

  private:
    void timerCallback() override {
      stopTimer();
      mFired = true;
      if (onLongPress)
        onLongPress();
    }
    bool mFired = false;
  };

  PresetManager &mPresets;
  std::array<SlotButton, kSlotsPerBank> mSlotButtons;
  juce::TextButton mBankDown{"<"}, mBankUp{">"};
  juce::Label mBankLabel;
  juce::TextButton mBypass{"BYP"}, mMute{"TUNE"}, mSave{"SAVE"};
  juce::TextButton mLearn{"LEARN"}, mSettings{"AUDIO"}, mPower{"OFF"};
};

// Full-screen on-screen QWERTY for naming presets on the touchscreen (no
// physical keyboard in a kiosk).  Child component, so touch works under cage.
class KeyboardOverlay : public juce::Component {
public:
  KeyboardOverlay();
  void setPrompt(const juce::String &prompt, const juce::String &initialText);
  void resized() override;
  void paint(juce::Graphics &g) override;

  std::function<void(juce::String)> onAccept;
  std::function<void()> onCancel;
  std::function<void(int)> onMove; // reorder: -1 = move left, +1 = move right

  // USB-keyboard support: type/backspace/enter/escape drive renaming too.
  bool keyPressed(const juce::KeyPress &key) override;
  void visibilityChanged() override;

private:
  void buildKeys();   // create the on-screen keys once
  void relabelKeys(); // update letter case in place (never destroy mid-click)
  void refreshDisplay();
  void appendChar(const juce::String &c);

  struct LetterKey {
    juce::TextButton *button;
    juce::juce_wchar upper;
  };

  juce::String mPrompt, mText;
  bool mShift = true; // start capitalised (amp names read nicer)
  juce::Label mPromptLabel, mTextLabel;
  juce::OwnedArray<juce::TextButton> mKeys; // built once, persistent
  juce::Array<LetterKey> mLetterKeys;       // letter buttons + base char
  juce::TextButton *mShiftKey = nullptr;
  juce::TextButton mMoveLeft{juce::String::fromUTF8("\xe2\x97\x80 MOVE")};
  juce::TextButton mMoveRight{juce::String::fromUTF8("MOVE \xe2\x96\xb6")};
  juce::TextButton mCancel{"CANCEL"}, mAccept{"SAVE"};
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
// cage kiosk — native AlertWindows do not get pointer input there).  Offers
// a primary action, an optional secondary action, and cancel.
class ConfirmOverlay : public juce::Component {
public:
  ConfirmOverlay(const juce::String &title, const juce::String &primaryLabel,
                 const juce::String &secondaryLabel = juce::String());
  void resized() override;
  void paint(juce::Graphics &g) override;
  std::function<void()> onPrimary;
  std::function<void()> onSecondary;
  std::function<void()> onCancel;

private:
  juce::String mTitle;
  bool mHasSecondary = false;
  juce::TextButton mPrimary, mSecondary, mCancel{"CANCEL"};
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
  // Timer (which polls device state, incl. a JACK scan) runs ONLY while the
  // panel is visible — a hidden component's timer otherwise keeps opening
  // JACK clients forever, wasting CPU and crashing at teardown.
  void visibilityChanged() override;
  std::function<void()> onClose;

private:
  void timerCallback() override;
  void rebuildDeviceButtons();
  void reopen();

  const Config &mConfig;
  juce::Label mTitle, mStatus;
  juce::Label mDeviceHeader, mInputHeader, mBufHeader;
  juce::OwnedArray<juce::TextButton> mDeviceButtons;
  std::array<juce::TextButton, 3> mInputButtons; // In 1 / In 2 / Both
  std::array<juce::TextButton, 3> mBufButtons;
  juce::TextButton mTest{"TEST TONE"}, mClose{"CLOSE"};

  juce::String mSelectedName;
  int mInputMask = 1;         // bits 0/1 -> device capture channels (In 1 default)
  int mBufSel = 128;
  bool mJackCached = false;   // JACK availability, refreshed only on open
  static constexpr double kRate = 48000.0; // NAM native; fixed
};

// Static, self-contained audio-device configuration used by both the
// watchdog and the AudioPanel.  Returns the number of live input channels
// after the attempt (0 = input failed).  inputChannelMask selects which
// device capture channels feed the amp (bit0=in1, bit1=in2). Standalone only.
int gpTryOpenDevice(const juce::String &deviceNameContains, double sampleRate,
                    int bufferSize, int inputChannelMask = 3);
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
  void openRename(int slot);
  void requestShutdown();
  void hideOverlaysExcept(juce::Component *keep); // one overlay at a time

  NAMixAudioProcessor &mProcessor;
  Config mConfig;
  GatewayPiLookAndFeel mLnf; // bigger text on the kiosk chrome
  std::shared_ptr<PresetManager> mPresets;
  std::unique_ptr<MidiEngine> mMidi;
  PresetBar mBar;
  TunerOverlay mTuner;
  std::unique_ptr<MidiLearnOverlay> mLearnOverlay;
  std::unique_ptr<AudioPanel> mAudioPanel;
  std::unique_ptr<ConfirmOverlay> mConfirm;
  std::unique_ptr<KeyboardOverlay> mKeyboard;
  std::unique_ptr<NAMixAudioProcessorEditor> mInner;

  // Last-seen model/IR paths — the inner editor is rebuilt when these move
  // underneath it (it reads processor state in its constructor).
  juce::String mLastModelPath, mLastIRPath;

  bool mAudioDeviceConfigured = false;
  bool mUserSetAudio = false; // user picked a device in the AUDIO panel
  int mConfigureAttempts = 0;

  // Field-debug dump: `touch /tmp/gatewaypi-dump-request` over ssh records
  // ~8 s of the app's raw input and final output to /tmp/gp-{in,out}.f32
  // for offline analysis.  Zero cost when idle.
  class DebugDump : private juce::Timer {
  public:
    explicit DebugDump(NAMixAudioProcessor &p) : mProc(p) { startTimer(1000); }
    ~DebugDump() override { stopTimer(); finish(); }
    bool isActive() const { return mIn != nullptr; }

  private:
    void timerCallback() override;
    void finish();
    NAMixAudioProcessor &mProc;
    FILE *mIn = nullptr, *mOut = nullptr;
    int mTicksLeft = 0;
  };
  std::unique_ptr<DebugDump> mDump;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KioskShell)
};

} // namespace gatewaypi

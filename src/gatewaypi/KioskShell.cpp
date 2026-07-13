/*
 * GatewayPi appliance layer for NAMix Linux
 * Copyright (C) 2026
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 */

#include "KioskShell.h"

#if JucePlugin_Build_Standalone
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

namespace gatewaypi {

// ---------------------------------------------------------------------------
// PresetBar
// ---------------------------------------------------------------------------

namespace colours {
const juce::Colour bg{0xff17181a};
const juce::Colour active{0xffe29a3a};   // tube amber
const juce::Colour idle{0xff2a2c30};
const juce::Colour warn{0xffb0413e};
const juce::Colour text{0xffe9e6df};
} // namespace colours

PresetBar::PresetBar(PresetManager &presets) : mPresets(presets) {
  for (int i = 0; i < kSlotsPerBank; ++i) {
    auto &b = mSlotButtons[(size_t)i];
    b.setColour(juce::TextButton::buttonColourId, colours::idle);
    b.setColour(juce::TextButton::buttonOnColourId, colours::active);
    b.setColour(juce::TextButton::textColourOffId, colours::text);
    b.setColour(juce::TextButton::textColourOnId, colours::bg);
    b.setClickingTogglesState(false);
    b.onClick = [this, i] { mPresets.selectSlot(i); };
    addAndMakeVisible(b);
  }

  mBankDown.onClick = [this] { mPresets.bankDown(); };
  mBankUp.onClick = [this] { mPresets.bankUp(); };
  mBypass.onClick = [this] { mPresets.toggleBypass(); };
  mMute.onClick = [this] { mPresets.toggleMute(); };
  mSave.onClick = [this] { mPresets.saveCurrentSlot(); };
  mLearn.onClick = [this] {
    if (onOpenMidiLearn)
      onOpenMidiLearn();
  };
  mSettings.onClick = [this] {
    if (onOpenAudioSettings)
      onOpenAudioSettings();
  };
  mPower.onClick = [this] {
    if (onShutdown)
      onShutdown();
  };

  for (auto *b : {&mBankDown, &mBankUp, &mBypass, &mMute, &mSave, &mLearn,
                  &mSettings, &mPower}) {
    b->setColour(juce::TextButton::buttonColourId, colours::idle);
    b->setColour(juce::TextButton::buttonOnColourId, colours::warn);
    b->setColour(juce::TextButton::textColourOffId, colours::text);
    b->setColour(juce::TextButton::textColourOnId, colours::text);
    addAndMakeVisible(*b);
  }

  mBankLabel.setJustificationType(juce::Justification::centred);
  mBankLabel.setColour(juce::Label::textColourId, colours::text);
  addAndMakeVisible(mBankLabel);

  refresh();
}

void PresetBar::refresh() {
  for (int i = 0; i < kSlotsPerBank; ++i) {
    auto &b = mSlotButtons[(size_t)i];
    b.setButtonText(juce::String(i + 1) + juce::String::fromUTF8(" \xc2\xb7 ") +
                    mPresets.getSlotName(i));
    b.setToggleState(i == mPresets.getSlot() && mPresets.isSlotOccupied(i),
                     juce::dontSendNotification);
    b.setAlpha(mPresets.isSlotOccupied(i) ? 1.0f : 0.55f);
  }
  mBankLabel.setText("BANK " + juce::String(mPresets.getBank() + 1) + "/" +
                         juce::String(mPresets.getNumBanks()),
                     juce::dontSendNotification);
  mBypass.setToggleState(mPresets.getBypass(), juce::dontSendNotification);
  mMute.setToggleState(mPresets.getMute(), juce::dontSendNotification);
  repaint();
}

void PresetBar::paint(juce::Graphics &g) { g.fillAll(colours::bg); }

void PresetBar::resized() {
  auto area = getLocalBounds().reduced(6);
  const int gap = 6;

  // Row 1: the four preset slots — the biggest touch targets on screen.
  auto row1 = area.removeFromTop((area.getHeight() * 3) / 5);
  const int slotW = (row1.getWidth() - gap * (kSlotsPerBank - 1)) / kSlotsPerBank;
  for (int i = 0; i < kSlotsPerBank; ++i) {
    mSlotButtons[(size_t)i].setBounds(row1.removeFromLeft(slotW));
    row1.removeFromLeft(gap);
  }

  // Row 2: bank navigation and utility buttons.
  auto row2 = area.withTrimmedTop(gap);
  const int n = 9; // <, label, >, BYP, TUNE, SAVE, LEARN, AUDIO, OFF
  const int w = (row2.getWidth() - gap * (n - 1)) / n;
  for (auto *c : std::initializer_list<juce::Component *>{
           &mBankDown, &mBankLabel, &mBankUp, &mBypass, &mMute, &mSave,
           &mLearn, &mSettings}) {
    c->setBounds(row2.removeFromLeft(w));
    row2.removeFromLeft(gap);
  }
  mPower.setBounds(row2);
}

// ---------------------------------------------------------------------------
// MidiLearnOverlay
// ---------------------------------------------------------------------------

namespace {
constexpr MidiAction kAllActions[] = {
    MidiAction::Slot1, MidiAction::Slot2, MidiAction::Slot3, MidiAction::Slot4,
    MidiAction::BankDown, MidiAction::BankUp, MidiAction::Bypass,
    MidiAction::Tuner};
}

MidiLearnOverlay::MidiLearnOverlay(MidiEngine &midi) : mMidi(midi) {
  mTitle.setText("MIDI learn", juce::dontSendNotification);
  mTitle.setJustificationType(juce::Justification::centred);
  mTitle.setColour(juce::Label::textColourId, colours::active);
  addAndMakeVisible(mTitle);

  mStatus.setText("Tap an action, then press a footswitch.",
                  juce::dontSendNotification);
  mStatus.setJustificationType(juce::Justification::centred);
  mStatus.setColour(juce::Label::textColourId, colours::text);
  addAndMakeVisible(mStatus);

  for (int i = 0; i < kNumActions; ++i) {
    auto &b = mActionButtons[(size_t)i];
    b.setColour(juce::TextButton::buttonColourId, colours::idle);
    b.setColour(juce::TextButton::buttonOnColourId, colours::active);
    b.setColour(juce::TextButton::textColourOffId, colours::text);
    b.setColour(juce::TextButton::textColourOnId, colours::bg);
    b.onClick = [this, i] { armAction(kAllActions[i]); };
    addAndMakeVisible(b);
  }
  refreshBindings();

  mDone.setColour(juce::TextButton::buttonColourId, colours::active);
  mDone.setColour(juce::TextButton::textColourOffId, colours::bg);
  mDone.onClick = [this] {
    mMidi.setLearnTarget(std::nullopt);
    if (onClose)
      onClose();
  };
  addAndMakeVisible(mDone);
}

void MidiLearnOverlay::refreshBindings() {
  for (int i = 0; i < kNumActions; ++i) {
    const auto a = kAllActions[i];
    mActionButtons[(size_t)i].setButtonText(
        juce::String(midiActionLabel(a)) + juce::String::fromUTF8(" \xc2\xb7 ") +
        mMidi.describeBinding(a));
    mActionButtons[(size_t)i].setToggleState(false, juce::dontSendNotification);
  }
}

void MidiLearnOverlay::armAction(MidiAction a) {
  refreshBindings();
  for (int i = 0; i < kNumActions; ++i)
    if (kAllActions[i] == a)
      mActionButtons[(size_t)i].setToggleState(true, juce::dontSendNotification);
  mStatus.setText(juce::String("Press a footswitch for \"") +
                      midiActionLabel(a) + "\"...",
                  juce::dontSendNotification);
  mMidi.setLearnTarget(
      a, [safe = juce::Component::SafePointer<MidiLearnOverlay>(this),
          a](juce::String desc) {
        if (safe == nullptr)
          return;
        safe->refreshBindings();
        safe->mStatus.setText(juce::String(midiActionLabel(a)) + " = " + desc +
                                  ". Tap another action or DONE.",
                              juce::dontSendNotification);
      });
}

void MidiLearnOverlay::paint(juce::Graphics &g) {
  g.fillAll(juce::Colour(0xf2141517));
}

void MidiLearnOverlay::resized() {
  auto area = getLocalBounds().reduced(juce::jmax(12, getWidth() / 16));
  mTitle.setFont(juce::FontOptions((float)getHeight() * 0.05f, juce::Font::bold));
  mTitle.setBounds(area.removeFromTop(area.getHeight() / 10));
  mStatus.setBounds(area.removeFromTop(area.getHeight() / 9));
  auto footer = area.removeFromBottom(area.getHeight() / 6);
  const int gap = 8;
  area.removeFromBottom(gap);

  // 2 columns x 4 rows of action buttons.
  const int rows = 4, cols = 2;
  const int cw = (area.getWidth() - gap * (cols - 1)) / cols;
  const int rh = (area.getHeight() - gap * (rows - 1)) / rows;
  for (int i = 0; i < kNumActions; ++i) {
    const int r = i % rows, c = i / rows;
    mActionButtons[(size_t)i].setBounds(area.getX() + c * (cw + gap),
                                        area.getY() + r * (rh + gap), cw, rh);
  }
  mDone.setBounds(footer.withSizeKeepingCentre(footer.getWidth() / 3,
                                               footer.getHeight() - 8));
}

// ---------------------------------------------------------------------------
// KioskShell
// ---------------------------------------------------------------------------

KioskShell::KioskShell(NAMixAudioProcessor &proc)
    : juce::AudioProcessorEditor(proc), mProcessor(proc),
      mConfig(Config::load()),
      mPresets(std::make_shared<PresetManager>(proc, mConfig)),
      mBar(*mPresets), mTuner(proc, mConfig.tunerReference) {
  mPresets->onChanged = [this] { refresh(); };
  mBar.onOpenAudioSettings = [this] { openAudioSettings(); };
  mBar.onOpenMidiLearn = [this] { openMidiLearn(); };
  mBar.onShutdown = [this] { requestShutdown(); };
  addAndMakeVisible(mBar);
  addChildComponent(mTuner);

  recreateInnerEditor();

  // Size to the touchscreen; the kiosk window fullscreens us and resized()
  // rescales.  Fall back to the Touch Display 2's portrait resolution.
  auto *display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
  const auto area = display != nullptr ? display->userArea
                                       : juce::Rectangle<int>(0, 0, 720, 1280);
  setResizable(true, false);
  setSize(area.getWidth(), area.getHeight());

  mMidi = std::make_unique<MidiEngine>(mPresets, mConfig);

  // Restore the last-used preset once the message loop is running.
  juce::MessageManager::callAsync(
      [weak = std::weak_ptr<PresetManager>(mPresets)] {
        if (auto p = weak.lock())
          p->restoreLastState();
      });

  startTimer(1000); // audio-device watchdog
}

KioskShell::~KioskShell() {
  mMidi.reset(); // stop MIDI callbacks before anything else goes away
  mPresets->onChanged = nullptr;
}

void KioskShell::paint(juce::Graphics &g) { g.fillAll(colours::bg); }

void KioskShell::refresh() {
  mBar.refresh();

  // Mute doubles as silent tuning: the tap feeds the detector only while
  // the overlay is up.
  const bool tuning = mPresets->getMute();
  mProcessor.gpSetTunerTap(tuning);
  mTuner.setVisible(tuning);

  if (mProcessor.getModelPath() != mLastModelPath ||
      mProcessor.getIRPath() != mLastIRPath)
    recreateInnerEditor();
}

void KioskShell::recreateInnerEditor() {
  mInner = std::make_unique<NAMixAudioProcessorEditor>(mProcessor);
  mLastModelPath = mProcessor.getModelPath();
  mLastIRPath = mProcessor.getIRPath();
  addAndMakeVisible(*mInner);
  mTuner.toFront(false); // overlays always cover the panel when visible
  if (mLearnOverlay != nullptr)
    mLearnOverlay->toFront(false);
  resized();
}

void KioskShell::resized() {
  auto area = getLocalBounds();

  const bool portrait = area.getHeight() > area.getWidth();
  const int barH = juce::jlimit(96, 176, area.getHeight() / (portrait ? 8 : 5));
  mBar.setBounds(area.removeFromTop(barH));
  mTuner.setBounds(area);
  if (mLearnOverlay != nullptr)
    mLearnOverlay->setBounds(area);

  if (mInner == nullptr)
    return;

  // The NAMix editor is a fixed 600x400; scale it to fill the rest.
  const float scale = juce::jmin((float)area.getWidth() / 600.0f,
                                 (float)area.getHeight() / 400.0f);
  const float w = 600.0f * scale, h = 400.0f * scale;
  const float x = (float)area.getX() + ((float)area.getWidth() - w) * 0.5f;
  const float y = (float)area.getY() + ((float)area.getHeight() - h) * 0.5f;
  mInner->setBounds(0, 0, 600, 400);
  mInner->setTransform(juce::AffineTransform::scale(scale).translated(x, y));
}

// ---------------------------------------------------------------------------
// Standalone audio device management
// ---------------------------------------------------------------------------

void KioskShell::timerCallback() { configureAudioDevice(); }

void KioskShell::configureAudioDevice() {
#if JucePlugin_Build_Standalone
  auto *holder = juce::StandalonePluginHolder::getInstance();
  if (holder == nullptr)
    return;

  // JUCE standalones default to muting the audio input as feedback
  // protection.  This is a guitar amp: the input IS the instrument.
  holder->shouldMuteInput.setValue(false);

  if (mAudioDeviceConfigured || mConfig.audioDeviceMatch.isEmpty())
    return;

  auto &dm = holder->deviceManager;
  auto setup = dm.getAudioDeviceSetup();

  // Configured only counts if the names match AND the input is actually
  // live — a matching device with zero active input channels is still a
  // silent amp (this happens when the interface was plugged in after the
  // app scanned, or the saved setup lost its input).
  auto *current = dm.getCurrentAudioDevice();
  const bool inputLive =
      current != nullptr && !current->getActiveInputChannels().isZero();
  if (inputLive &&
      setup.inputDeviceName.containsIgnoreCase(mConfig.audioDeviceMatch) &&
      setup.outputDeviceName.containsIgnoreCase(mConfig.audioDeviceMatch)) {
    mAudioDeviceConfigured = true;
    return;
  }

  // Look for a device matching the configured name on any device type.
  for (auto *type : dm.getAvailableDeviceTypes()) {
    type->scanForDevices();
    for (const auto &name : type->getDeviceNames(false)) { // outputs
      if (!name.containsIgnoreCase(mConfig.audioDeviceMatch))
        continue;
      dm.setCurrentAudioDeviceType(type->getTypeName(), true);
      setup = dm.getAudioDeviceSetup();
      setup.outputDeviceName = name;
      // Prefer the matching input with the same name; else leave as-is.
      for (const auto &inName : type->getDeviceNames(true))
        if (inName.containsIgnoreCase(mConfig.audioDeviceMatch))
          setup.inputDeviceName = inName;
      setup.sampleRate = mConfig.sampleRate;
      setup.bufferSize = mConfig.bufferSize;
      setup.useDefaultInputChannels = true;
      setup.useDefaultOutputChannels = true;
      const auto err = dm.setAudioDeviceSetup(setup, true);
      if (err.isEmpty()) {
        juce::Logger::writeToLog("GatewayPi: audio device set to " + name);
        mAudioDeviceConfigured = true;
      } else {
        juce::Logger::writeToLog("GatewayPi: audio device error: " + err);
      }
      return;
    }
  }
#endif
}

void KioskShell::openMidiLearn() {
  if (mMidi == nullptr)
    return;
  if (mLearnOverlay == nullptr) {
    mLearnOverlay = std::make_unique<MidiLearnOverlay>(*mMidi);
    mLearnOverlay->onClose = [this] { mLearnOverlay->setVisible(false); };
    addChildComponent(*mLearnOverlay);
  }
  mLearnOverlay->refreshBindings();
  mLearnOverlay->setVisible(true);
  mLearnOverlay->toFront(false);
  resized();
}

void KioskShell::openAudioSettings() {
#if JucePlugin_Build_Standalone
  if (auto *holder = juce::StandalonePluginHolder::getInstance()) {
    // Re-arm the watchdog off: the user is taking manual control.
    mAudioDeviceConfigured = true;
    holder->showAudioSettingsDialog();
  }
#endif
}

void KioskShell::requestShutdown() {
  auto opts = juce::MessageBoxOptions()
                  .withIconType(juce::MessageBoxIconType::QuestionIcon)
                  .withTitle("Power off?")
                  .withMessage("Shut down the amp safely?")
                  .withButton("Power off")
                  .withButton("Cancel");
  juce::AlertWindow::showAsync(opts, [](int result) {
    if (result == 1) { // first button
      juce::ChildProcess p;
      // Requires the sudoers rule installed by install.sh.
      p.start("sudo -n /usr/bin/systemctl poweroff");
    }
  });
}

} // namespace gatewaypi

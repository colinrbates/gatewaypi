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
  mSettings.onClick = [this] {
    if (onOpenAudioSettings)
      onOpenAudioSettings();
  };
  mPower.onClick = [this] {
    if (onShutdown)
      onShutdown();
  };

  for (auto *b : {&mBankDown, &mBankUp, &mBypass, &mMute, &mSave, &mSettings, &mPower}) {
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
  const int n = 8; // <, label, >, BYP, MUTE, SAVE, AUDIO, OFF
  const int w = (row2.getWidth() - gap * (n - 1)) / n;
  mBankDown.setBounds(row2.removeFromLeft(w));
  row2.removeFromLeft(gap);
  mBankLabel.setBounds(row2.removeFromLeft(w));
  row2.removeFromLeft(gap);
  mBankUp.setBounds(row2.removeFromLeft(w));
  row2.removeFromLeft(gap);
  mBypass.setBounds(row2.removeFromLeft(w));
  row2.removeFromLeft(gap);
  mMute.setBounds(row2.removeFromLeft(w));
  row2.removeFromLeft(gap);
  mSave.setBounds(row2.removeFromLeft(w));
  row2.removeFromLeft(gap);
  mSettings.setBounds(row2.removeFromLeft(w));
  row2.removeFromLeft(gap);
  mPower.setBounds(row2);
}

// ---------------------------------------------------------------------------
// KioskShell
// ---------------------------------------------------------------------------

KioskShell::KioskShell(NAMixAudioProcessor &proc)
    : juce::AudioProcessorEditor(proc), mProcessor(proc),
      mConfig(Config::load()),
      mPresets(std::make_shared<PresetManager>(proc, mConfig)),
      mBar(*mPresets) {
  mPresets->onChanged = [this] { refresh(); };
  mBar.onOpenAudioSettings = [this] { openAudioSettings(); };
  mBar.onShutdown = [this] { requestShutdown(); };
  addAndMakeVisible(mBar);

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
  if (mProcessor.getModelPath() != mLastModelPath ||
      mProcessor.getIRPath() != mLastIRPath)
    recreateInnerEditor();
}

void KioskShell::recreateInnerEditor() {
  mInner = std::make_unique<NAMixAudioProcessorEditor>(mProcessor);
  mLastModelPath = mProcessor.getModelPath();
  mLastIRPath = mProcessor.getIRPath();
  addAndMakeVisible(*mInner);
  resized();
}

void KioskShell::resized() {
  auto area = getLocalBounds();

  const bool portrait = area.getHeight() > area.getWidth();
  const int barH = juce::jlimit(96, 176, area.getHeight() / (portrait ? 8 : 5));
  mBar.setBounds(area.removeFromTop(barH));

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
  if (mAudioDeviceConfigured || mConfig.audioDeviceMatch.isEmpty())
    return;
  auto *holder = juce::StandalonePluginHolder::getInstance();
  if (holder == nullptr)
    return;

  auto &dm = holder->deviceManager;
  auto setup = dm.getAudioDeviceSetup();

  if (setup.inputDeviceName.containsIgnoreCase(mConfig.audioDeviceMatch) &&
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

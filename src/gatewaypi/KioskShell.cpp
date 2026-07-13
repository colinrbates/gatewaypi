/*
 * GatewayPi appliance layer for NAMix Linux
 * Copyright (C) 2026
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 */

#include "KioskShell.h"

#include <cstdio>

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
  // Route JUCE's internal logging (incl. ALSA device diagnostics) to a file
  // so device negotiation is visible over ssh.
  if (juce::Logger::getCurrentLogger() == nullptr)
    juce::Logger::setCurrentLogger(juce::FileLogger::createDefaultAppLogger(
        "GatewayPi", "juce.log", "GatewayPi JUCE log"));

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
  if (mAudioPanel != nullptr)
    mAudioPanel->toFront(false);
  if (mConfirm != nullptr)
    mConfirm->toFront(false);
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
  if (mAudioPanel != nullptr)
    mAudioPanel->setBounds(area);
  if (mConfirm != nullptr)
    mConfirm->setBounds(area);

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

// Trace to stderr (journald / the service log) directly — bypasses any
// Logger indirection so field debugging over ssh always has eyes.
static void gpTrace(const juce::String &msg) {
  std::fprintf(stderr, "GatewayPi: %s\n", msg.toRawUTF8());
  std::fflush(stderr);
}

#if JucePlugin_Build_Standalone

// Hardware, full-duplex devices only: names present in BOTH the input and
// output lists, excluding the PulseAudio/JACK/resampler pseudo-devices.
juce::StringArray gpListDuplexDevices() {
  juce::StringArray out;
  auto *holder = juce::StandalonePluginHolder::getInstance();
  if (holder == nullptr)
    return out;
  for (auto *type : holder->deviceManager.getAvailableDeviceTypes()) {
    type->scanForDevices();
    const auto ins = type->getDeviceNames(true);
    const auto outs = type->getDeviceNames(false);
    for (const auto &in : ins) {
      if (!outs.contains(in))
        continue;
      if (in.containsIgnoreCase("pulse") || in.containsIgnoreCase("jack") ||
          in.containsIgnoreCase("converter") || in.containsIgnoreCase("plugin") ||
          in.containsIgnoreCase("default") || in.containsIgnoreCase("dmix") ||
          in.containsIgnoreCase("upmix") || in.containsIgnoreCase("downmix") ||
          in.containsIgnoreCase("surround"))
        continue;
      out.addIfNotAlreadyThere(in);
    }
  }
  return out;
}

// Open a device whose name contains `deviceNameContains` for full-duplex use
// and return the number of live input channels (0 = input failed).  Iterates
// exact input-list names so the ALSA input id is never empty, and verifies by
// reading back the opened device — the failure mode that dogged the iTwo was
// JUCE opening output-only with a mismatched input name.
int gpTryOpenDevice(const juce::String &deviceNameContains, double sampleRate,
                    int bufferSize) {
  auto *holder = juce::StandalonePluginHolder::getInstance();
  if (holder == nullptr || deviceNameContains.isEmpty())
    return 0;
  holder->shouldMuteInput.setValue(false); // this is a guitar amp
  auto &dm = holder->deviceManager;

  for (auto *type : dm.getAvailableDeviceTypes()) {
    type->scanForDevices();
    const auto ins = type->getDeviceNames(true);
    const auto outs = type->getDeviceNames(false);
    for (const auto &in : ins) {
      if (!in.containsIgnoreCase(deviceNameContains))
        continue;
      juce::String out = outs.contains(in) ? in : juce::String();
      if (out.isEmpty())
        for (const auto &o : outs)
          if (o.containsIgnoreCase(deviceNameContains)) {
            out = o;
            break;
          }

      dm.setCurrentAudioDeviceType(type->getTypeName(), true);
      auto setup = dm.getAudioDeviceSetup();
      setup.inputDeviceName = in;   // exact string from the input list
      setup.outputDeviceName = out;
      setup.sampleRate = sampleRate;
      setup.bufferSize = bufferSize;
      setup.useDefaultInputChannels = false;
      setup.inputChannels.clear();
      setup.inputChannels.setRange(0, 2, true);
      setup.useDefaultOutputChannels = false;
      setup.outputChannels.clear();
      setup.outputChannels.setRange(0, 2, true);

      const auto err = dm.setAudioDeviceSetup(setup, true);
      auto *dev = dm.getCurrentAudioDevice();
      const int liveIn =
          dev ? dev->getActiveInputChannels().countNumberOfSetBits() : 0;
      const int liveOut =
          dev ? dev->getActiveOutputChannels().countNumberOfSetBits() : 0;
      gpTrace("try type=" + type->getTypeName() + " in=\"" + in + "\" out=\"" +
              out + "\" err=\"" + err + "\" liveIn=" + juce::String(liveIn) +
              " liveOut=" + juce::String(liveOut));
      if (err.isEmpty() && liveIn > 0)
        return liveIn;
    }
  }
  return 0;
}

#endif // JucePlugin_Build_Standalone

void KioskShell::configureAudioDevice() {
#if JucePlugin_Build_Standalone
  auto *holder = juce::StandalonePluginHolder::getInstance();
  if (holder == nullptr)
    return;
  holder->shouldMuteInput.setValue(false);

  static bool announced = false;
  if (!announced) {
    announced = true;
    gpTrace("watchdog live; deviceMatch=\"" + mConfig.audioDeviceMatch +
            "\" duplexDevices=[" + gpListDuplexDevices().joinIntoString(" | ") +
            "]");
  }

  if (mAudioDeviceConfigured || mConfig.audioDeviceMatch.isEmpty())
    return;

  auto *current = holder->deviceManager.getCurrentAudioDevice();
  if (current != nullptr && !current->getActiveInputChannels().isZero() &&
      current->getName().containsIgnoreCase(mConfig.audioDeviceMatch)) {
    gpTrace("device OK: " + current->getName());
    mAudioDeviceConfigured = true;
    return;
  }

  if (++mConfigureAttempts > 15) {
    gpTrace("giving up on auto audio config; use the AUDIO button");
    mAudioDeviceConfigured = true;
    return;
  }

  const int liveIn = gpTryOpenDevice(mConfig.audioDeviceMatch,
                                     mConfig.sampleRate, mConfig.bufferSize);
  if (liveIn > 0) {
    gpTrace("input live (" + juce::String(liveIn) + " ch)");
    mAudioDeviceConfigured = true;
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
  // The user is taking manual control — stop the watchdog fighting them.
  mAudioDeviceConfigured = true;
  if (mAudioPanel == nullptr) {
    mAudioPanel = std::make_unique<AudioPanel>(mConfig);
    mAudioPanel->onClose = [this] { mAudioPanel->setVisible(false); };
    addChildComponent(*mAudioPanel);
  }
  mAudioPanel->refreshDevices();
  mAudioPanel->setVisible(true);
  mAudioPanel->toFront(false);
  resized();
#endif
}

void KioskShell::requestShutdown() {
  if (mConfirm == nullptr) {
    mConfirm = std::make_unique<ConfirmOverlay>("Power off the amp?", "POWER OFF");
    mConfirm->onCancel = [this] { mConfirm->setVisible(false); };
    mConfirm->onYes = [this] {
      mConfirm->setVisible(false);
      juce::ChildProcess p; // needs the sudoers rule from install.sh
      p.start("sudo -n /usr/bin/systemctl poweroff");
    };
    addChildComponent(*mConfirm);
  }
  mConfirm->setVisible(true);
  mConfirm->toFront(false);
  resized();
}

// ---------------------------------------------------------------------------
// ConfirmOverlay
// ---------------------------------------------------------------------------

ConfirmOverlay::ConfirmOverlay(const juce::String &title,
                               const juce::String &yesLabel)
    : mTitle(title), mYes(yesLabel) {
  mYes.setColour(juce::TextButton::buttonColourId, colours::warn);
  mYes.setColour(juce::TextButton::textColourOffId, colours::text);
  mYes.onClick = [this] {
    if (onYes)
      onYes();
  };
  mCancel.setColour(juce::TextButton::buttonColourId, colours::idle);
  mCancel.setColour(juce::TextButton::textColourOffId, colours::text);
  mCancel.onClick = [this] {
    if (onCancel)
      onCancel();
  };
  addAndMakeVisible(mYes);
  addAndMakeVisible(mCancel);
}

void ConfirmOverlay::paint(juce::Graphics &g) {
  g.fillAll(juce::Colour(0xf2141517));
  g.setColour(colours::text);
  g.setFont(juce::FontOptions((float)getHeight() * 0.08f, juce::Font::bold));
  g.drawText(mTitle, getLocalBounds().removeFromTop(getHeight() / 2),
             juce::Justification::centred);
}

void ConfirmOverlay::resized() {
  auto area = getLocalBounds().reduced(getWidth() / 8);
  auto row = area.removeFromBottom(juce::jmax(64, area.getHeight() / 3));
  const int gap = 16;
  const int w = (row.getWidth() - gap) / 2;
  mCancel.setBounds(row.removeFromLeft(w));
  mYes.setBounds(row.removeFromRight(w));
}

// ---------------------------------------------------------------------------
// AudioPanel
// ---------------------------------------------------------------------------

AudioPanel::AudioPanel(const Config &config) : mConfig(config) {
  mTitle.setText("Audio", juce::dontSendNotification);
  mTitle.setJustificationType(juce::Justification::centred);
  mTitle.setColour(juce::Label::textColourId, colours::active);
  addAndMakeVisible(mTitle);

  mStatus.setJustificationType(juce::Justification::centred);
  mStatus.setColour(juce::Label::textColourId, colours::text);
  addAndMakeVisible(mStatus);

  for (auto *hdr : {&mDeviceHeader, &mBufHeader, &mRateHeader})
    hdr->setColour(juce::Label::textColourId, colours::text);
  mDeviceHeader.setText("Interface", juce::dontSendNotification);
  mBufHeader.setText("Buffer (latency)", juce::dontSendNotification);
  mRateHeader.setText("Sample rate", juce::dontSendNotification);
  addAndMakeVisible(mDeviceHeader);
  addAndMakeVisible(mBufHeader);
  addAndMakeVisible(mRateHeader);

  const char *bufLabels[] = {"64", "128", "256"};
  for (size_t i = 0; i < mBufButtons.size(); ++i) {
    auto &b = mBufButtons[i];
    b.setButtonText(bufLabels[i]);
    b.setColour(juce::TextButton::buttonColourId, colours::idle);
    b.setColour(juce::TextButton::buttonOnColourId, colours::active);
    b.setColour(juce::TextButton::textColourOffId, colours::text);
    b.setColour(juce::TextButton::textColourOnId, colours::bg);
    b.setClickingTogglesState(false);
    b.onClick = [this] { applyRateBuffer(); };
    addAndMakeVisible(b);
  }
  const char *rateLabels[] = {"44100", "48000"};
  for (size_t i = 0; i < mRateButtons.size(); ++i) {
    auto &b = mRateButtons[i];
    b.setButtonText(rateLabels[i]);
    b.setColour(juce::TextButton::buttonColourId, colours::idle);
    b.setColour(juce::TextButton::buttonOnColourId, colours::active);
    b.setColour(juce::TextButton::textColourOffId, colours::text);
    b.setColour(juce::TextButton::textColourOnId, colours::bg);
    b.setClickingTogglesState(false);
    b.onClick = [this] { applyRateBuffer(); };
    addAndMakeVisible(b);
  }
  mClose.setColour(juce::TextButton::buttonColourId, colours::active);
  mClose.setColour(juce::TextButton::textColourOffId, colours::bg);
  mClose.onClick = [this] {
    if (onClose)
      onClose();
  };
  addAndMakeVisible(mClose);

  refreshDevices();
  startTimerHz(2); // keep the status line honest
}

void AudioPanel::rebuildDeviceButtons() {
#if JucePlugin_Build_Standalone
  mDeviceButtons.clear();
  for (const auto &name : gpListDuplexDevices()) {
    auto *b = new juce::TextButton(name);
    b->setColour(juce::TextButton::buttonColourId, colours::idle);
    b->setColour(juce::TextButton::buttonOnColourId, colours::active);
    b->setColour(juce::TextButton::textColourOffId, colours::text);
    b->setColour(juce::TextButton::textColourOnId, colours::bg);
    b->onClick = [this, name] { openDevice(name); };
    addAndMakeVisible(b);
    mDeviceButtons.add(b);
  }
  resized();
#endif
}

void AudioPanel::refreshDevices() { rebuildDeviceButtons(); }

void AudioPanel::openDevice(const juce::String &name) {
#if JucePlugin_Build_Standalone
  mSelectedName = name;
  const int buf = mConfig.bufferSize;
  const int liveIn = gpTryOpenDevice(name, mConfig.sampleRate, buf);
  gpTrace("panel openDevice \"" + name + "\" -> liveIn=" + juce::String(liveIn));
  timerCallback();
#endif
}

void AudioPanel::applyRateBuffer() {
#if JucePlugin_Build_Standalone
  // Read the chosen toggles and re-open the current device with them.
  auto *holder = juce::StandalonePluginHolder::getInstance();
  if (holder == nullptr)
    return;
  auto *dev = holder->deviceManager.getCurrentAudioDevice();
  juce::String name = mSelectedName.isNotEmpty()
                          ? mSelectedName
                          : (dev ? dev->getName() : juce::String());
  if (name.isEmpty())
    return;
  int buf = 128;
  for (auto &b : mBufButtons)
    if (b.getToggleState())
      buf = b.getButtonText().getIntValue();
  double rate = 48000.0;
  for (auto &b : mRateButtons)
    if (b.getToggleState())
      rate = b.getButtonText().getDoubleValue();
  gpTryOpenDevice(name, rate, buf);
  timerCallback();
#endif
}

void AudioPanel::timerCallback() {
#if JucePlugin_Build_Standalone
  auto *holder = juce::StandalonePluginHolder::getInstance();
  auto *dev = holder ? holder->deviceManager.getCurrentAudioDevice() : nullptr;
  if (dev == nullptr) {
    mStatus.setText("No device open", juce::dontSendNotification);
    return;
  }
  const int in = dev->getActiveInputChannels().countNumberOfSetBits();
  const int out = dev->getActiveOutputChannels().countNumberOfSetBits();
  const int buf = dev->getCurrentBufferSizeSamples();
  const double sr = dev->getCurrentSampleRate();
  const float latency = sr > 0 ? (float)buf / (float)sr * 1000.0f : 0.0f;
  mStatus.setText(dev->getName() + juce::String::fromUTF8("  \xe2\x80\x94  in ") +
                      juce::String(in) + " / out " + juce::String(out) + "  " +
                      juce::String(sr / 1000.0, 1) + "kHz  " + juce::String(buf) +
                      " smp (" + juce::String(latency, 1) + " ms" +
                      (in > 0 ? ")" : ") — NO INPUT"),
                  juce::dontSendNotification);

  for (auto &b : mBufButtons)
    b.setToggleState(b.getButtonText().getIntValue() == buf,
                     juce::dontSendNotification);
  for (auto &b : mRateButtons)
    b.setToggleState((double)b.getButtonText().getIntValue() == sr,
                     juce::dontSendNotification);
  for (auto *b : mDeviceButtons)
    b->setToggleState(dev->getName().containsIgnoreCase(b->getButtonText()) ||
                          b->getButtonText().containsIgnoreCase(dev->getName()),
                      juce::dontSendNotification);
#endif
}

void AudioPanel::paint(juce::Graphics &g) { g.fillAll(juce::Colour(0xf2141517)); }

void AudioPanel::resized() {
  auto area = getLocalBounds().reduced(juce::jmax(12, getWidth() / 20));
  const int line = juce::jmax(28, getHeight() / 16);
  const int gap = 8;
  mTitle.setBounds(area.removeFromTop(line));
  mStatus.setBounds(area.removeFromTop(line));
  area.removeFromTop(gap);

  mClose.setBounds(area.removeFromBottom(juce::jmax(48, line * 2)));
  area.removeFromBottom(gap);

  mDeviceHeader.setBounds(area.removeFromTop(line));
  for (auto *b : mDeviceButtons) {
    b->setBounds(area.removeFromTop(juce::jmax(40, line + 12)));
    area.removeFromTop(4);
  }
  area.removeFromTop(gap);

  mBufHeader.setBounds(area.removeFromTop(line));
  {
    auto row = area.removeFromTop(juce::jmax(44, line + 14));
    const int w = (row.getWidth() - gap * 2) / 3;
    for (auto &b : mBufButtons) {
      b.setBounds(row.removeFromLeft(w));
      row.removeFromLeft(gap);
    }
  }
  area.removeFromTop(gap);

  mRateHeader.setBounds(area.removeFromTop(line));
  {
    auto row = area.removeFromTop(juce::jmax(44, line + 14));
    const int w = (row.getWidth() - gap) / 2;
    for (auto &b : mRateButtons) {
      b.setBounds(row.removeFromLeft(w));
      row.removeFromLeft(gap);
    }
  }
}

} // namespace gatewaypi

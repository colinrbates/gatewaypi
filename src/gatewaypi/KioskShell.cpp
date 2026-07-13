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
const juce::Colour signalBlue{0xff3f6273}; // cable/steel blue
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
    b.onLongPress = [this, i] {
      mPresets.selectSlot(i);
      if (onRenameSlot)
        onRenameSlot(i);
    };
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
  mBar.onRenameSlot = [this](int slot) { openRename(slot); };
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
  mDump = std::make_unique<DebugDump>(proc);

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
  // the overlay is up.  (Don't fight the debug dump, which borrows the tap.)
  const bool tuning = mPresets->getMute();
  if (mDump == nullptr || !mDump->isActive())
    mProcessor.gpSetTunerTap(tuning);
  if (tuning)
    hideOverlaysExcept(&mTuner); // tuner takes over from any open panel
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
  if (mKeyboard != nullptr)
    mKeyboard->toFront(false);
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
  if (mKeyboard != nullptr)
    mKeyboard->setBounds(area);

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
// True when jackd is running and JUCE sees a JACK device — in that mode
// jackd owns the interface and the raw ALSA hw devices must NOT be opened
// (doing so steals the card from jackd and kills the audio).
bool gpJackAvailable() {
  auto *holder = juce::StandalonePluginHolder::getInstance();
  if (holder == nullptr)
    return false;
  for (auto *type : holder->deviceManager.getAvailableDeviceTypes())
    if (type->getTypeName() == "JACK") {
      type->scanForDevices();
      if (!type->getDeviceNames(false).isEmpty())
        return true;
    }
  return false;
}

juce::StringArray gpListDuplexDevices() {
  juce::StringArray out;
  auto *holder = juce::StandalonePluginHolder::getInstance();
  if (holder == nullptr)
    return out;

  // When jackd owns the interface, the only valid choice is the JACK device.
  // Listing the raw ALSA hw device here is a trap: selecting it yanks the
  // card from jackd and drops the audio.
  if (gpJackAvailable()) {
    for (auto *type : holder->deviceManager.getAvailableDeviceTypes())
      if (type->getTypeName() == "JACK")
        for (const auto &n : type->getDeviceNames(false))
          out.addIfNotAlreadyThere(n);
    return out;
  }

  static const char *reject[] = {
      "pulse", "jack",       "converter", "plugin",     "default",
      "dmix",  "upmix",      "downmix",   "surround",   "hdmi",
      "stream output", "open sound", "snooping", "iec958", "front output"};
  juce::StringArray seenCards;
  for (auto *type : holder->deviceManager.getAvailableDeviceTypes()) {
    type->scanForDevices();
    const auto ins = type->getDeviceNames(true);
    const auto outs = type->getDeviceNames(false);
    for (const auto &in : ins) {
      if (!outs.contains(in))
        continue;
      bool bad = false;
      for (auto *r : reject)
        if (in.containsIgnoreCase(r)) {
          bad = true;
          break;
        }
      if (bad)
        continue;
      // One entry per physical card (dedupe on the card name before the
      // ';'/',' PCM descriptor).
      const juce::String card = in.upToFirstOccurrenceOf(";", false, false)
                                    .upToLastOccurrenceOf(",", false, false)
                                    .trim();
      if (seenCards.contains(card))
        continue;
      seenCards.add(card);
      out.add(in);
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
                    int bufferSize, int inputChannelMask) {
  auto *holder = juce::StandalonePluginHolder::getInstance();
  if (holder == nullptr || deviceNameContains.isEmpty())
    return 0;
  holder->shouldMuteInput.setValue(false); // this is a guitar amp
  auto &dm = holder->deviceManager;
  if (inputChannelMask == 0)
    inputChannelMask = 3;

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
      // Debug: GATEWAYPI_NO_INPUT opens output-only, to isolate whether the
      // warble/crackle is JUCE's full-duplex (linked read+write) path.
      const bool noInput = std::getenv("GATEWAYPI_NO_INPUT") != nullptr;
      if (noInput)
        setup.inputDeviceName = {};
      setup.sampleRate = sampleRate;
      setup.bufferSize = bufferSize;
      setup.useDefaultInputChannels = false;
      setup.inputChannels.clear();
      if (inputChannelMask & 1)
        setup.inputChannels.setBit(0);
      if (inputChannelMask & 2)
        setup.inputChannels.setBit(1);
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

// Open the JACK device (jackd owns the interface and does full-duplex
// clocking properly, which raw ALSA on the iTwo does not). Returns live
// input channel count. JUCE auto-connects its ports to the physical I/O.
int gpTryOpenJack() {
  auto *holder = juce::StandalonePluginHolder::getInstance();
  if (holder == nullptr)
    return 0;
  holder->shouldMuteInput.setValue(false);
  auto &dm = holder->deviceManager;
  for (auto *type : dm.getAvailableDeviceTypes()) {
    if (type->getTypeName() != "JACK")
      continue;
    type->scanForDevices();
    const auto outs = type->getDeviceNames(false);
    const auto ins = type->getDeviceNames(true);
    if (outs.isEmpty())
      return 0; // jackd not running yet
    dm.setCurrentAudioDeviceType("JACK", true);
    auto setup = dm.getAudioDeviceSetup();
    setup.outputDeviceName = outs[0];
    setup.inputDeviceName = ins.isEmpty() ? outs[0] : ins[0];
    setup.useDefaultInputChannels = true;
    setup.useDefaultOutputChannels = true;
    const auto err = dm.setAudioDeviceSetup(setup, true);
    auto *dev = dm.getCurrentAudioDevice();
    const int liveIn =
        dev ? dev->getActiveInputChannels().countNumberOfSetBits() : 0;
    const int liveOut =
        dev ? dev->getActiveOutputChannels().countNumberOfSetBits() : 0;
    gpTrace("JACK open: out=\"" + setup.outputDeviceName + "\" in=\"" +
            setup.inputDeviceName + "\" err=\"" + err + "\" liveIn=" +
            juce::String(liveIn) + " liveOut=" + juce::String(liveOut));
    return err.isEmpty() ? liveIn : 0;
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

  if (mConfig.audioDeviceMatch.isEmpty())
    return;

  // Already good? Nothing to do. (Checked every tick so we also notice the
  // interface disappearing — e.g. a USB glitch — and re-open it.)
  auto *current = holder->deviceManager.getCurrentAudioDevice();
  if (current != nullptr && !current->getActiveInputChannels().isZero() &&
      current->getName().containsIgnoreCase(mConfig.audioDeviceMatch)) {
    if (!mAudioDeviceConfigured)
      gpTrace("device OK: " + current->getName());
    mAudioDeviceConfigured = true;
    return;
  }

  // The user took manual control via the AUDIO panel — don't fight them.
  if (mUserSetAudio)
    return;

  // Never permanently give up: jackd may still be starting, or the
  // interface may be plugged in after boot. Keep trying, log occasionally.
  mAudioDeviceConfigured = false;
  const bool loud = (mConfigureAttempts++ % 10) == 0;

  // Prefer JACK (jackd owns the interface and clocks full-duplex cleanly);
  // fall back to raw ALSA only if JACK isn't available.
  int liveIn = gpTryOpenJack();
  if (liveIn <= 0)
    liveIn = gpTryOpenDevice(mConfig.audioDeviceMatch, mConfig.sampleRate,
                             mConfig.bufferSize);
  if (liveIn > 0) {
    gpTrace("input live (" + juce::String(liveIn) + " ch)");
    mAudioDeviceConfigured = true;
  } else if (loud) {
    gpTrace("waiting for JACK / interface \"" + mConfig.audioDeviceMatch +
            "\"...");
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
  hideOverlaysExcept(mLearnOverlay.get());
  mLearnOverlay->refreshBindings();
  mLearnOverlay->setVisible(true);
  mLearnOverlay->toFront(false);
  resized();
}

void KioskShell::openAudioSettings() {
#if JucePlugin_Build_Standalone
  // The user is taking manual control — stop the watchdog fighting them.
  mUserSetAudio = true;
  if (mAudioPanel == nullptr) {
    mAudioPanel = std::make_unique<AudioPanel>(mConfig);
    mAudioPanel->onClose = [this] { mAudioPanel->setVisible(false); };
    addChildComponent(*mAudioPanel);
  }
  hideOverlaysExcept(mAudioPanel.get());
  mAudioPanel->refreshDevices();
  mAudioPanel->setVisible(true);
  mAudioPanel->toFront(false);
  resized();
#endif
}

void KioskShell::openRename(int slot) {
  if (mKeyboard == nullptr) {
    mKeyboard = std::make_unique<KeyboardOverlay>();
    mKeyboard->onCancel = [this] { mKeyboard->setVisible(false); };
    mKeyboard->onAccept = [this](juce::String name) {
      mPresets->setCurrentSlotName(name);
      mKeyboard->setVisible(false);
    };
    addChildComponent(*mKeyboard);
  }
  const juce::String cur =
      mPresets->isSlotOccupied(slot) ? mPresets->getSlotName(slot) : juce::String();
  mKeyboard->setPrompt("Name preset " + juce::String(slot + 1),
                       cur == "-" ? juce::String() : cur);
  hideOverlaysExcept(mKeyboard.get());
  mKeyboard->setVisible(true);
  mKeyboard->toFront(false);
  resized();
}

void KioskShell::requestShutdown() {
  if (mConfirm == nullptr) {
    mConfirm = std::make_unique<ConfirmOverlay>("Power", "POWER OFF", "REBOOT");
    mConfirm->onCancel = [this] { mConfirm->setVisible(false); };
    mConfirm->onPrimary = [this] {
      mConfirm->setVisible(false);
      juce::ChildProcess p; // needs the sudoers rule from install.sh
      p.start("sudo -n /usr/bin/systemctl poweroff");
    };
    mConfirm->onSecondary = [this] {
      mConfirm->setVisible(false);
      juce::ChildProcess p;
      p.start("sudo -n /usr/bin/systemctl reboot");
    };
    addChildComponent(*mConfirm);
  }
  hideOverlaysExcept(mConfirm.get());
  mConfirm->setVisible(true);
  mConfirm->toFront(false);
  resized();
}

// Only one overlay is up at a time — opening one dismisses the others (and
// leaving the tuner requires unmuting, which the caller handles).
void KioskShell::hideOverlaysExcept(juce::Component *keep) {
  for (juce::Component *c : {(juce::Component *)mLearnOverlay.get(),
                             (juce::Component *)mAudioPanel.get(),
                             (juce::Component *)mConfirm.get(),
                             (juce::Component *)mKeyboard.get(),
                             (juce::Component *)&mTuner})
    if (c != nullptr && c != keep && c->isVisible())
      c->setVisible(false);
  // The tuner is driven by the mute flag; clear it when leaving the tuner.
  if (keep != &mTuner && mPresets->getMute())
    mProcessor.gpSetMute(false);
}

// ---------------------------------------------------------------------------
// DebugDump — ssh-triggered raw signal capture for field analysis
// ---------------------------------------------------------------------------

void KioskShell::DebugDump::timerCallback() {
  const juce::File req("/tmp/gatewaypi-dump-request");
  if (mIn == nullptr) {
    if (!req.existsAsFile())
      return;
    req.deleteFile();
    mIn = std::fopen("/tmp/gp-in.f32", "wb");
    mOut = std::fopen("/tmp/gp-out.f32", "wb");
    mProc.gpSetTunerTap(true);
    mProc.gpSetOutTap(true);
    mProc.gpSetInjectSine(true); // deterministic input — no playing needed
    gpTrace("dump: preparedBlock=" + juce::String(mProc.gpPreparedBlock()) +
            " maxSeenBlock=" + juce::String(mProc.gpMaxSeenBlock()));
    mTicksLeft = 8 * 50; // ~8 s at 50 Hz
    stopTimer();
    startTimerHz(50);
    gpTrace("debug dump started");
    return;
  }

  float buf[4096];
  for (;;) {
    const int n = mProc.gpReadTunerSamples(buf, 4096);
    if (n <= 0)
      break;
    std::fwrite(buf, sizeof(float), (size_t)n, mIn);
  }
  for (;;) {
    const int n = mProc.gpReadOutSamples(buf, 4096);
    if (n <= 0)
      break;
    std::fwrite(buf, sizeof(float), (size_t)n, mOut);
  }
  if (--mTicksLeft <= 0)
    finish();
}

void KioskShell::DebugDump::finish() {
  if (mIn == nullptr)
    return;
  mProc.gpSetTunerTap(false);
  mProc.gpSetOutTap(false);
  mProc.gpSetInjectSine(false);
  std::fclose(mIn);
  std::fclose(mOut);
  mIn = mOut = nullptr;
  juce::File("/tmp/gatewaypi-dump-done").create();
  stopTimer();
  startTimer(1000);
  gpTrace("debug dump finished");
}

// ---------------------------------------------------------------------------
// ConfirmOverlay
// ---------------------------------------------------------------------------

ConfirmOverlay::ConfirmOverlay(const juce::String &title,
                               const juce::String &primaryLabel,
                               const juce::String &secondaryLabel)
    : mTitle(title), mPrimary(primaryLabel), mSecondary(secondaryLabel) {
  mHasSecondary = secondaryLabel.isNotEmpty();
  mPrimary.setColour(juce::TextButton::buttonColourId, colours::warn);
  mPrimary.setColour(juce::TextButton::textColourOffId, colours::text);
  mPrimary.onClick = [this] {
    if (onPrimary)
      onPrimary();
  };
  addAndMakeVisible(mPrimary);
  if (mHasSecondary) {
    mSecondary.setColour(juce::TextButton::buttonColourId, colours::signalBlue);
    mSecondary.setColour(juce::TextButton::textColourOffId, colours::text);
    mSecondary.onClick = [this] {
      if (onSecondary)
        onSecondary();
    };
    addAndMakeVisible(mSecondary);
  }
  mCancel.setColour(juce::TextButton::buttonColourId, colours::idle);
  mCancel.setColour(juce::TextButton::textColourOffId, colours::text);
  mCancel.onClick = [this] {
    if (onCancel)
      onCancel();
  };
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
  const int gap = 12;
  const int n = mHasSecondary ? 3 : 2;
  const int w = (row.getWidth() - gap * (n - 1)) / n;
  mCancel.setBounds(row.removeFromLeft(w));
  row.removeFromLeft(gap);
  if (mHasSecondary) {
    mSecondary.setBounds(row.removeFromLeft(w));
    row.removeFromLeft(gap);
  }
  mPrimary.setBounds(row.removeFromLeft(w));
}

// ---------------------------------------------------------------------------
// AudioPanel
// ---------------------------------------------------------------------------

static void styleChoice(juce::TextButton &b) {
  b.setColour(juce::TextButton::buttonColourId, colours::idle);
  b.setColour(juce::TextButton::buttonOnColourId, colours::active);
  b.setColour(juce::TextButton::textColourOffId, colours::text);
  b.setColour(juce::TextButton::textColourOnId, colours::bg);
  b.setClickingTogglesState(false);
}

AudioPanel::AudioPanel(const Config &config) : mConfig(config) {
  mBufSel = mConfig.bufferSize;

  mTitle.setText("Audio Setup", juce::dontSendNotification);
  mTitle.setJustificationType(juce::Justification::centred);
  mTitle.setColour(juce::Label::textColourId, colours::active);
  mTitle.setFont(juce::FontOptions(22.0f, juce::Font::bold));
  addAndMakeVisible(mTitle);

  mStatus.setJustificationType(juce::Justification::centred);
  mStatus.setColour(juce::Label::textColourId, colours::text);
  addAndMakeVisible(mStatus);

  struct HdrInit { juce::Label *l; const char *t; };
  for (auto h : {HdrInit{&mDeviceHeader, "INTERFACE"},
                 HdrInit{&mInputHeader, "GUITAR INPUT"},
                 HdrInit{&mBufHeader, "BUFFER  (LOWER = LESS LATENCY)  \xc2\xb7  48 kHz"}}) {
    h.l->setText(juce::String::fromUTF8(h.t), juce::dontSendNotification);
    h.l->setColour(juce::Label::textColourId, colours::active);
    h.l->setFont(juce::FontOptions(13.0f, juce::Font::bold));
    addAndMakeVisible(*h.l);
  }

  const char *inLabels[] = {"IN 1", "IN 2", "BOTH"};
  const int inMasks[] = {1, 2, 3};
  for (size_t i = 0; i < mInputButtons.size(); ++i) {
    auto &b = mInputButtons[i];
    b.setButtonText(inLabels[i]);
    styleChoice(b);
    const int mask = inMasks[i];
    b.onClick = [this, mask] {
      mInputMask = mask;
      reopen();
    };
    addAndMakeVisible(b);
  }

  const char *bufLabels[] = {"64", "128", "256"};
  for (size_t i = 0; i < mBufButtons.size(); ++i) {
    auto &b = mBufButtons[i];
    b.setButtonText(bufLabels[i]);
    styleChoice(b);
    const int v = juce::String(bufLabels[i]).getIntValue();
    b.onClick = [this, v] {
      mBufSel = v;
      reopen();
    };
    addAndMakeVisible(b);
  }

  mTest.setColour(juce::TextButton::buttonColourId, colours::idle);
  mTest.setColour(juce::TextButton::textColourOffId, colours::text);
  mTest.onClick = [] {
#if JucePlugin_Build_Standalone
    if (auto *h = juce::StandalonePluginHolder::getInstance())
      h->deviceManager.playTestSound();
#endif
  };
  addAndMakeVisible(mTest);

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
  const bool jack = gpJackAvailable();
  for (const auto &name : gpListDuplexDevices()) {
    // Show the friendly card name (before the first ';'/','), not the raw
    // ALSA hint — but key actions off the full name.
    juce::String label = name.upToFirstOccurrenceOf(";", false, false)
                             .upToLastOccurrenceOf(",", false, false)
                             .trim();
    if (label.isEmpty())
      label = name;
    if (jack) // the JACK graph device ("system") — name it for what it is
      label = juce::String::fromUTF8("JACK \xe2\x80\x94 ") + mConfig.audioDeviceMatch;
    auto *b = new juce::TextButton(label);
    styleChoice(*b);
    b->onClick = [this, name] {
      mSelectedName = name;
      reopen();
    };
    addAndMakeVisible(b);
    mDeviceButtons.add(b);
  }
  resized();
#endif
}

void AudioPanel::refreshDevices() { rebuildDeviceButtons(); }

void AudioPanel::reopen() {
#if JucePlugin_Build_Standalone
  auto *holder = juce::StandalonePluginHolder::getInstance();
  if (holder == nullptr)
    return;

  // Under JACK, jackd owns the device: (re)connect through the JACK path and
  // never touch raw ALSA (which would steal the card and drop the audio).
  if (gpJackAvailable()) {
    const int liveIn = gpTryOpenJack();
    gpTrace("panel reopen via JACK -> liveIn=" + juce::String(liveIn));
    timerCallback();
    return;
  }

  auto *dev = holder->deviceManager.getCurrentAudioDevice();
  juce::String name = mSelectedName.isNotEmpty()
                          ? mSelectedName
                          : (dev ? dev->getName() : mConfig.audioDeviceMatch);
  if (name.isEmpty())
    return;
  const int liveIn = gpTryOpenDevice(name, kRate, mBufSel, mInputMask);
  gpTrace("panel reopen \"" + name + "\" buf=" + juce::String(mBufSel) +
          " inMask=" + juce::String(mInputMask) + " -> liveIn=" +
          juce::String(liveIn));
  timerCallback();
#endif
}

void AudioPanel::timerCallback() {
#if JucePlugin_Build_Standalone
  auto *holder = juce::StandalonePluginHolder::getInstance();
  auto *dev = holder ? holder->deviceManager.getCurrentAudioDevice() : nullptr;
  if (dev == nullptr) {
    mStatus.setText("No device open — pick your interface below",
                    juce::dontSendNotification);
    return;
  }
  const int in = dev->getActiveInputChannels().countNumberOfSetBits();
  const int out = dev->getActiveOutputChannels().countNumberOfSetBits();
  const int buf = dev->getCurrentBufferSizeSamples();
  const double sr = dev->getCurrentSampleRate();
  const float latency = sr > 0 ? (float)buf / (float)sr * 1000.0f : 0.0f;
  mStatus.setText(juce::String(in > 0 ? juce::String::fromUTF8("\xe2\x97\x8f live")
                                       : juce::String("NO INPUT")) +
                      "   in " + juce::String(in) + " / out " + juce::String(out) +
                      "   " + juce::String(sr / 1000.0, 1) + " kHz   " +
                      juce::String(buf) + " smp   " + juce::String(latency, 1) +
                      " ms round trip",
                  juce::dontSendNotification);
  mStatus.setColour(juce::Label::textColourId,
                    in > 0 ? juce::Colour(0xff5abf6e) : colours::warn);

  // Buffer and input-channel routing are owned by jackd when on JACK, so
  // grey those controls out and say so rather than letting them break audio.
  const bool jack = gpJackAvailable();
  mBufHeader.setText(jack ? juce::String("BUFFER / RATE  \xc2\xb7  managed by JACK")
                          : juce::String::fromUTF8(
                                "BUFFER  (LOWER = LESS LATENCY)  \xc2\xb7  48 kHz"),
                     juce::dontSendNotification);
  for (auto &b : mBufButtons) {
    b.setEnabled(!jack);
    b.setToggleState(!jack && b.getButtonText().getIntValue() == buf,
                     juce::dontSendNotification);
  }
  for (auto &b : mInputButtons)
    b.setEnabled(!jack);
  // Reflect the user's chosen input routing, not the device's raw active
  // channel mask (JUCE may open both capture channels regardless).
  mInputButtons[0].setToggleState(!jack && mInputMask == 1, juce::dontSendNotification);
  mInputButtons[1].setToggleState(!jack && mInputMask == 2, juce::dontSendNotification);
  mInputButtons[2].setToggleState(!jack && mInputMask == 3, juce::dontSendNotification);
  for (auto *b : mDeviceButtons)
    b->setToggleState(jack ? in > 0 // the single JACK device, lit when live
                           : dev->getName().containsIgnoreCase(b->getButtonText()),
                      juce::dontSendNotification);
#endif
}

void AudioPanel::paint(juce::Graphics &g) {
  g.fillAll(juce::Colour(0xf5141517));
  // Framed card for a less "raw" feel.
  g.setColour(juce::Colour(0x22ffffff));
  g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(6.0f), 10.0f, 1.0f);
}

void AudioPanel::resized() {
  auto area = getLocalBounds().reduced(juce::jmax(14, getWidth() / 18));
  const int line = juce::jlimit(26, 40, getHeight() / 18);
  const int rowH = juce::jlimit(40, 60, getHeight() / 13);
  const int gap = 8;

  mTitle.setBounds(area.removeFromTop(line + 6));
  mStatus.setBounds(area.removeFromTop(line));
  area.removeFromTop(gap);

  auto footer = area.removeFromBottom(juce::jmax(48, rowH));
  {
    const int w = (footer.getWidth() - gap) / 2;
    mTest.setBounds(footer.removeFromLeft(w));
    mClose.setBounds(footer.removeFromRight(w));
  }
  area.removeFromBottom(gap);

  auto rowOf = [&](std::initializer_list<juce::TextButton *> btns) {
    auto row = area.removeFromTop(rowH);
    const int n = (int)btns.size();
    const int w = (row.getWidth() - gap * (n - 1)) / n;
    for (auto *b : btns) {
      b->setBounds(row.removeFromLeft(w));
      row.removeFromLeft(gap);
    }
    area.removeFromTop(gap);
  };

  mDeviceHeader.setBounds(area.removeFromTop(line));
  for (auto *b : mDeviceButtons) {
    b->setBounds(area.removeFromTop(rowH));
    area.removeFromTop(4);
  }
  area.removeFromTop(gap);

  mInputHeader.setBounds(area.removeFromTop(line));
  rowOf({&mInputButtons[0], &mInputButtons[1], &mInputButtons[2]});

  mBufHeader.setBounds(area.removeFromTop(line));
  rowOf({&mBufButtons[0], &mBufButtons[1], &mBufButtons[2]});
}

// ---------------------------------------------------------------------------
// KeyboardOverlay
// ---------------------------------------------------------------------------

KeyboardOverlay::KeyboardOverlay() {
  mPromptLabel.setJustificationType(juce::Justification::centred);
  mPromptLabel.setColour(juce::Label::textColourId, colours::active);
  mPromptLabel.setFont(juce::FontOptions(18.0f, juce::Font::bold));
  addAndMakeVisible(mPromptLabel);

  mTextLabel.setJustificationType(juce::Justification::centred);
  mTextLabel.setColour(juce::Label::textColourId, colours::text);
  mTextLabel.setColour(juce::Label::backgroundColourId, juce::Colour(0xff26282c));
  mTextLabel.setFont(juce::FontOptions(28.0f, juce::Font::bold));
  addAndMakeVisible(mTextLabel);

  mCancel.setColour(juce::TextButton::buttonColourId, colours::idle);
  mCancel.setColour(juce::TextButton::textColourOffId, colours::text);
  mCancel.onClick = [this] {
    if (onCancel)
      onCancel();
  };
  addAndMakeVisible(mCancel);

  mAccept.setColour(juce::TextButton::buttonColourId, colours::active);
  mAccept.setColour(juce::TextButton::textColourOffId, colours::bg);
  mAccept.onClick = [this] {
    if (onAccept)
      onAccept(mText.trim());
  };
  addAndMakeVisible(mAccept);

  rebuildKeys();
}

void KeyboardOverlay::setPrompt(const juce::String &prompt,
                                const juce::String &initialText) {
  mPrompt = prompt;
  mText = initialText;
  mShift = mText.isEmpty(); // capitalise the first letter of a fresh name
  mPromptLabel.setText(prompt, juce::dontSendNotification);
  rebuildKeys();
  refreshDisplay();
}

void KeyboardOverlay::refreshDisplay() {
  mTextLabel.setText(mText + juce::String::fromUTF8("\xe2\x96\x8c"), // caret
                     juce::dontSendNotification);
}

void KeyboardOverlay::addKey(const juce::String &cap,
                             std::function<void()> action) {
  auto *b = new juce::TextButton(cap);
  b->setColour(juce::TextButton::buttonColourId, colours::idle);
  b->setColour(juce::TextButton::textColourOffId, colours::text);
  b->onClick = [this, action = std::move(action)] {
    action();
    refreshDisplay();
  };
  addAndMakeVisible(b);
  mKeys.add(b);
}

void KeyboardOverlay::rebuildKeys() {
  mKeys.clear();
  const char *rows[] = {"1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
  for (const char *row : rows)
    for (const char *p = row; *p; ++p) {
      juce::juce_wchar ch = (juce::juce_wchar)*p;
      const bool letter = juce::CharacterFunctions::isLetter(ch);
      juce::String cap =
          letter && !mShift ? juce::String::charToString(ch).toLowerCase()
                            : juce::String::charToString(ch);
      addKey(cap, [this, cap] {
        if (mText.length() < 24)
          mText += cap;
        if (mShift) { // one-shot capitalisation, like a phone keyboard
          mShift = false;
          rebuildKeys();
          resized();
        }
      });
    }
  addKey(mShift ? "shift*" : "SHIFT", [this] {
    mShift = !mShift;
    rebuildKeys();
    resized();
  });
  addKey("SPACE", [this] {
    if (mText.length() < 24)
      mText += " ";
  });
  addKey(juce::String::fromUTF8("\xe2\x8c\xab"), [this] { // backspace
    mText = mText.dropLastCharacters(1);
  });
  resized();
}

void KeyboardOverlay::paint(juce::Graphics &g) {
  g.fillAll(juce::Colour(0xf5141517));
}

void KeyboardOverlay::resized() {
  auto area = getLocalBounds().reduced(juce::jmax(10, getWidth() / 30));
  const int line = juce::jlimit(24, 40, getHeight() / 14);
  const int gap = 6;
  mPromptLabel.setBounds(area.removeFromTop(line));
  mTextLabel.setBounds(area.removeFromTop(juce::jmax(44, line + 16)));
  area.removeFromTop(gap);

  auto footer = area.removeFromBottom(juce::jmax(48, line + 14));
  {
    const int w = (footer.getWidth() - gap) / 2;
    mCancel.setBounds(footer.removeFromLeft(w));
    mAccept.setBounds(footer.removeFromRight(w));
  }
  area.removeFromBottom(gap);

  // Lay the keys out in the same row structure they were added in.
  const int rowLens[] = {10, 10, 9, 7, 3}; // digits, qwerty, asdf, zxcv, controls
  int idx = 0;
  const int nRows = 5;
  const int rowH = (area.getHeight() - gap * (nRows - 1)) / nRows;
  for (int r = 0; r < nRows; ++r) {
    auto row = area.removeFromTop(rowH);
    const int n = rowLens[r];
    const int w = (row.getWidth() - gap * (n - 1)) / n;
    for (int i = 0; i < n && idx < mKeys.size(); ++i, ++idx) {
      mKeys[idx]->setBounds(row.removeFromLeft(r == nRows - 1 ? row.getWidth() / (n - i)
                                                              : w));
      row.removeFromLeft(gap);
    }
    area.removeFromTop(gap);
  }
}

} // namespace gatewaypi

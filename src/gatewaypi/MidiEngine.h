/*
 * GatewayPi appliance layer for NAMix Linux
 * Copyright (C) 2026
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 */

#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <map>
#include <memory>
#include <mutex>
#include <optional>

#include "GatewayPiConfig.h"
#include "PresetManager.h"

namespace gatewaypi {

// Everything a footswitch can do.
enum class MidiAction {
  Slot1, Slot2, Slot3, Slot4, BankDown, BankUp, Bypass, Tuner
};

inline const char *midiActionKey(MidiAction a) {
  switch (a) {
    case MidiAction::Slot1: return "slot1";
    case MidiAction::Slot2: return "slot2";
    case MidiAction::Slot3: return "slot3";
    case MidiAction::Slot4: return "slot4";
    case MidiAction::BankDown: return "bankDown";
    case MidiAction::BankUp: return "bankUp";
    case MidiAction::Bypass: return "bypass";
    case MidiAction::Tuner: return "tuner";
  }
  return "?";
}

inline const char *midiActionLabel(MidiAction a) {
  switch (a) {
    case MidiAction::Slot1: return "Preset 1";
    case MidiAction::Slot2: return "Preset 2";
    case MidiAction::Slot3: return "Preset 3";
    case MidiAction::Slot4: return "Preset 4";
    case MidiAction::BankDown: return "Bank down";
    case MidiAction::BankUp: return "Bank up";
    case MidiAction::Bypass: return "Bypass";
    case MidiAction::Tuner: return "Tuner";
  }
  return "?";
}

// Opens every MIDI input it can find (USB and BLE-MIDI devices both surface
// as ALSA sequencer clients) and maps incoming messages to actions through a
// binding table.  Defaults follow the documented Chocolate contract
// (PC 0-3 = slots, CC 80-83 = bank/bypass/tuner) but any binding can be
// re-learned in the app: call setLearnTarget() and the next Program Change,
// CC press (value >= 64) or Note-On is captured, bound, and persisted to
// config.json under "midiMap" — no pedal-side reprogramming needed.
//
// Devices are rescanned every 2 s so the M-Vave Chocolate is picked up
// whenever it is plugged in or reconnects over Bluetooth.  MIDI callbacks
// arrive on a background thread; actions are marshalled to the message
// thread, and the PresetManager is held through a weak_ptr so a callback
// landing during shutdown is a no-op rather than a use-after-free.
class MidiEngine : private juce::Timer, private juce::MidiInputCallback {
public:
  MidiEngine(std::weak_ptr<PresetManager> presets, const Config &config)
      : mPresets(std::move(presets)), mConfig(config) {
    loadBindings();
    rescan();
    startTimer(2000);
  }

  ~MidiEngine() override {
    stopTimer();
    for (auto &kv : mOpenInputs)
      kv.second->stop();
  }

  // Arm learn mode: the next qualifying message is bound to `action`.
  // `onLearned` fires on the message thread with a description ("CC 22").
  // Pass nullopt to disarm.
  void setLearnTarget(std::optional<MidiAction> action,
                      std::function<void(juce::String)> onLearned = nullptr) {
    const std::scoped_lock lock(mLock);
    mLearnTarget = action;
    mOnLearned = std::move(onLearned);
  }

  // Current binding of an action as display text ("PC 2", "CC 82", "-").
  juce::String describeBinding(MidiAction action) const {
    const std::scoped_lock lock(mLock);
    for (const auto &kv : mBindings)
      if (kv.second == action)
        return describeKey(kv.first);
    return "-";
  }

private:
  // Binding keys encode message kind and number: kind * 1000 + number.
  enum Kind { kPC = 0, kCC = 1, kNote = 2 };
  static int makeKey(Kind kind, int number) { return (int)kind * 1000 + number; }

  static juce::String describeKey(int key) {
    const int number = key % 1000;
    switch (key / 1000) {
      case kPC: return "PC " + juce::String(number);
      case kCC: return "CC " + juce::String(number);
      default: return "Note " + juce::String(number);
    }
  }

  void loadBindings() {
    // Documented defaults first...
    mBindings[makeKey(kPC, 0)] = MidiAction::Slot1;
    mBindings[makeKey(kPC, 1)] = MidiAction::Slot2;
    mBindings[makeKey(kPC, 2)] = MidiAction::Slot3;
    mBindings[makeKey(kPC, 3)] = MidiAction::Slot4;
    mBindings[makeKey(kCC, mConfig.ccBankDown)] = MidiAction::BankDown;
    mBindings[makeKey(kCC, mConfig.ccBankUp)] = MidiAction::BankUp;
    mBindings[makeKey(kCC, mConfig.ccBypass)] = MidiAction::Bypass;
    mBindings[makeKey(kCC, mConfig.ccMute)] = MidiAction::Tuner;

    // ...then learned overrides from config.json's "midiMap".
    if (auto *map = mConfig.midiMap.getDynamicObject()) {
      for (auto action : {MidiAction::Slot1, MidiAction::Slot2, MidiAction::Slot3,
                          MidiAction::Slot4, MidiAction::BankDown,
                          MidiAction::BankUp, MidiAction::Bypass, MidiAction::Tuner}) {
        const auto entry = map->getProperty(midiActionKey(action));
        if (auto *obj = entry.getDynamicObject()) {
          const juce::String type = obj->getProperty("type").toString();
          const int number = (int)obj->getProperty("number");
          const Kind kind = type == "pc" ? kPC : type == "note" ? kNote : kCC;
          // Unbind this action's default before applying the override.
          for (auto it = mBindings.begin(); it != mBindings.end();)
            it = (it->second == action) ? mBindings.erase(it) : std::next(it);
          mBindings[makeKey(kind, number)] = action;
        }
      }
    }
  }

  // Build {action: {type, number}} from a bindings snapshot and merge it
  // into config.json.  Static and self-contained so the message-thread hop
  // from the MIDI thread never dangles a `this`.
  static void persistBindings(const juce::File &configFile,
                              const std::map<int, MidiAction> &bindings) {
    auto *map = new juce::DynamicObject();
    for (const auto &kv : bindings) {
      auto *entry = new juce::DynamicObject();
      const int kind = kv.first / 1000;
      entry->setProperty("type", kind == kPC ? "pc" : kind == kNote ? "note" : "cc");
      entry->setProperty("number", kv.first % 1000);
      map->setProperty(midiActionKey(kv.second), juce::var(entry));
    }
    Config::updateConfigKey(configFile, "midiMap", juce::var(map));
  }

  void timerCallback() override { rescan(); }

  void rescan() {
    const auto devices = juce::MidiInput::getAvailableDevices();

    // Drop handles for devices that have disappeared.
    for (auto it = mOpenInputs.begin(); it != mOpenInputs.end();) {
      const bool stillPresent =
          std::any_of(devices.begin(), devices.end(),
                      [&](const auto &d) { return d.identifier == it->first; });
      it = stillPresent ? std::next(it) : mOpenInputs.erase(it);
    }

    // Open anything new.  Skip ALSA "Through" ports to avoid feedback loops.
    for (const auto &d : devices) {
      if (d.name.containsIgnoreCase("through"))
        continue;
      if (mOpenInputs.count(d.identifier) != 0)
        continue;
      if (auto input = juce::MidiInput::openDevice(d.identifier, this)) {
        input->start();
        juce::Logger::writeToLog("GatewayPi: MIDI input opened: " + d.name);
        mOpenInputs[d.identifier] = std::move(input);
      }
    }
  }

  // Marshal an action onto the message thread, dropping it if the
  // PresetManager has already been destroyed.
  void async(std::function<void(PresetManager &)> action) {
    juce::MessageManager::callAsync(
        [weak = mPresets, action = std::move(action)] {
          if (auto p = weak.lock())
            action(*p);
        });
  }

  void handleIncomingMidiMessage(juce::MidiInput *,
                                 const juce::MidiMessage &m) override {
    if (mConfig.midiChannel != 0 && m.getChannel() != mConfig.midiChannel)
      return;

    int key = -1;
    if (m.isProgramChange())
      key = makeKey(kPC, m.getProgramChangeNumber());
    else if (m.isController() && m.getControllerValue() >= 64)
      key = makeKey(kCC, m.getControllerNumber());
    else if (m.isNoteOnOrOff())
      // Footswitches often send Note-On OR Note-Off (many pedals, incl. the
      // M-Vave Chocolate in its default mode, emit only a velocity-0 note
      // per press, which reads as Note-Off). Accept either; a debounce below
      // collapses a momentary pedal's on+off pair into one action.
      key = makeKey(kNote, m.getNoteNumber());
    if (key < 0)
      return;

    // Debounce: ignore a repeat of the SAME message within 250 ms, so a
    // momentary switch that sends note-on (press) + note-off (release) fires
    // just once. (Pedals that send one event per press are unaffected.)
    {
      const juce::uint32 now = juce::Time::getMillisecondCounter();
      const std::scoped_lock lock(mLock);
      if (!mLearnTarget.has_value()) {
        const auto prev = mLastFire.find(key);
        if (prev != mLastFire.end() && now - prev->second < 250)
          return;
        mLastFire[key] = now;
      }
    }

    MidiAction action;
    {
      const std::scoped_lock lock(mLock);
      if (mLearnTarget.has_value()) {
        const MidiAction target = *mLearnTarget;
        mLearnTarget.reset();
        // One trigger = one action: clear the target's old binding and any
        // previous owner of this exact message.
        for (auto it = mBindings.begin(); it != mBindings.end();)
          it = (it->second == target || it->first == key) ? mBindings.erase(it)
                                                          : std::next(it);
        mBindings[key] = target;
        auto onLearned = std::move(mOnLearned);
        mOnLearned = nullptr;
        juce::MessageManager::callAsync(
            [configFile = mConfig.configFile, snapshot = mBindings, key,
             onLearned = std::move(onLearned)] {
              persistBindings(configFile, snapshot);
              if (onLearned)
                onLearned(describeKey(key));
            });
        return;
      }
      const auto it = mBindings.find(key);
      if (it == mBindings.end())
        return;
      action = it->second;
    }

    switch (action) {
      case MidiAction::Slot1: async([](PresetManager &p) { p.selectSlot(0); }); break;
      case MidiAction::Slot2: async([](PresetManager &p) { p.selectSlot(1); }); break;
      case MidiAction::Slot3: async([](PresetManager &p) { p.selectSlot(2); }); break;
      case MidiAction::Slot4: async([](PresetManager &p) { p.selectSlot(3); }); break;
      case MidiAction::BankDown: async([](PresetManager &p) { p.bankDown(); }); break;
      case MidiAction::BankUp: async([](PresetManager &p) { p.bankUp(); }); break;
      case MidiAction::Bypass: async([](PresetManager &p) { p.toggleBypass(); }); break;
      case MidiAction::Tuner: async([](PresetManager &p) { p.toggleMute(); }); break;
    }
  }

  std::weak_ptr<PresetManager> mPresets;
  Config mConfig;
  std::map<juce::String, std::unique_ptr<juce::MidiInput>> mOpenInputs;

  mutable std::mutex mLock;
  std::map<int, MidiAction> mBindings;
  std::map<int, juce::uint32> mLastFire; // per-message debounce timestamps
  std::optional<MidiAction> mLearnTarget;
  std::function<void(juce::String)> mOnLearned;
};

} // namespace gatewaypi

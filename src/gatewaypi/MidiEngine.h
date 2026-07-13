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

#include "GatewayPiConfig.h"
#include "PresetManager.h"

namespace gatewaypi {

// Opens every MIDI input it can find (USB and BLE-MIDI devices both surface
// as ALSA sequencer clients) and maps footswitch messages to preset actions:
//
//   Program Change 0-3  -> preset slot 1-4 of the current bank
//   CC ccBankDown >= 64 -> bank down       CC ccBankUp >= 64 -> bank up
//   CC ccBypass  >= 64  -> toggle bypass   CC ccMute  >= 64  -> toggle mute
//
// Devices are rescanned every 2 s so the M-Vave Chocolate is picked up
// whenever it is plugged in or reconnects over Bluetooth.  MIDI callbacks
// arrive on a background thread; all actions are marshalled to the message
// thread before touching the PresetManager.  The PresetManager is held
// through a weak_ptr so a callback that lands during shutdown is a no-op
// rather than a use-after-free.
class MidiEngine : private juce::Timer, private juce::MidiInputCallback {
public:
  MidiEngine(std::weak_ptr<PresetManager> presets, const Config &config)
      : mPresets(std::move(presets)), mConfig(config) {
    rescan();
    startTimer(2000);
  }

  ~MidiEngine() override {
    stopTimer();
    for (auto &kv : mOpenInputs)
      kv.second->stop();
  }

private:
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

    if (m.isProgramChange()) {
      const int slot = m.getProgramChangeNumber() % kSlotsPerBank;
      async([slot](PresetManager &p) { p.selectSlot(slot); });
      return;
    }

    if (m.isController() && m.getControllerValue() >= 64) {
      const int cc = m.getControllerNumber();
      if (cc == mConfig.ccBankDown)
        async([](PresetManager &p) { p.bankDown(); });
      else if (cc == mConfig.ccBankUp)
        async([](PresetManager &p) { p.bankUp(); });
      else if (cc == mConfig.ccBypass)
        async([](PresetManager &p) { p.toggleBypass(); });
      else if (cc == mConfig.ccMute)
        async([](PresetManager &p) { p.toggleMute(); });
    }
  }

  std::weak_ptr<PresetManager> mPresets;
  Config mConfig;
  std::map<juce::String, std::unique_ptr<juce::MidiInput>> mOpenInputs;
};

} // namespace gatewaypi

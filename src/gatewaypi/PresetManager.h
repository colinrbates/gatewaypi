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

#include <array>
#include <functional>

#include "GatewayPiConfig.h"
#include "PluginProcessor.h"

namespace gatewaypi {

// Slots per bank — matches the four switches on the M-Vave Chocolate.
constexpr int kSlotsPerBank = 4;

// Message-thread-only preset bank.  Presets are JSON files in
// <dataRoot>/presets named "NN-anything.json"; NN (two digits, 1-based)
// determines the global slot: NN 01-04 = bank 1, 05-08 = bank 2, etc.
// Slots without a file are empty and selectable only for saving.
class PresetManager {
public:
  PresetManager(NAMixAudioProcessor &proc, const Config &config);

  // Called after any change (preset applied/saved, bank moved, bypass/mute).
  // The UI subscribes to refresh itself.
  std::function<void()> onChanged;

  int getBank() const { return mBank; }
  int getSlot() const { return mSlot; }
  int getNumBanks() const;

  // Display name for a slot of the current bank ("-" if empty).
  juce::String getSlotName(int slot) const;
  bool isSlotOccupied(int slot) const;

  // Select and apply slot 0-3 of the current bank.  Empty slots just move
  // the selection (so Save can target them) without touching the processor.
  void selectSlot(int slot);
  void bankUp() { setBank(mBank + 1); }
  void bankDown() { setBank(mBank - 1); }

  // Capture the processor's current model/IR/params into the selected slot.
  void saveCurrentSlot();

  bool getBypass() const;
  bool getMute() const;
  void toggleBypass();
  void toggleMute();

  // Restore last-used preset from state.json (call once after construction).
  void restoreLastState();

private:
  juce::File slotFile(int bank, int slot) const;
  void setBank(int bank);
  void applySlot(int slot);
  void persistState() const;

  NAMixAudioProcessor &mProcessor;
  Config mConfig;
  int mBank = 0; // 0-based
  int mSlot = 0; // 0-based within bank
};

} // namespace gatewaypi

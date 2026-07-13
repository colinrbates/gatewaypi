/*
 * GatewayPi appliance layer for NAMix Linux
 * Copyright (C) 2026
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 */

#include "PresetManager.h"

namespace gatewaypi {

namespace {

int slotNumber(int bank, int slot) { return bank * kSlotsPerBank + slot + 1; }

// Two-digit prefix of a preset filename, or -1 if it doesn't match "NN-*.json".
int prefixOf(const juce::File &f) {
  const auto name = f.getFileName();
  if (name.length() < 4 || !name.endsWithIgnoreCase(".json"))
    return -1;
  if (!juce::CharacterFunctions::isDigit(name[0]) ||
      !juce::CharacterFunctions::isDigit(name[1]) || name[2] != '-')
    return -1;
  return name.substring(0, 2).getIntValue();
}

} // namespace

PresetManager::PresetManager(NAMixAudioProcessor &proc, const Config &config)
    : mProcessor(proc), mConfig(config) {
  mPersistedSnapshot = snapshot(); // defaults are not "unsaved edits"
  if (mConfig.autosavePresets)
    startTimer(2000); // debounced autosave tick
}

juce::File PresetManager::slotFile(int bank, int slot) const {
  const int n = slotNumber(bank, slot);
  for (const auto &f : mConfig.presetsDir.findChildFiles(juce::File::findFiles,
                                                         false, "*.json"))
    if (prefixOf(f) == n)
      return f;
  return mConfig.presetsDir.getChildFile(
      juce::String::formatted("%02d-preset.json", n));
}

int PresetManager::getNumBanks() const {
  int maxPrefix = 0;
  for (const auto &f : mConfig.presetsDir.findChildFiles(juce::File::findFiles,
                                                         false, "*.json"))
    maxPrefix = juce::jmax(maxPrefix, prefixOf(f));
  const int fromFiles = (maxPrefix + kSlotsPerBank - 1) / kSlotsPerBank;
  return juce::jmax(1, fromFiles, mBank + 1);
}

bool PresetManager::isSlotOccupied(int slot) const {
  return slotFile(mBank, slot).existsAsFile();
}

juce::String PresetManager::getSlotName(int slot) const {
  const auto f = slotFile(mBank, slot);
  if (!f.existsAsFile())
    return "-";
  const auto parsed = juce::JSON::parse(f.loadFileAsString());
  if (auto *obj = parsed.getDynamicObject())
    if (obj->hasProperty("name"))
      return obj->getProperty("name").toString();
  // Fall back to the filename stem without the "NN-" prefix.
  return f.getFileNameWithoutExtension().substring(3);
}

void PresetManager::setBank(int bank) {
  maybeAutosave(); // the active slot still belongs to the old bank
  mBank = juce::jlimit(0, juce::jmax(0, getNumBanks() - 1), bank);
  if (onChanged)
    onChanged();
}

void PresetManager::selectSlot(int slot) {
  maybeAutosave(); // never lose a tweak to a quick preset change
  mSlot = juce::jlimit(0, kSlotsPerBank - 1, slot);
  if (isSlotOccupied(mSlot))
    applySlot(mSlot);
  else if (onChanged)
    onChanged();
}

void PresetManager::applySlot(int slot) {
  const auto f = slotFile(mBank, slot);
  const auto parsed = juce::JSON::parse(f.loadFileAsString());
  auto *obj = parsed.getDynamicObject();
  if (obj == nullptr) {
    juce::Logger::writeToLog("GatewayPi: bad preset file " + f.getFullPathName());
    return;
  }

  const juce::String modelPath = obj->getProperty("model").toString();
  const juce::String irPath = obj->getProperty("ir").toString();

  if (modelPath.isNotEmpty() && juce::File(modelPath).existsAsFile())
    mProcessor.loadModel(juce::File(modelPath));
  else
    mProcessor.clearModel();

  if (irPath.isNotEmpty() && juce::File(irPath).existsAsFile())
    mProcessor.loadIR(juce::File(irPath));
  else
    mProcessor.clearIR();

  if (auto *params = obj->getProperty("params").getDynamicObject()) {
    for (const auto &kv : params->getProperties()) {
      if (auto *p = mProcessor.apvts.getParameter(kv.name.toString())) {
        const float realValue = (float)(double)kv.value;
        p->setValueNotifyingHost(p->convertTo0to1(realValue));
      }
    }
  }

  // A preset change always lands you in a playable state.
  mProcessor.gpSetBypass(false);
  mProcessor.gpSetMute(false);

  mPersistedSnapshot = snapshot(); // freshly applied == on disk
  persistState();
  if (onChanged)
    onChanged();
}

juce::var PresetManager::buildPresetVar(const juce::String &name) const {
  auto *params = new juce::DynamicObject();
  for (auto *p : mProcessor.getParameters())
    if (auto *rp = dynamic_cast<juce::RangedAudioParameter *>(p))
      params->setProperty(rp->paramID, rp->convertFrom0to1(rp->getValue()));

  auto *obj = new juce::DynamicObject();
  obj->setProperty("name", name);
  obj->setProperty("model", mProcessor.getModelPath());
  obj->setProperty("ir", mProcessor.getIRPath());
  obj->setProperty("params", juce::var(params));
  return juce::var(obj);
}

juce::String PresetManager::snapshot() const {
  const auto f = slotFile(mBank, mSlot);
  juce::String name = getSlotName(mSlot);
  if (!f.existsAsFile())
    name = juce::String("Preset ") + juce::String(slotNumber(mBank, mSlot));
  return juce::JSON::toString(buildPresetVar(name), true);
}

void PresetManager::saveCurrentSlot() {
  const auto f = slotFile(mBank, mSlot);
  const juce::String defaultName =
      juce::String("Preset ") + juce::String(slotNumber(mBank, mSlot));

  // Preserve an existing custom name; default to "Preset N".
  juce::String name = defaultName;
  if (f.existsAsFile()) {
    const auto old = juce::JSON::parse(f.loadFileAsString());
    if (auto *obj = old.getDynamicObject())
      if (obj->hasProperty("name"))
        name = obj->getProperty("name").toString();
  }

  // Auto-name from the loaded capture's filename while the name is still a
  // default — so a fresh slot picks up "Fender Twin" instead of "Preset 3".
  // A name the user chose (anything else) is left untouched.
  if ((name.isEmpty() || name == defaultName) &&
      mProcessor.getModelPath().isNotEmpty())
    name = juce::File(mProcessor.getModelPath()).getFileNameWithoutExtension();

  f.replaceWithText(juce::JSON::toString(buildPresetVar(name), false));
  mPersistedSnapshot = snapshot();
  if (onChanged)
    onChanged();
}

void PresetManager::setCurrentSlotName(const juce::String &name) {
  const auto f = slotFile(mBank, mSlot);
  const juce::String clean = name.trim();
  if (clean.isEmpty())
    return;
  f.replaceWithText(juce::JSON::toString(buildPresetVar(clean), false));
  mPersistedSnapshot = snapshot();
  if (onChanged)
    onChanged();
}

void PresetManager::maybeAutosave() {
  if (!mConfig.autosavePresets)
    return;
  if (snapshot() != mPersistedSnapshot)
    saveCurrentSlot();
}

void PresetManager::timerCallback() { maybeAutosave(); }

bool PresetManager::getBypass() const { return mProcessor.gpGetBypass(); }
bool PresetManager::getMute() const { return mProcessor.gpGetMute(); }

void PresetManager::toggleBypass() {
  mProcessor.gpSetBypass(!mProcessor.gpGetBypass());
  if (onChanged)
    onChanged();
}

void PresetManager::toggleMute() {
  mProcessor.gpSetMute(!mProcessor.gpGetMute());
  if (onChanged)
    onChanged();
}

void PresetManager::persistState() const {
  auto *obj = new juce::DynamicObject();
  obj->setProperty("bank", mBank);
  obj->setProperty("slot", mSlot);
  mConfig.stateFile.replaceWithText(juce::JSON::toString(juce::var(obj), false));
}

void PresetManager::restoreLastState() {
  if (!mConfig.stateFile.existsAsFile())
    return;
  const auto parsed = juce::JSON::parse(mConfig.stateFile.loadFileAsString());
  if (auto *obj = parsed.getDynamicObject()) {
    mBank = juce::jlimit(0, juce::jmax(0, getNumBanks() - 1),
                         (int)obj->getProperty("bank"));
    const int slot = juce::jlimit(0, kSlotsPerBank - 1, (int)obj->getProperty("slot"));
    mSlot = slot;
    if (isSlotOccupied(slot))
      applySlot(slot);
    else {
      mPersistedSnapshot = snapshot();
      if (onChanged)
        onChanged();
    }
  }
}

} // namespace gatewaypi

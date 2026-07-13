/*
 * GatewayPi appliance layer for NAMix Linux
 * Copyright (C) 2026
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 */

#pragma once

#include <juce_core/juce_core.h>

namespace gatewaypi {

// Central configuration: data-directory layout plus user settings loaded from
// <dataRoot>/config.json.  The data root defaults to /var/lib/gatewaypi and
// can be overridden with the GATEWAYPI_DATA environment variable (useful for
// desktop testing).
struct Config {
  juce::File dataRoot;
  juce::File presetsDir;
  juce::File modelsDir;
  juce::File irsDir;
  juce::File stateFile;
  juce::File configFile;

  // config.json settings
  int midiChannel = 0;           // 0 = omni, 1-16 = specific channel
  int ccBankDown = 80;
  int ccBankUp = 81;
  int ccBypass = 82;
  int ccMute = 83;
  juce::String audioDeviceMatch; // substring match for preferred ALSA device, e.g. "iTwo"
  double sampleRate = 48000.0; // NAM is optimised for 48 kHz; fixed.
  int bufferSize = 64;
  double tunerReference = 440.0; // A4 reference pitch (Hz)
  bool autosavePresets = true;   // persist tweaks to the active slot
  juce::var midiMap;             // learned bindings: {action: {type, number}}

  static Config load() {
    Config c;
    const juce::String env{std::getenv("GATEWAYPI_DATA") != nullptr
                               ? std::getenv("GATEWAYPI_DATA")
                               : ""};
    c.dataRoot = env.isNotEmpty() ? juce::File(env)
                                  : juce::File("/var/lib/gatewaypi");
    c.presetsDir = c.dataRoot.getChildFile("presets");
    c.stateFile = c.dataRoot.getChildFile("state.json");
    c.configFile = c.dataRoot.getChildFile("config.json");

    // Captures (.nam) and IRs (.wav) live in friendly home-level folders,
    // next to Desktop — visible in the file manager, easy to browse from
    // another machine.  Overridable via config.json or env for testing.
    const juce::File home = juce::File::getSpecialLocation(
        juce::File::userHomeDirectory);
    c.modelsDir = home.getChildFile("Captures");
    c.irsDir = home.getChildFile("IRs");

    if (c.configFile.existsAsFile()) {
      const auto parsed = juce::JSON::parse(c.configFile.loadFileAsString());
      if (auto *obj = parsed.getDynamicObject()) {
        c.midiChannel = (int)obj->getProperty("midiChannel");
        if (obj->hasProperty("ccBankDown")) c.ccBankDown = (int)obj->getProperty("ccBankDown");
        if (obj->hasProperty("ccBankUp")) c.ccBankUp = (int)obj->getProperty("ccBankUp");
        if (obj->hasProperty("ccBypass")) c.ccBypass = (int)obj->getProperty("ccBypass");
        if (obj->hasProperty("ccMute")) c.ccMute = (int)obj->getProperty("ccMute");
        if (obj->hasProperty("audioDeviceMatch"))
          c.audioDeviceMatch = obj->getProperty("audioDeviceMatch").toString();
        if (obj->hasProperty("bufferSize")) c.bufferSize = (int)obj->getProperty("bufferSize");
        // sampleRate is intentionally fixed at 48 kHz (NAM's native rate).
        if (obj->hasProperty("tunerReference"))
          c.tunerReference = (double)obj->getProperty("tunerReference");
        if (obj->hasProperty("autosavePresets"))
          c.autosavePresets = (bool)obj->getProperty("autosavePresets");
        if (obj->hasProperty("capturesDir"))
          c.modelsDir = juce::File(obj->getProperty("capturesDir").toString());
        if (obj->hasProperty("irsDir"))
          c.irsDir = juce::File(obj->getProperty("irsDir").toString());
        c.midiMap = obj->getProperty("midiMap");
      }
    }

    // Ensure the layout exists so first run works on an empty data dir.
    for (const auto &d : {c.presetsDir, c.modelsDir, c.irsDir})
      d.createDirectory();
    return c;
  }

  // Merge one key into config.json, preserving everything else (and any
  // hand-edits made since load).  Message thread only.
  static void updateConfigKey(const juce::File &configFile,
                              const juce::String &key, const juce::var &value) {
    juce::var root;
    if (configFile.existsAsFile())
      root = juce::JSON::parse(configFile.loadFileAsString());
    if (root.getDynamicObject() == nullptr)
      root = juce::var(new juce::DynamicObject());
    root.getDynamicObject()->setProperty(key, value);
    configFile.replaceWithText(juce::JSON::toString(root, false));
  }
};

} // namespace gatewaypi

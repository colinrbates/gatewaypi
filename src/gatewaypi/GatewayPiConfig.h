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

  // config.json settings
  int midiChannel = 0;           // 0 = omni, 1-16 = specific channel
  int ccBankDown = 80;
  int ccBankUp = 81;
  int ccBypass = 82;
  int ccMute = 83;
  juce::String audioDeviceMatch; // substring match for preferred ALSA device, e.g. "iTwo"
  double sampleRate = 48000.0;
  int bufferSize = 128;

  static Config load() {
    Config c;
    const juce::String env{std::getenv("GATEWAYPI_DATA") != nullptr
                               ? std::getenv("GATEWAYPI_DATA")
                               : ""};
    c.dataRoot = env.isNotEmpty() ? juce::File(env)
                                  : juce::File("/var/lib/gatewaypi");
    c.presetsDir = c.dataRoot.getChildFile("presets");
    c.modelsDir = c.dataRoot.getChildFile("models");
    c.irsDir = c.dataRoot.getChildFile("irs");
    c.stateFile = c.dataRoot.getChildFile("state.json");

    // Ensure the layout exists so first run works on an empty data dir.
    for (const auto &d : {c.presetsDir, c.modelsDir, c.irsDir})
      d.createDirectory();

    const auto configFile = c.dataRoot.getChildFile("config.json");
    if (configFile.existsAsFile()) {
      const auto parsed = juce::JSON::parse(configFile.loadFileAsString());
      if (auto *obj = parsed.getDynamicObject()) {
        c.midiChannel = (int)obj->getProperty("midiChannel");
        if (obj->hasProperty("ccBankDown")) c.ccBankDown = (int)obj->getProperty("ccBankDown");
        if (obj->hasProperty("ccBankUp")) c.ccBankUp = (int)obj->getProperty("ccBankUp");
        if (obj->hasProperty("ccBypass")) c.ccBypass = (int)obj->getProperty("ccBypass");
        if (obj->hasProperty("ccMute")) c.ccMute = (int)obj->getProperty("ccMute");
        if (obj->hasProperty("audioDeviceMatch"))
          c.audioDeviceMatch = obj->getProperty("audioDeviceMatch").toString();
        if (obj->hasProperty("sampleRate")) c.sampleRate = (double)obj->getProperty("sampleRate");
        if (obj->hasProperty("bufferSize")) c.bufferSize = (int)obj->getProperty("bufferSize");
      }
    }
    return c;
  }
};

} // namespace gatewaypi

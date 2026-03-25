"use strict";

const path = require("path");
const { execFileSync } = require("child_process");

const isElectron = !!(process.versions && process.versions.electron);
const platform = process.platform;

let nativeModule;
try {
  nativeModule = require(path.resolve(__dirname, "./build/Release/system_utils_native.node"));
} catch (error) {
  if (isElectron) {
    console.error("Failed to load system-utils native module. Rebuild for Electron:");
    console.error("npx @electron/rebuild -f -w system-utils");
  }
  throw error;
}

const OSASCRIPT = "/usr/bin/osascript";

function runOsaGet(script) {
  try {
    return execFileSync(OSASCRIPT, ["-e", script], {
      encoding: "utf8",
      timeout: 2000,
      stdio: ["ignore", "pipe", "ignore"],
    }).trim();
  } catch {
    return null;
  }
}

function runOsa(script) {
  try {
    execFileSync(OSASCRIPT, ["-e", script], {
      timeout: 2000,
      stdio: "ignore",
    });
    return true;
  } catch {
    return false;
  }
}

class SystemUtils {
  constructor() {
    this._initialized = false;
    this._setupCleanup();
  }

  _setupCleanup() {
    const cleanup = () => {
      if (this._initialized) {
        try {
          this.shutdown();
        } catch {}
      }
    };

    process.on("exit", cleanup);
    process.on("SIGINT", cleanup);
    process.on("SIGTERM", cleanup);

    if (isElectron) {
      try {
        const electron = require("electron");
        const app = electron.app || (electron.remote && electron.remote.app);
        if (app) {
          app.on("before-quit", cleanup);
        }
      } catch {}
    }
  }

  init() {
    if (this._initialized) return true;

    if (platform === "win32") {
      this._initialized = !!nativeModule.init();
      return this._initialized;
    }

    if (platform === "darwin") {
      this._initialized = true;
      return true;
    }

    throw new Error(`Unsupported platform: ${platform}`);
  }

  async initAsync() {
    if (this._initialized) return true;
    if (isElectron) {
      await new Promise((resolve) => setTimeout(resolve, 100));
    }
    return this.init();
  }

  shutdown() {
    if (!this._initialized) return;
    if (platform === "win32") {
      nativeModule.shutdown();
    }
    this._initialized = false;
  }

  _ensureInitialized() {
    if (!this._initialized) {
      throw new Error("SystemUtils is not initialized. Please call init() first.");
    }
  }

  sendKey(input) {
    this._ensureInitialized();
    if (platform === "darwin") {
      const options = typeof input === "string" ? { input } : { input: input.key };
      return nativeModule.sendKey(options);
    }
    return nativeModule.sendKey(input);
  }

  setMediaState(state) {
    this._ensureInitialized();
    if (platform === "darwin") {
      let ok = true;
      if (state && state.speaker) {
        if (typeof state.speaker.volume === "number") {
          const v = Math.min(100, Math.max(0, Math.round(state.speaker.volume)));
          ok = runOsa(`set volume output volume ${v}`) && ok;
        }
        if (typeof state.speaker.mute === "boolean") {
          ok =
            runOsa(`set volume output muted ${state.speaker.mute ? "true" : "false"}`) && ok;
        }
      }
      if (state && state.mic && typeof state.mic.volume === "number") {
        const v = Math.min(100, Math.max(0, Math.round(state.mic.volume)));
        ok = runOsa(`set volume input volume ${v}`) && ok;
      }
      if (ok) return true;
    }
    return nativeModule.setMediaState(state);
  }

  getMediaState(query) {
    this._ensureInitialized();
    if (platform === "darwin") {
      const result = {};
      let ok = true;
      if (query && query.speaker) {
        const volRaw = runOsaGet("output volume of (get volume settings)");
        const muteRaw = runOsaGet("output muted of (get volume settings)");
        const volume = volRaw === null ? NaN : parseInt(volRaw, 10);
        if (!Number.isNaN(volume) && (muteRaw === "true" || muteRaw === "false")) {
          result.speaker = { volume, mute: muteRaw === "true" };
        } else {
          ok = false;
        }
      }
      if (query && query.mic) {
        const volRaw = runOsaGet("input volume of (get volume settings)");
        const volume = volRaw === null ? NaN : parseInt(volRaw, 10);
        if (!Number.isNaN(volume)) {
          result.mic = { volume, mute: false };
        } else {
          ok = false;
        }
      }
      if (ok) return result;
    }
    return nativeModule.getMediaState(query);
  }

  getAllAudioSessions() {
    this._ensureInitialized();
    if (platform === "darwin") return [];
    return nativeModule.getAllAudioSessions();
  }

  setAudioSessionVolume(sessionId, volume) {
    this._ensureInitialized();
    if (platform === "darwin") return false;
    return nativeModule.setAudioSessionVolume(sessionId, volume);
  }

  getAudioSessionVolume(sessionId) {
    this._ensureInitialized();
    if (platform === "darwin") return null;
    return nativeModule.getAudioSessionVolume(sessionId);
  }

  getIcon(options) {
    this._ensureInitialized();
    if (!options || !Array.isArray(options.path)) {
      throw new Error("IconOptions.path must be a string array");
    }
    if (platform === "darwin") {
      return nativeModule.getIcon({
        path: options.path,
        size: options.size || 32,
        format: options.format || "base64",
      });
    }
    return nativeModule.getIcon(options);
  }

  macroRecorder(data) {
    this._ensureInitialized();
    if (platform === "darwin") {
      if (!data || !data.action) {
        return { status: "failed", error: "Missing action field" };
      }
      switch (data.action) {
        case "record_start":
          return this.startMacroRecording(data);
        case "record_stop":
          return this.stopMacroRecording();
        case "record_get_result":
          return this.getMacroRecordingResult();
        case "play_start":
          return this.startMacroPlayback(data);
        case "play_stop":
          return this.stopMacroPlayback();
        default:
          return { status: "failed", error: `Unknown action: ${data.action}` };
      }
    }
    return nativeModule.macroRecorder(data);
  }

  setMacroStatusCallback(callback) {
    this._ensureInitialized();
    nativeModule.setMacroStatusCallback(callback || null);
  }

  setMacroRecordingCallback(callback) {
    return this.setMacroStatusCallback(callback);
  }

  startMacroRecording(options = {}) {
    this._ensureInitialized();
    const normalized = typeof options === "string"
      ? { macroId: options, blockInput: false }
      : {
          macroId: options?.macroId || "",
          blockInput: !!options?.blockInput,
        };
    if (platform === "darwin") {
      const result = nativeModule.macroRecordStart(normalized);
      return { status: result.status || "started" };
    }
    return nativeModule.startMacroRecording(normalized);
  }

  stopMacroRecording(options = {}) {
    this._ensureInitialized();
    if (platform === "darwin") {
      const result = nativeModule.macroRecordStop();
      return {
        status: result.status || "stopped",
        events: result.events || [],
        duration: result.duration || 0,
      };
    }
    const result = nativeModule.stopMacroRecording();
    if (options.compress && result.events) {
      // Reserved compression hook, behavior kept from legacy wrappers.
    }
    return result;
  }

  startMacroPlayback(data) {
    this._ensureInitialized();
    if (!data || !Array.isArray(data.events)) {
      throw new Error("Invalid playback data. Must include events array.");
    }
    if (platform === "darwin") {
      const payload = {
        events: data.events,
        speed: data.speed !== undefined ? data.speed : 1.0,
        loop: data.loop !== undefined ? data.loop : 1,
        macroId: data.macroId || "",
      };
      const result = nativeModule.macroPlayStart(payload);
      return { status: result.status || "play started" };
    }
    const payload = {
      events: data.events,
      speed: data.speed ? 1.0 / data.speed : 1.0,
      loop: data.loop === undefined ? 1 : data.loop,
      smoothing_factor: data.smoothing_factor || 1,
      macroId: data.macroId || "",
    };
    return nativeModule.startMacroPlayback(payload);
  }

  stopMacroPlayback() {
    this._ensureInitialized();
    if (platform === "darwin") {
      const result = nativeModule.macroPlayStop();
      return { status: result.status || "play stopped" };
    }
    return nativeModule.stopMacroPlayback();
  }

  getMacroRecordingResult() {
    this._ensureInitialized();
    if (platform === "darwin") {
      const result = nativeModule.macroRecordGetResult();
      return { events: result.events || [] };
    }
    return nativeModule.macroRecorder({ action: "record_get_result" });
  }

  static isRunningInElectron() {
    return isElectron;
  }
}

module.exports = SystemUtils;
module.exports.SystemUtils = SystemUtils;

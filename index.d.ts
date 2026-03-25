export interface DeviceState {
  volume?: number;
  mute?: boolean;
}

export interface MediaStateQuery {
  speaker?: boolean;
  mic?: boolean;
  outputDevices?: boolean;
  inputDevices?: boolean;
}

export interface MediaStateSet {
  speaker?: DeviceState;
  mic?: DeviceState;
  outputDevices?: Record<string, DeviceState>;
  inputDevices?: Record<string, DeviceState>;
}

export interface IconOptions {
  path: string[];
  size?: number;
  bgr?: boolean;
  alpha?: boolean;
  format?: "buffer" | "base64";
}

export interface MacroEvent {
  dt: number;
  evt: string;
  pos: [number, number];
  data: Record<string, unknown>;
}

export interface MacroPlaybackData {
  events: MacroEvent[];
  speed?: number;
  loop?: number;
  smoothing_factor?: number;
  macroId?: string;
}

export type MacroStatusCallback = (eventData: unknown) => void;

export interface OperationResult {
  status: string;
  error?: string;
}

export default class SystemUtils {
  static isRunningInElectron(): boolean;

  init(): boolean;
  initAsync(): Promise<boolean>;
  shutdown(): void;

  sendKey(input: string | { key: string }): boolean;
  setMediaState(state: MediaStateSet): boolean;
  getMediaState(query: MediaStateQuery): Record<string, unknown>;
  getAllAudioSessions(): unknown[];
  setAudioSessionVolume(sessionId: string, volume: number): boolean;
  getAudioSessionVolume(sessionId: string): number | null;
  getIcon(options: IconOptions): { icons: unknown[] };

  macroRecorder(data: Record<string, unknown>): Record<string, unknown>;
  setMacroStatusCallback(callback: MacroStatusCallback | null): void;
  setMacroRecordingCallback(callback: MacroStatusCallback | null): void;
  startMacroRecording(options?: string | { macroId?: string; blockInput?: boolean }): OperationResult;
  stopMacroRecording(options?: { compress?: boolean; epsilon?: number }): {
    status: string;
    events: MacroEvent[];
    duration: number;
  };
  startMacroPlayback(data: MacroPlaybackData): OperationResult;
  stopMacroPlayback(): OperationResult;
  getMacroRecordingResult(): { events: MacroEvent[] };
}

export { SystemUtils };

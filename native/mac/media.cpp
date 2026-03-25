#include "media.h"
#include <CoreAudio/CoreAudio.h>
#include <AudioToolbox/AudioToolbox.h>

namespace MacService {

// 辅助函数：尝试读取/设置属性，增加兼容性
static bool TryGetFloatProperty(AudioObjectID deviceID, const AudioObjectPropertyAddress &address, Float32 *outValue) {
    UInt32 size = sizeof(Float32);
    OSStatus status = AudioObjectGetPropertyData(deviceID, &address, 0, NULL, &size, outValue);
    return status == noErr;
}

static bool TrySetFloatProperty(AudioObjectID deviceID, const AudioObjectPropertyAddress &address, Float32 value) {
    UInt32 size = sizeof(Float32);
    OSStatus status = AudioObjectSetPropertyData(deviceID, &address, 0, NULL, size, &value);
    return status == noErr;
}

static bool TryGetUInt32Property(AudioObjectID deviceID, const AudioObjectPropertyAddress &address, UInt32 *outValue) {
    UInt32 size = sizeof(UInt32);
    OSStatus status = AudioObjectGetPropertyData(deviceID, &address, 0, NULL, &size, outValue);
    return status == noErr;
}

static bool TrySetUInt32Property(AudioObjectID deviceID, const AudioObjectPropertyAddress &address, UInt32 value) {
    UInt32 size = sizeof(UInt32);
    OSStatus status = AudioObjectSetPropertyData(deviceID, &address, 0, NULL, size, &value);
    return status == noErr;
}

// 获取默认输出设备ID
AudioDeviceID GetDefaultOutputDevice() {
    AudioDeviceID deviceID = kAudioObjectUnknown;
    UInt32 size = sizeof(deviceID);
    
    AudioObjectPropertyAddress address = {
        kAudioHardwarePropertyDefaultOutputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    
    OSStatus status = AudioObjectGetPropertyData(
        kAudioObjectSystemObject,
        &address,
        0,
        NULL,
        &size,
        &deviceID
    );
    
    if (status != noErr) {
        return kAudioObjectUnknown;
    }
    
    return deviceID;
}

// 获取音量（0.0 - 1.0）
bool GetOutputVolume(AudioDeviceID deviceID, Float32* volume) {
    // 1) 优先 VirtualMainVolume (Main)
    AudioObjectPropertyAddress addrVirtualMainMain = {
        kAudioHardwareServiceDeviceProperty_VirtualMainVolume,
        kAudioDevicePropertyScopeOutput,
        kAudioObjectPropertyElementMain
    };
    if (TryGetFloatProperty(deviceID, addrVirtualMainMain, volume)) {
        return true;
    }

    // 2) 尝试 VirtualMainVolume (Master)
    AudioObjectPropertyAddress addrVirtualMainMaster = {
        kAudioHardwareServiceDeviceProperty_VirtualMainVolume,
        kAudioDevicePropertyScopeOutput,
        kAudioObjectPropertyElementMaster
    };
    if (TryGetFloatProperty(deviceID, addrVirtualMainMaster, volume)) {
        return true;
    }

    // 3) 尝试 VolumeScalar (Master)
    AudioObjectPropertyAddress addrScalarMaster = {
        kAudioDevicePropertyVolumeScalar,
        kAudioDevicePropertyScopeOutput,
        kAudioObjectPropertyElementMaster
    };
    if (TryGetFloatProperty(deviceID, addrScalarMaster, volume)) {
        return true;
    }

    // 4) 按通道尝试 VolumeScalar，合并左右声道（平均）
    Float32 left = -1.0f, right = -1.0f;
    AudioObjectPropertyAddress addrScalarCh1 = {
        kAudioDevicePropertyVolumeScalar,
        kAudioDevicePropertyScopeOutput,
        1 // 左声道
    };
    if (TryGetFloatProperty(deviceID, addrScalarCh1, &left)) {
        *volume = left;
    }
    AudioObjectPropertyAddress addrScalarCh2 = {
        kAudioDevicePropertyVolumeScalar,
        kAudioDevicePropertyScopeOutput,
        2 // 右声道
    };
    if (TryGetFloatProperty(deviceID, addrScalarCh2, &right)) {
        if (left >= 0.0f) {
            *volume = (left + right) / 2.0f;
        } else {
            *volume = right;
        }
    }

    return (left >= 0.0f) || (right >= 0.0f);
}

// 设置音量（0.0 - 1.0）
bool SetOutputVolume(AudioDeviceID deviceID, Float32 volume) {
    bool ok = false;

    // 1) 优先 VirtualMainVolume (Main)
    AudioObjectPropertyAddress addrVirtualMainMain = {
        kAudioHardwareServiceDeviceProperty_VirtualMainVolume,
        kAudioDevicePropertyScopeOutput,
        kAudioObjectPropertyElementMain
    };
    ok = TrySetFloatProperty(deviceID, addrVirtualMainMain, volume);
    if (ok) return true;

    // 2) 尝试 VirtualMainVolume (Master)
    AudioObjectPropertyAddress addrVirtualMainMaster = {
        kAudioHardwareServiceDeviceProperty_VirtualMainVolume,
        kAudioDevicePropertyScopeOutput,
        kAudioObjectPropertyElementMaster
    };
    ok = TrySetFloatProperty(deviceID, addrVirtualMainMaster, volume);
    if (ok) return true;

    // 3) 尝试 VolumeScalar (Master)
    AudioObjectPropertyAddress addrScalarMaster = {
        kAudioDevicePropertyVolumeScalar,
        kAudioDevicePropertyScopeOutput,
        kAudioObjectPropertyElementMaster
    };
    ok = TrySetFloatProperty(deviceID, addrScalarMaster, volume);
    if (ok) return true;

    // 4) 回退为按通道设置（1/2），任意一个设置成功即视为成功
    bool chOk = false;
    AudioObjectPropertyAddress addrScalarCh1 = {
        kAudioDevicePropertyVolumeScalar,
        kAudioDevicePropertyScopeOutput,
        1
    };
    chOk = TrySetFloatProperty(deviceID, addrScalarCh1, volume) || chOk;
    AudioObjectPropertyAddress addrScalarCh2 = {
        kAudioDevicePropertyVolumeScalar,
        kAudioDevicePropertyScopeOutput,
        2
    };
    chOk = TrySetFloatProperty(deviceID, addrScalarCh2, volume) || chOk;

    return chOk;
}

// 获取静音状态
bool GetOutputMute(AudioDeviceID deviceID, UInt32* mute) {
    // 1) 设备级（Main）
    AudioObjectPropertyAddress addrMain = {
        kAudioDevicePropertyMute,
        kAudioDevicePropertyScopeOutput,
        kAudioObjectPropertyElementMain
    };
    if (TryGetUInt32Property(deviceID, addrMain, mute)) {
        return true;
    }

    // 2) 设备级（Master）
    AudioObjectPropertyAddress addrMaster = {
        kAudioDevicePropertyMute,
        kAudioDevicePropertyScopeOutput,
        kAudioObjectPropertyElementMaster
    };
    if (TryGetUInt32Property(deviceID, addrMaster, mute)) {
        return true;
    }

    // 3) 通道级（1/2），任意通道静音则视为静音
    UInt32 muteCh = 0;
    AudioObjectPropertyAddress addrCh1 = {
        kAudioDevicePropertyMute,
        kAudioDevicePropertyScopeOutput,
        1
    };
    if (TryGetUInt32Property(deviceID, addrCh1, &muteCh) && muteCh) {
        *mute = 1;
        return true;
    }
    AudioObjectPropertyAddress addrCh2 = {
        kAudioDevicePropertyMute,
        kAudioDevicePropertyScopeOutput,
        2
    };
    if (TryGetUInt32Property(deviceID, addrCh2, &muteCh)) {
        *mute = muteCh ? 1 : 0;
        return true;
    }

    return false;
}

// 设置静音状态
bool SetOutputMute(AudioDeviceID deviceID, UInt32 mute) {
    // 1) 设备级（Main）
    AudioObjectPropertyAddress addrMain = {
        kAudioDevicePropertyMute,
        kAudioDevicePropertyScopeOutput,
        kAudioObjectPropertyElementMain
    };
    if (TrySetUInt32Property(deviceID, addrMain, mute)) {
        return true;
    }

    // 2) 设备级（Master）
    AudioObjectPropertyAddress addrMaster = {
        kAudioDevicePropertyMute,
        kAudioDevicePropertyScopeOutput,
        kAudioObjectPropertyElementMaster
    };
    if (TrySetUInt32Property(deviceID, addrMaster, mute)) {
        return true;
    }

    // 3) 通道级（1/2），任意一个成功即视为成功
    bool ok = false;
    AudioObjectPropertyAddress addrCh1 = {
        kAudioDevicePropertyMute,
        kAudioDevicePropertyScopeOutput,
        1
    };
    ok = TrySetUInt32Property(deviceID, addrCh1, mute) || ok;
    AudioObjectPropertyAddress addrCh2 = {
        kAudioDevicePropertyMute,
        kAudioDevicePropertyScopeOutput,
        2
    };
    ok = TrySetUInt32Property(deviceID, addrCh2, mute) || ok;
    return ok;
}

// 获取默认输入设备ID
AudioDeviceID GetDefaultInputDevice() {
    AudioDeviceID deviceID = kAudioObjectUnknown;
    UInt32 size = sizeof(deviceID);
    
    AudioObjectPropertyAddress address = {
        kAudioHardwarePropertyDefaultInputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    
    OSStatus status = AudioObjectGetPropertyData(
        kAudioObjectSystemObject,
        &address,
        0,
        NULL,
        &size,
        &deviceID
    );
    
    if (status != noErr) {
        return kAudioObjectUnknown;
    }
    
    return deviceID;
}

// 获取输入设备音量
bool GetInputVolume(AudioDeviceID deviceID, Float32* volume) {
    // 1) VirtualMainVolume (Main)
    AudioObjectPropertyAddress addrVirtualMainMain = {
        kAudioHardwareServiceDeviceProperty_VirtualMainVolume,
        kAudioDevicePropertyScopeInput,
        kAudioObjectPropertyElementMain
    };
    if (TryGetFloatProperty(deviceID, addrVirtualMainMain, volume)) {
        return true;
    }

    // 2) VirtualMainVolume (Master)
    AudioObjectPropertyAddress addrVirtualMainMaster = {
        kAudioHardwareServiceDeviceProperty_VirtualMainVolume,
        kAudioDevicePropertyScopeInput,
        kAudioObjectPropertyElementMaster
    };
    if (TryGetFloatProperty(deviceID, addrVirtualMainMaster, volume)) {
        return true;
    }

    // 3) VolumeScalar (Master)
    AudioObjectPropertyAddress addrScalarMaster = {
        kAudioDevicePropertyVolumeScalar,
        kAudioDevicePropertyScopeInput,
        kAudioObjectPropertyElementMaster
    };
    if (TryGetFloatProperty(deviceID, addrScalarMaster, volume)) {
        return true;
    }

    // 4) 按通道 VolumeScalar（平均）
    Float32 ch1 = -1.0f, ch2 = -1.0f;
    AudioObjectPropertyAddress addrScalarCh1 = {
        kAudioDevicePropertyVolumeScalar,
        kAudioDevicePropertyScopeInput,
        1
    };
    if (TryGetFloatProperty(deviceID, addrScalarCh1, &ch1)) {
        *volume = ch1;
    }
    AudioObjectPropertyAddress addrScalarCh2 = {
        kAudioDevicePropertyVolumeScalar,
        kAudioDevicePropertyScopeInput,
        2
    };
    if (TryGetFloatProperty(deviceID, addrScalarCh2, &ch2)) {
        if (ch1 >= 0.0f) {
            *volume = (ch1 + ch2) / 2.0f;
        } else {
            *volume = ch2;
        }
    }
    return (ch1 >= 0.0f) || (ch2 >= 0.0f);
}

// 设置输入设备音量
bool SetInputVolume(AudioDeviceID deviceID, Float32 volume) {
    // 1) VirtualMainVolume (Main)
    AudioObjectPropertyAddress addrVirtualMainMain = {
        kAudioHardwareServiceDeviceProperty_VirtualMainVolume,
        kAudioDevicePropertyScopeInput,
        kAudioObjectPropertyElementMain
    };
    if (TrySetFloatProperty(deviceID, addrVirtualMainMain, volume)) {
        return true;
    }

    // 2) VirtualMainVolume (Master)
    AudioObjectPropertyAddress addrVirtualMainMaster = {
        kAudioHardwareServiceDeviceProperty_VirtualMainVolume,
        kAudioDevicePropertyScopeInput,
        kAudioObjectPropertyElementMaster
    };
    if (TrySetFloatProperty(deviceID, addrVirtualMainMaster, volume)) {
        return true;
    }

    // 3) VolumeScalar (Master)
    AudioObjectPropertyAddress addrScalarMaster = {
        kAudioDevicePropertyVolumeScalar,
        kAudioDevicePropertyScopeInput,
        kAudioObjectPropertyElementMaster
    };
    if (TrySetFloatProperty(deviceID, addrScalarMaster, volume)) {
        return true;
    }

    // 4) 按通道设置（1/2），任意成功即视为成功
    bool ok = false;
    AudioObjectPropertyAddress addrScalarCh1 = {
        kAudioDevicePropertyVolumeScalar,
        kAudioDevicePropertyScopeInput,
        1
    };
    ok = TrySetFloatProperty(deviceID, addrScalarCh1, volume) || ok;
    AudioObjectPropertyAddress addrScalarCh2 = {
        kAudioDevicePropertyVolumeScalar,
        kAudioDevicePropertyScopeInput,
        2
    };
    ok = TrySetFloatProperty(deviceID, addrScalarCh2, volume) || ok;
    return ok;
}

// 获取输入设备静音状态
bool GetInputMute(AudioDeviceID deviceID, UInt32* mute) {
    // 1) 设备级（Main）
    AudioObjectPropertyAddress addrMain = {
        kAudioDevicePropertyMute,
        kAudioDevicePropertyScopeInput,
        kAudioObjectPropertyElementMain
    };
    if (TryGetUInt32Property(deviceID, addrMain, mute)) {
        return true;
    }

    // 2) 设备级（Master）
    AudioObjectPropertyAddress addrMaster = {
        kAudioDevicePropertyMute,
        kAudioDevicePropertyScopeInput,
        kAudioObjectPropertyElementMaster
    };
    if (TryGetUInt32Property(deviceID, addrMaster, mute)) {
        return true;
    }

    // 3) 通道级（1/2），任意通道静音则视为静音
    UInt32 muteCh = 0;
    AudioObjectPropertyAddress addrCh1 = {
        kAudioDevicePropertyMute,
        kAudioDevicePropertyScopeInput,
        1
    };
    if (TryGetUInt32Property(deviceID, addrCh1, &muteCh) && muteCh) {
        *mute = 1;
        return true;
    }
    AudioObjectPropertyAddress addrCh2 = {
        kAudioDevicePropertyMute,
        kAudioDevicePropertyScopeInput,
        2
    };
    if (TryGetUInt32Property(deviceID, addrCh2, &muteCh)) {
        *mute = muteCh ? 1 : 0;
        return true;
    }
    return false;
}

// 设置输入设备静音状态
bool SetInputMute(AudioDeviceID deviceID, UInt32 mute) {
    // 1) 设备级（Main）
    AudioObjectPropertyAddress addrMain = {
        kAudioDevicePropertyMute,
        kAudioDevicePropertyScopeInput,
        kAudioObjectPropertyElementMain
    };
    if (TrySetUInt32Property(deviceID, addrMain, mute)) {
        return true;
    }

    // 2) 设备级（Master）
    AudioObjectPropertyAddress addrMaster = {
        kAudioDevicePropertyMute,
        kAudioDevicePropertyScopeInput,
        kAudioObjectPropertyElementMaster
    };
    if (TrySetUInt32Property(deviceID, addrMaster, mute)) {
        return true;
    }

    // 3) 通道级（1/2），任意成功即视为成功
    bool ok = false;
    AudioObjectPropertyAddress addrCh1 = {
        kAudioDevicePropertyMute,
        kAudioDevicePropertyScopeInput,
        1
    };
    ok = TrySetUInt32Property(deviceID, addrCh1, mute) || ok;
    AudioObjectPropertyAddress addrCh2 = {
        kAudioDevicePropertyMute,
        kAudioDevicePropertyScopeInput,
        2
    };
    ok = TrySetUInt32Property(deviceID, addrCh2, mute) || ok;
    return ok;
}

// GetMediaState - 获取媒体状态
Napi::Value GetMediaState(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    // 检查参数
    if (info.Length() < 1 || !info[0].IsObject()) {
        Napi::TypeError::New(env, "参数必须是一个对象").ThrowAsJavaScriptException();
        return env.Null();
    }
    
    Napi::Object options = info[0].As<Napi::Object>();
    Napi::Object result = Napi::Object::New(env);
    
    // 处理speaker（扬声器/输出）
    if (options.Has("speaker") && options.Get("speaker").IsBoolean() && 
        options.Get("speaker").As<Napi::Boolean>().Value()) {
        
        AudioDeviceID outputDevice = GetDefaultOutputDevice();
        
        if (outputDevice != kAudioObjectUnknown) {
            Napi::Object speaker = Napi::Object::New(env);
            
            // 获取音量
            Float32 volume = 0.0f;
            if (GetOutputVolume(outputDevice, &volume)) {
                // 转换为0-100的整数
                speaker.Set("volume", Napi::Number::New(env, (int)(volume * 100)));
            } else {
                speaker.Set("volume", env.Null());
            }
            
            // 获取静音状态
            UInt32 mute = 0;
            if (GetOutputMute(outputDevice, &mute)) {
                speaker.Set("mute", Napi::Boolean::New(env, mute != 0));
            } else {
                speaker.Set("mute", env.Null());
            }
            
            result.Set("speaker", speaker);
        }
    }
    
    // 处理mic（麦克风/输入）
    if (options.Has("mic") && options.Get("mic").IsBoolean() && 
        options.Get("mic").As<Napi::Boolean>().Value()) {
        
        AudioDeviceID inputDevice = GetDefaultInputDevice();
        
        if (inputDevice != kAudioObjectUnknown) {
            Napi::Object mic = Napi::Object::New(env);
            
            // 获取音量
            Float32 volume = 0.0f;
            if (GetInputVolume(inputDevice, &volume)) {
                mic.Set("volume", Napi::Number::New(env, (int)(volume * 100)));
            } else {
                mic.Set("volume", env.Null());
            }
            
            // 获取静音状态
            UInt32 mute = 0;
            if (GetInputMute(inputDevice, &mute)) {
                mic.Set("mute", Napi::Boolean::New(env, mute != 0));
            } else {
                mic.Set("mute", env.Null());
            }
            
            result.Set("mic", mic);
        }
    }
    
    return result;
}

// SetMediaState - 设置媒体状态
Napi::Value SetMediaState(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    // 检查参数
    if (info.Length() < 1 || !info[0].IsObject()) {
        Napi::TypeError::New(env, "参数必须是一个对象").ThrowAsJavaScriptException();
        return env.Null();
    }
    
    Napi::Object options = info[0].As<Napi::Object>();
    bool success = true;
    
    // 处理speaker（扬声器/输出）
    if (options.Has("speaker") && options.Get("speaker").IsObject()) {
        Napi::Object speaker = options.Get("speaker").As<Napi::Object>();
        AudioDeviceID outputDevice = GetDefaultOutputDevice();
        
        if (outputDevice != kAudioObjectUnknown) {
            // 设置音量
            if (speaker.Has("volume")) {
                Napi::Value volumeValue = speaker.Get("volume");
                if (volumeValue.IsNumber()) {
                    int volumeInt = volumeValue.As<Napi::Number>().Int32Value();
                    Float32 volume = volumeInt / 100.0f;
                    
                    // 限制范围 0.0 - 1.0
                    if (volume < 0.0f) volume = 0.0f;
                    if (volume > 1.0f) volume = 1.0f;
                    
                    if (!SetOutputVolume(outputDevice, volume)) {
                        success = false;
                    }
                }
            }
            
            // 设置静音
            if (speaker.Has("mute")) {
                Napi::Value muteValue = speaker.Get("mute");
                if (muteValue.IsBoolean()) {
                    UInt32 mute = muteValue.As<Napi::Boolean>().Value() ? 1 : 0;
                    if (!SetOutputMute(outputDevice, mute)) {
                        success = false;
                    }
                }
            }
        } else {
            success = false;
        }
    }
    
    // 处理mic（麦克风/输入）
    if (options.Has("mic") && options.Get("mic").IsObject()) {
        Napi::Object mic = options.Get("mic").As<Napi::Object>();
        AudioDeviceID inputDevice = GetDefaultInputDevice();
        
        if (inputDevice != kAudioObjectUnknown) {
            // 设置音量
            if (mic.Has("volume")) {
                Napi::Value volumeValue = mic.Get("volume");
                if (volumeValue.IsNumber()) {
                    int volumeInt = volumeValue.As<Napi::Number>().Int32Value();
                    Float32 volume = volumeInt / 100.0f;
                    
                    // 限制范围 0.0 - 1.0
                    if (volume < 0.0f) volume = 0.0f;
                    if (volume > 1.0f) volume = 1.0f;
                    
                    if (!SetInputVolume(inputDevice, volume)) {
                        success = false;
                    }
                }
            }
            
            // 设置静音
            if (mic.Has("mute")) {
                Napi::Value muteValue = mic.Get("mute");
                if (muteValue.IsBoolean()) {
                    UInt32 mute = muteValue.As<Napi::Boolean>().Value() ? 1 : 0;
                    if (!SetInputMute(inputDevice, mute)) {
                        success = false;
                    }
                }
            }
        } else {
            success = false;
        }
    }
    
    return Napi::Boolean::New(env, success);
}

} // namespace MacService


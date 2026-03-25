#include <napi.h>
#include "SystemModuleManager.h"
#include <iostream>
#include <thread>
#include <cmath>
#include <atomic>

// 全局管理器实例声明（已在头文件中定义）

// 全局宏状态回调（统一处理录制事件和状态变更）
Napi::ThreadSafeFunction g_macro_status_callback;

// 全局关闭标志，用于防止在模块卸载时访问已释放的资源
static std::atomic<bool> g_module_shutting_down{false};

// 初始化函数
Napi::Value Init(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    // 重置关闭标志
    g_module_shutting_down = false;

    if (g_system_manager) {
        // 已经初始化过了
        return Napi::Boolean::New(env, true);
    }

    g_system_manager = new SystemModuleManager();

    if (!g_system_manager->Initialize()) {
        delete g_system_manager;
        g_system_manager = nullptr;
        Napi::Error::New(env, "Failed to initialize SystemModuleManager").ThrowAsJavaScriptException();
        return Napi::Boolean::New(env, false);
    }

    return Napi::Boolean::New(env, true);
}

// 关闭函数
Napi::Value Shutdown(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    // 设置关闭标志，阻止新的回调
    g_module_shutting_down = true;

    // 释放线程安全回调
    if (g_macro_status_callback) {
        g_macro_status_callback.Release();
    }

    if (g_system_manager) {
        g_system_manager->Shutdown();
        delete g_system_manager;
        g_system_manager = nullptr;
    }

    return env.Undefined();
}

// 发送键盘输入函数
Napi::Value SendKey(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1) {
        Napi::TypeError::New(env, "Wrong number of arguments").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (!g_system_manager) {
        Napi::Error::New(env, "SystemModuleManager not initialized").ThrowAsJavaScriptException();
        return env.Null();
    }

    std::string input;
    if (info[0].IsString()) {
        input = info[0].As<Napi::String>().Utf8Value();
    } else if (info[0].IsObject()) {
        Napi::Object obj = info[0].As<Napi::Object>();
        if (obj.Has("input")) {
            // 处理Python格式的输入对象
            Napi::Value inputValue = obj.Get("input");
            if (inputValue.IsString()) {
                input = inputValue.As<Napi::String>().Utf8Value();
            }
        } else if (obj.Has("hotkey")) {
            // 处理热键输入
            Napi::Value hotkeyValue = obj.Get("hotkey");
            if (hotkeyValue.IsString() && hotkeyValue.As<Napi::String>().Utf8Value() == "paste") {
                input = "paste";
            }
        }
    }

    if (input.empty()) {
        Napi::TypeError::New(env, "Invalid input parameter").ThrowAsJavaScriptException();
        return env.Null();
    }

    bool result = g_system_manager->SendKeyInput(input);

    return Napi::Boolean::New(env, result);
}

// 设置媒体状态函数
Napi::Value SetMediaState(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1) {
        Napi::TypeError::New(env, "Wrong number of arguments").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (!g_system_manager) {
        Napi::Error::New(env, "SystemModuleManager not initialized").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (!info[0].IsObject()) {
        Napi::TypeError::New(env, "Argument must be an object").ThrowAsJavaScriptException();
        return env.Null();
    }

    Napi::Object data = info[0].As<Napi::Object>();

    bool result = true;

    // 处理扬声器设置
    if (data.Has("speaker")) {
        Napi::Object speaker = data.Get("speaker").As<Napi::Object>();
        float volume = -1.0f; // -1表示不设置音量
        bool mute = false;
        bool hasVolume = false;
        bool hasMute = false;

        if (speaker.Has("volume")) {
            volume = speaker.Get("volume").As<Napi::Number>().FloatValue() / 100.0f; // 转换为0-1范围
            hasVolume = true;
        }
        if (speaker.Has("mute")) {
            mute = speaker.Get("mute").As<Napi::Boolean>().Value();
            hasMute = true;
        }

        // 如果有任何设置，则执行设置操作
        if (hasVolume || hasMute) {
            result = g_system_manager->SetMediaState("speaker", volume, hasMute ? mute : -1) && result;
        }
    }

    // 处理麦克风设置
    if (data.Has("mic")) {
        Napi::Object mic = data.Get("mic").As<Napi::Object>();
        float volume = -1.0f; // -1表示不设置音量
        bool mute = false;
        bool hasVolume = false;
        bool hasMute = false;

        if (mic.Has("volume")) {
            volume = mic.Get("volume").As<Napi::Number>().FloatValue() / 100.0f; // 转换为0-1范围
            hasVolume = true;
        }
        if (mic.Has("mute")) {
            mute = mic.Get("mute").As<Napi::Boolean>().Value();
            hasMute = true;
        }

        // 如果有任何设置，则执行设置操作
        if (hasVolume || hasMute) {
            result = g_system_manager->SetMediaState("mic", volume, hasMute ? mute : -1) && result;
        }
    }

    // 处理输出设备设置
    if (data.Has("outputDevices") && data.Get("outputDevices").IsObject()) {
        Napi::Object outputDevices = data.Get("outputDevices").As<Napi::Object>();
        Napi::Array keys = outputDevices.GetPropertyNames();
        
        for (uint32_t i = 0; i < keys.Length(); i++) {
            Napi::Value key = keys.Get(i);
            if (!key.IsString()) continue;
            
            std::string deviceId = key.As<Napi::String>().Utf8Value();
            Napi::Value deviceValue = outputDevices.Get(key);
            
            if (!deviceValue.IsObject()) continue;
            
            Napi::Object deviceState = deviceValue.As<Napi::Object>();
            bool deviceResult = true;
            
            if (deviceState.Has("volume")) {
                float volume = deviceState.Get("volume").As<Napi::Number>().FloatValue() / 100.0f; // 转换为0-1范围
                deviceResult = g_system_manager->SetOutputDeviceVolume(deviceId, volume) && deviceResult;
            }
            
            if (deviceState.Has("mute")) {
                bool mute = deviceState.Get("mute").As<Napi::Boolean>().Value();
                deviceResult = g_system_manager->SetOutputDeviceMute(deviceId, mute) && deviceResult;
            }
            
            result = deviceResult && result;
        }
    }

    // 处理输入设备设置
    if (data.Has("inputDevices") && data.Get("inputDevices").IsObject()) {
        Napi::Object inputDevices = data.Get("inputDevices").As<Napi::Object>();
        Napi::Array keys = inputDevices.GetPropertyNames();
        
        for (uint32_t i = 0; i < keys.Length(); i++) {
            Napi::Value key = keys.Get(i);
            if (!key.IsString()) continue;
            
            std::string deviceId = key.As<Napi::String>().Utf8Value();
            Napi::Value deviceValue = inputDevices.Get(key);
            
            if (!deviceValue.IsObject()) continue;
            
            Napi::Object deviceState = deviceValue.As<Napi::Object>();
            bool deviceResult = true;
            
            if (deviceState.Has("volume")) {
                float volume = deviceState.Get("volume").As<Napi::Number>().FloatValue() / 100.0f; // 转换为0-1范围
                deviceResult = g_system_manager->SetInputDeviceVolume(deviceId, volume) && deviceResult;
            }
            
            if (deviceState.Has("mute")) {
                bool mute = deviceState.Get("mute").As<Napi::Boolean>().Value();
                deviceResult = g_system_manager->SetInputDeviceMute(deviceId, mute) && deviceResult;
            }
            
            result = deviceResult && result;
        }
    }

    return Napi::Boolean::New(env, result);
}

// 获取媒体状态函数
Napi::Value GetMediaState(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1) {
        Napi::TypeError::New(env, "Wrong number of arguments").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (!g_system_manager) {
        Napi::Error::New(env, "SystemModuleManager not initialized").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (!info[0].IsObject()) {
        Napi::TypeError::New(env, "Argument must be an object").ThrowAsJavaScriptException();
        return env.Null();
    }

    Napi::Object result = Napi::Object::New(env);

    Napi::Object data = info[0].As<Napi::Object>();

    // 获取扬声器状态
    if (data.Has("speaker")) {
        float volume;
        bool mute;

        if (g_system_manager->GetMediaState("speaker", volume, mute)) {
            Napi::Object speaker = Napi::Object::New(env);
            speaker.Set("volume", Napi::Number::New(env, static_cast<int>(std::round(volume * 100)))); // 转换为整数百分比
            speaker.Set("mute", Napi::Boolean::New(env, mute));
            result.Set("speaker", speaker);
        }
    }

    // 获取麦克风状态
    if (data.Has("mic")) {
        float volume;
        bool mute;

        if (g_system_manager->GetMediaState("mic", volume, mute)) {
            Napi::Object mic = Napi::Object::New(env);
            mic.Set("volume", Napi::Number::New(env, static_cast<int>(std::round(volume * 100)))); // 转换为整数百分比
            mic.Set("mute", Napi::Boolean::New(env, mute));
            result.Set("mic", mic);
        }
    }

    // 获取所有输出设备状态
    if (data.Has("outputDevices") && data.Get("outputDevices").IsBoolean() && data.Get("outputDevices").As<Napi::Boolean>().Value()) {
        std::vector<OutputDeviceInfo> devices = g_system_manager->GetAllOutputDevices();
        Napi::Array outputDevicesArray = Napi::Array::New(env, devices.size());
        
        for (size_t i = 0; i < devices.size(); i++) {
            const OutputDeviceInfo& device = devices[i];
            Napi::Object deviceObj = Napi::Object::New(env);
            deviceObj.Set("id", Napi::String::New(env, device.id));
            deviceObj.Set("name", Napi::String::New(env, device.name));
            
            Napi::Object stateObj = Napi::Object::New(env);
            stateObj.Set("volume", Napi::Number::New(env, static_cast<int>(std::round(device.volume * 100)))); // 转换为整数百分比
            stateObj.Set("mute", Napi::Boolean::New(env, device.mute));
            deviceObj.Set("state", stateObj);
            
            outputDevicesArray.Set(i, deviceObj);
        }
        
        result.Set("outputDevices", outputDevicesArray);
    }

    // 获取所有输入设备状态
    if (data.Has("inputDevices") && data.Get("inputDevices").IsBoolean() && data.Get("inputDevices").As<Napi::Boolean>().Value()) {
        std::vector<InputDeviceInfo> devices = g_system_manager->GetAllInputDevices();
        Napi::Array inputDevicesArray = Napi::Array::New(env, devices.size());
        
        for (size_t i = 0; i < devices.size(); i++) {
            const InputDeviceInfo& device = devices[i];
            Napi::Object deviceObj = Napi::Object::New(env);
            deviceObj.Set("id", Napi::String::New(env, device.id));
            deviceObj.Set("name", Napi::String::New(env, device.name));
            
            Napi::Object stateObj = Napi::Object::New(env);
            stateObj.Set("volume", Napi::Number::New(env, static_cast<int>(std::round(device.volume * 100)))); // 转换为整数百分比
            stateObj.Set("mute", Napi::Boolean::New(env, device.mute));
            deviceObj.Set("state", stateObj);
            
            inputDevicesArray.Set(i, deviceObj);
        }
        
        result.Set("inputDevices", inputDevicesArray);
    }

    return result;
}

// 获取图标函数
Napi::Value GetIcon(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1) {
        Napi::TypeError::New(env, "Wrong number of arguments").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (!info[0].IsObject()) {
        Napi::TypeError::New(env, "Argument must be an object").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (!g_system_manager) {
        Napi::Error::New(env, "SystemModuleManager not initialized").ThrowAsJavaScriptException();
        return env.Null();
    }

    Napi::Object data = info[0].As<Napi::Object>();

    // 获取文件路径数组（强制为数组，提高批量提取性能）
    std::vector<std::string> paths;
    if (data.Has("path")) {
        Napi::Value pathValue = data.Get("path");
        if (!pathValue.IsArray()) {
            Napi::TypeError::New(env, "path must be an array of strings").ThrowAsJavaScriptException();
            return env.Null();
        }
        Napi::Array pathArray = pathValue.As<Napi::Array>();
        for (uint32_t i = 0; i < pathArray.Length(); ++i) {
            Napi::Value item = pathArray.Get(i);
            if (item.IsString()) {
                paths.push_back(item.As<Napi::String>().Utf8Value());
            }
        }
    }

    // 图标大小
    int iconSize = 32;
    if (data.Has("size")) {
        iconSize = data.Get("size").As<Napi::Number>().Int32Value();
    }

    // BGR格式标志
    bool bgr = true;
    if (data.Has("bgr")) {
        bgr = data.Get("bgr").As<Napi::Boolean>().Value();
    }

    // Alpha通道标志
    bool alpha = true;
    if (data.Has("alpha")) {
        alpha = data.Get("alpha").As<Napi::Boolean>().Value();
    }

    // 提取图标（单次调用内对重复路径去重缓存，避免重复初始化相同资源）
    Napi::Object result = Napi::Object::New(env);
    Napi::Array icons = Napi::Array::New(env, paths.size());

    std::unordered_map<std::string, std::string> cache; // path -> base64
    cache.reserve(paths.size());

    for (size_t i = 0; i < paths.size(); ++i) {
        const std::string &p = paths[i];
        auto it = cache.find(p);
        if (it != cache.end()) {
            icons.Set(static_cast<uint32_t>(i), Napi::String::New(env, it->second));
            continue;
        }
        std::string iconBase64 = g_system_manager->GetIcon(p, iconSize, bgr, alpha);
        cache.emplace(p, iconBase64);
        icons.Set(static_cast<uint32_t>(i), Napi::String::New(env, iconBase64));
    }

    result.Set("icons", icons);
    return result;
}

// 获取所有音频会话函数
Napi::Value GetAllAudioSessions(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (!g_system_manager) {
        Napi::Error::New(env, "SystemModuleManager not initialized").ThrowAsJavaScriptException();
        return env.Null();
    }

    std::vector<AudioSessionInfo> sessions = g_system_manager->GetAllAudioSessions();

    Napi::Array result = Napi::Array::New(env, sessions.size());

    for (size_t i = 0; i < sessions.size(); ++i) {
        const AudioSessionInfo& session = sessions[i];

        Napi::Object sessionObj = Napi::Object::New(env);
        sessionObj.Set("name", Napi::String::New(env, session.name));
        sessionObj.Set("id", Napi::String::New(env, session.id));
        sessionObj.Set("path", Napi::String::New(env, session.path));
        sessionObj.Set("icon", Napi::String::New(env, session.icon));
        sessionObj.Set("volume", Napi::Number::New(env, static_cast<int>(std::round(session.volume * 100)))); // 转换为整数百分比

        result.Set(i, sessionObj);
    }

    return result;
}

// 设置音频会话音量函数
Napi::Value SetAudioSessionVolume(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 2) {
        Napi::TypeError::New(env, "Wrong number of arguments").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (!g_system_manager) {
        Napi::Error::New(env, "SystemModuleManager not initialized").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (!info[0].IsString()) {
        Napi::TypeError::New(env, "First argument must be a string (sessionId)").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (!info[1].IsNumber()) {
        Napi::TypeError::New(env, "Second argument must be a number (volume)").ThrowAsJavaScriptException();
        return env.Null();
    }

    std::string sessionId = info[0].As<Napi::String>().Utf8Value();
    float volume = info[1].As<Napi::Number>().FloatValue() / 100.0f; // 转换为0-1范围

    bool result = g_system_manager->SetAudioSessionVolume(sessionId, volume);

    return Napi::Boolean::New(env, result);
}

// 获取音频会话音量函数
Napi::Value GetAudioSessionVolume(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1) {
        Napi::TypeError::New(env, "Wrong number of arguments").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (!g_system_manager) {
        Napi::Error::New(env, "SystemModuleManager not initialized").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (!info[0].IsString()) {
        Napi::TypeError::New(env, "First argument must be a string (sessionId)").ThrowAsJavaScriptException();
        return env.Null();
    }

    std::string sessionId = info[0].As<Napi::String>().Utf8Value();

    float volume = g_system_manager->GetAudioSessionVolume(sessionId);

    if (volume < 0.0f) {
        return env.Null(); // 未找到会话
    }

    return Napi::Number::New(env, static_cast<int>(std::round(volume * 100))); // 转换为整数百分比
}

// 设置宏状态回调函数（统一处理录制事件和状态变更）
Napi::Value SetMacroStatusCallback(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    if (!g_system_manager) {
        Napi::Error::New(env, "SystemModuleManager not initialized").ThrowAsJavaScriptException();
        return env.Null();
    }
    
    // 如果传入null或undefined，清除回调
    if (info.Length() < 1 || !info[0].IsFunction()) {
        // 释放旧的回调
        if (g_macro_status_callback) {
            g_macro_status_callback.Release();
        }
        g_system_manager->SetMacroStatusCallback(nullptr);
        return Napi::Boolean::New(env, true);
    }
    
    Napi::Function callback = info[0].As<Napi::Function>();
    
    // 释放旧的回调
    if (g_macro_status_callback) {
        g_macro_status_callback.Release();
    }
    
    // 创建线程安全函数，添加错误处理
    g_macro_status_callback = Napi::ThreadSafeFunction::New(
        env,
        callback,
        "MacroStatusCallback",
        0,  // 无限队列
        1   // 单个线程调用
    );
    
    // 设置C++回调
    g_system_manager->SetMacroStatusCallback([](const MacroEvent* event, const MacroStatusEvent* statusEvent) {
        // 检查是否正在关闭
        if (g_module_shutting_down.load()) return;
        if (!g_macro_status_callback) return;
        
        // 复制数据以避免指针失效问题
        struct CallbackData {
            bool isStatusEvent;
            MacroEvent event;
            MacroStatusEvent statusEvent;
        };
        
        auto data = new CallbackData();
        if (statusEvent) {
            data->isStatusEvent = true;
            data->statusEvent = *statusEvent;
        } else if (event) {
            data->isStatusEvent = false;
            data->event = *event;
        } else {
            delete data;
            return;
        }
        
        // 使用 NonBlockingCall 避免阻塞，并添加错误处理
        auto status = g_macro_status_callback.NonBlockingCall(data, [](Napi::Env env, Napi::Function jsCallback, CallbackData* data) {
            try {
                Napi::HandleScope scope(env);
                
                // 构造统一的事件对象，格式为：{"event": "macro_recorder", "data": {...}}
                Napi::Object resultObj = Napi::Object::New(env);
                resultObj.Set("event", Napi::String::New(env, "macro_recorder"));
                
                Napi::Object dataObjOut = Napi::Object::New(env);
                
                if (data->isStatusEvent) {
                    // 状态事件：{"event": "macro_recorder", "data": {"status": "play started", "macroId": "xxx"}}
                    dataObjOut.Set("status", Napi::String::New(env, data->statusEvent.status));
                    dataObjOut.Set("macroId", Napi::String::New(env, data->statusEvent.macroId));
                } else {
                    // 录制事件：{"event": "macro_recorder", "data": {"status": "recording", "event": {...}}}
                    dataObjOut.Set("status", Napi::String::New(env, "recording"));
                    
                    const MacroEvent& evt = data->event;
                    Napi::Object eventObj = Napi::Object::New(env);
                    eventObj.Set("dt", Napi::Number::New(env, evt.dt));
                    eventObj.Set("evt", Napi::String::New(env, evt.evt));
                    
                    Napi::Array posArray = Napi::Array::New(env, 2);
                    posArray.Set(uint32_t(0), Napi::Number::New(env, evt.pos[0]));
                    posArray.Set(uint32_t(1), Napi::Number::New(env, evt.pos[1]));
                    eventObj.Set("pos", posArray);
                    
                    // 根据事件类型设置数据
                    Napi::Object eventDataObj = Napi::Object::New(env);
                    if (evt.evt == "mouse-btn-changed") {
                        Napi::Array buttonArray = Napi::Array::New(env, 3);
                        buttonArray.Set(uint32_t(0), Napi::Number::New(env, evt.data.button[0]));
                        buttonArray.Set(uint32_t(1), Napi::Number::New(env, evt.data.button[1]));
                        buttonArray.Set(uint32_t(2), Napi::Number::New(env, evt.data.button[2]));
                        eventDataObj.Set("button", buttonArray);
                    } else if (evt.evt == "mouse-wheel") {
                        eventDataObj.Set("dx", Napi::Number::New(env, evt.data.dx));
                        eventDataObj.Set("dy", Napi::Number::New(env, evt.data.dy));
                    } else if (evt.evt == "key-changed") {
                        Napi::Array keysArray = Napi::Array::New(env, evt.data.keys.size());
                        for (size_t j = 0; j < evt.data.keys.size(); ++j) {
                            keysArray.Set(uint32_t(j), Napi::Number::New(env, evt.data.keys[j]));
                        }
                        eventDataObj.Set("key", keysArray);
                        // 添加 key_name 字段
                        if (!evt.data.key_name.empty()) {
                            eventDataObj.Set("key_name", Napi::String::New(env, evt.data.key_name));
                        }
                    }
                    
                    eventObj.Set("data", eventDataObj);
                    dataObjOut.Set("event", eventObj);
                }
                
                resultObj.Set("data", dataObjOut);
                
                // 调用回调，捕获可能的异常
                jsCallback.Call({resultObj});
                
                // 检查是否有挂起的异常
                if (env.IsExceptionPending()) {
                    env.GetAndClearPendingException();
                    // 静默处理异常，不输出错误
                }
                
                // 释放数据
                delete data;
            } catch (const std::exception&) {
                // 捕获所有 C++ 异常，避免崩溃
                delete data;
            } catch (...) {
                // 捕获所有其他异常
                delete data;
            }
        });
        
        // 如果队列已关闭，忽略错误并清理数据
        if (status != napi_ok && status != napi_closing) {
            delete data;
        }
    });
    
    return Napi::Boolean::New(env, true);
}

// 宏录制开始函数
Napi::Value StartMacroRecording(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    if (!g_system_manager) {
        Napi::Error::New(env, "SystemModuleManager not initialized").ThrowAsJavaScriptException();
        return env.Null();
    }
    
    // 获取 macroId/blockInput（可选参数）
    std::string macroId = "";
    bool blockInput = false;
    if (info.Length() > 0 && info[0].IsObject()) {
        Napi::Object data = info[0].As<Napi::Object>();
        if (data.Has("macroId") && data.Get("macroId").IsString()) {
            macroId = data.Get("macroId").As<Napi::String>().Utf8Value();
        }
        if (data.Has("blockInput") && data.Get("blockInput").IsBoolean()) {
            blockInput = data.Get("blockInput").As<Napi::Boolean>().Value();
        }
    }
    
    bool result = g_system_manager->StartMacroRecording(macroId, blockInput);
    
    Napi::Object response = Napi::Object::New(env);
    response.Set("status", result ? "started" : "failed");
    
    return response;
}

// 宏录制停止函数
Napi::Value StopMacroRecording(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    if (!g_system_manager) {
        Napi::Error::New(env, "SystemModuleManager not initialized").ThrowAsJavaScriptException();
        return env.Null();
    }
    
    bool result = g_system_manager->StopMacroRecording();
    std::vector<MacroEvent> events = g_system_manager->GetMacroEvents();
    double duration = g_system_manager->GetRecordingDuration();
    
    Napi::Object response = Napi::Object::New(env);
    response.Set("status", result ? "stopped" : "failed");
    response.Set("duration", Napi::Number::New(env, duration));
    
    // 转换事件为JavaScript数组
    Napi::Array eventsArray = Napi::Array::New(env, events.size());
    for (size_t i = 0; i < events.size(); ++i) {
        const MacroEvent& event = events[i];
        Napi::Object eventObj = Napi::Object::New(env);
        
        eventObj.Set("dt", Napi::Number::New(env, event.dt));
        eventObj.Set("evt", Napi::String::New(env, event.evt));
        
        Napi::Array posArray = Napi::Array::New(env, 2);
        posArray.Set(uint32_t(0), Napi::Number::New(env, event.pos[0]));
        posArray.Set(uint32_t(1), Napi::Number::New(env, event.pos[1]));
        eventObj.Set("pos", posArray);
        
        // 根据事件类型设置数据
        Napi::Object dataObj = Napi::Object::New(env);
        if (event.evt == "mouse-btn-changed") {
            Napi::Array buttonArray = Napi::Array::New(env, 3);
            buttonArray.Set(uint32_t(0), Napi::Number::New(env, event.data.button[0]));
            buttonArray.Set(uint32_t(1), Napi::Number::New(env, event.data.button[1]));
            buttonArray.Set(uint32_t(2), Napi::Number::New(env, event.data.button[2]));
            dataObj.Set("button", buttonArray);
        } else if (event.evt == "mouse-wheel") {
            dataObj.Set("dx", Napi::Number::New(env, event.data.dx));
            dataObj.Set("dy", Napi::Number::New(env, event.data.dy));
        } else if (event.evt == "key-changed") {
            Napi::Array keysArray = Napi::Array::New(env, event.data.keys.size());
            for (size_t j = 0; j < event.data.keys.size(); ++j) {
                keysArray.Set(uint32_t(j), Napi::Number::New(env, event.data.keys[j]));
            }
            dataObj.Set("key", keysArray);
        }
        
        eventObj.Set("data", dataObj);
        eventsArray.Set(i, eventObj);
    }
    
    response.Set("events", eventsArray);
    
    return response;
}

// 获取录制结果函数
Napi::Value GetRecordingResult(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    if (!g_system_manager) {
        Napi::Error::New(env, "SystemModuleManager not initialized").ThrowAsJavaScriptException();
        return env.Null();
    }
    
    std::vector<MacroEvent> events = g_system_manager->GetMacroEvents();
    
    Napi::Object response = Napi::Object::New(env);
    
    // 转换事件为JavaScript数组
    Napi::Array eventsArray = Napi::Array::New(env, events.size());
    for (size_t i = 0; i < events.size(); ++i) {
        const MacroEvent& event = events[i];
        Napi::Object eventObj = Napi::Object::New(env);
        
        eventObj.Set("dt", Napi::Number::New(env, event.dt));
        eventObj.Set("evt", Napi::String::New(env, event.evt));
        
        Napi::Array posArray = Napi::Array::New(env, 2);
        posArray.Set(uint32_t(0), Napi::Number::New(env, event.pos[0]));
        posArray.Set(uint32_t(1), Napi::Number::New(env, event.pos[1]));
        eventObj.Set("pos", posArray);
        
        // 根据事件类型设置数据
        Napi::Object dataObj = Napi::Object::New(env);
        if (event.evt == "mouse-btn-changed") {
            Napi::Array buttonArray = Napi::Array::New(env, 3);
            buttonArray.Set(uint32_t(0), Napi::Number::New(env, event.data.button[0]));
            buttonArray.Set(uint32_t(1), Napi::Number::New(env, event.data.button[1]));
            buttonArray.Set(uint32_t(2), Napi::Number::New(env, event.data.button[2]));
            dataObj.Set("button", buttonArray);
        } else if (event.evt == "mouse-wheel") {
            dataObj.Set("dx", Napi::Number::New(env, event.data.dx));
            dataObj.Set("dy", Napi::Number::New(env, event.data.dy));
        } else if (event.evt == "key-changed") {
            Napi::Array keysArray = Napi::Array::New(env, event.data.keys.size());
            for (size_t j = 0; j < event.data.keys.size(); ++j) {
                keysArray.Set(uint32_t(j), Napi::Number::New(env, event.data.keys[j]));
            }
            dataObj.Set("key", keysArray);
        }
        
        eventObj.Set("data", dataObj);
        eventsArray.Set(i, eventObj);
    }
    
    response.Set("events", eventsArray);
    
    return response;
}

// 宏播放开始函数
Napi::Value StartMacroPlayback(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    if (!g_system_manager) {
        Napi::Error::New(env, "SystemModuleManager not initialized").ThrowAsJavaScriptException();
        return env.Null();
    }
    
    if (info.Length() < 1 || !info[0].IsObject()) {
        Napi::TypeError::New(env, "Argument must be an object").ThrowAsJavaScriptException();
        return env.Null();
    }
    
    Napi::Object data = info[0].As<Napi::Object>();
    
    // 解析事件数组
    std::vector<MacroEvent> events;
    if (data.Has("events") && data.Get("events").IsArray()) {
        Napi::Array eventsArray = data.Get("events").As<Napi::Array>();
        
        for (uint32_t i = 0; i < eventsArray.Length(); ++i) {
            Napi::Object eventObj = eventsArray.Get(i).As<Napi::Object>();
            MacroEvent event;
            
            event.dt = eventObj.Get("dt").As<Napi::Number>().Uint32Value();
            event.evt = eventObj.Get("evt").As<Napi::String>().Utf8Value();
            
            if (eventObj.Has("pos") && eventObj.Get("pos").IsArray()) {
                Napi::Array posArray = eventObj.Get("pos").As<Napi::Array>();
                event.pos[0] = posArray.Get(uint32_t(0)).As<Napi::Number>().Int32Value();
                event.pos[1] = posArray.Get(uint32_t(1)).As<Napi::Number>().Int32Value();
            } else {
                event.pos[0] = 0;
                event.pos[1] = 0;
            }
            
            // 解析数据字段
            if (eventObj.Has("data") && eventObj.Get("data").IsObject()) {
                Napi::Object dataObj = eventObj.Get("data").As<Napi::Object>();
                
                if (event.evt == "mouse-btn-changed" && dataObj.Has("button")) {
                    Napi::Array buttonArray = dataObj.Get("button").As<Napi::Array>();
                    event.data.button[0] = buttonArray.Get(uint32_t(0)).As<Napi::Number>().Int32Value();
                    event.data.button[1] = buttonArray.Get(uint32_t(1)).As<Napi::Number>().Int32Value();
                    event.data.button[2] = buttonArray.Get(uint32_t(2)).As<Napi::Number>().Int32Value();
                } else if (event.evt == "mouse-wheel") {
                    if (dataObj.Has("dx")) {
                        event.data.dx = dataObj.Get("dx").As<Napi::Number>().Int32Value();
                    }
                    if (dataObj.Has("dy")) {
                        event.data.dy = dataObj.Get("dy").As<Napi::Number>().Int32Value();
                    }
                } else if (event.evt == "key-changed" && dataObj.Has("key")) {
                    Napi::Array keysArray = dataObj.Get("key").As<Napi::Array>();
                    for (uint32_t j = 0; j < keysArray.Length(); ++j) {
                        event.data.keys.push_back(keysArray.Get(j).As<Napi::Number>().Uint32Value());
                    }
                }
            }
            
            events.push_back(event);
        }
    }
    
    // 获取播放参数
    std::string macroId = "";
    if (data.Has("macroId") && data.Get("macroId").IsString()) {
        macroId = data.Get("macroId").As<Napi::String>().Utf8Value();
    }
    
    double speed = 1.0;
    if (data.Has("speed")) {
        speed = data.Get("speed").As<Napi::Number>().DoubleValue();
    }
    
    int loop = 1;
    if (data.Has("loop")) {
        loop = data.Get("loop").As<Napi::Number>().Int32Value();
    }
    
    int smoothingFactor = 1;
    if (data.Has("smoothing_factor")) {
        smoothingFactor = data.Get("smoothing_factor").As<Napi::Number>().Int32Value();
    }
    
    bool result = g_system_manager->StartMacroPlayback(events, macroId, speed, loop, smoothingFactor);
    
    Napi::Object response = Napi::Object::New(env);
    response.Set("status", result ? "play started" : "failed");
    
    return response;
}

// 宏播放停止函数
Napi::Value StopMacroPlayback(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    if (!g_system_manager) {
        Napi::Error::New(env, "SystemModuleManager not initialized").ThrowAsJavaScriptException();
        return env.Null();
    }
    
    g_system_manager->StopMacroPlayback();
    
    Napi::Object response = Napi::Object::New(env);
    response.Set("status", "play stopped");
    
    return response;
}

// 处理宏录制器函数（集成接口）
Napi::Value MacroRecorder(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    if (info.Length() < 1 || !info[0].IsObject()) {
        Napi::TypeError::New(env, "Argument must be an object").ThrowAsJavaScriptException();
        return env.Null();
    }
    
    Napi::Object data = info[0].As<Napi::Object>();
    
    if (!data.Has("action")) {
        Napi::TypeError::New(env, "Missing 'action' field").ThrowAsJavaScriptException();
        return env.Null();
    }
    
    std::string action = data.Get("action").As<Napi::String>().Utf8Value();
    
    if (action == "record_start") {
        return StartMacroRecording(info);
    } else if (action == "record_stop") {
        return StopMacroRecording(info);
    } else if (action == "record_get_result") {
        return GetRecordingResult(info);
    } else if (action == "play_start") {
        return StartMacroPlayback(info);
    } else if (action == "play_stop") {
        return StopMacroPlayback(info);
    } else {
        Napi::Error::New(env, "Unknown action: " + action).ThrowAsJavaScriptException();
        return env.Null();
    }
}

// 模块初始化函数
Napi::Object InitModule(Napi::Env env, Napi::Object exports) {
    // 导出函数
    exports.Set(Napi::String::New(env, "init"),
                Napi::Function::New(env, Init));

    exports.Set(Napi::String::New(env, "shutdown"),
                Napi::Function::New(env, Shutdown));

    exports.Set(Napi::String::New(env, "sendKey"),
                Napi::Function::New(env, SendKey));

    exports.Set(Napi::String::New(env, "setMediaState"),
                Napi::Function::New(env, SetMediaState));

    exports.Set(Napi::String::New(env, "getMediaState"),
                Napi::Function::New(env, GetMediaState));

    exports.Set(Napi::String::New(env, "getIcon"),
                Napi::Function::New(env, GetIcon));

    exports.Set(Napi::String::New(env, "getAllAudioSessions"),
                Napi::Function::New(env, GetAllAudioSessions));

    exports.Set(Napi::String::New(env, "setAudioSessionVolume"),
                Napi::Function::New(env, SetAudioSessionVolume));

    exports.Set(Napi::String::New(env, "getAudioSessionVolume"),
                Napi::Function::New(env, GetAudioSessionVolume));
    
    // 宏录制功能
    exports.Set(Napi::String::New(env, "macroRecorder"),
                Napi::Function::New(env, MacroRecorder));
    
    exports.Set(Napi::String::New(env, "setMacroStatusCallback"),
                Napi::Function::New(env, SetMacroStatusCallback));
    
    // 保留旧的函数名以保持兼容性
    exports.Set(Napi::String::New(env, "setMacroRecordingCallback"),
                Napi::Function::New(env, SetMacroStatusCallback));
    
    exports.Set(Napi::String::New(env, "startMacroRecording"),
                Napi::Function::New(env, StartMacroRecording));
    
    exports.Set(Napi::String::New(env, "stopMacroRecording"),
                Napi::Function::New(env, StopMacroRecording));
    
    exports.Set(Napi::String::New(env, "startMacroPlayback"),
                Napi::Function::New(env, StartMacroPlayback));
    
    exports.Set(Napi::String::New(env, "stopMacroPlayback"),
                Napi::Function::New(env, StopMacroPlayback));

    return exports;
}

// 模块定义
NODE_API_MODULE(windows_system_module, InitModule)

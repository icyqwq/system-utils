#include "macro.h"
#import <Cocoa/Cocoa.h>
#import <CoreGraphics/CoreGraphics.h>
#import <Carbon/Carbon.h>
#include <vector>
#include <map>
#include <set>
#include <chrono>
#include <thread>
#include <atomic>
#include <fstream>

// 调试日志（仅在DEBUG模式下启用）
#ifdef DEBUG_MACRO
    #define DEBUG_LOG(msg) NSLog(@"[Macro] %s", msg)
#else
    #define DEBUG_LOG(msg) do {} while(0)
#endif

namespace MacService {

// 事件结构
struct MacroEvent {
    int dt;  // 时间增量（毫秒）
    std::string evt;  // 事件类型
    int pos[2];  // 位置 [x, y]
    // 鼠标按钮状态
    int buttons[3];  // [Left, Middle, Right]
    // 滚轮数据
    int wheel_dx;
    int wheel_dy;
    int wheel_unit; // 0 = line, 1 = pixel
    // 键盘按键列表（按下中的虚拟键码）
    std::vector<int> keys;

    MacroEvent(): dt(0), evt(), pos{0,0}, buttons{0,0,0}, wheel_dx(0), wheel_dy(0), wheel_unit(0), keys() {}
};

// 全局状态
static std::vector<MacroEvent> g_recorded_events;
static std::atomic<bool> g_is_recording(false);
static std::atomic<bool> g_is_playing(false);
static std::atomic<bool> g_block_input(false);
static std::chrono::steady_clock::time_point g_record_start_time;
static std::chrono::steady_clock::time_point g_last_event_time;
static CFMachPortRef g_event_tap = nullptr;
static CFRunLoopSourceRef g_runloop_source = nullptr;
static std::thread g_record_thread;
static std::thread g_play_thread;
static Napi::ThreadSafeFunction g_status_callback;

// 当前状态
static std::map<CGKeyCode, bool> g_current_keys;
static bool g_current_buttons[3] = {false, false, false};  // [左, 中, 右]
static CGPoint g_last_pos = {0, 0};

// Macro IDs
static std::string g_recording_macro_id;
static std::string g_playing_macro_id;

// 键码到名称的映射（简化版）
static std::map<int, std::string> g_vk_to_name = {
    {0x00, "A"}, {0x01, "S"}, {0x02, "D"}, {0x03, "F"}, {0x04, "H"},
    {0x05, "G"}, {0x06, "Z"}, {0x07, "X"}, {0x08, "C"}, {0x09, "V"},
    {0x0B, "B"}, {0x0C, "Q"}, {0x0D, "W"}, {0x0E, "E"}, {0x0F, "R"},
    {0x10, "Y"}, {0x11, "T"}, {0x12, "1"}, {0x13, "2"}, {0x14, "3"},
    {0x15, "4"}, {0x16, "6"}, {0x17, "5"}, {0x18, "="}, {0x19, "9"},
    {0x1A, "7"}, {0x1B, "-"}, {0x1C, "8"}, {0x1D, "0"}, {0x1E, "]"},
    {0x1F, "O"}, {0x20, "U"}, {0x21, "["}, {0x22, "I"}, {0x23, "P"},
    {0x24, "Enter"}, {0x25, "L"}, {0x26, "J"}, {0x27, "'"}, {0x28, "K"},
    {0x29, ";"}, {0x2A, "\\"}, {0x2B, ","}, {0x2C, "/"}, {0x2D, "N"},
    {0x2E, "M"}, {0x2F, "."}, {0x30, "Tab"}, {0x31, "Space"},
    {0x32, "`"}, {0x33, "Delete"}, {0x35, "Esc"},
    {0x37, "⌘"}, {0x38, "⇧"}, {0x39, "Caps Lock"}, {0x3A, "⌥"}, {0x3B, "^"},
    {0x3C, "⇧R"}, {0x3D, "⌥R"}, {0x3E, "^R"}, {0x3F, "Fn"},
    {0x7A, "F1"}, {0x78, "F2"}, {0x63, "F3"}, {0x76, "F4"}, {0x60, "F5"},
    {0x61, "F6"}, {0x62, "F7"}, {0x64, "F8"}, {0x65, "F9"}, {0x6D, "F10"},
    {0x67, "F11"}, {0x6F, "F12"}, {0x7B, "Left Arrow"}, {0x7C, "Right Arrow"},
    {0x7D, "Down Arrow"}, {0x7E, "Up Arrow"}
};

// 辅助函数：触发状态回调
struct StatusCallbackData {
    std::string status;
    std::string macro_id;
    MacroEvent* event;
    std::string key_name;
};

void triggerStatusCallback(const std::string& status, const std::string& macro_id, MacroEvent* evt = nullptr, const std::string& key_name = "") {
    if (!g_status_callback) {
        if (evt) delete evt;
        return;
    }
    
    auto callback_data = new StatusCallbackData{status, macro_id, evt, key_name};
    g_status_callback.NonBlockingCall(callback_data, [](Napi::Env env, Napi::Function jsCallback, StatusCallbackData* data) {
        // 构建回调对象：{"event": "macro_recorder", "data": {...}}
        Napi::Object payload = Napi::Object::New(env);
        payload.Set("event", Napi::String::New(env, "macro_recorder"));
        
        Napi::Object eventData = Napi::Object::New(env);
        eventData.Set("status", Napi::String::New(env, data->status));
        
        if (!data->macro_id.empty()) {
            eventData.Set("macroId", Napi::String::New(env, data->macro_id));
        }
        
        // 如果有事件数据（recording 状态）
        if (data->event) {
            Napi::Object event = Napi::Object::New(env);
            event.Set("dt", Napi::Number::New(env, data->event->dt));
            event.Set("evt", Napi::String::New(env, data->event->evt));
            
            Napi::Array pos = Napi::Array::New(env, 2);
            pos[uint32_t(0)] = Napi::Number::New(env, data->event->pos[0]);
            pos[uint32_t(1)] = Napi::Number::New(env, data->event->pos[1]);
            event.Set("pos", pos);
            
            // 根据事件类型设置data字段
            if (data->event->evt == "mouse-btn-changed") {
                Napi::Object evtData = Napi::Object::New(env);
                Napi::Array buttons = Napi::Array::New(env, 3);
                buttons[uint32_t(0)] = Napi::Number::New(env, data->event->buttons[0]);
                buttons[uint32_t(1)] = Napi::Number::New(env, data->event->buttons[1]);
                buttons[uint32_t(2)] = Napi::Number::New(env, data->event->buttons[2]);
                evtData.Set("button", buttons);
                event.Set("data", evtData);
            } else if (data->event->evt == "mouse-wheel") {
                Napi::Object evtData = Napi::Object::New(env);
                evtData.Set("dx", Napi::Number::New(env, data->event->wheel_dx));
                evtData.Set("dy", Napi::Number::New(env, data->event->wheel_dy));
                event.Set("data", evtData);
            } else if (data->event->evt == "key-changed") {
                Napi::Object evtData = Napi::Object::New(env);
                Napi::Array keys = Napi::Array::New(env, data->event->keys.size());
                for (size_t i = 0; i < data->event->keys.size(); ++i) {
                    keys[uint32_t(i)] = Napi::Number::New(env, data->event->keys[i]);
                }
                evtData.Set("key", keys);
                if (!data->key_name.empty()) {
                    evtData.Set("key_name", Napi::String::New(env, data->key_name));
                }
                event.Set("data", evtData);
            } else {
                event.Set("data", env.Null());
            }
            
            eventData.Set("event", event);
            delete data->event;
        }
        
        payload.Set("data", eventData);
        jsCallback.Call({payload});
        delete data;
    });
}

// 事件回调函数
CGEventRef eventCallback(CGEventTapProxy proxy, CGEventType type, CGEventRef event, void *refcon) {
    if (!g_is_recording) {
        return event;
    }
    
    auto current_time = std::chrono::steady_clock::now();
    int dt = 0;
    
    if (g_recorded_events.empty()) {
        g_record_start_time = current_time;
        g_last_event_time = current_time;
    } else {
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - g_last_event_time);
        dt = duration.count();
    }
    
    g_last_event_time = current_time;
    
    MacroEvent macro_event;
    macro_event.dt = dt;
    
    CGPoint location = CGEventGetLocation(event);
    macro_event.pos[0] = (int)location.x;
    macro_event.pos[1] = (int)location.y;
    g_last_pos = location;
    // 同步当前按钮与键状态到事件（便于回放）
    macro_event.buttons[0] = g_current_buttons[0] ? 1 : 0;
    macro_event.buttons[1] = g_current_buttons[1] ? 1 : 0;
    macro_event.buttons[2] = g_current_buttons[2] ? 1 : 0;
    for (const auto& kv : g_current_keys) {
        if (kv.second) macro_event.keys.push_back((int)kv.first);
    }
    
    switch (type) {
        case kCGEventMouseMoved:
        case kCGEventLeftMouseDragged:
        case kCGEventRightMouseDragged:
        case kCGEventOtherMouseDragged:
            macro_event.evt = "mouse-move";
            g_recorded_events.push_back(macro_event);
            triggerStatusCallback("recording", g_recording_macro_id, new MacroEvent(macro_event));
            break;
            
        case kCGEventLeftMouseDown:
            g_current_buttons[0] = true;
            macro_event.evt = "mouse-btn-changed";
            macro_event.buttons[0] = 1;
            g_recorded_events.push_back(macro_event);
            triggerStatusCallback("recording", g_recording_macro_id, new MacroEvent(macro_event));
            break;
            
        case kCGEventLeftMouseUp:
            g_current_buttons[0] = false;
            macro_event.evt = "mouse-btn-changed";
            macro_event.buttons[0] = 0;
            g_recorded_events.push_back(macro_event);
            triggerStatusCallback("recording", g_recording_macro_id, new MacroEvent(macro_event));
            break;
            
        case kCGEventRightMouseDown:
            g_current_buttons[1] = true;
            macro_event.evt = "mouse-btn-changed";
            macro_event.buttons[1] = 1;
            g_recorded_events.push_back(macro_event);
            triggerStatusCallback("recording", g_recording_macro_id, new MacroEvent(macro_event));
            break;
            
        case kCGEventRightMouseUp:
            g_current_buttons[1] = false;
            macro_event.evt = "mouse-btn-changed";
            macro_event.buttons[1] = 0;
            g_recorded_events.push_back(macro_event);
            triggerStatusCallback("recording", g_recording_macro_id, new MacroEvent(macro_event));
            break;
            
        case kCGEventOtherMouseDown: {
            int64_t button_number = CGEventGetIntegerValueField(event, kCGMouseEventButtonNumber);
            if (button_number == 2) {
                g_current_buttons[2] = true;
                macro_event.buttons[2] = 1;
            }
            macro_event.evt = "mouse-btn-changed";
            g_recorded_events.push_back(macro_event);
            triggerStatusCallback("recording", g_recording_macro_id, new MacroEvent(macro_event));
            break;
        }
            
        case kCGEventOtherMouseUp: {
            int64_t button_number = CGEventGetIntegerValueField(event, kCGMouseEventButtonNumber);
            if (button_number == 2) {
                g_current_buttons[2] = false;
                macro_event.buttons[2] = 0;
            }
            macro_event.evt = "mouse-btn-changed";
            g_recorded_events.push_back(macro_event);
            triggerStatusCallback("recording", g_recording_macro_id, new MacroEvent(macro_event));
            break;
        }
            
        case kCGEventScrollWheel: {
            // 捕获滚轮增量与单位（像素优先使用PointDelta字段）
            int64_t isPixel = CGEventGetIntegerValueField(event, kCGScrollWheelEventIsContinuous);
            int64_t dy = isPixel
                ? CGEventGetIntegerValueField(event, kCGScrollWheelEventPointDeltaAxis1)
                : CGEventGetIntegerValueField(event, kCGScrollWheelEventDeltaAxis1);
            int64_t dx = isPixel
                ? CGEventGetIntegerValueField(event, kCGScrollWheelEventPointDeltaAxis2)
                : CGEventGetIntegerValueField(event, kCGScrollWheelEventDeltaAxis2);
            macro_event.evt = "mouse-wheel";
            macro_event.pos[0] = (int)location.x;
            macro_event.pos[1] = (int)location.y;
            macro_event.wheel_dx = (int)dx;
            macro_event.wheel_dy = (int)dy;
            macro_event.wheel_unit = isPixel ? 1 : 0; // 1=pixel,0=line
            g_recorded_events.push_back(macro_event);
            triggerStatusCallback("recording", g_recording_macro_id, new MacroEvent(macro_event));
            break;
        }
            
        case kCGEventKeyDown: {
            CGKeyCode keycode = (CGKeyCode)CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);
            g_current_keys[keycode] = true;
            macro_event.evt = "key-changed";
            macro_event.pos[0] = (int)g_last_pos.x;
            macro_event.pos[1] = (int)g_last_pos.y;
            macro_event.keys.clear();
            
            // 构建 key_name
            std::string key_name;
            bool first = true;
            for (const auto& kv : g_current_keys) {
                if (kv.second) {
                    macro_event.keys.push_back((int)kv.first);
                    if (!first) key_name += " + ";
                    auto it = g_vk_to_name.find((int)kv.first);
                    if (it != g_vk_to_name.end()) {
                        key_name += it->second;
                    } else {
                        key_name += "VK_" + std::to_string((int)kv.first);
                    }
                    first = false;
                }
            }
            
            g_recorded_events.push_back(macro_event);
            triggerStatusCallback("recording", g_recording_macro_id, new MacroEvent(macro_event), key_name);
            break;
        }
            
        case kCGEventKeyUp: {
            CGKeyCode keycode = (CGKeyCode)CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);
            g_current_keys[keycode] = false;
            macro_event.evt = "key-changed";
            macro_event.pos[0] = (int)g_last_pos.x;
            macro_event.pos[1] = (int)g_last_pos.y;
            macro_event.keys.clear();
            
            // 构建 key_name
            std::string key_name;
            bool first = true;
            for (const auto& kv : g_current_keys) {
                if (kv.second) {
                    macro_event.keys.push_back((int)kv.first);
                    if (!first) key_name += " + ";
                    auto it = g_vk_to_name.find((int)kv.first);
                    if (it != g_vk_to_name.end()) {
                        key_name += it->second;
                    } else {
                        key_name += "VK_" + std::to_string((int)kv.first);
                    }
                    first = false;
                }
            }
            
            g_recorded_events.push_back(macro_event);
            triggerStatusCallback("recording", g_recording_macro_id, new MacroEvent(macro_event), key_name);
            break;
        }
            
        case kCGEventFlagsChanged: {
            // 处理修饰键变化
            CGEventFlags flags = CGEventGetFlags(event);
            
            // 获取键码以确定具体是哪个修饰键
            CGKeyCode keycode = (CGKeyCode)CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);
            
            // 检查每个修饰键的状态
            bool isPressed = false;
            
            switch (keycode) {
                case 0x37: // Command Left
                case 0x36: // Command Right
                    isPressed = (flags & kCGEventFlagMaskCommand) != 0;
                    break;
                case 0x38: // Shift Left
                case 0x3C: // Shift Right
                    isPressed = (flags & kCGEventFlagMaskShift) != 0;
                    break;
                case 0x3B: // Control Left
                case 0x3E: // Control Right
                    isPressed = (flags & kCGEventFlagMaskControl) != 0;
                    break;
                case 0x3A: // Option Left
                case 0x3D: // Option Right
                    isPressed = (flags & kCGEventFlagMaskAlternate) != 0;
                    break;
                case 0x39: // Caps Lock
                    isPressed = (flags & kCGEventFlagMaskAlphaShift) != 0;
                    break;
                case 0x3F: // Fn
                    isPressed = (flags & kCGEventFlagMaskSecondaryFn) != 0;
                    break;
                default:
                    break;
            }
            
            // 更新键状态
            bool stateChanged = (g_current_keys[keycode] != isPressed);
            g_current_keys[keycode] = isPressed;
            
            // 只有状态真正改变时才记录事件
            if (stateChanged) {
                macro_event.evt = "key-changed";
                macro_event.pos[0] = (int)g_last_pos.x;
                macro_event.pos[1] = (int)g_last_pos.y;
                macro_event.keys.clear();
                
                // 构建 key_name
                std::string key_name;
                bool first = true;
                for (const auto& kv : g_current_keys) {
                    if (kv.second) {
                        macro_event.keys.push_back((int)kv.first);
                        if (!first) key_name += " + ";
                        auto it = g_vk_to_name.find((int)kv.first);
                        if (it != g_vk_to_name.end()) {
                            key_name += it->second;
                        } else {
                            key_name += "VK_" + std::to_string((int)kv.first);
                        }
                        first = false;
                    }
                }
                
                g_recorded_events.push_back(macro_event);
                triggerStatusCallback("recording", g_recording_macro_id, new MacroEvent(macro_event), key_name);
            }
            break;
        }
            
        default:
            break;
    }
    
    const bool shouldBlockKeyEvent =
        g_block_input &&
        (type == kCGEventKeyDown || type == kCGEventKeyUp || type == kCGEventFlagsChanged);

    return shouldBlockKeyEvent ? NULL : event;
}

// 设置宏状态回调函数
Napi::Value SetMacroStatusCallback(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    if (info.Length() < 1 || !info[0].IsFunction()) {
        Napi::TypeError::New(env, "参数必须是一个回调函数").ThrowAsJavaScriptException();
        return env.Null();
    }
    
    Napi::Function callback = info[0].As<Napi::Function>();
    
    // 释放旧的回调
    if (g_status_callback) {
        g_status_callback.Release();
    }
    
    // 设置新的回调
    g_status_callback = Napi::ThreadSafeFunction::New(
        env,
        callback,
        "MacroStatusCallback",
        0,
        1
    );
    
    Napi::Object result = Napi::Object::New(env);
    result.Set("status", Napi::String::New(env, "callback set"));
    return result;
}

// 运行事件循环的线程函数
void runEventLoop() {
    @autoreleasepool {
        CFRunLoopRef runLoop = CFRunLoopGetCurrent();
        
        // 添加事件源到当前线程的RunLoop
        if (g_runloop_source) {
            CFRunLoopAddSource(runLoop, g_runloop_source, kCFRunLoopCommonModes);
        }
        
        // 运行RunLoop直到停止
        while (g_is_recording) {
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, false);
        }
        
        // 清理
        if (g_runloop_source) {
            CFRunLoopRemoveSource(runLoop, g_runloop_source, kCFRunLoopCommonModes);
        }
    }
}

// 开始录制
Napi::Value MacroRecordStart(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    DEBUG_LOG("MacroRecordStart called");
    
    if (g_is_recording) {
        DEBUG_LOG("Already recording");
        Napi::Object result = Napi::Object::New(env);
        result.Set("status", Napi::String::New(env, "already recording"));
        return result;
    }
    
    // 确保上一次的线程已经完全清理
    if (g_record_thread.joinable()) {
        g_record_thread.join();
    }
    
    DEBUG_LOG("Checking for options...");
    // 获取选项
    bool blockInput = false;
    if (info.Length() > 0 && info[0].IsObject()) {
        DEBUG_LOG("Options provided");
        Napi::Object options = info[0].As<Napi::Object>();
        
        // 获取 macroId
        if (options.Has("macroId") && options.Get("macroId").IsString()) {
            g_recording_macro_id = options.Get("macroId").As<Napi::String>().Utf8Value();
        } else {
            g_recording_macro_id = "0";
        }
        if (options.Has("blockInput") && options.Get("blockInput").IsBoolean()) {
            blockInput = options.Get("blockInput").As<Napi::Boolean>().Value();
        }
    } else {
        g_recording_macro_id = "0";
    }
    g_block_input = blockInput;
    
    // 清空之前的录制
    g_recorded_events.clear();
    g_current_keys.clear();
    g_current_buttons[0] = g_current_buttons[1] = g_current_buttons[2] = false;
    
    // 确保之前的资源已经清理
    if (g_event_tap) {
        CGEventTapEnable(g_event_tap, false);
        if (g_runloop_source) {
            CFRelease(g_runloop_source);
            g_runloop_source = nullptr;
        }
        CFRelease(g_event_tap);
        g_event_tap = nullptr;
    }
    
    // 创建事件tap
    CGEventMask eventMask = CGEventMaskBit(kCGEventMouseMoved) |
                           CGEventMaskBit(kCGEventLeftMouseDown) |
                           CGEventMaskBit(kCGEventLeftMouseUp) |
                           CGEventMaskBit(kCGEventRightMouseDown) |
                           CGEventMaskBit(kCGEventRightMouseUp) |
                           CGEventMaskBit(kCGEventOtherMouseDown) |
                           CGEventMaskBit(kCGEventOtherMouseUp) |
                           CGEventMaskBit(kCGEventLeftMouseDragged) |
                           CGEventMaskBit(kCGEventRightMouseDragged) |
                           CGEventMaskBit(kCGEventOtherMouseDragged) |
                           CGEventMaskBit(kCGEventScrollWheel) |
                           CGEventMaskBit(kCGEventKeyDown) |
                           CGEventMaskBit(kCGEventKeyUp) |
                           CGEventMaskBit(kCGEventFlagsChanged);  // 用于捕获修饰键
    
    g_event_tap = CGEventTapCreate(
        kCGSessionEventTap,
        kCGHeadInsertEventTap,
        g_block_input ? kCGEventTapOptionDefault : kCGEventTapOptionListenOnly,
        eventMask,
        eventCallback,
        NULL
    );
    
    if (!g_event_tap) {
        Napi::Error::New(env, "无法创建事件tap，请检查辅助功能权限").ThrowAsJavaScriptException();
        return env.Null();
    }
    
    g_runloop_source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, g_event_tap, 0);
    if (!g_runloop_source) {
        CFRelease(g_event_tap);
        g_event_tap = nullptr;
        Napi::Error::New(env, "无法创建RunLoop源").ThrowAsJavaScriptException();
        return env.Null();
    }
    
    CGEventTapEnable(g_event_tap, true);
    
    g_is_recording = true;
    
    g_record_thread = std::thread(runEventLoop);
    
    // 发送 started 状态
    triggerStatusCallback("started", g_recording_macro_id);
    
    Napi::Object result = Napi::Object::New(env);
    result.Set("status", Napi::String::New(env, "started"));
    return result;
}

// 停止录制
Napi::Value MacroRecordStop(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    if (!g_is_recording) {
        Napi::Object result = Napi::Object::New(env);
        result.Set("status", Napi::String::New(env, "not recording"));
        return result;
    }
    
    g_is_recording = false;
    g_block_input = false;
    
    // 等待录制线程结束
    if (g_record_thread.joinable()) {
        g_record_thread.join();
    }
    
    // 停止事件tap
    if (g_event_tap) {
        CGEventTapEnable(g_event_tap, false);
        
        if (g_runloop_source) {
            CFRelease(g_runloop_source);
            g_runloop_source = nullptr;
        }
        
        CFRelease(g_event_tap);
        g_event_tap = nullptr;
    }
    
    // 发送 record stopped 状态
    triggerStatusCallback("record stopped", g_recording_macro_id);
    
    // 计算录制时长（秒，小数）
    double duration_seconds = 0.0;
    if (!g_recorded_events.empty()) {
        auto stop_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(stop_time - g_record_start_time);
        duration_seconds = duration.count() / 1000000.0;
    }
    
    // 注意：不要在这里释放回调函数，因为它是全局的，下次录制还会用到
    // 只在 SetMacroStatusCallback 被重新调用时才释放旧的回调
    
    // 转换events为JavaScript数组
    Napi::Array events = Napi::Array::New(env, g_recorded_events.size());
    for (size_t i = 0; i < g_recorded_events.size(); i++) {
        const MacroEvent& evt = g_recorded_events[i];
        Napi::Object event_obj = Napi::Object::New(env);
        
        event_obj.Set("dt", Napi::Number::New(env, evt.dt));
        event_obj.Set("evt", Napi::String::New(env, evt.evt));
        
        Napi::Array pos = Napi::Array::New(env, 2);
        pos[uint32_t(0)] = Napi::Number::New(env, evt.pos[0]);
        pos[uint32_t(1)] = Napi::Number::New(env, evt.pos[1]);
        event_obj.Set("pos", pos);
        
        // 设置data字段（使用事件快照值）
        if (evt.evt == "mouse-btn-changed") {
            Napi::Object data = Napi::Object::New(env);
            Napi::Array buttons = Napi::Array::New(env, 3);
            buttons[uint32_t(0)] = Napi::Number::New(env, evt.buttons[0]);
            buttons[uint32_t(1)] = Napi::Number::New(env, evt.buttons[1]);
            buttons[uint32_t(2)] = Napi::Number::New(env, evt.buttons[2]);
            data.Set("button", buttons);
            event_obj.Set("data", data);
        } else if (evt.evt == "mouse-wheel") {
            Napi::Object data = Napi::Object::New(env);
            data.Set("dx", Napi::Number::New(env, evt.wheel_dx));
            data.Set("dy", Napi::Number::New(env, evt.wheel_dy));
            event_obj.Set("data", data);
        } else if (evt.evt == "key-changed") {
            Napi::Object data = Napi::Object::New(env);
            Napi::Array keys = Napi::Array::New(env, evt.keys.size());
            for (size_t k = 0; k < evt.keys.size(); ++k) {
                keys[uint32_t(k)] = Napi::Number::New(env, evt.keys[k]);
            }
            data.Set("key", keys);
            event_obj.Set("data", data);
        } else {
            event_obj.Set("data", env.Null());
        }
        
        events[uint32_t(i)] = event_obj;
    }
    
    Napi::Object result = Napi::Object::New(env);
    result.Set("status", Napi::String::New(env, "stopped"));
    result.Set("events", events);
    result.Set("duration", Napi::Number::New(env, duration_seconds));
    
    return result;
}

// 获取录制结果
Napi::Value MacroRecordGetResult(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    // 转换events为JavaScript数组
    Napi::Array events = Napi::Array::New(env, g_recorded_events.size());
    for (size_t i = 0; i < g_recorded_events.size(); i++) {
        const MacroEvent& evt = g_recorded_events[i];
        Napi::Object event_obj = Napi::Object::New(env);
        
        event_obj.Set("dt", Napi::Number::New(env, evt.dt));
        event_obj.Set("evt", Napi::String::New(env, evt.evt));
        
        Napi::Array pos = Napi::Array::New(env, 2);
        pos[uint32_t(0)] = Napi::Number::New(env, evt.pos[0]);
        pos[uint32_t(1)] = Napi::Number::New(env, evt.pos[1]);
        event_obj.Set("pos", pos);
        
        event_obj.Set("data", env.Null());
        
        events[uint32_t(i)] = event_obj;
    }
    
    Napi::Object result = Napi::Object::New(env);
    result.Set("events", events);
    
    return result;
}

// 回放线程函数
void playMacro(const std::vector<MacroEvent>& events, double speed_factor, int loop_count) {
    @autoreleasepool {
        int loops = 0;
        
        while (g_is_playing && (loop_count == 0 || loops < loop_count)) {
            loops++;
            
            std::set<CGKeyCode> pressed_keys;
            
            for (size_t i = 0; i < events.size() && g_is_playing; i++) {
                const MacroEvent& evt = events[i];
                
                // 等待
                if (evt.dt > 0) {
                    int wait_ms = (int)(evt.dt * speed_factor);
                    std::this_thread::sleep_for(std::chrono::milliseconds(wait_ms));
                }
                
                if (evt.evt == "mouse-move") {
                    CGPoint point = CGPointMake(evt.pos[0], evt.pos[1]);
                    CGEventRef move_event = CGEventCreateMouseEvent(NULL, kCGEventMouseMoved, point, kCGMouseButtonLeft);
                    CGEventPost(kCGHIDEventTap, move_event);
                    CFRelease(move_event);
                }
                else if (evt.evt == "mouse-btn-changed") {
                    CGPoint point = CGPointMake(evt.pos[0], evt.pos[1]);
                    // 根据按钮状态发送按下/抬起事件
                    static int last_buttons[3] = {0, 0, 0};
                    // Left
                    if (evt.buttons[0] != last_buttons[0]) {
                        CGEventType t = evt.buttons[0] ? kCGEventLeftMouseDown : kCGEventLeftMouseUp;
                        CGEventRef e = CGEventCreateMouseEvent(NULL, t, point, kCGMouseButtonLeft);
                        CGEventPost(kCGHIDEventTap, e);
                        CFRelease(e);
                        last_buttons[0] = evt.buttons[0];
                    }
                    // Right
                    if (evt.buttons[1] != last_buttons[1]) {
                        CGEventType t = evt.buttons[1] ? kCGEventRightMouseDown : kCGEventRightMouseUp;
                        CGEventRef e = CGEventCreateMouseEvent(NULL, t, point, kCGMouseButtonRight);
                        CGEventPost(kCGHIDEventTap, e);
                        CFRelease(e);
                        last_buttons[1] = evt.buttons[1];
                    }
                    // Middle
                    if (evt.buttons[2] != last_buttons[2]) {
                        CGEventType t = evt.buttons[2] ? kCGEventOtherMouseDown : kCGEventOtherMouseUp;
                        CGEventRef e = CGEventCreateMouseEvent(NULL, t, point, kCGMouseButtonCenter);
                        CGEventPost(kCGHIDEventTap, e);
                        CFRelease(e);
                        last_buttons[2] = evt.buttons[2];
                    }
                }
                else if (evt.evt == "mouse-wheel") {
                    // 为了匹配录制体感，放大像素单位滚动，保持行单位不变
                    int dy = evt.wheel_dy;
                    int dx = evt.wheel_dx;
                    if (evt.wheel_unit) { // pixel
                        dy *= 3;
                        dx *= 3;
                    }
                    CGEventRef scroll_event = CGEventCreateScrollWheelEvent(NULL,
                        evt.wheel_unit ? kCGScrollEventUnitPixel : kCGScrollEventUnitLine,
                        2,
                        dy,
                        dx);
                    CGEventPost(kCGHIDEventTap, scroll_event);
                    CFRelease(scroll_event);
                }
                else if (evt.evt == "key-changed") {
                    // 根据keys集合与pressed_keys集合差异发送按下/抬起
                    std::set<CGKeyCode> current_keys(evt.keys.begin(), evt.keys.end());
                    // key down
                    for (CGKeyCode k : current_keys) {
                        if (pressed_keys.find(k) == pressed_keys.end()) {
                            CGEventRef kd = CGEventCreateKeyboardEvent(NULL, k, true);
                            CGEventPost(kCGHIDEventTap, kd);
                            CFRelease(kd);
                            pressed_keys.insert(k);
                        }
                    }
                    // key up
                    std::set<CGKeyCode> to_release;
                    for (CGKeyCode k : pressed_keys) {
                        if (current_keys.find(k) == current_keys.end()) to_release.insert(k);
                    }
                    for (CGKeyCode k : to_release) {
                        CGEventRef ku = CGEventCreateKeyboardEvent(NULL, k, false);
                        CGEventPost(kCGHIDEventTap, ku);
                        CFRelease(ku);
                        pressed_keys.erase(k);
                    }
                }
            }
            
            // 释放所有按下的键
            for (CGKeyCode key : pressed_keys) {
                CGEventRef keyup_event = CGEventCreateKeyboardEvent(NULL, key, false);
                CGEventPost(kCGHIDEventTap, keyup_event);
                CFRelease(keyup_event);
            }
            
            // 检查是否是手动停止
            if (!g_is_playing) {
                break;
            }
        }
        
        g_is_playing = false;
        
        // 无论手动停止还是自动结束，都发送 play stopped 状态
        triggerStatusCallback("play stopped", g_playing_macro_id);
    }
}

// 开始回放
Napi::Value MacroPlayStart(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    if (info.Length() < 1 || !info[0].IsObject()) {
        Napi::TypeError::New(env, "参数必须是一个对象").ThrowAsJavaScriptException();
        return env.Null();
    }
    
    Napi::Object options = info[0].As<Napi::Object>();
    
    // 获取 macroId
    if (options.Has("macroId") && options.Get("macroId").IsString()) {
        g_playing_macro_id = options.Get("macroId").As<Napi::String>().Utf8Value();
    } else {
        g_playing_macro_id = "0";
    }
    
    // 获取events
    if (!options.Has("events") || !options.Get("events").IsArray()) {
        Napi::TypeError::New(env, "必须提供events数组").ThrowAsJavaScriptException();
        return env.Null();
    }
    
    Napi::Array js_events = options.Get("events").As<Napi::Array>();
    std::vector<MacroEvent> events;
    
    for (uint32_t i = 0; i < js_events.Length(); i++) {
        Napi::Value item = js_events[i];
        if (item.IsObject()) {
            Napi::Object event_obj = item.As<Napi::Object>();
            MacroEvent evt;
            
            evt.dt = event_obj.Get("dt").As<Napi::Number>().Int32Value();
            evt.evt = event_obj.Get("evt").As<Napi::String>().Utf8Value();
            
            if (event_obj.Has("pos") && event_obj.Get("pos").IsArray()) {
                Napi::Array pos = event_obj.Get("pos").As<Napi::Array>();
                Napi::Value pos_x = pos.Get(uint32_t(0));
                Napi::Value pos_y = pos.Get(uint32_t(1));
                if (pos_x.IsNumber()) {
                    evt.pos[0] = pos_x.As<Napi::Number>().Int32Value();
                }
                if (pos_y.IsNumber()) {
                    evt.pos[1] = pos_y.As<Napi::Number>().Int32Value();
                }
            }
            // 解析data
            if (event_obj.Has("data") && event_obj.Get("data").IsObject()) {
                Napi::Object data = event_obj.Get("data").As<Napi::Object>();
                if (evt.evt == "mouse-btn-changed" && data.Has("button")) {
                    Napi::Array buttons = data.Get("button").As<Napi::Array>();
                    evt.buttons[0] = buttons.Get(uint32_t(0)).As<Napi::Number>().Int32Value();
                    evt.buttons[1] = buttons.Get(uint32_t(1)).As<Napi::Number>().Int32Value();
                    evt.buttons[2] = buttons.Get(uint32_t(2)).As<Napi::Number>().Int32Value();
                } else if (evt.evt == "mouse-wheel") {
                    if (data.Has("dx")) evt.wheel_dx = data.Get("dx").As<Napi::Number>().Int32Value();
                    if (data.Has("dy")) evt.wheel_dy = data.Get("dy").As<Napi::Number>().Int32Value();
                    if (data.Has("unit") && data.Get("unit").IsString()) {
                        std::string u = data.Get("unit").As<Napi::String>().Utf8Value();
                        evt.wheel_unit = (u == "pixel") ? 1 : 0;
                    }
                } else if (evt.evt == "key-changed" && data.Has("key")) {
                    Napi::Array keys = data.Get("key").As<Napi::Array>();
                    for (uint32_t j = 0; j < keys.Length(); ++j) {
                        evt.keys.push_back(keys.Get(j).As<Napi::Number>().Int32Value());
                    }
                }
            }
            
            events.push_back(evt);
        }
    }
    
    // 获取speed，与 Python 保持一致
    // Python: speed = 1 / speed if speed > 0 else 1
    // 然后: adjusted_wait_time = (wait_time * self.speed_factor)
    // 所以 speed_factor = 1 / speed，wait_ms = dt * speed_factor
    double speed_factor = 1.0;
    if (options.Has("speed") && options.Get("speed").IsNumber()) {
        double speed = options.Get("speed").As<Napi::Number>().DoubleValue();
        if (speed > 0) {
            speed_factor = 1.0 / speed;  // speed=5 -> speed_factor=0.2，播放快5倍
        }
    }
    
    // 获取loop
    int loop = 1;
    if (options.Has("loop") && options.Get("loop").IsNumber()) {
        loop = options.Get("loop").As<Napi::Number>().Int32Value();
    }
    
    // 停止之前的播放
    if (g_is_playing) {
        g_is_playing = false;
        if (g_play_thread.joinable()) {
            g_play_thread.join();
        }
    }
    
    g_is_playing = true;
    
    // 发送 play started 状态
    triggerStatusCallback("play started", g_playing_macro_id);
    
    g_play_thread = std::thread(playMacro, events, speed_factor, loop);
    g_play_thread.detach();
    
    Napi::Object result = Napi::Object::New(env);
    result.Set("status", Napi::String::New(env, "play started"));
    return result;
}

// 停止回放
Napi::Value MacroPlayStop(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    if (!g_is_playing) {
        Napi::Object result = Napi::Object::New(env);
        result.Set("status", Napi::String::New(env, "not playing"));
        return result;
    }
    
    // 设置停止标志，playMacro 线程会检测并退出，退出时会发送 play stopped 回调
    g_is_playing = false;
    
    Napi::Object result = Napi::Object::New(env);
    result.Set("status", Napi::String::New(env, "stopping"));
    return result;
}

// 宏录制器通用接口（兼容 Python 版本和 Windows 版本）
Napi::Value MacroRecorder(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    if (info.Length() < 1 || !info[0].IsObject()) {
        Napi::TypeError::New(env, "参数必须是一个对象").ThrowAsJavaScriptException();
        return env.Null();
    }
    
    Napi::Object data = info[0].As<Napi::Object>();
    
    if (!data.Has("action")) {
        Napi::TypeError::New(env, "缺少 'action' 字段").ThrowAsJavaScriptException();
        return env.Null();
    }
    
    std::string action = data.Get("action").As<Napi::String>().Utf8Value();
    
    if (action == "record_start") {
        return MacroRecordStart(info);
    } else if (action == "record_stop") {
        return MacroRecordStop(info);
    } else if (action == "record_get_result") {
        return MacroRecordGetResult(info);
    } else if (action == "play_start") {
        return MacroPlayStart(info);
    } else if (action == "play_stop") {
        return MacroPlayStop(info);
    } else {
        Napi::Error::New(env, "未知的 action: " + action).ThrowAsJavaScriptException();
        return env.Null();
    }
}

} // namespace MacService


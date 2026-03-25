#include <napi.h>
#include "keyboard.h"
#include "media.h"
#include "icon.h"
#include "macro.h"

// 全局初始化标志
static bool g_initialized = false;

// 初始化函数
Napi::Value InitModule(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (g_initialized) {
        // 已经初始化过了
        return Napi::Boolean::New(env, true);
    }

    // Mac 版本不需要显式初始化 COM 等，直接标记为已初始化
    g_initialized = true;

    return Napi::Boolean::New(env, true);
}

// 关闭函数
Napi::Value Shutdown(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (g_initialized) {
        // Mac 版本不需要显式清理，直接标记为未初始化
        g_initialized = false;
    }

    return env.Undefined();
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    // 导出初始化和关闭函数
    exports.Set(
        Napi::String::New(env, "init"),
        Napi::Function::New(env, InitModule)
    );
    
    exports.Set(
        Napi::String::New(env, "shutdown"),
        Napi::Function::New(env, Shutdown)
    );
    
    // 导出 sendKey 函数
    exports.Set(
        Napi::String::New(env, "sendKey"),
        Napi::Function::New(env, MacService::SendKey)
    );
    
    // 导出 getMediaState 函数
    exports.Set(
        Napi::String::New(env, "getMediaState"),
        Napi::Function::New(env, MacService::GetMediaState)
    );
    
    // 导出 setMediaState 函数
    exports.Set(
        Napi::String::New(env, "setMediaState"),
        Napi::Function::New(env, MacService::SetMediaState)
    );
    
    // 导出 getIcon 函数
    exports.Set(
        Napi::String::New(env, "getIcon"),
        Napi::Function::New(env, MacService::GetIcon)
    );
    
    // 导出宏相关函数
    exports.Set(
        Napi::String::New(env, "setMacroStatusCallback"),
        Napi::Function::New(env, MacService::SetMacroStatusCallback)
    );
    
    // 保留旧的函数名以保持兼容性
    exports.Set(
        Napi::String::New(env, "setMacroRecordingCallback"),
        Napi::Function::New(env, MacService::SetMacroStatusCallback)
    );
    
    exports.Set(
        Napi::String::New(env, "macroRecordStart"),
        Napi::Function::New(env, MacService::MacroRecordStart)
    );
    
    exports.Set(
        Napi::String::New(env, "macroRecordStop"),
        Napi::Function::New(env, MacService::MacroRecordStop)
    );
    
    exports.Set(
        Napi::String::New(env, "macroRecordGetResult"),
        Napi::Function::New(env, MacService::MacroRecordGetResult)
    );
    
    exports.Set(
        Napi::String::New(env, "macroPlayStart"),
        Napi::Function::New(env, MacService::MacroPlayStart)
    );
    
    exports.Set(
        Napi::String::New(env, "macroPlayStop"),
        Napi::Function::New(env, MacService::MacroPlayStop)
    );
    
    // 导出 Windows 风格的别名函数
    exports.Set(
        Napi::String::New(env, "startMacroRecording"),
        Napi::Function::New(env, MacService::MacroRecordStart)
    );
    
    exports.Set(
        Napi::String::New(env, "stopMacroRecording"),
        Napi::Function::New(env, MacService::MacroRecordStop)
    );
    
    exports.Set(
        Napi::String::New(env, "startMacroPlayback"),
        Napi::Function::New(env, MacService::MacroPlayStart)
    );
    
    exports.Set(
        Napi::String::New(env, "stopMacroPlayback"),
        Napi::Function::New(env, MacService::MacroPlayStop)
    );
    
    // 导出通用 macroRecorder 接口（兼容 Python/Windows 版本）
    exports.Set(
        Napi::String::New(env, "macroRecorder"),
        Napi::Function::New(env, MacService::MacroRecorder)
    );
    
    return exports;
}

NODE_API_MODULE(mac_system_module, Init)



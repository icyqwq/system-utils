#ifndef MACRO_H
#define MACRO_H

#include <napi.h>

namespace MacService {

// 设置宏状态回调函数
Napi::Value SetMacroStatusCallback(const Napi::CallbackInfo& info);

// 开始宏录制
Napi::Value MacroRecordStart(const Napi::CallbackInfo& info);

// 停止宏录制
Napi::Value MacroRecordStop(const Napi::CallbackInfo& info);

// 获取录制结果
Napi::Value MacroRecordGetResult(const Napi::CallbackInfo& info);

// 开始宏回放
Napi::Value MacroPlayStart(const Napi::CallbackInfo& info);

// 停止宏回放
Napi::Value MacroPlayStop(const Napi::CallbackInfo& info);

// 宏录制器通用接口（兼容 Python 版本）
Napi::Value MacroRecorder(const Napi::CallbackInfo& info);

} // namespace MacService

#endif // MACRO_H


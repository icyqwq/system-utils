#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <napi.h>
#include <string>

namespace MacService {

// 发送键盘输入
Napi::Value SendKey(const Napi::CallbackInfo& info);

} // namespace MacService

#endif // KEYBOARD_H



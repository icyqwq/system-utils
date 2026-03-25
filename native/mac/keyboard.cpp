#include "keyboard.h"
#include <CoreGraphics/CoreGraphics.h>
#include <Carbon/Carbon.h>
#include <ApplicationServices/ApplicationServices.h>
#include <vector>
#include <unistd.h>

namespace MacService {

// 辅助函数：发送单个按键事件
void SendKeyPress(CGKeyCode keyCode, bool keyDown) {
    CGEventRef event = CGEventCreateKeyboardEvent(NULL, keyCode, keyDown);
    if (event) {
        CGEventPost(kCGHIDEventTap, event);
        CFRelease(event);
    }
}

// 辅助函数：发送修饰键组合
void SendModifierKey(CGKeyCode keyCode, CGEventFlags modifier, bool withDelay = true) {
    CGEventRef keyDownEvent = CGEventCreateKeyboardEvent(NULL, keyCode, true);
    CGEventSetFlags(keyDownEvent, modifier);
    CGEventPost(kCGHIDEventTap, keyDownEvent);
    CFRelease(keyDownEvent);
    
    if (withDelay) {
        usleep(100000); // 100ms延迟
    }
    
    CGEventRef keyUpEvent = CGEventCreateKeyboardEvent(NULL, keyCode, false);
    CGEventPost(kCGHIDEventTap, keyUpEvent);
    CFRelease(keyUpEvent);
}

// 辅助函数：发送文本字符串
void SendTextString(const std::string& text) {
    // 将std::string转换为UTF-16
    std::vector<UniChar> unicodeString;
    for (size_t i = 0; i < text.length(); ) {
        unsigned char c = text[i];
        UniChar unichar = 0;
        
        if ((c & 0x80) == 0) {
            // 单字节字符 (ASCII)
            unichar = c;
            i += 1;
        } else if ((c & 0xE0) == 0xC0) {
            // 双字节字符
            if (i + 1 < text.length()) {
                unichar = ((c & 0x1F) << 6) | (text[i + 1] & 0x3F);
                i += 2;
            } else {
                i += 1;
            }
        } else if ((c & 0xF0) == 0xE0) {
            // 三字节字符
            if (i + 2 < text.length()) {
                unichar = ((c & 0x0F) << 12) | 
                         ((text[i + 1] & 0x3F) << 6) | 
                         (text[i + 2] & 0x3F);
                i += 3;
            } else {
                i += 1;
            }
        } else if ((c & 0xF8) == 0xF0) {
            // 四字节字符 (需要代理对)
            if (i + 3 < text.length()) {
                uint32_t codepoint = ((c & 0x07) << 18) |
                                    ((text[i + 1] & 0x3F) << 12) |
                                    ((text[i + 2] & 0x3F) << 6) |
                                    (text[i + 3] & 0x3F);
                
                // 转换为UTF-16代理对
                if (codepoint > 0xFFFF) {
                    codepoint -= 0x10000;
                    unicodeString.push_back(0xD800 + (codepoint >> 10));
                    unicodeString.push_back(0xDC00 + (codepoint & 0x3FF));
                } else {
                    unicodeString.push_back(unichar);
                }
                i += 4;
                continue;
            } else {
                i += 1;
            }
        } else {
            i += 1;
            continue;
        }
        
        unicodeString.push_back(unichar);
    }
    
    // 创建键盘事件并设置Unicode字符串
    if (!unicodeString.empty()) {
        CGEventRef event = CGEventCreateKeyboardEvent(NULL, 0, true);
        CGEventKeyboardSetUnicodeString(event, unicodeString.size(), unicodeString.data());
        CGEventPost(kCGHIDEventTap, event);
        CFRelease(event);
    }
}

// 主函数：SendKey
Napi::Value SendKey(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    // 检查参数
    if (info.Length() < 1 || !info[0].IsObject()) {
        Napi::TypeError::New(env, "参数必须是一个对象").ThrowAsJavaScriptException();
        return env.Null();
    }
    
    Napi::Object options = info[0].As<Napi::Object>();
    
    // 处理文本输入
    if (options.Has("input")) {
        Napi::Value inputValue = options.Get("input");
        if (inputValue.IsString()) {
            std::string input = inputValue.As<Napi::String>().Utf8Value();
            
            // 处理转义字符
            std::string processed;
            for (size_t i = 0; i < input.length(); i++) {
                if (input[i] == '\\' && i + 1 < input.length()) {
                    if (input[i + 1] == 'n') {
                        processed += '\n';
                        i++;
                    } else if (input[i + 1] == 'r') {
                        processed += '\r';
                        i++;
                    } else if (input[i + 1] == 't') {
                        processed += '\t';
                        i++;
                    } else if (input[i + 1] == '\\') {
                        processed += '\\';
                        i++;
                    } else {
                        processed += input[i];
                    }
                } else {
                    processed += input[i];
                }
            }
            
            SendTextString(processed);
            return Napi::Boolean::New(env, true);
        }
    }
    
    // 处理快捷键
    if (options.Has("hotkey")) {
        Napi::Value hotkeyValue = options.Get("hotkey");
        if (hotkeyValue.IsString()) {
            std::string hotkey = hotkeyValue.As<Napi::String>().Utf8Value();
            
            if (hotkey == "paste") {
                // 模拟 Cmd+V
                CGEventRef cmdDown = CGEventCreateKeyboardEvent(NULL, kVK_Command, true);
                CGEventPost(kCGHIDEventTap, cmdDown);
                CFRelease(cmdDown);
                
                usleep(10000); // 10ms
                
                CGEventRef vDown = CGEventCreateKeyboardEvent(NULL, kVK_ANSI_V, true);
                CGEventSetFlags(vDown, kCGEventFlagMaskCommand);
                CGEventPost(kCGHIDEventTap, vDown);
                CFRelease(vDown);
                
                usleep(100000); // 100ms
                
                CGEventRef vUp = CGEventCreateKeyboardEvent(NULL, kVK_ANSI_V, false);
                CGEventPost(kCGHIDEventTap, vUp);
                CFRelease(vUp);
                
                CGEventRef cmdUp = CGEventCreateKeyboardEvent(NULL, kVK_Command, false);
                CGEventPost(kCGHIDEventTap, cmdUp);
                CFRelease(cmdUp);
                
                return Napi::Boolean::New(env, true);
            } else if (hotkey == "copy") {
                // 模拟 Cmd+C
                CGEventRef cmdDown = CGEventCreateKeyboardEvent(NULL, kVK_Command, true);
                CGEventPost(kCGHIDEventTap, cmdDown);
                CFRelease(cmdDown);
                
                usleep(10000);
                
                CGEventRef cDown = CGEventCreateKeyboardEvent(NULL, kVK_ANSI_C, true);
                CGEventSetFlags(cDown, kCGEventFlagMaskCommand);
                CGEventPost(kCGHIDEventTap, cDown);
                CFRelease(cDown);
                
                usleep(100000);
                
                CGEventRef cUp = CGEventCreateKeyboardEvent(NULL, kVK_ANSI_C, false);
                CGEventPost(kCGHIDEventTap, cUp);
                CFRelease(cUp);
                
                CGEventRef cmdUp = CGEventCreateKeyboardEvent(NULL, kVK_Command, false);
                CGEventPost(kCGHIDEventTap, cmdUp);
                CFRelease(cmdUp);
                
                return Napi::Boolean::New(env, true);
            } else if (hotkey == "cut") {
                // 模拟 Cmd+X
                CGEventRef cmdDown = CGEventCreateKeyboardEvent(NULL, kVK_Command, true);
                CGEventPost(kCGHIDEventTap, cmdDown);
                CFRelease(cmdDown);
                
                usleep(10000);
                
                CGEventRef xDown = CGEventCreateKeyboardEvent(NULL, kVK_ANSI_X, true);
                CGEventSetFlags(xDown, kCGEventFlagMaskCommand);
                CGEventPost(kCGHIDEventTap, xDown);
                CFRelease(xDown);
                
                usleep(100000);
                
                CGEventRef xUp = CGEventCreateKeyboardEvent(NULL, kVK_ANSI_X, false);
                CGEventPost(kCGHIDEventTap, xUp);
                CFRelease(xUp);
                
                CGEventRef cmdUp = CGEventCreateKeyboardEvent(NULL, kVK_Command, false);
                CGEventPost(kCGHIDEventTap, cmdUp);
                CFRelease(cmdUp);
                
                return Napi::Boolean::New(env, true);
            }
        }
    }
    
    return Napi::Boolean::New(env, false);
}

} // namespace MacService



#include "SystemModuleManager.h"
#include <iostream>
#include <algorithm>
#include <cwctype>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <audiosessiontypes.h>
#include <audiopolicy.h>
#include <functiondiscoverykeys.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <sstream>
#include <iomanip>
#include <vector>
#include <gdiplus.h>
#include <objbase.h>
#include <fstream>
#include <map>
#include <chrono>
#include <mmsystem.h>
#include <random>

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Gdiplus.lib")
#pragma comment(lib, "Winmm.lib")

// Base64编码辅助函数
std::string base64_encode(const std::vector<unsigned char>& data) {
    static const char* base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    std::string result;
    size_t i = 0;

    while (i < data.size()) {
        // 取3个字节
        unsigned char byte1 = (i < data.size()) ? data[i++] : 0;
        unsigned char byte2 = (i < data.size()) ? data[i++] : 0;
        unsigned char byte3 = (i < data.size()) ? data[i++] : 0;

        // 转换为4个Base64字符
        unsigned char idx1 = (byte1 >> 2) & 0x3F;
        unsigned char idx2 = ((byte1 & 0x03) << 4) | ((byte2 >> 4) & 0x0F);
        unsigned char idx3 = ((byte2 & 0x0F) << 2) | ((byte3 >> 6) & 0x03);
        unsigned char idx4 = byte3 & 0x3F;

        result += base64_chars[idx1];
        result += base64_chars[idx2];
        result += (i > data.size() + 1) ? '=' : base64_chars[idx3];
        result += (i > data.size() + 2) ? '=' : base64_chars[idx4];
    }

    return result;
}

// 键盘管理器实现
KeyboardManager::KeyboardManager() {
    // 构造函数
}

KeyboardManager::~KeyboardManager() {
    // 析构函数
}

bool KeyboardManager::SendKey(const std::string& key_sequence, bool is_hotkey) {
    try {
        if (is_hotkey) {
            // 处理快捷键（如 %hotkey%ctrl+c）
            std::string keys = key_sequence.substr(8); // 去掉"%hotkey%"前缀
            std::vector<WORD> vk_codes;

            // 解析组合键
            size_t pos = 0;
            std::string token;
            while ((pos = keys.find('+')) != std::string::npos) {
                token = keys.substr(0, pos);
                vk_codes.push_back(StringToVkCode(token));
                keys.erase(0, pos + 1);
            }
            if (!keys.empty()) {
                vk_codes.push_back(StringToVkCode(keys));
            }

            return SendKeyCombination(vk_codes, true);
        } else {
            // 处理普通文本输入
            std::vector<INPUT> inputs = CreateKeyInputs(key_sequence, true);

            if (inputs.empty()) {
                return false;
            }

            // 使用随机延迟逐个发送输入，避免输入过快导致错误
            static std::random_device rd;
            static std::mt19937 gen(rd());
            std::uniform_int_distribution<> delay_dist(1, 5); // 1-5ms 随机延迟

            UINT total_sent = 0;
            for (size_t i = 0; i < inputs.size(); ++i) {
                UINT sent = SendInput(1, &inputs[i], sizeof(INPUT));
                total_sent += sent;
                
                // 在每个输入之间添加随机延迟（最后一个输入后不需要延迟）
                if (i < inputs.size() - 1) {
                    Sleep(delay_dist(gen));
                }
            }
            return total_sent == inputs.size();
        }
    } catch (const std::exception& e) {
        std::cerr << "Error sending key: " << e.what() << std::endl;
        return false;
    }
}

bool KeyboardManager::SendKeyCombination(const std::vector<WORD>& keys, bool press) {
    try {
        std::vector<INPUT> inputs;

        if (press) {
            // 先按下所有键
            for (WORD vk_code : keys) {
                INPUT input = { 0 };
                input.type = INPUT_KEYBOARD;
                input.ki.wVk = vk_code;
                input.ki.dwFlags = 0; // 按下键
                inputs.push_back(input);
            }

            // 再释放所有键（逆序）
            for (auto it = keys.rbegin(); it != keys.rend(); ++it) {
                INPUT input = { 0 };
                input.type = INPUT_KEYBOARD;
                input.ki.wVk = *it;
                input.ki.dwFlags = KEYEVENTF_KEYUP; // 释放键
                inputs.push_back(input);
            }
        } else {
            // 只释放键（逆序）
            for (auto it = keys.rbegin(); it != keys.rend(); ++it) {
                INPUT input = { 0 };
                input.type = INPUT_KEYBOARD;
                input.ki.wVk = *it;
                input.ki.dwFlags = KEYEVENTF_KEYUP; // 释放键
                inputs.push_back(input);
            }
        }

        UINT sent = SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
        return sent == inputs.size();
    } catch (const std::exception& e) {
        std::cerr << "Error sending key combination: " << e.what() << std::endl;
        return false;
    }
}

bool KeyboardManager::IsKeyPressed(WORD vk_code) {
    return (GetKeyState(vk_code) & 0x8000) != 0;
}

void KeyboardManager::SendKeyDown(WORD vk_code) {
    INPUT input = { 0 };
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk_code;
    input.ki.dwFlags = 0;
    SendInput(1, &input, sizeof(INPUT));
}

void KeyboardManager::SendKeyUp(WORD vk_code) {
    INPUT input = { 0 };
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk_code;
    input.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(INPUT));
}

void KeyboardManager::SendKeyPress(WORD vk_code) {
    SendKeyDown(vk_code);
    Sleep(10); // 短暂延迟模拟按键持续时间
    SendKeyUp(vk_code);
}

WORD KeyboardManager::StringToVkCode(const std::string& key_str) {
    std::string lower_key = key_str;
    std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(), ::tolower);

    // 特殊键映射
    if (lower_key == "enter" || lower_key == "return") return VK_RETURN;
    if (lower_key == "tab") return VK_TAB;
    if (lower_key == "space" || lower_key == " ") return VK_SPACE;
    if (lower_key == "backspace") return VK_BACK;
    if (lower_key == "delete" || lower_key == "del") return VK_DELETE;
    if (lower_key == "escape" || lower_key == "esc") return VK_ESCAPE;
    if (lower_key == "shift") return VK_SHIFT;
    if (lower_key == "control" || lower_key == "ctrl") return VK_CONTROL;
    if (lower_key == "alt") return VK_MENU;
    if (lower_key == "capslock" || lower_key == "caps") return VK_CAPITAL;
    if (lower_key == "numlock") return VK_NUMLOCK;
    if (lower_key == "scrolllock") return VK_SCROLL;

    // 方向键
    if (lower_key == "up") return VK_UP;
    if (lower_key == "down") return VK_DOWN;
    if (lower_key == "left") return VK_LEFT;
    if (lower_key == "right") return VK_RIGHT;

    // 功能键
    if (lower_key == "f1") return VK_F1;
    if (lower_key == "f2") return VK_F2;
    if (lower_key == "f3") return VK_F3;
    if (lower_key == "f4") return VK_F4;
    if (lower_key == "f5") return VK_F5;
    if (lower_key == "f6") return VK_F6;
    if (lower_key == "f7") return VK_F7;
    if (lower_key == "f8") return VK_F8;
    if (lower_key == "f9") return VK_F9;
    if (lower_key == "f10") return VK_F10;
    if (lower_key == "f11") return VK_F11;
    if (lower_key == "f12") return VK_F12;

    // 小键盘
    if (lower_key == "numpad0" || lower_key == "num0") return VK_NUMPAD0;
    if (lower_key == "numpad1" || lower_key == "num1") return VK_NUMPAD1;
    if (lower_key == "numpad2" || lower_key == "num2") return VK_NUMPAD2;
    if (lower_key == "numpad3" || lower_key == "num3") return VK_NUMPAD3;
    if (lower_key == "numpad4" || lower_key == "num4") return VK_NUMPAD4;
    if (lower_key == "numpad5" || lower_key == "num5") return VK_NUMPAD5;
    if (lower_key == "numpad6" || lower_key == "num6") return VK_NUMPAD6;
    if (lower_key == "numpad7" || lower_key == "num7") return VK_NUMPAD7;
    if (lower_key == "numpad8" || lower_key == "num8") return VK_NUMPAD8;
    if (lower_key == "numpad9" || lower_key == "num9") return VK_NUMPAD9;

    // Windows键
    if (lower_key == "lwin" || lower_key == "leftwin") return VK_LWIN;
    if (lower_key == "rwin" || lower_key == "rightwin") return VK_RWIN;

    // 媒体键（如果支持）
    if (lower_key == "mediaplaypause" || lower_key == "playpause") return VK_MEDIA_PLAY_PAUSE;
    if (lower_key == "mediastop") return VK_MEDIA_STOP;
    if (lower_key == "medianext") return VK_MEDIA_NEXT_TRACK;
    if (lower_key == "mediaprev" || lower_key == "mediaprevious") return VK_MEDIA_PREV_TRACK;
    if (lower_key == "volumeup" || lower_key == "volup") return VK_VOLUME_UP;
    if (lower_key == "volumedown" || lower_key == "voldown") return VK_VOLUME_DOWN;
    if (lower_key == "volumemute") return VK_VOLUME_MUTE;

    // 普通字母和数字键
    if (lower_key.length() == 1) {
        char ch = lower_key[0];
        if (ch >= 'a' && ch <= 'z') {
            return static_cast<WORD>(ch - 'a' + 'A'); // 转换为大写虚拟键码
        }
        if (ch >= '0' && ch <= '9') {
            return static_cast<WORD>(ch); // 数字键
        }
        if (ch >= '!' && ch <= '/') {
            // 符号键映射（简化版）
            static const std::string symbols = "!@#$%^&*()";
            size_t pos = symbols.find(ch);
            if (pos != std::string::npos) {
                return VK_OEM_1 + pos; // 使用OEM键范围
            }
        }
    }

    // 如果无法识别，返回0
    return 0;
}

std::string KeyboardManager::VkCodeToString(WORD vk_code) {
    // 这里实现虚拟键码转字符串的逆向映射
    // 为了简化，先返回十六进制表示
    char buf[16];
    snprintf(buf, sizeof(buf), "VK_%04X", vk_code);
    return std::string(buf);
}

bool KeyboardManager::IsSpecialKey(const std::string& key) {
    std::string lower_key = key;
    std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(), ::tolower);

    return (lower_key == "shift" || lower_key == "control" || lower_key == "ctrl" ||
            lower_key == "alt" || lower_key == "enter" || lower_key == "tab" ||
            lower_key == "escape" || lower_key == "esc" ||
            lower_key.find('f') == 0 || lower_key.find("num") == 0 ||
            lower_key.find("media") == 0 || lower_key.find("volume") == 0);
}

std::vector<INPUT> KeyboardManager::CreateKeyInputs(const std::string& key_sequence, bool press) {
    std::vector<INPUT> inputs;

    // 处理转义字符
    std::string processed_sequence = key_sequence;
    size_t pos = 0;
    while ((pos = processed_sequence.find("\\n", pos)) != std::string::npos) {
        processed_sequence.replace(pos, 2, "\n");
        pos += 1;
    }
    pos = 0;
    while ((pos = processed_sequence.find("\\r", pos)) != std::string::npos) {
        processed_sequence.replace(pos, 2, "\r");
        pos += 1;
    }

    // 将UTF-8字符串转换为宽字符（Unicode）
    int wide_char_count = MultiByteToWideChar(CP_UTF8, 0, processed_sequence.c_str(), -1, nullptr, 0);
    if (wide_char_count <= 0) {
        // 如果转换失败，回退到原有逻辑
        return CreateKeyInputsLegacy(key_sequence, press);
    }

    std::vector<wchar_t> wide_chars(wide_char_count);
    MultiByteToWideChar(CP_UTF8, 0, processed_sequence.c_str(), -1, wide_chars.data(), wide_char_count);

    // 处理每个Unicode字符
    for (size_t i = 0; i < wide_chars.size() - 1; ++i) { // -1 因为末尾是null终止符
        wchar_t ch = wide_chars[i];

        if (ch == L'\n') {
            // Enter键
            INPUT input = { 0 };
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = VK_RETURN;
            if (!press) input.ki.dwFlags = KEYEVENTF_KEYUP;
            inputs.push_back(input);
        } else if (ch == L'\r') {
            // 回车键（与\n相同处理）
            INPUT input = { 0 };
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = VK_RETURN;
            if (!press) input.ki.dwFlags = KEYEVENTF_KEYUP;
            inputs.push_back(input);
        } else if (ch == L'\t') {
            // Tab键
            INPUT input = { 0 };
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = VK_TAB;
            if (!press) input.ki.dwFlags = KEYEVENTF_KEYUP;
            inputs.push_back(input);
        } else if (ch >= 32 && ch <= 126) {
            // ASCII 可打印字符，使用虚拟键码
            INPUT input = { 0 };
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = 0;
            input.ki.wScan = ch;
            input.ki.dwFlags = KEYEVENTF_UNICODE;
            if (!press) input.ki.dwFlags |= KEYEVENTF_KEYUP;
            inputs.push_back(input);
        } else {
            // Unicode字符（包括中文、emoji等）
            INPUT input = { 0 };
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = 0;
            input.ki.wScan = ch;
            input.ki.dwFlags = KEYEVENTF_UNICODE;
            if (!press) input.ki.dwFlags |= KEYEVENTF_KEYUP;
            inputs.push_back(input);
        }
    }

    return inputs;
}

// 回退方法：处理转换失败的情况
std::vector<INPUT> KeyboardManager::CreateKeyInputsLegacy(const std::string& key_sequence, bool press) {
    std::vector<INPUT> inputs;

    // 处理转义字符
    std::string processed_sequence = key_sequence;
    size_t pos = 0;
    while ((pos = processed_sequence.find("\\n", pos)) != std::string::npos) {
        processed_sequence.replace(pos, 2, "\n");
        pos += 1;
    }
    pos = 0;
    while ((pos = processed_sequence.find("\\r", pos)) != std::string::npos) {
        processed_sequence.replace(pos, 2, "\r");
        pos += 1;
    }

    // 对于普通字符，直接转换为键盘输入（原有逻辑）
    for (char ch : processed_sequence) {
        if (ch == '\n') {
            // Enter键
            INPUT input = { 0 };
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = VK_RETURN;
            if (!press) input.ki.dwFlags = KEYEVENTF_KEYUP;
            inputs.push_back(input);
        } else if (ch == '\r') {
            // 回车键（与\n相同处理）
            INPUT input = { 0 };
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = VK_RETURN;
            if (!press) input.ki.dwFlags = KEYEVENTF_KEYUP;
            inputs.push_back(input);
        } else if (ch == '\t') {
            // Tab键
            INPUT input = { 0 };
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = VK_TAB;
            if (!press) input.ki.dwFlags = KEYEVENTF_KEYUP;
            inputs.push_back(input);
        } else {
            // 普通字符，使用键盘事件
            INPUT input = { 0 };
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = 0;
            input.ki.wScan = ch;
            input.ki.dwFlags = KEYEVENTF_UNICODE;
            if (!press) input.ki.dwFlags |= KEYEVENTF_KEYUP;
            inputs.push_back(input);
        }
    }

    return inputs;
}

// 音频管理器实现
AudioManager::AudioManager() :
    pSpeakerDevice(nullptr),
    pSpeakerEndpointVolume(nullptr),
    pMicrophoneDevice(nullptr),
    pMicrophoneEndpointVolume(nullptr),
    pSessionManager(nullptr),
    pSessionEnumerator(nullptr) {
    // 初始化COM
    CoInitialize(nullptr);
}

AudioManager::~AudioManager() {
    Cleanup();
    CoUninitialize();
}

bool AudioManager::InitializeSpeaker() {
    if (pSpeakerEndpointVolume) {
        // 检查默认设备是否切换
        RefreshDefaultDevicesIfChanged();
        if (pSpeakerEndpointVolume) return true;
    }

    try {
        // 获取音频设备枚举器
        IMMDeviceEnumerator* pEnumerator = nullptr;
        HRESULT hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator),
            (void**)&pEnumerator
        );

        if (FAILED(hr) || !pEnumerator) return false;

        // 获取默认音频渲染设备（扬声器）
        IMMDevice* pDevice = nullptr;
        hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);

        if (FAILED(hr) || !pDevice) {
            pEnumerator->Release();
            return false;
        }

        // 激活音频终点音量接口
        IAudioEndpointVolume* pEndpointVolume = nullptr;
        hr = pDevice->Activate(
            __uuidof(IAudioEndpointVolume),
            CLSCTX_ALL,
            nullptr,
            (void**)&pEndpointVolume
        );

        pEnumerator->Release();

        if (FAILED(hr) || !pEndpointVolume) {
            pDevice->Release();
            return false;
        }

        pSpeakerDevice = pDevice;
        pSpeakerEndpointVolume = pEndpointVolume;
        // 记录当前默认设备ID
        currentSpeakerDeviceId = GetDeviceId(pSpeakerDevice);
        return true;
    } catch (...) {
        return false;
    }
}

bool AudioManager::InitializeMicrophone() {
    if (pMicrophoneEndpointVolume) {
        RefreshDefaultDevicesIfChanged();
        if (pMicrophoneEndpointVolume) return true;
    }

    try {
        // 获取音频设备枚举器
        IMMDeviceEnumerator* pEnumerator = nullptr;
        HRESULT hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator),
            (void**)&pEnumerator
        );

        if (FAILED(hr) || !pEnumerator) return false;

        // 获取默认音频捕获设备（麦克风）
        IMMDevice* pDevice = nullptr;
        hr = pEnumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &pDevice);

        if (FAILED(hr) || !pDevice) {
            pEnumerator->Release();
            return false;
        }

        // 激活音频终点音量接口
        IAudioEndpointVolume* pEndpointVolume = nullptr;
        hr = pDevice->Activate(
            __uuidof(IAudioEndpointVolume),
            CLSCTX_ALL,
            nullptr,
            (void**)&pEndpointVolume
        );

        pEnumerator->Release();

        if (FAILED(hr) || !pEndpointVolume) {
            pDevice->Release();
            return false;
        }

        pMicrophoneDevice = pDevice;
        pMicrophoneEndpointVolume = pEndpointVolume;
        currentMicrophoneDeviceId = GetDeviceId(pMicrophoneDevice);
        return true;
    } catch (...) {
        return false;
    }
}

void AudioManager::Cleanup() {
    if (pSpeakerEndpointVolume) {
        pSpeakerEndpointVolume->Release();
        pSpeakerEndpointVolume = nullptr;
    }
    if (pSpeakerDevice) {
        pSpeakerDevice->Release();
        pSpeakerDevice = nullptr;
    }
    if (pMicrophoneEndpointVolume) {
        pMicrophoneEndpointVolume->Release();
        pMicrophoneEndpointVolume = nullptr;
    }
    if (pMicrophoneDevice) {
        pMicrophoneDevice->Release();
        pMicrophoneDevice = nullptr;
    }
    if (pSessionEnumerator) {
        pSessionEnumerator->Release();
        pSessionEnumerator = nullptr;
    }
    if (pSessionManager) {
        pSessionManager->Release();
        pSessionManager = nullptr;
    }
}

bool AudioManager::SetSpeakerVolume(float volume) {
    RefreshDefaultDevicesIfChanged();
    if (!InitializeSpeaker()) return false;

    // 音量范围检查
    if (volume < 0.0f || volume > 1.0f) return false;

    HRESULT hr = pSpeakerEndpointVolume->SetMasterVolumeLevelScalar(volume, nullptr);
    if (FAILED(hr)) {
        // 设备可能已切换或句柄失效，尝试重建一次
        if (pSpeakerEndpointVolume) { pSpeakerEndpointVolume->Release(); pSpeakerEndpointVolume = nullptr; }
        if (pSpeakerDevice) { pSpeakerDevice->Release(); pSpeakerDevice = nullptr; }
        if (!InitializeSpeaker()) return false;
        hr = pSpeakerEndpointVolume->SetMasterVolumeLevelScalar(volume, nullptr);
    }
    return SUCCEEDED(hr);
}

bool AudioManager::SetSpeakerMute(bool mute) {
    RefreshDefaultDevicesIfChanged();
    if (!InitializeSpeaker()) return false;

    HRESULT hr = pSpeakerEndpointVolume->SetMute(mute, nullptr);
    if (FAILED(hr)) {
        if (pSpeakerEndpointVolume) { pSpeakerEndpointVolume->Release(); pSpeakerEndpointVolume = nullptr; }
        if (pSpeakerDevice) { pSpeakerDevice->Release(); pSpeakerDevice = nullptr; }
        if (!InitializeSpeaker()) return false;
        hr = pSpeakerEndpointVolume->SetMute(mute, nullptr);
    }
    return SUCCEEDED(hr);
}

bool AudioManager::GetSpeakerVolume(float& volume) {
    RefreshDefaultDevicesIfChanged();
    if (!InitializeSpeaker()) return false;

    float currentVolume = 0.0f;
    HRESULT hr = pSpeakerEndpointVolume->GetMasterVolumeLevelScalar(&currentVolume);
    if (FAILED(hr)) {
        if (pSpeakerEndpointVolume) { pSpeakerEndpointVolume->Release(); pSpeakerEndpointVolume = nullptr; }
        if (pSpeakerDevice) { pSpeakerDevice->Release(); pSpeakerDevice = nullptr; }
        if (!InitializeSpeaker()) return false;
        hr = pSpeakerEndpointVolume->GetMasterVolumeLevelScalar(&currentVolume);
    }

    if (SUCCEEDED(hr)) {
        volume = currentVolume;
        return true;
    }

    return false;
}

bool AudioManager::GetSpeakerMute(bool& mute) {
    RefreshDefaultDevicesIfChanged();
    if (!InitializeSpeaker()) return false;

    BOOL currentMute = FALSE;
    HRESULT hr = pSpeakerEndpointVolume->GetMute(&currentMute);
    if (FAILED(hr)) {
        if (pSpeakerEndpointVolume) { pSpeakerEndpointVolume->Release(); pSpeakerEndpointVolume = nullptr; }
        if (pSpeakerDevice) { pSpeakerDevice->Release(); pSpeakerDevice = nullptr; }
        if (!InitializeSpeaker()) return false;
        hr = pSpeakerEndpointVolume->GetMute(&currentMute);
    }

    if (SUCCEEDED(hr)) {
        mute = (currentMute != FALSE);
        return true;
    }

    return false;
}

bool AudioManager::SetMicrophoneVolume(float volume) {
    RefreshDefaultDevicesIfChanged();
    if (!InitializeMicrophone()) return false;

    // 音量范围检查
    if (volume < 0.0f || volume > 1.0f) return false;

    HRESULT hr = pMicrophoneEndpointVolume->SetMasterVolumeLevelScalar(volume, nullptr);
    if (FAILED(hr)) {
        if (pMicrophoneEndpointVolume) { pMicrophoneEndpointVolume->Release(); pMicrophoneEndpointVolume = nullptr; }
        if (pMicrophoneDevice) { pMicrophoneDevice->Release(); pMicrophoneDevice = nullptr; }
        if (!InitializeMicrophone()) return false;
        hr = pMicrophoneEndpointVolume->SetMasterVolumeLevelScalar(volume, nullptr);
    }
    return SUCCEEDED(hr);
}

bool AudioManager::SetMicrophoneMute(bool mute) {
    RefreshDefaultDevicesIfChanged();
    if (!InitializeMicrophone()) return false;

    HRESULT hr = pMicrophoneEndpointVolume->SetMute(mute, nullptr);
    if (FAILED(hr)) {
        if (pMicrophoneEndpointVolume) { pMicrophoneEndpointVolume->Release(); pMicrophoneEndpointVolume = nullptr; }
        if (pMicrophoneDevice) { pMicrophoneDevice->Release(); pMicrophoneDevice = nullptr; }
        if (!InitializeMicrophone()) return false;
        hr = pMicrophoneEndpointVolume->SetMute(mute, nullptr);
    }
    return SUCCEEDED(hr);
}

bool AudioManager::GetMicrophoneVolume(float& volume) {
    RefreshDefaultDevicesIfChanged();
    if (!InitializeMicrophone()) return false;

    float currentVolume = 0.0f;
    HRESULT hr = pMicrophoneEndpointVolume->GetMasterVolumeLevelScalar(&currentVolume);
    if (FAILED(hr)) {
        if (pMicrophoneEndpointVolume) { pMicrophoneEndpointVolume->Release(); pMicrophoneEndpointVolume = nullptr; }
        if (pMicrophoneDevice) { pMicrophoneDevice->Release(); pMicrophoneDevice = nullptr; }
        if (!InitializeMicrophone()) return false;
        hr = pMicrophoneEndpointVolume->GetMasterVolumeLevelScalar(&currentVolume);
    }

    if (SUCCEEDED(hr)) {
        volume = currentVolume;
        return true;
    }

    return false;
}

bool AudioManager::GetMicrophoneMute(bool& mute) {
    RefreshDefaultDevicesIfChanged();
    if (!InitializeMicrophone()) return false;

    BOOL currentMute = FALSE;
    HRESULT hr = pMicrophoneEndpointVolume->GetMute(&currentMute);
    if (FAILED(hr)) {
        if (pMicrophoneEndpointVolume) { pMicrophoneEndpointVolume->Release(); pMicrophoneEndpointVolume = nullptr; }
        if (pMicrophoneDevice) { pMicrophoneDevice->Release(); pMicrophoneDevice = nullptr; }
        if (!InitializeMicrophone()) return false;
        hr = pMicrophoneEndpointVolume->GetMute(&currentMute);
    }

    if (SUCCEEDED(hr)) {
        mute = (currentMute != FALSE);
        return true;
    }

    return false;
}

bool AudioManager::InitializeSessionManager() {
    if (pSessionManager) return true; // 已经初始化

    try {
        // 获取音频设备枚举器
        IMMDeviceEnumerator* pEnumerator = nullptr;
        HRESULT hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator),
            (void**)&pEnumerator
        );

        if (FAILED(hr) || !pEnumerator) return false;

        // 获取默认音频渲染设备（扬声器）
        IMMDevice* pDevice = nullptr;
        hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);

        if (FAILED(hr) || !pDevice) {
            pEnumerator->Release();
            return false;
        }

        // 获取音频会话管理器
        IAudioSessionManager2* pSessionManager2 = nullptr;
        hr = pDevice->Activate(
            __uuidof(IAudioSessionManager2),
            CLSCTX_ALL,
            nullptr,
            (void**)&pSessionManager2
        );

        pEnumerator->Release();
        pDevice->Release();

        if (FAILED(hr) || !pSessionManager2) {
            return false;
        }

        // 获取会话枚举器
        hr = pSessionManager2->GetSessionEnumerator(&pSessionEnumerator);

        if (FAILED(hr) || !pSessionEnumerator) {
            pSessionManager2->Release();
            return false;
        }

        pSessionManager = pSessionManager2;
        return true;
    } catch (...) {
        return false;
    }
}

std::vector<AudioSessionInfo> AudioManager::GetAllAudioSessions() {
    std::vector<AudioSessionInfo> sessions;

    if (!InitializeSessionManager()) return sessions;

    try {
        // 每次刷新一次枚举器，避免缓存失效
        IAudioSessionManager2* pMgr2 = nullptr;
        HRESULT hr = ((pSessionManager) ? ((IAudioSessionManager*)pSessionManager)->QueryInterface(__uuidof(IAudioSessionManager2), (void**)&pMgr2) : E_POINTER);
        if (FAILED(hr) || !pMgr2) {
            // 重新初始化会话管理器
            if (pSessionEnumerator) { pSessionEnumerator->Release(); pSessionEnumerator = nullptr; }
            if (pSessionManager) { pSessionManager->Release(); pSessionManager = nullptr; }
            if (!InitializeSessionManager()) return sessions;
            ((IAudioSessionManager*)pSessionManager)->QueryInterface(__uuidof(IAudioSessionManager2), (void**)&pMgr2);
        }
        IAudioSessionEnumerator* pEnumLocal = nullptr;
        hr = (pMgr2 ? pMgr2->GetSessionEnumerator(&pEnumLocal) : E_POINTER);
        if (pMgr2) pMgr2->Release();
        if (FAILED(hr) || !pEnumLocal) return sessions;

        int sessionCount = 0;
        hr = pEnumLocal->GetCount(&sessionCount);

        if (FAILED(hr)) return sessions;

        for (int i = 0; i < sessionCount; ++i) {
            IAudioSessionControl* pSessionControl = nullptr;
            IAudioSessionControl2* pSessionControl2 = nullptr;
            ISimpleAudioVolume* pSimpleVolume = nullptr;

            hr = pEnumLocal->GetSession(i, &pSessionControl);

            if (FAILED(hr) || !pSessionControl) continue;

            // 获取音量控制接口
            hr = pSessionControl->QueryInterface(__uuidof(ISimpleAudioVolume), (void**)&pSimpleVolume);

            if (FAILED(hr) || !pSimpleVolume) {
                pSessionControl->Release();
                continue;
            }

            // 获取进程ID
            DWORD processId = 0;
            hr = pSessionControl->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&pSessionControl2);

            if (SUCCEEDED(hr) && pSessionControl2) {
                hr = pSessionControl2->GetProcessId(&processId);
                if (FAILED(hr)) processId = 0;
                pSessionControl2->Release();
            }

            // 获取音量
            float volume = 0.0f;
            pSimpleVolume->GetMasterVolume(&volume);

            AudioSessionInfo sessionInfo;

            if (processId > 0) {
                // 有进程ID的情况
                sessionInfo.name = GetProcessNameFromPid(processId);
                sessionInfo.id = sessionInfo.name;
                sessionInfo.path = GetProcessPathFromPid(processId);
                sessionInfo.volume = volume;

                // 获取图标
                if (!sessionInfo.path.empty()) {
                    sessionInfo.icon = GetIconBase64(sessionInfo.path, 64, false);
                } else {
                    sessionInfo.icon = "";
                }
            } else {
                // 系统声音
                sessionInfo.name = "$SystemSounds";
                sessionInfo.id = "$SystemSounds";
                sessionInfo.path = "";
                sessionInfo.volume = volume;
                sessionInfo.icon = "";
            }

            sessions.push_back(sessionInfo);

            // 释放接口
            pSimpleVolume->Release();
            pSessionControl->Release();
        }
        pEnumLocal->Release();
    } catch (...) {
        // 忽略错误，继续处理其他会话
    }

    return sessions;
}

bool AudioManager::SetAudioSessionVolume(const std::string& sessionId, float volume) {
    if (volume < 0.0f || volume > 1.0f) return false;

    if (!InitializeSessionManager()) return false;

    try {
        IAudioSessionManager2* pMgr2 = nullptr;
        HRESULT hr = ((pSessionManager) ? ((IAudioSessionManager*)pSessionManager)->QueryInterface(__uuidof(IAudioSessionManager2), (void**)&pMgr2) : E_POINTER);
        if (FAILED(hr) || !pMgr2) {
            if (pSessionEnumerator) { pSessionEnumerator->Release(); pSessionEnumerator = nullptr; }
            if (pSessionManager) { pSessionManager->Release(); pSessionManager = nullptr; }
            if (!InitializeSessionManager()) return false;
            ((IAudioSessionManager*)pSessionManager)->QueryInterface(__uuidof(IAudioSessionManager2), (void**)&pMgr2);
        }
        IAudioSessionEnumerator* pEnumLocal = nullptr;
        hr = (pMgr2 ? pMgr2->GetSessionEnumerator(&pEnumLocal) : E_POINTER);
        if (pMgr2) pMgr2->Release();
        if (FAILED(hr) || !pEnumLocal) return false;

        int sessionCount = 0;
        hr = pEnumLocal->GetCount(&sessionCount);

        if (FAILED(hr)) return false;

        for (int i = 0; i < sessionCount; ++i) {
            IAudioSessionControl* pSessionControl = nullptr;
            IAudioSessionControl2* pSessionControl2 = nullptr;
            ISimpleAudioVolume* pSimpleVolume = nullptr;

            hr = pEnumLocal->GetSession(i, &pSessionControl);

            if (FAILED(hr) || !pSessionControl) continue;

            // 获取音量控制接口
            hr = pSessionControl->QueryInterface(__uuidof(ISimpleAudioVolume), (void**)&pSimpleVolume);

            if (FAILED(hr) || !pSimpleVolume) {
                pSessionControl->Release();
                continue;
            }

            // 获取进程ID来匹配会话
            DWORD processId = 0;
            hr = pSessionControl->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&pSessionControl2);

            if (SUCCEEDED(hr) && pSessionControl2) {
                hr = pSessionControl2->GetProcessId(&processId);
                if (FAILED(hr)) processId = 0;
                pSessionControl2->Release();
            }

            std::string currentSessionId;
            if (processId > 0) {
                currentSessionId = GetProcessNameFromPid(processId);
            } else {
                currentSessionId = "$SystemSounds";
            }

            // 如果找到匹配的会话，设置音量
            if (currentSessionId == sessionId) {
                hr = pSimpleVolume->SetMasterVolume(volume, nullptr);
                pSimpleVolume->Release();
                pSessionControl->Release();
                return SUCCEEDED(hr);
            }

            // 释放接口
            pSimpleVolume->Release();
            pSessionControl->Release();
        }
        pEnumLocal->Release();
    } catch (...) {
        return false;
    }

    return false; // 未找到匹配的会话
}

float AudioManager::GetAudioSessionVolume(const std::string& sessionId) {
    if (!InitializeSessionManager()) return -1.0f;

    try {
        IAudioSessionManager2* pMgr2 = nullptr;
        HRESULT hr = ((pSessionManager) ? ((IAudioSessionManager*)pSessionManager)->QueryInterface(__uuidof(IAudioSessionManager2), (void**)&pMgr2) : E_POINTER);
        if (FAILED(hr) || !pMgr2) {
            if (pSessionEnumerator) { pSessionEnumerator->Release(); pSessionEnumerator = nullptr; }
            if (pSessionManager) { pSessionManager->Release(); pSessionManager = nullptr; }
            if (!InitializeSessionManager()) return -1.0f;
            ((IAudioSessionManager*)pSessionManager)->QueryInterface(__uuidof(IAudioSessionManager2), (void**)&pMgr2);
        }
        IAudioSessionEnumerator* pEnumLocal = nullptr;
        hr = (pMgr2 ? pMgr2->GetSessionEnumerator(&pEnumLocal) : E_POINTER);
        if (pMgr2) pMgr2->Release();
        if (FAILED(hr) || !pEnumLocal) return -1.0f;

        int sessionCount = 0;
        hr = pEnumLocal->GetCount(&sessionCount);

        if (FAILED(hr)) return -1.0f;

        for (int i = 0; i < sessionCount; ++i) {
            IAudioSessionControl* pSessionControl = nullptr;
            IAudioSessionControl2* pSessionControl2 = nullptr;
            ISimpleAudioVolume* pSimpleVolume = nullptr;

            hr = pEnumLocal->GetSession(i, &pSessionControl);

            if (FAILED(hr) || !pSessionControl) continue;

            // 获取音量控制接口
            hr = pSessionControl->QueryInterface(__uuidof(ISimpleAudioVolume), (void**)&pSimpleVolume);

            if (FAILED(hr) || !pSimpleVolume) {
                pSessionControl->Release();
                continue;
            }

            // 获取进程ID来匹配会话
            DWORD processId = 0;
            hr = pSessionControl->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&pSessionControl2);

            if (SUCCEEDED(hr) && pSessionControl2) {
                hr = pSessionControl2->GetProcessId(&processId);
                if (FAILED(hr)) processId = 0;
                pSessionControl2->Release();
            }

            std::string currentSessionId;
            if (processId > 0) {
                currentSessionId = GetProcessNameFromPid(processId);
            } else {
                currentSessionId = "$SystemSounds";
            }

            // 如果找到匹配的会话，返回音量
            if (currentSessionId == sessionId) {
                float volume = 0.0f;
                hr = pSimpleVolume->GetMasterVolume(&volume);
                pSimpleVolume->Release();
                pSessionControl->Release();
                return SUCCEEDED(hr) ? volume : -1.0f;
            }

            // 释放接口
            pSimpleVolume->Release();
            pSessionControl->Release();
        }
        pEnumLocal->Release();
    } catch (...) {
        return -1.0f;
    }

    return -1.0f; // 未找到匹配的会话
}

std::string AudioManager::GetProcessNameFromPid(DWORD pid) {
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProcess) return "Unknown";

    char processName[MAX_PATH] = { 0 };
    DWORD size = MAX_PATH;
    if (QueryFullProcessImageNameA(hProcess, 0, processName, &size)) {
        // 提取文件名（不含路径）
        char* fileName = PathFindFileNameA(processName);
        std::string name = fileName;
        // 去掉扩展名
        size_t dotPos = name.find_last_of('.');
        if (dotPos != std::string::npos) {
            name = name.substr(0, dotPos);
        }
        CloseHandle(hProcess);
        return name;
    }

    CloseHandle(hProcess);
    return "Unknown";
}

std::string AudioManager::GetProcessPathFromPid(DWORD pid) {
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProcess) return "";

    char processPath[MAX_PATH] = { 0 };
    DWORD size = MAX_PATH;
    if (QueryFullProcessImageNameA(hProcess, 0, processPath, &size)) {
        CloseHandle(hProcess);
        return std::string(processPath);
    }

    CloseHandle(hProcess);
    return "";
}

std::string AudioManager::GetIconBase64(const std::string& filePath, int iconSize, bool bgr, bool alpha) {
    if (filePath.empty()) {
        return "";
    }

    // 初始化GDI+
    static bool gdiPlusInitialized = false;
    if (!gdiPlusInitialized) {
        Gdiplus::GdiplusStartupInput gdiplusStartupInput;
        ULONG_PTR gdiplusToken;
        Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);
        gdiPlusInitialized = true;
    }

    std::string result;

    try {
        // 提取图标
        SHFILEINFO sfi = { 0 };
        DWORD flags = SHGFI_ICON | SHGFI_LARGEICON;
        
        // 检查文件是否存在以及是否为可执行文件
        DWORD fileAttr = GetFileAttributesA(filePath.c_str());
        bool fileExists = (fileAttr != INVALID_FILE_ATTRIBUTES);
        
        // 判断是否为目录或可执行文件
        bool isDirectory = fileExists && (fileAttr & FILE_ATTRIBUTE_DIRECTORY);
        bool isExecutable = false;
        if (fileExists && !isDirectory) {
            const char* ext = strrchr(filePath.c_str(), '.');
            isExecutable = (ext && (_stricmp(ext, ".exe") == 0 || _stricmp(ext, ".dll") == 0));
        }
        
        // 对于非可执行文件和不存在的文件（可能是文件扩展名），使用SHGFI_USEFILEATTRIBUTES
        // 这样可以获取关联程序的图标
        if (!isExecutable && !isDirectory) {
            flags |= SHGFI_USEFILEATTRIBUTES;
            fileAttr = FILE_ATTRIBUTE_NORMAL;
            if (SHGetFileInfoA(filePath.c_str(), fileAttr, &sfi, sizeof(sfi), flags)) {
                // 成功获取关联图标
            } else if (fileExists) {
                // 如果失败且文件存在，尝试不使用SHGFI_USEFILEATTRIBUTES
                flags = SHGFI_ICON | SHGFI_LARGEICON;
                SHGetFileInfoA(filePath.c_str(), 0, &sfi, sizeof(sfi), flags);
            }
        } else {
            // 对于可执行文件和目录，直接获取图标
            SHGetFileInfoA(filePath.c_str(), 0, &sfi, sizeof(sfi), flags);
        }
        
        if (sfi.hIcon) {
            // 获取屏幕DC
            HDC hdc = GetDC(nullptr);
            HDC hdcMem = CreateCompatibleDC(hdc);
            
            // 创建32位ARGB位图，用于支持alpha通道
            BITMAPINFO bmi = { 0 };
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = iconSize;
            bmi.bmiHeader.biHeight = -iconSize; // 负值表示自顶向下的位图
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;
            
            void* pBits = nullptr;
            HBITMAP hbmp = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
            
            if (hbmp && pBits) {
                HBITMAP hbmpOld = (HBITMAP)SelectObject(hdcMem, hbmp);
                
                // 先填充透明背景
                if (alpha) {
                    // 填充全透明
                    memset(pBits, 0, iconSize * iconSize * 4);
                } else {
                    // 填充黑色背景
                    for (int i = 0; i < iconSize * iconSize; i++) {
                        ((DWORD*)pBits)[i] = 0x0;
                    }
                }
                
                // 使用DrawIconEx绘制图标，保留alpha通道
                DrawIconEx(hdcMem, 0, 0, sfi.hIcon, iconSize, iconSize, 0, nullptr, DI_NORMAL);
                
                // 处理像素数据
                DWORD* pixels = (DWORD*)pBits;
                
                // 检查图标是否有alpha通道
                bool hasAlpha = false;
                if (alpha) {
                    for (int i = 0; i < iconSize * iconSize; i++) {
                        if ((pixels[i] & 0xFF000000) != 0) {
                            hasAlpha = true;
                            break;
                        }
                    }
                }
                
                // 根据是否需要alpha通道来决定字节数和stride
                int bytesPerPixel = alpha ? 4 : 3;
                int stride = ((iconSize * bytesPerPixel + 3) / 4) * 4; // 对齐到4字节
                std::vector<unsigned char> imageData(stride * iconSize);
                
                // 转换像素数据
                for (int y = 0; y < iconSize; y++) {
                    for (int x = 0; x < iconSize; x++) {
                        int pixelIndex = y * iconSize + x;
                        int dataIndex = y * stride + x * bytesPerPixel;
                        
                        DWORD pixel = pixels[pixelIndex];
                        unsigned char b = (pixel & 0xFF);
                        unsigned char g = ((pixel >> 8) & 0xFF);
                        unsigned char r = ((pixel >> 16) & 0xFF);
                        unsigned char a = ((pixel >> 24) & 0xFF);
                        
                        // 如果图标没有alpha通道信息，从颜色掩码推断
                        if (alpha && !hasAlpha) {
                            // 如果像素完全是黑色（0,0,0），则认为是透明的
                            if (r == 0 && g == 0 && b == 0) {
                                a = 0;
                            } else {
                                a = 255;
                            }
                        }
                        
                        // 注意：这里的bgr参数控制输出通道顺序，true表示BGR，false表示RGB
                        if (alpha) {
                            if (bgr) {
                                imageData[dataIndex + 0] = r;
                                imageData[dataIndex + 1] = g;
                                imageData[dataIndex + 2] = b;
                                imageData[dataIndex + 3] = a;
                            } else {
                                imageData[dataIndex + 0] = b;
                                imageData[dataIndex + 1] = g;
                                imageData[dataIndex + 2] = r;
                                imageData[dataIndex + 3] = a;
                            }
                        } else {
                            if (bgr) {
                                imageData[dataIndex + 0] = r;
                                imageData[dataIndex + 1] = g;
                                imageData[dataIndex + 2] = b;
                            } else {
                                imageData[dataIndex + 0] = b;
                                imageData[dataIndex + 1] = g;
                                imageData[dataIndex + 2] = r;
                            }
                        }
                    }
                }
                
                // 使用GDI+将图像数据编码为PNG
                Gdiplus::Bitmap* bitmap = new Gdiplus::Bitmap(
                    iconSize, 
                    iconSize, 
                    stride,
                    alpha ? PixelFormat32bppARGB : PixelFormat24bppRGB,
                    imageData.data()
                );
                
                if (bitmap && bitmap->GetLastStatus() == Gdiplus::Ok) {
                    IStream* stream = nullptr;
                    if (CreateStreamOnHGlobal(nullptr, TRUE, &stream) == S_OK && stream) {
                        CLSID pngClsid;
                        CLSIDFromString(L"{557CF406-1A04-11D3-9A73-0000F81EF32E}", &pngClsid);
                        if (bitmap->Save(stream, &pngClsid) == Gdiplus::Ok) {
                            STATSTG stat; 
                            stream->Stat(&stat, STATFLAG_NONAME);
                            LARGE_INTEGER zero = { 0 }; 
                            stream->Seek(zero, STREAM_SEEK_SET, nullptr);
                            std::vector<unsigned char> buffer(stat.cbSize.LowPart);
                            ULONG bytesRead; 
                            stream->Read(buffer.data(), stat.cbSize.LowPart, &bytesRead);
                            result = base64_encode(buffer);
                        }
                        stream->Release();
                    }
                }
                if (bitmap) delete bitmap;
                
                SelectObject(hdcMem, hbmpOld);
                DeleteObject(hbmp);
            }
            
            DeleteDC(hdcMem);
            ReleaseDC(nullptr, hdc);
            DestroyIcon(sfi.hIcon);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error extracting icon from " << filePath << ": " << e.what() << std::endl;
        result = "";
    }

    return result;
}

// 获取设备名称
std::string AudioManager::GetDeviceName(IMMDevice* device) {
    if (!device) return "";
    IPropertyStore* pProps = nullptr;
    PROPVARIANT varName;
    PropVariantInit(&varName);
    std::string name;

    HRESULT hr = device->OpenPropertyStore(STGM_READ, &pProps);
    if (SUCCEEDED(hr) && pProps) {
        hr = pProps->GetValue(PKEY_Device_FriendlyName, &varName);
        if (SUCCEEDED(hr) && varName.vt == VT_LPWSTR) {
            int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, varName.pwszVal, -1, nullptr, 0, nullptr, nullptr);
            if (sizeNeeded > 0) {
                std::vector<char> buf(sizeNeeded);
                WideCharToMultiByte(CP_UTF8, 0, varName.pwszVal, -1, buf.data(), sizeNeeded, nullptr, nullptr);
                name.assign(buf.data());
            }
        }
        PropVariantClear(&varName);
        pProps->Release();
    }
    return name;
}

// 获取设备ID
std::string AudioManager::GetDeviceId(IMMDevice* device) {
    if (!device) return "";
    LPWSTR pwszID = nullptr;
    std::string id;
    if (SUCCEEDED(device->GetId(&pwszID)) && pwszID) {
        int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, pwszID, -1, nullptr, 0, nullptr, nullptr);
        if (sizeNeeded > 0) {
            std::vector<char> buf(sizeNeeded);
            WideCharToMultiByte(CP_UTF8, 0, pwszID, -1, buf.data(), sizeNeeded, nullptr, nullptr);
            id.assign(buf.data());
        }
        CoTaskMemFree(pwszID);
    }
    return id;
}

// 获取当前默认设备ID（渲染/采集）
std::string AudioManager::GetDefaultDeviceId(EDataFlow flow) {
    IMMDeviceEnumerator* pEnumerator = nullptr;
    std::string id;
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        (void**)&pEnumerator
    );
    if (SUCCEEDED(hr) && pEnumerator) {
        IMMDevice* pDevice = nullptr;
        if (SUCCEEDED(pEnumerator->GetDefaultAudioEndpoint(flow, eConsole, &pDevice)) && pDevice) {
            id = GetDeviceId(pDevice);
            pDevice->Release();
        }
        pEnumerator->Release();
    }
    return id;
}

// 若默认设备切换则重建对应 EndpointVolume
bool AudioManager::RefreshDefaultDevicesIfChanged() {
    bool changed = false;
    // Speaker
    std::string defSpeaker = GetDefaultDeviceId(eRender);
    if (!defSpeaker.empty() && defSpeaker != currentSpeakerDeviceId) {
        if (pSpeakerEndpointVolume) { pSpeakerEndpointVolume->Release(); pSpeakerEndpointVolume = nullptr; }
        if (pSpeakerDevice) { pSpeakerDevice->Release(); pSpeakerDevice = nullptr; }
        InitializeSpeaker();
        changed = true;
    }
    // Microphone
    std::string defMic = GetDefaultDeviceId(eCapture);
    if (!defMic.empty() && defMic != currentMicrophoneDeviceId) {
        if (pMicrophoneEndpointVolume) { pMicrophoneEndpointVolume->Release(); pMicrophoneEndpointVolume = nullptr; }
        if (pMicrophoneDevice) { pMicrophoneDevice->Release(); pMicrophoneDevice = nullptr; }
        InitializeMicrophone();
        changed = true;
    }
    return changed;
}

// 获取所有输出设备
std::vector<OutputDeviceInfo> AudioManager::GetAllOutputDevices() {
    std::vector<OutputDeviceInfo> devices;
    
    try {
        IMMDeviceEnumerator* pEnumerator = nullptr;
        HRESULT hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator),
            (void**)&pEnumerator
        );
        
        if (FAILED(hr) || !pEnumerator) return devices;
        
        IMMDeviceCollection* pCollection = nullptr;
        hr = pEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pCollection);
        
        if (FAILED(hr) || !pCollection) {
            pEnumerator->Release();
            return devices;
        }
        
        UINT count = 0;
        hr = pCollection->GetCount(&count);
        
        if (SUCCEEDED(hr)) {
            for (UINT i = 0; i < count; i++) {
                IMMDevice* pDevice = nullptr;
                hr = pCollection->Item(i, &pDevice);
                
                if (SUCCEEDED(hr) && pDevice) {
                    OutputDeviceInfo info;
                    info.id = GetDeviceId(pDevice);
                    info.name = GetDeviceName(pDevice);
                    
                    IAudioEndpointVolume* pEndpointVolume = nullptr;
                    hr = pDevice->Activate(
                        __uuidof(IAudioEndpointVolume),
                        CLSCTX_ALL,
                        nullptr,
                        (void**)&pEndpointVolume
                    );
                    
                    if (SUCCEEDED(hr) && pEndpointVolume) {
                        hr = pEndpointVolume->GetMasterVolumeLevelScalar(&info.volume);
                        if (FAILED(hr)) info.volume = 0.0f;
                        
                        BOOL mute = FALSE;
                        hr = pEndpointVolume->GetMute(&mute);
                        if (SUCCEEDED(hr)) {
                            info.mute = (mute != FALSE);
                        } else {
                            info.mute = false;
                        }
                        
                        pEndpointVolume->Release();
                    } else {
                        info.volume = 0.0f;
                        info.mute = false;
                    }
                    
                    devices.push_back(info);
                    pDevice->Release();
                }
            }
        }
        
        pCollection->Release();
        pEnumerator->Release();
    } catch (...) {
        return devices;
    }
    
    return devices;
}

// 设置输出设备音量
bool AudioManager::SetOutputDeviceVolume(const std::string& deviceId, float volume) {
    if (deviceId.empty() || volume < 0.0f || volume > 1.0f) return false;
    
    try {
        IMMDeviceEnumerator* pEnumerator = nullptr;
        HRESULT hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator),
            (void**)&pEnumerator
        );
        
        if (FAILED(hr) || !pEnumerator) return false;
        
        // 将 UTF-8 字符串转换为宽字符串
        int wideSize = MultiByteToWideChar(CP_UTF8, 0, deviceId.c_str(), -1, nullptr, 0);
        if (wideSize <= 0) {
            pEnumerator->Release();
            return false;
        }
        std::vector<wchar_t> wideBuffer(wideSize);
        MultiByteToWideChar(CP_UTF8, 0, deviceId.c_str(), -1, wideBuffer.data(), wideSize);
        
        IMMDevice* pDevice = nullptr;
        hr = pEnumerator->GetDevice(wideBuffer.data(), &pDevice);
        
        pEnumerator->Release();
        
        if (FAILED(hr) || !pDevice) return false;
        
        IAudioEndpointVolume* pEndpointVolume = nullptr;
        hr = pDevice->Activate(
            __uuidof(IAudioEndpointVolume),
            CLSCTX_ALL,
            nullptr,
            (void**)&pEndpointVolume
        );
        
        pDevice->Release();
        
        if (FAILED(hr) || !pEndpointVolume) return false;
        
        hr = pEndpointVolume->SetMasterVolumeLevelScalar(volume, nullptr);
        pEndpointVolume->Release();
        
        return SUCCEEDED(hr);
    } catch (...) {
        return false;
    }
}

// 设置输出设备静音状态
bool AudioManager::SetOutputDeviceMute(const std::string& deviceId, bool mute) {
    if (deviceId.empty()) return false;
    
    try {
        IMMDeviceEnumerator* pEnumerator = nullptr;
        HRESULT hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator),
            (void**)&pEnumerator
        );
        
        if (FAILED(hr) || !pEnumerator) return false;
        
        // 将 UTF-8 字符串转换为宽字符串
        int wideSize = MultiByteToWideChar(CP_UTF8, 0, deviceId.c_str(), -1, nullptr, 0);
        if (wideSize <= 0) {
            pEnumerator->Release();
            return false;
        }
        std::vector<wchar_t> wideBuffer(wideSize);
        MultiByteToWideChar(CP_UTF8, 0, deviceId.c_str(), -1, wideBuffer.data(), wideSize);
        
        IMMDevice* pDevice = nullptr;
        hr = pEnumerator->GetDevice(wideBuffer.data(), &pDevice);
        
        pEnumerator->Release();
        
        if (FAILED(hr) || !pDevice) return false;
        
        IAudioEndpointVolume* pEndpointVolume = nullptr;
        hr = pDevice->Activate(
            __uuidof(IAudioEndpointVolume),
            CLSCTX_ALL,
            nullptr,
            (void**)&pEndpointVolume
        );
        
        pDevice->Release();
        
        if (FAILED(hr) || !pEndpointVolume) return false;
        
        hr = pEndpointVolume->SetMute(mute ? TRUE : FALSE, nullptr);
        pEndpointVolume->Release();
        
        return SUCCEEDED(hr);
    } catch (...) {
        return false;
    }
}

// 获取输出设备音量
bool AudioManager::GetOutputDeviceVolume(const std::string& deviceId, float& volume) {
    if (deviceId.empty()) return false;
    
    try {
        IMMDeviceEnumerator* pEnumerator = nullptr;
        HRESULT hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator),
            (void**)&pEnumerator
        );
        
        if (FAILED(hr) || !pEnumerator) return false;
        
        // 将 UTF-8 字符串转换为宽字符串
        int wideSize = MultiByteToWideChar(CP_UTF8, 0, deviceId.c_str(), -1, nullptr, 0);
        if (wideSize <= 0) {
            pEnumerator->Release();
            return false;
        }
        std::vector<wchar_t> wideBuffer(wideSize);
        MultiByteToWideChar(CP_UTF8, 0, deviceId.c_str(), -1, wideBuffer.data(), wideSize);
        
        IMMDevice* pDevice = nullptr;
        hr = pEnumerator->GetDevice(wideBuffer.data(), &pDevice);
        
        pEnumerator->Release();
        
        if (FAILED(hr) || !pDevice) return false;
        
        IAudioEndpointVolume* pEndpointVolume = nullptr;
        hr = pDevice->Activate(
            __uuidof(IAudioEndpointVolume),
            CLSCTX_ALL,
            nullptr,
            (void**)&pEndpointVolume
        );
        
        pDevice->Release();
        
        if (FAILED(hr) || !pEndpointVolume) return false;
        
        hr = pEndpointVolume->GetMasterVolumeLevelScalar(&volume);
        pEndpointVolume->Release();
        
        return SUCCEEDED(hr);
    } catch (...) {
        return false;
    }
}

// 获取输出设备静音状态
bool AudioManager::GetOutputDeviceMute(const std::string& deviceId, bool& mute) {
    if (deviceId.empty()) return false;
    
    try {
        IMMDeviceEnumerator* pEnumerator = nullptr;
        HRESULT hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator),
            (void**)&pEnumerator
        );
        
        if (FAILED(hr) || !pEnumerator) return false;
        
        // 将 UTF-8 字符串转换为宽字符串
        int wideSize = MultiByteToWideChar(CP_UTF8, 0, deviceId.c_str(), -1, nullptr, 0);
        if (wideSize <= 0) {
            pEnumerator->Release();
            return false;
        }
        std::vector<wchar_t> wideBuffer(wideSize);
        MultiByteToWideChar(CP_UTF8, 0, deviceId.c_str(), -1, wideBuffer.data(), wideSize);
        
        IMMDevice* pDevice = nullptr;
        hr = pEnumerator->GetDevice(wideBuffer.data(), &pDevice);
        
        pEnumerator->Release();
        
        if (FAILED(hr) || !pDevice) return false;
        
        IAudioEndpointVolume* pEndpointVolume = nullptr;
        hr = pDevice->Activate(
            __uuidof(IAudioEndpointVolume),
            CLSCTX_ALL,
            nullptr,
            (void**)&pEndpointVolume
        );
        
        pDevice->Release();
        
        if (FAILED(hr) || !pEndpointVolume) return false;
        
        BOOL muteValue = FALSE;
        hr = pEndpointVolume->GetMute(&muteValue);
        pEndpointVolume->Release();
        
        if (SUCCEEDED(hr)) {
            mute = (muteValue != FALSE);
            return true;
        }
        
        return false;
    } catch (...) {
        return false;
    }
}

// 获取所有输入设备
std::vector<InputDeviceInfo> AudioManager::GetAllInputDevices() {
    std::vector<InputDeviceInfo> devices;
    
    try {
        IMMDeviceEnumerator* pEnumerator = nullptr;
        HRESULT hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator),
            (void**)&pEnumerator
        );
        
        if (FAILED(hr) || !pEnumerator) return devices;
        
        IMMDeviceCollection* pCollection = nullptr;
        hr = pEnumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &pCollection);
        
        if (FAILED(hr) || !pCollection) {
            pEnumerator->Release();
            return devices;
        }
        
        UINT count = 0;
        hr = pCollection->GetCount(&count);
        
        if (SUCCEEDED(hr)) {
            for (UINT i = 0; i < count; i++) {
                IMMDevice* pDevice = nullptr;
                hr = pCollection->Item(i, &pDevice);
                
                if (SUCCEEDED(hr) && pDevice) {
                    InputDeviceInfo info;
                    info.id = GetDeviceId(pDevice);
                    info.name = GetDeviceName(pDevice);
                    
                    IAudioEndpointVolume* pEndpointVolume = nullptr;
                    hr = pDevice->Activate(
                        __uuidof(IAudioEndpointVolume),
                        CLSCTX_ALL,
                        nullptr,
                        (void**)&pEndpointVolume
                    );
                    
                    if (SUCCEEDED(hr) && pEndpointVolume) {
                        hr = pEndpointVolume->GetMasterVolumeLevelScalar(&info.volume);
                        if (FAILED(hr)) info.volume = 0.0f;
                        
                        BOOL mute = FALSE;
                        hr = pEndpointVolume->GetMute(&mute);
                        if (SUCCEEDED(hr)) {
                            info.mute = (mute != FALSE);
                        } else {
                            info.mute = false;
                        }
                        
                        pEndpointVolume->Release();
                    } else {
                        info.volume = 0.0f;
                        info.mute = false;
                    }
                    
                    devices.push_back(info);
                    pDevice->Release();
                }
            }
        }
        
        pCollection->Release();
        pEnumerator->Release();
    } catch (...) {
        return devices;
    }
    
    return devices;
}

// 设置输入设备音量
bool AudioManager::SetInputDeviceVolume(const std::string& deviceId, float volume) {
    if (deviceId.empty() || volume < 0.0f || volume > 1.0f) return false;
    
    try {
        IMMDeviceEnumerator* pEnumerator = nullptr;
        HRESULT hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator),
            (void**)&pEnumerator
        );
        
        if (FAILED(hr) || !pEnumerator) return false;
        
        // 将 UTF-8 字符串转换为宽字符串
        int wideSize = MultiByteToWideChar(CP_UTF8, 0, deviceId.c_str(), -1, nullptr, 0);
        if (wideSize <= 0) {
            pEnumerator->Release();
            return false;
        }
        std::vector<wchar_t> wideBuffer(wideSize);
        MultiByteToWideChar(CP_UTF8, 0, deviceId.c_str(), -1, wideBuffer.data(), wideSize);
        
        IMMDevice* pDevice = nullptr;
        hr = pEnumerator->GetDevice(wideBuffer.data(), &pDevice);
        
        pEnumerator->Release();
        
        if (FAILED(hr) || !pDevice) return false;
        
        IAudioEndpointVolume* pEndpointVolume = nullptr;
        hr = pDevice->Activate(
            __uuidof(IAudioEndpointVolume),
            CLSCTX_ALL,
            nullptr,
            (void**)&pEndpointVolume
        );
        
        pDevice->Release();
        
        if (FAILED(hr) || !pEndpointVolume) return false;
        
        hr = pEndpointVolume->SetMasterVolumeLevelScalar(volume, nullptr);
        pEndpointVolume->Release();
        
        return SUCCEEDED(hr);
    } catch (...) {
        return false;
    }
}

// 设置输入设备静音状态
bool AudioManager::SetInputDeviceMute(const std::string& deviceId, bool mute) {
    if (deviceId.empty()) return false;
    
    try {
        IMMDeviceEnumerator* pEnumerator = nullptr;
        HRESULT hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator),
            (void**)&pEnumerator
        );
        
        if (FAILED(hr) || !pEnumerator) return false;
        
        // 将 UTF-8 字符串转换为宽字符串
        int wideSize = MultiByteToWideChar(CP_UTF8, 0, deviceId.c_str(), -1, nullptr, 0);
        if (wideSize <= 0) {
            pEnumerator->Release();
            return false;
        }
        std::vector<wchar_t> wideBuffer(wideSize);
        MultiByteToWideChar(CP_UTF8, 0, deviceId.c_str(), -1, wideBuffer.data(), wideSize);
        
        IMMDevice* pDevice = nullptr;
        hr = pEnumerator->GetDevice(wideBuffer.data(), &pDevice);
        
        pEnumerator->Release();
        
        if (FAILED(hr) || !pDevice) return false;
        
        IAudioEndpointVolume* pEndpointVolume = nullptr;
        hr = pDevice->Activate(
            __uuidof(IAudioEndpointVolume),
            CLSCTX_ALL,
            nullptr,
            (void**)&pEndpointVolume
        );
        
        pDevice->Release();
        
        if (FAILED(hr) || !pEndpointVolume) return false;
        
        hr = pEndpointVolume->SetMute(mute ? TRUE : FALSE, nullptr);
        pEndpointVolume->Release();
        
        return SUCCEEDED(hr);
    } catch (...) {
        return false;
    }
}

// 获取输入设备音量
bool AudioManager::GetInputDeviceVolume(const std::string& deviceId, float& volume) {
    if (deviceId.empty()) return false;
    
    try {
        IMMDeviceEnumerator* pEnumerator = nullptr;
        HRESULT hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator),
            (void**)&pEnumerator
        );
        
        if (FAILED(hr) || !pEnumerator) return false;
        
        // 将 UTF-8 字符串转换为宽字符串
        int wideSize = MultiByteToWideChar(CP_UTF8, 0, deviceId.c_str(), -1, nullptr, 0);
        if (wideSize <= 0) {
            pEnumerator->Release();
            return false;
        }
        std::vector<wchar_t> wideBuffer(wideSize);
        MultiByteToWideChar(CP_UTF8, 0, deviceId.c_str(), -1, wideBuffer.data(), wideSize);
        
        IMMDevice* pDevice = nullptr;
        hr = pEnumerator->GetDevice(wideBuffer.data(), &pDevice);
        
        pEnumerator->Release();
        
        if (FAILED(hr) || !pDevice) return false;
        
        IAudioEndpointVolume* pEndpointVolume = nullptr;
        hr = pDevice->Activate(
            __uuidof(IAudioEndpointVolume),
            CLSCTX_ALL,
            nullptr,
            (void**)&pEndpointVolume
        );
        
        pDevice->Release();
        
        if (FAILED(hr) || !pEndpointVolume) return false;
        
        hr = pEndpointVolume->GetMasterVolumeLevelScalar(&volume);
        pEndpointVolume->Release();
        
        return SUCCEEDED(hr);
    } catch (...) {
        return false;
    }
}

// 获取输入设备静音状态
bool AudioManager::GetInputDeviceMute(const std::string& deviceId, bool& mute) {
    if (deviceId.empty()) return false;
    
    try {
        IMMDeviceEnumerator* pEnumerator = nullptr;
        HRESULT hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator),
            (void**)&pEnumerator
        );
        
        if (FAILED(hr) || !pEnumerator) return false;
        
        // 将 UTF-8 字符串转换为宽字符串
        int wideSize = MultiByteToWideChar(CP_UTF8, 0, deviceId.c_str(), -1, nullptr, 0);
        if (wideSize <= 0) {
            pEnumerator->Release();
            return false;
        }
        std::vector<wchar_t> wideBuffer(wideSize);
        MultiByteToWideChar(CP_UTF8, 0, deviceId.c_str(), -1, wideBuffer.data(), wideSize);
        
        IMMDevice* pDevice = nullptr;
        hr = pEnumerator->GetDevice(wideBuffer.data(), &pDevice);
        
        pEnumerator->Release();
        
        if (FAILED(hr) || !pDevice) return false;
        
        IAudioEndpointVolume* pEndpointVolume = nullptr;
        hr = pDevice->Activate(
            __uuidof(IAudioEndpointVolume),
            CLSCTX_ALL,
            nullptr,
            (void**)&pEndpointVolume
        );
        
        pDevice->Release();
        
        if (FAILED(hr) || !pEndpointVolume) return false;
        
        BOOL muteValue = FALSE;
        hr = pEndpointVolume->GetMute(&muteValue);
        pEndpointVolume->Release();
        
        if (SUCCEEDED(hr)) {
            mute = (muteValue != FALSE);
            return true;
        }
        
        return false;
    } catch (...) {
        return false;
    }
}

// 系统模块管理器实现
SystemModuleManager::SystemModuleManager() :
    keyboard_manager_(nullptr),
    audio_manager_(nullptr),
    macro_recorder_(nullptr),
    macro_player_(nullptr),
    initialized_(false) {
}

SystemModuleManager::~SystemModuleManager() {
    Shutdown();
}

bool SystemModuleManager::Initialize() {
    if (initialized_) return true;

    try {
        keyboard_manager_ = new KeyboardManager();
        audio_manager_ = new AudioManager();
        macro_recorder_ = new MacroRecorder();
        macro_player_ = new MacroPlayer();

        initialized_ = true;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize SystemModuleManager: " << e.what() << std::endl;
        return false;
    }
}

void SystemModuleManager::Shutdown() {
    if (!initialized_) return;

    if (macro_player_) {
        macro_player_->StopPlaying();
        delete macro_player_;
        macro_player_ = nullptr;
    }

    if (macro_recorder_) {
        macro_recorder_->StopRecording();
        delete macro_recorder_;
        macro_recorder_ = nullptr;
    }

    if (keyboard_manager_) {
        delete keyboard_manager_;
        keyboard_manager_ = nullptr;
    }

    if (audio_manager_) {
        delete audio_manager_;
        audio_manager_ = nullptr;
    }

    initialized_ = false;
}

bool SystemModuleManager::SendKeyInput(const std::string& input) {
    if (!initialized_ || !keyboard_manager_) return false;

    // 处理热键
    if (input.find("%hotkey%") == 0) {
        return keyboard_manager_->SendKey(input, true);
    }

    // 处理粘贴操作
    if (input == "paste") {
        return keyboard_manager_->SendKeyCombination({VK_CONTROL, 'V'}, true);
    }

    // 处理普通文本输入
    return keyboard_manager_->SendKey(input, false);
}

bool SystemModuleManager::SetMediaState(const std::string& type, float volume, int mute) {
    if (!initialized_ || !audio_manager_) return false;

    bool result = true;
    bool hasVolume = (volume >= 0.0f && volume <= 1.0f);
    bool hasMute = (mute != -1);

    if (type == "speaker") {
        if (hasVolume) {
            result = audio_manager_->SetSpeakerVolume(volume) && result;
        }
        if (hasMute) {
            result = audio_manager_->SetSpeakerMute(mute != 0) && result;
        }
    } else if (type == "mic" || type == "microphone") {
        if (hasVolume) {
            result = audio_manager_->SetMicrophoneVolume(volume) && result;
        }
        if (hasMute) {
            result = audio_manager_->SetMicrophoneMute(mute != 0) && result;
        }
    }

    return result;
}

bool SystemModuleManager::GetMediaState(const std::string& type, float& volume, bool& mute) {
    if (!initialized_ || !audio_manager_) return false;

    if (type == "speaker") {
        return audio_manager_->GetSpeakerVolume(volume) &&
               audio_manager_->GetSpeakerMute(mute);
    } else if (type == "mic" || type == "microphone") {
        return audio_manager_->GetMicrophoneVolume(volume) &&
               audio_manager_->GetMicrophoneMute(mute);
    }

    return false;
}

std::vector<OutputDeviceInfo> SystemModuleManager::GetAllOutputDevices() {
    if (!initialized_ || !audio_manager_) return {};
    return audio_manager_->GetAllOutputDevices();
}

bool SystemModuleManager::SetOutputDeviceVolume(const std::string& deviceId, float volume) {
    if (!initialized_ || !audio_manager_) return false;
    return audio_manager_->SetOutputDeviceVolume(deviceId, volume);
}

bool SystemModuleManager::SetOutputDeviceMute(const std::string& deviceId, bool mute) {
    if (!initialized_ || !audio_manager_) return false;
    return audio_manager_->SetOutputDeviceMute(deviceId, mute);
}

bool SystemModuleManager::GetOutputDeviceVolume(const std::string& deviceId, float& volume) {
    if (!initialized_ || !audio_manager_) return false;
    return audio_manager_->GetOutputDeviceVolume(deviceId, volume);
}

bool SystemModuleManager::GetOutputDeviceMute(const std::string& deviceId, bool& mute) {
    if (!initialized_ || !audio_manager_) return false;
    return audio_manager_->GetOutputDeviceMute(deviceId, mute);
}

std::vector<InputDeviceInfo> SystemModuleManager::GetAllInputDevices() {
    if (!initialized_ || !audio_manager_) return {};
    return audio_manager_->GetAllInputDevices();
}

bool SystemModuleManager::SetInputDeviceVolume(const std::string& deviceId, float volume) {
    if (!initialized_ || !audio_manager_) return false;
    return audio_manager_->SetInputDeviceVolume(deviceId, volume);
}

bool SystemModuleManager::SetInputDeviceMute(const std::string& deviceId, bool mute) {
    if (!initialized_ || !audio_manager_) return false;
    return audio_manager_->SetInputDeviceMute(deviceId, mute);
}

bool SystemModuleManager::GetInputDeviceVolume(const std::string& deviceId, float& volume) {
    if (!initialized_ || !audio_manager_) return false;
    return audio_manager_->GetInputDeviceVolume(deviceId, volume);
}

bool SystemModuleManager::GetInputDeviceMute(const std::string& deviceId, bool& mute) {
    if (!initialized_ || !audio_manager_) return false;
    return audio_manager_->GetInputDeviceMute(deviceId, mute);
}

std::vector<AudioSessionInfo> SystemModuleManager::GetAllAudioSessions() {
    if (!initialized_ || !audio_manager_) return {};

    return audio_manager_->GetAllAudioSessions();
}

bool SystemModuleManager::SetAudioSessionVolume(const std::string& sessionId, float volume) {
    if (!initialized_ || !audio_manager_) return false;

    return audio_manager_->SetAudioSessionVolume(sessionId, volume);
}

float SystemModuleManager::GetAudioSessionVolume(const std::string& sessionId) {
    if (!initialized_ || !audio_manager_) return -1.0f;

    return audio_manager_->GetAudioSessionVolume(sessionId);
}

std::string SystemModuleManager::GetIcon(const std::string& filePath, int iconSize, bool bgr, bool alpha) {
    if (!initialized_ || !audio_manager_) return "";

    return audio_manager_->GetIconBase64(filePath, iconSize, bgr, alpha);
}

// 宏录制功能实现
bool SystemModuleManager::StartMacroRecording(const std::string& macroId, bool blockInput) {
    if (!initialized_ || !macro_recorder_) return false;
    
    return macro_recorder_->StartRecording(macroId, blockInput);
}

void SystemModuleManager::SetMacroStatusCallback(std::function<void(const MacroEvent*, const MacroStatusEvent*)> callback) {
    if (initialized_ && macro_recorder_) {
        macro_recorder_->SetStatusCallback(callback);
    }
    if (initialized_ && macro_player_) {
        macro_player_->SetStatusCallback(callback);
    }
}

bool SystemModuleManager::StopMacroRecording() {
    if (!initialized_ || !macro_recorder_) return false;
    
    return macro_recorder_->StopRecording();
}

bool SystemModuleManager::IsRecording() const {
    if (!initialized_ || !macro_recorder_) return false;
    
    return macro_recorder_->IsRecording();
}

std::vector<MacroEvent> SystemModuleManager::GetMacroEvents() const {
    if (!initialized_ || !macro_recorder_) return {};
    
    return macro_recorder_->GetEvents();
}

double SystemModuleManager::GetRecordingDuration() const {
    if (!initialized_ || !macro_recorder_) return 0.0;
    
    return macro_recorder_->GetDuration();
}

// 宏播放功能实现
bool SystemModuleManager::StartMacroPlayback(const std::vector<MacroEvent>& events, const std::string& macroId, double speedFactor, int loopCount, int smoothingFactor) {
    if (!initialized_ || !macro_player_) return false;
    
    macro_player_->SetEvents(events);
    return macro_player_->StartPlaying(macroId, speedFactor, loopCount, smoothingFactor);
}

void SystemModuleManager::StopMacroPlayback() {
    if (!initialized_ || !macro_player_) return;
    
    macro_player_->StopPlaying();
}

bool SystemModuleManager::IsPlaying() const {
    if (!initialized_ || !macro_player_) return false;
    
    return macro_player_->IsPlaying();
}

// 全局管理器实例定义
SystemModuleManager* g_system_manager = nullptr;

// MacroRecorder 静态成员定义
MacroRecorder* MacroRecorder::instance = nullptr;

// 虚拟键码到名称的映射表
static std::map<BYTE, std::string> vkToName = {
    {0x01, "LeftMouseButton"}, {0x02, "RightMouseButton"}, {0x03, "Cancel"}, {0x04, "MiddleMouseButton"},
    {0x05, "X1MouseButton"}, {0x06, "X2MouseButton"}, {0x08, "Backspace"}, {0x09, "Tab"},
    {0x0C, "Clear"}, {0x0D, "Enter"}, {0x10, "Shift"}, {0x11, "Ctrl"},
    {0x12, "Alt"}, {0x13, "Pause"}, {0x14, "Caps Lock"}, {0x15, "Kana"},
    {0x16, "Ime On"}, {0x17, "Ime Junja"}, {0x18, "Ime Final"}, {0x19, "Ime Hanja"},
    {0x1A, "Ime Off"}, {0x1B, "Esc"}, {0x1C, "Ime Convert"}, {0x1D, "Ime Nonconvert"},
    {0x1E, "Ime Accept"}, {0x1F, "Ime Mode Change"}, {0x20, "Space"}, {0x21, "Page Up"},
    {0x22, "Page Down"}, {0x23, "End"}, {0x24, "Home"}, {0x25, "Left Arrow"},
    {0x26, "Up Arrow"}, {0x27, "Right Arrow"}, {0x28, "Down Arrow"}, {0x29, "Select"},
    {0x2A, "Print"}, {0x2B, "Execute"}, {0x2C, "Print Screen"}, {0x2D, "Insert"},
    {0x2E, "Delete"}, {0x2F, "Help"}, {0x30, "0"}, {0x31, "1"},
    {0x32, "2"}, {0x33, "3"}, {0x34, "4"}, {0x35, "5"},
    {0x36, "6"}, {0x37, "7"}, {0x38, "8"}, {0x39, "9"},
    {0x41, "A"}, {0x42, "B"}, {0x43, "C"}, {0x44, "D"},
    {0x45, "E"}, {0x46, "F"}, {0x47, "G"}, {0x48, "H"},
    {0x49, "I"}, {0x4A, "J"}, {0x4B, "K"}, {0x4C, "L"},
    {0x4D, "M"}, {0x4E, "N"}, {0x4F, "O"}, {0x50, "P"},
    {0x51, "Q"}, {0x52, "R"}, {0x53, "S"}, {0x54, "T"},
    {0x55, "U"}, {0x56, "V"}, {0x57, "W"}, {0x58, "X"},
    {0x59, "Y"}, {0x5A, "Z"}, {0x5B, "Left Windows"}, {0x5C, "Right Windows"},
    {0x5D, "Applications"}, {0x5F, "Sleep"}, {0x60, "Numpad 0"}, {0x61, "Numpad 1"},
    {0x62, "Numpad 2"}, {0x63, "Numpad 3"}, {0x64, "Numpad 4"}, {0x65, "Numpad 5"},
    {0x66, "Numpad 6"}, {0x67, "Numpad 7"}, {0x68, "Numpad 8"}, {0x69, "Numpad 9"},
    {0x6A, "*"}, {0x6B, "+"}, {0x6C, "Separator"}, {0x6D, "-"},
    {0x6E, "."}, {0x6F, "/"}, {0x70, "F1"}, {0x71, "F2"},
    {0x72, "F3"}, {0x73, "F4"}, {0x74, "F5"}, {0x75, "F6"},
    {0x76, "F7"}, {0x77, "F8"}, {0x78, "F9"}, {0x79, "F10"},
    {0x7A, "F11"}, {0x7B, "F12"}, {0x7C, "F13"}, {0x7D, "F14"},
    {0x7E, "F15"}, {0x7F, "F16"}, {0x80, "F17"}, {0x81, "F18"},
    {0x82, "F19"}, {0x83, "F20"}, {0x84, "F21"}, {0x85, "F22"},
    {0x86, "F23"}, {0x87, "F24"}, {0x90, "Num Lock"}, {0x91, "Scroll Lock"},
    {0xA0, "Left Shift"}, {0xA1, "Right Shift"}, {0xA2, "Left Ctrl"}, {0xA3, "Right Ctrl"},
    {0xA4, "Left Alt"}, {0xA5, "Right Alt"}, {0xA6, "Browser Back"}, {0xA7, "Browser Forward"},
    {0xA8, "Browser Refresh"}, {0xA9, "Browser Stop"}, {0xAA, "Browser Search"}, {0xAB, "Browser Favorites"},
    {0xAC, "Browser Home"}, {0xAD, "Volume Mute"}, {0xAE, "Volume Down"}, {0xAF, "Volume Up"},
    {0xB0, "Next Track"}, {0xB1, "Previous Track"}, {0xB2, "Stop Media"}, {0xB3, "Play Pause Media"},
    {0xB4, "Start Mail"}, {0xB5, "Select Media"}, {0xB6, "Start Application 1"}, {0xB7, "Start Application 2"},
    {0xBA, ";"}, {0xBB, "+"}, {0xBC, ","}, {0xBD, "-"},
    {0xBE, "."}, {0xBF, "/"}, {0xC0, "`"}, {0xDB, "["},
    {0xDC, "\\"}, {0xDD, "]"}, {0xDE, "'"}, {0xDF, "OEM 8"},
    {0xE2, "<"}, {0xE5, "Ime Process"}, {0xF6, "Attn"}, {0xF7, "CrSel"},
    {0xF8, "ExSel"}, {0xF9, "Erase EOF"}, {0xFA, "Play"}, {0xFB, "Zoom"},
    {0xFD, "PA1"}, {0xFE, "OEM Clear"}
};

// MacroRecorder 实现
MacroRecorder::MacroRecorder() : 
    isRecording(false),
    startTime(0),
    lastEventTime(0),
    endTime(0),
    currentMacroId(""),
    keyboardHook(nullptr),
    mouseHook(nullptr),
    hookThread(nullptr),
    hookThreadId(0),
    blockInputToSystem(false),
    hooksInstalled(false),
    threadRunning(false) {
    currentButtons[0] = currentButtons[1] = currentButtons[2] = 0;
    lastPos[0] = lastPos[1] = 0;
}

MacroRecorder::~MacroRecorder() {
    if (isRecording) {
        StopRecording();
    }
}

bool MacroRecorder::StartRecording(const std::string& macroId, bool blockInput) {
    if (isRecording) return false;
    
    currentMacroId = macroId.empty() ? "0" : macroId;
    blockInputToSystem = blockInput;
    
    // 清空之前的录制
    events.clear();
    currentKeys.clear();
    currentButtons[0] = currentButtons[1] = currentButtons[2] = 0;
    
    // 获取当前鼠标位置
    POINT pt;
    GetCursorPos(&pt);
    lastPos[0] = pt.x;
    lastPos[1] = pt.y;
    
    // 设置实例指针
    instance = this;
    
    // 启动钩子线程并在其中安装钩子与消息循环
    hooksInstalled = false;
    threadRunning = true;
    hookThread = new std::thread(&MacroRecorder::HookThreadFunc, this);

    // 等待钩子安装（最多500ms）
    for (int i = 0; i < 50 && !hooksInstalled; ++i) {
        Sleep(10);
    }
    if (!hooksInstalled) {
        // 未能安装成功，尝试停止线程
        threadRunning = false;
        if (hookThread && hookThread->joinable()) hookThread->join();
        delete hookThread; hookThread = nullptr;
        instance = nullptr;
        return false;
    }
    
    // 设置开始时间
    startTime = GetTickCount();
    lastEventTime = startTime;
    isRecording = true;
    
    // 发送 "record started" 状态事件
    if (statusCallback) {
        try {
            MacroStatusEvent statusEvent("record started", currentMacroId);
            statusCallback(nullptr, &statusEvent);
        } catch (...) {
            // 忽略回调中的异常
        }
    }
    
    return true;
}

bool MacroRecorder::StopRecording() {
    if (!isRecording) return false;
    
    isRecording = false;
    blockInputToSystem = false;
    endTime = GetTickCount();
    
    // 通知钩子线程退出消息循环并卸载钩子
    if (hookThreadId != 0) {
        PostThreadMessage(hookThreadId, WM_QUIT, 0, 0);
    }
    threadRunning = false;
    if (hookThread) {
        if (hookThread->joinable()) hookThread->join();
        delete hookThread;
        hookThread = nullptr;
    }
    
    instance = nullptr;
    
    // 发送 "record stopped" 状态事件
    if (statusCallback) {
        try {
            MacroStatusEvent statusEvent("record stopped", currentMacroId);
            statusCallback(nullptr, &statusEvent);
        } catch (...) {
            // 忽略回调中的异常
        }
    }
    
    return true;
}

double MacroRecorder::GetDuration() const {
    if (isRecording) {
        return (GetTickCount() - startTime) / 1000.0;
    } else if (endTime > 0) {
        return (endTime - startTime) / 1000.0;
    }
    return 0.0;
}

double MacroRecorder::GetScalingFactor() {
    // 获取DPI缩放因子
    HDC hdc = GetDC(nullptr);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(nullptr, hdc);
    return dpi / 96.0;  // 96 DPI is the default
}

void MacroRecorder::LogEvent(const std::string& evt, int x, int y, void* data) {
    if (!isRecording) return;
    
    DWORD currentTime = GetTickCount();
    DWORD dt = currentTime - lastEventTime;
    lastEventTime = currentTime;
    
    MacroEvent event;
    event.dt = dt;
    event.evt = evt;
    event.pos[0] = x;
    event.pos[1] = y;

    if (evt == "mouse-btn-changed" && data) {
        int* buttons = (int*)data;
        event.data.button[0] = buttons[0];
        event.data.button[1] = buttons[1];
        event.data.button[2] = buttons[2];
    } else if (evt == "mouse-wheel" && data) {
        int* wheel = (int*)data;
        event.data.dx = wheel[0];
        event.data.dy = wheel[1];
    } else if (evt == "key-changed" && data) {
        std::vector<BYTE>* keys = (std::vector<BYTE>*)data;
        event.data.keys = *keys;
        
        // 生成 key_name
        std::string keyName;
        for (size_t i = 0; i < keys->size(); ++i) {
            BYTE vk = (*keys)[i];
            auto it = vkToName.find(vk);
            if (it != vkToName.end()) {
                if (!keyName.empty()) keyName += " + ";
                keyName += it->second;
            } else {
                if (!keyName.empty()) keyName += " + ";
                char buf[16];
                snprintf(buf, sizeof(buf), "VK_%02X", vk);
                keyName += buf;
            }
        }
        event.data.key_name = keyName;
    }
    
    events.push_back(event);
    
    // 触发事件回调
    if (statusCallback) {
        try {
            statusCallback(&event, nullptr);
        } catch (...) {
            // 忽略回调中的异常
        }
    }
}


// 键盘钩子回调
LRESULT CALLBACK MacroRecorder::KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    // Safety check: verify instance is valid and hooks are properly installed
    if (!instance || !instance->hooksInstalled.load() || !instance->threadRunning.load()) {
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }
    
    bool shouldBlock = false;
    if (nCode >= 0 && instance->isRecording) {
        try {
            KBDLLHOOKSTRUCT* kbStruct = (KBDLLHOOKSTRUCT*)lParam;
            BYTE vkCode = (BYTE)kbStruct->vkCode;
            
            bool keyChanged = false;
            
            if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
                // 按键按下
                auto it = std::find(instance->currentKeys.begin(), instance->currentKeys.end(), vkCode);
                if (it == instance->currentKeys.end()) {
                    instance->currentKeys.push_back(vkCode);
                    keyChanged = true;
                }
            } else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
                // 按键释放
                auto it = std::find(instance->currentKeys.begin(), instance->currentKeys.end(), vkCode);
                if (it != instance->currentKeys.end()) {
                    instance->currentKeys.erase(it);
                    keyChanged = true;
                }
            }
            
            if (keyChanged) {
                instance->LogEvent("key-changed", instance->lastPos[0], instance->lastPos[1], &instance->currentKeys);
            }
            if (instance->blockInputToSystem &&
                (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN || wParam == WM_KEYUP || wParam == WM_SYSKEYUP)) {
                shouldBlock = true;
            }
        } catch (...) {
            // Catch any exceptions to prevent crash in hook callback
        }
    }
    if (shouldBlock) {
        return 1;
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

void MacroRecorder::HookThreadFunc() {
    // 记录线程ID
    hookThreadId = GetCurrentThreadId();

    // 安装低级钩子
    keyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandle(nullptr), 0);
    mouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseProc, GetModuleHandle(nullptr), 0);

    if (keyboardHook && mouseHook) {
        hooksInstalled = true;
    } else {
        hooksInstalled = false;
    }

    // 消息循环
    MSG msg;
    while (threadRunning && GetMessage(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // 卸载钩子
    if (keyboardHook) {
        UnhookWindowsHookEx(keyboardHook);
        keyboardHook = nullptr;
    }
    if (mouseHook) {
        UnhookWindowsHookEx(mouseHook);
        mouseHook = nullptr;
    }

    hookThreadId = 0;
}

// 鼠标钩子回调
LRESULT CALLBACK MacroRecorder::MouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    // Safety check: verify instance is valid and hooks are properly installed
    if (!instance || !instance->hooksInstalled.load() || !instance->threadRunning.load()) {
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }
    
    if (nCode >= 0 && instance->isRecording) {
        try {
            MSLLHOOKSTRUCT* mouseStruct = (MSLLHOOKSTRUCT*)lParam;
            int x = mouseStruct->pt.x;
            int y = mouseStruct->pt.y;
            
            switch (wParam) {
                case WM_MOUSEMOVE:
                    instance->lastPos[0] = x;
                    instance->lastPos[1] = y;
                    instance->LogEvent("mouse-move", x, y, nullptr);
                    break;
                    
                case WM_LBUTTONDOWN:
                    instance->currentButtons[0] = 1;
                    instance->lastPos[0] = x;
                    instance->lastPos[1] = y;
                    instance->LogEvent("mouse-btn-changed", x, y, instance->currentButtons);
                    break;
                    
                case WM_LBUTTONUP:
                    instance->currentButtons[0] = 0;
                    instance->lastPos[0] = x;
                    instance->lastPos[1] = y;
                    instance->LogEvent("mouse-btn-changed", x, y, instance->currentButtons);
                    break;
                    
                case WM_RBUTTONDOWN:
                    instance->currentButtons[2] = 1;
                    instance->lastPos[0] = x;
                    instance->lastPos[1] = y;
                    instance->LogEvent("mouse-btn-changed", x, y, instance->currentButtons);
                    break;
                    
                case WM_RBUTTONUP:
                    instance->currentButtons[2] = 0;
                    instance->lastPos[0] = x;
                    instance->lastPos[1] = y;
                    instance->LogEvent("mouse-btn-changed", x, y, instance->currentButtons);
                    break;
                    
                case WM_MBUTTONDOWN:
                    instance->currentButtons[1] = 1;
                    instance->lastPos[0] = x;
                    instance->lastPos[1] = y;
                    instance->LogEvent("mouse-btn-changed", x, y, instance->currentButtons);
                    break;
                    
                case WM_MBUTTONUP:
                    instance->currentButtons[1] = 0;
                    instance->lastPos[0] = x;
                    instance->lastPos[1] = y;
                    instance->LogEvent("mouse-btn-changed", x, y, instance->currentButtons);
                    break;
                    
                case WM_MOUSEWHEEL:
                    {
                        short delta = GET_WHEEL_DELTA_WPARAM(mouseStruct->mouseData);
                        int wheel[2] = { 0, delta / WHEEL_DELTA };
                        instance->LogEvent("mouse-wheel", x, y, wheel);
                    }
                    break;
                    
                case WM_MOUSEHWHEEL:
                    {
                        short delta = GET_WHEEL_DELTA_WPARAM(mouseStruct->mouseData);
                        int wheel[2] = { delta / WHEEL_DELTA, 0 };
                        instance->LogEvent("mouse-wheel", x, y, wheel);
                    }
                    break;
            }
        } catch (...) {
            // Catch any exceptions to prevent crash in hook callback
        }
    }
    
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

// MacroPlayer 实现
MacroPlayer::MacroPlayer() :
    isPlaying(false),
    shouldStop(false),
    speedFactor(1.0),
    loopCount(1),
    smoothingFactor(1),
    currentMacroId(""),
    playbackThread(nullptr) {
}

MacroPlayer::~MacroPlayer() {
    StopPlaying();
}

void MacroPlayer::SetEvents(const std::vector<MacroEvent>& evts) {
    events = evts;
}

bool MacroPlayer::StartPlaying(const std::string& macroId, double speed, int loops, int smoothing) {
    if (isPlaying || events.empty()) return false;
    
    currentMacroId = macroId.empty() ? "0" : macroId;
    speedFactor = speed > 0 ? speed : 1.0;
    // 保留 0 表示无限循环；负数按 1 次处理
    loopCount = (loops < 0) ? 1 : loops;
    smoothingFactor = smoothing > 0 ? smoothing : 1;
    
    shouldStop = false;
    isPlaying = true;
    
    // 发送 "play started" 状态事件
    if (statusCallback) {
        try {
            MacroStatusEvent statusEvent("play started", currentMacroId);
            statusCallback(nullptr, &statusEvent);
        } catch (...) {
            // 忽略回调中的异常
        }
    }
    
    // 创建播放线程
    playbackThread = new std::thread(&MacroPlayer::PlaybackThreadFunc, this);
    
    return true;
}

void MacroPlayer::StopPlaying() {
    if (!isPlaying) return;
    
    shouldStop = true;
    
    if (playbackThread) {
        if (playbackThread->joinable()) {
            playbackThread->join();
        }
        delete playbackThread;
        playbackThread = nullptr;
    }
    
    // 释放所有按键
    for (BYTE key : pressedKeys) {
        INPUT input = { 0 };
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = key;
        input.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, &input, sizeof(INPUT));
    }
    pressedKeys.clear();
    
    isPlaying = false;
    
    // 发送 "play stopped" 状态事件（手动停止）
    if (statusCallback) {
        try {
            MacroStatusEvent statusEvent("play stopped", currentMacroId);
            statusCallback(nullptr, &statusEvent);
        } catch (...) {
            // 忽略回调中的异常
        }
    }
}

double MacroPlayer::GetScalingFactor() {
    HDC hdc = GetDC(nullptr);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(nullptr, hdc);
    return dpi / 96.0;
}

std::vector<std::pair<int, int>> MacroPlayer::InterpolatePoints(int x1, int y1, int x2, int y2, int numPoints) {
    std::vector<std::pair<int, int>> points;
    
    if (numPoints <= 1) {
        points.push_back(std::make_pair(x2, y2));
        return points;
    }
    
    for (int i = 1; i <= numPoints; ++i) {
        int x = x1 + (x2 - x1) * i / numPoints;
        int y = y1 + (y2 - y1) * i / numPoints;
        points.push_back(std::make_pair(x, y));
    }
    
    return points;
}

void MacroPlayer::PlaybackThreadFunc() {
    // 提升计时精度，便于高速播放（1ms 分辨率）
    timeBeginPeriod(1);
    // 获取缩放因子 - 使用与Python相同的方法
    typedef HRESULT(WINAPI* GetScaleFactorForDeviceProc)(UINT);
    HMODULE hShcore = LoadLibraryA("Shcore.dll");
    double scalingFactor = 1.0;
    
    if (hShcore) {
        GetScaleFactorForDeviceProc GetScaleFactorForDevice = 
            (GetScaleFactorForDeviceProc)GetProcAddress(hShcore, "GetScaleFactorForDevice");
        if (GetScaleFactorForDevice) {
            UINT scaleFactor = GetScaleFactorForDevice(0);  // 返回值是百分比，如125表示125%
            scalingFactor = scaleFactor / 100.0;
        }
        FreeLibrary(hShcore);
    }
    
    // 如果获取失败，使用备用方法
    if (scalingFactor == 1.0) {
        HDC hdc = GetDC(nullptr);
        int dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(nullptr, hdc);
        scalingFactor = dpiX / 96.0;
    }

    // 在 Per-Monitor DPI Aware 进程（如 Electron）中，事件坐标与 SetCursorPos 使用的坐标已经一致，
    // 不应再做缩放，否则会出现偏移。这里动态检测进程 DPI 感知级别并在需要时禁用缩放。
    {
        HMODULE hShcoreAwareness = LoadLibraryA("Shcore.dll");
        if (hShcoreAwareness) {
            // 定义最小化的枚举，避免包含额外头文件
            enum PROCESS_DPI_AWARENESS_LOCAL { PROCESS_DPI_UNAWARE_L = 0, PROCESS_SYSTEM_DPI_AWARE_L = 1, PROCESS_PER_MONITOR_DPI_AWARE_L = 2 };
            typedef HRESULT (WINAPI* GetProcessDpiAwarenessProc)(HANDLE, PROCESS_DPI_AWARENESS_LOCAL*);
            GetProcessDpiAwarenessProc GetProcessDpiAwarenessDyn = (GetProcessDpiAwarenessProc)GetProcAddress(hShcoreAwareness, "GetProcessDpiAwareness");
            if (GetProcessDpiAwarenessDyn) {
                PROCESS_DPI_AWARENESS_LOCAL awareness = PROCESS_DPI_UNAWARE_L;
                if (SUCCEEDED(GetProcessDpiAwarenessDyn(GetCurrentProcess(), &awareness))) {
                    if (awareness == PROCESS_PER_MONITOR_DPI_AWARE_L) {
                        // 在每显示器 DPI 感知下，不做缩放
                        scalingFactor = 1.0;
                    }
                }
            }
            FreeLibrary(hShcoreAwareness);
        }
    }
    
    for (int loop = 0; (loopCount == 0 || loop < loopCount) && !shouldStop; ++loop) {
        int lastX = 0, lastY = 0;
        int lastButtons[3] = {0, 0, 0};  // 跟踪上一次的鼠标按键状态
        
        for (size_t i = 0; i < events.size() && !shouldStop; ++i) {
            const MacroEvent& event = events[i];
            
            // 预先计算该事件的休眠时长（单位：ms），用于不同分支分别消耗
            DWORD sleepTime = (event.dt > 0)
                ? static_cast<DWORD>(event.dt * speedFactor)
                : 0;
            
            // 处理不同类型的事件
            if (event.evt == "mouse-move") {
                // 鼠标移动 - 根据检测到的 DPI 感知按需应用缩放
                int targetX = static_cast<int>(event.pos[0] / scalingFactor);
                int targetY = static_cast<int>(event.pos[1] / scalingFactor);

                if (smoothingFactor > 1 && i > 0) {
                    // 将本事件的总等待时间按插值点均分，避免重复等待导致播放变慢
                    const int numPoints = smoothingFactor;
                    DWORD perStep = (numPoints > 0) ? (sleepTime / (numPoints + 1)) : 0;
                    auto points = InterpolatePoints(lastX, lastY, targetX, targetY, numPoints);
                    for (const auto& pt : points) {
                        if (perStep > 0) Sleep(perStep);
                        SetCursorPos(pt.first, pt.second);
                    }
                    // 最后到达目标点，不再额外 Sleep
                    SetCursorPos(targetX, targetY);
                } else {
                    // 无平滑或首个事件：先整体等待，再到达目标位置
                    if (sleepTime > 0) Sleep(sleepTime);
                    SetCursorPos(targetX, targetY);
                }

                lastX = targetX;
                lastY = targetY;
                
            } else if (event.evt == "mouse-btn-changed") {
                // 鼠标按钮状态改变 - 根据检测到的 DPI 感知按需应用缩放
                int targetX = static_cast<int>(event.pos[0] / scalingFactor);
                int targetY = static_cast<int>(event.pos[1] / scalingFactor);
                // 先等待本事件应有的时间，再执行点击
                if (sleepTime > 0) Sleep(sleepTime);
                SetCursorPos(targetX, targetY);
                
                // 左键 - 只在状态真正改变时发送事件
                if (event.data.button[0] != lastButtons[0]) {
                    INPUT input = { 0 };
                    input.type = INPUT_MOUSE;
                    input.mi.dwFlags = (event.data.button[0] == 1) ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
                    SendInput(1, &input, sizeof(INPUT));
                    lastButtons[0] = event.data.button[0];
                }
                
                // 中键 - 只在状态真正改变时发送事件
                if (event.data.button[1] != lastButtons[1]) {
                    INPUT input = { 0 };
                    input.type = INPUT_MOUSE;
                    input.mi.dwFlags = (event.data.button[1] == 1) ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
                    SendInput(1, &input, sizeof(INPUT));
                    lastButtons[1] = event.data.button[1];
                }
                
                // 右键 - 只在状态真正改变时发送事件
                if (event.data.button[2] != lastButtons[2]) {
                    INPUT input = { 0 };
                    input.type = INPUT_MOUSE;
                    input.mi.dwFlags = (event.data.button[2] == 1) ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
                    SendInput(1, &input, sizeof(INPUT));
                    lastButtons[2] = event.data.button[2];
                }
                
                lastX = targetX;
                lastY = targetY;
                
            } else if (event.evt == "mouse-wheel") {
                // 鼠标滚轮
                if (sleepTime > 0) Sleep(sleepTime);
                INPUT input = { 0 };
                input.type = INPUT_MOUSE;
                
                if (event.data.dy != 0) {
                    input.mi.dwFlags = MOUSEEVENTF_WHEEL;
                    input.mi.mouseData = event.data.dy * WHEEL_DELTA;
                    SendInput(1, &input, sizeof(INPUT));
                }
                
                if (event.data.dx != 0) {
                    input.mi.dwFlags = MOUSEEVENTF_HWHEEL;
                    input.mi.mouseData = event.data.dx * WHEEL_DELTA;
                    SendInput(1, &input, sizeof(INPUT));
                }
                
            } else if (event.evt == "key-changed") {
                // 键盘状态改变 - 根据检测到的 DPI 感知按需应用缩放
                int targetX = static_cast<int>(event.pos[0] / scalingFactor);
                int targetY = static_cast<int>(event.pos[1] / scalingFactor);
                if (sleepTime > 0) Sleep(sleepTime);
                SetCursorPos(targetX, targetY);
                
                // 计算需要释放和按下的键
                std::set<BYTE> newKeys(event.data.keys.begin(), event.data.keys.end());
                
                // 释放不在新状态中的键
                for (BYTE key : pressedKeys) {
                    if (newKeys.find(key) == newKeys.end()) {
                        INPUT input = { 0 };
                        input.type = INPUT_KEYBOARD;
                        input.ki.wVk = key;
                        input.ki.dwFlags = KEYEVENTF_KEYUP;
                        SendInput(1, &input, sizeof(INPUT));
                    }
                }
                
                // 按下新状态中的键
                for (BYTE key : newKeys) {
                    if (pressedKeys.find(key) == pressedKeys.end()) {
                        INPUT input = { 0 };
                        input.type = INPUT_KEYBOARD;
                        input.ki.wVk = key;
                        input.ki.dwFlags = 0;
                        SendInput(1, &input, sizeof(INPUT));
                    }
                }
                
                pressedKeys = newKeys;
                lastX = targetX;
                lastY = targetY;
            }
        }
        
        // 每次循环结束后，释放所有按下的键和鼠标按钮
        for (BYTE key : pressedKeys) {
            INPUT input = { 0 };
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = key;
            input.ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(1, &input, sizeof(INPUT));
        }
        pressedKeys.clear();
    }
    
    // 播放完成，确保所有按键都已释放
    for (BYTE key : pressedKeys) {
        INPUT input = { 0 };
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = key;
        input.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, &input, sizeof(INPUT));
    }
    pressedKeys.clear();
    
    isPlaying = false;
    
    // 发送 "play stopped" 状态事件（自动播放完成）
    // 注意：只有在非手动停止的情况下才发送（shouldStop 为 false）
    if (!shouldStop && statusCallback) {
        try {
            MacroStatusEvent statusEvent("play stopped", currentMacroId);
            statusCallback(nullptr, &statusEvent);
        } catch (...) {
            // 忽略回调中的异常
        }
    }
    
    // 还原计时器分辨率
    timeEndPeriod(1);
}
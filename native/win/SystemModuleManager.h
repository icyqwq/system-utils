#pragma once

#include <windows.h>
#include <string>
#include <functional>
#include <vector>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <audiosessiontypes.h>
#include <audiopolicy.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <gdiplus.h>
#include <objbase.h>
#include <thread>
#include <set>
#include <shellscalingapi.h>
#include <atomic>

// 前置声明
struct IMMDevice;
struct IAudioEndpointVolume;
struct IAudioSessionManager;
struct IAudioSessionEnumerator;
struct IAudioSessionControl;
struct IAudioSessionControl2;
struct ISimpleAudioVolume;
class MacroRecorder;
class MacroPlayer;
struct MacroEvent;

// 音频会话信息结构
struct AudioSessionInfo {
    std::string name;
    std::string id;
    std::string path;
    std::string icon; // base64编码的图标
    float volume; // 0.0 - 1.0
};

// 输出设备信息结构
struct OutputDeviceInfo {
    std::string id;
    std::string name;
    float volume; // 0.0 - 1.0
    bool mute;
};

// 输入设备信息结构
struct InputDeviceInfo {
    std::string id;
    std::string name;
    float volume; // 0.0 - 1.0
    bool mute;
};

// 输入事件相关结构
struct KeyInfo {
    std::string key_name;
    WORD vk_code;
    bool is_pressed;
};

struct MouseInfo {
    int x;
    int y;
    int button_state; // 位掩码：0=左键，1=中键，2=右键
};

struct InputEvent {
    DWORD timestamp;
    std::string event_type; // "key", "mouse", "macro"
    KeyInfo key_info;
    MouseInfo mouse_info;
};

// 键盘管理器类
class KeyboardManager {
public:
    KeyboardManager();
    ~KeyboardManager();

    // 发送键盘输入
    bool SendKey(const std::string& key_sequence, bool is_hotkey = false);
    bool SendKeyCombination(const std::vector<WORD>& keys, bool press = true);

    // 键盘状态查询
    bool IsKeyPressed(WORD vk_code);

private:
    // 键盘输入辅助函数
    void SendKeyDown(WORD vk_code);
    void SendKeyUp(WORD vk_code);
    void SendKeyPress(WORD vk_code);

    // 字符串转虚拟键码
    WORD StringToVkCode(const std::string& key_str);
    std::string VkCodeToString(WORD vk_code);

    // 特殊键处理
    bool IsSpecialKey(const std::string& key);
    std::vector<INPUT> CreateKeyInputs(const std::string& key_sequence, bool press = true);
    std::vector<INPUT> CreateKeyInputsLegacy(const std::string& key_sequence, bool press = true);
};

// 音频管理器类
class AudioManager {
public:
    AudioManager();
    ~AudioManager();

    // 扬声器控制
    bool SetSpeakerVolume(float volume); // 0.0 - 1.0
    bool SetSpeakerMute(bool mute);
    bool GetSpeakerVolume(float& volume);
    bool GetSpeakerMute(bool& mute);

    // 麦克风控制
    bool SetMicrophoneVolume(float volume); // 0.0 - 1.0
    bool SetMicrophoneMute(bool mute);
    bool GetMicrophoneVolume(float& volume);
    bool GetMicrophoneMute(bool& mute);

    // 音频会话管理
    std::vector<AudioSessionInfo> GetAllAudioSessions();
    bool SetAudioSessionVolume(const std::string& sessionId, float volume);
    float GetAudioSessionVolume(const std::string& sessionId);

    // 输出设备管理
    std::vector<OutputDeviceInfo> GetAllOutputDevices();
    bool SetOutputDeviceVolume(const std::string& deviceId, float volume);
    bool SetOutputDeviceMute(const std::string& deviceId, bool mute);
    bool GetOutputDeviceVolume(const std::string& deviceId, float& volume);
    bool GetOutputDeviceMute(const std::string& deviceId, bool& mute);

    // 输入设备管理
    std::vector<InputDeviceInfo> GetAllInputDevices();
    bool SetInputDeviceVolume(const std::string& deviceId, float volume);
    bool SetInputDeviceMute(const std::string& deviceId, bool mute);
    bool GetInputDeviceVolume(const std::string& deviceId, float& volume);
    bool GetInputDeviceMute(const std::string& deviceId, bool& mute);

private:
    // COM接口指针
    IMMDevice* pSpeakerDevice;
    IAudioEndpointVolume* pSpeakerEndpointVolume;
    IMMDevice* pMicrophoneDevice;
    IAudioEndpointVolume* pMicrophoneEndpointVolume;

    // 会话管理相关
    IAudioSessionManager* pSessionManager;
    IAudioSessionEnumerator* pSessionEnumerator;

    // 初始化COM接口
    bool InitializeSpeaker();
    bool InitializeMicrophone();
    bool InitializeSessionManager();
    void Cleanup();

    // 辅助方法
    std::string GetProcessNameFromPid(DWORD pid);
    std::string GetProcessPathFromPid(DWORD pid);
    std::string GetDeviceName(IMMDevice* device);

    // 图标提取方法
public:
    std::string GetIconBase64(const std::string& filePath, int iconSize = 32, bool bgr = true, bool alpha = true);

private:
    // 设备切换自愈
    std::string currentSpeakerDeviceId;
    std::string currentMicrophoneDeviceId;
    bool RefreshDefaultDevicesIfChanged();
    std::string GetDeviceId(IMMDevice* device);
    std::string GetDefaultDeviceId(EDataFlow flow);
};

// 宏录制事件结构（前置定义）
struct MacroEvent {
    DWORD dt;           // 相对时间戳（毫秒）
    std::string evt;    // 事件类型: "mouse-move", "mouse-btn-changed", "mouse-wheel", "key-changed"
    int pos[2];        // 鼠标位置 [x, y]
    
    // 事件数据
    struct EventData {
        int button[3];              // 鼠标按钮状态 [左, 中, 右] 1=按下 0=释放
        int dx, dy;                 // 滚轮偏移量
        std::vector<BYTE> keys;     // 当前按下的键的虚拟键码列表
        std::string key_name;       // 按键名称，例如 "Ctrl + A"
        
        EventData() : button{0, 0, 0}, dx(0), dy(0), key_name("") {}
    } data;
    
    MacroEvent() : dt(0), evt(""), pos{0, 0} {}
};

// 宏状态事件结构（前置定义）
struct MacroStatusEvent {
    std::string status;     // 状态: "record started", "record stopped", "play started", "play stopped"
    std::string macroId;    // 宏ID
    
    MacroStatusEvent() : status(""), macroId("") {}
    MacroStatusEvent(const std::string& s, const std::string& id) : status(s), macroId(id) {}
};

// 系统模块管理器主类
class SystemModuleManager {
public:
    SystemModuleManager();
    ~SystemModuleManager();

    // 初始化和清理
    bool Initialize();
    void Shutdown();

    // 键盘功能
    bool SendKeyInput(const std::string& input);

    // 音频功能
    bool SetMediaState(const std::string& type, float volume, int mute); // mute: -1表示不设置，0表示取消静音，1表示静音
    bool GetMediaState(const std::string& type, float& volume, bool& mute);
    
    // 输出设备功能
    std::vector<OutputDeviceInfo> GetAllOutputDevices();
    bool SetOutputDeviceVolume(const std::string& deviceId, float volume);
    bool SetOutputDeviceMute(const std::string& deviceId, bool mute);
    bool GetOutputDeviceVolume(const std::string& deviceId, float& volume);
    bool GetOutputDeviceMute(const std::string& deviceId, bool& mute);
    
    // 输入设备功能
    std::vector<InputDeviceInfo> GetAllInputDevices();
    bool SetInputDeviceVolume(const std::string& deviceId, float volume);
    bool SetInputDeviceMute(const std::string& deviceId, bool mute);
    bool GetInputDeviceVolume(const std::string& deviceId, float& volume);
    bool GetInputDeviceMute(const std::string& deviceId, bool& mute);

    // 音频混音器功能
    std::vector<AudioSessionInfo> GetAllAudioSessions();
    bool SetAudioSessionVolume(const std::string& sessionId, float volume);
    float GetAudioSessionVolume(const std::string& sessionId);

    // 图标功能
    std::string GetIcon(const std::string& filePath, int iconSize = 32, bool bgr = true, bool alpha = true);
    
    // 宏录制功能
    bool StartMacroRecording(const std::string& macroId = "", bool blockInput = false);
    bool StopMacroRecording();
    bool IsRecording() const;
    std::vector<MacroEvent> GetMacroEvents() const;
    double GetRecordingDuration() const;
    
    // 宏播放功能  
    bool StartMacroPlayback(const std::vector<MacroEvent>& events, const std::string& macroId = "", double speedFactor = 1.0, int loopCount = 1, int smoothingFactor = 1);
    void StopMacroPlayback();
    bool IsPlaying() const;
    
    // 统一的状态回调（包括录制事件和状态变更）
    void SetMacroStatusCallback(std::function<void(const MacroEvent*, const MacroStatusEvent*)> callback);

private:
    KeyboardManager* keyboard_manager_;
    AudioManager* audio_manager_;
    MacroRecorder* macro_recorder_;
    MacroPlayer* macro_player_;
    bool initialized_;
};

// 宏录制器类
class MacroRecorder {
public:
    MacroRecorder();
    ~MacroRecorder();
    
    // 统一回调类型（传递 MacroEvent 指针或 MacroStatusEvent 指针，其中一个为 null）
    using StatusCallback = std::function<void(const MacroEvent*, const MacroStatusEvent*)>;
    
    // 开始录制
    bool StartRecording(const std::string& macroId = "", bool blockInput = false);
    
    // 停止录制
    bool StopRecording();
    
    // 获取录制状态
    bool IsRecording() const { return isRecording; }
    
    // 获取录制的事件
    std::vector<MacroEvent> GetEvents() const { return events; }
    
    // 清空事件
    void ClearEvents() { events.clear(); }
    
    // 获取录制时长（秒）
    double GetDuration() const;
    
    // 设置状态回调
    void SetStatusCallback(StatusCallback callback) { statusCallback = callback; }
    
    // 获取当前的 macroId
    std::string GetMacroId() const { return currentMacroId; }
    
private:
    bool isRecording;
    std::vector<MacroEvent> events;
    DWORD startTime;
    DWORD lastEventTime;
    DWORD endTime;
    std::string currentMacroId;
    bool blockInputToSystem;
    
    // 统一回调
    StatusCallback statusCallback;
    
    // 当前状态
    std::vector<BYTE> currentKeys;  // 当前按下的键
    int currentButtons[3];          // 当前鼠标按钮状态
    int lastPos[2];                 // 上次鼠标位置
    
    // Windows钩子
    HHOOK keyboardHook;
    HHOOK mouseHook;
    std::thread* hookThread;
    DWORD hookThreadId;
    std::atomic<bool> hooksInstalled;
    std::atomic<bool> threadRunning;
    
    // 钩子回调函数（需要是静态的）
    static LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam);
    
    // 实例管理（因为钩子回调是静态的）
    static MacroRecorder* instance;
    
    // 记录事件
    void LogEvent(const std::string& evt, int x, int y, void* data);
    
    // 获取缩放因子
    double GetScalingFactor();

    // 钩子线程函数
    void HookThreadFunc();
};

// 宏播放器类
class MacroPlayer {
public:
    MacroPlayer();
    ~MacroPlayer();
    
    // 统一回调类型（与 MacroRecorder 相同）
    using StatusCallback = std::function<void(const MacroEvent*, const MacroStatusEvent*)>;
    
    // 设置要播放的事件
    void SetEvents(const std::vector<MacroEvent>& events);
    
    // 开始播放
    bool StartPlaying(const std::string& macroId = "", double speedFactor = 1.0, int loopCount = 1, int smoothingFactor = 1);
    
    // 停止播放（手动停止）
    void StopPlaying();
    
    // 获取播放状态
    bool IsPlaying() const { return isPlaying; }
    
    // 设置状态回调
    void SetStatusCallback(StatusCallback callback) { statusCallback = callback; }
    
    // 获取当前的 macroId
    std::string GetMacroId() const { return currentMacroId; }
    
private:
    bool isPlaying;
    bool shouldStop;
    std::vector<MacroEvent> events;
    double speedFactor;
    int loopCount;
    int smoothingFactor;
    std::string currentMacroId;
    StatusCallback statusCallback;
    
    // 播放线程
    std::thread* playbackThread;
    
    // 播放线程函数
    void PlaybackThreadFunc();
    
    // 获取缩放因子
    double GetScalingFactor();
    
    // 插值函数（用于平滑鼠标移动）
    std::vector<std::pair<int, int>> InterpolatePoints(int x1, int y1, int x2, int y2, int numPoints);
    
    // 当前按下的键（用于确保正确释放）
    std::set<BYTE> pressedKeys;
};

// 全局管理器实例声明
extern SystemModuleManager* g_system_manager;

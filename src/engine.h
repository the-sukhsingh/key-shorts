#pragma once

#include "config.h"
#include "keys.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

class Engine {
public:
    static Engine& Instance();

    bool Start();
    void Stop();
    int Reload(const Config& config, bool enabled);
    void SetEnabled(bool enabled);
    bool IsEnabled() const { return enabled_.load(); }
    bool IsRunning() const { return running_.load() && hook_.load() != nullptr; }
    void SetPaused(bool paused) { paused_.store(paused); }

private:
    struct CompiledRule {
        std::wstring name;
        std::wstring target;
        std::wstring args;
        std::wstring app;
        int type = RT_LAUNCH;
        int scope = RS_GLOBAL;
        bool swallow = true;
        bool repeat = false;
        KeyCombo trigger;
        KeyCombo out;
    };

    struct Job {
        int type = RT_LAUNCH;
        std::wstring target;
        std::wstring args;
    };

    struct RepeatState {
        bool active = false;
        Job job;
    };

    Engine() = default;
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    static DWORD WINAPI ThreadProc(LPVOID param);
    static LRESULT CALLBACK HookProc(int code, WPARAM wParam, LPARAM lParam);
    static void CALLBACK RepeatTimerProc(HWND hwnd, UINT msg, UINT_PTR id, DWORD time);

    void ThreadMain();
    bool HandleKey(int msg, const KBDLLHOOKSTRUCT* info);
    bool FindMatch(int vk, UINT mods, CompiledRule* out);
    UINT CurrentMods() const;
    const std::wstring& ForegroundApp();

    void EnqueueAction(const CompiledRule& rule);
    void EnqueueJob(const Job& job);
    void StartRepeat(int vk, const CompiledRule& rule);
    void StopRepeat(int vk);

    void WorkerMain();
    void RunJob(const Job& job);
    void RunMacro(const std::wstring& macro);
    bool WaitMs(int ms);

    std::atomic<bool> running_{false};
    std::atomic<bool> enabled_{true};
    std::atomic<bool> paused_{false};
    std::atomic<HHOOK> hook_{nullptr};
    std::atomic<bool> workerStop_{false};

    HANDLE thread_ = nullptr;
    DWORD threadId_ = 0;
    HANDLE readyEvent_ = nullptr;

    SRWLOCK rulesLock_ = SRWLOCK_INIT;
    std::vector<CompiledRule> rules_;

    bool down_[256] = {};
    bool swallowUp_[256] = {};
    bool remapActive_[256] = {};
    // Set when a swallowed Win+key rule fires, so the Win key-up can be
    // preceded by a no-op key and Windows does not treat it as a lone tap.
    bool winShield_ = false;
    KeyCombo remapHold_[256];
    RepeatState repeats_[256];

    HWND fgWindow_ = nullptr;
    std::wstring fgApp_;

    std::thread worker_;
    std::mutex queueMutex_;
    std::condition_variable queueCv_;
    std::deque<Job> queue_;
    bool stopping_ = false;
};

#include "engine.h"
#include "util.h"

#include <algorithm>
#include <shellapi.h>
#include <dwmapi.h>
#include <objbase.h>
#include <oleauto.h>
#include <exdisp.h>
#include <knownfolders.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <cwchar>
#include <cwctype>
#include <cstdlib>

namespace {

constexpr UINT_PTR kRepeatTimerBase = 0x1000;
constexpr UINT_PTR kSpotifyTimerBase = 0x2000;
constexpr DWORD kSpotifyDoubleMs = 350;

// Reserved key code (VK_NONAME). No application reacts to it, but Windows
// counts it as a key press when deciding whether the Win key was tapped alone.
constexpr int kNoOpVk = 0xFC;

bool IsWinVk(int vk) { return vk == VK_LWIN || vk == VK_RWIN; }

UINT ModBitFromWord(const std::wstring& word) {
    std::wstring w = Lower(Trim(word));
    if (w == L"ctrl" || w == L"control" || w == L"lctrl" || w == L"rctrl") return KS_CTRL;
    if (w == L"shift" || w == L"lshift" || w == L"rshift") return KS_SHIFT;
    if (w == L"alt" || w == L"lalt" || w == L"ralt" || w == L"menu") return KS_ALT;
    if (w == L"win" || w == L"lwin" || w == L"rwin" || w == L"windows" || w == L"super") return KS_WIN;
    return 0;
}

void SendKeyWithMods(int vk, UINT extra, UINT held, UINT* sticky) {
    UINT stickyNow = *sticky;
    UINT pressExtra = extra & ~(held | stickyNow);
    UINT pressSticky = stickyNow & ~held;

    InjectModsDown(pressExtra, kSigMacro);
    if (pressSticky) InjectModsDown(pressSticky, kSigMacro);
    InjectVk(vk, true, kSigMacro);
    InjectVk(vk, false, kSigMacro);
    if (pressSticky) InjectModsUp(pressSticky, kSigMacro);
    InjectModsUp(pressExtra, kSigMacro);
    *sticky = 0;
}

class ScopedCom {
public:
    ScopedCom() : result_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~ScopedCom() {
        if (SUCCEEDED(result_)) CoUninitialize();
    }
    bool usable() const { return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE; }

private:
    HRESULT result_;
};

bool IsDirectoryPath(const std::wstring& path) {
    if (path.empty()) return false;
    DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring HomeDirectory() {
    PWSTR raw = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, KF_FLAG_DEFAULT, nullptr, &raw)) && raw) {
        std::wstring path(raw);
        CoTaskMemFree(raw);
        if (IsDirectoryPath(path)) return path;
    }

    wchar_t buffer[MAX_PATH]{};
    DWORD length = GetEnvironmentVariableW(L"USERPROFILE", buffer, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        std::wstring path(buffer);
        if (IsDirectoryPath(path)) return path;
    }
    return {};
}

bool IsExplorerWindow(HWND hwnd) {
    wchar_t className[64]{};
    if (!hwnd || !GetClassNameW(hwnd, className, 64)) return false;
    return _wcsicmp(className, L"CabinetWClass") == 0 || _wcsicmp(className, L"ExploreWClass") == 0;
}

BOOL CALLBACK FindExplorerProc(HWND hwnd, LPARAM param) {
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) return TRUE;
    if (IsExplorerWindow(hwnd)) {
        *reinterpret_cast<HWND*>(param) = hwnd;
        return FALSE;
    }
    return TRUE;
}

HWND FindExplorerWindow() {
    HWND foreground = GetForegroundWindow();
    if (foreground) {
        HWND root = GetAncestor(foreground, GA_ROOT);
        if (IsExplorerWindow(root)) return root;
    }

    HWND found = nullptr;
    EnumWindows(FindExplorerProc, reinterpret_cast<LPARAM>(&found));
    return found;
}

std::wstring FileUrlToPath(const wchar_t* url) {
    if (!url || !*url) return {};

    DWORD length = 1024;
    std::wstring path(length, L'\0');
    HRESULT result = PathCreateFromUrlW(url, path.data(), &length, 0);
    if (result == E_NOT_SUFFICIENT_BUFFER) {
        path.resize(length);
        result = PathCreateFromUrlW(url, path.data(), &length, 0);
    }
    if (FAILED(result)) return {};

    path.resize(wcslen(path.c_str()));
    return path;
}

std::wstring ExplorerFolder() {
    HWND target = FindExplorerWindow();
    if (!target) return {};

    ScopedCom com;
    if (!com.usable()) return {};

    IShellWindows* windows = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_LOCAL_SERVER,
                                IID_PPV_ARGS(&windows))) ||
        !windows) {
        return {};
    }

    std::wstring folder;
    long count = 0;
    if (SUCCEEDED(windows->get_Count(&count))) {
        for (long index = 0; index < count; ++index) {
            VARIANT item;
            VariantInit(&item);
            item.vt = VT_I4;
            item.lVal = index;

            IDispatch* dispatch = nullptr;
            HRESULT result = windows->Item(item, &dispatch);
            VariantClear(&item);
            if (FAILED(result) || !dispatch) continue;

            IWebBrowserApp* browser = nullptr;
            if (SUCCEEDED(dispatch->QueryInterface(IID_PPV_ARGS(&browser))) && browser) {
                SHANDLE_PTR handle = 0;
                if (SUCCEEDED(browser->get_HWND(&handle)) &&
                    reinterpret_cast<HWND>(handle) == target) {
                    BSTR url = nullptr;
                    if (SUCCEEDED(browser->get_LocationURL(&url)) && url) {
                        folder = FileUrlToPath(url);
                        SysFreeString(url);
                    }
                }
                browser->Release();
            }
            dispatch->Release();

            if (!folder.empty()) break;
        }
    }
    windows->Release();

    if (!IsDirectoryPath(folder)) return {};
    return folder;
}

std::wstring LaunchDirectory() {
    std::wstring folder = ExplorerFolder();
    if (!folder.empty()) return folder;
    return HomeDirectory();
}

bool IsCloakedWindow(HWND hwnd) {
    int cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)))) {
        return cloaked != 0;
    }
    return false;
}

std::wstring ProcessImageName(DWORD pid);

BOOL CALLBACK SpotifyPlayPauseProc(HWND hwnd, LPARAM) {
    if (!IsWindowVisible(hwnd)) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return TRUE;
    if (!EqualsI(ProcessImageName(pid), L"Spotify.exe")) return TRUE;
    LRESULT r = SendMessageW(hwnd, WM_APPCOMMAND, 0, MAKELPARAM(0, APPCOMMAND_MEDIA_PLAY_PAUSE));
    FILE* log = _wfopen(L"C:\\\\Users\\\\sukha\\\\AppData\\\\Local\\\\Temp\\\\opencode\\\\ks.log", L"a");
    if (log) { fwprintf(log, L"Spotify toggle hwnd=%p pid=%lu rc=%lld\n", (void*)hwnd, (unsigned long)pid, (long long)r); fclose(log); }
    return FALSE;
}

void ToggleSpotify() {
    EnumWindows(&SpotifyPlayPauseProc, 0);
}

BOOL CALLBACK SpotifyNextProc(HWND hwnd, LPARAM) {
    if (!IsWindowVisible(hwnd)) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return TRUE;
    if (!EqualsI(ProcessImageName(pid), L"Spotify.exe")) return TRUE;
    SendMessageW(hwnd, WM_APPCOMMAND, 0, MAKELPARAM(0, APPCOMMAND_MEDIA_NEXTTRACK));
    return FALSE;
}

void NextSpotifyTrack() {
    EnumWindows(&SpotifyNextProc, 0);
}

BOOL CALLBACK SpotifyPrevProc(HWND hwnd, LPARAM) {
    if (!IsWindowVisible(hwnd)) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return TRUE;
    if (!EqualsI(ProcessImageName(pid), L"Spotify.exe")) return TRUE;
    SendMessageW(hwnd, WM_APPCOMMAND, 0, MAKELPARAM(0, APPCOMMAND_MEDIA_PREVIOUSTRACK));
    return FALSE;
}

void PrevSpotifyTrack() {
    EnumWindows(&SpotifyPrevProc, 0);
}

bool IsOnCurrentDesktop(HWND hwnd) {
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool uninit = (init == S_OK || init == S_FALSE);
    bool result = true;
    IVirtualDesktopManager* manager = nullptr;
    const CLSID clsidVirtualDesktopManager = {
        0xaa509086, 0x5ca9, 0x4c25, {0x8f, 0x95, 0x58, 0x9d, 0x3c, 0x07, 0xb4, 0x8a}
    };
    HRESULT hr = CoCreateInstance(clsidVirtualDesktopManager, nullptr, CLSCTX_ALL,
                                  __uuidof(IVirtualDesktopManager), reinterpret_cast<void**>(&manager));
    if (SUCCEEDED(hr) && manager) {
        BOOL onCurrent = FALSE;
        if (SUCCEEDED(manager->IsWindowOnCurrentVirtualDesktop(hwnd, &onCurrent))) {
            result = (onCurrent == TRUE);
        }
        manager->Release();
    }
    if (uninit) CoUninitialize();
    return result;
}

std::wstring ProcessImageName(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return {};

    wchar_t path[1024] = {};
    DWORD size = static_cast<DWORD>(sizeof(path) / sizeof(path[0]));
    std::wstring name;
    if (QueryFullProcessImageNameW(process, 0, path, &size)) {
        name = FileNameOf(std::wstring(path, size));
    }
    CloseHandle(process);
    return name;
}

struct AppWindow {
    HWND hwnd;
    int z;
};

struct AppWindowSearch {
    const std::wstring* exe = nullptr;
    std::vector<AppWindow> windows;
    int z = 0;
};

BOOL CALLBACK CollectAppWindowsProc(HWND hwnd, LPARAM param) {
    auto* search = reinterpret_cast<AppWindowSearch*>(param);
    int z = search->z++;
    if (!IsWindowVisible(hwnd) || GetWindowTextLengthW(hwnd) == 0) return TRUE;
    if (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;
    if (IsCloakedWindow(hwnd)) return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return TRUE;
    if (!EqualsI(ProcessImageName(pid), *search->exe)) return TRUE;

    search->windows.push_back({hwnd, z});
    return TRUE;
}

void ActivateWindow(HWND hwnd) {
    if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);

    HWND foreground = GetForegroundWindow();
    DWORD foregroundThread = foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0;
    DWORD targetThread = GetWindowThreadProcessId(hwnd, nullptr);
    bool attached = foregroundThread && targetThread && foregroundThread != targetThread &&
                    AttachThreadInput(foregroundThread, targetThread, TRUE) != FALSE;

    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);
    if (attached) AttachThreadInput(foregroundThread, targetThread, FALSE);
}

// Brings up the next window of an app that is already running. Returns false
// when the app has no open windows so the caller can launch it instead.
bool FocusExistingWindow(const std::wstring& target, bool allowCrossDesktop) {
    std::wstring exe = FileNameOf(target);
    if (exe.empty()) return false;

    AppWindowSearch search;
    search.exe = &exe;
    EnumWindows(CollectAppWindowsProc, reinterpret_cast<LPARAM>(&search));
    if (!allowCrossDesktop && !search.windows.empty()) {
        std::vector<AppWindow> current;
        for (const AppWindow& window : search.windows) {
            if (IsOnCurrentDesktop(window.hwnd)) current.push_back(window);
        }
        search.windows.swap(current);
    }
    if (search.windows.empty()) return false;

    // Cycle in creation order. The z-order changes as windows are raised, so
    // walking it would just bounce between the two most recent windows.
    std::vector<HWND> ordered;
    ordered.reserve(search.windows.size());
    for (const AppWindow& window : search.windows) ordered.push_back(window.hwnd);
    std::sort(ordered.begin(), ordered.end(), [](HWND a, HWND b) {
        return reinterpret_cast<ULONG_PTR>(a) < reinterpret_cast<ULONG_PTR>(b);
    });

    HWND foreground = GetAncestor(GetForegroundWindow(), GA_ROOT);
    auto current = std::find(ordered.begin(), ordered.end(), foreground);
    if (current == ordered.end()) {
        // Coming from another app: show the most recently used window first.
        ActivateWindow(search.windows.front().hwnd);
        return true;
    }

    size_t next = static_cast<size_t>(current - ordered.begin() + 1) % ordered.size();
    ActivateWindow(ordered[next]);
    return true;
}

} // namespace

Engine& Engine::Instance() {
    static Engine instance;
    return instance;
}

Engine::~Engine() {
    Stop();
}

bool Engine::Start() {
    if (running_.load()) return true;

    workerStop_.store(false);
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        stopping_ = false;
        queue_.clear();
    }
    try {
        worker_ = std::thread(&Engine::WorkerMain, this);
    } catch (...) {
        return false;
    }

    readyEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread_ = CreateThread(nullptr, 0, &Engine::ThreadProc, this, 0, &threadId_);
    if (!thread_) {
        workerStop_.store(true);
        queueCv_.notify_all();
        if (worker_.joinable()) worker_.join();
        return false;
    }

    if (readyEvent_) {
        WaitForSingleObject(readyEvent_, 3000);
        CloseHandle(readyEvent_);
        readyEvent_ = nullptr;
    }

    if (!running_.load()) {
        Stop();
        return false;
    }
    return true;
}

void Engine::Stop() {
    paused_.store(true);

    if (thread_) {
        if (threadId_) PostThreadMessageW(threadId_, WM_QUIT, 0, 0);
        WaitForSingleObject(thread_, 3000);
        CloseHandle(thread_);
        thread_ = nullptr;
        threadId_ = 0;
        running_.store(false);
    }

    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        stopping_ = true;
    }
    workerStop_.store(true);
    queueCv_.notify_all();
    if (worker_.joinable()) worker_.join();

    for (auto& repeat : repeats_) repeat.active = false;
    for (int vk = 0; vk < 256; ++vk) {
        if (spotifyPending_[vk].active) {
            KillTimer(nullptr, kSpotifyTimerBase + static_cast<UINT_PTR>(vk));
            spotifyPending_[vk].active = false;
        }
    }
    winShield_ = false;
    paused_.store(false);
}

int Engine::Reload(const Config& config, bool enabled) {
    std::vector<CompiledRule> compiled;
    compiled.reserve(config.rules.size());
    int invalid = 0;

    for (int vk = 0; vk < 256; ++vk) {
        if (spotifyPending_[vk].active) {
            KillTimer(nullptr, kSpotifyTimerBase + static_cast<UINT_PTR>(vk));
            spotifyPending_[vk].active = false;
        }
    }

    for (const Rule& rule : config.rules) {
        if (!rule.enabled) continue;

        KeyCombo trigger;
        if (!ParseCombo(rule.trigger, &trigger)) {
            ++invalid;
            continue;
        }

        CompiledRule entry;
        entry.name = rule.name;
        entry.target = rule.target;
        entry.args = rule.args;
        entry.type = rule.type;
        entry.scope = rule.scope;
        entry.swallow = rule.swallow;
        entry.repeat = rule.repeat;
        entry.focusExisting = rule.focusExisting;
        entry.reuseAcrossDesktops = rule.reuseAcrossDesktops;
        entry.trigger = trigger;

        if (rule.type == RT_REMAP) {
            if (!ParseCombo(rule.target, &entry.out)) {
                ++invalid;
                continue;
            }
        } else if (rule.type != RT_SPOTIFY && Trim(rule.target).empty()) {
            ++invalid;
            continue;
        }

        if (rule.scope == RS_APP) {
            entry.app = Trim(FileNameOf(rule.app));
            if (entry.app.empty()) {
                ++invalid;
                continue;
            }
        }

        compiled.push_back(std::move(entry));
    }

    std::stable_partition(compiled.begin(), compiled.end(),
                          [](const CompiledRule& rule) { return rule.scope == RS_APP; });

    AcquireSRWLockExclusive(&rulesLock_);
    rules_.swap(compiled);
    ReleaseSRWLockExclusive(&rulesLock_);

    enabled_.store(enabled);
    return invalid;
}

void Engine::SetEnabled(bool enabled) {
    enabled_.store(enabled);
}

void Engine::EnqueueAction(const CompiledRule& rule) {
    Job job;
    job.type = rule.type;
    job.target = rule.target;
    job.args = rule.args;
    job.focusExisting = rule.focusExisting;
    job.reuseAcrossDesktops = rule.reuseAcrossDesktops;
    EnqueueJob(job);
}

void Engine::EnqueueJob(const Job& job) {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if (stopping_ || queue_.size() >= 64) return;
        queue_.push_back(job);
    }
    queueCv_.notify_one();
}

void Engine::StartRepeat(int vk, const CompiledRule& rule) {
    RepeatState& state = repeats_[vk];
    state.active = true;
    state.job.type = rule.type;
    state.job.target = rule.target;
    state.job.args = rule.args;
    state.job.focusExisting = rule.focusExisting;
    state.job.reuseAcrossDesktops = rule.reuseAcrossDesktops;
    if (SetTimer(nullptr, kRepeatTimerBase + static_cast<UINT_PTR>(vk), 100, &Engine::RepeatTimerProc) == 0) {
        state.active = false;
    }
}

void Engine::StopRepeat(int vk) {
    if (!repeats_[vk].active) return;
    KillTimer(nullptr, kRepeatTimerBase + static_cast<UINT_PTR>(vk));
    repeats_[vk].active = false;
}

UINT Engine::CurrentMods() const {
    UINT mods = 0;
    if (down_[VK_LCONTROL] || down_[VK_RCONTROL] || down_[VK_CONTROL]) mods |= KS_CTRL;
    if (down_[VK_LSHIFT] || down_[VK_RSHIFT] || down_[VK_SHIFT]) mods |= KS_SHIFT;
    if (down_[VK_LMENU] || down_[VK_RMENU] || down_[VK_MENU]) mods |= KS_ALT;
    if (down_[VK_LWIN] || down_[VK_RWIN]) mods |= KS_WIN;
    return mods;
}

const std::wstring& Engine::ForegroundApp() {
    HWND foreground = GetForegroundWindow();
    if (!foreground) {
        fgWindow_ = nullptr;
        fgApp_.clear();
        return fgApp_;
    }
    if (foreground == fgWindow_) return fgApp_;

    fgWindow_ = foreground;
    fgApp_.clear();

    DWORD pid = 0;
    GetWindowThreadProcessId(foreground, &pid);
    if (pid == 0) return fgApp_;

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return fgApp_;

    wchar_t path[1024] = {};
    DWORD size = static_cast<DWORD>(sizeof(path) / sizeof(path[0]));
    if (QueryFullProcessImageNameW(process, 0, path, &size)) {
        fgApp_ = FileNameOf(std::wstring(path, size));
    }
    CloseHandle(process);
    return fgApp_;
}

bool Engine::FindMatch(int vk, UINT mods, CompiledRule* out) {
    AcquireSRWLockShared(&rulesLock_);
    bool result = false;

    bool needsApp = false;
    for (const CompiledRule& rule : rules_) {
        if (rule.scope == RS_APP) { needsApp = true; break; }
    }
    if (needsApp) ForegroundApp();

    for (const CompiledRule& rule : rules_) {
        if (rule.scope == RS_APP && !EqualsI(rule.app, fgApp_)) continue;
        if (rule.trigger.vk == vk && rule.trigger.mods == mods) {
            *out = rule;
            result = true;
            break;
        }
    }

    ReleaseSRWLockShared(&rulesLock_);
    return result;
}

bool Engine::HandleKey(int msg, const KBDLLHOOKSTRUCT* info) {
    int vk = static_cast<int>(info->vkCode);
    if (vk < 0 || vk > 255) return false;

    bool isDown = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
    bool isUp = msg == WM_KEYUP || msg == WM_SYSKEYUP;
    if (!isDown && !isUp) return false;

    ULONG_PTR signature = info->dwExtraInfo;
    if (signature == kSigMacro) return false;
    if (signature == kSigRemap) {
        if (IsModifierVk(vk)) down_[vk] = isDown;
        return false;
    }

    if (paused_.load()) {
        if (isUp) {
            down_[vk] = false;
            if (remapActive_[vk]) {
                InjectComboUp(remapHold_[vk], kSigRemap);
                remapActive_[vk] = false;
            }
            swallowUp_[vk] = false;
            if (repeats_[vk].active) StopRepeat(vk);
            if (IsWinVk(vk)) winShield_ = false;
        } else {
            down_[vk] = true;
        }
        return false;
    }

    if (isUp) {
        down_[vk] = false;
        bool consumed = false;
        if (remapActive_[vk]) {
            InjectComboUp(remapHold_[vk], kSigRemap);
            remapActive_[vk] = false;
            consumed = true;
        }
        if (swallowUp_[vk]) {
            swallowUp_[vk] = false;
            consumed = true;
        }
        if (repeats_[vk].active) StopRepeat(vk);
        if (winShield_ && IsWinVk(vk)) {
            // Windows opens the Start menu when the Win key is released without
            // any other key press in between. The swallowed trigger key never
            // reached Windows, so send a no-op key before letting this release
            // through, otherwise every Win+key shortcut also pops Start.
            winShield_ = false;
            InjectVk(kNoOpVk, true, kSigMacro);
            InjectVk(kNoOpVk, false, kSigMacro);
        }
        return consumed;
    }

    bool wasDown = down_[vk];
    if (isDown && vk == VK_LCONTROL) lctrlDownTick_ = GetTickCount();
    down_[vk] = true;

    if (wasDown) {
        if (remapActive_[vk]) {
            InjectVk(remapHold_[vk].vk, true, kSigRemap);
            return true;
        }
        if (swallowUp_[vk]) return true;
        return false;
    }

    if (!enabled_.load()) return false;

    UINT mods = CurrentMods();
    if (vk == VK_RMENU && lctrlDownTick_ != 0 &&
        GetTickCount() - lctrlDownTick_ <= 100) {
        mods &= ~KS_CTRL;
    }
    if (IsModifierVk(vk)) mods &= ~ModsFromVk(vk);

    CompiledRule rule;
    if (!FindMatch(vk, mods, &rule)) return false;

    bool swallowSource = rule.type == RT_REMAP || rule.swallow;
    if (swallowSource && IsModifierVk(vk)) {
        // The app never sees this physical modifier; injected output tracks its own state.
        down_[vk] = false;
    }
    if (swallowSource && (rule.trigger.mods & KS_WIN)) {
        // This Win press reached Windows and the trigger key will not, so the
        // release needs shielding from the Start menu's lone-Win detection.
        winShield_ = true;
    }

    if (rule.type == RT_REMAP) {
        InjectComboDown(rule.out, kSigRemap);
        remapHold_[vk] = rule.out;
        remapActive_[vk] = true;
        return true;
    }

    if (rule.type == RT_SPOTIFY) {
        DWORD now = GetTickCount();
        SpotifyPending& pending = spotifyPending_[vk];
        if (pending.active && (pending.vk != vk || pending.mods != mods ||
                                (now - pending.started) > kSpotifyDoubleMs)) {
            // Previous click sequence ended; flush it now.
            KillTimer(nullptr, kSpotifyTimerBase + static_cast<UINT_PTR>(vk));
            int action = (pending.clicks >= 3) ? 2 : (pending.clicks == 2 ? 1 : 0);
            pending.job.type = RT_SPOTIFY;
            pending.job.spotifyAction = action;
            EnqueueJob(pending.job);
            pending.active = false;
        }

        if (!pending.active) {
            pending.active = true;
            pending.started = now;
            pending.vk = vk;
            pending.mods = mods;
            pending.clicks = 1;
            pending.job = Job{};
            pending.job.type = RT_SPOTIFY;
        } else {
            pending.clicks = std::min(pending.clicks + 1, 3);
            pending.started = now;
        }

        KillTimer(nullptr, kSpotifyTimerBase + static_cast<UINT_PTR>(vk));
        if (SetTimer(nullptr, kSpotifyTimerBase + static_cast<UINT_PTR>(vk),
                     kSpotifyDoubleMs, &Engine::SpotifyTimerProc) == 0) {
            int action = (pending.clicks >= 3) ? 2 : (pending.clicks == 2 ? 1 : 0);
            pending.job.type = RT_SPOTIFY;
            pending.job.spotifyAction = action;
            EnqueueJob(pending.job);
            pending.active = false;
        }
    } else {
        if (rule.repeat && rule.type == RT_SENDKEYS) StartRepeat(vk, rule);
        EnqueueAction(rule);
    }

    if (rule.swallow) {
        swallowUp_[vk] = true;
        return true;
    }
    return false;
}

void Engine::WorkerMain() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_ && queue_.empty()) return;
            job = std::move(queue_.front());
            queue_.pop_front();
        }
        RunJob(job);
    }
}

bool Engine::WaitMs(int ms) {
    int remaining = ms;
    while (remaining > 0) {
        if (workerStop_.load()) return false;
        int step = remaining > 50 ? 50 : remaining;
        Sleep(static_cast<DWORD>(step));
        remaining -= step;
    }
    return !workerStop_.load();
}

void Engine::RunJob(const Job& job) {
    if (workerStop_.load()) return;

    switch (job.type) {
    case RT_LAUNCH: {
        std::wstring target = ExpandEnv(job.target);
        std::wstring args = ExpandEnv(job.args);
        if (target.empty()) return;
        if (job.focusExisting) {
            bool reused = FocusExistingWindow(target, job.reuseAcrossDesktops);
            if (reused) break;
        }
        std::wstring directory = LaunchDirectory();
        ShellExecuteW(nullptr, L"open", target.c_str(), args.empty() ? nullptr : args.c_str(),
                      directory.empty() ? nullptr : directory.c_str(), SW_SHOWNORMAL);
        break;
    }
    case RT_COMMAND: {
        std::wstring command = L"cmd.exe /d /s /c \"" + job.target + L"\"";
        std::wstring directory = LaunchDirectory();
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                           nullptr, directory.empty() ? nullptr : directory.c_str(), &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
        break;
    }
    case RT_SENDKEYS:
        RunMacro(job.target);
        break;
    case RT_SPOTIFY:
        if (job.spotifyAction == 1) NextSpotifyTrack();
        else if (job.spotifyAction == 2) PrevSpotifyTrack();
        else ToggleSpotify();
        break;
    default:
        break;
    }
}

void Engine::RunMacro(const std::wstring& macro) {
    UINT held = 0;
    UINT sticky = 0;
    HKL layout = GetKeyboardLayout(0);

    auto typeChar = [&](wchar_t ch) {
        UINT active = held | sticky;
        bool sendUnicode = active == 0;
        if (!sendUnicode) {
            SHORT mapped = VkKeyScanExW(ch, layout);
            if (mapped == -1) {
                sendUnicode = true;
            } else {
                int vk = mapped & 0xFF;
                BYTE need = static_cast<BYTE>((mapped >> 8) & 0xFF);
                UINT autoMods = 0;
                if (need & 1) autoMods |= KS_SHIFT;
                if (need & 2) autoMods |= KS_CTRL;
                if (need & 4) autoMods |= KS_ALT;
                SendKeyWithMods(vk, autoMods, held, &sticky);
            }
        }
        if (sendUnicode) {
            InjectUnicode(ch, kSigMacro);
            if (sticky) {
                InjectModsUp(sticky, kSigMacro);
                sticky = 0;
            }
        }
    };

    for (size_t i = 0; i < macro.size();) {
        if (workerStop_.load()) return;
        wchar_t c = macro[i];

        if (c == L'{' && i + 1 < macro.size() && macro[i + 1] == L'{') {
            typeChar(L'{');
            i += 2;
            continue;
        }

        if (c == L'{') {
            size_t close = macro.find(L'}', i + 1);
            if (close == std::wstring::npos) {
                typeChar(L'{');
                ++i;
                continue;
            }

            std::wstring token = Trim(macro.substr(i + 1, close - i - 1));
            i = close + 1;
            if (token.empty()) continue;

            size_t space = token.find(L' ');
            std::wstring word = space == std::wstring::npos ? token : token.substr(0, space);
            std::wstring rest = space == std::wstring::npos ? L"" : Trim(token.substr(space + 1));

            UINT mod = ModBitFromWord(word);
            if (mod) {
                if (rest.empty()) {
                    sticky |= mod;
                } else if (EqualsI(rest, L"DOWN")) {
                    if (!(held & mod)) {
                        InjectModsDown(mod, kSigMacro);
                        held |= mod;
                    }
                } else if (EqualsI(rest, L"UP")) {
                    if (held & mod) {
                        InjectModsUp(mod, kSigMacro);
                        held &= ~mod;
                    }
                }
                continue;
            }

            if (EqualsI(word, L"WAIT") || EqualsI(word, L"SLEEP")) {
                std::wstring digits;
                for (wchar_t digit : rest) {
                    if (iswdigit(digit)) digits += digit;
                    else break;
                }
                int ms = digits.empty() ? 100 : _wtoi(digits.c_str());
                if (ms < 0) ms = 0;
                if (ms > 60000) ms = 60000;
                if (!WaitMs(ms)) return;
                continue;
            }

            int named = VkFromName(token);
            if (named != 0) {
                SendKeyWithMods(named, 0, held, &sticky);
                continue;
            }

            // Unknown token: type it literally, braces included.
            typeChar(L'{');
            for (wchar_t ch : token) typeChar(ch);
            typeChar(L'}');
            continue;
        }

        typeChar(c);
        ++i;
    }

    if (sticky) InjectModsUp(sticky, kSigMacro);
    if (held) InjectModsUp(held, kSigMacro);
}

DWORD WINAPI Engine::ThreadProc(LPVOID param) {
    auto* self = static_cast<Engine*>(param);
    self->ThreadMain();
    return 0;
}

void Engine::ThreadMain() {
    MSG msg;
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

    hook_.store(SetWindowsHookExW(WH_KEYBOARD_LL, &Engine::HookProc, GetModuleHandleW(nullptr), 0));
    if (hook_.load()) running_.store(true);

    if (readyEvent_) SetEvent(readyEvent_);
    if (!hook_.load()) return;

    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UnhookWindowsHookEx(hook_.load());
    hook_.store(nullptr);
    running_.store(false);
}

LRESULT CALLBACK Engine::HookProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION) {
        auto* info = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        if (Instance().HandleKey(static_cast<int>(wParam), info)) return 1;
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

void CALLBACK Engine::RepeatTimerProc(HWND, UINT, UINT_PTR id, DWORD) {
    Engine& engine = Instance();
    if (!engine.enabled_.load()) return;

    int vk = static_cast<int>(id - kRepeatTimerBase);
    if (vk < 0 || vk > 255) return;
    if (!engine.repeats_[vk].active) {
        KillTimer(nullptr, id);
        return;
    }
    engine.EnqueueJob(engine.repeats_[vk].job);
}

void CALLBACK Engine::SpotifyTimerProc(HWND, UINT, UINT_PTR id, DWORD) {
    Engine& engine = Instance();
    if (!engine.enabled_.load()) return;

    int vk = static_cast<int>(id - kSpotifyTimerBase);
    if (vk < 0 || vk > 255) return;
    SpotifyPending& pending = engine.spotifyPending_[vk];
    if (!pending.active) {
        KillTimer(nullptr, id);
        return;
    }
    int action = (pending.clicks >= 3) ? 2 : (pending.clicks == 2 ? 1 : 0);
    pending.job.type = RT_SPOTIFY;
    pending.job.spotifyAction = action;
    engine.EnqueueJob(pending.job);
    pending.active = false;
    KillTimer(nullptr, id);
}

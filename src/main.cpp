#include "common.h"
#include "config.h"
#include "engine.h"
#include "resource.h"
#include "ui.h"

#include <shellapi.h>
#include <cwchar>

namespace {

int RunSelfTest() {
    Config config;
    config.Load();

    Engine& engine = Engine::Instance();
    engine.Reload(config, true);
    bool started = engine.Start();
    if (started) {
        Sleep(250);
        started = engine.IsRunning();
    }
    engine.Stop();
    return started ? 0 : 1;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int showCmd) {
    bool startHidden = false;
    bool selfTest = false;

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (_wcsicmp(argv[i], L"--tray") == 0 || _wcsicmp(argv[i], L"--hidden") == 0) {
                startHidden = true;
            } else if (_wcsicmp(argv[i], L"--self-test") == 0) {
                selfTest = true;
            }
        }
        LocalFree(argv);
    }

    if (selfTest) return RunSelfTest();

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"KeyShorts_SingleInstance");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(kWindowClass, nullptr);
        if (existing) PostMessageW(existing, WM_APP_SHOW, 0, 0);
        CloseHandle(mutex);
        return 0;
    }

    int result = RunApp(instance, showCmd, startHidden);
    if (mutex) CloseHandle(mutex);
    return result;
}

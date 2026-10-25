#include "ui_rule.h"
#include "engine.h"
#include "keys.h"
#include "resource.h"
#include "util.h"

#include <commctrl.h>
#include <commdlg.h>

namespace {

HHOOK g_captureHook = nullptr;
bool g_captureActive = false;
int g_captureMode = 0;
UINT g_captureMods = 0;
HWND g_captureDialog = nullptr;
wchar_t g_captureText[128] = {};

Rule* g_editRule = nullptr;
bool g_editIsNew = false;

std::wstring GetControlText(HWND control) {
    int length = GetWindowTextLengthW(control);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    int copied = GetWindowTextW(control, text.data(), length + 1);
    text.resize(static_cast<size_t>(copied));
    return text;
}

void UpdateTypeFields(HWND dialog) {
    int typeIndex = static_cast<int>(SendDlgItemMessageW(dialog, IDC_TYPE, CB_GETCURSEL, 0, 0));
    int scopeIndex = static_cast<int>(SendDlgItemMessageW(dialog, IDC_SCOPE, CB_GETCURSEL, 0, 0));
    int type = static_cast<int>(SendDlgItemMessageW(dialog, IDC_TYPE, CB_GETITEMDATA,
                                                   static_cast<WPARAM>(typeIndex), 0));
    int scope = static_cast<int>(SendDlgItemMessageW(dialog, IDC_SCOPE, CB_GETITEMDATA,
                                                    static_cast<WPARAM>(scopeIndex), 0));

    bool isRemap = type == RT_REMAP;
    bool isLaunch = type == RT_LAUNCH;
    bool isCommand = type == RT_COMMAND;
    bool isKeys = type == RT_SENDKEYS;

    const wchar_t* label = isRemap ? L"Map to:" : isLaunch ? L"Program:" : isCommand ? L"Command:" : L"Send keys:";
    SetDlgItemTextW(dialog, IDC_TARGET_LABEL, label);

    ShowWindow(GetDlgItem(dialog, IDC_BROWSE), isLaunch ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(dialog, IDC_SET_TARGET), isRemap ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(dialog, IDC_ARGS_LABEL), isLaunch ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(dialog, IDC_ARGS), isLaunch ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(dialog, IDC_REPEAT), isKeys ? SW_SHOW : SW_HIDE);
    EnableWindow(GetDlgItem(dialog, IDC_APP), scope == RS_APP);

    HWND swallow = GetDlgItem(dialog, IDC_SWALLOW);
    EnableWindow(swallow, !isRemap);
    if (isRemap) SendMessageW(swallow, BM_SETCHECK, BST_CHECKED, 0);

    const wchar_t* cue = isRemap ? L"e.g. Esc, Ctrl+C, LCtrl"
                        : isLaunch ? L"e.g. wt.exe or https://example.com"
                        : isCommand ? L"e.g. shutdown /s /t 0"
                        : L"e.g. Hello{ENTER} or {CTRL}c";
    SendDlgItemMessageW(dialog, IDC_TARGET, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(cue));
}

LRESULT CALLBACK CaptureProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code != HC_ACTION || !g_captureActive) return CallNextHookEx(nullptr, code, wParam, lParam);
    if (wParam != WM_KEYDOWN && wParam != WM_KEYUP && wParam != WM_SYSKEYDOWN && wParam != WM_SYSKEYUP) {
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }

    auto* info = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
    int vk = static_cast<int>(info->vkCode);
    bool isDown = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
    UINT mod = ModsFromVk(vk);

    if (isDown) {
        if (mod != 0) {
            g_captureMods |= mod;
            return 1;
        }
        if (vk == VK_ESCAPE) {
            g_captureMode = 0;
            PostMessageW(g_captureDialog, WM_APP_CAPTURED, 0, 0);
            return 1;
        }
        if (g_captureMode != 0) {
            KeyCombo combo{vk, g_captureMods};
            std::wstring text = FormatCombo(combo);
            wcsncpy_s(g_captureText, text.c_str(), _TRUNCATE);
            int mode = g_captureMode;
            g_captureMode = 0;
            PostMessageW(g_captureDialog, WM_APP_CAPTURED, static_cast<WPARAM>(mode), 0);
        }
        return 1;
    }

    if (mod != 0) g_captureMods &= ~mod;
    return 1;
}

void RestoreCaptureButtons(HWND dialog) {
    SetDlgItemTextW(dialog, IDC_SET_TRIGGER, L"Set...");
    SetDlgItemTextW(dialog, IDC_SET_TARGET, L"Set key...");
    SetDlgItemTextW(dialog, IDC_HINT, L"");
}

void EndCapture(HWND dialog) {
    if (g_captureHook) {
        UnhookWindowsHookEx(g_captureHook);
        g_captureHook = nullptr;
    }
    g_captureActive = false;
    g_captureMode = 0;
    g_captureMods = 0;
    if (dialog) RestoreCaptureButtons(dialog);
}

void BeginCapture(HWND dialog, int mode) {
    if (g_captureActive) return;

    g_captureActive = true;
    g_captureMode = mode;
    g_captureMods = 0;
    g_captureDialog = dialog;
    SetDlgItemTextW(dialog, mode == 1 ? IDC_SET_TRIGGER : IDC_SET_TARGET, L"Press...");
    SetDlgItemTextW(dialog, IDC_HINT, L"Press a key combo - Esc cancels");

    g_captureHook = SetWindowsHookExW(WH_KEYBOARD_LL, CaptureProc, GetModuleHandleW(nullptr), 0);
    if (!g_captureHook) {
        g_captureActive = false;
        g_captureMode = 0;
        RestoreCaptureButtons(dialog);
        MessageBoxW(dialog, L"Could not capture the keyboard shortcut.", L"KeyShorts", MB_ICONWARNING);
    }
}

void BrowseForProgram(HWND dialog) {
    wchar_t file[MAX_PATH] = {};
    std::wstring current = Trim(GetControlText(GetDlgItem(dialog, IDC_TARGET)));
    if (!current.empty()) wcsncpy_s(file, current.c_str(), _TRUNCATE);

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = dialog;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"Programs\0*.exe;*.bat;*.cmd;*.lnk;*.url\0All files\0*.*\0\0";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) SetDlgItemTextW(dialog, IDC_TARGET, file);
}

INT_PTR CALLBACK RuleDialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG: {
        g_editRule = reinterpret_cast<Rule*>(lParam);
        if (!g_editRule) {
            EndDialog(dialog, 0);
            return TRUE;
        }

        Engine::Instance().SetPaused(true);
        SetWindowTextW(dialog, g_editIsNew ? L"Add Rule" : L"Edit Rule");
        SendMessageW(dialog, WM_SETICON, ICON_SMALL,
                     reinterpret_cast<LPARAM>(LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP))));

        HWND typeCombo = GetDlgItem(dialog, IDC_TYPE);
        const int types[] = {RT_REMAP, RT_LAUNCH, RT_COMMAND, RT_SENDKEYS};
        int selectedType = 1;
        for (int i = 0; i < 4; ++i) {
            int index = static_cast<int>(SendMessageW(typeCombo, CB_ADDSTRING, 0,
                                                      reinterpret_cast<LPARAM>(RuleTypeLabel(types[i]))));
            SendMessageW(typeCombo, CB_SETITEMDATA, static_cast<WPARAM>(index), static_cast<LPARAM>(types[i]));
            if (types[i] == g_editRule->type) selectedType = i;
        }
        SendMessageW(typeCombo, CB_SETCURSEL, static_cast<WPARAM>(selectedType), 0);

        HWND scopeCombo = GetDlgItem(dialog, IDC_SCOPE);
        SendMessageW(scopeCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Global (all apps)"));
        SendMessageW(scopeCombo, CB_SETITEMDATA, 0, RS_GLOBAL);
        SendMessageW(scopeCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Only in this app"));
        SendMessageW(scopeCombo, CB_SETITEMDATA, 1, RS_APP);
        SendMessageW(scopeCombo, CB_SETCURSEL, g_editRule->scope == RS_APP ? 1 : 0, 0);

        SetDlgItemTextW(dialog, IDC_NAME, g_editRule->name.c_str());
        SetDlgItemTextW(dialog, IDC_TRIGGER, g_editRule->trigger.c_str());
        SetDlgItemTextW(dialog, IDC_TARGET, g_editRule->target.c_str());
        SetDlgItemTextW(dialog, IDC_ARGS, g_editRule->args.c_str());
        SetDlgItemTextW(dialog, IDC_APP, g_editRule->app.c_str());
        SendDlgItemMessageW(dialog, IDC_APP, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"e.g. chrome.exe"));

        SendMessageW(GetDlgItem(dialog, IDC_ENABLED), BM_SETCHECK,
                     g_editRule->enabled ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(GetDlgItem(dialog, IDC_SWALLOW), BM_SETCHECK,
                     g_editRule->swallow ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(GetDlgItem(dialog, IDC_REPEAT), BM_SETCHECK,
                     g_editRule->repeat ? BST_CHECKED : BST_UNCHECKED, 0);

        UpdateTypeFields(dialog);

        RECT dialogRect{};
        RECT parentRect{};
        GetWindowRect(dialog, &dialogRect);
        GetWindowRect(GetParent(dialog), &parentRect);
        int width = dialogRect.right - dialogRect.left;
        int height = dialogRect.bottom - dialogRect.top;
        int x = parentRect.left + ((parentRect.right - parentRect.left) - width) / 2;
        int y = parentRect.top + ((parentRect.bottom - parentRect.top) - height) / 2;
        SetWindowPos(dialog, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

        SetFocus(GetDlgItem(dialog, IDC_NAME));
        return FALSE;
    }

    case WM_APP_CAPTURED: {
        int mode = static_cast<int>(wParam);
        EndCapture(dialog);
        if (mode == 1) {
            SetDlgItemTextW(dialog, IDC_TRIGGER, g_captureText);
            SetFocus(GetDlgItem(dialog, IDC_TRIGGER));
        } else if (mode == 2) {
            SetDlgItemTextW(dialog, IDC_TARGET, g_captureText);
            SetFocus(GetDlgItem(dialog, IDC_TARGET));
        }
        return TRUE;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int notification = HIWORD(wParam);

        if (id == IDOK) {
            if (!g_editRule) return TRUE;

            Rule rule = *g_editRule;
            std::wstring name = Trim(GetControlText(GetDlgItem(dialog, IDC_NAME)));
            std::wstring trigger = Trim(GetControlText(GetDlgItem(dialog, IDC_TRIGGER)));
            std::wstring target = Trim(GetControlText(GetDlgItem(dialog, IDC_TARGET)));
            std::wstring args = Trim(GetControlText(GetDlgItem(dialog, IDC_ARGS)));
            std::wstring app = Trim(GetControlText(GetDlgItem(dialog, IDC_APP)));

            KeyCombo triggerCombo;
            if (!ParseCombo(trigger, &triggerCombo)) {
                MessageBoxW(dialog,
                            L"Trigger is empty or not a valid key combination.\n"
                            L"Examples: Ctrl+Alt+T, CapsLock, F13.",
                            L"Invalid trigger", MB_ICONWARNING);
                SetFocus(GetDlgItem(dialog, IDC_TRIGGER));
                return TRUE;
            }
            trigger = FormatCombo(triggerCombo);

            int type = static_cast<int>(SendDlgItemMessageW(
                dialog, IDC_TYPE, CB_GETITEMDATA,
                static_cast<WPARAM>(SendDlgItemMessageW(dialog, IDC_TYPE, CB_GETCURSEL, 0, 0)), 0));
            int scope = static_cast<int>(SendDlgItemMessageW(
                dialog, IDC_SCOPE, CB_GETITEMDATA,
                static_cast<WPARAM>(SendDlgItemMessageW(dialog, IDC_SCOPE, CB_GETCURSEL, 0, 0)), 0));

            if (target.empty()) {
                MessageBoxW(dialog, type == RT_REMAP ? L"Choose the key to map to." : L"Target cannot be empty.",
                            L"Invalid target", MB_ICONWARNING);
                SetFocus(GetDlgItem(dialog, IDC_TARGET));
                return TRUE;
            }

            if (type == RT_REMAP) {
                KeyCombo output;
                if (!ParseCombo(target, &output)) {
                    MessageBoxW(dialog,
                                L"Map-to value is not a valid key or combination.\n"
                                L"Examples: Esc, Ctrl+C, LCtrl.",
                                L"Invalid remap target", MB_ICONWARNING);
                    SetFocus(GetDlgItem(dialog, IDC_TARGET));
                    return TRUE;
                }
                target = FormatCombo(output);
            }

            if (scope == RS_APP) {
                if (app.empty()) {
                    MessageBoxW(dialog, L"Enter the executable name for the app scope, for example chrome.exe.",
                                L"Invalid scope", MB_ICONWARNING);
                    SetFocus(GetDlgItem(dialog, IDC_APP));
                    return TRUE;
                }
                app = FileNameOf(app);
            } else {
                app.clear();
            }

            if (name.empty()) name = trigger;

            rule.name = name;
            rule.trigger = trigger;
            rule.target = target;
            rule.args = type == RT_LAUNCH ? args : std::wstring();
            rule.type = type;
            rule.scope = scope;
            rule.app = app;
            rule.enabled = SendMessageW(GetDlgItem(dialog, IDC_ENABLED), BM_GETCHECK, 0, 0) == BST_CHECKED;
            rule.swallow = SendMessageW(GetDlgItem(dialog, IDC_SWALLOW), BM_GETCHECK, 0, 0) == BST_CHECKED;
            rule.repeat = type == RT_SENDKEYS &&
                          SendMessageW(GetDlgItem(dialog, IDC_REPEAT), BM_GETCHECK, 0, 0) == BST_CHECKED;

            *g_editRule = rule;
            EndDialog(dialog, 1);
            return TRUE;
        }

        if (id == IDCANCEL) {
            EndDialog(dialog, 0);
            return TRUE;
        }
        if (id == IDC_SET_TRIGGER) {
            BeginCapture(dialog, 1);
            return TRUE;
        }
        if (id == IDC_SET_TARGET) {
            BeginCapture(dialog, 2);
            return TRUE;
        }
        if (id == IDC_BROWSE) {
            BrowseForProgram(dialog);
            return TRUE;
        }
        if (id == IDC_TYPE && notification == CBN_SELCHANGE) {
            UpdateTypeFields(dialog);
            return TRUE;
        }
        if (id == IDC_SCOPE && notification == CBN_SELCHANGE) {
            UpdateTypeFields(dialog);
            return TRUE;
        }
        break;
    }

    case WM_CLOSE:
        EndDialog(dialog, 0);
        return TRUE;

    case WM_DESTROY:
        EndCapture(dialog);
        Engine::Instance().SetPaused(false);
        g_editRule = nullptr;
        return TRUE;
    }
    return FALSE;
}

} // namespace

bool ShowRuleDialog(HWND parent, Rule& rule, bool isNew) {
    g_editRule = &rule;
    g_editIsNew = isNew;
    g_captureHook = nullptr;
    g_captureActive = false;
    g_captureMode = 0;
    g_captureMods = 0;
    g_captureDialog = nullptr;
    g_captureText[0] = L'\0';

    INT_PTR result = DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_RULE), parent,
                                     RuleDialogProc, reinterpret_cast<LPARAM>(&rule));
    return result == 1;
}

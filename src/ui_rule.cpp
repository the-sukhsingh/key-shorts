#include "ui_rule.h"
#include "controls.h"
#include "engine.h"
#include "keys.h"
#include "resource.h"
#include "theme.h"
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

HWND g_name = nullptr;
HWND g_trigger = nullptr;
HWND g_target = nullptr;
HWND g_args = nullptr;
HWND g_app = nullptr;
HWND g_setTrigger = nullptr;
HWND g_setTarget = nullptr;
HWND g_browse = nullptr;
HWND g_type = nullptr;
HWND g_scope = nullptr;
HWND g_enabled = nullptr;
HWND g_swallow = nullptr;
HWND g_reuse = nullptr;
HWND g_crossDesktop = nullptr;
HWND g_repeat = nullptr;
HWND g_save = nullptr;
HWND g_cancel = nullptr;

HBRUSH g_wellBrush = nullptr;
HBRUSH g_disabledBrush = nullptr;
HBRUSH g_bgBrush = nullptr;

constexpr int kTypes[] = {RT_REMAP, RT_LAUNCH, RT_COMMAND, RT_SENDKEYS, RT_SPOTIFY};

std::wstring GetControlText(HWND control) {
    int length = GetWindowTextLengthW(control);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    int copied = GetWindowTextW(control, text.data(), length + 1);
    text.resize(static_cast<size_t>(copied));
    return text;
}

int S(int value) {
    return theme::S(value);
}

RECT NameWell() {
    return RECT{S(120), S(64), S(536), S(96)};
}

RECT TriggerWell() {
    return RECT{S(120), S(108), S(452), S(140)};
}

RECT TargetWell() {
    return RECT{S(120), S(196), S(452), S(228)};
}

RECT ArgsWell() {
    return RECT{S(120), S(240), S(536), S(272)};
}

RECT AppWell() {
    return RECT{S(328), S(284), S(536), S(316)};
}

RECT WellFor(HWND edit) {
    if (edit == g_name) return NameWell();
    if (edit == g_trigger) return TriggerWell();
    if (edit == g_target) return TargetWell();
    if (edit == g_args) return ArgsWell();
    if (edit == g_app) return AppWell();
    return RECT{};
}

void InvalidateWell(HWND dialog, const RECT& well) {
    RECT rect = well;
    InflateRect(&rect, S(2), S(2));
    InvalidateRect(dialog, &rect, FALSE);
}

int SelectedType() {
    int index = ui::SegmentIndex(g_type);
    if (index < 0 || index >= 5) index = 1;
    return kTypes[index];
}

bool IsAppScope() {
    return ui::SegmentIndex(g_scope) == 1;
}

const wchar_t* TargetLabel() {
    switch (SelectedType()) {
    case RT_REMAP:
        return L"Map to";
    case RT_LAUNCH:
        return L"Program";
    case RT_COMMAND:
        return L"Command";
    default:
        return L"Send keys";
    }
}

const wchar_t* TargetPlaceholder() {
    switch (SelectedType()) {
    case RT_REMAP:
        return L"Esc, Ctrl+C, LCtrl";
    case RT_LAUNCH:
        return L"wt.exe or https://example.com";
    case RT_COMMAND:
        return L"shutdown /s /t 0";
    default:
        return L"Hello{ENTER} or {CTRL}c";
    }
}

std::wstring ContextHint() {
    switch (SelectedType()) {
    case RT_REMAP:
        return L"Remaps always block the original key.";
    case RT_LAUNCH:
        return L"Reuse open windows cycles through the app's windows on each press.";
    case RT_SENDKEYS:
        return L"Repeat while held repeats the sequence while the trigger is held.";
    case RT_SPOTIFY:
        return L"Toggles Spotify play/pause. Press twice quickly for next track, three times for previous.";
    default:
        return L"";
    }
}

void SetPlaceholder(HWND edit, const wchar_t* text) {
    if (!edit) return;
    SendMessageW(edit, EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(text));
}

void UpdateTypeFields(HWND dialog) {
    int type = SelectedType();
    bool isRemap = type == RT_REMAP;
    bool isLaunch = type == RT_LAUNCH;
    bool isKeys = type == RT_SENDKEYS;
    bool isSpotify = type == RT_SPOTIFY;

    ShowWindow(g_browse, isLaunch ? SW_SHOW : SW_HIDE);
    ShowWindow(g_setTarget, isRemap ? SW_SHOW : SW_HIDE);
    ShowWindow(g_args, isLaunch ? SW_SHOW : SW_HIDE);
    ShowWindow(g_reuse, isLaunch ? SW_SHOW : SW_HIDE);
    ShowWindow(g_repeat, isKeys ? SW_SHOW : SW_HIDE);
    ShowWindow(g_crossDesktop, isLaunch ? SW_SHOW : SW_HIDE);
    ShowWindow(g_target, isSpotify ? SW_HIDE : SW_SHOW);
    ShowWindow(g_app, isSpotify ? SW_HIDE : SW_SHOW);
    ShowWindow(g_scope, isSpotify ? SW_HIDE : SW_SHOW);
    EnableWindow(g_crossDesktop, isLaunch && ui::CheckOn(g_reuse));

    EnableWindow(g_swallow, !isRemap);
    if (isRemap) ui::SetCheckOn(g_swallow, true);

    EnableWindow(g_app, IsAppScope());
    SetPlaceholder(g_target, TargetPlaceholder());
    InvalidateRect(g_target, nullptr, TRUE);

    InvalidateRect(dialog, nullptr, FALSE);
}

void FinishCapture(int vk, UINT mods) {
    KeyCombo combo{vk, mods};
    std::wstring text = FormatCombo(combo);
    wcsncpy_s(g_captureText, text.c_str(), _TRUNCATE);
    int mode = g_captureMode;
    g_captureMode = 0;
    PostMessageW(g_captureDialog, WM_APP_CAPTURED, static_cast<WPARAM>(mode), 0);
}

LRESULT CALLBACK CaptureProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code != HC_ACTION || !g_captureActive) return CallNextHookEx(nullptr, code, wParam, lParam);
    if (wParam != WM_KEYDOWN && wParam != WM_KEYUP && wParam != WM_SYSKEYDOWN &&
        wParam != WM_SYSKEYUP) {
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
        if (g_captureMode != 0) FinishCapture(vk, g_captureMods);
        return 1;
    }

    if (mod != 0) {
        UINT held = g_captureMods & ~mod;
        g_captureMods = held;
        // Nothing else was pressed while this modifier was held, so the user is
        // setting the modifier itself (e.g. LCtrl) as the trigger or target.
        if (g_captureMode != 0) FinishCapture(vk, held);
        return 1;
    }
    return 1;
}

void RestoreCaptureButtons(HWND dialog) {
    if (g_setTrigger) ui::SetButtonText(g_setTrigger, L"Set");
    if (g_setTarget) ui::SetButtonText(g_setTarget, L"Set key");
    if (dialog) {
        InvalidateWell(dialog, TriggerWell());
        RECT hintRect{S(24), S(404), S(536), S(426)};
        InvalidateRect(dialog, &hintRect, FALSE);
    }
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
    ui::SetButtonText(mode == 1 ? g_setTrigger : g_setTarget, L"Press...");
    RECT hintRect{S(24), S(404), S(536), S(426)};
    InvalidateRect(dialog, &hintRect, FALSE);

    g_captureHook = SetWindowsHookExW(WH_KEYBOARD_LL, CaptureProc, GetModuleHandleW(nullptr), 0);
    if (!g_captureHook) {
        g_captureActive = false;
        g_captureMode = 0;
        RestoreCaptureButtons(dialog);
        MessageBoxW(dialog, L"Could not capture the keyboard shortcut.", L"KeyShorts",
                    MB_ICONWARNING);
    }
}

void BrowseForProgram(HWND dialog) {
    wchar_t file[MAX_PATH] = {};
    std::wstring current = Trim(GetControlText(g_target));
    if (!current.empty()) wcsncpy_s(file, current.c_str(), _TRUNCATE);

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = dialog;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"Programs\0*.exe;*.bat;*.cmd;*.lnk;*.url\0All files\0*.*\0\0";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) {
        SetWindowTextW(g_target, file);
        InvalidateWell(dialog, TargetWell());
    }
}

void PaintDialog(HWND dialog) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(dialog, &ps);
    RECT client{};
    GetClientRect(dialog, &client);
    theme::FillSolid(dc, client, theme::kBg);

    int margin = S(24);
    int right = client.right - margin;

    theme::DrawLabel(dc, g_editIsNew ? L"Add rule" : L"Edit rule",
                     RECT{margin, S(20), right, S(46)}, theme::Font().title, theme::kText);
    theme::FillSolid(dc, RECT{margin, S(56), right, S(57)}, theme::kHairlineSoft);

    auto label = [&](int y, const wchar_t* text) {
        theme::DrawLabel(dc, text, RECT{margin, y, S(104), y + S(32)}, theme::Font().meta,
                         theme::kTextSecondary);
    };
    auto well = [&](const RECT& rect, HWND edit) {
        bool enabled = IsWindowEnabled(edit) != FALSE;
        bool focused = GetFocus() == edit;
        theme::FillRound(dc, rect, S(8), enabled ? theme::kRaised : RGB(0x12, 0x15, 0x1D));
        theme::StrokeRound(dc, rect, S(8),
                           focused ? theme::kAccent
                                   : (enabled ? theme::kHairline : theme::kHairlineSoft));
    };

    label(S(64), L"Name");
    well(NameWell(), g_name);

    label(S(108), L"Trigger");
    well(TriggerWell(), g_trigger);

    label(S(152), L"Action");

    if (SelectedType() != RT_SPOTIFY) {
        label(S(196), TargetLabel());
        well(TargetWell(), g_target);
    }

    if (IsWindowVisible(g_args)) {
        label(S(240), L"Arguments");
        well(ArgsWell(), g_args);
    }

    if (SelectedType() != RT_SPOTIFY) {
        label(S(284), L"Applies to");
        well(AppWell(), g_app);
    }

    theme::FillSolid(dc, RECT{margin, S(332), right, S(333)}, theme::kHairlineSoft);
    theme::FillSolid(dc, RECT{margin, S(436), right, S(437)}, theme::kHairlineSoft);

    std::wstring hint = g_captureActive ? L"Press a key or combo. Esc cancels." : ContextHint();
    if (!hint.empty()) {
        theme::DrawLabel(dc, hint, RECT{margin, S(404), right, S(424)}, theme::Font().meta,
                         g_captureActive ? theme::kAccent : theme::kTextMuted);
    }

    EndPaint(dialog, &ps);
}

void LayoutDialog(HWND dialog) {
    auto place = [&](HWND edit, const RECT& rect) {
        MoveWindow(edit, rect.left + S(10), rect.top + S(6), (rect.right - rect.left) - S(20), S(20),
                   TRUE);
    };

    place(g_name, NameWell());
    place(g_trigger, TriggerWell());
    place(g_target, TargetWell());
    place(g_args, ArgsWell());
    place(g_app, AppWell());
}

HWND MakeEdit(HWND dialog, int id, HFONT font) {
    HWND edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                0, 0, 10, 10, dialog,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                GetModuleHandleW(nullptr), nullptr);
    if (edit) SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return edit;
}

void SaveRule(HWND dialog) {
    if (!g_editRule) return;

    Rule rule = *g_editRule;
    std::wstring name = Trim(GetControlText(g_name));
    std::wstring trigger = Trim(GetControlText(g_trigger));
    std::wstring target = Trim(GetControlText(g_target));
    std::wstring args = Trim(GetControlText(g_args));
    std::wstring app = Trim(GetControlText(g_app));
    int type = SelectedType();
    int scope = IsAppScope() ? RS_APP : RS_GLOBAL;

    KeyCombo triggerCombo;
    if (!ParseCombo(trigger, &triggerCombo)) {
        MessageBoxW(dialog,
                    L"Trigger is empty or not a valid key combination.\n"
                    L"Examples: Ctrl+Alt+T, CapsLock, F13.",
                    L"Invalid trigger", MB_ICONWARNING);
        SetFocus(g_trigger);
        return;
    }
    trigger = FormatCombo(triggerCombo);

    if (target.empty() && type != RT_SPOTIFY) {
        MessageBoxW(dialog,
                    type == RT_REMAP ? L"Choose the key to map to." : L"Target cannot be empty.",
                    L"Invalid target", MB_ICONWARNING);
        SetFocus(g_target);
        return;
    }

    if (type == RT_REMAP) {
        KeyCombo output;
        if (!ParseCombo(target, &output)) {
            MessageBoxW(dialog,
                        L"Map-to value is not a valid key or combination.\n"
                        L"Examples: Esc, Ctrl+C, LCtrl.",
                        L"Invalid remap target", MB_ICONWARNING);
            SetFocus(g_target);
            return;
        }
        target = FormatCombo(output);
    }

    if (scope == RS_APP) {
        if (app.empty()) {
            MessageBoxW(dialog,
                        L"Enter the executable name for the app scope, for example chrome.exe.",
                        L"Invalid scope", MB_ICONWARNING);
            SetFocus(g_app);
            return;
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
    rule.enabled = ui::CheckOn(g_enabled);
    rule.swallow = ui::CheckOn(g_swallow);
    rule.repeat = type == RT_SENDKEYS && ui::CheckOn(g_repeat);
    rule.focusExisting = type == RT_LAUNCH && ui::CheckOn(g_reuse);
    rule.reuseAcrossDesktops = type == RT_LAUNCH && ui::CheckOn(g_crossDesktop);

    *g_editRule = rule;
    EndDialog(dialog, 1);
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
        SetWindowTextW(dialog, g_editIsNew ? L"Add rule" : L"Edit rule");
        SendMessageW(dialog, WM_SETICON, ICON_SMALL,
                     reinterpret_cast<LPARAM>(
                         LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP))));
        theme::ApplyDarkChrome(dialog);

        RECT windowRect{0, 0, S(560), S(500)};
        AdjustWindowRectEx(&windowRect, GetWindowLongW(dialog, GWL_STYLE), FALSE,
                           GetWindowLongW(dialog, GWL_EXSTYLE));
        int width = windowRect.right - windowRect.left;
        int height = windowRect.bottom - windowRect.top;
        RECT parentRect{};
        GetWindowRect(GetParent(dialog), &parentRect);
        int x = parentRect.left + ((parentRect.right - parentRect.left) - width) / 2;
        int y = parentRect.top + ((parentRect.bottom - parentRect.top) - height) / 2;
        SetWindowPos(dialog, nullptr, x, y, width, height, SWP_NOZORDER);

        g_wellBrush = CreateSolidBrush(theme::kRaised);
        g_disabledBrush = CreateSolidBrush(RGB(0x12, 0x15, 0x1D));
        g_bgBrush = CreateSolidBrush(theme::kBg);

        g_name = MakeEdit(dialog, IDC_NAME, theme::Font().body);
        g_trigger = MakeEdit(dialog, IDC_TRIGGER, theme::Font().mono);
        g_setTrigger = ui::Button(dialog, IDC_SET_TRIGGER, L"Set", nullptr,
                                  ui::ButtonStyle::Secondary, S(460), S(108), S(76), S(32));
        g_type = ui::Segments(dialog, IDC_TYPE,
                               {L"Remap", L"Launch", L"Command", L"Send keys", L"Spotify"}, S(120), S(152),
                               S(416), S(32));
        g_target = MakeEdit(dialog, IDC_TARGET, theme::Font().body);
        g_browse = ui::Button(dialog, IDC_BROWSE, L"Browse", nullptr, ui::ButtonStyle::Secondary,
                              S(460), S(196), S(76), S(32));
        g_setTarget = ui::Button(dialog, IDC_SET_TARGET, L"Set key", nullptr,
                                 ui::ButtonStyle::Secondary, S(460), S(196), S(76), S(32));
        g_args = MakeEdit(dialog, IDC_ARGS, theme::Font().body);
        g_scope = ui::Segments(dialog, IDC_SCOPE, {L"Global", L"This app"}, S(120), S(284), S(200),
                               S(32));
        g_app = MakeEdit(dialog, IDC_APP, theme::Font().body);
        g_enabled = ui::Check(dialog, IDC_ENABLED, L"Enabled", S(24), S(348), S(160), S(20));
        g_swallow = ui::Check(dialog, IDC_SWALLOW, L"Block the original key", S(224), S(348),
                              S(280), S(20));
        g_reuse = ui::Check(dialog, IDC_REUSE, L"Reuse open windows", S(24), S(376), S(260), S(20));
        g_repeat = ui::Check(dialog, IDC_REPEAT, L"Repeat while held", S(224), S(376), S(240), S(20));
        g_crossDesktop = ui::Check(dialog, IDC_CROSSDESKTOP, L"Switch desktops when reusing", S(224), S(376), S(312), S(20));
        g_cancel = ui::Button(dialog, IDCANCEL, L"Cancel", nullptr, ui::ButtonStyle::Secondary,
                              S(352), S(448), S(88), S(36));
        g_save = ui::Button(dialog, IDOK, L"Save", nullptr, ui::ButtonStyle::Primary, S(448),
                            S(448), S(88), S(36));

        LayoutDialog(dialog);

        SetPlaceholder(g_trigger, L"Not set");
        SetPlaceholder(g_args, L"Optional");
        SetPlaceholder(g_app, L"chrome.exe");

        SetWindowTextW(g_name, g_editRule->name.c_str());
        SetWindowTextW(g_trigger, g_editRule->trigger.c_str());
        SetWindowTextW(g_target, g_editRule->target.c_str());
        SetWindowTextW(g_args, g_editRule->args.c_str());
        SetWindowTextW(g_app, g_editRule->app.c_str());

        int typeIndex = 1;
        for (int i = 0; i < 5; ++i) {
            if (kTypes[i] == g_editRule->type) typeIndex = i;
        }
        ui::SetSegmentIndex(g_type, typeIndex);
        ui::SetSegmentIndex(g_scope, g_editRule->scope == RS_APP ? 1 : 0);

        ui::SetCheckOn(g_enabled, g_editRule->enabled);
        ui::SetCheckOn(g_swallow, g_editRule->swallow);
        ui::SetCheckOn(g_reuse, g_editRule->focusExisting);
        ui::SetCheckOn(g_crossDesktop, g_editRule->reuseAcrossDesktops);
        ui::SetCheckOn(g_repeat, g_editRule->repeat);

        UpdateTypeFields(dialog);
        SendMessageW(dialog, DM_SETDEFID, IDOK, 0);

        SetFocus(g_name);
        SendMessageW(g_name, EM_SETSEL, 0, -1);
        return FALSE;
    }

    case WM_APP_CAPTURED: {
        int mode = static_cast<int>(wParam);
        EndCapture(dialog);
        if (mode == 1) {
            SetWindowTextW(g_trigger, g_captureText);
            InvalidateWell(dialog, TriggerWell());
            SetFocus(g_trigger);
            SendMessageW(g_trigger, EM_SETSEL, 0, -1);
        } else if (mode == 2) {
            SetWindowTextW(g_target, g_captureText);
            InvalidateWell(dialog, TargetWell());
            SetFocus(g_target);
            SendMessageW(g_target, EM_SETSEL, 0, -1);
        }
        return TRUE;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int notification = HIWORD(wParam);

        if (id == IDOK) {
            SaveRule(dialog);
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
        if (id == IDC_REUSE && notification == BN_CLICKED) {
            EnableWindow(g_crossDesktop, IsWindowVisible(g_crossDesktop) && ui::CheckOn(g_reuse));
            return TRUE;
        }
        if ((id == IDC_TYPE || id == IDC_SCOPE) && notification == CBN_SELCHANGE) {
            UpdateTypeFields(dialog);
            return TRUE;
        }
        if (notification == EN_SETFOCUS || notification == EN_KILLFOCUS) {
            HWND edit = reinterpret_cast<HWND>(lParam);
            InvalidateWell(dialog, WellFor(edit));
            return TRUE;
        }
        if (notification == EN_CHANGE) {
            HWND edit = reinterpret_cast<HWND>(lParam);
            InvalidateWell(dialog, WellFor(edit));
            return TRUE;
        }
        break;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        PaintDialog(dialog);
        return TRUE;

    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkColor(dc, theme::kRaised);
        SetTextColor(dc, theme::kText);
        return reinterpret_cast<LRESULT>(g_wellBrush);
    }

    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        HWND control = reinterpret_cast<HWND>(lParam);
        if (control == g_app && !IsWindowEnabled(g_app)) {
            SetBkColor(dc, RGB(0x12, 0x15, 0x1D));
            SetTextColor(dc, theme::kTextMuted);
            return reinterpret_cast<LRESULT>(g_disabledBrush);
        }
        SetBkColor(dc, theme::kBg);
        SetTextColor(dc, theme::kTextSecondary);
        return reinterpret_cast<LRESULT>(g_bgBrush);
    }

    case WM_CLOSE:
        EndDialog(dialog, 0);
        return TRUE;

    case WM_DESTROY:
        EndCapture(dialog);
        Engine::Instance().SetPaused(false);
        if (g_wellBrush) DeleteObject(g_wellBrush);
        if (g_disabledBrush) DeleteObject(g_disabledBrush);
        if (g_bgBrush) DeleteObject(g_bgBrush);
        g_wellBrush = nullptr;
        g_disabledBrush = nullptr;
        g_bgBrush = nullptr;
        g_editRule = nullptr;
        return TRUE;

    default:
        break;
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

#include "ui.h"
#include "config.h"
#include "controls.h"
#include "engine.h"
#include "resource.h"
#include "theme.h"
#include "ui_rule.h"
#include "util.h"

#include <commctrl.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>

namespace {

HINSTANCE g_instance = nullptr;
HWND g_hwnd = nullptr;
HWND g_list = nullptr;
HWND g_add = nullptr;
HWND g_edit = nullptr;
HWND g_delete = nullptr;
HWND g_reload = nullptr;
HWND g_cfgButton = nullptr;
HWND g_master = nullptr;
HWND g_startup = nullptr;

Config g_cfg;
int g_invalid = 0;
bool g_engineOk = true;

bool g_trayAdded = false;
HICON g_trayIcon = nullptr;
bool g_ownsTrayIcon = false;
NOTIFYICONDATAW g_nid{};

constexpr wchar_t kListClass[] = L"KeyShortsList";

constexpr wchar_t kGlyphAdd[] = L"\xE710";
constexpr wchar_t kGlyphEdit[] = L"\xE70F";
constexpr wchar_t kGlyphDelete[] = L"\xE74D";
constexpr wchar_t kGlyphReload[] = L"\xE72C";
constexpr wchar_t kGlyphFolder[] = L"\xE8B7";
constexpr wchar_t kGlyphKeyboard[] = L"\xE765";
constexpr wchar_t kGlyphCheck[] = L"\xE73E";

int g_sel = -1;
int g_hover = -1;
int g_scroll = 0;
bool g_scrollHot = false;
bool g_dragScroll = false;
int g_dragOffset = 0;
RECT g_emptyButton{};
RECT g_thumbRect{};
RECT g_trackRect{};

void LayoutMain();
void RefreshList();
void UpdateControls();
void UpdateStatus();
void UpdateSelectionButtons();
void AddRule();
void EditSelectedRule();
void DeleteSelectedRule();
void ToggleRuleEnabled(int index);
bool PersistConfig();
void ApplyEngine();
void OnListAdd();
void OnListActivate();
void OnListToggle(int index);

int HeaderH() {
    return theme::S(34);
}

int RowH() {
    return theme::S(44);
}

int ContentH() {
    return HeaderH() + static_cast<int>(g_cfg.rules.size()) * RowH();
}

int MaxScroll(int viewHeight) {
    return std::max(0, ContentH() - viewHeight);
}

void ClampScroll() {
    if (!g_list) return;
    RECT rc{};
    GetClientRect(g_list, &rc);
    g_scroll = std::min(g_scroll, MaxScroll(rc.bottom));
    if (g_scroll < 0) g_scroll = 0;
}

void EnsureVisible(int index) {
    if (!g_list) return;
    RECT rc{};
    GetClientRect(g_list, &rc);
    int top = HeaderH() + index * RowH() - g_scroll;
    int bottom = top + RowH();
    if (top < HeaderH()) {
        g_scroll = index * RowH();
    } else if (bottom > rc.bottom) {
        g_scroll = HeaderH() + (index + 1) * RowH() - rc.bottom;
    }
    ClampScroll();
}

int SelectedRuleIndex() {
    if (g_sel < 0 || g_sel >= static_cast<int>(g_cfg.rules.size())) return -1;
    return g_sel;
}

void SelectRule(int index) {
    if (index < 0 || index >= static_cast<int>(g_cfg.rules.size())) return;
    g_sel = index;
    EnsureVisible(index);
    if (g_list) InvalidateRect(g_list, nullptr, FALSE);
    UpdateSelectionButtons();
}

bool WriteStartupEntry(bool enabled) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return false;

    bool ok = true;
    if (enabled) {
        std::wstring command = L"\"" + ExePath() + L"\" --tray";
        LONG result = RegSetValueExW(key, kRunValue, 0, REG_SZ,
                                     reinterpret_cast<const BYTE*>(command.c_str()),
                                     static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
        ok = result == ERROR_SUCCESS;
    } else {
        LONG result = RegDeleteValueW(key, kRunValue);
        ok = result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
    }
    RegCloseKey(key);
    return ok;
}

bool PersistConfig() {
    bool ok = g_cfg.Save();
    if (!ok && g_hwnd) {
        MessageBoxW(g_hwnd, L"KeyShorts could not write its configuration file.\n"
                            L"Check permissions for %APPDATA%\\KeyShorts.",
                    kAppName, MB_ICONWARNING);
    }
    return ok;
}

struct ActionParts {
    std::wstring verb;
    std::wstring detail;
};

ActionParts SplitAction(const Rule& rule) {
    switch (rule.type) {
    case RT_REMAP:
        return {L"Remap to", rule.target.empty() ? std::wstring(L"(not set)") : rule.target};
    case RT_LAUNCH: {
        std::wstring detail = rule.target.empty() ? std::wstring(L"(not set)") : FileNameOf(rule.target);
        if (!rule.args.empty()) detail += L" " + rule.args;
        return {L"Launch", detail};
    }
    case RT_COMMAND:
        return {L"Run", rule.target.empty() ? std::wstring(L"(not set)") : rule.target};
    case RT_SENDKEYS:
        return {L"Send", rule.target.empty() ? std::wstring(L"(not set)") : rule.target};
    default:
        return {L"", L"(unknown)"};
    }
}

std::wstring ScopeText(const Rule& rule) {
    if (rule.scope != RS_APP) return L"Global";
    std::wstring app = Trim(rule.app);
    return app.empty() ? std::wstring(L"(no app)") : app;
}

void StatusInfo(std::wstring& text, COLORREF& dot) {
    if (!g_engineOk) {
        dot = theme::kDanger;
        text = L"Keyboard hook unavailable";
        return;
    }
    if (!g_cfg.enabled) {
        dot = theme::kTextMuted;
        text = L"Paused";
        return;
    }
    if (g_invalid > 0) {
        dot = theme::kDanger;
        text = std::to_wstring(g_invalid);
        text += g_invalid == 1 ? L" rule needs attention" : L" rules need attention";
        return;
    }
    dot = theme::kAccent;
    text = L"Active";
}

void UpdateTrayTip() {
    if (!g_trayAdded) return;
    std::wstring tip = L"KeyShorts: " + std::to_wstring(g_cfg.rules.size()) +
                       (g_cfg.rules.size() == 1 ? L" rule, " : L" rules, ") +
                       (g_cfg.enabled ? L"active" : L"paused");
    wcsncpy_s(g_nid.szTip, tip.c_str(), _TRUNCATE);
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void UpdateStatus() {
    if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
    UpdateTrayTip();
}

void UpdateControls() {
    if (g_master) ui::SetSwitchOn(g_master, g_cfg.enabled);
    if (g_startup) ui::SetSwitchOn(g_startup, g_cfg.runAtStartup);
    UpdateStatus();
}

void ApplyEngine() {
    g_invalid = Engine::Instance().Reload(g_cfg, g_cfg.enabled);
    UpdateStatus();
    if (g_list) InvalidateRect(g_list, nullptr, FALSE);
}

void SetMasterEnabled(bool enabled) {
    g_cfg.enabled = enabled;
    Engine::Instance().SetEnabled(enabled);
    if (g_master) ui::SetSwitchOn(g_master, enabled);
    PersistConfig();
    UpdateStatus();
}

void SetStartupEnabled(bool enabled) {
    if (!WriteStartupEntry(enabled)) {
        MessageBoxW(g_hwnd, L"Could not update the Windows startup entry.", kAppName, MB_ICONWARNING);
        if (g_startup) ui::SetSwitchOn(g_startup, g_cfg.runAtStartup);
        return;
    }
    g_cfg.runAtStartup = enabled;
    if (g_startup) ui::SetSwitchOn(g_startup, enabled);
    PersistConfig();
}

void UpdateSelectionButtons() {
    bool hasSelection = SelectedRuleIndex() >= 0;
    if (g_edit) EnableWindow(g_edit, hasSelection);
    if (g_delete) EnableWindow(g_delete, hasSelection);
}

void RefreshList() {
    if (!g_list) return;
    int count = static_cast<int>(g_cfg.rules.size());
    if (g_sel >= count) g_sel = count - 1;
    if (g_sel < -1) g_sel = -1;
    if (count == 0) g_sel = -1;
    if (g_hover >= count) g_hover = -1;
    ClampScroll();
    InvalidateRect(g_list, nullptr, FALSE);
    UpdateSelectionButtons();
    UpdateStatus();
}

void AddTrayIcon() {
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_APP_TRAY;

    int cx = GetSystemMetrics(SM_CXSMICON);
    int cy = GetSystemMetrics(SM_CYSMICON);
    g_trayIcon = static_cast<HICON>(LoadImageW(g_instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, cx, cy,
                                               LR_DEFAULTCOLOR));
    if (g_trayIcon) {
        g_ownsTrayIcon = true;
    } else {
        g_trayIcon = LoadIconW(nullptr, IDI_APPLICATION);
        g_ownsTrayIcon = false;
    }
    g_nid.hIcon = g_trayIcon;
    wcscpy_s(g_nid.szTip, L"KeyShorts");

    g_trayAdded = Shell_NotifyIconW(NIM_ADD, &g_nid) != FALSE;
    if (g_trayAdded) UpdateTrayTip();
}

void RemoveTrayIcon() {
    if (g_trayAdded) {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        g_trayAdded = false;
    }
    if (g_ownsTrayIcon && g_trayIcon) DestroyIcon(g_trayIcon);
    g_trayIcon = nullptr;
    g_ownsTrayIcon = false;
}

void ShowMain() {
    if (!g_hwnd) return;
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    if (GetWindowPlacement(g_hwnd, &placement) && placement.showCmd == SW_SHOWMINIMIZED) {
        placement.showCmd = SW_SHOWNORMAL;
        SetWindowPlacement(g_hwnd, &placement);
    }
    ShowWindow(g_hwnd, SW_SHOW);
    SetForegroundWindow(g_hwnd);
}

void HideToTray() {
    if (!g_trayAdded) {
        DestroyWindow(g_hwnd);
        return;
    }
    ShowWindow(g_hwnd, SW_HIDE);
}

void OpenConfigFolder() {
    std::wstring path = ConfigPath();
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) PersistConfig();
    std::wstring params = L"/select,\"" + path + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", params.c_str(), nullptr, SW_SHOWNORMAL);
}

void OpenConfigFile() {
    std::wstring path = ConfigPath();
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) PersistConfig();
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void ReloadConfigFromDisk() {
    Config fresh;
    if (!fresh.Load()) {
        MessageBoxW(g_hwnd, L"Could not read the configuration file.", kAppName, MB_ICONWARNING);
        return;
    }
    g_cfg = std::move(fresh);
    ApplyEngine();
    RefreshList();
    UpdateControls();
}

void AddRule() {
    Rule rule;
    rule.name = L"New shortcut";
    rule.type = RT_LAUNCH;
    if (!ShowRuleDialog(g_hwnd, rule, true)) return;

    g_cfg.rules.push_back(rule);
    PersistConfig();
    ApplyEngine();
    RefreshList();
    SelectRule(static_cast<int>(g_cfg.rules.size()) - 1);
}

void EditSelectedRule() {
    int index = SelectedRuleIndex();
    if (index < 0) return;

    Rule rule = g_cfg.rules[static_cast<size_t>(index)];
    if (!ShowRuleDialog(g_hwnd, rule, false)) return;

    g_cfg.rules[static_cast<size_t>(index)] = rule;
    PersistConfig();
    ApplyEngine();
    RefreshList();
    SelectRule(index);
}

void DeleteSelectedRule() {
    int index = SelectedRuleIndex();
    if (index < 0) return;

    std::wstring name = g_cfg.rules[static_cast<size_t>(index)].name;
    std::wstring message = L"Delete \"" + name + L"\"?";
    if (MessageBoxW(g_hwnd, message.c_str(), L"Delete rule", MB_ICONQUESTION | MB_YESNO) != IDYES) return;

    g_cfg.rules.erase(g_cfg.rules.begin() + index);
    PersistConfig();
    ApplyEngine();
    RefreshList();
}

void ToggleRuleEnabled(int index) {
    if (index < 0 || index >= static_cast<int>(g_cfg.rules.size())) return;
    Rule& rule = g_cfg.rules[static_cast<size_t>(index)];
    rule.enabled = !rule.enabled;
    PersistConfig();
    ApplyEngine();
    RefreshList();
}

void OnListAdd() {
    AddRule();
}

void OnListActivate() {
    EditSelectedRule();
}

void OnListToggle(int index) {
    ToggleRuleEnabled(index);
}

void ShowTrayMenu() {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    AppendMenuW(menu, MF_STRING, IDM_SHOW, L"Open KeyShorts");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (g_cfg.enabled ? MF_CHECKED : 0), IDM_ENABLED, L"Enabled");
    AppendMenuW(menu, MF_STRING | (g_cfg.runAtStartup ? MF_CHECKED : 0), IDM_STARTUP,
                L"Start with Windows");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_CONFIGFOLDER, L"Open config folder");
    AppendMenuW(menu, MF_STRING, IDM_CONFIG, L"Open config file");
    AppendMenuW(menu, MF_STRING, IDM_RELOAD, L"Reload config");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Exit");

    POINT point{};
    GetCursorPos(&point);
    SetForegroundWindow(g_hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN, point.x, point.y, 0, g_hwnd,
                   nullptr);
    DestroyMenu(menu);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);
}

struct Columns {
    int check = 0;
    int name = 0;
    int nameW = 0;
    int trig = 0;
    int trigW = 0;
    int act = 0;
    int actW = 0;
    int scope = 0;
    int scopeW = 0;
};

Columns ListColumns(int width) {
    Columns c;
    c.check = theme::S(16);
    c.name = theme::S(44);
    c.nameW = std::max(theme::S(150), width * 24 / 100);
    c.trig = c.name + c.nameW + theme::S(16);
    c.trigW = std::max(theme::S(170), width * 26 / 100);
    c.scopeW = std::max(theme::S(100), width * 15 / 100);
    c.scope = width - theme::S(16) - c.scopeW;
    c.act = c.trig + c.trigW + theme::S(16);
    c.actW = std::max(theme::S(100), c.scope - theme::S(16) - c.act);
    return c;
}

int HitRow(int y) {
    if (y < HeaderH()) return -1;
    int index = (y - HeaderH() + g_scroll) / RowH();
    if (index < 0 || index >= static_cast<int>(g_cfg.rules.size())) return -1;
    return index;
}

void PaintList(HDC dc, int width, int height) {
    if (width <= 0 || height <= 0) return;

    theme::FillSolid(dc, RECT{0, 0, width, height}, theme::kBg);
    RECT card{0, 0, width, height};
    theme::FillRound(dc, card, theme::S(12), theme::kCard);

    HRGN clip = CreateRoundRectRgn(0, 0, width + 1, height + 1, theme::S(12), theme::S(12));
    int saved = SaveDC(dc);
    SelectClipRgn(dc, clip);

    Columns columns = ListColumns(width);
    int header = HeaderH();
    int rowHeight = RowH();

    theme::DrawLabel(dc, L"Name", RECT{columns.name, 0, columns.name + columns.nameW, header},
                     theme::Font().meta, theme::kTextMuted);
    theme::DrawLabel(dc, L"Trigger", RECT{columns.trig, 0, columns.trig + columns.trigW, header},
                     theme::Font().meta, theme::kTextMuted);
    theme::DrawLabel(dc, L"Action", RECT{columns.act, 0, columns.act + columns.actW, header},
                     theme::Font().meta, theme::kTextMuted);
    theme::DrawLabel(dc, L"Scope", RECT{columns.scope, 0, columns.scope + columns.scopeW, header},
                     theme::Font().meta, theme::kTextMuted,
                     DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    theme::FillSolid(dc, RECT{0, header - 1, width, header}, theme::kHairlineSoft);

    int count = static_cast<int>(g_cfg.rules.size());
    for (int i = 0; i < count; ++i) {
        int y = header + i * rowHeight - g_scroll;
        if (y + rowHeight <= header || y >= height) continue;

        const Rule& rule = g_cfg.rules[static_cast<size_t>(i)];
        bool selected = i == g_sel;
        bool hovered = i == g_hover;
        bool enabledRule = rule.enabled;

        if (selected) {
            theme::FillSolid(dc, RECT{0, y, width, y + rowHeight}, theme::kRowSelected);
            theme::FillSolid(dc, RECT{0, y, theme::S(2), y + rowHeight}, theme::kAccent);
        } else if (hovered) {
            theme::FillSolid(dc, RECT{0, y, width, y + rowHeight}, theme::kRowHover);
        }

        if (i < count - 1) {
            theme::FillSolid(dc, RECT{columns.name, y + rowHeight - 1, width - theme::S(16),
                                      y + rowHeight},
                             theme::kSeparator);
        }

        int box = theme::S(16);
        int boxY = y + (rowHeight - box) / 2;
        RECT boxRect{columns.check, boxY, columns.check + box, boxY + box};
        theme::FillRound(dc, boxRect, theme::S(4), enabledRule ? theme::kAccent : theme::kRaised);
        if (!enabledRule) theme::StrokeRound(dc, boxRect, theme::S(4), theme::kHairline);
        if (enabledRule) {
            theme::DrawGlyph(dc, boxRect, kGlyphCheck, theme::Font().iconSmall, theme::kAccentInk);
        }

        std::wstring name = rule.name.empty() ? std::wstring(L"(unnamed)") : rule.name;
        theme::DrawLabel(dc, name, RECT{columns.name, y, columns.name + columns.nameW, y + rowHeight},
                         theme::Font().body, enabledRule ? theme::kText : theme::kTextMuted);

        theme::DrawKeycaps(dc, rule.trigger, columns.trig, y + (rowHeight - theme::S(22)) / 2,
                           columns.trigW, enabledRule);

        ActionParts parts = SplitAction(rule);
        SIZE verbSize = theme::MeasureText(dc, parts.verb, theme::Font().meta);
        theme::DrawLabel(dc, parts.verb,
                         RECT{columns.act, y, columns.act + verbSize.cx, y + rowHeight},
                         theme::Font().meta, theme::kTextMuted);
        theme::DrawLabel(dc, parts.detail,
                         RECT{columns.act + verbSize.cx + theme::S(6), y,
                              columns.act + columns.actW, y + rowHeight},
                         theme::Font().body,
                         enabledRule ? theme::kTextSecondary : theme::kTextMuted);

        theme::DrawLabel(dc, ScopeText(rule),
                         RECT{columns.scope, y, columns.scope + columns.scopeW, y + rowHeight},
                         theme::Font().meta,
                         rule.scope == RS_APP ? theme::kTextSecondary : theme::kTextMuted,
                         DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }

    g_emptyButton = RECT{};
    g_thumbRect = RECT{};
    g_trackRect = RECT{};

    if (count == 0) {
        int cx = width / 2;
        int cy = header + (height - header) / 2;
        RECT key{cx - theme::S(28), cy - theme::S(92), cx + theme::S(28), cy - theme::S(36)};
        theme::FillRound(dc, key, theme::S(12), theme::kRaised);
        theme::DrawGlyph(dc, key, kGlyphKeyboard, theme::Font().icon, theme::kTextMuted);

        theme::DrawLabel(dc, L"No rules yet",
                         RECT{cx - theme::S(220), cy - theme::S(22), cx + theme::S(220),
                              cy + theme::S(4)},
                         theme::Font().wordmark, theme::kText,
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        theme::DrawLabel(dc, L"Add a rule to launch an app, send keys or remap a key.",
                         RECT{cx - theme::S(260), cy + theme::S(10), cx + theme::S(260),
                              cy + theme::S(32)},
                         theme::Font().meta, theme::kTextMuted,
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        g_emptyButton = RECT{cx - theme::S(58), cy + theme::S(48), cx + theme::S(58),
                             cy + theme::S(80)};
        theme::FillRound(dc, g_emptyButton, theme::S(8), theme::kAccent);
        theme::DrawLabel(dc, L"Add rule", g_emptyButton, theme::Font().bodyBold, theme::kAccentInk,
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    int maxScroll = MaxScroll(height);
    if (maxScroll > 0) {
        int trackWidth = theme::S(4);
        int trackX = width - theme::S(8);
        int trackY = header + theme::S(4);
        int trackH = height - trackY - theme::S(4);
        g_trackRect = RECT{trackX, trackY, trackX + trackWidth, trackY + trackH};
        theme::FillRound(dc, g_trackRect, trackWidth / 2, RGB(0x0E, 0x11, 0x18));

        int thumbH = std::max(theme::S(24), trackH * height / std::max(height, ContentH()));
        int thumbY = trackY + (trackH - thumbH) * g_scroll / maxScroll;
        g_thumbRect = RECT{trackX, thumbY, trackX + trackWidth, thumbY + thumbH};
        theme::FillRound(dc, g_thumbRect, trackWidth / 2,
                         (g_scrollHot || g_dragScroll) ? RGB(0x3A, 0x42, 0x52)
                                                       : RGB(0x2A, 0x31, 0x40));
    }

    RestoreDC(dc, saved);
    DeleteObject(clip);

    theme::StrokeRound(dc, card, theme::S(12), theme::kHairline);
}

void PaintMain(HWND hwnd) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd, &ps);
    RECT client{};
    GetClientRect(hwnd, &client);
    theme::FillSolid(dc, client, theme::kBg);

    int margin = theme::S(24);

    int textX = margin;
    theme::DrawLabel(dc, L"KeyShorts",
                      RECT{textX, theme::S(12), textX + theme::S(320), theme::S(34)},
                      theme::Font().wordmark, theme::kText);

    std::wstring status;
    COLORREF dotColor = theme::kAccent;
    StatusInfo(status, dotColor);
    theme::FillEllipse(dc, RECT{textX, theme::S(39), textX + theme::S(6), theme::S(45)}, dotColor);
    theme::DrawLabel(dc, status,
                     RECT{textX + theme::S(12), theme::S(33), textX + theme::S(360),
                          theme::S(51)},
                     theme::Font().meta, theme::kTextSecondary);

    int switchX = client.right - margin - theme::S(36);
    theme::DrawLabel(dc, L"Enabled",
                     RECT{margin, theme::S(14), switchX - theme::S(8), theme::S(34)},
                     theme::Font().meta, theme::kTextSecondary,
                     DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    theme::DrawLabel(dc, L"Start with Windows",
                     RECT{margin, theme::S(38), switchX - theme::S(8), theme::S(58)},
                     theme::Font().meta, theme::kTextSecondary,
                     DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    int footerY = client.bottom - theme::S(38);
    std::wstring count = std::to_wstring(g_cfg.rules.size());
    count += g_cfg.rules.size() == 1 ? L" rule" : L" rules";
    SIZE countSize = theme::MeasureText(dc, count, theme::Font().mono);
    theme::DrawLabel(dc, count, RECT{margin, footerY, margin + countSize.cx, footerY + theme::S(18)},
                     theme::Font().mono, theme::kTextMuted);
    if (g_invalid > 0) {
        std::wstring invalid = L", " + std::to_wstring(g_invalid) + L" invalid";
        theme::DrawLabel(dc, invalid,
                         RECT{margin + countSize.cx, footerY,
                              margin + countSize.cx + theme::S(180), footerY + theme::S(18)},
                         theme::Font().mono, theme::kDanger);
    }

    EndPaint(hwnd, &ps);
}

void LayoutMain() {
    if (!g_hwnd || !g_list) return;
    RECT client{};
    GetClientRect(g_hwnd, &client);
    int width = client.right;
    int height = client.bottom;
    if (width <= 0 || height <= 0) return;

    int margin = theme::S(24);
    int right = width - margin;

    int switchW = theme::S(36);
    int switchH = theme::S(20);
    MoveWindow(g_master, right - switchW, theme::S(14), switchW, switchH, TRUE);
    MoveWindow(g_startup, right - switchW, theme::S(38), switchW, switchH, TRUE);

    int toolbarY = theme::S(72);
    int buttonH = theme::S(32);
    int x = margin;
    MoveWindow(g_add, x, toolbarY, theme::S(116), buttonH, TRUE);
    x += theme::S(116) + theme::S(8);
    MoveWindow(g_edit, x, toolbarY, theme::S(84), buttonH, TRUE);
    x += theme::S(84) + theme::S(8);
    MoveWindow(g_delete, x, toolbarY, theme::S(92), buttonH, TRUE);

    int configW = theme::S(132);
    int reloadW = theme::S(92);
    MoveWindow(g_cfgButton, right - configW, toolbarY, configW, buttonH, TRUE);
    MoveWindow(g_reload, right - configW - theme::S(8) - reloadW, toolbarY, reloadW, buttonH, TRUE);

    int listY = theme::S(112);
    int listHeight = height - listY - theme::S(56);
    if (listHeight < theme::S(80)) listHeight = theme::S(80);
    MoveWindow(g_list, margin, listY, width - margin * 2, listHeight, TRUE);
}

void OnCreate(HWND hwnd) {
    g_hwnd = hwnd;
    theme::Init();
    theme::ApplyDarkChrome(hwnd);

    g_master = ui::Switch(hwnd, IDC_MASTER, 0, 0);
    g_startup = ui::Switch(hwnd, IDC_STARTUP, 0, 0);

    g_add = ui::Button(hwnd, IDC_ADD, L"Add rule", kGlyphAdd, ui::ButtonStyle::Primary, 0, 0, 10, 10);
    g_edit = ui::Button(hwnd, IDC_EDIT, L"Edit", kGlyphEdit, ui::ButtonStyle::Ghost, 0, 0, 10, 10);
    g_delete =
        ui::Button(hwnd, IDC_DELETE, L"Delete", kGlyphDelete, ui::ButtonStyle::Ghost, 0, 0, 10, 10);
    g_reload =
        ui::Button(hwnd, IDC_RELOAD, L"Reload", kGlyphReload, ui::ButtonStyle::Ghost, 0, 0, 10, 10);
    g_cfgButton = ui::Button(hwnd, IDC_OPENCFG, L"Config folder", kGlyphFolder,
                             ui::ButtonStyle::Ghost, 0, 0, 10, 10);

    g_list = CreateWindowExW(0, kListClass, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 10, 10,
                             hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LIST)),
                             g_instance, nullptr);

    RefreshList();
    UpdateControls();
    LayoutMain();
    SetFocus(g_list);
}

LRESULT CALLBACK ListProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        if (rc.right > 0 && rc.bottom > 0) {
            HDC memory = CreateCompatibleDC(dc);
            HBITMAP bitmap = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
            HGDIOBJ old = SelectObject(memory, bitmap);
            PaintList(memory, rc.right, rc.bottom);
            BitBlt(dc, 0, 0, rc.right, rc.bottom, memory, 0, 0, SRCCOPY);
            SelectObject(memory, old);
            DeleteObject(bitmap);
            DeleteDC(memory);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SIZE:
        ClampScroll();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_LBUTTONDOWN: {
        SetFocus(hwnd);
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);

        if (g_cfg.rules.empty()) {
            POINT point{x, y};
            if (PtInRect(&g_emptyButton, point)) OnListAdd();
            return 0;
        }

        POINT point{x, y};
        if (g_thumbRect.right > g_thumbRect.left && PtInRect(&g_thumbRect, point)) {
            g_dragScroll = true;
            g_dragOffset = y - g_thumbRect.top;
            SetCapture(hwnd);
            return 0;
        }
        if (g_trackRect.right > g_trackRect.left && PtInRect(&g_trackRect, point)) {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            int maxScroll = MaxScroll(rc.bottom);
            if (maxScroll > 0) {
                g_scroll += (y < g_thumbRect.top ? -1 : 1) * rc.bottom / 2;
                ClampScroll();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        int index = HitRow(y);
        if (index >= 0) {
            Columns columns = ListColumns(g_list ? [&] {
                RECT rc{};
                GetClientRect(hwnd, &rc);
                return rc.right;
            }() : 0);
            if (x >= columns.check && x <= columns.check + theme::S(16)) {
                SelectRule(index);
                OnListToggle(index);
                return 0;
            }
            SelectRule(index);
        }
        return 0;
    }

    case WM_LBUTTONDBLCLK: {
        int index = HitRow(GET_Y_LPARAM(lParam));
        if (index >= 0) {
            SelectRule(index);
            OnListActivate();
        }
        return 0;
    }

    case WM_LBUTTONUP:
        if (g_dragScroll) {
            g_dragScroll = false;
            ReleaseCapture();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);

        if (g_dragScroll) {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            int maxScroll = MaxScroll(rc.bottom);
            int trackH = g_trackRect.bottom - g_trackRect.top;
            int thumbH = g_thumbRect.bottom - g_thumbRect.top;
            int span = trackH - thumbH;
            if (span > 0 && maxScroll > 0) {
                int position = y - g_dragOffset - g_trackRect.top;
                position = std::clamp(position, 0, span);
                g_scroll = position * maxScroll / span;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        POINT point{x, y};
        int index = HitRow(y);
        bool scrollHot = g_thumbRect.right > g_thumbRect.left && PtInRect(&g_thumbRect, point);
        if (index != g_hover || scrollHot != g_scrollHot) {
            g_hover = index;
            g_scrollHot = scrollHot;
            TRACKMOUSEEVENT track{};
            track.cbSize = sizeof(track);
            track.dwFlags = TME_LEAVE;
            track.hwndTrack = hwnd;
            TrackMouseEvent(&track);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        if (g_hover != -1 || g_scrollHot) {
            g_hover = -1;
            g_scrollHot = false;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        g_scroll -= (delta / WHEEL_DELTA) * RowH() * 3;
        ClampScroll();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_KEYDOWN: {
        int count = static_cast<int>(g_cfg.rules.size());
        if (count == 0) return 0;

        switch (wParam) {
        case VK_UP:
            SelectRule(std::max(0, g_sel - 1));
            return 0;
        case VK_DOWN:
            SelectRule(g_sel < 0 ? 0 : std::min(count - 1, g_sel + 1));
            return 0;
        case VK_HOME:
            SelectRule(0);
            return 0;
        case VK_END:
            SelectRule(count - 1);
            return 0;
        case VK_SPACE:
            if (g_sel >= 0) OnListToggle(g_sel);
            return 0;
        case VK_RETURN:
            if (g_sel >= 0) OnListActivate();
            return 0;
        case VK_DELETE:
            if (g_sel >= 0) DeleteSelectedRule();
            return 0;
        default:
            break;
        }
        return 0;
    }

    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTCHARS;

    default:
        break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK MainProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        OnCreate(hwnd);
        return 0;

    case WM_SIZE:
        LayoutMain();
        return 0;

    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = theme::S(780);
        info->ptMinTrackSize.y = theme::S(480);
        return 0;
    }

    case WM_PAINT:
        PaintMain(hwnd);
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int notification = HIWORD(wParam);
        switch (id) {
        case IDC_ADD:
            AddRule();
            return 0;
        case IDC_EDIT:
            EditSelectedRule();
            return 0;
        case IDC_DELETE:
            DeleteSelectedRule();
            return 0;
        case IDC_RELOAD:
            ReloadConfigFromDisk();
            return 0;
        case IDC_OPENCFG:
            OpenConfigFolder();
            return 0;
        case IDC_MASTER:
            if (notification == BN_CLICKED) SetMasterEnabled(ui::SwitchOn(g_master));
            return 0;
        case IDC_STARTUP:
            if (notification == BN_CLICKED) SetStartupEnabled(ui::SwitchOn(g_startup));
            return 0;
        case IDM_SHOW:
            ShowMain();
            return 0;
        case IDM_ENABLED:
            SetMasterEnabled(!g_cfg.enabled);
            return 0;
        case IDM_STARTUP:
            SetStartupEnabled(!g_cfg.runAtStartup);
            return 0;
        case IDM_CONFIGFOLDER:
            OpenConfigFolder();
            return 0;
        case IDM_CONFIG:
            OpenConfigFile();
            return 0;
        case IDM_RELOAD:
            ReloadConfigFromDisk();
            return 0;
        case IDM_EXIT:
            DestroyWindow(hwnd);
            return 0;
        default:
            break;
        }
        break;
    }

    case WM_APP_TRAY: {
        UINT event = LOWORD(lParam);
        if (event == WM_LBUTTONUP || event == WM_LBUTTONDBLCLK) {
            ShowMain();
        } else if (event == WM_RBUTTONUP || event == WM_CONTEXTMENU) {
            ShowTrayMenu();
        }
        return 0;
    }

    case WM_APP_SHOW:
        ShowMain();
        return 0;

    case WM_CLOSE:
        HideToTray();
        return 0;

    case WM_QUERYENDSESSION:
        return TRUE;

    case WM_DESTROY:
        RemoveTrayIcon();
        Engine::Instance().Stop();
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace

int RunApp(HINSTANCE instance, int showCmd, bool startHidden) {
    g_instance = instance;

    HDC screen = GetDC(nullptr);
    UINT dpi = static_cast<UINT>(GetDeviceCaps(screen, LOGPIXELSX));
    ReleaseDC(nullptr, screen);
    if (dpi == 0) dpi = 96;
    theme::SetDpi(dpi);

    bool existed = g_cfg.Load();
    if (!existed) {
        g_cfg.enabled = true;
        g_cfg.runAtStartup = true;
        g_cfg.Save();
    }
    WriteStartupEntry(g_cfg.runAtStartup);

    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    ui::RegisterControls(instance);

    WNDCLASSEXW listClass{};
    listClass.cbSize = sizeof(listClass);
    listClass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    listClass.lpfnWndProc = ListProc;
    listClass.hInstance = instance;
    listClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    listClass.hbrBackground = nullptr;
    listClass.lpszClassName = kListClass;
    if (!RegisterClassExW(&listClass)) {
        Engine::Instance().Stop();
        return 1;
    }

    g_invalid = Engine::Instance().Reload(g_cfg, g_cfg.enabled);
    g_engineOk = Engine::Instance().Start();

    HBRUSH background = CreateSolidBrush(theme::kBg);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = MainProc;
    windowClass.hInstance = instance;
    windowClass.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                                     GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON),
                                                     LR_DEFAULTCOLOR));
    windowClass.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                                       GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                                                       LR_DEFAULTCOLOR));
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = background;
    windowClass.lpszClassName = kWindowClass;

    if (!RegisterClassExW(&windowClass)) {
        DeleteObject(background);
        Engine::Instance().Stop();
        return 1;
    }

    RECT windowRect{0, 0, theme::S(900), theme::S(580)};
    AdjustWindowRectEx(&windowRect, WS_OVERLAPPEDWINDOW, FALSE, 0);
    int windowWidth = windowRect.right - windowRect.left;
    int windowHeight = windowRect.bottom - windowRect.top;

    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int workWidth = work.right - work.left;
    int workHeight = work.bottom - work.top;
    if (windowWidth > workWidth) windowWidth = workWidth;
    if (windowHeight > workHeight) windowHeight = workHeight;
    int x = work.left + (workWidth - windowWidth) / 2;
    int y = work.top + (workHeight - windowHeight) / 2;

    g_hwnd = CreateWindowExW(0, kWindowClass, L"KeyShorts", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             x, y, windowWidth, windowHeight, nullptr, nullptr, instance, nullptr);
    if (!g_hwnd) {
        DeleteObject(background);
        Engine::Instance().Stop();
        return 1;
    }

    AddTrayIcon();
    UpdateControls();

    if (startHidden) {
        ShowWindow(g_hwnd, SW_HIDE);
    } else {
        ShowWindow(g_hwnd, showCmd == SW_SHOWMAXIMIZED ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL);
    }

    if (!g_engineOk) {
        MessageBoxW(g_hwnd,
                    L"KeyShorts could not install its global keyboard hook.\n"
                    L"Shortcuts will not fire until this is resolved.",
                    kAppName, MB_ICONWARNING);
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (IsDialogMessageW(g_hwnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Engine::Instance().Stop();
    theme::Shutdown();
    DeleteObject(background);

    return static_cast<int>(msg.wParam);
}

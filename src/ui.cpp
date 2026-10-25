#include "ui.h"
#include "config.h"
#include "engine.h"
#include "resource.h"
#include "ui_rule.h"
#include "util.h"

#include <commctrl.h>
#include <shellapi.h>

namespace {

HINSTANCE g_instance = nullptr;
HWND g_hwnd = nullptr;
HWND g_list = nullptr;
HWND g_title = nullptr;
HWND g_subtitle = nullptr;
HWND g_master = nullptr;
HWND g_startup = nullptr;
HWND g_add = nullptr;
HWND g_edit = nullptr;
HWND g_delete = nullptr;
HWND g_reload = nullptr;
HWND g_openCfg = nullptr;
HWND g_hide = nullptr;
HWND g_status = nullptr;

HFONT g_font = nullptr;
HFONT g_fontBold = nullptr;
HFONT g_fontTitle = nullptr;
HFONT g_fontList = nullptr;
HFONT g_fontListBold = nullptr;
HBRUSH g_white = nullptr;
UINT g_dpi = 96;

Config g_cfg;
int g_invalid = 0;
bool g_updating = false;
bool g_engineOk = true;

bool g_trayAdded = false;
HICON g_trayIcon = nullptr;
bool g_ownsTrayIcon = false;
NOTIFYICONDATAW g_nid{};

int S(int value) {
    return MulDiv(value, static_cast<int>(g_dpi), 96);
}

HFONT CreateUiFont(int points, int weight) {
    LOGFONTW lf{};
    lf.lfHeight = -MulDiv(points, static_cast<int>(g_dpi), 72);
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    return CreateFontIndirectW(&lf);
}

void SetControlFont(HWND control, HFONT font) {
    if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

std::wstring GetControlText(HWND control) {
    int length = GetWindowTextLengthW(control);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    int copied = GetWindowTextW(control, text.data(), length + 1);
    text.resize(static_cast<size_t>(copied));
    return text;
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

std::wstring ActionText(const Rule& rule) {
    std::wstring text;
    switch (rule.type) {
    case RT_REMAP:
        text = L"Remap to " + (rule.target.empty() ? std::wstring(L"(not set)") : rule.target);
        break;
    case RT_LAUNCH:
        text = L"Launch " + (rule.target.empty() ? std::wstring(L"(not set)") : FileNameOf(rule.target));
        if (!rule.args.empty()) text += L" " + rule.args;
        break;
    case RT_COMMAND:
        text = L"Run " + (rule.target.empty() ? std::wstring(L"(not set)") : rule.target);
        break;
    case RT_SENDKEYS:
        text = L"Keys " + (rule.target.empty() ? std::wstring(L"(not set)") : rule.target);
        break;
    default:
        text = L"(unknown)";
        break;
    }
    if (text.size() > 90) text = text.substr(0, 87) + L"...";
    return text;
}

std::wstring ScopeText(const Rule& rule) {
    if (rule.scope != RS_APP) return L"Global";
    std::wstring app = Trim(rule.app);
    return L"Only in " + (app.empty() ? std::wstring(L"(no app)") : app);
}

int SelectedRuleIndex() {
    int item = ListView_GetNextItem(g_list, -1, LVNI_SELECTED);
    if (item < 0) return -1;
    LVITEMW data{};
    data.mask = LVIF_PARAM;
    data.iItem = item;
    if (!ListView_GetItem(g_list, &data)) return -1;
    return static_cast<int>(data.lParam);
}

void SelectRule(int index) {
    if (index < 0 || index >= static_cast<int>(g_cfg.rules.size())) return;
    ListView_SetItemState(g_list, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(g_list, index, FALSE);
}

void SetSubItemText(int item, int column, const std::wstring& text) {
    ListView_SetItemText(g_list, item, column, const_cast<wchar_t*>(text.c_str()));
}

void UpdateSelectionButtons() {
    bool hasSelection = g_list && ListView_GetSelectedCount(g_list) > 0;
    if (g_edit) EnableWindow(g_edit, hasSelection);
    if (g_delete) EnableWindow(g_delete, hasSelection);
}

void RefreshList() {
    if (!g_list) return;
    g_updating = true;
    SendMessageW(g_list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g_list);

    for (int i = 0; i < static_cast<int>(g_cfg.rules.size()); ++i) {
        const Rule& rule = g_cfg.rules[static_cast<size_t>(i)];
        std::wstring name = rule.name.empty() ? std::wstring(L"(unnamed)") : rule.name;

        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = i;
        item.pszText = const_cast<wchar_t*>(name.c_str());
        item.lParam = i;
        int inserted = ListView_InsertItem(g_list, &item);
        if (inserted < 0) continue;

        SetSubItemText(inserted, 1, rule.trigger);
        SetSubItemText(inserted, 2, ActionText(rule));
        SetSubItemText(inserted, 3, ScopeText(rule));
        ListView_SetCheckState(g_list, inserted, rule.enabled ? TRUE : FALSE);
    }

    SendMessageW(g_list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_list, nullptr, TRUE);
    g_updating = false;
    UpdateSelectionButtons();
}

void UpdateStatus() {
    if (!g_status) return;

    std::wstring text = std::to_wstring(g_cfg.rules.size());
    text += g_cfg.rules.size() == 1 ? L" rule" : L" rules";
    if (g_invalid > 0) text += L" \u00B7 " + std::to_wstring(g_invalid) + L" invalid";
    text += g_cfg.enabled ? L" \u00B7 Active" : L" \u00B7 Paused";
    if (!g_engineOk) text += L" \u00B7 hook unavailable";
    SetWindowTextW(g_status, text.c_str());
    InvalidateRect(g_status, nullptr, TRUE);
}

void UpdateTrayTip() {
    if (!g_trayAdded) return;
    std::wstring tip = L"KeyShorts - " + std::to_wstring(g_cfg.rules.size()) +
                       (g_cfg.enabled ? L" rules - active" : L" rules - paused");
    wcsncpy_s(g_nid.szTip, tip.c_str(), _TRUNCATE);
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void UpdateControls() {
    if (g_master) {
        SendMessageW(g_master, BM_SETCHECK, g_cfg.enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    if (g_startup) {
        SendMessageW(g_startup, BM_SETCHECK, g_cfg.runAtStartup ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    UpdateStatus();
    UpdateTrayTip();
}

void ApplyEngine() {
    g_invalid = Engine::Instance().Reload(g_cfg, g_cfg.enabled);
    UpdateStatus();
    UpdateTrayTip();
}

void SetMasterEnabled(bool enabled) {
    g_cfg.enabled = enabled;
    Engine::Instance().SetEnabled(enabled);
    if (g_master) SendMessageW(g_master, BM_SETCHECK, enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    PersistConfig();
    UpdateStatus();
    UpdateTrayTip();
}

void SetStartupEnabled(bool enabled) {
    if (!WriteStartupEntry(enabled)) {
        MessageBoxW(g_hwnd, L"Could not update the Windows startup entry.", kAppName, MB_ICONWARNING);
        if (g_startup) {
            SendMessageW(g_startup, BM_SETCHECK, g_cfg.runAtStartup ? BST_CHECKED : BST_UNCHECKED, 0);
        }
        return;
    }
    g_cfg.runAtStartup = enabled;
    if (g_startup) SendMessageW(g_startup, BM_SETCHECK, enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    PersistConfig();
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
    if (index < 0 || index >= static_cast<int>(g_cfg.rules.size())) return;

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
    if (index < 0 || index >= static_cast<int>(g_cfg.rules.size())) return;

    std::wstring name = g_cfg.rules[static_cast<size_t>(index)].name;
    std::wstring message = L"Delete \"" + name + L"\"?";
    if (MessageBoxW(g_hwnd, message.c_str(), L"Delete rule", MB_ICONQUESTION | MB_YESNO) != IDYES) return;

    g_cfg.rules.erase(g_cfg.rules.begin() + index);
    PersistConfig();
    ApplyEngine();
    RefreshList();
}

void ShowTrayMenu() {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    AppendMenuW(menu, MF_STRING, IDM_SHOW, L"Open KeyShorts");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (g_cfg.enabled ? MF_CHECKED : 0), IDM_ENABLED, L"Enabled");
    AppendMenuW(menu, MF_STRING | (g_cfg.runAtStartup ? MF_CHECKED : 0), IDM_STARTUP, L"Run at startup");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_CONFIG, L"Open config file");
    AppendMenuW(menu, MF_STRING, IDM_RELOAD, L"Reload config");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Exit");

    POINT point{};
    GetCursorPos(&point);
    SetForegroundWindow(g_hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN, point.x, point.y, 0, g_hwnd, nullptr);
    DestroyMenu(menu);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);
}

void LayoutMain(HWND hwnd) {
    if (!g_list) return;
    RECT client{};
    GetClientRect(hwnd, &client);
    int width = client.right;
    int height = client.bottom;
    if (width <= 0 || height <= 0) return;

    int margin = S(20);
    int right = width - margin;

    MoveWindow(g_title, margin + S(12), S(12), S(420), S(30), TRUE);
    MoveWindow(g_subtitle, margin + S(12), S(44), S(560), S(20), TRUE);
    MoveWindow(g_master, right - S(180), S(18), S(180), S(22), TRUE);
    MoveWindow(g_startup, right - S(180), S(44), S(180), S(22), TRUE);

    int toolbarY = S(88);
    MoveWindow(g_add, margin, toolbarY, S(96), S(30), TRUE);
    MoveWindow(g_edit, margin + S(104), toolbarY, S(76), S(30), TRUE);
    MoveWindow(g_delete, margin + S(188), toolbarY, S(86), S(30), TRUE);
    MoveWindow(g_openCfg, right - S(112), toolbarY, S(112), S(30), TRUE);
    MoveWindow(g_reload, right - S(112) - S(8) - S(84), toolbarY, S(84), S(30), TRUE);

    int listY = S(134);
    int listHeight = height - listY - S(62);
    if (listHeight < S(60)) listHeight = S(60);
    int listWidth = width - 2 * margin;
    MoveWindow(g_list, margin, listY, listWidth, listHeight, TRUE);

    MoveWindow(g_status, margin, height - S(40), listWidth - S(100), S(20), TRUE);
    MoveWindow(g_hide, right - S(84), height - S(46), S(84), S(30), TRUE);

    RECT listClient{};
    GetClientRect(g_list, &listClient);
    int colName = S(230);
    int colTrigger = S(180);
    int colScope = S(180);
    int colAction = listClient.right - colName - colTrigger - colScope - S(4);
    if (colAction < S(160)) colAction = S(160);
    ListView_SetColumnWidth(g_list, 0, colName);
    ListView_SetColumnWidth(g_list, 1, colTrigger);
    ListView_SetColumnWidth(g_list, 2, colAction);
    ListView_SetColumnWidth(g_list, 3, colScope);
}

void OnCreate(HWND hwnd) {
    g_hwnd = hwnd;

    g_font = CreateUiFont(9, FW_NORMAL);
    g_fontBold = CreateUiFont(9, FW_SEMIBOLD);
    g_fontTitle = CreateUiFont(19, FW_SEMIBOLD);
    g_fontList = CreateUiFont(10, FW_NORMAL);
    g_fontListBold = CreateUiFont(10, FW_SEMIBOLD);

    auto makeStatic = [&](int id, const wchar_t* text, HFONT font) {
        HWND control = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
                                       0, 0, 10, 10, hwnd,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_instance, nullptr);
        SetControlFont(control, font);
        return control;
    };
    auto makeButton = [&](int id, const wchar_t* text, HFONT font) {
        HWND control = CreateWindowExW(0, L"BUTTON", text,
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                       0, 0, 10, 10, hwnd,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_instance, nullptr);
        SetControlFont(control, font);
        return control;
    };
    auto makeCheck = [&](int id, const wchar_t* text, HFONT font) {
        HWND control = CreateWindowExW(0, L"BUTTON", text,
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                       0, 0, 10, 10, hwnd,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_instance, nullptr);
        SetControlFont(control, font);
        return control;
    };

    g_title = makeStatic(IDC_TITLE, L"KeyShorts", g_fontTitle);
    g_subtitle = makeStatic(IDC_SUBTITLE,
                            L"Global shortcuts \u00B7 per-app rules \u00B7 remaps \u00B7 macros", g_font);
    g_master = makeCheck(IDC_MASTER, L"Enabled", g_font);
    g_startup = makeCheck(IDC_STARTUP, L"Run at startup", g_font);

    g_add = makeButton(IDC_ADD, L"+ Add rule", g_fontBold);
    g_edit = makeButton(IDC_EDIT, L"Edit", g_font);
    g_delete = makeButton(IDC_DELETE, L"Delete", g_font);
    g_reload = makeButton(IDC_RELOAD, L"Reload", g_font);
    g_openCfg = makeButton(IDC_OPENCFG, L"Open config", g_font);
    g_hide = makeButton(IDC_HIDE, L"Hide to tray", g_font);

    g_status = makeStatic(IDC_STATUS, L"", g_font);

    g_list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL |
                                 LVS_SHOWSELALWAYS | LVS_NOSORTHEADER,
                             0, 0, 10, 10, hwnd,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LIST)), g_instance, nullptr);
    ListView_SetExtendedListViewStyle(g_list,
                                      LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER |
                                          LVS_EX_CHECKBOXES | LVS_EX_LABELTIP);
    ListView_SetBkColor(g_list, RGB(255, 255, 255));
    ListView_SetTextBkColor(g_list, RGB(255, 255, 255));
    SetControlFont(g_list, g_fontList);

    const wchar_t* columns[] = {L"Name", L"Trigger", L"Action", L"Scope"};
    for (int i = 0; i < 4; ++i) {
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        column.iSubItem = i;
        column.pszText = const_cast<wchar_t*>(columns[i]);
        column.cx = S(100);
        ListView_InsertColumn(g_list, i, &column);
    }
    HWND header = ListView_GetHeader(g_list);
    if (header) SetControlFont(header, g_fontListBold);

    RefreshList();
    UpdateControls();
    LayoutMain(hwnd);
    SetFocus(g_list);
}

LRESULT CALLBACK MainProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        OnCreate(hwnd);
        return 0;

    case WM_SIZE:
        LayoutMain(hwnd);
        return 0;

    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = S(800);
        info->ptMinTrackSize.y = S(520);
        return 0;
    }

    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        HWND control = reinterpret_cast<HWND>(lParam);
        SetBkColor(dc, RGB(255, 255, 255));
        SetBkMode(dc, TRANSPARENT);
        if (control == g_status) {
            COLORREF color = RGB(107, 114, 128);
            if (g_cfg.enabled && g_engineOk && g_invalid == 0) color = RGB(21, 128, 61);
            else if (g_cfg.enabled && g_invalid > 0) color = RGB(180, 83, 9);
            SetTextColor(dc, color);
        } else if (control == g_subtitle) {
            SetTextColor(dc, RGB(107, 114, 128));
        } else {
            SetTextColor(dc, RGB(17, 24, 39));
        }
        return reinterpret_cast<LRESULT>(g_white);
    }

    case WM_CTLCOLORBTN: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkColor(dc, RGB(255, 255, 255));
        return reinterpret_cast<LRESULT>(g_white);
    }

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT client{};
        GetClientRect(hwnd, &client);

        HBRUSH accent = CreateSolidBrush(RGB(37, 99, 235));
        RECT bar{S(20), S(16), S(24), S(52)};
        FillRect(dc, &bar, accent);
        DeleteObject(accent);

        HPEN pen = CreatePen(PS_SOLID, 1, RGB(229, 231, 235));
        HGDIOBJ oldPen = SelectObject(dc, pen);
        MoveToEx(dc, S(20), S(78), nullptr);
        LineTo(dc, client.right - S(20), S(78));
        MoveToEx(dc, S(20), client.bottom - S(60), nullptr);
        LineTo(dc, client.right - S(20), client.bottom - S(60));
        SelectObject(dc, oldPen);
        DeleteObject(pen);

        EndPaint(hwnd, &ps);
        return 0;
    }

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
        case IDC_HIDE:
            HideToTray();
            return 0;
        case IDC_MASTER:
            if (notification == BN_CLICKED) {
                SetMasterEnabled(SendMessageW(g_master, BM_GETCHECK, 0, 0) == BST_CHECKED);
            }
            return 0;
        case IDC_STARTUP:
            if (notification == BN_CLICKED) {
                SetStartupEnabled(SendMessageW(g_startup, BM_GETCHECK, 0, 0) == BST_CHECKED);
            }
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

    case WM_NOTIFY: {
        auto* headerInfo = reinterpret_cast<NMHDR*>(lParam);
        if (headerInfo->idFrom != IDC_LIST) break;

        if (headerInfo->code == LVN_ITEMCHANGED) {
            auto* listInfo = reinterpret_cast<NMLISTVIEW*>(lParam);
            UpdateSelectionButtons();
            if (!g_updating && listInfo->iItem >= 0 &&
                listInfo->iItem < static_cast<int>(g_cfg.rules.size())) {
                bool checked = ListView_GetCheckState(g_list, listInfo->iItem) != FALSE;
                Rule& rule = g_cfg.rules[static_cast<size_t>(listInfo->iItem)];
                if (rule.enabled != checked) {
                    rule.enabled = checked;
                    PersistConfig();
                    ApplyEngine();
                }
            }
            return 0;
        }
        if (headerInfo->code == NM_DBLCLK) {
            EditSelectedRule();
            return 0;
        }
        if (headerInfo->code == LVN_KEYDOWN) {
            auto* keyInfo = reinterpret_cast<NMLVKEYDOWN*>(lParam);
            if (keyInfo->wVKey == VK_DELETE) DeleteSelectedRule();
            else if (keyInfo->wVKey == VK_RETURN) EditSelectedRule();
            return 0;
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
    g_dpi = static_cast<UINT>(GetDeviceCaps(screen, LOGPIXELSX));
    ReleaseDC(nullptr, screen);
    if (g_dpi == 0) g_dpi = 96;

    bool existed = g_cfg.Load();
    if (!existed) {
        g_cfg.enabled = true;
        g_cfg.runAtStartup = true;
        g_cfg.Save();
    }
    WriteStartupEntry(g_cfg.runAtStartup);

    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    g_white = CreateSolidBrush(RGB(255, 255, 255));

    g_invalid = Engine::Instance().Reload(g_cfg, g_cfg.enabled);
    g_engineOk = Engine::Instance().Start();

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
    windowClass.hbrBackground = g_white;
    windowClass.lpszClassName = kWindowClass;

    if (!RegisterClassExW(&windowClass)) {
        Engine::Instance().Stop();
        return 1;
    }

    RECT windowRect{0, 0, S(900), S(580)};
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

    g_hwnd = CreateWindowExW(0, kWindowClass, L"KeyShorts", WS_OVERLAPPEDWINDOW,
                             x, y, windowWidth, windowHeight, nullptr, nullptr, instance, nullptr);
    if (!g_hwnd) {
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
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Engine::Instance().Stop();
    if (g_font) DeleteObject(g_font);
    if (g_fontBold) DeleteObject(g_fontBold);
    if (g_fontTitle) DeleteObject(g_fontTitle);
    if (g_fontList) DeleteObject(g_fontList);
    if (g_fontListBold) DeleteObject(g_fontListBold);
    if (g_white) DeleteObject(g_white);

    return static_cast<int>(msg.wParam);
}

#include "keys.h"
#include "util.h"

#include <cstdio>
#include <cstdlib>

namespace {

struct NamedKey {
    int vk;
    const wchar_t* name;
};

const NamedKey kNamed[] = {
    {VK_BACK, L"Backspace"},
    {VK_TAB, L"Tab"},
    {VK_CLEAR, L"Clear"},
    {VK_RETURN, L"Enter"},
    {VK_PAUSE, L"Pause"},
    {VK_CAPITAL, L"CapsLock"},
    {VK_ESCAPE, L"Esc"},
    {VK_SPACE, L"Space"},
    {VK_PRIOR, L"PageUp"},
    {VK_NEXT, L"PageDown"},
    {VK_END, L"End"},
    {VK_HOME, L"Home"},
    {VK_LEFT, L"Left"},
    {VK_UP, L"Up"},
    {VK_RIGHT, L"Right"},
    {VK_DOWN, L"Down"},
    {VK_SELECT, L"Select"},
    {VK_PRINT, L"Print"},
    {VK_EXECUTE, L"Execute"},
    {VK_SNAPSHOT, L"PrintScreen"},
    {VK_INSERT, L"Insert"},
    {VK_DELETE, L"Delete"},
    {VK_HELP, L"Help"},
    {VK_LWIN, L"LWin"},
    {VK_RWIN, L"RWin"},
    {VK_APPS, L"Menu"},
    {VK_SLEEP, L"Sleep"},
    {VK_MULTIPLY, L"NumMultiply"},
    {VK_ADD, L"NumAdd"},
    {VK_SEPARATOR, L"NumSeparator"},
    {VK_SUBTRACT, L"NumSubtract"},
    {VK_DECIMAL, L"NumDecimal"},
    {VK_DIVIDE, L"NumDivide"},
    {VK_NUMLOCK, L"NumLock"},
    {VK_SCROLL, L"ScrollLock"},
    {VK_LSHIFT, L"LShift"},
    {VK_RSHIFT, L"RShift"},
    {VK_LCONTROL, L"LCtrl"},
    {VK_RCONTROL, L"RCtrl"},
    {VK_LMENU, L"LAlt"},
    {VK_RMENU, L"RAlt"},
    {VK_BROWSER_BACK, L"BrowserBack"},
    {VK_BROWSER_FORWARD, L"BrowserForward"},
    {VK_BROWSER_REFRESH, L"BrowserRefresh"},
    {VK_BROWSER_STOP, L"BrowserStop"},
    {VK_BROWSER_SEARCH, L"BrowserSearch"},
    {VK_BROWSER_FAVORITES, L"BrowserFavorites"},
    {VK_BROWSER_HOME, L"BrowserHome"},
    {VK_VOLUME_MUTE, L"VolumeMute"},
    {VK_VOLUME_DOWN, L"VolumeDown"},
    {VK_VOLUME_UP, L"VolumeUp"},
    {VK_MEDIA_NEXT_TRACK, L"MediaNext"},
    {VK_MEDIA_PREV_TRACK, L"MediaPrev"},
    {VK_MEDIA_STOP, L"MediaStop"},
    {VK_MEDIA_PLAY_PAUSE, L"MediaPlayPause"},
    {VK_LAUNCH_MAIL, L"LaunchMail"},
    {VK_LAUNCH_MEDIA_SELECT, L"LaunchMedia"},
    {VK_LAUNCH_APP1, L"LaunchApp1"},
    {VK_LAUNCH_APP2, L"LaunchApp2"},
    {VK_OEM_1, L";"},
    {VK_OEM_PLUS, L"="},
    {VK_OEM_COMMA, L","},
    {VK_OEM_MINUS, L"-"},
    {VK_OEM_PERIOD, L"."},
    {VK_OEM_2, L"/"},
    {VK_OEM_3, L"`"},
    {VK_OEM_4, L"["},
    {VK_OEM_5, L"\\"},
    {VK_OEM_6, L"]"},
    {VK_OEM_7, L"'"},
    {VK_OEM_8, L"Oem8"},
    {VK_OEM_102, L"Oem102"},
};

const NamedKey kAliases[] = {
    {VK_ESCAPE, L"Escape"},
    {VK_RETURN, L"Return"},
    {VK_DELETE, L"Del"},
    {VK_INSERT, L"Ins"},
    {VK_PRIOR, L"PgUp"},
    {VK_NEXT, L"PgDn"},
    {VK_SNAPSHOT, L"PrtSc"},
    {VK_BACK, L"BkSp"},
    {VK_SPACE, L"Spacebar"},
    {VK_LEFT, L"LeftArrow"},
    {VK_RIGHT, L"RightArrow"},
    {VK_UP, L"UpArrow"},
    {VK_DOWN, L"DownArrow"},
    {VK_OEM_1, L"OemSemicolon"},
    {VK_OEM_PLUS, L"OemPlus"},
    {VK_OEM_COMMA, L"OemComma"},
    {VK_OEM_MINUS, L"OemMinus"},
    {VK_OEM_PERIOD, L"OemPeriod"},
    {VK_OEM_2, L"OemQuestion"},
    {VK_OEM_3, L"OemTilde"},
    {VK_OEM_4, L"OemOpenBracket"},
    {VK_OEM_5, L"OemBackslash"},
    {VK_OEM_6, L"OemCloseBracket"},
    {VK_OEM_7, L"OemQuote"},
    {VK_LCONTROL, L"Control"},
    {VK_LCONTROL, L"Ctrl"},
    {VK_LSHIFT, L"Shift"},
    {VK_LMENU, L"Alt"},
    {VK_LWIN, L"Win"},
    {VK_LWIN, L"Windows"},
    {VK_LWIN, L"Super"},
    {VK_LWIN, L"Meta"},
};

bool ModifierToken(const std::wstring& token, UINT* bit, int* vk) {
    std::wstring t = Lower(Trim(token));
    if (t == L"ctrl" || t == L"control") { *bit = KS_CTRL; *vk = VK_LCONTROL; return true; }
    if (t == L"lctrl" || t == L"lcontrol") { *bit = KS_CTRL; *vk = VK_LCONTROL; return true; }
    if (t == L"rctrl" || t == L"rcontrol") { *bit = KS_CTRL; *vk = VK_RCONTROL; return true; }
    if (t == L"shift") { *bit = KS_SHIFT; *vk = VK_LSHIFT; return true; }
    if (t == L"lshift") { *bit = KS_SHIFT; *vk = VK_LSHIFT; return true; }
    if (t == L"rshift") { *bit = KS_SHIFT; *vk = VK_RSHIFT; return true; }
    if (t == L"alt" || t == L"menu") { *bit = KS_ALT; *vk = VK_LMENU; return true; }
    if (t == L"lalt") { *bit = KS_ALT; *vk = VK_LMENU; return true; }
    if (t == L"ralt" || t == L"altgr") { *bit = KS_ALT; *vk = VK_RMENU; return true; }
    if (t == L"win" || t == L"windows" || t == L"super" || t == L"meta") { *bit = KS_WIN; *vk = VK_LWIN; return true; }
    if (t == L"lwin") { *bit = KS_WIN; *vk = VK_LWIN; return true; }
    if (t == L"rwin") { *bit = KS_WIN; *vk = VK_RWIN; return true; }
    return false;
}

} // namespace

std::wstring VkName(int vk) {
    if (vk >= 'A' && vk <= 'Z') return std::wstring(1, static_cast<wchar_t>(vk));
    if (vk >= '0' && vk <= '9') return std::wstring(1, static_cast<wchar_t>(vk));
    if (vk >= VK_F1 && vk <= VK_F24) {
        wchar_t buf[8];
        swprintf_s(buf, L"F%d", vk - VK_F1 + 1);
        return buf;
    }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
        wchar_t buf[8];
        swprintf_s(buf, L"Num%d", vk - VK_NUMPAD0);
        return buf;
    }
    for (const auto& entry : kNamed) {
        if (entry.vk == vk) return entry.name;
    }
    wchar_t buf[16];
    swprintf_s(buf, L"VK_%02X", vk);
    return buf;
}

int VkFromName(const std::wstring& name) {
    std::wstring t = Trim(name);
    if (t.empty()) return 0;

    if (t.size() == 1) {
        wchar_t c = static_cast<wchar_t>(towupper(t[0]));
        if (c >= L'A' && c <= L'Z') return static_cast<int>(c);
        if (c >= L'0' && c <= L'9') return static_cast<int>(c);
    }

    std::wstring lower = Lower(t);
    if (lower.size() > 3 && lower.compare(0, 3, L"vk_") == 0) {
        wchar_t* end = nullptr;
        long value = wcstol(lower.c_str() + 3, &end, 16);
        if (end && *end == L'\0' && value > 0 && value < 256) return static_cast<int>(value);
    }
    if (lower.size() > 6 && lower.compare(0, 6, L"numpad") == 0) {
        wchar_t c = lower[6];
        if (c >= L'0' && c <= L'9' && lower.size() == 7) return VK_NUMPAD0 + (c - L'0');
    }
    if (lower == L"numpadadd") return VK_ADD;
    if (lower == L"numpadsubtract") return VK_SUBTRACT;
    if (lower == L"numpadmultiply") return VK_MULTIPLY;
    if (lower == L"numpaddivide") return VK_DIVIDE;
    if (lower == L"numpaddecimal") return VK_DECIMAL;

    for (const auto& entry : kNamed) {
        if (EqualsI(t, entry.name)) return entry.vk;
    }
    for (const auto& entry : kAliases) {
        if (EqualsI(t, entry.name)) return entry.vk;
    }
    return 0;
}

std::wstring FormatCombo(const KeyCombo& combo) {
    std::wstring out;
    if (combo.mods & KS_CTRL) out += L"Ctrl+";
    if (combo.mods & KS_ALT) out += L"Alt+";
    if (combo.mods & KS_SHIFT) out += L"Shift+";
    if (combo.mods & KS_WIN) out += L"Win+";
    out += VkName(combo.vk);
    return out;
}

bool ParseCombo(const std::wstring& text, KeyCombo* out) {
    if (!out) return false;
    std::wstring input = Trim(text);
    if (input.empty()) return false;

    UINT mods = 0;
    int key = 0;
    std::vector<int> modKeys;

    size_t start = 0;
    while (start <= input.size()) {
        size_t end = input.find(L'+', start);
        std::wstring token = Trim(input.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start));
        if (token.empty()) return false;

        UINT bit = 0;
        int modVk = 0;
        if (ModifierToken(token, &bit, &modVk)) {
            mods |= bit;
            modKeys.push_back(modVk);
        } else {
            int vk = VkFromName(token);
            if (vk == 0 || key != 0) return false;
            key = vk;
        }
        if (end == std::wstring::npos) break;
        start = end + 1;
    }

    if (key == 0) {
        if (modKeys.empty()) return false;
        key = modKeys.back();
        mods &= ~ModsFromVk(key);
    }

    out->vk = key;
    out->mods = mods;
    return true;
}

UINT ModsFromVk(int vk) {
    switch (vk) {
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_CONTROL:
        return KS_CTRL;
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_SHIFT:
        return KS_SHIFT;
    case VK_LMENU:
    case VK_RMENU:
    case VK_MENU:
        return KS_ALT;
    case VK_LWIN:
    case VK_RWIN:
        return KS_WIN;
    default:
        return 0;
    }
}

bool IsModifierVk(int vk) {
    return ModsFromVk(vk) != 0;
}

bool IsExtendedVk(int vk) {
    switch (vk) {
    case VK_INSERT:
    case VK_DELETE:
    case VK_HOME:
    case VK_END:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_LEFT:
    case VK_UP:
    case VK_RIGHT:
    case VK_DOWN:
    case VK_NUMLOCK:
    case VK_DIVIDE:
    case VK_RCONTROL:
    case VK_RMENU:
    case VK_LWIN:
    case VK_RWIN:
    case VK_APPS:
    case VK_SNAPSHOT:
        return true;
    default:
        return false;
    }
}

int ModifierVkForBit(UINT bit) {
    switch (bit) {
    case KS_CTRL: return VK_LCONTROL;
    case KS_SHIFT: return VK_LSHIFT;
    case KS_ALT: return VK_LMENU;
    case KS_WIN: return VK_LWIN;
    default: return 0;
    }
}

void InjectVk(int vk, bool down, ULONG_PTR sig) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.dwExtraInfo = sig;

    UINT scan = MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC_EX);
    BYTE base = static_cast<BYTE>(scan & 0xFF);
    bool extended = ((scan & 0xFF00) == 0xE000) || IsExtendedVk(vk);

    if (base != 0) {
        input.ki.wScan = base;
        input.ki.dwFlags = KEYEVENTF_SCANCODE | (extended ? KEYEVENTF_EXTENDEDKEY : 0);
    } else {
        input.ki.wVk = static_cast<WORD>(vk);
        input.ki.dwFlags = extended ? KEYEVENTF_EXTENDEDKEY : 0;
    }
    if (!down) input.ki.dwFlags |= KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(input));
}

void InjectUnicode(wchar_t ch, ULONG_PTR sig) {
    INPUT inputs[2]{};
    for (int i = 0; i < 2; ++i) {
        inputs[i].type = INPUT_KEYBOARD;
        inputs[i].ki.wScan = ch;
        inputs[i].ki.dwFlags = KEYEVENTF_UNICODE | (i == 1 ? KEYEVENTF_KEYUP : 0);
        inputs[i].ki.dwExtraInfo = sig;
    }
    SendInput(2, inputs, sizeof(INPUT));
}

void InjectModsDown(UINT mods, ULONG_PTR sig) {
    if (mods & KS_CTRL) InjectVk(VK_LCONTROL, true, sig);
    if (mods & KS_SHIFT) InjectVk(VK_LSHIFT, true, sig);
    if (mods & KS_ALT) InjectVk(VK_LMENU, true, sig);
    if (mods & KS_WIN) InjectVk(VK_LWIN, true, sig);
}

void InjectModsUp(UINT mods, ULONG_PTR sig) {
    if (mods & KS_WIN) InjectVk(VK_LWIN, false, sig);
    if (mods & KS_ALT) InjectVk(VK_LMENU, false, sig);
    if (mods & KS_SHIFT) InjectVk(VK_LSHIFT, false, sig);
    if (mods & KS_CTRL) InjectVk(VK_LCONTROL, false, sig);
}

void InjectComboDown(const KeyCombo& combo, ULONG_PTR sig) {
    InjectModsDown(combo.mods, sig);
    InjectVk(combo.vk, true, sig);
}

void InjectComboUp(const KeyCombo& combo, ULONG_PTR sig) {
    InjectVk(combo.vk, false, sig);
    InjectModsUp(combo.mods, sig);
}

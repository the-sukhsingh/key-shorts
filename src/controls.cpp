#include "controls.h"
#include "theme.h"

#include <commctrl.h>
#include <windowsx.h>

namespace ui {
namespace {

constexpr wchar_t kButtonClass[] = L"KeyShortsButton";
constexpr wchar_t kSwitchClass[] = L"KeyShortsSwitch";
constexpr wchar_t kCheckClass[] = L"KeyShortsCheck";
constexpr wchar_t kSegmentsClass[] = L"KeyShortsSegments";

HINSTANCE g_instance = nullptr;

struct ButtonData {
    std::wstring text;
    std::wstring glyph;
    ButtonStyle style = ButtonStyle::Secondary;
    bool hover = false;
    bool pressed = false;
};

struct SwitchData {
    bool on = false;
    bool hover = false;
};

struct CheckData {
    std::wstring text;
    bool on = false;
    bool hover = false;
};

struct SegmentData {
    std::vector<std::wstring> items;
    int selected = 0;
    int hover = -1;
};

template <typename T>
T* Data(HWND hwnd) {
    return reinterpret_cast<T*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

void Notify(HWND hwnd, int code) {
    SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), code),
                 reinterpret_cast<LPARAM>(hwnd));
}

void TrackHover(HWND hwnd) {
    TRACKMOUSEEVENT track{};
    track.cbSize = sizeof(track);
    track.dwFlags = TME_LEAVE;
    track.hwndTrack = hwnd;
    TrackMouseEvent(&track);
}

void PaintButton(HWND hwnd, ButtonData* data) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc{};
    GetClientRect(hwnd, &rc);
    theme::FillSolid(dc, rc, theme::kBg);

    if (!data) {
        EndPaint(hwnd, &ps);
        return;
    }

    bool enabled = IsWindowEnabled(hwnd) != FALSE;
    bool hot = data->hover && enabled;
    bool down = data->pressed && enabled;

    COLORREF fill = theme::kRaised;
    COLORREF textColor = theme::kText;
    bool border = false;

    switch (data->style) {
    case ButtonStyle::Primary:
        fill = !enabled ? theme::kRaised
                        : down ? theme::kAccentPressed : hot ? theme::kAccentHover : theme::kAccent;
        textColor = enabled ? theme::kAccentInk : theme::kTextMuted;
        break;
    case ButtonStyle::Secondary:
        fill = !enabled ? theme::kCard : down ? theme::kDeep : hot ? theme::kRaisedHover : theme::kRaised;
        textColor = enabled ? theme::kText : theme::kTextMuted;
        border = true;
        break;
    case ButtonStyle::Ghost:
    default:
        fill = down || hot ? theme::kRaised : theme::kBg;
        textColor = enabled ? (hot ? theme::kText : theme::kTextSecondary) : theme::kTextDisabled;
        break;
    }

    int radius = theme::S(8);
    theme::FillRound(dc, rc, radius, fill);
    if (border) theme::StrokeRound(dc, rc, radius, theme::kHairline);

    if (GetFocus() == hwnd) {
        RECT focus = rc;
        InflateRect(&focus, -theme::S(1), -theme::S(1));
        theme::StrokeRound(dc, focus, radius - theme::S(1),
                           data->style == ButtonStyle::Primary ? theme::kAccentInk : theme::kAccent);
    }

    HFONT font = data->style == ButtonStyle::Primary ? theme::Font().bodyBold : theme::Font().body;
    int iconSize = theme::S(16);
    int gap = theme::S(8);
    SIZE textSize = theme::MeasureText(dc, data->text, font);
    int contentWidth = textSize.cx;
    if (!data->glyph.empty()) contentWidth += iconSize + (data->text.empty() ? 0 : gap);

    int x = rc.left + ((rc.right - rc.left) - contentWidth) / 2;
    int cy = rc.top + (rc.bottom - rc.top) / 2;
    int shift = down ? theme::S(1) : 0;

    if (!data->glyph.empty()) {
        RECT iconRect{x, cy - iconSize / 2 + shift, x + iconSize, cy + iconSize / 2 + shift};
        theme::DrawGlyph(dc, iconRect, data->glyph.c_str(), theme::Font().icon, textColor);
        x += iconSize + (data->text.empty() ? 0 : gap);
    }
    if (!data->text.empty()) {
        RECT textRect{x, rc.top + shift, rc.right - theme::S(4), rc.bottom + shift};
        theme::DrawLabel(dc, data->text, textRect, font, textColor,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    EndPaint(hwnd, &ps);
}

void PaintSwitch(HWND hwnd, SwitchData* data) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc{};
    GetClientRect(hwnd, &rc);
    theme::FillSolid(dc, rc, theme::kBg);

    bool on = data && data->on;
    bool hot = data && data->hover;
    int height = rc.bottom - rc.top;

    theme::FillRound(dc, rc, height / 2,
                     on ? theme::kAccent : (hot ? theme::kRaisedHover : theme::kRaised));
    if (!on) theme::StrokeRound(dc, rc, height / 2, theme::kHairline);

    int knob = theme::S(16);
    int y = rc.top + (height - knob) / 2;
    int x = on ? rc.right - theme::S(2) - knob : rc.left + theme::S(2);
    RECT knobRect{x, y, x + knob, y + knob};
    theme::FillEllipse(dc, knobRect, on ? theme::kText : theme::kTextSecondary);

    if (GetFocus() == hwnd) {
        RECT focus = rc;
        InflateRect(&focus, -theme::S(1), -theme::S(1));
        theme::StrokeRound(dc, focus, (height - theme::S(2)) / 2,
                           on ? theme::kText : theme::kAccent);
    }

    EndPaint(hwnd, &ps);
}

void PaintCheck(HWND hwnd, CheckData* data) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc{};
    GetClientRect(hwnd, &rc);
    theme::FillSolid(dc, rc, theme::kBg);

    bool enabled = IsWindowEnabled(hwnd) != FALSE;
    bool on = data && data->on;
    bool hot = data && data->hover;
    int box = theme::S(16);
    int y = rc.top + (rc.bottom - rc.top - box) / 2;
    RECT boxRect{rc.left, y, rc.left + box, y + box};

    theme::FillRound(dc, boxRect, theme::S(4),
                     on ? (enabled ? theme::kAccent : theme::kRaisedHover)
                        : (hot ? theme::kRaisedHover : theme::kRaised));
    if (!on) theme::StrokeRound(dc, boxRect, theme::S(4), theme::kHairline);
    if (on) theme::DrawGlyph(dc, boxRect, L"\xE73E", theme::Font().iconSmall, theme::kAccentInk);

    RECT labelRect{rc.left + box + theme::S(8), rc.top, rc.right, rc.bottom};
    theme::DrawLabel(dc, data ? data->text : L"", labelRect, theme::Font().body,
                     enabled ? (hot ? theme::kText : theme::kTextSecondary) : theme::kTextMuted);

    if (GetFocus() == hwnd) {
        RECT focus = boxRect;
        InflateRect(&focus, theme::S(2), theme::S(2));
        theme::StrokeRound(dc, focus, theme::S(5), theme::kAccent);
    }

    EndPaint(hwnd, &ps);
}

int HitSegment(HWND hwnd, int x) {
    SegmentData* data = Data<SegmentData>(hwnd);
    if (!data || data->items.empty()) return -1;

    RECT rc{};
    GetClientRect(hwnd, &rc);
    int pad = theme::S(3);
    int count = static_cast<int>(data->items.size());
    int inner = (rc.right - rc.left) - pad * 2;
    int width = inner / count;
    if (width <= 0 || x < pad) return -1;

    int index = (x - pad) / width;
    return index >= 0 && index < count ? index : -1;
}

void PaintSegments(HWND hwnd, SegmentData* data) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc{};
    GetClientRect(hwnd, &rc);
    theme::FillSolid(dc, rc, theme::kBg);

    if (!data || data->items.empty()) {
        EndPaint(hwnd, &ps);
        return;
    }

    int count = static_cast<int>(data->items.size());
    int radius = theme::S(8);
    theme::FillRound(dc, rc, radius, theme::kDeep);
    theme::StrokeRound(dc, rc, radius, theme::kHairline);

    int pad = theme::S(3);
    int inner = (rc.right - rc.left) - pad * 2;
    int width = inner / count;

    for (int i = 0; i < count; ++i) {
        RECT segment{rc.left + pad + i * width, rc.top + pad, rc.left + pad + (i + 1) * width,
                     rc.bottom - pad};
        if (i == data->selected) {
            theme::FillRound(dc, segment, theme::S(5), theme::kRaised);
            RECT bar{segment.left + theme::S(8), segment.bottom - theme::S(4),
                     segment.right - theme::S(8), segment.bottom - theme::S(2)};
            theme::FillRound(dc, bar, theme::S(1), theme::kAccent);
        } else if (i == data->hover) {
            theme::FillRound(dc, segment, theme::S(5), theme::kRowHover);
        }

        theme::DrawLabel(dc, data->items[static_cast<size_t>(i)], segment,
                         i == data->selected ? theme::Font().bodyBold : theme::Font().body,
                         i == data->selected ? theme::kText : theme::kTextSecondary,
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }

    if (GetFocus() == hwnd) {
        RECT focus = rc;
        InflateRect(&focus, -theme::S(1), -theme::S(1));
        theme::StrokeRound(dc, focus, radius - theme::S(1), theme::kAccent);
    }

    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK ButtonProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ButtonData* data = Data<ButtonData>(hwnd);

    switch (msg) {
    case WM_NCCREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return TRUE;
    }
    case WM_NCDESTROY:
        delete data;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        PaintButton(hwnd, data);
        return 0;
    case WM_MOUSEMOVE:
        if (data && !data->hover) {
            data->hover = true;
            TrackHover(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSELEAVE:
        if (data) {
            data->hover = false;
            data->pressed = false;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (data && IsWindowEnabled(hwnd)) {
            data->pressed = true;
            SetCapture(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (data && data->pressed) {
            data->pressed = false;
            ReleaseCapture();
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            RECT rc{};
            GetClientRect(hwnd, &rc);
            if (PtInRect(&rc, point) && IsWindowEnabled(hwnd)) Notify(hwnd, BN_CLICKED);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_KEYDOWN:
        if ((wParam == VK_SPACE || wParam == VK_RETURN) && IsWindowEnabled(hwnd)) {
            Notify(hwnd, BN_CLICKED);
            return 0;
        }
        break;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_ENABLE:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_GETDLGCODE:
        return DLGC_BUTTON;
    default:
        break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK SwitchProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    SwitchData* data = Data<SwitchData>(hwnd);

    switch (msg) {
    case WM_NCCREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return TRUE;
    }
    case WM_NCDESTROY:
        delete data;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        PaintSwitch(hwnd, data);
        return 0;
    case WM_MOUSEMOVE:
        if (data && !data->hover) {
            data->hover = true;
            TrackHover(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSELEAVE:
        if (data) {
            data->hover = false;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (data && IsWindowEnabled(hwnd)) SetCapture(hwnd);
        return 0;
    case WM_LBUTTONUP:
        if (data && IsWindowEnabled(hwnd)) {
            ReleaseCapture();
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            RECT rc{};
            GetClientRect(hwnd, &rc);
            if (PtInRect(&rc, point)) {
                data->on = !data->on;
                Notify(hwnd, BN_CLICKED);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_SPACE && data && IsWindowEnabled(hwnd)) {
            data->on = !data->on;
            Notify(hwnd, BN_CLICKED);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        break;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_ENABLE:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_GETDLGCODE:
        return DLGC_BUTTON;
    default:
        break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK CheckProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    CheckData* data = Data<CheckData>(hwnd);

    switch (msg) {
    case WM_NCCREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return TRUE;
    }
    case WM_NCDESTROY:
        delete data;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        PaintCheck(hwnd, data);
        return 0;
    case WM_MOUSEMOVE:
        if (data && !data->hover) {
            data->hover = true;
            TrackHover(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSELEAVE:
        if (data) {
            data->hover = false;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (data && IsWindowEnabled(hwnd)) SetCapture(hwnd);
        return 0;
    case WM_LBUTTONUP:
        if (data && IsWindowEnabled(hwnd)) {
            ReleaseCapture();
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            RECT rc{};
            GetClientRect(hwnd, &rc);
            if (PtInRect(&rc, point)) {
                data->on = !data->on;
                Notify(hwnd, BN_CLICKED);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_SPACE && data && IsWindowEnabled(hwnd)) {
            data->on = !data->on;
            Notify(hwnd, BN_CLICKED);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        break;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_ENABLE:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_GETDLGCODE:
        return DLGC_BUTTON;
    default:
        break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK SegmentsProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    SegmentData* data = Data<SegmentData>(hwnd);

    switch (msg) {
    case WM_NCCREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return TRUE;
    }
    case WM_NCDESTROY:
        delete data;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        PaintSegments(hwnd, data);
        return 0;
    case WM_MOUSEMOVE: {
        int index = HitSegment(hwnd, GET_X_LPARAM(lParam));
        if (data && index != data->hover) {
            data->hover = index;
            TrackHover(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (data) {
            data->hover = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (data && IsWindowEnabled(hwnd)) {
            int index = HitSegment(hwnd, GET_X_LPARAM(lParam));
            if (index >= 0 && index != data->selected) {
                data->selected = index;
                Notify(hwnd, CBN_SELCHANGE);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    case WM_KEYDOWN:
        if (data && !data->items.empty() &&
            (wParam == VK_LEFT || wParam == VK_RIGHT)) {
            int count = static_cast<int>(data->items.size());
            int next = data->selected + (wParam == VK_RIGHT ? 1 : -1);
            if (next < 0) next = 0;
            if (next >= count) next = count - 1;
            if (next != data->selected) {
                data->selected = next;
                Notify(hwnd, CBN_SELCHANGE);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        break;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_ENABLE:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_GETDLGCODE:
        return DLGC_BUTTON | DLGC_WANTARROWS;
    default:
        break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void RegisterOne(const wchar_t* name, WNDPROC proc) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = proc;
    wc.hInstance = g_instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = name;
    RegisterClassExW(&wc);
}

} // namespace

void RegisterControls(HINSTANCE instance) {
    g_instance = instance;
    RegisterOne(kButtonClass, ButtonProc);
    RegisterOne(kSwitchClass, SwitchProc);
    RegisterOne(kCheckClass, CheckProc);
    RegisterOne(kSegmentsClass, SegmentsProc);
}

HWND Button(HWND parent, int id, const std::wstring& text, const wchar_t* glyph,
            ButtonStyle style, int x, int y, int w, int h) {
    auto* data = new ButtonData{text, glyph ? glyph : L"", style, false, false};
    HWND control = CreateWindowExW(0, kButtonClass, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, x, y,
                                   w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                   g_instance, data);
    if (!control) delete data;
    return control;
}

void SetButtonText(HWND button, const std::wstring& text) {
    ButtonData* data = Data<ButtonData>(button);
    if (!data) return;
    data->text = text;
    InvalidateRect(button, nullptr, FALSE);
}

void SetButtonGlyph(HWND button, const wchar_t* glyph) {
    ButtonData* data = Data<ButtonData>(button);
    if (!data) return;
    data->glyph = glyph ? glyph : L"";
    InvalidateRect(button, nullptr, FALSE);
}

HWND Switch(HWND parent, int id, int x, int y) {
    auto* data = new SwitchData{false, false};
    HWND control = CreateWindowExW(0, kSwitchClass, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, x, y,
                                   theme::S(36), theme::S(20), parent,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_instance,
                                   data);
    if (!control) delete data;
    return control;
}

bool SwitchOn(HWND control) {
    SwitchData* data = Data<SwitchData>(control);
    return data && data->on;
}

void SetSwitchOn(HWND control, bool on) {
    SwitchData* data = Data<SwitchData>(control);
    if (!data || data->on == on) return;
    data->on = on;
    InvalidateRect(control, nullptr, FALSE);
}

HWND Check(HWND parent, int id, const std::wstring& text, int x, int y, int w, int h) {
    auto* data = new CheckData{text, false, false};
    HWND control = CreateWindowExW(0, kCheckClass, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, x, y, w,
                                   h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                   g_instance, data);
    if (!control) delete data;
    return control;
}

bool CheckOn(HWND control) {
    CheckData* data = Data<CheckData>(control);
    return data && data->on;
}

void SetCheckOn(HWND control, bool on) {
    CheckData* data = Data<CheckData>(control);
    if (!data || data->on == on) return;
    data->on = on;
    InvalidateRect(control, nullptr, FALSE);
}

HWND Segments(HWND parent, int id, const std::vector<std::wstring>& items, int x, int y, int w,
              int h) {
    auto* data = new SegmentData{items, 0, -1};
    HWND control = CreateWindowExW(0, kSegmentsClass, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, x, y,
                                   w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                   g_instance, data);
    if (!control) delete data;
    return control;
}

int SegmentIndex(HWND control) {
    SegmentData* data = Data<SegmentData>(control);
    return data ? data->selected : 0;
}

void SetSegmentIndex(HWND control, int index) {
    SegmentData* data = Data<SegmentData>(control);
    if (!data) return;
    if (index < 0) index = 0;
    if (index >= static_cast<int>(data->items.size())) index = static_cast<int>(data->items.size()) - 1;
    if (data->selected == index) return;
    data->selected = index;
    InvalidateRect(control, nullptr, FALSE);
}

} // namespace ui

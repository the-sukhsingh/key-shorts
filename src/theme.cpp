#include "theme.h"

#include <dwmapi.h>
#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>

namespace theme {
namespace {

UINT g_dpi = 96;
Fonts g_fonts;
bool g_fontsReady = false;

int CALLBACK FontEnumProc(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM lParam) {
    *reinterpret_cast<bool*>(lParam) = true;
    return 0;
}

bool FontExists(const wchar_t* face) {
    HDC dc = GetDC(nullptr);
    LOGFONTW lf{};
    lf.lfCharSet = DEFAULT_CHARSET;
    wcsncpy_s(lf.lfFaceName, face, _TRUNCATE);
    bool found = false;
    EnumFontFamiliesExW(dc, &lf, FontEnumProc, reinterpret_cast<LPARAM>(&found), 0);
    ReleaseDC(nullptr, dc);
    return found;
}

HFONT MakeFont(const wchar_t* face, int points, int weight) {
    LOGFONTW lf{};
    lf.lfHeight = -MulDiv(points, static_cast<int>(g_dpi), 72);
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH;
    wcsncpy_s(lf.lfFaceName, face, _TRUNCATE);
    return CreateFontIndirectW(&lf);
}

Gdiplus::Color ToGdi(COLORREF color) {
    return Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color));
}

void AddRoundRect(Gdiplus::GraphicsPath& path, const RECT& r, int radius) {
    int w = r.right - r.left;
    int h = r.bottom - r.top;
    if (w <= 0 || h <= 0) return;

    int d = radius * 2;
    if (d > w) d = w;
    if (d > h) d = h;
    if (d <= 0) {
        path.AddRectangle(Gdiplus::Rect(r.left, r.top, w, h));
        return;
    }
    path.AddArc(r.left, r.top, d, d, 180, 90);
    path.AddArc(r.right - d, r.top, d, d, 270, 90);
    path.AddArc(r.right - d, r.bottom - d, d, d, 0, 90);
    path.AddArc(r.left, r.bottom - d, d, d, 90, 90);
    path.CloseFigure();
}

std::vector<std::wstring> SplitCombo(const std::wstring& combo) {
    std::vector<std::wstring> parts;
    size_t start = 0;
    while (start <= combo.size()) {
        size_t plus = combo.find(L'+', start);
        std::wstring token = combo.substr(start, plus == std::wstring::npos ? std::wstring::npos : plus - start);
        size_t first = token.find_first_not_of(L" \t");
        size_t last = token.find_last_not_of(L" \t");
        if (first != std::wstring::npos) parts.push_back(token.substr(first, last - first + 1));
        if (plus == std::wstring::npos) break;
        start = plus + 1;
    }
    return parts;
}

} // namespace

void SetDpi(UINT dpi) {
    g_dpi = dpi ? dpi : 96;
}

int S(int value) {
    return MulDiv(value, static_cast<int>(g_dpi), 96);
}

const Fonts& Font() {
    return g_fonts;
}

void Init() {
    if (g_fontsReady) return;

    const wchar_t* mono = FontExists(L"Cascadia Mono") ? L"Cascadia Mono" : L"Consolas";
    const wchar_t* icons = FontExists(L"Segoe Fluent Icons") ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";

    g_fonts.body = MakeFont(L"Segoe UI", 10, FW_NORMAL);
    g_fonts.bodyBold = MakeFont(L"Segoe UI", 10, FW_SEMIBOLD);
    g_fonts.meta = MakeFont(L"Segoe UI", 9, FW_NORMAL);
    g_fonts.metaBold = MakeFont(L"Segoe UI", 9, FW_SEMIBOLD);
    g_fonts.wordmark = MakeFont(L"Segoe UI", 12, FW_SEMIBOLD);
    g_fonts.title = MakeFont(L"Segoe UI", 15, FW_SEMIBOLD);
    g_fonts.mono = MakeFont(mono, 9, FW_NORMAL);
    g_fonts.monoBold = MakeFont(mono, 9, FW_SEMIBOLD);
    g_fonts.keycap = MakeFont(mono, 10, FW_NORMAL);
    g_fonts.icon = MakeFont(icons, 11, FW_NORMAL);
    g_fonts.iconSmall = MakeFont(icons, 9, FW_NORMAL);
    g_fontsReady = true;
}

void Shutdown() {
    if (!g_fontsReady) return;
    HFONT fonts[] = {g_fonts.body,        g_fonts.bodyBold, g_fonts.meta,   g_fonts.metaBold,
                     g_fonts.wordmark,    g_fonts.title,    g_fonts.mono,    g_fonts.monoBold,
                     g_fonts.keycap,      g_fonts.icon,     g_fonts.iconSmall};
    for (HFONT font : fonts) {
        if (font) DeleteObject(font);
    }
    g_fonts = Fonts{};
    g_fontsReady = false;
}

void ApplyDarkChrome(HWND hwnd) {
    if (!hwnd) return;
    BOOL dark = TRUE;
    if (FAILED(DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark)))) {
        DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
    }
    COLORREF caption = kBg;
    COLORREF text = kText;
    COLORREF border = kBg;
    DwmSetWindowAttribute(hwnd, 35, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd, 36, &text, sizeof(text));
    DwmSetWindowAttribute(hwnd, 34, &border, sizeof(border));
}

void FillSolid(HDC dc, const RECT& r, COLORREF color) {
    HGDIOBJ old = SelectObject(dc, GetStockObject(DC_BRUSH));
    SetDCBrushColor(dc, color);
    FillRect(dc, &r, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    SelectObject(dc, old);
}

void FillRound(HDC dc, const RECT& r, int radius, COLORREF color) {
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::GraphicsPath path;
    AddRoundRect(path, r, radius);
    Gdiplus::SolidBrush brush(ToGdi(color));
    g.FillPath(&brush, &path);
}

void StrokeRound(HDC dc, const RECT& r, int radius, COLORREF color, float width) {
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::GraphicsPath path;
    AddRoundRect(path, r, radius);
    Gdiplus::Pen pen(ToGdi(color), width);
    g.DrawPath(&pen, &path);
}

void FillEllipse(HDC dc, const RECT& r, COLORREF color) {
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::SolidBrush brush(ToGdi(color));
    g.FillEllipse(&brush, static_cast<INT>(r.left), static_cast<INT>(r.top),
                  static_cast<INT>(r.right - r.left), static_cast<INT>(r.bottom - r.top));
}

void DrawLabel(HDC dc, const std::wstring& text, const RECT& r, HFONT font, COLORREF color, UINT flags) {
    HGDIOBJ old = SelectObject(dc, font);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    RECT rc = r;
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rc, flags);
    SelectObject(dc, old);
}

SIZE MeasureText(HDC dc, const std::wstring& text, HFONT font) {
    HGDIOBJ old = SelectObject(dc, font);
    SIZE size{};
    GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
    SelectObject(dc, old);
    return size;
}

void DrawGlyph(HDC dc, const RECT& r, const wchar_t* glyph, HFONT font, COLORREF color) {
    DrawLabel(dc, glyph, r, font, color, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

int KeycapsWidth(HDC dc, const std::wstring& combo) {
    std::vector<std::wstring> parts = SplitCombo(combo);
    if (parts.empty()) return 0;

    int total = 0;
    for (const std::wstring& part : parts) {
        SIZE size = MeasureText(dc, part, g_fonts.keycap);
        total += std::max(S(20), static_cast<int>(size.cx) + S(14)) + S(4);
    }
    return total - S(4);
}

void DrawKeycaps(HDC dc, const std::wstring& combo, int x, int y, int maxWidth, bool enabled) {
    std::vector<std::wstring> parts = SplitCombo(combo);
    int height = S(22);
    int end = x + maxWidth;

    for (const std::wstring& part : parts) {
        SIZE size = MeasureText(dc, part, g_fonts.keycap);
        int width = std::max(S(20), static_cast<int>(size.cx) + S(14));
        if (x + width > end) break;

        RECT outer{x, y, x + width, y + height};
        FillRound(dc, outer, S(5), kKeyEdge);

        RECT inner{x, y, x + width, y + height - S(2)};
        FillRound(dc, inner, S(5), enabled ? kKey : RGB(0x16, 0x1A, 0x22));

        RECT label = inner;
        label.left += S(4);
        label.right -= S(4);
        if (part == L"`" || part == L"~" || part == L"^") {
            label.top += S(3);
            label.bottom += S(3);
        }
        DrawLabel(dc, part, label, g_fonts.keycap, enabled ? kText : kTextMuted,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

        x += width + S(4);
    }
}

} // namespace theme

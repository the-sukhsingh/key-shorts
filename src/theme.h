#pragma once

#include "common.h"

// Ink + signal green: a quiet, nocturnal instrument panel.
namespace theme {

inline constexpr COLORREF kBg = RGB(0x0D, 0x11, 0x1A);
inline constexpr COLORREF kDeep = RGB(0x08, 0x0A, 0x12);
inline constexpr COLORREF kCard = RGB(0x13, 0x18, 0x24);
inline constexpr COLORREF kRaised = RGB(0x1D, 0x23, 0x33);
inline constexpr COLORREF kRaisedHover = RGB(0x26, 0x2F, 0x43);
inline constexpr COLORREF kHairline = RGB(0x2A, 0x34, 0x4A);
inline constexpr COLORREF kHairlineSoft = RGB(0x1E, 0x25, 0x36);
inline constexpr COLORREF kText = RGB(0xEE, 0xF2, 0xF8);
inline constexpr COLORREF kTextSecondary = RGB(0xA3, 0xAD, 0xC0);
inline constexpr COLORREF kTextMuted = RGB(0x7A, 0x84, 0x99);
inline constexpr COLORREF kTextDisabled = RGB(0x56, 0x5F, 0x72);
inline constexpr COLORREF kAccent = RGB(0x5B, 0x8D, 0xEF);
inline constexpr COLORREF kAccentHover = RGB(0x7A, 0xA6, 0xF5);
inline constexpr COLORREF kAccentPressed = RGB(0x4A, 0x79, 0xD9);
inline constexpr COLORREF kAccentInk = RGB(0x06, 0x0A, 0x14);
inline constexpr COLORREF kDanger = RGB(0xEF, 0x5A, 0x65);
inline constexpr COLORREF kKey = RGB(0x25, 0x2D, 0x40);
inline constexpr COLORREF kKeyEdge = RGB(0x08, 0x0A, 0x12);
inline constexpr COLORREF kRowHover = RGB(0x1A, 0x20, 0x30);
inline constexpr COLORREF kRowSelected = RGB(0x20, 0x28, 0x3A);
inline constexpr COLORREF kSeparator = RGB(0x16, 0x1B, 0x28);

struct Fonts {
    HFONT body = nullptr;
    HFONT bodyBold = nullptr;
    HFONT meta = nullptr;
    HFONT metaBold = nullptr;
    HFONT wordmark = nullptr;
    HFONT title = nullptr;
    HFONT mono = nullptr;
    HFONT monoBold = nullptr;
    HFONT keycap = nullptr;
    HFONT icon = nullptr;
    HFONT iconSmall = nullptr;
};

void SetDpi(UINT dpi);
int S(int value);
const Fonts& Font();

void Init();
void Shutdown();
void ApplyDarkChrome(HWND hwnd);

void FillSolid(HDC dc, const RECT& r, COLORREF color);
void FillRound(HDC dc, const RECT& r, int radius, COLORREF color);
void StrokeRound(HDC dc, const RECT& r, int radius, COLORREF color, float width = 1.0f);
void FillEllipse(HDC dc, const RECT& r, COLORREF color);

void DrawLabel(HDC dc, const std::wstring& text, const RECT& r, HFONT font, COLORREF color,
               UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
SIZE MeasureText(HDC dc, const std::wstring& text, HFONT font);
void DrawGlyph(HDC dc, const RECT& r, const wchar_t* glyph, HFONT font, COLORREF color);

void DrawKeycaps(HDC dc, const std::wstring& combo, int x, int y, int maxWidth, bool enabled);
int KeycapsWidth(HDC dc, const std::wstring& combo);

} // namespace theme

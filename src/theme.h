#pragma once

#include "common.h"

// Ink + signal green: a quiet, nocturnal instrument panel.
namespace theme {

inline constexpr COLORREF kBg = RGB(0x0B, 0x0D, 0x14);
inline constexpr COLORREF kDeep = RGB(0x07, 0x08, 0x0D);
inline constexpr COLORREF kCard = RGB(0x11, 0x14, 0x1C);
inline constexpr COLORREF kRaised = RGB(0x1A, 0x1E, 0x28);
inline constexpr COLORREF kRaisedHover = RGB(0x23, 0x29, 0x36);
inline constexpr COLORREF kHairline = RGB(0x24, 0x2A, 0x38);
inline constexpr COLORREF kHairlineSoft = RGB(0x1B, 0x1F, 0x2A);
inline constexpr COLORREF kText = RGB(0xEE, 0xF0, 0xF5);
inline constexpr COLORREF kTextSecondary = RGB(0xA3, 0xA9, 0xB8);
inline constexpr COLORREF kTextMuted = RGB(0x7A, 0x82, 0x96);
inline constexpr COLORREF kTextDisabled = RGB(0x56, 0x5D, 0x6E);
inline constexpr COLORREF kAccent = RGB(0x4C, 0xC3, 0x8A);
inline constexpr COLORREF kAccentHover = RGB(0x5B, 0xD4, 0x9A);
inline constexpr COLORREF kAccentPressed = RGB(0x42, 0xB3, 0x7C);
inline constexpr COLORREF kAccentInk = RGB(0x06, 0x13, 0x0C);
inline constexpr COLORREF kDanger = RGB(0xE5, 0x48, 0x4D);
inline constexpr COLORREF kKey = RGB(0x23, 0x29, 0x36);
inline constexpr COLORREF kKeyEdge = RGB(0x07, 0x08, 0x0D);
inline constexpr COLORREF kRowHover = RGB(0x18, 0x1D, 0x28);
inline constexpr COLORREF kRowSelected = RGB(0x1E, 0x24, 0x31);
inline constexpr COLORREF kSeparator = RGB(0x15, 0x18, 0x21);

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

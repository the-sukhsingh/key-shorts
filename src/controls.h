#pragma once

#include "common.h"

namespace ui {

enum class ButtonStyle { Primary, Secondary, Ghost };

void RegisterControls(HINSTANCE instance);

HWND Button(HWND parent, int id, const std::wstring& text, const wchar_t* glyph,
            ButtonStyle style, int x, int y, int w, int h);
void SetButtonText(HWND button, const std::wstring& text);
void SetButtonGlyph(HWND button, const wchar_t* glyph);

HWND Switch(HWND parent, int id, int x, int y);
bool SwitchOn(HWND control);
void SetSwitchOn(HWND control, bool on);

HWND Check(HWND parent, int id, const std::wstring& text, int x, int y, int w, int h);
bool CheckOn(HWND control);
void SetCheckOn(HWND control, bool on);

HWND Segments(HWND parent, int id, const std::vector<std::wstring>& items, int x, int y, int w, int h);
int SegmentIndex(HWND control);
void SetSegmentIndex(HWND control, int index);

} // namespace ui

#pragma once

#include "common.h"

enum : UINT {
    KS_CTRL = 1,
    KS_SHIFT = 2,
    KS_ALT = 4,
    KS_WIN = 8,
};

struct KeyCombo {
    int vk = 0;
    UINT mods = 0;
    bool valid() const { return vk > 0; }
};

std::wstring VkName(int vk);
int VkFromName(const std::wstring& name);
std::wstring FormatCombo(const KeyCombo& combo);
bool ParseCombo(const std::wstring& text, KeyCombo* out);

UINT ModsFromVk(int vk);
bool IsModifierVk(int vk);
bool IsExtendedVk(int vk);
int ModifierVkForBit(UINT bit);

void InjectVk(int vk, bool down, ULONG_PTR sig);
void InjectUnicode(wchar_t ch, ULONG_PTR sig);
void InjectModsDown(UINT mods, ULONG_PTR sig);
void InjectModsUp(UINT mods, ULONG_PTR sig);
void InjectComboDown(const KeyCombo& combo, ULONG_PTR sig);
void InjectComboUp(const KeyCombo& combo, ULONG_PTR sig);

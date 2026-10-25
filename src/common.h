#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <string>
#include <vector>

constexpr wchar_t kAppName[] = L"KeyShorts";
constexpr wchar_t kWindowClass[] = L"KeyShortsMain";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"KeyShorts";

// Markers attached to synthetic input so the hook can recognise its own events.
constexpr ULONG_PTR kSigRemap = 0x4B53484BUL;
constexpr ULONG_PTR kSigMacro = 0x4B53484DUL;

#pragma once

#include "common.h"

std::wstring Utf8ToWide(const std::string& text);
std::string WideToUtf8(const std::wstring& text);

std::wstring Trim(const std::wstring& text);
std::wstring Lower(std::wstring text);
bool EqualsI(const std::wstring& a, const std::wstring& b);
bool StartsWithI(const std::wstring& text, const std::wstring& prefix);

std::wstring AppDataDir();
std::wstring ConfigPath();
std::wstring ExePath();
bool EnsureDir(const std::wstring& dir);
std::wstring FileNameOf(const std::wstring& path);
std::wstring ExpandEnv(const std::wstring& text);

std::wstring EscapeValue(const std::wstring& text);
std::wstring UnescapeValue(const std::wstring& text);
std::vector<std::wstring> SplitLines(const std::wstring& text);

bool ReadTextFile(const std::wstring& path, std::wstring* out);
bool WriteTextFileAtomic(const std::wstring& path, const std::wstring& text);

#include "util.h"

#include <shlobj.h>
#include <cwctype>
#include <cstdio>

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (len <= 0) return {};
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), len);
    return out;
}

std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), len, nullptr, nullptr);
    return out;
}

std::wstring Trim(const std::wstring& text) {
    size_t start = 0;
    while (start < text.size() && iswspace(text[start])) ++start;
    size_t end = text.size();
    while (end > start && iswspace(text[end - 1])) --end;
    return text.substr(start, end - start);
}

std::wstring Lower(std::wstring text) {
    for (wchar_t& c : text) c = static_cast<wchar_t>(towlower(c));
    return text;
}

bool EqualsI(const std::wstring& a, const std::wstring& b) {
    if (a.size() != b.size()) return false;
    return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

bool StartsWithI(const std::wstring& text, const std::wstring& prefix) {
    if (text.size() < prefix.size()) return false;
    return EqualsI(text.substr(0, prefix.size()), prefix);
}

std::wstring AppDataDir() {
    wchar_t buf[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA | CSIDL_FLAG_CREATE, nullptr, SHGFP_TYPE_CURRENT, buf))) {
        return std::wstring(buf) + L"\\KeyShorts";
    }
    return L".";
}

std::wstring ConfigPath() {
    return AppDataDir() + L"\\config.ini";
}

std::wstring ExePath() {
    std::wstring out(MAX_PATH, L'\0');
    for (;;) {
        DWORD n = GetModuleFileNameW(nullptr, out.data(), static_cast<DWORD>(out.size()));
        if (n == 0) return {};
        if (n < out.size()) {
            out.resize(n);
            return out;
        }
        out.resize(out.size() * 2);
    }
}

bool EnsureDir(const std::wstring& dir) {
    if (CreateDirectoryW(dir.c_str(), nullptr)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

std::wstring FileNameOf(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? path : path.substr(pos + 1);
}

std::wstring ExpandEnv(const std::wstring& text) {
    if (text.empty()) return text;
    DWORD n = ExpandEnvironmentStringsW(text.c_str(), nullptr, 0);
    if (n == 0) return text;
    std::wstring out(n, L'\0');
    DWORD written = ExpandEnvironmentStringsW(text.c_str(), out.data(), n);
    if (written == 0) return text;
    out.resize(written - 1);
    return out;
}

std::wstring EscapeValue(const std::wstring& text) {
    std::wstring out;
    out.reserve(text.size() + 8);
    for (wchar_t c : text) {
        switch (c) {
        case L'\\': out += L"\\\\"; break;
        case L'\n': out += L"\\n"; break;
        case L'\r': break;
        case L'\t': out += L"\\t"; break;
        default: out += c; break;
        }
    }
    return out;
}

std::wstring UnescapeValue(const std::wstring& text) {
    std::wstring out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        wchar_t c = text[i];
        if (c == L'\\' && i + 1 < text.size()) {
            wchar_t next = text[i + 1];
            if (next == L'\\') { out += L'\\'; ++i; continue; }
            if (next == L'n') { out += L'\n'; ++i; continue; }
            if (next == L't') { out += L'\t'; ++i; continue; }
            if (next == L'r') { out += L'\r'; ++i; continue; }
        }
        out += c;
    }
    return out;
}

std::vector<std::wstring> SplitLines(const std::wstring& text) {
    std::vector<std::wstring> lines;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        std::wstring line = text.substr(start, end - start);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        lines.push_back(line);
        start = end + 1;
    }
    return lines;
}

bool ReadTextFile(const std::wstring& path, std::wstring* out) {
    if (!out) return false;
    out->clear();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart > 4 * 1024 * 1024) {
        CloseHandle(file);
        return false;
    }
    std::vector<char> data(static_cast<size_t>(size.QuadPart));
    size_t total = 0;
    while (total < data.size()) {
        DWORD chunk = 0;
        if (!ReadFile(file, data.data() + total, static_cast<DWORD>(data.size() - total), &chunk, nullptr) || chunk == 0) break;
        total += chunk;
    }
    CloseHandle(file);
    data.resize(total);

    if (data.size() >= 2 && static_cast<unsigned char>(data[0]) == 0xFF && static_cast<unsigned char>(data[1]) == 0xFE) {
        const size_t chars = (data.size() - 2) / 2;
        out->resize(chars);
        memcpy(out->data(), data.data() + 2, chars * 2);
        return true;
    }
    size_t offset = 0;
    if (data.size() >= 3 && static_cast<unsigned char>(data[0]) == 0xEF &&
        static_cast<unsigned char>(data[1]) == 0xBB && static_cast<unsigned char>(data[2]) == 0xBF) {
        offset = 3;
    }
    *out = Utf8ToWide(std::string(data.begin() + static_cast<ptrdiff_t>(offset), data.end()));
    return true;
}

bool WriteTextFileAtomic(const std::wstring& path, const std::wstring& text) {
    std::string utf8 = WideToUtf8(text);
    std::wstring temp = path + L".tmp";
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    size_t total = 0;
    bool ok = true;
    while (total < utf8.size()) {
        DWORD chunk = 0;
        DWORD want = static_cast<DWORD>(utf8.size() - total);
        if (!WriteFile(file, utf8.data() + total, want, &chunk, nullptr) || chunk == 0) { ok = false; break; }
        total += chunk;
    }
    FlushFileBuffers(file);
    CloseHandle(file);

    if (!ok) {
        DeleteFileW(temp.c_str());
        return false;
    }
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp.c_str());
        return false;
    }
    return true;
}

#include "config.h"
#include "util.h"

#include <cstdlib>

namespace {

bool ParseBool(const std::wstring& value, bool fallback) {
    std::wstring v = Lower(Trim(value));
    if (v == L"1" || v == L"true" || v == L"yes" || v == L"on") return true;
    if (v == L"0" || v == L"false" || v == L"no" || v == L"off") return false;
    return fallback;
}

int ParseInt(const std::wstring& value, int fallback) {
    wchar_t* end = nullptr;
    long parsed = wcstol(Trim(value).c_str(), &end, 10);
    if (!end || *end != L'\0') return fallback;
    return static_cast<int>(parsed);
}

int ParseType(const std::wstring& value, int fallback) {
    std::wstring v = Lower(Trim(value));
    if (v == L"remap") return RT_REMAP;
    if (v == L"launch") return RT_LAUNCH;
    if (v == L"command") return RT_COMMAND;
    if (v == L"sendkeys") return RT_SENDKEYS;
    int n = ParseInt(v, -1);
    if (n >= RT_REMAP && n <= RT_SENDKEYS) return n;
    return fallback;
}

int ParseScope(const std::wstring& value, int fallback) {
    std::wstring v = Lower(Trim(value));
    if (v == L"global" || v == L"all") return RS_GLOBAL;
    if (v == L"app" || v == L"application") return RS_APP;
    if (v == L"0") return RS_GLOBAL;
    if (v == L"1") return RS_APP;
    return fallback;
}

std::wstring BoolText(bool value) {
    return value ? L"1" : L"0";
}

} // namespace

const wchar_t* RuleTypeLabel(int type) {
    switch (type) {
    case RT_REMAP: return L"Remap key";
    case RT_LAUNCH: return L"Launch program";
    case RT_COMMAND: return L"Run command";
    case RT_SENDKEYS: return L"Send keys (macro)";
    default: return L"Launch program";
    }
}

const wchar_t* RuleTypeKey(int type) {
    switch (type) {
    case RT_REMAP: return L"remap";
    case RT_LAUNCH: return L"launch";
    case RT_COMMAND: return L"command";
    case RT_SENDKEYS: return L"sendkeys";
    default: return L"launch";
    }
}

const wchar_t* RuleScopeLabel(int scope) {
    return scope == RS_APP ? L"Only in app" : L"Global";
}

bool Config::Load() {
    *this = Config{};
    std::wstring text;
    if (!ReadTextFile(ConfigPath(), &text)) return false;

    Rule* current = nullptr;
    for (const std::wstring& raw : SplitLines(text)) {
        std::wstring line = Trim(raw);
        if (line.empty() || line[0] == L'#' || line[0] == L';') continue;

        if (line[0] == L'[') {
            std::wstring section = Lower(line);
            if (section == L"[rule]") {
                rules.push_back(Rule{});
                current = &rules.back();
            } else {
                current = nullptr;
            }
            continue;
        }

        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring key = Lower(Trim(line.substr(0, eq)));
        std::wstring value = UnescapeValue(Trim(line.substr(eq + 1)));

        if (current) {
            if (key == L"name") current->name = value;
            else if (key == L"enabled") current->enabled = ParseBool(value, current->enabled);
            else if (key == L"swallow") current->swallow = ParseBool(value, current->swallow);
            else if (key == L"repeat") current->repeat = ParseBool(value, current->repeat);
            else if (key == L"type") current->type = ParseType(value, current->type);
            else if (key == L"trigger") current->trigger = value;
            else if (key == L"target") current->target = value;
            else if (key == L"args") current->args = value;
            else if (key == L"scope") current->scope = ParseScope(value, current->scope);
            else if (key == L"app") current->app = value;
        } else {
            if (key == L"enabled") enabled = ParseBool(value, enabled);
            else if (key == L"run_at_startup") runAtStartup = ParseBool(value, runAtStartup);
        }
    }
    return true;
}

bool Config::Save() const {
    std::wstring out;
    out += L"# KeyShorts configuration\r\n";
    out += L"# Edits are picked up with the Reload button in the app.\r\n";
    out += L"version=1\r\n";
    out += L"enabled=" + BoolText(enabled) + L"\r\n";
    out += L"run_at_startup=" + BoolText(runAtStartup) + L"\r\n";

    for (const Rule& rule : rules) {
        out += L"\r\n[rule]\r\n";
        out += L"name=" + EscapeValue(rule.name) + L"\r\n";
        out += L"enabled=" + BoolText(rule.enabled) + L"\r\n";
        out += L"trigger=" + EscapeValue(rule.trigger) + L"\r\n";
        out += L"type=" + std::wstring(RuleTypeKey(rule.type)) + L"\r\n";
        out += L"target=" + EscapeValue(rule.target) + L"\r\n";
        out += L"args=" + EscapeValue(rule.args) + L"\r\n";
        out += L"scope=" + std::wstring(rule.scope == RS_APP ? L"app" : L"global") + L"\r\n";
        out += L"app=" + EscapeValue(rule.app) + L"\r\n";
        out += L"swallow=" + BoolText(rule.swallow) + L"\r\n";
        out += L"repeat=" + BoolText(rule.repeat) + L"\r\n";
    }

    if (!EnsureDir(AppDataDir())) return false;
    return WriteTextFileAtomic(ConfigPath(), out);
}

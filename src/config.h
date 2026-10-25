#pragma once

#include "common.h"

enum RuleType : int {
    RT_REMAP = 0,
    RT_LAUNCH = 1,
    RT_COMMAND = 2,
    RT_SENDKEYS = 3,
};

enum RuleScope : int {
    RS_GLOBAL = 0,
    RS_APP = 1,
};

struct Rule {
    std::wstring name;
    bool enabled = true;
    bool swallow = true;
    bool repeat = false;
    int type = RT_LAUNCH;
    std::wstring trigger;
    std::wstring target;
    std::wstring args;
    int scope = RS_GLOBAL;
    std::wstring app;
};

struct Config {
    bool enabled = true;
    bool runAtStartup = false;
    std::vector<Rule> rules;

    bool Load();
    bool Save() const;
};

const wchar_t* RuleTypeLabel(int type);
const wchar_t* RuleTypeKey(int type);
const wchar_t* RuleScopeLabel(int scope);

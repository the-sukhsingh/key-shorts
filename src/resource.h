#pragma once

#define IDI_APP 101
#define IDD_RULE 102

// Main window commands
#define IDC_ADD        1001
#define IDC_EDIT       1002
#define IDC_DELETE     1003
#define IDC_RELOAD     1004
#define IDC_OPENCFG    1005
#define IDC_HIDE       1006
#define IDC_MASTER     1007
#define IDC_STARTUP    1008
#define IDC_LIST       1009
#define IDC_STATUS     1010
#define IDC_TITLE      1011
#define IDC_SUBTITLE   1012

// Rule dialog commands
#define IDC_NAME         1101
#define IDC_ENABLED      1102
#define IDC_TRIGGER      1103
#define IDC_SET_TRIGGER  1104
#define IDC_TYPE         1105
#define IDC_TARGET_LABEL 1106
#define IDC_TARGET       1107
#define IDC_BROWSE       1108
#define IDC_SET_TARGET   1109
#define IDC_ARGS_LABEL   1110
#define IDC_ARGS         1111
#define IDC_SCOPE        1112
#define IDC_APP          1113
#define IDC_SWALLOW      1114
#define IDC_REPEAT       1115
#define IDC_HINT         1116

// Tray menu commands
#define IDM_SHOW       2001
#define IDM_ENABLED    2002
#define IDM_STARTUP    2003
#define IDM_CONFIG     2004
#define IDM_EXIT       2005
#define IDM_RELOAD     2006

// Private messages
#define WM_APP_TRAY      (WM_APP + 1)
#define WM_APP_SHOW      (WM_APP + 2)
#define WM_APP_CAPTURED  (WM_APP + 3)

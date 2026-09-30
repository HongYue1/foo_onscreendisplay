#pragma once

// Resource IDs shared by foo_osd.rc and the C++ side. Included by the resource compiler, so
// #define only. IDC_STATIC (-1) comes from winres.h. Control ids are unique across all tabs.

#define IDD_OSD_PREFERENCES 101
// One child dialog per tab, consecutive and in tab order.
#define IDD_OSD_TAB_GENERAL 102
#define IDD_OSD_TAB_STYLE 103
#define IDD_OSD_TAB_ELEMENTS 104
#define IDD_OSD_TAB_TEXT 105
#define IDD_OSD_TAB_FONTS 106

// Tab strip, and the (never shown) area the tab dialogs are placed in.
#define IDC_TABS 1000
#define IDC_PAGE_HOST 1001

// Always visible
#define IDC_PREVIEW 1010

// General
#define IDC_ENABLED 1021
#define IDC_ON_TRACK 1022
#define IDC_ON_PAUSE 1023
#define IDC_ON_SEEK 1024
#define IDC_ONLY_UNFOCUSED 1025
#define IDC_HIDE_FULLSCREEN 1026
#define IDC_POSITION 1028
#define IDC_MARGIN 1029
#define IDC_SCALE 1030
#define IDC_SECONDS 1032
#define IDC_ON_STREAM 1033
#define IDC_HOLD_PAUSED 1034
#define IDC_WAIT_ART 1035
#define IDC_FADE_HOVER 1036
#define IDC_MONITOR 1037

// Style
#define IDC_PRESET 1020
#define IDC_OPACITY 1031
#define IDC_LAYOUT 1040
#define IDC_BG_MODE 1041
#define IDC_BG_HEX 1042
#define IDC_BORDER 1043
#define IDC_RADIUS 1044
#define IDC_TEXT_MODE 1046
#define IDC_TEXT_HEX 1047
#define IDC_ACCENT_HEX 1048
#define IDC_ANIMATION 1049
#define IDC_ANIM_SPEED 1050
#define IDC_SHADOW 1057
#define IDC_SHEEN 1058
#define IDC_ACCENT_COVER 1059
// Colour swatches (owner-drawn buttons) next to the three hex fields; click opens the picker.
#define IDC_BG_SWATCH 1060
#define IDC_TEXT_SWATCH 1061
#define IDC_ACCENT_SWATCH 1062

// Elements
#define IDC_ART_SHAPE 1045
#define IDC_BAR_STYLE 1051
#define IDC_SHOW_ART 1052
#define IDC_SHOW_PROGRESS 1053
#define IDC_SHOW_TIMES 1054
#define IDC_SHOW_GLYPH 1055
#define IDC_SHOW_KNOB 1056
#define IDC_SHOW_REMAINING 1063

// Text
#define IDC_LINE1 1070
#define IDC_LINE2 1071
#define IDC_LINE3 1072
#define IDC_TITLEFORMAT_HELP 1073
#define IDC_TEXT_DEFAULT 1089

// Fonts. Each font row is a text control showing the choice plus Select and Default/Clear buttons.
// The fallback ids run consecutively: text 1080-1082, Select 1083-1085, Clear 1086-1088.
#define IDC_TITLE_FONT 1074
#define IDC_TITLE_FONT_PICK 1075
#define IDC_TITLE_FONT_CLEAR 1076
#define IDC_DETAIL_FONT 1077
#define IDC_DETAIL_FONT_PICK 1078
#define IDC_DETAIL_FONT_CLEAR 1079
#define IDC_FALLBACK_TEXT 1080
#define IDC_FALLBACK_PICK 1083
#define IDC_FALLBACK_CLEAR 1086

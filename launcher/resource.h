#ifndef GOLDE_RESOURCE_H
#define GOLDE_RESOURCE_H

#define IDD_MAIN            100
#define IDD_LAUNCH          101
#define IDI_APPICON         200
#define IDR_PATCH_DLL       201
#define IDR_INI_TEMPLATE    202

#define IDC_HDR_FOLDER      1005
#define IDC_SUBHEAD         1008
#define IDC_GAMEDIR         1001
#define IDC_BROWSE          1002
#define IDC_STATUS_EXE      1003
#define IDC_STATUS_PATCH    1004
#define IDC_INSTALL         1020
#define IDC_UNINSTALL       1021
#define IDC_HELPBTN         1023

/* Keep contiguous and in display order: draw_scene walks IDC_ROW_FIRST + i. */
#define IDC_ROW_MODE        1040
#define IDC_ROW_RES         1041
#define IDC_ROW_SHAPE       1042
#define IDC_ROW_CAP         1043
#define IDC_ROW_BG          1044
#define IDC_ROW_PATCH       1045
#define IDC_ROW_FIRST       IDC_ROW_MODE
#define IDC_ROW_LAST        IDC_ROW_PATCH
#define IDC_ROW_COUNT       6

#define IDC_LBL_WIDTH       1030
#define IDC_WIDTH           1011
#define IDC_LBL_HEIGHT      1031
#define IDC_HEIGHT          1012

#define IDC_MODE_FULL       1140
#define IDC_MODE_WIND       1141

#define IDC_FILT_SHARP      1150
#define IDC_FILT_LINEAR     1151
#define IDC_FILT_POINT      1152
#define IDC_AUTORES         1110
#define IDC_STRETCH         1114
#define IDC_CAP60           1115
#define IDC_ENABLED         1116
#define IDC_BACKGROUND      1117

#define IDC_PLAY            1026
#define IDC_SETUPBTN        1027

#define IDC_ABOUT           1025
#define IDC_MSG             1024

#endif

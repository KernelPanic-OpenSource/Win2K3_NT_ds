/****************************** Module Header ******************************\
* Module Name: langicon.h
*
* Copyright (c) 1998, Microsoft Corporation
*
* Define apis in langicon.c
*
\***************************************************************************/

//
// Prototypes
//

typedef enum _LAYOUT_USER {
    LAYOUT_DEF_USER,
    LAYOUT_CUR_USER
} LAYOUT_USER;

#define TIMER_MYLANGUAGECHECK     1

BOOL
DisplayLanguageIcon(
    HWND hwnd,
    LAYOUT_USER LayoutUser,
    HKL  hkl);

void
FreeLayoutInfo(
    LAYOUT_USER LayoutUser);

void
LayoutCheckHandler(
    HWND hwnd,
    LAYOUT_USER LayoutUser);

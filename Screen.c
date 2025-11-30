#include <windows.h>
#include <string.h>
#include <wchar.h>
#include <stdlib.h>
#include "Screen.h"

static int g_nScreenIndex;
static HANDLE g_hScreen[2];

void ScreenInit(void)
{
    CONSOLE_CURSOR_INFO cci;

    // Stable ASCII output
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);

    // Double buffer
    g_hScreen[0] = CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE, 0,
        NULL, CONSOLE_TEXTMODE_BUFFER, NULL);
    g_hScreen[1] = CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE, 0,
        NULL, CONSOLE_TEXTMODE_BUFFER, NULL);

    // Hide cursor
    cci.dwSize = 1;
    cci.bVisible = FALSE;
    SetConsoleCursorInfo(g_hScreen[0], &cci);
    SetConsoleCursorInfo(g_hScreen[1], &cci);
}

void ScreenFlipping(void)
{
    SetConsoleActiveScreenBuffer(g_hScreen[g_nScreenIndex]);
    g_nScreenIndex = !g_nScreenIndex;
}

static const WORD DEFAULT_ATTR = 0x07;

void ScreenClear(void)
{
    COORD Coor = { 0, 0 };
    DWORD dw;
    FillConsoleOutputCharacter(g_hScreen[g_nScreenIndex], ' ', 80 * 25, Coor, &dw);
    FillConsoleOutputAttribute(g_hScreen[g_nScreenIndex], DEFAULT_ATTR, 80 * 25, Coor, &dw);
    SetConsoleTextAttribute(g_hScreen[g_nScreenIndex], DEFAULT_ATTR);
}

void ScreenRelease(void)
{
    CloseHandle(g_hScreen[0]);
    CloseHandle(g_hScreen[1]);
}

void ScreenPrint(int x, int y, char *string)
{
    DWORD dw;
    COORD CursorPosition = { (SHORT)x, (SHORT)y };
    SetConsoleCursorPosition(g_hScreen[g_nScreenIndex], CursorPosition);
    // convert UTF-8 -> wide and print
    int wlen = MultiByteToWideChar(CP_UTF8, 0, string, -1, NULL, 0);
    if (wlen <= 0) return;
    wchar_t* wbuf = (wchar_t*)malloc(sizeof(wchar_t) * wlen);
    if (!wbuf) return;
    MultiByteToWideChar(CP_UTF8, 0, string, -1, wbuf, wlen);
    WriteConsoleW(g_hScreen[g_nScreenIndex], wbuf, (DWORD)wcslen(wbuf), &dw, NULL);
    free(wbuf);
}

void ScreenPrintW(int x, int y, const wchar_t* ws){
    COORD pos = { (SHORT)x, (SHORT)y };
    DWORD written = 0;
    SetConsoleCursorPosition(g_hScreen[g_nScreenIndex], pos);
    WriteConsoleW(g_hScreen[g_nScreenIndex], ws, (DWORD)wcslen(ws), &written, NULL);
}

void SetColor(unsigned short color)
{
    SetConsoleTextAttribute(g_hScreen[g_nScreenIndex], color);
}

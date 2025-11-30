#pragma once
#include <wchar.h>

void ScreenInit();
void ScreenFlipping();
void ScreenClear();
void ScreenRelease();
void ScreenPrint(int x, int y, char* string);
void ScreenPrintW(int x, int y, const wchar_t* ws);
void SetColor(unsigned short color);

// MapUI.c
// UI rendering (borders, score, hold/next previews, ready/result)
#include "MapUI.h"
#include "Screen.h"
#include <stdio.h>
#include "BlockData.h"

#define PREVIEW_BOX_WIDTH   14
#define PREVIEW_BOX_HEIGHT   6
#define PREVIEW_INNER_X      4
#define PREVIEW_INNER_Y      1
#define PREVIEW_COL_GAP      2
#define PREVIEW_ROW_GAP      2

// 전체 틀 (ASCII)
void Map(void) {
    ScreenPrint(0, 0, "########################");
    for (int i = 1; i < 18; i++) {
        ScreenPrint(0, i, "##");
        ScreenPrint(22, i, "##");
    }
    ScreenPrint(0, 18, "########################");
}

// 점수 틀
void MapScore(int* nScore) {
	char chScore[15];
	sprintf_s(chScore, sizeof(chScore), "Score : %d", (*nScore));
	ScreenPrint(60, 10, chScore);
}

// TETRIO-like palette (type index 기준): Z, S, L, J, T, O, I
static unsigned short ColorForType(int type){
    static const unsigned short colors[7] = {
        0x0C, // Z = red
        0x0A, // S = green
        0x06, // L = orange
        0x01, // J = blue
        0x0D, // T = purple
        0x0E, // O = yellow
        0x0B  // I = cyan
    };
    if (type < 0 || type > 6) return 0x07;
    return colors[type];
}

// 프리뷰 박스 테두리
static void DrawPreviewBox(int x, int y) {
    ScreenPrint(x, y, "################");
    ScreenPrint(x, y + PREVIEW_BOX_HEIGHT, "################");
    for (int i = 0; i < PREVIEW_BOX_HEIGHT; i++) {
        ScreenPrint(x, y + 1 + i, "##");
        ScreenPrint(x + PREVIEW_BOX_WIDTH, y + 1 + i, "##");
    }
}

// 프리뷰 블록 (회전 0 기준)
static void DrawPreviewBlock(int type, int originX, int originY) {
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            ScreenPrint(originX + j * 2, originY + i, "  ");
        }
    }
    if (type < 0 || type > 6) return;

    unsigned short col = ColorForType(type);
    SetColor(col);
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            if (BlockArr[type][0][i][j] == 1) {
                ScreenPrint(originX + j * 2, originY + i, "[]");
            }
        }
    }
    SetColor(0x07);
}

void MapHold(int holdType) {
    const int x = 25;
    const int y = 0;
    DrawPreviewBox(x, y);
    ScreenPrint(x + 4, y, "HOLD");
    DrawPreviewBlock(holdType, x + PREVIEW_INNER_X, y + PREVIEW_INNER_Y);
    SetColor(0x07);
}

void MapNext(const int* nextTypes, int count) {
    const int baseX = 25;
    const int baseY = 8;
    const int colStride = PREVIEW_BOX_WIDTH + PREVIEW_COL_GAP; // 16
    const int rowStride = PREVIEW_BOX_HEIGHT + PREVIEW_ROW_GAP; // 8

    for (int idx = 0; idx < count; idx++) {
        int col = idx % 2;
        int row = idx / 2;
        int x = baseX + col * colStride;
        int y = baseY + row * rowStride;

        char label[16];
        snprintf(label, sizeof(label), "NEXT %d", idx + 1);
        ScreenPrint(x, y - 1, label);

        DrawPreviewBox(x, y);
        DrawPreviewBlock(nextTypes[idx], x + PREVIEW_INNER_X, y + PREVIEW_INNER_Y);
        SetColor(0x07);
    }
}

// 준비 화면
void MapReady1(void) {
    ScreenPrint(6, 2, "T E T R I S");
    ScreenPrint(4, 7, "Left   : <-");
    ScreenPrint(4, 8, "Right  : ->");
    ScreenPrint(4, 9, "Rotate : ^");
    ScreenPrint(4, 10, "Down   : v");
    ScreenPrint(4, 11, "Hold   : C");
}

// 준비 화면
void MapReady2(void) {
    ScreenPrint(4, 14, "Press Enter" );
    ScreenPrint(12, 15, "to Start");
}

// 결과
void MapResult(int* nScore, int* check_clear) {
    if(*check_clear==1)
        ScreenPrint(5, 5, "Congratulations!");
	else
		ScreenPrint(5, 5, "Game Over");
	char chScore[15];
	sprintf_s(chScore, sizeof(chScore), "Score : %d", (*nScore));
	ScreenPrint(5, 10, chScore);
    for (int i = 1; i < 11; i++) {
        ScreenPrint(i*2, 17, "  ");
    }
}

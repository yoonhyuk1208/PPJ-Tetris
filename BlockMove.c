#include "BlockData.h"
#include "PanData.h"

// ?? ?? ?? ??? (? 1)
static int collect_piece(int (*arr)[12], POS out[4]){
    int n = 0;
    for (int i = 1; i < 21; i++){
        for (int j = 1; j < 11; j++){
            if (arr[i][j] == 1){
                out[n].x = i;
                out[n].y = j;
                n++;
            }
        }
    }
    return n;
}

static int is_blocked(int (*arr)[12], int x, int y){
    return arr[x][y] >= 2;
}

void LeftMove(int (*arr)[12]){
    POS cur[4];
    if (collect_piece(arr, cur) != 4) return;
    for (int i = 0; i < 4; i++){
        if (is_blocked(arr, cur[i].x, cur[i].y - 1)) return;
    }
    for (int i = 0; i < 4; i++) arr[cur[i].x][cur[i].y] = 0;
    for (int i = 0; i < 4; i++) arr[cur[i].x][cur[i].y - 1] = 1;
}

void RightMove(int (*arr)[12]){
    POS cur[4];
    if (collect_piece(arr, cur) != 4) return;
    for (int i = 0; i < 4; i++){
        if (is_blocked(arr, cur[i].x, cur[i].y + 1)) return;
    }
    for (int i = 0; i < 4; i++) arr[cur[i].x][cur[i].y] = 0;
    for (int i = 0; i < 4; i++) arr[cur[i].x][cur[i].y + 1] = 1;
}

// ? ? ??? ??. ??? ?? ??? ??(2)?? 1 ??
int SoftDropOne(int (*arr)[12], int curType){
    POS cur[4];
    if (collect_piece(arr, cur) != 4) return 0;
    for (int i = 0; i < 4; i++){
        if (is_blocked(arr, cur[i].x + 1, cur[i].y)){
            for (int k = 0; k < 4; k++) arr[cur[k].x][cur[k].y] = 2 + curType;
            return 1;
        }
    }
    for (int i = 0; i < 4; i++) arr[cur[i].x][cur[i].y] = 0;
    for (int i = 0; i < 4; i++) arr[cur[i].x + 1][cur[i].y] = 1;
    return 0;
}

// ????: ?? ??? ??/?? ?? ??
void HardDrop(int (*arr)[12], int curType){
    POS cur[4];
    if (collect_piece(arr, cur) != 4) return;
    int minDrop = 30;
    for (int i = 0; i < 4; i++){
        int d = 0;
        int x = cur[i].x;
        int y = cur[i].y;
        while (!is_blocked(arr, x + d + 1, y)) d++;
        if (d < minDrop) minDrop = d;
    }
    for (int i = 0; i < 4; i++) arr[cur[i].x][cur[i].y] = 0;
    for (int i = 0; i < 4; i++) arr[cur[i].x + minDrop][cur[i].y] = 2 + curType;
}

static int can_place(int (*arr)[12], int type, int rot, int baseX, int baseY){
    for (int i = 0; i < 4; i++){
        for (int j = 0; j < 4; j++){
            if (BlockArr[type][rot][i][j]){
                int x = baseX + i;
                int y = baseY + j;
                if (x < 1 || x > 20 || y < 1 || y > 10) return 0;
                if (arr[x][y] >= 2) return 0;
            }
        }
    }
    return 1;
}

int Rotate(int (*arr)[12], int nType, int nextRot){
    POS cur[4];
    if (collect_piece(arr, cur) != 4) return 0;

    int minX = cur[0].x, minY = cur[0].y;
    for (int i = 1; i < 4; i++){
        if (cur[i].x < minX) minX = cur[i].x;
        if (cur[i].y < minY) minY = cur[i].y;
    }

    const int kicks[6][2] = { {0,0},{0,-1},{0,1},{-1,0},{1,0},{0,-2} };
    for (int k = 0; k < 6; k++){
        int baseX = minX + kicks[k][0];
        int baseY = minY + kicks[k][1];
        if (!can_place(arr, nType, nextRot, baseX, baseY)) continue;

        for (int i = 0; i < 4; i++) arr[cur[i].x][cur[i].y] = 0;
        for (int i = 0; i < 4; i++){
            for (int j = 0; j < 4; j++){
                if (BlockArr[nType][nextRot][i][j]){
                    int tx = baseX + i;
                    int ty = baseY + j;
                    if (tx < 1 || tx > PLAY_BOTTOM || ty < 1 || ty > PLAY_RIGHT) continue; // 방어적 경계 체크
                    arr[tx][ty] = 1;
                }
            }
        }
        return 1;
    }
    return 0;
}

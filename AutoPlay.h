#pragma once
#include "PanData.h"   // H, W, PLAY_*

// 보드 위에서 최적 수(회전/왼쪽열/스코어)만 계산하는 순수 플래너
// fast=1이면 1-ply(다음 블록 생략)로 빠르게, fast=0이면 2-ply로 정밀
int AutoPlanBest(const int board[H][W], int curType, int nextType,
                 int* outRot, int* outLeft, int* outScore, int fast);

// 실제 게임 보드(nArr)에서 해당 수를 실행(회전→좌/우→하드드랍)
void AutoPlay(int curType, int nextType, int sprintMode);

// 시뮬레이션용: 조각을 드랍+고정+라인클리어 수행, 이번 수로 지운 줄 수를 lines_out로 반환
int sim_drop_lock_clear_ex(int F[H][W], int type, int rot, int xLeft, int* lines_out);

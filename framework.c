#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <ctype.h>
#include <windows.h>
#include <conio.h>
#include <inttypes.h>

#include "Screen.h"
#include "PanData.h"
#include "MapUI.h"
#include "BlockData.h"
#include "BlockMove.h"
#include "BlockSpwan.h"
#include "AutoPlay.h"
#include "Weights.h"
#include "Tuner.h"
#include "GameState.h"

// 소리 출력 PlaySound함수
#include <mmsystem.h>

// 키 값
#define LEFT  75
#define RIGHT 77
#define UP    72
#define DOWN  80

// ===== 전역 상태 =====
int nScore = 0;       // 누적 점수
int nBlockType;       // 현재 블록
int nBlockType2;      // 다음 블록
int nRot;             // 회전 인덱스(0~3)
int nSpawning;        // 0=스폰대기, 1=하강중, 3=고정 완료
int nSpeed;           // 낙하 속도(ms)
int seed;             // 랜덤 시드
int check_clear = 0; // 클리어 여부

typedef enum { MODE_NORMAL=0, MODE_40L=1 } GAME_MODE;
GAME_MODE gMode = MODE_NORMAL;
typedef enum { MANUAL, AUTO } CONTROL_MODE;
CONTROL_MODE gControl = MANUAL;

int gLinesCleared = 0;    // 40L 진행
int gPiecesUsed   = 0;

// 고해상도 타이머 (40L)
static LARGE_INTEGER gFreq, gT0, gT1;
static void SprintTimerInit(){ QueryPerformanceFrequency(&gFreq); }
static void SprintTimerStart(){ QueryPerformanceCounter(&gT0); }
static int64_t SprintTimerStopMs(){
    QueryPerformanceCounter(&gT1);
    return (int64_t)((gT1.QuadPart - gT0.QuadPart) * 1000LL / gFreq.QuadPart);
}

// 40L 결과 저장
static void SaveRecord40L(int64_t ms, int pieces) {
    FILE *fp = fopen("records_40L.csv","a");
    if (!fp) return;
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(fp, "%04d-%02d-%02d %02d:%02d:%02d,%" PRId64 ",%d\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
            (int64_t)ms, pieces);
    fclose(fp);
}

// 진행 표시 콜백(UI)
static void TuneProgressUI(
    int iter, int iters,
    long long bestMs, long long minFastMs,
    int span,
    const Weights* best, const Weights* cur
){
    char buf[128];
    int pct = (iter * 100) / (iters ? iters : 1);
    int bar = pct / 5; // 20칸 진행바

    ScreenClear();

    ScreenPrint(5, 3,  "[Auto-Tune 40L]");
    snprintf(buf, sizeof(buf), "Iter: %d / %d (%d%%)", iter, iters, pct);
    ScreenPrint(5, 4, buf);

    // 진행바 (20칸)
    char barbuf[32]; int i;
    for(i=0;i<20;i++) barbuf[i] = (i<bar)?'#':'-';
    barbuf[20]='\0';
    snprintf(buf, sizeof(buf), "[%s]", barbuf);
    ScreenPrint(5, 5, buf);

    snprintf(buf, sizeof(buf),
         "Best(ms): %" PRId64 "   MinFast(ms): %" PRId64 "   Span: %d",
         (int64_t)bestMs, (int64_t)minFastMs, span);
    ScreenPrint(5, 7, buf);

    // 가중치 일부 보여주기
    snprintf(buf, sizeof(buf), "W_lines: %d/%d/%d/%d",
        best->W_lines1, best->W_lines2, best->W_lines3, best->W_lines4);
    ScreenPrint(5, 9, buf);

    snprintf(buf, sizeof(buf), "W_aggH=%d  W_holes=%d  W_bump=%d",
        best->W_agg_height, best->W_holes, best->W_bump);
    ScreenPrint(5,10, buf);

    ScreenPrint(5,12, "This blocks the game. Please wait...");
    ScreenFlipping();
}

// ===== 게임 스테이트 =====
STAGE Stage;
clock_t Oldtime = 0;

// ===== 초기화 =====
static void init(){
    PanMap(nArr);          // 보드 리셋
    Stage = READY;
	gControl = MANUAL;
    nRot = 1;
    nSpawning = 0;
    nSpeed = 500;
    nScore = 0;

    gMode = MODE_NORMAL;
    gLinesCleared = 0;
    gPiecesUsed   = 0;
    SprintTimerInit();

    // 가중치 로드 (없으면 기본값)
    if (!WeightsLoad("weights_40L.csv")) {
        WeightsSetDefault40L();
        WeightsSave("weights_40L.csv", &gW);
    }
}

// ===== 업데이트 =====
static void Update() {
    clock_t Curtime = clock();

    switch (Stage) {
    case READY:
        break;

    case RUNNING: {
        // --- 스폰 & 오토플레이(하드드랍) ---
        if (nSpawning == 0){
            // NOTE: BlockSpwan/BlockSpwan2의 시그니처가 (void)인 버전 기준
            //       네 프로젝트가 seed를 넘기는 형태라면 인자만 맞춰줘!
            nBlockType  = BlockSpwan(seed);
            nBlockType2 = BlockSpwan(seed);
            BlockSpwan2(nArr, &nBlockType, seed);

            nSpawning = 1;
            gPiecesUsed++;

            // 스폰 직후 최적 수 실행 (2-ply; sprintMode 플래그 전달)
			if(gControl == AUTO)
            	AutoPlay(nBlockType, nBlockType2, (gMode==MODE_40L));
        }
        if (nSpawning == 3){
            nBlockType  = nBlockType2;
            nBlockType2 = BlockSpwan(seed);
            BlockSpwan2(nArr, &nBlockType, seed);

            nSpawning = 1;
            nRot = 1;
            gPiecesUsed++;
			
			if(gControl == AUTO)
            	AutoPlay(nBlockType, nBlockType2, (gMode==MODE_40L));
        }

        // --- (구) 낙하 루프: 하드드랍을 쓰므로 거의 건드릴 게 없음 ---
        //     그래도 남겨두되 '1' 블록이 없는 경우는 자연히 skip
        if (Curtime - Oldtime > nSpeed) {
            Oldtime = Curtime;

            // 현재 낙하 중인 조각을 찾고 한 칸 내리기
            int picked = 0;
            for (int i = 1; i < 21; i++) {
                for (int j = 1; j < 11; j++) {
                    if (nArr[i][j] == 1) {
                        Block_pos[picked].Pos.x = i;
                        Block_pos[picked].Pos.y = j;
                        nArr[i][j] = 0;
                        picked++;
                    }
                }
            }

            if (picked == 4) {
                // 충돌 검사
                int collide = 0;
                for (int i = 0; i < 4; i++) {
                    if (nArr[Block_pos[i].Pos.x + 1][Block_pos[i].Pos.y] == 2) {
                        collide = 1; break;
                    }
                }
                if (collide) {
                    for (int j = 0; j < 4; j++)
                        nArr[Block_pos[j].Pos.x][Block_pos[j].Pos.y] = 2;
                    nSpawning = 3;
                    nRot = 1;
                } else {
                    for (int i = 0; i < 4; i++)
                        nArr[Block_pos[i].Pos.x + 1][Block_pos[i].Pos.y] = 1;
                }
            }
        }

        // --- 라인 클리어 ---
        int clearedThisPass = 0;
        for (int i = 1; i < 21; i++) {
            int filled = 0;
            for (int j = 1; j < 11; j++)
                if (nArr[i][j] == 2) filled++;

            if (filled == 10) {
                clearedThisPass++;
				nScore += 100; // 한 줄당 100점
                for (int r = i; r > 1; r--)
                    for (int c = 1; c < 11; c++)
                        nArr[r][c] = nArr[r-1][c];
                for (int c = 1; c < 11; c++) nArr[1][c] = 0;
                i--; // 당겨진 줄 재검사
            }
        }

        // --- 40L 스프린트 ---
        if (gMode == MODE_40L && clearedThisPass > 0){
            gLinesCleared += clearedThisPass;
            if (gLinesCleared >= 40){
                int64_t ms = SprintTimerStopMs();
                SaveRecord40L(ms, gPiecesUsed);
				check_clear = 1;
                Stage = RESULT;
            }
        }

        // --- 게임오버: 3행에 고정블록 존재 ---
        for (int i = 1; i < 11; i++) {
            if (nArr[3][i] == 2) {
                Stage = RESULT;
                break;
            }
        }
        break;
    }

    case RESULT:
        break;
    }
}

// ===== 렌더 =====
static void Render() {
    clock_t Curtime = clock();
    ScreenClear();

    switch (Stage) {
    case READY:
        MapReady1();
        if (Curtime % 1000 > 500) MapReady2();
        ScreenPrint(26, 2, "Press '1' = NORMAL");
        ScreenPrint(26, 3, "Press '2' = 40-LINE Sprint");
        ScreenPrint(26, 4, "Press '3' = Auto-Tune 40L");
        break;

    case RUNNING: {
        // 보드
        for (int i = 4; i < 22; i++) {
            for (int j = 0; j < 12; j++) {
                if (nArr[i][j] == 2) {
                    ScreenPrint(j * 2, i - 3, "▩");
                } else if (nArr[i][j] == 1) {
                    ScreenPrint(j * 2, i - 3, "■");
                } else {
                    ScreenPrint(j * 2, i - 3, "  ");
                }
            }
        }

        // 다음 블록
        MapNext(&nBlockType2);

        // 우측 패널
        if (gMode == MODE_40L){
            LARGE_INTEGER now; QueryPerformanceCounter(&now);
            long long ms = (now.QuadPart - gT0.QuadPart) * 1000LL / gFreq.QuadPart;
            char buf[64];
            ScreenPrint(26, 7, "== 40L SPRINT ==");
            snprintf(buf, sizeof(buf), "Lines: %d / 40", gLinesCleared); ScreenPrint(26, 4, buf);
            snprintf(buf, sizeof(buf), "Pieces: %d", gPiecesUsed);       ScreenPrint(26, 5, buf);
            snprintf(buf, sizeof(buf), "Time: %" PRId64 " ms", (int64_t)ms);
            ScreenPrint(26, 6, buf);
        } else {
            MapScore(&nScore);
        }
        break;
    }

    case RESULT:
        MapResult(&nScore, &check_clear);
        ScreenPrint(26, 2, "Press 'R' = RESTART");
        break;
    }

    Map();
    ScreenFlipping();
}

// ===== 메인 =====
int main(void) {
    seed = (int)time(NULL);
	srand(seed);

    ScreenInit();
    init();  // ★ 한 번만 초기화

    while (1) {
        if (_kbhit()) {
            int nKey = _getch();

            // ----- READY 상태에서 모드 선택 -----
            if (Stage == READY){
                if (nKey == '1'){
                    gMode = MODE_NORMAL;
                    PlaySound(TEXT("tetris.wav"), NULL, SND_ASYNC | SND_LOOP);
                    Stage = RUNNING;
                }
                else if (nKey == '2'){
                    gMode = MODE_40L;
                    gLinesCleared = 0;
                    gPiecesUsed   = 0;
                    PlaySound(TEXT("tetris.wav"), NULL, SND_ASYNC | SND_LOOP);
                    SprintTimerStart();
                    Stage = RUNNING;
                }
                else if (nKey == '3'){
					// 시작 안내 1회
					ScreenClear();
					ScreenPrint(5, 4, "[Auto-Tune] Starting...");
					ScreenFlipping();

					// 콜백으로 진행 상황 실시간 렌더
					RunAutoTune40LEx(/*trials=*/5, /*iters=*/300, "weights_40L.csv", TuneProgressUI);

					// 완료 후 새 가중치 로드 및 안내
					WeightsLoad("weights_40L.csv");
					init();
					ScreenClear();
					ScreenPrint(5, 4, "[Auto-Tune] Done! New weights loaded.");
					ScreenPrint(5, 6, "Press any key to continue...");
					ScreenFlipping();
					_getch();
					Stage = READY;
                }
                else if (nKey == 13){ // Enter
                    PlaySound(TEXT("tetris.wav"), NULL, SND_ASYNC | SND_LOOP);
                    Stage = RUNNING;
                }
            }

            // ----- RESULT 상태에서 재시작 -----
            else if (Stage == RESULT){
                if (nKey == 'R' || nKey == 'r'){
                    init();           // ★ 전체 초기화로 충분
                    Stage = READY;
                }
            }

            // ----- RUNNING 상태에서 수동 조작 -----
            else if (Stage == RUNNING){
				if (nKey == 'A' || nKey == 'a'){
					gControl = AUTO;
					AutoPlay(nBlockType, nBlockType2, (gMode==MODE_40L));
				}
					
                if (nKey == 224) {           // Arrow prefix
                    nKey = _getch();
                    int k = 1;
                    switch (nKey) {
                    case LEFT:
                        Beep(200, 200);
                        for (int i = 0; i < 4; i++) {
                            if (nArr[Block_pos[i].Pos.x][Block_pos[i].Pos.y-1] == 2) { k = 0; break; }
                        }
                        if (k) LeftMove(nArr);
                        break;

                    case RIGHT:
                        Beep(200, 200);
                        for (int i = 0; i < 4; i++) {
                            if (nArr[Block_pos[i].Pos.x][Block_pos[i].Pos.y+1] == 2) { k = 0; break; }
                        }
                        if (k) RightMove(nArr);
                        break;

                    case UP:
                        Beep(200, 200);
                        Rotate(nArr, nBlockType, Block_pos, nRot);
                        if (nRot == 3) nRot = -1;
                        nRot++;
                        break;

                    case DOWN:
                        DownMove(nArr);
                        nSpawning = 3;
                        nRot = 1;
                        break;
                    }
                }
            }
        }

        Update();  // 데이터 갱신
        Render();  // 화면 출력
    }

    ScreenRelease();
    return 0;
}

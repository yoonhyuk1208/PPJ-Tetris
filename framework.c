// 테트리스 // 

#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <ctype.h>
#include <windows.h>
#include <time.h>
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
#include<mmsystem.h>

#define LEFT 75 
#define RIGHT 77 
#define UP 72 
#define DOWN 80 

int nScore = 0; // 전역변수 for 누적점수  
int nBlockType; // 0~6까지의 블록 모형 
int nBlockType2; // 다음에 출력할 블록 모형
int nRot; // 회전 차순의 지표 0,1,2,3,0,1,2,3,0,1,2,3.... 무한반복
int nSpawning; // 1이면 내려가는 중, 0이면 스폰준비완료
int nSpeed; // 내려가는 속도 조절



typedef enum { MODE_NORMAL=0, MODE_40L=1 } GAME_MODE;
GAME_MODE gMode = MODE_NORMAL;

int gLinesCleared = 0;
int gPiecesUsed   = 0;

// 고해상도 타이머
static LARGE_INTEGER gFreq, gT0, gT1;
static void SprintTimerInit(){ QueryPerformanceFrequency(&gFreq); }
static void SprintTimerStart(){ QueryPerformanceCounter(&gT0); }
static int64_t SprintTimerStopMs(){
    QueryPerformanceCounter(&gT1);
    return (int64_t)((gT1.QuadPart - gT0.QuadPart) * 1000LL / gFreq.QuadPart);
}

static void SaveRecord40L(int64_t ms, int pieces) {
    FILE *fp = fopen("records_40L.csv","a");
    if (!fp) return;
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(fp, "%04d-%02d-%02d %02d:%02d:%02d,%" PRId64 ",%d\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
            (int64_t)ms, pieces);
    fclose(fp);
}

STAGE Stage;


void init(){
    PanMap(nArr);
    Stage = READY;
    nRot = 1;
    nSpawning = 0;
    nSpeed = 500;

    gMode = MODE_NORMAL;
    gLinesCleared = 0;
    gPiecesUsed   = 0;
    SprintTimerInit();

    // 가중치 로드 (없으면 기본)
    if(!WeightsLoad("weights_40L.csv")) WeightsSetDefault40L();
}


clock_t Oldtime = 0;
void Update() {
	clock_t Curtime = clock();
	switch (Stage) {
	case READY :
		break;
	case RUNNING:
		//브금 재생
		// 블록 생성하기 
		// 현재 1,1 좌표에서 생성되어 내려온다.
		if (nSpawning == 0){
			nBlockType  = BlockSpwan();
			nBlockType2 = BlockSpwan();
			BlockSpwan2(nArr, &nBlockType);
			nSpawning = 1;
			gPiecesUsed++;
			AutoPlay(nBlockType, nBlockType2, (gMode==MODE_40L));
		}
		if (nSpawning == 3){
			nBlockType  = nBlockType2;
			nBlockType2 = BlockSpwan();
			BlockSpwan2(nArr, &nBlockType);
			nSpawning = 1;
			nRot = 1;
			gPiecesUsed++;
			AutoPlay(nBlockType, nBlockType2, (gMode==MODE_40L));
		}





		// 블록 내리기
		//===================================
		int n = 0;
		if (Curtime - Oldtime > nSpeed) { // 내려가는 속도 조절
			Oldtime = Curtime;
			// 1) 저장받기
			for (int i = 1; i < 21; i++) {
				for (int j = 1; j < 11; j++) {
					if (nArr[i][j] == 1) {
						nArr[i][j] = 0;
						Block_pos[n].Pos.x = i;
						Block_pos[n].Pos.y = j;
						n++;
					}
				}
			}

			// 1-1) 충돌처리
			for (int i = 0; i < 4; i++) {
				if (nArr[Block_pos[i].Pos.x + 1][Block_pos[i].Pos.y] == 2) {// 내려가게 될 공간이 2라면 충돌...
					for (int j = 0; j < 4; j++) {
						nArr[Block_pos[j].Pos.x][Block_pos[j].Pos.y] = 2;
					}
					nSpawning = 3;
					nRot = 1;
					break;
				}
				// 1-2) 한칸내려 재배열하기 ( 위에서 충돌하지 않았다면)
				else if (i == 3) {
					for (int i = 0; i < 4; i++) {
						nArr[Block_pos[i].Pos.x + 1][Block_pos[i].Pos.y] = 1;
					}
				}
			}



		}
		//===================================

		// 완성된 행 검사 후 빼기
		int clearedThisPass = 0;
		int m = 0;
		for (int i = 1; i < 21; i++) {
			for (int j = 1; j < 11; j++) if (nArr[i][j] == 2) m++;
			if (m == 10) {
				clearedThisPass++;
				for (int r = i; r > 1; r--)
					for (int c = 1; c < 11; c++)
						nArr[r][c] = nArr[r-1][c];
				for (int c = 1; c < 11; c++) nArr[1][c] = 0;
				i--; // 당겨진 줄 재검사
			}
			m = 0;
		}

		if (gMode == MODE_40L && clearedThisPass > 0){
			gLinesCleared += clearedThisPass;
			if (gLinesCleared >= 40){
				int64_t ms = SprintTimerStopMs();
				SaveRecord40L(ms, gPiecesUsed);
				Stage = RESULT;
			}
		}

		// 게임 패배 조건 - nArr의 3행에 2가 저장된다면 gg
		for (int i = 1; i < 11; i++) {
			if (nArr[3][i] == 2) {
				Stage = RESULT;
			}
		}
		break;
	case RESULT:

		break;
	}
	

}



void Render() {
	clock_t Curtime = clock();
	ScreenClear();
	//출력코드
	

	switch (Stage) {
	case READY :
		MapReady1();
		if (Curtime % 1000 > 500) MapReady2();
		ScreenPrint(26, 2, "Press '1' = NORMAL");
		ScreenPrint(26, 3, "Press '2' = 40-LINE Sprint");
		ScreenPrint(26, 4, "Press '3' = Auto-Tune 40L");
		break;
	case RUNNING :
		//=============================
		// 배열을 출력
		//=============================
		for (int i = 4; i < 22; i++) {
			for (int j = 0; j < 12; j++) {
				if (nArr[i][j] == 2) {
					ScreenPrint(j * 2, i - 3, "▩");
				}
				else if (nArr[i][j] == 1) {
					ScreenPrint(j * 2, i - 3, "■");
				}
				else {
					ScreenPrint(j * 2, i - 3, "  ");
				}
			}
		}
		// 다음 블록을 우측에서 미리 출력
		MapNext(&nBlockType2);

		if (gMode == MODE_40L){
			LARGE_INTEGER now; QueryPerformanceCounter(&now);
			long long ms = (now.QuadPart - gT0.QuadPart) * 1000LL / gFreq.QuadPart;
			char buf[64];
			ScreenPrint(26, 2, "== 40L SPRINT ==");
			sprintf(buf, "Lines: %d / 40", gLinesCleared);                   ScreenPrint(26, 4, buf);
			sprintf(buf, "Pieces: %d", gPiecesUsed);                         ScreenPrint(26, 5, buf);
			snprintf(buf, sizeof(buf), "Time: %" PRId64 " ms", (int64_t)ms); ScreenPrint(26, 6, buf);
		} else {
			MapScore(&nScore);
		}
		break;
	case RESULT:
		MapResult(&nScore);
		break;
	}
	Map();
	ScreenFlipping();
}

void Release() {

}

int main(void) {
	
	ScreenInit();
	init(); // 초기화
	
	while (1) {
		int nKey;
		while (1) {
			if (_kbhit()) {
				nKey = _getch();
				int k = 1;
				if (Stage == READY){
					PanMap(nArr);
					nRot = 1; nSpawning = 0;
					gLinesCleared = 0; gPiecesUsed = 0;
					if (nKey == '1'){
						gMode = MODE_NORMAL;
						PlaySound(TEXT("tetris.wav"), NULL, SND_ASYNC | SND_LOOP);
						Stage = RUNNING;
					}
					if (nKey == '2'){
						gMode = MODE_40L;
						gLinesCleared = 0;
						gPiecesUsed   = 0;
						PlaySound(TEXT("tetris.wav"), NULL, SND_ASYNC | SND_LOOP);
						SprintTimerStart();
						Stage = RUNNING;
					}
					if(nKey == '3'){
						 // 화면에 진행 상태 표시
						ScreenClear();
						ScreenPrint(5, 4, "[Auto-Tune] Running... (trials=5, iters=300)");
						ScreenPrint(5, 6, "Please wait. This will block the game.");
						ScreenFlipping();

						RunAutoTune40L(/*trials=*/5, /*iters=*/300, "weights_40L.csv");

						// 완료 표시 + 최신 가중치 로드
						WeightsLoad("weights_40L.csv");
						init();
						ScreenClear();
						ScreenPrint(5, 4, "[Auto-Tune] Done! New weights loaded.");
						ScreenPrint(5, 6, "Press any key to continue...");
						ScreenFlipping();
						_getch();

						Stage = READY;
					}
				}
				if (nKey == 13) {
					PlaySound(TEXT("tetris.wav"), NULL, SND_ASYNC | SND_LOOP);
					Stage = RUNNING;
				}
				if (nKey == 224) {
					nKey = _getch();
					switch (nKey) {
					case LEFT:
						Beep(200, 200);
						// 경계와 충돌처리
						for (int i = 0; i < 4; i++) {
							if (nArr[Block_pos[i].Pos.x][Block_pos[i].Pos.y-1] == 2) { // 못 간다.
								Beep(400, 200); 
								k = 0;
							}
						}
						// 경계와 충돌안되면 갈 수 있다.
						if (k) {
							LeftMove(nArr); // 좌로 이동
						}
						break;
					case RIGHT:
						Beep(200, 200);
						// 경계와 충돌처리
						for (int i = 0; i < 4; i++) {
							if (nArr[Block_pos[i].Pos.x][Block_pos[i].Pos.y + 1] == 2) { // 못 간다.
								Beep(400, 200);
								k = 0;
							}
						}
						// 경계와 충돌안되면 갈 수 있다.
						if (k) {
							RightMove(nArr); // 우로 이동
						}
						break;
					case UP : // 회전
						Beep(200, 200);
						Rotate(nArr,nBlockType, Block_pos, nRot);
						if (nRot == 3) {
							nRot = -1;
						}
						nRot++;
						break;
					case DOWN: // 바로 내려보리기
						DownMove(nArr);
						nSpawning = 3;
						nRot = 1;
					}
				}
				
			}
			Update();  // 데이터 갱신
			Render();  // 화면출력
		}
	

		
	}
	Release(); // 해제
	ScreenRelease();
	return 0;
}
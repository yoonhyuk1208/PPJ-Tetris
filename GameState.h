#pragma once
#include <time.h>   // clock_t 정의

// 게임 단계
typedef enum _STAGE { READY, RUNNING, RESULT } STAGE;

// ---- 전역 변수들: 여기서는 "선언"만 (extern) ----
extern int      nScore;
extern STAGE    Stage;
extern clock_t  Oldtime;

// (다른 모듈들이 필요로 하는 전역들도 함께 선언)
extern int      gLinesCleared;
extern int      gPiecesUsed;
extern int      nRot;
extern int      nSpawning;
extern int      nBlockType;
extern int      nBlockType2;

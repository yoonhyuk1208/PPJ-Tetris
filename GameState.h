#pragma once
#include <time.h> 

typedef enum _STAGE { READY, RUNNING, RESULT } STAGE;

#define NEXT_PREVIEW_COUNT 4

extern int      nScore;
extern STAGE    Stage;
extern clock_t  Oldtime;

extern int      gLinesCleared;
extern int      gPiecesUsed;
extern int      nHoldType;
extern int      nNextQueue[NEXT_PREVIEW_COUNT];
extern int      holdUsedThisTurn;
extern int      nRot;
extern int      nSpawning;
extern int      nBlockType;
extern int      nBlockType2;

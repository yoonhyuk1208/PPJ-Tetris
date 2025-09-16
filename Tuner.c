#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "Tuner.h"
#include "Weights.h"
#include "AutoPlay.h"
#include "BlockData.h"
#include "BlockMove.h"
#include "BlockSpwan.h"
#include "PanData.h"
#include "GameState.h"

extern int nArr[22][12];
extern int nBlockType, nBlockType2;
extern int nRot, nSpawning;
extern int gLinesCleared, gPiecesUsed;

static LARGE_INTEGER qpf;
static void t_init(){ QueryPerformanceFrequency(&qpf); }
static long long t_ms(){ LARGE_INTEGER t; QueryPerformanceCounter(&t);
    return (long long)(t.QuadPart * 1000LL / qpf.QuadPart); }

static void ResetBoard(){ PanMap(nArr); nRot=1; nSpawning=0; gLinesCleared=0; gPiecesUsed=0; }
static void CleanFalling(){ for(int i=1;i<=20;i++) for(int j=1;j<=10;j++) if(nArr[i][j]==1) nArr[i][j]=0; }

static int ClearLines(){
    int cleared=0, m=0;
    for(int i=1;i<=20;i++){
        m=0;
        for(int j=1;j<=10;j++)
            if(nArr[i][j]==2) m++;
        if(m==10){
            cleared++;
            for(int r=i;r>1;r--) for(int c=1;c<=10;c++) nArr[r][c]=nArr[r-1][c];
            for(int c=1;c<=10;c++) nArr[1][c]=0;
            i--;
        }
    }
    return cleared;
}

static long long SimRun40L(unsigned seed){
    srand(seed);
    ResetBoard(); CleanFalling();
    long long t0 = t_ms();

    nBlockType = BlockSpwan(); nBlockType2 = BlockSpwan();
    BlockSpwan2(nArr, &nBlockType); gPiecesUsed++; nSpawning=1;

    while(gLinesCleared < 40){
        AutoPlay(nBlockType, nBlockType2, 1);
        for(int i=1;i<=20;i++) for(int j=1;j<=10;j++) if(nArr[i][j]==1) nArr[i][j]=2;
        int c = ClearLines(); gLinesCleared += c;

        // fail: 3행에 고정 블록
        for(int j=1;j<=10;j++) if(nArr[3][j]==2) return 1e12;

        nBlockType = nBlockType2;
        nBlockType2 = BlockSpwan();
        BlockSpwan2(nArr, &nBlockType); gPiecesUsed++; nSpawning=1;
    }
    return t_ms() - t0;
}

static long long FitnessAvgMs(int trials){
    long long sum=0;
    for(int k=0;k<trials;k++){
        unsigned seed = 12345u + 7919u*k;
        sum += SimRun40L(seed);
    }
    return sum / (trials?trials:1);
}

static void Clamp(Weights* w){
    if(w->W_line  < 0) w->W_line=0;
    if(w->W_line  > 20000) w->W_line=20000;

    if(w->W_holes < 0) w->W_holes=0;
    if(w->W_holes > 1000)  w->W_holes=1000;

    if(w->W_height< 0) w->W_height=0;
    if(w->W_height> 200)   w->W_height=200;

    if(w->W_bump  < 0) w->W_bump=0;
    if(w->W_bump  > 200)   w->W_bump=200;

    if(w->W_well  < 0) w->W_well=0;
    if(w->W_well  > 200)   w->W_well=200;

    if(w->W_trans < 0) w->W_trans=0;
    if(w->W_trans > 200)   w->W_trans=200;
}

static Weights Perturb(const Weights* base, int span){
    Weights w = *base;
    w.W_line   += (rand()%(2*span+1))-span;
    w.W_holes  += (rand()%(2*span+1))-span/2;
    w.W_height += (rand()%(2*span+1))-span/2;
    w.W_bump   += (rand()%(2*span+1))-span/2;
    w.W_well   += (rand()%(2*span+1))-span/3;
    w.W_trans  += (rand()%(2*span+1))-span/3;
    Clamp(&w);
    return w;
}

void RunAutoTune40L(int trials, int iters, const char* out_csv){
    int nArr_backup[22][12]; memcpy(nArr_backup, nArr, sizeof(nArr_backup));
    int bt_bak=nBlockType, bt2_bak=nBlockType2;
    int rot_bak=nRot, spawn_bak=nSpawning;
    int lines_bak=gLinesCleared, pieces_bak=gPiecesUsed;
    int score_bak=nScore; STAGE stage_bak=Stage; clock_t old_bak=Oldtime;

    t_init();

    // ===== 가중치 로드; 없으면 기본값 저장해서 파일을 먼저 만든다 =====
    if (!WeightsLoad(out_csv)) {
        WeightsSetDefault40L();
        /* 파일이 없거나 읽기 실패면 즉시 생성 */
        WeightsSave(out_csv, &gW);
    }

    Weights best = gW, cur = gW;
    long long bestMs = FitnessAvgMs(trials);
    int span=200, no_improve=0;

    for (int it=1; it<=iters; it++){
        Weights cand = Perturb(&cur, span);
        gW = cand;
        long long ms = FitnessAvgMs(trials);

        if (ms < bestMs){
            bestMs = ms; best = cand; cur = cand; no_improve=0;
            /* 새 베스트 즉시 저장 */
            WeightsSave(out_csv, &best);
            if (span>20) span -= 10;
        } else {
            no_improve++;
            if (no_improve % 25 == 0 && span < 400) span += 20;
            if (no_improve % 60 == 0) cur = Perturb(&best, 300);
        }
    }

    /* 루프 종료 후에도 무조건 한 번 더 저장(개선 없었어도 보장) */
    gW = best;
    WeightsSave(out_csv, &best);

    // ===== 전역 상태 복원 =====
    memcpy(nArr, nArr_backup, sizeof(nArr_backup));
    nBlockType=bt_bak; nBlockType2=bt2_bak;
    nRot=rot_bak; nSpawning=spawn_bak;
    gLinesCleared=lines_bak; gPiecesUsed=pieces_bak;
    nScore=score_bak; Stage=stage_bak; Oldtime=old_bak;
}

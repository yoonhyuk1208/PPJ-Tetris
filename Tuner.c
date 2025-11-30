// Tuner.c — 4-특징 휴리스틱 튜너 + 진행 콜백

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <windows.h>

#include "Tuner.h"
#include "Weights.h"
#include "AutoPlay.h"   // AutoPlanBest, sim_drop_lock_clear_ex
#include "PanData.h"    // H, W, PLAY_* , PanMap(...)
#include "BlockData.h"
#include "BlockMove.h"
#include "GameState.h"  // nArr, nBlockType.., Stage, Oldtime 등 extern

// === 튜닝 파라미터 ===
enum { TRIALS_FAST = 1, TRIALS_FULL = 3, BATCH = 8, TOPK = 2 };
enum { EARLY_PIECES = 40, EARLY_MIN_LINES = 2 };
enum { HOLE_CAP = 18, MAXH_CAP = 16 };
enum { TIME_CAP_MS = 30000, PIECE_CAP = 500 };

static const long long FAIL_MS = 1000000000000LL;

// ---------- 경량 PRNG + 7-bag ----------
static unsigned xors(uint32_t* s){ *s ^= *s<<13; *s ^= *s>>17; *s ^= *s<<5; return *s; }
static void bag_init(int bag[7]){ for(int i=0;i<7;i++) bag[i]=i; }
static void bag_shuffle(int bag[7], uint32_t* st){
    for(int i=6;i>0;i--){ unsigned r = xors(st)%(i+1); int t=bag[i]; bag[i]=bag[r]; bag[r]=t; }
}
static int next_piece(uint32_t* st, int* idx, int bag7[7]){
    if (*idx >= 7){ bag_shuffle(bag7, st); *idx = 0; }
    return bag7[(*idx)++];
}

// ---------- 보드 통계(조기중단용) ----------
static void hole_and_maxH(const int F[H][W], int* holes, int* maxh){
    int h=0, mh=0;
    for(int j=PLAY_LEFT;j<=PLAY_RIGHT;j++){
        int seen=0;
        for(int i=PLAY_TOP;i<=PLAY_BOTTOM;i++){
            if(F[i][j]>=2){
                if(!mh) mh=(PLAY_BOTTOM+1)-i;
                seen=1;
            } else if(seen && F[i][j]==0){
                h++;
            }
        }
    }
    if (holes) *holes = h;
    if (maxh)  *maxh  = mh;
}

// ---------- 40L 시뮬(빠른/정밀 공용) ----------
static long long SimRun40L_fast(unsigned seed, int fast){
    int B[H][W];
    PanMap(B);  // 보드를 지역 배열 B에 초기화 (프로젝트의 PanMap이 보드 포인터 받도록 구현되어 있어야 함)

    uint32_t st = seed?seed:1u;
    int bag7[7]; bag_init(bag7); bag_shuffle(bag7, &st);
    int bi=7;

    int cur = next_piece(&st, &bi, bag7);
    int nxt = next_piece(&st, &bi, bag7);

    LARGE_INTEGER qpf; QueryPerformanceFrequency(&qpf);
    LARGE_INTEGER t0;  QueryPerformanceCounter(&t0);

    int lines=0, pieces=0;
    int piece_ckpt=0, lines_ckpt=0;

    while (lines < 40){
        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        long long elapsed = (now.QuadPart - t0.QuadPart) * 1000LL / qpf.QuadPart;
        if (elapsed > TIME_CAP_MS) return FAIL_MS;
        if (pieces  > PIECE_CAP )  return FAIL_MS;

        int rot,left,score;
        if(!AutoPlanBest((const int (*)[W])B, cur, nxt, &rot, &left, &score, fast))
            return FAIL_MS;

        int cleared=0;
        if(!sim_drop_lock_clear_ex(B, cur, rot, left, &cleared))
            return FAIL_MS;

        lines += cleared; pieces++;

        // 최근 10피스 동안 한 줄도 못 지우면 실패
        if (pieces - piece_ckpt >= 10){
            if (lines - lines_ckpt == 0) return FAIL_MS;
            piece_ckpt = pieces; lines_ckpt = lines;
        }

        if (lines >= 40) break;

        cur = nxt;
        nxt = next_piece(&st, &bi, bag7);

        int holes=0, maxh=0; hole_and_maxH(B, &holes, &maxh);
        if (holes > HOLE_CAP || maxh > MAXH_CAP) return FAIL_MS;
    }

    LARGE_INTEGER t1; QueryPerformanceCounter(&t1);
    return (t1.QuadPart - t0.QuadPart) * 1000LL / qpf.QuadPart;
}

static long long FitnessAvgMs(int trials, int fast){
    long long sum=0;
    for(int k=0;k<trials;k++){
        unsigned seed = 0x9e3779b9u ^ (k*7919u);
        sum += SimRun40L_fast(seed, fast);
    }
    return sum / (trials?trials:1);
}

// ---------- Clamp & Perturb : 4-특징만 ----------
static void Clamp(Weights* w){
    #define CLAMP(x,lo,hi) do{ if((w)->x<(lo)) (w)->x=(lo); if((w)->x>(hi)) (w)->x=(hi);}while(0)
    CLAMP(W_lines1, 0, 20000); CLAMP(W_lines2, 0, 40000);
    CLAMP(W_lines3, 0, 60000); CLAMP(W_lines4, 0, 80000);
    CLAMP(W_agg_height, 0, 2000);
    CLAMP(W_holes,      0, 5000);
    CLAMP(W_bump,       0, 2000);
    CLAMP(W_wells,      0, 2000);
    CLAMP(W_row_trans,  0, 5000);
    CLAMP(W_col_trans,  0, 5000);
    CLAMP(W_hole_depth, 0, 2000);
    CLAMP(W_blockades,  0, 5000);
    #undef CLAMP
}
static Weights Perturb(const Weights* base, int span){
    Weights w = *base;
    #define JITTER(x,s)  (w.x += (rand()%(2*(s)+1))-(s))
    JITTER(W_lines1, span);
    // 비율 유지(선택): 1x,2x,3x,4x
    w.W_lines2 = 2 * w.W_lines1;
    w.W_lines3 = 3 * w.W_lines1;
    w.W_lines4 = 4 * w.W_lines1;

    JITTER(W_agg_height, span/2);
    JITTER(W_holes,      span);
    JITTER(W_bump,       span/2);
    JITTER(W_wells,      span/2);
    JITTER(W_row_trans,  span/2);
    JITTER(W_col_trans,  span/2);
    JITTER(W_hole_depth, span/3);
    JITTER(W_blockades,  span);
    #undef JITTER
    Clamp(&w);
    return w;
}

// ---------- 배치 기반 튜너 + 진행 콜백 ----------
void RunAutoTune40LEx(int trials, int iters, const char* out_path, TuneProgressFn cb){
    extern int nArr[H][W];
    // 전역 백업
    int nArr_bak[H][W]; memcpy(nArr_bak, nArr, sizeof(nArr_bak));
    int bt_bak=nBlockType, bt2_bak=nBlockType2, rot_bak=nRot, sp_bak=nSpawning;
    int next_bak[NEXT_PREVIEW_COUNT]; memcpy(next_bak, nNextQueue, sizeof(next_bak));
    int hold_bak=nHoldType, hold_used_bak=holdUsedThisTurn;
    int lines_bak=gLinesCleared, pcs_bak=gPiecesUsed, score_bak=nScore;
    STAGE st_bak=Stage; clock_t old_bak=Oldtime;

    if(!WeightsLoad(out_path)){ WeightsSetDefault40L(); WeightsSave(out_path, &gW); }

    Weights best=gW, cur=gW;
    long long bestMs = FitnessAvgMs(TRIALS_FULL, /*fast=*/0);
    int span=200, no_imp=0;

    for(int it=1; it<=iters; ++it){
        // 1) 후보 배치 생성(빠른 선발전)
        Weights cand[BATCH];
        long long fastMs[BATCH];
        for(int i=0;i<BATCH;i++){
            cand[i] = Perturb(&cur, span);
            gW = cand[i];
            fastMs[i] = FitnessAvgMs(TRIALS_FAST, /*fast=*/1);  // 1-ply, 1 trial
        }
        long long minFast = fastMs[0];
        for(int i=1;i<BATCH;i++) if (fastMs[i] < minFast) minFast = fastMs[i];

        // 진행 콜백(배치 스크리닝 직후)
        if (cb) cb(it, iters, bestMs, minFast, span, &best, &cur);
        else if ((it % 10) == 1) {
            printf("[FAST] it=%d/%d  best=%lld ms  minFast=%lld ms  span=%d\n",
                   it, iters, bestMs, minFast, span);
        }

        // 2) 상위 TOPK
        int idx[TOPK]; for(int k=0;k<TOPK;k++) idx[k]=-1;
        for(int i=0;i<BATCH;i++){
            for(int k=0;k<TOPK;k++){
                if(idx[k]<0 || fastMs[i] < fastMs[idx[k]]){
                    for(int t=TOPK-1;t>k;t--) idx[t]=idx[t-1];
                    idx[k]=i; break;
                }
            }
        }

        // 3) 정밀 평가(여러 trial + 2-ply)
        int improved=0;
        for(int k=0;k<TOPK;k++){
            if(idx[k]<0) continue;
            gW = cand[idx[k]];
            long long ms = FitnessAvgMs(trials, /*fast=*/0);
            if (ms < bestMs){
                bestMs = ms; best = gW; cur = gW; improved=1; no_imp=0;
                WeightsSave(out_path, &best);
            }
            // 진행 콜백(중간 스냅샷)
            if (cb) cb(it, iters, bestMs, minFast, span, &best, &cur);
        }

        if(improved){
            if(span>20) span -= 10;
        }else{
            no_imp++;
            if(no_imp % 25 == 0 && span < 400) span += 20;
            if(no_imp % 60 == 0) cur = Perturb(&best, 300);
        }
    }

    gW = best;
    WeightsSave(out_path, &best);

    // 전역 복원
    memcpy(nArr, nArr_bak, sizeof(nArr_bak));
    nBlockType=bt_bak; nBlockType2=bt2_bak; nRot=rot_bak; nSpawning=sp_bak;
    memcpy(nNextQueue, next_bak, sizeof(next_bak));
    nHoldType=hold_bak; holdUsedThisTurn=hold_used_bak;
    gLinesCleared=lines_bak; gPiecesUsed=pcs_bak; nScore=score_bak; Stage=st_bak; Oldtime=old_bak;
}

// 콜백 없는 버전
void RunAutoTune40L(int trials, int iters, const char* out_path){
    RunAutoTune40LEx(trials, iters, out_path, /*cb=*/NULL);
}

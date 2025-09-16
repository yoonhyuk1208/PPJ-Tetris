#include <limits.h>
#include <string.h>
#include "PanData.h"
#include "BlockData.h"   // BlockArr[type][rot][4][4]
#include "BlockMove.h"   // Rotate/LeftMove/RightMove/DownMove
#include "GameState.h"   // nRot, nSpawning, Stage 등
#include "AutoPlay.h"

#define USE_BLOG_EVAL 1  // 블로그식(4-특징) 평가 사용

// ========== 내부 유틸 ==========

// 현재 nArr에서 낙하 중 조각(값=1)의 가장 왼쪽 열
static int current_piece_leftmost_col(void){
    extern int nArr[H][W];
    int mn = 999;
    for(int i=PLAY_TOP;i<=PLAY_BOTTOM;i++)
        for(int j=PLAY_LEFT;j<=PLAY_RIGHT;j++)
            if(nArr[i][j]==1 && j<mn) mn=j;
    if (mn==999) mn=PLAY_LEFT;
    return mn;
}

// 블록 4x4 마스크의 경계 (min/max 행·열) 계산
// AutoPlay.c
static void mask_bounds(int type, int rot, int* minR, int* maxR, int* minC, int* maxC){
    *minR = 4;  *maxR = -1;
    *minC = 4;  *maxC = -1;

    for (int i = 0; i < 4; ++i){
        for (int j = 0; j < 4; ++j){
            if (BlockArr[type][rot][i][j]){
                if (i < *minR) *minR = i;
                if (i > *maxR) *maxR = i;
                if (j < *minC) *minC = j;
                if (j > *maxC) *maxC = j;
            }
        }
    }

    // 마스크가 비어있을 때 방어
    if (*maxR < *minR){
        *minR = *minC = 0;
        *maxR = *maxC = 0;
    }
}


static void copy_and_clean_from(int dst[H][W], const int src[H][W]){
    memcpy(dst, src, sizeof(int)*H*W);
    for(int i=PLAY_TOP;i<=PLAY_BOTTOM;i++)
        for(int j=PLAY_LEFT;j<=PLAY_RIGHT;j++)
            if (dst[i][j]==1) dst[i][j]=0;
}

// after 보드에 대해 4-특징(라인/총높이/홀/범피)만 계산해 점수
typedef struct {
    int agg_height;   // 열별 높이 합
    int holes;        // 구멍 수
    int bump;         // 인접 열 높이 차의 총합
} Feats;

static void compute_features(const int F[H][W], Feats* ft){
    memset(ft,0,sizeof(*ft));
    int h[PLAY_RIGHT+1]={0};

    // 높이/홀
    for(int j=PLAY_LEFT;j<=PLAY_RIGHT;j++){
        int seenTop=0;
        for(int i=PLAY_TOP;i<=PLAY_BOTTOM;i++){
            if(F[i][j]==2){
                if(!h[j]) h[j]=(PLAY_BOTTOM+1)-i; // 맨위 블록에서의 높이
                seenTop=1;
            }else if(seenTop && F[i][j]==0){
                ft->holes++;
            }
        }
        ft->agg_height += h[j];
    }
    // 범피
    for(int j=PLAY_LEFT;j<PLAY_RIGHT;j++)
        ft->bump += (h[j]>h[j+1] ? h[j]-h[j+1] : h[j+1]-h[j]);
}

// 블로그식 평가식(라인 보상 + 높이/홀/범피 페널티)
#include "Weights.h"
static int evaluate_field_blog(const int F[H][W], int lines_last){
    Feats f; compute_features(F, &f);

    int lineReward = 0;
    if      (lines_last==1) lineReward = gW.W_lines1;
    else if (lines_last==2) lineReward = gW.W_lines2;
    else if (lines_last==3) lineReward = gW.W_lines3;
    else if (lines_last==4) lineReward = gW.W_lines4;

    int score = 0;
    score += lineReward;
    score -= gW.W_agg_height * f.agg_height;
    score -= gW.W_holes      * f.holes;
    score -= gW.W_bump       * f.bump;
    return score;
}

// 조각 드랍+고정+라인클리어 시뮬 (성공=1, 실패=0)
int sim_drop_lock_clear_ex(int F[H][W], int type, int rot, int xLeft, int* lines_out){
    int minR,maxR,minC,maxC; mask_bounds(type, rot, &minR,&maxR,&minC,&maxC);
    int width = maxC - minC + 1;
    if (xLeft < PLAY_LEFT || xLeft > PLAY_RIGHT - width + 1) return 0;

    int y = PLAY_TOP - minR;

    // 초기 충돌 검사
    for(int i=minR;i<=maxR;i++)
    for(int j=minC;j<=maxC;j++) if (BlockArr[type][rot][i][j]) {
        int rr = y+i, cc = xLeft+(j-minC);
        if (rr<0 || rr>=H || cc<0 || cc>=W) return 0;
        if (F[rr][cc]==2) return 0;
    }

    // 낙하
    while(1){
        int blocked=0;
        for(int i=minR;i<=maxR;i++)
        for(int j=minC;j<=maxC;j++) if (BlockArr[type][rot][i][j]) {
            int rr=y+i+1, cc=xLeft+(j-minC);
            if (rr>=H || F[rr][cc]==2) { blocked=1; break; }
        }
        if(blocked) break;
        y++;
    }

    // 고정
    for(int i=minR;i<=maxR;i++)
    for(int j=minC;j<=maxC;j++) if (BlockArr[type][rot][i][j]) {
        int rr=y+i, cc=xLeft+(j-minC);
        F[rr][cc]=2;
    }

    // 라인 클리어
    int cleared=0;
    for(int i=PLAY_TOP;i<=PLAY_BOTTOM;i++){
        int full=1;
        for(int j=PLAY_LEFT;j<=PLAY_RIGHT;j++)
            if(F[i][j]!=2){ full=0; break; }
        if(full){
            cleared++;
            for(int r=i;r>PLAY_TOP;r--)
                for(int c=PLAY_LEFT;c<=PLAY_RIGHT;c++)
                    F[r][c]=F[r-1][c];
            for(int c=PLAY_LEFT;c<=PLAY_RIGHT;c++) F[PLAY_TOP][c]=0;
            i--;
        }
    }
    if(lines_out) *lines_out = cleared;
    return 1;
}

// ========== 플래너(탐색) ==========

int AutoPlanBest(const int board[H][W], int curType, int nextType,
                 int* outRot, int* outLeft, int* outScore, int fast)
{
    const int NEG_INF = -0x3f3f3f3f;

    int bestScore = NEG_INF;
    int bestRot   = 0;
    int bestLeft  = PLAY_LEFT;

    // 타이브레이커(보수적)
    int tie_holes = INT_MAX, tie_aggh = INT_MAX, tie_bump = INT_MAX;

    for (int rot=0; rot<4; ++rot){
        int minR,maxR,minC,maxC;
        mask_bounds(curType, rot, &minR,&maxR,&minC,&maxC);
        int width = maxC - minC + 1;
        int Lmin  = PLAY_LEFT;
        int Lmax  = PLAY_RIGHT - width + 1;

        for (int xLeft=Lmin; xLeft<=Lmax; ++xLeft){
            int tmp1[H][W]; copy_and_clean_from(tmp1, board);
            int lines1=0;
            if (!sim_drop_lock_clear_ex(tmp1, curType, rot, xLeft, &lines1)) continue;

            int s1 = evaluate_field_blog(tmp1, lines1);
            int total = s1;

            if (!fast){
                int bestNext = NEG_INF;
                for (int rot2=0; rot2<4; ++rot2){
                    int mnR2,mxR2,mnC2,mxC2; mask_bounds(nextType, rot2, &mnR2,&mxR2,&mnC2,&mxC2);
                    int width2 = mxC2 - mnC2 + 1;
                    int Lmin2  = PLAY_LEFT;
                    int Lmax2  = PLAY_RIGHT - width2 + 1;

                    for (int xLeft2=Lmin2; xLeft2<=Lmax2; ++xLeft2){
                        int tmp2[H][W]; memcpy(tmp2, tmp1, sizeof(tmp2));
                        int lines2=0;
                        if (!sim_drop_lock_clear_ex(tmp2, nextType, rot2, xLeft2, &lines2)) continue;
                        int s2 = evaluate_field_blog(tmp2, lines2);
                        if (s2 > bestNext) bestNext = s2;
                    }
                }
                total = s1 + bestNext;
            }

            // 타이브레이커: holes → agg_height → bumpiness
            Feats f; compute_features(tmp1, &f);
            int choose = 0;
            if (total > bestScore) choose = 1;
            else if (total == bestScore){
                if (f.holes < tie_holes) choose = 1;
                else if (f.holes == tie_holes){
                    if (f.agg_height < tie_aggh) choose = 1;
                    else if (f.agg_height == tie_aggh){
                        if (f.bump < tie_bump) choose = 1;
                    }
                }
            }

            if (choose){
                bestScore = total;
                bestRot   = rot;
                bestLeft  = xLeft;
                tie_holes = f.holes;
                tie_aggh  = f.agg_height;
                tie_bump  = f.bump;
            }
        }
    }

    if (bestScore == NEG_INF) return 0;

    if (outRot)   *outRot   = bestRot;
    if (outLeft)  *outLeft  = bestLeft;
    if (outScore) *outScore = bestScore;
    return 1;
}

// ========== 실행(조작) ==========

void AutoPlay(int curType, int nextType, int sprintMode){
    (void)sprintMode;

    extern int nArr[H][W];
    int bestRot, bestLeft, bestScore;
    if (!AutoPlanBest((const int (*)[W])nArr, curType, nextType,
                      &bestRot, &bestLeft, &bestScore, /*fast=*/0)) {
        // 놓을 곳이 없으면 그냥 드랍
        DownMove(nArr);
        nSpawning = 3; nRot = 1;
        return;
    }

    // 회전
    for (int k=0; k<bestRot; ++k){
        Rotate(nArr, curType, Block_pos, nRot);
        nRot = (nRot + 1) & 3;
    }

    // 좌/우
    int curLeft = current_piece_leftmost_col();
    int guard=0, GUARD_MAX=40;
    while (curLeft > bestLeft && guard++ < GUARD_MAX){ LeftMove(nArr);  int nl=current_piece_leftmost_col(); if(nl==curLeft) break; curLeft=nl; }
    while (curLeft < bestLeft && guard++ < GUARD_MAX){ RightMove(nArr); int nl=current_piece_leftmost_col(); if(nl==curLeft) break; curLeft=nl; }

    // 하드드랍
    DownMove(nArr);
    nSpawning = 3; nRot = 1;
}

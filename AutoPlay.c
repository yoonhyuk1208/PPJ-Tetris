#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <limits.h>

#include "AutoPlay.h"

#include "BlockData.h"
#include "BlockMove.h"
#include "PanData.h"
#include "Weights.h"

extern int nRot;
extern int nSpawning;

#define H 22
#define W 12
#define PLAY_TOP    1
#define PLAY_BOTTOM 20
#define PLAY_LEFT   1
#define PLAY_RIGHT  10

// =========================================================
// 1) 유틸: 현재 조각의 "가장 왼쪽 열" (필드 좌표) 구하기
// =========================================================
static int current_piece_leftmost_col(void){
    int m = 999;
    for(int k=0;k<4;k++){
        if (Block_pos[k].Pos.y < m) m = Block_pos[k].Pos.y;
    }
    return m;
}

// =========================================================
// 2) 유틸: 마스크의 유효 칸(min/max col,row) 계산
//    (4x4 중 실제 칸이 있는 최소/최대 i,j)
// =========================================================
static void mask_bounds(int type, int rot, int* minR, int* maxR, int* minC, int* maxC){
    int mnR=4, mxR=-1, mnC=4, mxC=-1;
    for(int i=0;i<4;i++){
        for(int j=0;j<4;j++){
            if (BlockArr[type][rot][i][j]){
                if (i<mnR) mnR=i;
                if (i>mxR) mxR=i;
                if (j<mnC) mnC=j;
                if (j>mxC) mxC=j;
            }
        }
    }
    if(minR) *minR= (mnR==4?0:mnR);
    if(maxR) *maxR= (mxR==-1?0:mxR);
    if(minC) *minC= (mnC==4?0:mnC);
    if(maxC) *maxC= (mxC==-1?0:mxC);
}

// =========================================================
// 3) 유틸: 보드 복사 + 낙하중(1) 제거 (평가용 필수)
// =========================================================
static void copy_and_clean(int dst[H][W], const int src[H][W]){
    memcpy(dst, src, sizeof(int)*H*W);
    for(int i=PLAY_TOP;i<=PLAY_BOTTOM;i++)
        for(int j=PLAY_LEFT;j<=PLAY_RIGHT;j++)
            if (dst[i][j]==1) dst[i][j]=0;
}

// =========================================================
// 4) 라인 클리어(시뮬레이션 보드용), 반환: 지운 줄 수
// =========================================================
static int clear_lines_on(int F[H][W]){
    int cleared=0;
    for(int i=PLAY_TOP;i<=PLAY_BOTTOM;i++){
        int full=1;
        for(int j=PLAY_LEFT;j<=PLAY_RIGHT;j++){
            if (F[i][j]!=2){ full=0; break; }
        }
        if(full){
            cleared++;
            // 한 줄 내리기
            for(int r=i;r>PLAY_TOP;r--)
                for(int c=PLAY_LEFT;c<=PLAY_RIGHT;c++)
                    F[r][c]=F[r-1][c];
            for(int c=PLAY_LEFT;c<=PLAY_RIGHT;c++) F[PLAY_TOP][c]=0;
            i--; // 당겨진 줄 재검사
        }
    }
    return cleared;
}

// =========================================================
// 5) 시뮬레이션: "왼쪽 가장자리" 기준 배치 후 하드드랍 + 고정 + 즉시 클리어
//    xLeft: 필드 기준 '블록이 실제로 차지하는 가장 왼쪽 열'
//    place 실패 시 0, 성공 시 1
// =========================================================
static int sim_drop_lock_clear(int F[H][W], int type, int rot, int xLeft){
    int minR,maxR,minC,maxC;
    mask_bounds(type, rot, &minR,&maxR,&minC,&maxC);

    int width = maxC - minC + 1;
    if (xLeft < PLAY_LEFT || xLeft > PLAY_RIGHT - width + 1) return 0;

    // 초기 y는 "마스크의 top이 1행"이 되도록
    int y = PLAY_TOP - minR;

    // 초기 충돌 검사
    for(int i=minR;i<=maxR;i++){
        for(int j=minC;j<=maxC;j++){
            if (!BlockArr[type][rot][i][j]) continue;
            int rr = y + i;
            int cc = xLeft + (j - minC);
            if (rr<0 || rr>=H || cc<0 || cc>=W) return 0;
            if (F[rr][cc]==2) return 0;
        }
    }

    // 아래로 내릴 수 있을 때까지 반복
    while(1){
        int blocked=0;
        for(int i=minR;i<=maxR;i++){
            for(int j=minC;j<=maxC;j++){
                if (!BlockArr[type][rot][i][j]) continue;
                int rr = y + i + 1;                 // 한 칸 아래
                int cc = xLeft + (j - minC);
                if (rr>=H) { blocked=1; break; }    // 보호벽이 있어야 정상인데 가드
                if (F[rr][cc]==2){ blocked=1; break; }
            }
            if(blocked) break;
        }
        if(blocked) break;
        y++;
    }

    // 최종 위치에 고정(2)
    for(int i=minR;i<=maxR;i++){
        for(int j=minC;j<=maxC;j++){
            if (!BlockArr[type][rot][i][j]) continue;
            int rr = y + i;
            int cc = xLeft + (j - minC);
            F[rr][cc]=2;
        }
    }

    // 즉시 라인 클리어
    clear_lines_on(F);
    return 1;
}

// === 기존 evaluate_field(...) 를 아래 코드로 교체 ===
typedef struct {
    int lines;
    int agg_height;
    int max_height;
    int holes;
    int hole_blockades;   // 구멍 위에 쌓인 블록 수의 누적(깊은 구멍 가중)
    int bump;
    int row_trans, col_trans;
    int well_sums;        // 웰 깊이 제곱 합 비슷한 효과(간단 가중 누적)
    int danger_cells;     // 플레이 최상단 3줄의 점유 칸 수
} Feats;

static void compute_features(const int F[H][W], Feats* ft){
    memset(ft, 0, sizeof(*ft));

    // A) 라인
    for(int i=PLAY_TOP;i<=PLAY_BOTTOM;i++){
        int full=1;
        for(int j=PLAY_LEFT;j<=PLAY_RIGHT;j++)
            if(F[i][j]!=2){ full=0; break; }
        if(full) ft->lines++;
    }

    // B) 열별 높이, 구멍/막힘, 최대 높이, 웰
    int h[PLAY_RIGHT+1]={0};
    for(int j=PLAY_LEFT;j<=PLAY_RIGHT;j++){
        int seen_block=0, filled_above=0;
        int col_height=0;

        // 높이: 첫 블록 발견 시 (PLAY_BOTTOM+1)-i
        for(int i=PLAY_TOP;i<=PLAY_BOTTOM;i++){
            if(F[i][j]==2){
                if(!col_height) col_height = (PLAY_BOTTOM + 1) - i;
                seen_block=1;
                filled_above++;
            } else if (seen_block && F[i][j]==0){
                ft->holes++;
                ft->hole_blockades += filled_above; // 위에 쌓인 블록 수만큼 가중
            }
        }
        h[j]=col_height;
        if(col_height > ft->max_height) ft->max_height = col_height;
        ft->agg_height += col_height;

        // 웰(간단): 좌/우가 점유(2)이고 자신은 빈칸이면 깊이 누적
        int depth=0, sum=0;
        for(int i=PLAY_TOP;i<=PLAY_BOTTOM;i++){
            int L = (j==PLAY_LEFT)?2:F[i][j-1];
            int R = (j==PLAY_RIGHT)?2:F[i][j+1];
            if (F[i][j]==0 && L==2 && R==2){ depth++; sum += depth; }
            else if (F[i][j]==2) depth=0;
        }
        ft->well_sums += sum;
    }

    // C) 울퉁불퉁
    for(int j=PLAY_LEFT;j<PLAY_RIGHT;j++)
        ft->bump += abs(h[j]-h[j+1]);

    // D) 전이(행/열)
    for(int i=PLAY_TOP;i<=PLAY_BOTTOM;i++){
        int prev=2; // 왼벽 가상 점유
        for(int j=PLAY_LEFT;j<=PLAY_RIGHT;j++){
            int v = (F[i][j]==2)?2:0;
            if(v!=prev) ft->row_trans++;
            prev=v;
        }
        if(prev!=2) ft->row_trans++; // 오른벽
    }
    for(int j=PLAY_LEFT;j<=PLAY_RIGHT;j++){
        int prev=2; // 천장 가상 점유
        for(int i=PLAY_TOP;i<=PLAY_BOTTOM;i++){
            int v = (F[i][j]==2)?2:0;
            if(v!=prev) ft->col_trans++;
            prev=v;
        }
        if(prev!=2) ft->col_trans++; // 바닥
    }

    // E) 위험구역: 최상단 3줄 점유
    for(int i=PLAY_TOP;i<PLAY_TOP+3;i++)
        for(int j=PLAY_LEFT;j<=PLAY_RIGHT;j++)
            if(F[i][j]==2) ft->danger_cells++;
}

static int evaluate_field(const int F[H][W]){
    Feats f; compute_features(F, &f);

    // gW 사용 (CSV 로드/기본값에서 주입)
    // 라인이 최우선이지만, 생존 위해 holes/height/bump/trans도 중요
    int score = 0;
    score +=  gW.W_line  * f.lines;
    score -=  gW.W_holes * f.holes;
    score -=  gW.W_height* f.agg_height;   // 필요시 max_height로 바꿔도 됨
    score -=  gW.W_bump  * f.bump;
    score +=  gW.W_well  * (-(f.well_sums)); // wells를 페널티로 쓰고 싶으면 음수화
    score -=  gW.W_trans * (f.row_trans + f.col_trans);
    return score;
}

// =========================================================
// 7) 두-플라이(현재+다음) 탐색 → 최적 (rot, xLeft) 선택
//    선택 후 실제 보드에서 회전→좌/우→하드드랍을 수행
// =========================================================
void AutoPlay(int curType, int nextType, int sprintMode){
    (void)sprintMode; // 지금은 미사용(튜닝 단계에서 반영)

    const int NEG_INF = -0x3f3f3f3f;

    int bestScore = NEG_INF;
    int bestRot   = 0;
    int bestLeft  = PLAY_LEFT;

    // 타이브레이커 기준(초기값 세팅)
    int tie_danger = INT_MAX;   // 적을수록 좋음
    int tie_maxh   = INT_MAX;   // 적을수록 좋음
    int tie_holes  = INT_MAX;   // 적을수록 좋음
    int tie_aggh   = INT_MAX;   // 적을수록 좋음
    int tie_bump   = INT_MAX;   // 적을수록 좋음
    int tie_trans  = INT_MAX;   // 적을수록 좋음 (row+col)
    int tie_wells  = INT_MAX;   // 적을수록 좋음

    // 현재 블록 모든 회전
    for (int rot=0; rot<4; rot++){
        int minR,maxR,minC,maxC;
        mask_bounds(curType, rot, &minR,&maxR,&minC,&maxC);
        int width = maxC - minC + 1;
        int Lmin  = PLAY_LEFT;
        int Lmax  = PLAY_RIGHT - width + 1;

        for (int xLeft=Lmin; xLeft<=Lmax; xLeft++){
            int tmp1[H][W]; copy_and_clean(tmp1, (const int(*)[W])nArr);
            if (!sim_drop_lock_clear(tmp1, curType, rot, xLeft)) continue;

            int s1 = evaluate_field(tmp1);

            // 다음 블록(두-플라이)
            int bestNext = NEG_INF;
            for (int rot2=0; rot2<4; rot2++){
                int mnR2,mxR2,mnC2,mxC2;
                mask_bounds(nextType, rot2, &mnR2,&mxR2,&mnC2,&mxC2);
                int width2 = mxC2 - mnC2 + 1;
                int Lmin2  = PLAY_LEFT;
                int Lmax2  = PLAY_RIGHT - width2 + 1;

                for (int xLeft2=Lmin2; xLeft2<=Lmax2; xLeft2++){
                    int tmp2[H][W]; memcpy(tmp2, tmp1, sizeof(tmp2));
                    if (!sim_drop_lock_clear(tmp2, nextType, rot2, xLeft2)) continue;
                    int s2 = evaluate_field(tmp2);
                    if (s2 > bestNext) bestNext = s2;
                }
            }

            int total = s1 + bestNext;

            // ========= 타이브레이커 계산을 위한 현재 피처 산출 =========
            Feats curFt; compute_features(tmp1, &curFt);
            int curTrans = curFt.row_trans + curFt.col_trans;

            // ========= 선택 로직: 점수 > 타이브레이커 순 =========
            int choose = 0;
            if (total > bestScore) {
                choose = 1;
            } else if (total == bestScore) {
                // 1) 위험구역 점유 ↓
                if (curFt.danger_cells < tie_danger) choose = 1;
                else if (curFt.danger_cells == tie_danger) {
                    // 2) 최대 높이 ↓
                    if (curFt.max_height < tie_maxh) choose = 1;
                    else if (curFt.max_height == tie_maxh) {
                        // 3) 홀 ↓
                        if (curFt.holes < tie_holes) choose = 1;
                        else if (curFt.holes == tie_holes) {
                            // 4) 총 높이 ↓
                            if (curFt.agg_height < tie_aggh) choose = 1;
                            else if (curFt.agg_height == tie_aggh) {
                                // 5) 울퉁불퉁 ↓
                                if (curFt.bump < tie_bump) choose = 1;
                                else if (curFt.bump == tie_bump) {
                                    // 6) 전이(row+col) ↓
                                    if (curTrans < tie_trans) choose = 1;
                                    else if (curTrans == tie_trans) {
                                        // 7) 웰 합 ↓ (깊은 우물 피하기)
                                        if (curFt.well_sums < tie_wells) choose = 1;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            if (choose) {
                bestScore = total;
                bestRot   = rot;
                bestLeft  = xLeft;

                // 타이브레이커 기준 업데이트
                tie_danger = curFt.danger_cells;
                tie_maxh   = curFt.max_height;
                tie_holes  = curFt.holes;
                tie_aggh   = curFt.agg_height;
                tie_bump   = curFt.bump;
                tie_trans  = curTrans;
                tie_wells  = curFt.well_sums;
            }
        }
    }

    // === 실제 보드에서 실행: 회전 → 좌/우 → 하드드랍 ===
    // 1) 회전
    for (int k=0; k<bestRot; k++){
        Rotate(nArr, curType, Block_pos, nRot);
        nRot = (nRot+1) & 3;
    }

    // 2) 목표 '왼쪽 가장자리'로 정렬
    int minR,maxR,minC,maxC;
    mask_bounds(curType, bestRot, &minR,&maxR,&minC,&maxC);

    int curLeft = current_piece_leftmost_col();
    while (curLeft > bestLeft){ LeftMove(nArr);  curLeft--; }
    while (curLeft < bestLeft){ RightMove(nArr); curLeft++; }

    // 3) 하드드랍
    DownMove(nArr);
    nSpawning = 3;
    nRot = 1;
}
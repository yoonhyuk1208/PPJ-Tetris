#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include "AutoPlay.h"
#include "BlockData.h"
#include "BlockMove.h"
#include "PanData.h"
#include "Weights.h"

#define LOOKAHEAD_BLEND 0.65f

// extern state
extern int nArr[H][W];
extern int nRot;
extern int nSpawning;
extern int nBlockType, nBlockType2;
extern BLOCK_POS Block_pos[4];

// ===== helpers =====
static int current_piece_leftmost_col(){
    int mn = 999;
    for(int i=PLAY_TOP;i<=PLAY_BOTTOM;i++)
        for(int j=PLAY_LEFT;j<=PLAY_RIGHT;j++)
            if(nArr[i][j]==1 && j<mn) mn=j;
    if (mn==999) mn=PLAY_LEFT;
    return mn;
}

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
    if (*maxR < *minR){
        *minR = *minC = 0;
        *maxR = *maxC = 0;
    }
}

static void copy_and_clean(int dst[H][W], const int src[H][W]){
    memcpy(dst, src, sizeof(int)*H*W);
    for(int i=PLAY_TOP;i<=PLAY_BOTTOM;i++)
        for(int j=PLAY_LEFT;j<=PLAY_RIGHT;j++)
            if (dst[i][j]==1) dst[i][j]=0;
}

// ===== feature/score =====
typedef struct {
    int agg_height;
    int holes;
    int bump;
    int wells;
    int hole_depth;
    int blockades;
    int row_trans;
    int col_trans;
    int max_height;
    int min_height;
    int surface_range;
    int filled_cells;
} Feats;

static void compute_features(const int F[H][W], Feats* ft){
    memset(ft,0,sizeof(*ft));
    int h[PLAY_RIGHT+1]={0};
    const int boardHeight = PLAY_BOTTOM - PLAY_TOP + 1;
    ft->min_height = boardHeight;

    for(int j=PLAY_LEFT;j<=PLAY_RIGHT;j++){
        int seenTop=0;
        int holeBelow=0;
        for(int i=PLAY_TOP;i<=PLAY_BOTTOM;i++){
            if(F[i][j]>=2){
                if(!h[j]) h[j]=(PLAY_BOTTOM+1)-i;
                ft->filled_cells++;
                if(holeBelow) ft->blockades++;
                seenTop=1;
            }else if(seenTop){
                ft->holes++;
                ft->hole_depth += (PLAY_BOTTOM - i + 1);
                holeBelow=1;
            }
        }
        ft->agg_height += h[j];
        if(h[j] > ft->max_height) ft->max_height = h[j];
        if(h[j] < ft->min_height) ft->min_height = h[j];
    }
    for(int j=PLAY_LEFT;j<PLAY_RIGHT;j++)
        ft->bump += abs(h[j]-h[j+1]);
    for(int j=PLAY_LEFT;j<=PLAY_RIGHT;j++){
        int leftH  = (j==PLAY_LEFT)?boardHeight:h[j-1];
        int rightH = (j==PLAY_RIGHT)?boardHeight:h[j+1];
        int neighMin = leftH < rightH ? leftH : rightH;
        int well = neighMin - h[j];
        if(well>0) ft->wells += well;
    }
    for(int i=PLAY_TOP;i<=PLAY_BOTTOM;i++){
        int prev=1;
        for(int j=PLAY_LEFT;j<=PLAY_RIGHT;j++){
            int filled = (F[i][j]>=2);
            if(filled != prev) ft->row_trans++;
            prev = filled;
        }
        if(prev==0) ft->row_trans++;
    }
    for(int j=PLAY_LEFT;j<=PLAY_RIGHT;j++){
        int prev=1;
        for(int i=PLAY_TOP;i<=PLAY_BOTTOM;i++){
            int filled = (F[i][j]>=2);
            if(filled != prev) ft->col_trans++;
            prev = filled;
        }
        if(prev==0) ft->col_trans++;
    }
    if(ft->min_height > ft->max_height) ft->min_height = ft->max_height;
    ft->surface_range = ft->max_height - ft->min_height;
    if(ft->surface_range < 0) ft->surface_range = 0;
}

static inline float clamp01(float x){
    if(x < 0.f) return 0.f;
    if(x > 1.f) return 1.f;
    return x;
}

static int evaluate_field_from_feats(const Feats* f, int lines_last){
    int lineReward = 0;
    if      (lines_last==1) lineReward = gW.W_lines1;
    else if (lines_last==2) lineReward = gW.W_lines2;
    else if (lines_last==3) lineReward = gW.W_lines3;
    else if (lines_last==4) lineReward = gW.W_lines4;

    int score = 0;
    score += lineReward;
    const float board_h = (float)(PLAY_BOTTOM - PLAY_TOP + 1);
    const float stack_ratio = clamp01((float)f->max_height / board_h);
    const float roughness = clamp01((float)f->surface_range / 8.0f);
    const float hole_depth_ratio = clamp01((float)f->hole_depth / 80.0f);
    const float blockade_ratio = clamp01((float)f->blockades / 10.0f);
    const float hole_ratio = clamp01((float)f->holes / 12.0f);

    float aggScale   = 0.55f + 0.75f * stack_ratio;
    float holeScale  = 1.0f  + 0.7f  * stack_ratio + 0.4f * hole_depth_ratio;
    float bumpScale  = 0.7f  + 0.5f  * roughness;
    float wellScale  = 0.35f + 0.65f * (1.0f - hole_ratio);
    float rowScale   = 0.5f  + 0.5f  * roughness;
    float colScale   = 0.5f  + 0.5f  * roughness;
    float depthScale = 0.8f  + 0.6f  * stack_ratio;
    float blockScale = 1.0f  + 0.6f  * stack_ratio + 0.4f * blockade_ratio;

    score -= (int)(gW.W_agg_height * aggScale)   * f->agg_height;
    score -= (int)(gW.W_holes      * holeScale)  * f->holes;
    score -= (int)(gW.W_bump       * bumpScale)  * f->bump;
    score -= (int)(gW.W_wells      * wellScale)  * f->wells;
    score -= (int)(gW.W_row_trans  * rowScale)   * f->row_trans;
    score -= (int)(gW.W_col_trans  * colScale)   * f->col_trans;
    score -= (int)(gW.W_hole_depth * depthScale) * f->hole_depth;
    score -= (int)(gW.W_blockades  * blockScale) * f->blockades;

    float holeDanger     = clamp01(((float)f->holes - 1.5f) / 6.0f);
    float depthDanger    = clamp01((float)f->hole_depth / 100.0f);
    float blockadeDanger = clamp01((float)f->blockades / 12.0f);
    float stackedDanger  = stack_ratio;
    float combinedDanger = clamp01(holeDanger*0.6f + depthDanger*0.25f + blockadeDanger*0.15f);
    float severe = combinedDanger * combinedDanger;
    score -= (int)((900.f + 2000.f * stackedDanger) * severe);
    if(f->holes > 3){
        int excess = f->holes - 3;
        score -= excess * excess * 45;
    }
    const int boardHeight = PLAY_BOTTOM - PLAY_TOP + 1;
    int safeRows = boardHeight - f->max_height;
    if(safeRows < 4){
        score -= (4 - safeRows) * 220;
    }
    if(f->holes==0 && f->surface_range <= 4 && f->max_height <= 8){
        score += 160;
    }
    if(f->surface_range > 6){
        score -= (f->surface_range - 6) * 35;
    }
    if(f->blockades > 0){
        score -= f->blockades * 8;
    }
    return score;
}

int evaluate_field(const int F[H][W], int lines_last){
    Feats f; compute_features(F, &f);
    return evaluate_field_from_feats(&f, lines_last);
}

int sim_drop_lock_clear_ex(int F[H][W], int type, int rot, int xLeft, int* lines_out){
    int minR,maxR,minC,maxC;
    mask_bounds(type, rot, &minR,&maxR,&minC,&maxC);
    int width = maxC - minC + 1;
    if (xLeft < PLAY_LEFT || xLeft > PLAY_RIGHT - width + 1) return 0;

    int y = PLAY_TOP - minR;

    // initial collision test
    for(int i=minR;i<=maxR;i++){
        for(int j=minC;j<=maxC;j++){
            if (BlockArr[type][rot][i][j]) {
                int rr = y+i, cc = xLeft+(j-minC);
                if (rr<0 || rr>=H || cc<0 || cc>=W) return 0;
                if (F[rr][cc]>=2) return 0;
            }
        }
    }

    // drop
    while(1){
        int blocked=0;
        for(int i=minR;i<=maxR && !blocked;i++){
            for(int j=minC;j<=maxC;j++) if (BlockArr[type][rot][i][j]) {
                int rr=y+i+1, cc=xLeft+(j-minC);
                if (rr>=H || F[rr][cc]>=2) { blocked=1; break; }
            }
        }
        if(blocked) break;
        y++;
    }

    // lock
    for(int i=minR;i<=maxR;i++){
        for(int j=minC;j<=maxC;j++) if (BlockArr[type][rot][i][j]) {
            int rr=y+i, cc=xLeft+(j-minC);
            F[rr][cc]=2+type;
        }
    }

    // clear lines
    int cleared=0;
    for(int i=PLAY_TOP;i<=PLAY_BOTTOM;i++){
        int full=1;
        for(int j=PLAY_LEFT;j<=PLAY_RIGHT;j++)
            if(F[i][j]<2){ full=0; break; }
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

static int evaluate_field_mixed(const int F[H][W], int lines_last){
    Feats f; compute_features(F, &f);
    return evaluate_field_from_feats(&f, lines_last);
}

static int evaluate_with_next(const int board[H][W], int nextType){
    if(nextType < 0) return evaluate_field_mixed(board, 0);
    int best = INT_MIN;
    for(int r=0;r<4;r++){
        int minR,maxR,minC,maxC;
        mask_bounds(nextType,r,&minR,&maxR,&minC,&maxC);
        int width = maxC - minC + 1;
        for(int x=PLAY_LEFT;x<=PLAY_RIGHT-width+1;x++){
            int tmp[H][W];
            memcpy(tmp, board, sizeof(int)*H*W);
            int cleared=0;
            if(!sim_drop_lock_clear_ex(tmp,nextType,r,x,&cleared)) continue;
            int sc = evaluate_field_mixed(tmp, cleared);
            if(sc>best) best=sc;
        }
    }
    if(best==INT_MIN) best = evaluate_field_mixed(board, 0);
    return best;
}

// ===== planner =====
int AutoPlanBest(const int F[H][W], int cur, int nxt,
    int* rot, int* left, int* score, int fast){
    int bestScore=INT_MIN;
    int bestRot=0, bestLeft=PLAY_LEFT;

    for(int r=0;r<4;r++){
        int minR,maxR,minC,maxC;
        mask_bounds(cur,r,&minR,&maxR,&minC,&maxC);
        int width=maxC-minC+1;
        for(int x=PLAY_LEFT;x<=PLAY_RIGHT-width+1;x++){
            int tmp[H][W];
            copy_and_clean(tmp,F);
            int cleared=0;
            if(!sim_drop_lock_clear_ex(tmp,cur,r,x,&cleared)) continue;
            int sc = evaluate_field_mixed(tmp,cleared);
            if(!fast){
                int look = evaluate_with_next(tmp, nxt);
                sc = (int)((1.0f-LOOKAHEAD_BLEND)*sc + LOOKAHEAD_BLEND*look);
            }
            if(sc>bestScore){
                bestScore=sc;
                bestRot=r; bestLeft=x;
            }
        }
    }

    *rot=bestRot; *left=bestLeft; *score=bestScore;
    return (bestScore==INT_MIN)?0:1;
}

// ===== execute =====
void AutoPlay(int curType,int nextType,int sprintMode){
    (void)sprintMode;
    int rot,left,score;
    if(!AutoPlanBest((const int(*)[W])nArr,curType,nextType,&rot,&left,&score,0))
        return;

    for(int k=0;k<rot;k++){
        int next = (nRot + 1) & 3;
        if (Rotate(nArr, curType, next)) nRot = next;
    }
    int curLeft=current_piece_leftmost_col();
    while(curLeft>left){ LeftMove(nArr); curLeft--; }
    while(curLeft<left){ RightMove(nArr); curLeft++; }
    HardDrop(nArr, curType);
    nSpawning=3;
    nRot=0;
}

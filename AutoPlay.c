#include <stdio.h>
#include <string.h>
#include <limits.h>
#include "AutoPlay.h"
#include "BlockData.h"
#include "BlockMove.h"
#include "PanData.h"
#include "Weights.h"
#include "NNEval.h"

// 외부 전역
extern int nArr[H][W];
extern int nRot;
extern int nSpawning;
extern int nBlockType, nBlockType2;
extern BLOCK_POS Block_pos[4];

// ===== 유틸 =====

// 현재 조각의 가장 왼쪽 열 찾기
static int current_piece_leftmost_col(){
    extern int nArr[H][W];
    int mn = 999;
    for(int i=PLAY_TOP;i<=PLAY_BOTTOM;i++)
        for(int j=PLAY_LEFT;j<=PLAY_RIGHT;j++)
            if(nArr[i][j]==1 && j<mn) mn=j;
    if (mn==999) mn=PLAY_LEFT;
    return mn;
}

// 마스크 범위
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
// 보드 복사 + 낙하 블록 제거
static void copy_and_clean(int dst[H][W], const int src[H][W]){
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
// ===== 휴리스틱 =====

// 단순 휴리스틱 평가
int evaluate_field(const int F[H][W], int lines_last){
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

// NN 혼합 평가
static int evaluate_field_mixed(const int F[H][W], int lines_last){
    int base = evaluate_field(F, lines_last);
    if(gNN.ready){
        float x[FEAT_DIM]; NN_ExtractFeatures(F,x);
        float y = NN_Predict(&gNN,x);
        float alpha = 0.3f;
        float combined = (1.0f-alpha)*base + alpha*y;
        return (int)combined;
    }
    return base;
}

// ===== 오토플랜 =====
int AutoPlanBest(const int F[H][W], int cur, int nxt,
    int* rot, int* left, int* score, int fast){
    int bestScore=INT_MIN;
    int bestRot=0, bestLeft=PLAY_LEFT;

    for(int r=0;r<4;r++){
        int minR,maxR,minC,maxC;
        mask_bounds(cur,r,&minR,&maxR,&minC,&maxC);
        int width=maxC-minC+1;
        for(int x=PLAY_LEFT;x<=PLAY_RIGHT-width+1;x++){
            int tmp[H][W]; copy_and_clean(tmp,F);
            int cleared=0;
            if(!sim_drop_lock_clear_ex(tmp,cur,r,x,&cleared)) continue;
            int sc = evaluate_field_mixed(tmp,cleared);
            if(sc>bestScore){
                bestScore=sc;
                bestRot=r; bestLeft=x;
            }
        }
    }

    *rot=bestRot; *left=bestLeft; *score=bestScore;
    return (bestScore==INT_MIN)?0:1;
}

void DumpSample(const int F[H][W], int score) {
    FILE* fp = fopen("train.bin","ab");
    if(!fp) return;
    float x[FEAT_DIM];
    NN_ExtractFeatures(F, x);
    fwrite(x, sizeof(float), FEAT_DIM, fp);
    float y = (float)score;
    fwrite(&y, sizeof(float), 1, fp);
    fclose(fp);
}

// ===== 오토플레이 실행 =====
void AutoPlay(int curType,int nextType,int sprintMode){
    (void)sprintMode;
    int rot,left,score;
    if(!AutoPlanBest((const int(*)[W])nArr,curType,nextType,&rot,&left,&score,0))
        return;

    int tmp[H][W]; copy_and_clean(tmp,(const int(*)[W])nArr);
    int cleared=0;
    if(sim_drop_lock_clear_ex(tmp,curType,rot,left,&cleared)){
        DumpSample(tmp, score);   // 여기서 실제 플레이 결과를 학습 데이터로 저장
    }
    
    // 회전
    for(int k=0;k<rot;k++){
        Rotate(nArr,curType,Block_pos,nRot);
        nRot=(nRot+1)&3;
    }
    // 이동
    int curLeft=current_piece_leftmost_col();
    while(curLeft>left){ LeftMove(nArr); curLeft--; }
    while(curLeft<left){ RightMove(nArr); curLeft++; }
    // 하드드랍
    DownMove(nArr);
    nSpawning=3;
    nRot=1;
}

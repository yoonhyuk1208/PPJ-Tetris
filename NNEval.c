#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "NNEval.h"

// 전역 NN
NN gNN = {0, FEAT_DIM, HIDDEN_DIM, 1, NULL, NULL, NULL, NULL};

// 초기화
static void NN_Init(NN* nn){
    nn->in_dim  = FEAT_DIM;
    nn->h_dim   = HIDDEN_DIM;
    nn->out_dim = 1;

    nn->W1 = (float*)malloc(sizeof(float)*nn->in_dim*nn->h_dim);
    nn->b1 = (float*)calloc(nn->h_dim,sizeof(float));
    nn->W2 = (float*)malloc(sizeof(float)*nn->h_dim*nn->out_dim);
    nn->b2 = (float*)calloc(nn->out_dim,sizeof(float));

    // Xavier 초기화
    for(int i=0;i<nn->in_dim*nn->h_dim;i++) nn->W1[i]=((float)rand()/RAND_MAX-0.5f)/nn->in_dim;
    for(int i=0;i<nn->h_dim*nn->out_dim;i++) nn->W2[i]=((float)rand()/RAND_MAX-0.5f)/nn->h_dim;

    nn->ready=1;
}

// 특징 추출 (구멍 + 최대 높이 + 패딩)
void NN_ExtractFeatures(const int F[H][W], float* x){
    int idx=0;

    // 구멍 수
    int holes=0;
    for(int j=PLAY_LEFT;j<=PLAY_RIGHT;j++){
        int seen=0;
        for(int i=PLAY_TOP;i<=PLAY_BOTTOM;i++){
            if(F[i][j]==2) seen=1;
            else if(seen && F[i][j]==0) holes++;
        }
    }
    x[idx++] = (float)holes;

    // 최대 높이
    int maxh=0;
    for(int j=PLAY_LEFT;j<=PLAY_RIGHT;j++){
        for(int i=PLAY_TOP;i<=PLAY_BOTTOM;i++){
            if(F[i][j]==2){
                int h=(PLAY_BOTTOM+1)-i;
                if(h>maxh) maxh=h;
                break;
            }
        }
    }
    x[idx++] = (float)maxh;

    // 추가 피처: 블록 채워진 칸 수
    int filled=0;
    for(int i=PLAY_TOP;i<=PLAY_BOTTOM;i++)
        for(int j=PLAY_LEFT;j<=PLAY_RIGHT;j++)
            if(F[i][j]==2) filled++;
    x[idx++] = (float)filled;

    // 나머지는 0으로 패딩
    while(idx<FEAT_DIM) x[idx++]=0.0f;
}

// ReLU
static inline float relu(float x){ return x>0?x:0; }

// 예측
float NN_Predict(const NN* nn, const float* x){
    if(!nn || !nn->ready) return 0.0f;

    float h[HIDDEN_DIM];
    for(int i=0;i<nn->h_dim;i++){
        float sum=nn->b1[i];
        for(int j=0;j<nn->in_dim;j++) sum+=x[j]*nn->W1[i*nn->in_dim+j];
        h[i]=relu(sum);
    }

    float y=nn->b2[0];
    for(int i=0;i<nn->h_dim;i++) y+=h[i]*nn->W2[i];
    return y;
}

// 더미 저장/로드 (이진 저장 구현 가능)
int NN_Save(const char* path){
    FILE* fp = fopen(path, "wb");
    if(!fp) {
        perror("[NN] Save fopen");
        return 0;
    }
    // 차원 저장
    fwrite(&gNN.in_dim, sizeof(int), 1, fp);
    fwrite(&gNN.h_dim, sizeof(int), 1, fp);

    // W1, b1, W2, b2 순서대로 저장
    fwrite(gNN.W1, sizeof(float), gNN.in_dim * gNN.h_dim, fp);
    fwrite(gNN.b1, sizeof(float), gNN.h_dim, fp);
    fwrite(gNN.W2, sizeof(float), gNN.h_dim, fp);
    fwrite(gNN.b2, sizeof(float), 1, fp);

    fclose(fp);
    printf("[NN] Saved to %s\n", path);
    return 1;
}

// === NN 로드 ===
int NN_Load(const char* path){
    FILE* fp = fopen(path, "rb");
    if(!fp) {
        perror("[NN] Load fopen");
        return 0;
    }
    int in_dim, h_dim;
    fread(&in_dim, sizeof(int), 1, fp);
    fread(&h_dim, sizeof(int), 1, fp);

    // 초기화
    NN_Init(&gNN);
    gNN.in_dim = in_dim;
    gNN.h_dim  = h_dim;

    // 메모리 할당
    gNN.W1 = (float*)malloc(sizeof(float) * in_dim * h_dim);
    gNN.b1 = (float*)malloc(sizeof(float) * h_dim);
    gNN.W2 = (float*)malloc(sizeof(float) * h_dim);
    gNN.b2 = (float*)malloc(sizeof(float));

    if(!gNN.W1 || !gNN.b1 || !gNN.W2 || !gNN.b2){
        fclose(fp);
        fprintf(stderr, "[NN] Load failed: malloc error\n");
        return 0;
    }

    // 파라미터 로드
    fread(gNN.W1, sizeof(float), in_dim * h_dim, fp);
    fread(gNN.b1, sizeof(float), h_dim, fp);
    fread(gNN.W2, sizeof(float), h_dim, fp);
    fread(gNN.b2, sizeof(float), 1, fp);

    gNN.ready = 1;
    fclose(fp);
    printf("[NN] Loaded from %s (in=%d, hidden=%d)\n", path, in_dim, h_dim);
    return 1;
}

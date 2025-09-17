#ifndef NNEVAL_H
#define NNEVAL_H

#include "PanData.h"

#define FEAT_DIM 16   // 특징 벡터 크기 (필요시 늘려도 됨)
#define HIDDEN_DIM 32 // 은닉층 크기 (가볍게)

typedef struct {
    int ready;          // 학습된 NN이 준비되었는가?
    int in_dim;         // 입력 차원
    int h_dim;          // 은닉 차원
    int out_dim;        // 출력 차원
    float *W1, *b1;     // 1층 가중치
    float *W2, *b2;     // 2층 가중치
} NN;

extern NN gNN;

// 특징 추출
void NN_ExtractFeatures(const int F[H][W], float* x);

// 예측
float NN_Predict(const NN* nn, const float* x);

// 로드/세이브
int  NN_Load(const char* path);
int  NN_Save(const char* path);

#endif

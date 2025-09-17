#pragma once
#include "NNEval.h"

// 진행 콜백: (에폭, 총에폭, 미니배치, 총미니배치, 학습손실, 검증손실)
typedef void (*NNTrainProgress)(
    int epoch, int epochs, int mb, int total_mb,
    float train_loss, float val_loss
);

// C-학습: train.bin에서 (x,y) 읽어 1-은닉층 MLP를 Adam으로 학습 → nn_weights.bin 저장
//  - in_path  : "data/train.bin" (각 샘플은 FEAT_DIM floats + 1 float target)
//  - out_path : "nn_weights.bin"
//  - hidden   : 은닉 크기(예: 128)
//  - epochs   : 에폭(예: 20~100)
//  - lr       : 학습률(예: 1e-3)
//  - batch    : 미니배치(예: 512)
//  - val_split: 검증 비율(0.0~0.5 권장; 예: 0.1)
//  - cb       : 진행 콜백(없으면 NULL)
int NN_TrainFromBin(const char* in_path, const char* out_path,
                    int hidden, int epochs, float lr, int batch,
                    float val_split, NNTrainProgress cb);

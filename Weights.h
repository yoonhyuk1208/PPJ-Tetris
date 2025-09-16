#pragma once

typedef struct {
    int W_line, W_holes, W_height, W_bump, W_well, W_trans;
} Weights;

extern Weights gW;  // 전역 가중치 (AutoPlay가 참조)

void WeightsSetDefault40L(void);                      // 기본값(40L용 출발점)
int  WeightsLoad(const char* path);                   // CSV에서 로드 (성공=1)
int  WeightsSave(const char* path, const Weights* w); // CSV로 저장
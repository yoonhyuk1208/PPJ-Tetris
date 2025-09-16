#pragma once

typedef struct {
    // 라인 보상(이번 수에서 지운 줄 수에 따라)
    int W_lines1, W_lines2, W_lines3, W_lines4;

    // 페널티(보드 상태 기반)
    int W_agg_height;   // 전체 높이 합
    int W_holes;        // 구멍 수
    int W_bump;         // 인접 열 높이 차의 합
} Weights;

extern Weights gW;

// 기본값 로드(블로그식 4-특징): 나머지 특징은 사용하지 않음
void WeightsSetDefault40L(void);

// 키=값 파일에서 로드/저장 (존재하지 않으면 Load는 0 반환)
int  WeightsLoad(const char* path);
int  WeightsSave(const char* path, const Weights* w);

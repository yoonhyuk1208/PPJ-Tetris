#pragma once
#include "Weights.h"

// 진행 상황 콜백: (현재 반복, 총 반복, 현재 best(ms), 이번 배치의 최소 fast(ms),
//                 현재 탐색 span, best 가중치, 현재 기준 가중치)
typedef void (*TuneProgressFn)(
    int iter, int iters,
    long long bestMs, long long minFastMs,
    int span,
    const Weights* best, const Weights* cur
);

// 기존 API (그대로 사용 가능)
void RunAutoTune40L(int trials, int iters, const char* out_path);

// 진행 콜백을 받는 확장 API
void RunAutoTune40LEx(int trials, int iters, const char* out_path, TuneProgressFn cb);

#include <stdio.h>
#include "Weights.h"

Weights gW;

void WeightsSetDefault40L(void){
    // 40L 속도형 (안정성도 고려된 준수치)
    gW.W_line   = 5000;
    gW.W_holes  = 120;
    gW.W_height = 10;
    gW.W_bump   = 6;
    gW.W_well   = 2;   // 너무 높이면 우물 파는 경향
    gW.W_trans  = 4;
}

int WeightsLoad(const char* path){
    FILE* fp = fopen(path, "r");
    if(!fp) return 0;
    int a,b,c,d,e,f;
    if (fscanf(fp, "%d,%d,%d,%d,%d,%d", &a,&b,&c,&d,&e,&f) != 6){
        fclose(fp); return 0;
    }
    fclose(fp);
    gW.W_line=a; gW.W_holes=b; gW.W_height=c; gW.W_bump=d; gW.W_well=e; gW.W_trans=f;
    return 1;
}

int WeightsSave(const char* path, const Weights* w){
    FILE* fp = fopen(path, "w");
    if(!fp) return 0;
    fprintf(fp, "%d,%d,%d,%d,%d,%d\n",
        w->W_line, w->W_holes, w->W_height, w->W_bump, w->W_well, w->W_trans);
    fclose(fp);
    return 1;
}

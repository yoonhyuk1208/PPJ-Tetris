#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "Weights.h"

Weights gW;

void WeightsSetDefault40L(void){
    // 블로그/델라셰리 계열 4-특징 시드(정수 스케일)
    gW.W_lines1 = 760;   gW.W_lines2 = 1520;  gW.W_lines3 = 2280;  gW.W_lines4 = 3040;
    gW.W_agg_height = 510;
    gW.W_holes      = 360;
    gW.W_bump       = 180;
    gW.W_wells      = 40;
    gW.W_row_trans  = 32;
    gW.W_col_trans  = 18;
    gW.W_hole_depth = 10;
    gW.W_blockades  = 60;
}

static int parse_line(const char* s, char* key, int* val){
    while (*s==' '||*s=='\t') s++;
    if (*s=='#' || *s=='\0' || *s=='\r' || *s=='\n') return 0;
    const char* eq = strchr(s, '=');
    if (!eq) return 0;
    size_t klen = (size_t)(eq - s);
    while (klen && (s[klen-1]==' '||s[klen-1]=='\t')) klen--;
    strncpy(key, s, klen); key[klen]='\0';
    *val = atoi(eq+1);
    return 1;
}

static void set_field(Weights* w, const char* k, int v){
    #define SET(name) if(!strcmp(k,#name)) { w->name = v; return; }
    SET(W_lines1) SET(W_lines2) SET(W_lines3) SET(W_lines4)
    SET(W_agg_height) SET(W_holes) SET(W_bump)
    SET(W_wells) SET(W_row_trans) SET(W_col_trans)
    SET(W_hole_depth) SET(W_blockades)
    #undef SET
}

int WeightsLoad(const char* path){
    FILE* fp = fopen(path, "r");
    if(!fp) return 0;
    WeightsSetDefault40L(); // 기본값 후 덮어쓰기
    char buf[256], key[128]; int val;
    while (fgets(buf, sizeof(buf), fp)){
        if (parse_line(buf, key, &val)) set_field(&gW, key, val);
    }
    fclose(fp);
    return 1;
}

int WeightsSave(const char* path, const Weights* w){
    FILE* fp = fopen(path, "w");
    if(!fp) return 0;
    fprintf(fp,
        "W_lines1=%d\nW_lines2=%d\nW_lines3=%d\nW_lines4=%d\n"
        "W_agg_height=%d\nW_holes=%d\nW_bump=%d\n"
        "W_wells=%d\nW_row_trans=%d\nW_col_trans=%d\n"
        "W_hole_depth=%d\nW_blockades=%d\n",
        w->W_lines1, w->W_lines2, w->W_lines3, w->W_lines4,
        w->W_agg_height, w->W_holes, w->W_bump,
        w->W_wells, w->W_row_trans, w->W_col_trans,
        w->W_hole_depth, w->W_blockades
    );
    fclose(fp);
    return 1;
}

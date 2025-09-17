#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "NNTrain.h"
#include "NNEval.h"

#undef H
#undef W

// ---------- 유틸 ----------
static float frand_uni(void){ return (float)rand()/(float)RAND_MAX - 0.5f; }
static void* xmalloc(size_t n){ void* p=malloc(n); if(!p){ fprintf(stderr,"OOM\n"); exit(1);} return p; }

// ---------- 데이터 로드 (train.bin: [FEAT_DIM floats][1 float] 반복) ----------
typedef struct {
    int n;          // 샘플 수
    float* X;       // [n * FEAT_DIM]
    float* y;       // [n]
} DS;

static int load_bin(const char* path, DS* ds){
    memset(ds,0,sizeof(*ds));
    FILE* fp=fopen(path,"rb"); if(!fp) return 0;
    fseek(fp,0,SEEK_END); long sz=ftell(fp); fseek(fp,0,SEEK_SET);
    long rec = (long)(FEAT_DIM+1)*sizeof(float);
    if (sz <= 0 || sz % rec != 0){ fclose(fp); return 0; }
    ds->n = (int)(sz / rec);
    ds->X = (float*)xmalloc(sizeof(float)*ds->n*FEAT_DIM);
    ds->y = (float*)xmalloc(sizeof(float)*ds->n);
    for(int i=0;i<ds->n;i++){
        if (fread(ds->X + i*FEAT_DIM, sizeof(float), FEAT_DIM, fp) != (size_t)FEAT_DIM){ fclose(fp); return 0; }
        if (fread(ds->y + i,           sizeof(float), 1,        fp) != 1){ fclose(fp); return 0; }
    }
    fclose(fp);
    return 1;
}
static void free_ds(DS* ds){ free(ds->X); free(ds->y); memset(ds,0,sizeof(*ds)); }

// ---------- MLP 파라미터/옵티마 ----------
typedef struct {
    int D, H;
    float *W1, *b1, *W2, b2;
    float *mW1,*mb1,*mW2;
    float *vW1,*vb1,*vW2;
    float mb2,vb2;
} MLP;

static void mlp_init(MLP* m, int D, int H){
    memset(m,0,sizeof(*m));
    m->D=D; m->H=H;
    m->W1=(float*)xmalloc(sizeof(float)*H*D);
    m->b1=(float*)xmalloc(sizeof(float)*H);
    m->W2=(float*)xmalloc(sizeof(float)*H);

    m->mW1=(float*)calloc(H*D,sizeof(float));
    m->mb1=(float*)calloc(H,  sizeof(float));
    m->mW2=(float*)calloc(H,  sizeof(float));

    m->vW1=(float*)calloc(H*D,sizeof(float));
    m->vb1=(float*)calloc(H,  sizeof(float));
    m->vW2=(float*)calloc(H,  sizeof(float));

    m->b2=0.f; m->mb2=0.f; m->vb2=0.f;

    // Xavier 초기화
    float s = sqrtf(2.0f/(D+H));
    for(int i=0;i<H*D;i++) m->W1[i] = frand_uni()*s;
    for(int i=0;i<H;i++)   m->b1[i] = 0.f, m->W2[i] = frand_uni()*s;
}
static void mlp_free(MLP* m){
    if(!m) return;
    free(m->W1); free(m->b1); free(m->W2);
    free(m->mW1); free(m->mb1); free(m->mW2);
    free(m->vW1); free(m->vb1); free(m->vW2);
    memset(m,0,sizeof(*m));
}

// ---------- 순전파/역전파(MSE, ReLU) ----------
typedef struct { float* z1; float* a1; } Cache;

static float forward_one(const MLP* m, const float* x, Cache* c){
    int D=m->D, H=m->H;
    float y=m->b2;
    for(int i=0;i<H;i++){
        float z=m->b1[i];
        const float* w1=m->W1 + i*D;
        for(int d=0;d<D;d++) z += w1[d]*x[d];
        c->z1[i]=z;
        float a = (z>0.f)?z:0.f;
        c->a1[i]=a;
        y += m->W2[i]*a;
    }
    return y;
}

static float train_minibatch(MLP* m, const float* X, const float* y,
                             int* idx, int n, int start, int bs,
                             float lr, float beta1, float beta2, float eps,
                             int t)
{
    int D=m->D, H=m->H;
    // 누적 그라디언트
    float *gW1=(float*)calloc(H*D,sizeof(float));
    float *gb1=(float*)calloc(H,  sizeof(float));
    float *gW2=(float*)calloc(H,  sizeof(float));
    float  gb2=0.f;

    float loss=0.f;

    float* z1=(float*)xmalloc(sizeof(float)*H);
    float* a1=(float*)xmalloc(sizeof(float)*H);
    Cache c={z1,a1};

    for(int k=0;k<bs;k++){
        int i = idx[start+k];
        const float* x = X + i*D;
        float pred = forward_one(m, x, &c);
        float err  = pred - y[i];
        loss += 0.5f * err * err;

        // dL/dW2 = err * a1, dL/db2 = err
        for(int h=0; h<H; h++) gW2[h] += err * c.a1[h];
        gb2 += err;

        // dL/dW1 = (err * W2 * relu') outer x
        for(int h=0; h<H; h++){
            float dz = (c.z1[h]>0.f) ? err * m->W2[h] : 0.f;
            gb1[h] += dz;
            float* grow = gW1 + h*D;
            for(int d=0; d<D; d++) grow[d] += dz * x[d];
        }
    }

    // Adam 업데이트
    float b1=beta1, b2=beta2;
    float ib1t = 1.f / (1.f - powf(b1,(float)t));
    float ib2t = 1.f / (1.f - powf(b2,(float)t));

    // 평균 그라디언트
    float inv = 1.f / (float)bs;
    for(int i=0;i<H*D;i++) gW1[i]*=inv;
    for(int i=0;i<H;i++)   gb1[i]*=inv, gW2[i]*=inv;
    gb2 *= inv;

    // W1
    for(int i=0;i<H*D;i++){
        m->mW1[i] = b1*m->mW1[i] + (1-b1)*gW1[i];
        m->vW1[i] = b2*m->vW1[i] + (1-b2)*gW1[i]*gW1[i];
        float mhat = m->mW1[i]*ib1t, vhat = m->vW1[i]*ib2t;
        m->W1[i]  -= lr * mhat / (sqrtf(vhat)+eps);
    }
    // b1
    for(int i=0;i<H;i++){
        m->mb1[i] = b1*m->mb1[i] + (1-b1)*gb1[i];
        m->vb1[i] = b2*m->vb1[i] + (1-b2)*gb1[i]*gb1[i];
        float mhat = m->mb1[i]*ib1t, vhat = m->vb1[i]*ib2t;
        m->b1[i]  -= lr * mhat / (sqrtf(vhat)+eps);
    }
    // W2
    for(int i=0;i<H;i++){
        m->mW2[i] = b1*m->mW2[i] + (1-b1)*gW2[i];
        m->vW2[i] = b2*m->vW2[i] + (1-b2)*gW2[i]*gW2[i];
        float mhat = m->mW2[i]*ib1t, vhat = m->vW2[i]*ib2t;
        m->W2[i]  -= lr * mhat / (sqrtf(vhat)+eps);
    }
    // b2
    {
        static float mb2=0.f, vb2=0.f; // 로컬 정적: 샘플 구현 간단화
        mb2 = b1*mb2 + (1-b1)*gb2;
        vb2 = b2*vb2 + (1-b2)*gb2*gb2;
        float mhat = mb2*ib1t, vhat = vb2*ib2t;
        m->b2 -= lr * mhat / (sqrtf(vhat)+eps);
    }

    free(gW1); free(gb1); free(gW2);
    free(z1);  free(a1);
    return loss / (float)bs;
}

// ---------- 학습 메인 ----------
int NN_TrainFromBin(const char* in_path, const char* out_path,
                    int hidden, int epochs, float lr, int batch,
                    float val_split, NNTrainProgress cb)
{
    DS ds; if(!load_bin(in_path,&ds)) return 0;
    if(ds.n < 10){ free_ds(&ds); return 0; }

    // Train/Val split
    int n_val = (int)(ds.n * (val_split<0.f?0.f:(val_split>0.5f?0.5f:val_split)));
    int n_tr  = ds.n - n_val;
    int* idx = (int*)xmalloc(sizeof(int)*ds.n);
    for(int i=0;i<ds.n;i++) idx[i]=i;
    srand((unsigned)time(NULL));
    for(int i=ds.n-1;i>0;i--){ int r=rand()%(i+1); int t=idx[i]; idx[i]=idx[r]; idx[r]=t; }

    MLP m; mlp_init(&m, FEAT_DIM, hidden);
    int steps_per_epoch = (n_tr + batch - 1)/batch;
    if(steps_per_epoch<1) steps_per_epoch=1;

    float beta1=0.9f, beta2=0.999f, eps=1e-8f;
    int t=1;

    for(int ep=1; ep<=epochs; ep++){
        // shuffle train part only
        for(int i=n_val;i<ds.n;i++){ int r = n_val + rand()%(ds.n-n_val); int t2=idx[i]; idx[i]=idx[r]; idx[r]=t2; }

        float epoch_loss=0.f;
        for(int s=0; s<steps_per_epoch; s++){
            int start = n_val + s*batch;
            int bs = batch;
            if(start+bs > ds.n) bs = ds.n - start;
            if(bs<=0) break;
            float L = train_minibatch(&m, ds.X, ds.y, idx, ds.n,
                                      start, bs, lr, beta1, beta2, eps, t++);
            epoch_loss += L;
            if(cb) cb(ep, epochs, s+1, steps_per_epoch, L, -1.f);
        }
        epoch_loss /= (float)steps_per_epoch;

        // validation
        float val_loss=0.f;
        if(n_val>0){
            float* z1=(float*)xmalloc(sizeof(float)*m.H);
            float* a1=(float*)xmalloc(sizeof(float)*m.H);
            //Cache c={z1,a1};
            for(int i=0;i<n_val;i++){
                int id = idx[i];
                //float pred = 0.f; // forward
                float yhat=m.b2;
                for(int h=0;h<m.H;h++){
                    float z=m.b1[h];
                    const float* w1=m.W1 + h*m.D;
                    const float* x = ds.X + id*m.D;
                    for(int d=0; d<m.D; d++) z += w1[d]*x[d];
                    float a = (z>0)?z:0;
                    yhat += m.W2[h]*a;
                }
                float err = yhat - ds.y[id];
                val_loss += 0.5f*err*err;
            }
            free(z1); free(a1);
            val_loss /= (float)n_val;
        }

        if(cb) cb(ep, epochs, steps_per_epoch, steps_per_epoch, epoch_loss, val_loss);
    }

    // 저장
    gNN.in_dim = FEAT_DIM;
    gNN.h_dim  = hidden;
    gNN.W1 = m.W1; gNN.b1 = m.b1; gNN.W2 = m.W2; gNN.b2 = &m.b2;
    gNN.ready=1;
    int ok = NN_Save(out_path);

    // 소유권 넘겼으니 포인터 null 처리
    mlp_free(&m); // m->W1, b1, W2, b2는 free 안 함
    free(idx); free_ds(&ds);
    return ok;
}

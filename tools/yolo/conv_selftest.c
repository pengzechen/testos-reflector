/*
 * conv_selftest.c — validate the restructured closed-form NC1HWC2 im2col and
 * readback loops in yolo_conv.c against the reference feature_data() layout.
 *
 * This copies the EXACT loop bodies from yolo_conv.c (fast path) and, for the
 * same inputs, also fills/reads using the original per-element feature_data()
 * calls (slow path). If they disagree, the restructuring broke something.
 *
 * Build: gcc -O2 -w -I../../include -I../../src conv_selftest.c npu_matmul_host.o -o /tmp/ct
 *   where npu_matmul_host.o provides feature_data() (compiled from npu_matmul.c).
 */
#include "t_types.h"
extern int printf(const char *, ...);
extern void *calloc(size_t, size_t);

int feature_data(int C, int H, int W, int C2, int c, int h, int w);

static uint32_t rng = 2463534242u;
static int8_t r8(void){ rng ^= rng<<13; rng ^= rng>>17; rng ^= rng<<5; return (int8_t)(rng & 0xff); }

int
main(void)
{
    struct { int C,H,W,oc,kh,kw,s,p; } T[] = {
        {3,40,40,16,6,6,2,2}, {32,40,40,16,1,1,1,0}, {16,40,40,16,3,3,1,1},
        {32,20,20,64,3,3,2,1}, {8,10,10,32,1,1,1,0}, {128,20,20,256,3,3,1,1},
    };
    int fails = 0;
    static int8_t in[512*40*40];
    static int8_t fast[2<<20], slow[2<<20];
    static int32_t dev[2<<20], of[512*40*40], os[512*40*40];
    static int kbase[4096];

    for (int t = 0; t < 6; t++) {
        int C=T[t].C,H=T[t].H,W=T[t].W,oc=T[t].oc,kh=T[t].kh,kw=T[t].kw,stride=T[t].s,pad=T[t].p;
        int oh=(H+2*pad-kh)/stride+1, ow=(W+2*pad-kw)/stride+1;
        int N=oc, K=C*kh*kw, M=oh*ow;
        int Kp=((K+31)/32)*32, Np=((oc+31)/32)*32;
        for (int i=0;i<C*H*W;i++) in[i]=r8();

        /* ---- IM2COL: fast (closed-form) vs slow (feature_data) ---- */
        int fb=((Kp+15)/16)*16*M;
        for(int i=0;i<fb;i++){fast[i]=0;slow[i]=0;}
        for(int k=0;k<K;k++) kbase[k]=(k>>4)*M*16+(k&15);
        for(int row=0;row<M;row++){
            int mm=row, oy=mm/ow, ox=mm%ow, by=oy*stride-pad, bx=ox*stride-pad, row16=row*16, k=0;
            for(int c=0;c<C;c++){const int8_t *ch=in+(int64_t)c*H*W;
                for(int kr=0;kr<kh;kr++){int iy=by+kr; const int8_t *rp=ch+iy*W; int inr=(iy>=0&&iy<H);
                    for(int kc=0;kc<kw;kc++,k++){int ix=bx+kc; int8_t v=0; if(inr&&ix>=0&&ix<W)v=rp[ix];
                        fast[kbase[k]+row16]=v;}}}
        }
        for(int row=0;row<M;row++){
            int mm=row, oy=mm/ow, ox=mm%ow, by=oy*stride-pad, bx=ox*stride-pad;
            for(int c=0;c<C;c++){const int8_t *ch=in+(int64_t)c*H*W;
                for(int kr=0;kr<kh;kr++){int iy=by+kr;
                    for(int kc=0;kc<kw;kc++){int ix=bx+kc; int k=(c*kh+kr)*kw+kc; int8_t v=0;
                        if(iy>=0&&iy<H&&ix>=0&&ix<W)v=ch[iy*W+ix];
                        slow[feature_data(Kp,M,1,16,k+1,row+1,1)]=v;}}}
        }
        int im2col_bad=0;
        for(int i=0;i<fb;i++) if(fast[i]!=slow[i]){im2col_bad++; if(im2col_bad<=2)printf("  T%d im2col@%d fast=%d slow=%d\n",t,i,fast[i],slow[i]);}

        /* ---- READBACK: fast vs slow, with a synthetic NC1HWC2 dev buffer ---- */
        int db=((Np+3)/4)*4*M;
        for(int i=0;i<db;i++) dev[i]=(int32_t)r8()*7 + i%13;
        static int nbase[512];
        for(int n=0;n<N;n++) nbase[n]=(n>>2)*M*4+(n&3);
        for(int n=0;n<N;n++){int32_t *outn=of+(int64_t)n*M; int base=nbase[n];
            for(int row=0;row<M;row++) outn[row]=dev[base+row*4];}
        for(int n=0;n<N;n++)for(int row=0;row<M;row++)
            os[(int64_t)n*M+row]=dev[feature_data(Np,M,1,4,n+1,row+1,1)];
        int rb_bad=0;
        for(int n=0;n<N;n++)for(int row=0;row<M;row++)
            if(of[(int64_t)n*M+row]!=os[(int64_t)n*M+row]){rb_bad++; if(rb_bad<=2)printf("  T%d readback n=%d row=%d fast=%d slow=%d\n",t,n,row,of[(int64_t)n*M+row],os[(int64_t)n*M+row]);}

        printf("T%d C=%d %dx%d k%dx%d s%d oc=%d M=%d Kp=%d Np=%d: im2col=%s readback=%s\n",
               t,C,H,W,kh,kw,stride,oc,M,Kp,Np, im2col_bad?"FAIL":"OK", rb_bad?"FAIL":"OK");
        if(im2col_bad||rb_bad) fails++;
    }
    printf(fails?"\nCONV SELFTEST FAILED\n":"\nCONV SELFTEST: ALL OK\n");
    return fails;
}

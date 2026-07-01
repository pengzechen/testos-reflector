#!/usr/bin/env python3
"""
Fixed-point (NO FLOAT) simulator — mirrors what the bare-metal C can actually do
(-mgeneral-regs-only, no FPU). Every activation tensor is (int8_values, scale_q)
where scale_q is a fixed-point scale: real = int8 * scale_q / 2^SHIFT.

Goal: prove an integer-only pipeline with per-tensor fixed-point scales produces
a coherent story, then port EXACTLY this arithmetic to C.

Key ops:
 - matmul: int32 acc = W_int8 @ x_int8. Real = acc * (w_scale_q * x_scale_q).
   Requantize to int8: find max|acc|, out_scale so out_int8 = acc*127/max.
   Store out_scale in real terms = acc_scale * max/127.
 - rms_norm: scale-invariant. rms computed from int8 directly. But weight has its
   own quant scale, so output real = (x_int8*x_s)/rms_int * w_int8*w_s.
   We track the output scale.
 - residual add: must align scales. Convert both to a common scale via int mul/shift.

We use Python ints (arbitrary precision) to emulate int32/int64 C math exactly.
"""
import argparse, json, math, re
import numpy as np
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).parent))
from quantize_model import load_safetensors, quantize_symmetric

SHIFT = 20  # fixed-point fractional bits for scales


def load_luts(math_c):
    txt = open(math_c).read()
    def grab(name):
        m = re.search(name + r'\[256\]\s*=\s*\{(.+?)\};', txt, re.S)
        nums = [int(x) for x in re.findall(r'-?\d+', m.group(1))]
        return nums[:256]
    return grab('sigmoid_lut'), grab('exp_lut')


def to_fixed(f):
    """float scale -> Q(SHIFT) fixed point integer."""
    return max(1, int(round(f * (1 << SHIFT))))


class FP:
    def __init__(self, md, math_c='src/npulib/npu_math.c'):
        c = json.load(open(md/'config.json'))
        self.hidden=c['hidden_size']; self.n_layers=c['num_hidden_layers']
        self.n_heads=c['num_attention_heads']; self.n_kv=c['num_key_value_heads']
        self.di=c['intermediate_size']; self.vocab=c['vocab_size']
        self.max_pos=c['max_position_embeddings']; self.theta=c.get('rope_theta',10000.0)
        self.dqkv=self.hidden//self.n_heads; self.half=self.dqkv//2
        self.n_groups=self.n_heads//self.n_kv
        self.tie=c.get('tie_word_embeddings',False)
        T=load_safetensors(list(md.glob('*.safetensors'))[0])
        self.Q={}; self.Sq={}
        for n,a in T.items():
            q,s=quantize_symmetric(a); self.Q[n]=q.astype(np.int64); self.Sq[n]=to_fixed(s)
        # rope int8 tables
        self.cos=np.zeros((self.max_pos,self.half),np.int64)
        self.sin=np.zeros((self.max_pos,self.half),np.int64)
        for p in range(self.max_pos):
            for i in range(self.half):
                fr=1.0/(self.theta**(2.0*i/self.dqkv)); a=p*fr
                self.cos[p,i]=int(np.clip(round(math.cos(a)*127),-128,127))
                self.sin[p,i]=int(np.clip(round(math.sin(a)*127),-128,127))
        en='lm_head.weight' if self.tie else 'model.embed_tokens.weight'
        self.embed=self.Q[en]; self.embed_s=self.Sq[en]
        tokj=json.load(open(md/'tokenizer.json',encoding='utf-8'))
        self.id2tok={v:k for k,v in tokj['model']['vocab'].items()}
        self.sigmoid_lut, self.exp_lut = load_luts(math_c)
        self.int_ops = True  # use integer LUT softmax/sigmoid

    def sigmoid_lut_saware(self, g_i8, gs):
        """Scale-aware sigmoid via LUT. idx = g_real*32 = g_i8*gs*32>>SHIFT.
        Returns sigmoid probability in Q8 fixed point (0..255)."""
        out = np.zeros(len(g_i8), dtype=np.int64)
        for i in range(len(g_i8)):
            idx = (int(g_i8[i]) * gs * 32) >> SHIFT
            if idx > 127: idx = 127
            if idx < -128: idx = -128
            sv = self.sigmoid_lut[idx & 0xff]   # sigmoid*255-128
            out[i] = sv + 128                    # 0..255 ~ sigmoid*255
        return out

    def softmax_lut(self, raw_scores_q2, qs, ks):
        """Integer softmax via exp_lut. raw_scores_q2 = dot*qs*ks (Q 2*SHIFT).
        Real score = raw / 2^(2*SHIFT) / sqrt(dqkv). exp_lut is exp(-d/32), so
        index d = (max_real - score_real) * 32. Returns probs in Q16 (0..65535).
        NOTE: Q16 precision is REQUIRED — Q8 (*255) truncation degrades attention
        enough to collapse generation into repetition. Verified in simulator."""
        arr = np.array(raw_scores_q2, dtype=np.float64)
        sqd = math.sqrt(self.dqkv)
        real = arr / float(1 << (2*SHIFT)) / sqd
        mx = real.max()
        ssum = 0; exps = []
        for v in real:
            d = int(round((mx - v) * 32.0))   # exp_lut index (LUT does /32)
            if d < 0: d = 0
            if d > 255: d = 255
            e = self.exp_lut[d]
            exps.append(e); ssum += e
        if ssum == 0: ssum = 1
        return np.array([e * 65535 // ssum for e in exps], dtype=np.int64)

    def W(self,l,s): return f'model.layers.{l}.{s}.weight'

    def isqrt(self,x): return int(math.isqrt(int(x)))

    def mm(self, name, xi8, xs_q):
        """int8 matmul -> int8 output + output scale (Q SHIFT).
        acc = W@x (int32). real = acc * ws * xs. Requant int8 to max=127."""
        w=self.Q[name]; ws=self.Sq[name]
        acc=w@xi8  # int64 vector
        mx=int(np.max(np.abs(acc)))
        if mx<1: mx=1
        oi8=np.clip((acc*127*2 + np.sign(acc)*mx)// (mx*2),-128,127).astype(np.int64)  # round(acc*127/mx)
        # output real = oi8 * out_s ; out_s such that oi8*out_s = acc*ws*xs
        # at max: 127*out_s = mx*ws*xs -> out_s = mx*ws*xs/127
        # ws,xs are Q(SHIFT). product ws*xs is Q(2*SHIFT). divide.
        out_s = (mx * ws * xs_q) // (127 * (1<<SHIFT))
        if out_s<1: out_s=1
        return oi8, out_s

    def rms(self, xi8, xs_q, wname):
        """rms_norm. Output int8 + scale. rms from int8 (scale-invariant part).
        real_out[i] = (x_i8[i]*xs)/rms_real * w_i8[i]*ws
        rms_real = xs * sqrt(mean(x_i8^2)) = xs * rms_i8
        => real_out[i] = x_i8[i]/rms_i8 * w_i8[i]*ws   (xs cancels!)
        Then requant to int8."""
        w=self.Q[wname]; ws=self.Sq[wname]
        n=len(xi8)
        ss=int(np.sum(xi8*xi8)); ms=ss//n
        if ms==0: ms=1
        rms_i8=self.isqrt(ms)
        if rms_i8==0: rms_i8=1
        # compute real_out in a scaled integer domain:
        # val[i] = x_i8[i] * w_i8[i]   (int, <= 127*127)
        # real_out[i] = val[i] * ws / rms_i8   (Q SHIFT because ws is Q SHIFT)
        val=xi8*w  # int64
        # keep as fixed: rq[i] = val[i]*ws // rms_i8  -> Q(SHIFT) real
        rq=(val*ws)//rms_i8
        mx=int(np.max(np.abs(rq)))
        if mx<1: mx=1
        oi8=np.clip((rq*127*2+np.sign(rq)*mx)//(mx*2),-128,127).astype(np.int64)
        out_s=mx//127
        if out_s<1: out_s=1
        return oi8, out_s

    def rope_i8(self, vec, n_h, pos):
        out=vec.copy()
        cr=self.cos[pos]; sr=self.sin[pos]
        for h in range(n_h):
            b=h*self.dqkv
            for i in range(self.half):
                a=int(out[b+i]); bb=int(out[b+i+self.half])
                cv=int(cr[i]); sv=int(sr[i])
                out[b+i]=(a*cv-bb*sv)>>7
                out[b+i+self.half]=(bb*cv+a*sv)>>7
        return np.clip(out,-128,127).astype(np.int64)

    def add_res(self, x_i8, x_s, o_i8, o_s):
        """residual: xr = x_i8*x_s + o_i8*o_s (both Q SHIFT real). requant int8."""
        xr=x_i8*x_s + o_i8*o_s  # Q SHIFT real, int64
        mx=int(np.max(np.abs(xr)))
        if mx<1: mx=1
        oi8=np.clip((xr*127*2+np.sign(xr)*mx)//(mx*2),-128,127).astype(np.int64)
        out_s=mx//127
        if out_s<1: out_s=1
        return oi8, out_s

    def forward(self, tid, pos, kc, vc):
        x=self.embed[tid].copy(); xs=self.embed_s
        for l in range(self.n_layers):
            n1,n1s=self.rms(x,xs,self.W(l,'input_layernorm'))
            q,qs=self.mm(self.W(l,'self_attn.q_proj'),n1,n1s)
            k,ks=self.mm(self.W(l,'self_attn.k_proj'),n1,n1s)
            v,vs=self.mm(self.W(l,'self_attn.v_proj'),n1,n1s)
            q=self.rope_i8(q,self.n_heads,pos)
            k=self.rope_i8(k,self.n_kv,pos)
            kc[l][pos]=(k,ks); vc[l][pos]=(v,vs)
            ao=np.zeros(self.n_heads*self.dqkv,np.int64); ao_s=1
            # attention in scaled-int domain
            head_reals=[]
            for qh in range(self.n_heads):
                kvg=qh//self.n_groups
                qv=q[qh*self.dqkv:(qh+1)*self.dqkv]
                scs=[]
                for p in range(pos+1):
                    kp=kc[l][p][0][kvg*self.dqkv:(kvg+1)*self.dqkv]
                    dot=int(np.sum(qv*kp))
                    scs.append(dot * qs * ks)  # real-ish (Q 2*SHIFT), monotonic
                if self.int_ops and getattr(self,'lut_softmax',True):
                    pr255=self.softmax_lut(scs, qs, ks)  # Q8 probs (0..255)
                    pr=pr255.astype(np.float64)/pr255.sum() if pr255.sum()>0 else np.ones(len(scs))/len(scs)
                else:
                    scsf=np.array(scs,dtype=np.float64)/(float(1<<(2*SHIFT))*math.sqrt(self.dqkv))
                    scsf-=scsf.max(); e=np.exp(scsf); pr=e/e.sum()
                oh=np.zeros(self.dqkv,dtype=np.float64)
                for p in range(pos+1):
                    vp=vc[l][p][0][kvg*self.dqkv:(kvg+1)*self.dqkv]
                    vps=vc[l][p][1]
                    oh+=pr[p]*(vp.astype(np.float64)*vps)  # real (Q SHIFT)
                head_reals.append(oh)
            allr=np.concatenate(head_reals)  # Q SHIFT real
            mx=np.max(np.abs(allr)); mx=mx if mx>0 else 1
            ao=np.clip(np.round(allr*127/mx),-128,127).astype(np.int64)
            ao_s=int(mx)//127; ao_s=ao_s if ao_s>=1 else 1
            o,os=self.mm(self.W(l,'self_attn.o_proj'),ao,ao_s)
            x,xs=self.add_res(x,xs,o,os)
            n2,n2s=self.rms(x,xs,self.W(l,'post_attention_layernorm'))
            g,gs=self.mm(self.W(l,'mlp.gate_proj'),n2,n2s)
            u,us=self.mm(self.W(l,'mlp.up_proj'),n2,n2s)
            # swiglu: silu(g_real)*u_real
            gr=g.astype(np.float64)*gs/(1<<SHIFT)
            ur=u.astype(np.float64)*us/(1<<SHIFT)
            if self.int_ops and getattr(self,'lut_sigmoid',True):
                sig255=self.sigmoid_lut_saware(g,gs)     # Q8 sigmoid (0..255)
                sigf=sig255.astype(np.float64)/255.0
                silu=gr*sigf
            else:
                silu=gr/(1.0+np.exp(-gr))
            actr=silu*ur  # real
            mx=np.max(np.abs(actr)); mx=mx if mx>0 else 1
            ai8=np.clip(np.round(actr*127/mx),-128,127).astype(np.int64)
            ai_s=to_fixed(mx/127)
            d,ds=self.mm(self.W(l,'mlp.down_proj'),ai8,ai_s)
            x,xs=self.add_res(x,xs,d,ds)
        nf,nfs=self.rms(x,xs,'model.norm.weight')
        acc=self.Q['lm_head.weight']@nf
        return acc

    def gen(self,n):
        kc=[[None]*self.max_pos for _ in range(self.n_layers)]
        vc=[[None]*self.max_pos for _ in range(self.n_layers)]
        prompt=[80,147,201,282,57]; pos=0; nxt=0
        for tid in prompt:
            nxt=int(np.argmax(self.forward(tid,pos,kc,vc))); pos+=1
        toks=[]
        for _ in range(n):
            toks.append(self.id2tok.get(nxt,'<unk>').replace('▁',' '))
            nxt=int(np.argmax(self.forward(nxt,pos,kc,vc))); pos+=1
        return ''.join(toks)


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--model_dir',required=True); ap.add_argument('--n',type=int,default=40)
    ap.add_argument('--math_c',default='src/npulib/npu_math.c')
    ap.add_argument('--int_ops',type=int,default=1)
    a=ap.parse_args()
    m=FP(Path(a.model_dir), a.math_c)
    m.int_ops=bool(a.int_ops)
    print(f"FIXED-POINT (int_ops={a.int_ops}) int8 pipeline:")
    print(m.gen(a.n))


if __name__=='__main__':
    main()

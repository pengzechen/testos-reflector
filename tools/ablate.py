#!/usr/bin/env python3
"""
Ablation: run the SAME forward with selectable precision per stage to find
where INT8 quantization destroys the signal.

Modes:
  float       : full float (reference)
  w8          : INT8 weights, float activations, float matmul
  w8_a8mm     : INT8 weights + INT8 activations, but matmul accumulates then
                requantizes with float scale (no OUT_CVT integer rounding)
  full_int8   : everything integer (mirrors hardware)

By diffing consecutive modes we localize the loss.
"""
import argparse, json, math
import numpy as np
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).parent))
from quantize_model import load_safetensors, quantize_symmetric


def rms_norm_f(x, w, eps=1e-5):
    return x / np.sqrt(np.mean(x*x)+eps) * w


def softmax(x):
    x = x - x.max(); e = np.exp(x); return e/e.sum()


class Model:
    def __init__(self, md):
        self.cfg = json.load(open(md/'config.json'))
        c = self.cfg
        self.hidden=c['hidden_size']; self.n_layers=c['num_hidden_layers']
        self.n_heads=c['num_attention_heads']; self.n_kv=c['num_key_value_heads']
        self.di=c['intermediate_size']; self.vocab=c['vocab_size']
        self.max_pos=c['max_position_embeddings']; self.theta=c.get('rope_theta',10000.0)
        self.dqkv=self.hidden//self.n_heads; self.half=self.dqkv//2
        self.n_groups=self.n_heads//self.n_kv
        self.tie=c.get('tie_word_embeddings',False)
        self.T = load_safetensors(list(md.glob('*.safetensors'))[0])
        # quantize weights: store int8 + per-tensor scale
        self.Q={}; self.S={}
        for n,a in self.T.items():
            q,s = quantize_symmetric(a); self.Q[n]=q; self.S[n]=s
        self.inv_freq = 1.0/(self.theta**(np.arange(0,self.dqkv,2)/self.dqkv))
        embed_name='lm_head.weight' if self.tie else 'model.embed_tokens.weight'
        self.embed_f = self.T[embed_name]
        self.embed_q = self.Q[embed_name]; self.embed_s = self.S[embed_name]
        tokj=json.load(open(md/'tokenizer.json',encoding='utf-8'))
        self.id2tok={v:k for k,v in tokj['model']['vocab'].items()}

    def W(self,l,s): return f'model.layers.{l}.{s}.weight'

    def linear(self, name, x_f, mode):
        """x_f: float activation vector. Returns float output."""
        if mode=='float':
            return self.T[name] @ x_f
        wq = self.Q[name].astype(np.float64); ws = self.S[name]
        if mode=='w8':
            # int8 weight (dequant), float activation
            return (wq*ws) @ x_f
        # activations quantized to int8 too
        ax = np.max(np.abs(x_f))
        if ax < 1e-9: ax = 1e-9
        asc = ax/127.0
        xq = np.clip(np.round(x_f/asc), -128, 127)
        acc = wq @ xq  # int32 accumulate
        if mode=='w8_a8mm':
            return acc * ws * asc  # dequant with DYNAMIC activation scale
        if mode=='w8_a8_fixedscale':
            # mimic hardware: OUT_CVT produces int8 with FIXED offline scale,
            # then that int8 is the activation for the next layer (scale folded in)
            # Here we just requantize the int32 output to int8 using a fixed
            # per-layer scale calibrated once, then dequant for the float path.
            fs = self.fixed_scale.get(name, ws*asc)
            out_i8 = np.clip(np.round(acc * fs), -128, 127)
            # return as int8-domain values (NOT dequanted) — next rms_norm sees int8
            return out_i8.astype(np.float64)
        return acc * ws * asc

    def rope(self, vec, n_h, pos):
        out = vec.reshape(n_h, self.dqkv).copy()
        for i in range(self.half):
            c=math.cos(pos*self.inv_freq[i]); s=math.sin(pos*self.inv_freq[i])
            a=out[:,i].copy(); b=out[:,i+self.half].copy()
            out[:,i]=a*c-b*s; out[:,i+self.half]=b*c+a*s
        return out.reshape(-1)

    def forward(self, tid, pos, kc, vc, mode):
        if mode=='float':
            x=self.embed_f[tid].astype(np.float64).copy()
        else:
            x=(self.embed_q[tid].astype(np.float64))*self.embed_s
        for l in range(self.n_layers):
            h=rms_norm_f(x, self.T[self.W(l,'input_layernorm')])
            q=self.linear(self.W(l,'self_attn.q_proj'),h,mode)
            k=self.linear(self.W(l,'self_attn.k_proj'),h,mode)
            v=self.linear(self.W(l,'self_attn.v_proj'),h,mode)
            q=self.rope(q,self.n_heads,pos); k=self.rope(k,self.n_kv,pos)
            kc[l][pos]=k; vc[l][pos]=v
            ao=np.zeros(self.n_heads*self.dqkv)
            for qh in range(self.n_heads):
                kvg=qh//self.n_groups
                qv=q[qh*self.dqkv:(qh+1)*self.dqkv]
                sc=np.array([qv@kc[l][p][kvg*self.dqkv:(kvg+1)*self.dqkv] for p in range(pos+1)])/math.sqrt(self.dqkv)
                pr=softmax(sc)
                oh=sum(pr[p]*vc[l][p][kvg*self.dqkv:(kvg+1)*self.dqkv] for p in range(pos+1))
                ao[qh*self.dqkv:(qh+1)*self.dqkv]=oh
            x=x+self.linear(self.W(l,'self_attn.o_proj'),ao,mode)
            h2=rms_norm_f(x, self.T[self.W(l,'post_attention_layernorm')])
            g=self.linear(self.W(l,'mlp.gate_proj'),h2,mode)
            u=self.linear(self.W(l,'mlp.up_proj'),h2,mode)
            silu=g/(1.0+np.exp(-g))
            x=x+self.linear(self.W(l,'mlp.down_proj'),silu*u,mode)
        h=rms_norm_f(x, self.T['model.norm.weight'])
        return self.linear('lm_head.weight',h,mode)

    def generate(self, mode, n):
        kc=[[None]*self.max_pos for _ in range(self.n_layers)]
        vc=[[None]*self.max_pos for _ in range(self.n_layers)]
        prompt=[80,147,201,282,57]; pos=0; nxt=0
        for tid in prompt:
            nxt=int(np.argmax(self.forward(tid,pos,kc,vc,mode))); pos+=1
        toks=[]
        for _ in range(n):
            toks.append(self.id2tok.get(nxt,'<unk>').replace('▁',' '))
            nxt=int(np.argmax(self.forward(nxt,pos,kc,vc,mode))); pos+=1
        return ''.join(toks)


    def forward_int8(self, tid, pos, kc, vc):
        """Full int8-domain path mirroring hardware, but with DYNAMIC per-op
        requantization (output int8 = round(int32 * 127 / max|int32|)).
        Activations stay in int8 domain across ops (like hardware)."""
        h_i8 = self.embed_q[tid].astype(np.int64)  # int8 domain

        def mm_dyn(name, x_i8):
            wq = self.Q[name].astype(np.int64)
            acc = wq @ x_i8
            mx = np.max(np.abs(acc))
            if mx < 1: mx = 1
            return np.clip(np.round(acc*127.0/mx), -128, 127).astype(np.int64), acc

        def rms_i8(x_i8, wname):
            x = x_i8.astype(np.int64)
            w = self.Q[wname].astype(np.int64)
            n=len(x); ss=int(np.sum(x*x)); ms=ss//n
            if ms==0: ms=1
            rms=int(math.isqrt(ms))
            if rms==0: rms=1
            val=np.array([int(x[i]*w[i]) for i in range(n)],dtype=np.int64)//rms
            return np.clip(val,-128,127).astype(np.int64)

        x=h_i8
        for l in range(self.n_layers):
            nrm=rms_i8(x, self.W(l,'input_layernorm'))
            q,_=mm_dyn(self.W(l,'self_attn.q_proj'),nrm)
            k,_=mm_dyn(self.W(l,'self_attn.k_proj'),nrm)
            v,_=mm_dyn(self.W(l,'self_attn.v_proj'),nrm)
            q=self.rope(q.astype(np.float64),self.n_heads,pos)
            q=np.clip(np.round(q),-128,127).astype(np.int64)
            k=self.rope(k.astype(np.float64),self.n_kv,pos)
            k=np.clip(np.round(k),-128,127).astype(np.int64)
            kc[l][pos]=k; vc[l][pos]=v
            ao=np.zeros(self.n_heads*self.dqkv,dtype=np.int64)
            for qh in range(self.n_heads):
                kvg=qh//self.n_groups
                qv=q[qh*self.dqkv:(qh+1)*self.dqkv]
                sc=np.array([int(qv@kc[l][p][kvg*self.dqkv:(kvg+1)*self.dqkv]) for p in range(pos+1)],dtype=np.float64)
                sc/=math.sqrt(self.dqkv)*127.0/8  # heuristic scale to reasonable range
                pr=softmax(sc)
                oh=np.zeros(self.dqkv)
                for p in range(pos+1):
                    oh+=pr[p]*vc[l][p][kvg*self.dqkv:(kvg+1)*self.dqkv]
                ao[qh*self.dqkv:(qh+1)*self.dqkv]=np.clip(np.round(oh),-128,127)
            o,_=mm_dyn(self.W(l,'self_attn.o_proj'),ao)
            x=np.clip(x + (o>>1),-128,127)
            nrm2=rms_i8(x, self.W(l,'post_attention_layernorm'))
            g,_=mm_dyn(self.W(l,'mlp.gate_proj'),nrm2)
            u,_=mm_dyn(self.W(l,'mlp.up_proj'),nrm2)
            gf=g.astype(np.float64)
            sig=127.0/(1.0+np.exp(-gf*(np.max(np.abs(gf))/127.0 if np.max(np.abs(gf))>0 else 1)/1.0))
            # silu approx
            silu=np.clip(np.round(g*sig/127.0),-128,127).astype(np.int64)
            act=np.clip((silu*u)>>7,-128,127).astype(np.int64)
            d,_=mm_dyn(self.W(l,'mlp.down_proj'),act)
            x=np.clip(x + (d>>1),-128,127)
        nrm=rms_i8(x,'model.norm.weight')
        lm,acc=mm_dyn('lm_head.weight',nrm)
        return acc  # use raw int32 for argmax

    def generate_int8(self, n):
        kc=[[None]*self.max_pos for _ in range(self.n_layers)]
        vc=[[None]*self.max_pos for _ in range(self.n_layers)]
        prompt=[80,147,201,282,57]; pos=0; nxt=0
        for tid in prompt:
            nxt=int(np.argmax(self.forward_int8(tid,pos,kc,vc))); pos+=1
        toks=[]
        for _ in range(n):
            toks.append(self.id2tok.get(nxt,'<unk>').replace('▁',' '))
            nxt=int(np.argmax(self.forward_int8(nxt,pos,kc,vc))); pos+=1
        return ''.join(toks)


    def forward_int8_scaled(self, tid, pos, kc, vc):
        """int8 activations + a running FLOAT scale per activation tensor.
        This is the standard int8 inference contract: each tensor is
        (int8_values, scale) where real = int8 * scale. matmul does int32
        accumulate; requant to int8 uses the KNOWN output scale.
        rms_norm operates on the REAL (dequantized) values.
        This is implementable on the NPU: OUT_CVT scale == (in_s*w_s/out_s)."""
        # embedding: real = int8 * embed_s
        x_i8 = self.embed_q[tid].astype(np.int64)
        x_s = self.embed_s

        def mm(name, xi8, xs):
            wq = self.Q[name].astype(np.int64); ws=self.S[name]
            acc = wq @ xi8                     # int32
            real = acc.astype(np.float64)*xs*ws  # true float output
            mx = np.max(np.abs(real))
            if mx<1e-9: mx=1e-9
            os = mx/127.0
            oi8 = np.clip(np.round(real/os),-128,127).astype(np.int64)
            return oi8, os

        def rms(xi8, xs, wname):
            real = xi8.astype(np.float64)*xs
            r = np.sqrt(np.mean(real*real)+1e-5)
            w = self.T[wname]
            out = real/r*w
            mx=np.max(np.abs(out))
            if mx<1e-9: mx=1e-9
            os=mx/127.0
            return np.clip(np.round(out/os),-128,127).astype(np.int64), os

        x=x_i8; xs=x_s
        for l in range(self.n_layers):
            n1,n1s=rms(x,xs,self.W(l,'input_layernorm'))
            q,qs=mm(self.W(l,'self_attn.q_proj'),n1,n1s)
            k,ks=mm(self.W(l,'self_attn.k_proj'),n1,n1s)
            v,vs=mm(self.W(l,'self_attn.v_proj'),n1,n1s)
            qf=self.rope((q.astype(np.float64)),self.n_heads,pos)
            kf=self.rope((k.astype(np.float64)),self.n_kv,pos)
            kc[l][pos]=(kf,ks); vc[l][pos]=(v.astype(np.float64),vs)
            ao=np.zeros(self.n_heads*self.dqkv);
            for qh in range(self.n_heads):
                kvg=qh//self.n_groups
                qv=qf[qh*self.dqkv:(qh+1)*self.dqkv]*qs
                sc=np.array([qv@(kc[l][p][0][kvg*self.dqkv:(kvg+1)*self.dqkv]*kc[l][p][1]) for p in range(pos+1)])/math.sqrt(self.dqkv)
                pr=softmax(sc)
                oh=sum(pr[p]*(vc[l][p][0][kvg*self.dqkv:(kvg+1)*self.dqkv]*vc[l][p][1]) for p in range(pos+1))
                ao[qh*self.dqkv:(qh+1)*self.dqkv]=oh
            # ao is real; quantize
            amx=np.max(np.abs(ao)); amx=amx if amx>1e-9 else 1e-9; aos=amx/127.0
            ai8=np.clip(np.round(ao/aos),-128,127).astype(np.int64)
            o,os=mm(self.W(l,'self_attn.o_proj'),ai8,aos)
            # residual add in REAL domain
            xr=x.astype(np.float64)*xs + o.astype(np.float64)*os
            xmx=np.max(np.abs(xr)); xmx=xmx if xmx>1e-9 else 1e-9; xs=xmx/127.0
            x=np.clip(np.round(xr/xs),-128,127).astype(np.int64)
            n2,n2s=rms(x,xs,self.W(l,'post_attention_layernorm'))
            g,gs=mm(self.W(l,'mlp.gate_proj'),n2,n2s)
            u,us=mm(self.W(l,'mlp.up_proj'),n2,n2s)
            gr=g.astype(np.float64)*gs; ur=u.astype(np.float64)*us
            silu=gr/(1.0+np.exp(-gr))
            actr=silu*ur
            amx=np.max(np.abs(actr)); amx=amx if amx>1e-9 else 1e-9; aos=amx/127.0
            ai8=np.clip(np.round(actr/aos),-128,127).astype(np.int64)
            d,ds=mm(self.W(l,'mlp.down_proj'),ai8,aos)
            xr=x.astype(np.float64)*xs + d.astype(np.float64)*ds
            xmx=np.max(np.abs(xr)); xmx=xmx if xmx>1e-9 else 1e-9; xs=xmx/127.0
            x=np.clip(np.round(xr/xs),-128,127).astype(np.int64)
        nf,nfs=rms(x,xs,'model.norm.weight')
        wq=self.Q['lm_head.weight'].astype(np.int64)
        acc=wq@nf
        return acc

    def generate_int8_scaled(self,n):
        kc=[[None]*self.max_pos for _ in range(self.n_layers)]
        vc=[[None]*self.max_pos for _ in range(self.n_layers)]
        prompt=[80,147,201,282,57]; pos=0; nxt=0
        for tid in prompt:
            nxt=int(np.argmax(self.forward_int8_scaled(tid,pos,kc,vc))); pos+=1
        toks=[]
        for _ in range(n):
            toks.append(self.id2tok.get(nxt,'<unk>').replace('▁',' '))
            nxt=int(np.argmax(self.forward_int8_scaled(nxt,pos,kc,vc))); pos+=1
        return ''.join(toks)


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--model_dir',required=True)
    ap.add_argument('--n',type=int,default=40)
    args=ap.parse_args()
    m=Model(Path(args.model_dir))
    m.fixed_scale={}
    for mode in ['float','w8','w8_a8mm']:
        print(f"\n=== mode={mode} ===")
        print(m.generate(mode,args.n))
    print(f"\n=== mode=full_int8_dynamic ===")
    print(m.generate_int8(args.n))
    print(f"\n=== mode=int8_scaled (running float scale) ===")
    print(m.generate_int8_scaled(args.n))


if __name__=='__main__':
    main()

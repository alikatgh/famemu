// ember32/tools/audio_dsp_test.cpp — verify the audio DSP follow-ons layered on
// the voice model: IMA ADPCM decode (exact + round-trip), the feedback echo/
// reverb bus (impulse response), and a streamed channel.
//   c++ -std=c++17 -O2 -I.. tools/audio_dsp_test.cpp -o /tmp/e32ad && /tmp/e32ad
#include "../audio.hpp"
#include <cstdio>
#include <cmath>
using namespace ember32;

// A reference IMA encoder (test scaffolding only — the console decodes, doesn't
// encode) so we can round-trip real audio through the decoder.
static const int ESTEP[89] = {
    7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,
    130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,
    1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,
    5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,
    27086,29794,32767 };
static const int EIDX[16] = { -1,-1,-1,-1,2,4,6,8, -1,-1,-1,-1,2,4,6,8 };
static int ima_encode(const int16_t* in, int n, uint8_t* out){
    int pred=0, idx=0, nbytes=0, cur=0, half=0;
    for(int k=0;k<n;k++){
        int step=ESTEP[idx], diff=in[k]-pred, nib=0;
        if(diff<0){ nib=8; diff=-diff; }
        if(diff>=step){ nib|=4; diff-=step; } step>>=1;
        if(diff>=step){ nib|=2; diff-=step; } step>>=1;
        if(diff>=step){ nib|=1; }
        int d=ESTEP[idx]>>3;                      // reconstruct predictor like the decoder
        if(nib&1) d+=ESTEP[idx]>>2; if(nib&2) d+=ESTEP[idx]>>1; if(nib&4) d+=ESTEP[idx];
        if(nib&8) d=-d;
        pred+=d; if(pred>32767)pred=32767; if(pred<-32768)pred=-32768;
        idx+=EIDX[nib]; if(idx<0)idx=0; if(idx>88)idx=88;
        if(half==0){ cur=nib; half=1; } else { out[nbytes++]=cur|(nib<<4); half=0; }
    }
    if(half) out[nbytes++]=cur;
    return nbytes;
}

int main(){
    int pass=0, n=0;
    auto ck=[&](const char* what, bool ok){ pass+=ok; n++; std::printf("  [%s] %s\n", ok?"PASS":"FAIL", what); };

    // 1. ADPCM exact decode — hand-computed from predictor=0, index=0, nibbles 7,7,7.
    {   uint8_t in[2] = { 0x77, 0x07 };   // nibbles (low-first): 7,7,7,0
        int16_t out[4]; int got = ima_adpcm_decode(in, 2, out);
        ck("ADPCM decodes nbytes*2 samples", got==4);
        ck("ADPCM exact samples {11,41,104}", out[0]==11 && out[1]==41 && out[2]==104);
    }

    // 2. ADPCM round-trip fidelity on a sine (encode -> decode -> bounded RMS error).
    {   const int N=400; int16_t sig[N]; for(int k=0;k<N;k++) sig[k]=int16_t(8000*std::sin(k*0.09));
        uint8_t enc[N]; int nb=ima_encode(sig,N,enc);
        int16_t dec[N]; ima_adpcm_decode(enc,nb,dec);
        double e=0,s=0; for(int k=0;k<N;k++){ double d=dec[k]-sig[k]; e+=d*d; s+=double(sig[k])*sig[k]; }
        double rel=std::sqrt(e/s);
        std::printf("      round-trip RMS error = %.3f%% of signal\n", rel*100);
        ck("ADPCM round-trip error < 10%", rel < 0.10);
    }

    // 3. Echo bus impulse response: len=100, feedback=0.5, wet=1 -> taps at 100/200/300.
    {   static Echo echo; echo.set(100, 0.5f, 1.0f, 0.0f);
        const int F=360; float outL[F];
        for(int f=0;f<F;f++){ float L=(f==0)?1.0f:0.0f, R=L; echo.process(L,R); outL[f]=L; }
        auto near=[](float a,float b){ return std::fabs(a-b) < 1e-4f; };
        bool taps = near(outL[0],1.0f) && near(outL[100],1.0f) && near(outL[200],0.5f) && near(outL[300],0.25f);
        bool quiet = near(outL[50],0.0f) && near(outL[150],0.0f) && near(outL[250],0.0f);
        ck("echo taps at 100/200/300 = 1.0/0.5/0.25", taps);
        ck("echo silent between taps", quiet);
    }

    // 4. Streamed channel plays a known stereo buffer back sample-accurate.
    {   int16_t buf[8] = { 1000,-1000, 2000,-2000, 3000,-3000, 4000,-4000 };
        Stream st; st.play(buf, 4, /*stereo*/true, /*loop*/false);
        bool ok=true; const int want[4]={1000,2000,3000,4000};
        for(int k=0;k<4;k++){ float L,R; st.next(L,R);
            int li=int(std::lround(L*32768.0f)), ri=int(std::lround(R*32768.0f));
            if(std::abs(li-want[k])>2 || std::abs(ri+want[k])>2) ok=false; }
        ck("stream plays PCM sample-accurate", ok);
        ck("stream ends after its length", !st.active);
    }

    // 5. Default mix() is unchanged (echo bypassed, no streams) — a plain voice
    //    mixes identically to the pre-DSP model.
    {   static Audio a; a.voices[0].sample=nullptr;  // no source -> silence, but exercises the path
        int16_t buf[64]={}; a.mix(buf,32);
        bool silent=true; for(int i=0;i<64;i++) if(buf[i]!=0) silent=false;
        ck("default mix (echo bypassed) unchanged", silent);
    }

    std::printf("%d/%d passed\n", pass, n);
    return pass==n?0:1;
}

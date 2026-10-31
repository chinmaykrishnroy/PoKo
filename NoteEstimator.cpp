#include "NoteEstimator.h"
#include <algorithm>
#include <math.h>

namespace {
constexpr float PI=3.14159265358979323846f;
constexpr float STRETCH[]={0,.00005f,.00015f,.0003f,.0006f};
float cents(float a,float b) { return 1200*log2f(a/b); }
unsigned gcd(unsigned a,unsigned b) { while(b) { unsigned r=a%b;a=b;b=r; }return a; }
}
NoteEstimator::NoteEstimator() {
    for(unsigned h=0;h<HARMONICS;++h) _harmonics[h]=powf((float)(h+1),-.5f);
    for(unsigned n=0;n<CANDIDATES;++n) _centers[n]=_reference*powf(2.0f,((float)(FIRST_MIDI+n)-69)/12);
    for(unsigned s=0;s<5;++s) for(unsigned h=0;h<HARMONICS;++h)
        _stretchFactors[s][h]=sqrtf((1+STRETCH[s]*(h+1)*(h+1))/(1+STRETCH[s]));
    reset();
}
void NoteEstimator::reset() {
    _frame={};_lastTime=_onsetTime=0;_history=_haveOnset=false;_previousDb=-120;_singleFundamental=-1;
    for(float& x:_tracked) x=0;
    for(float& x:_previousFast) x=0;
    for(float& x:_previousNotes) x=0;
}
bool NoteEstimator::configure(float referenceHz,Profile profile) {
    if(!isfinite(referenceHz) || referenceHz<400 || referenceHz>480 || (profile!=GUITAR && profile!=KEYBOARD)) return false;
    if(referenceHz!=_reference || profile!=_profile) {
        _reference=referenceHz;_profile=profile;
        for(unsigned h=0;h<HARMONICS;++h) _harmonics[h]=powf((float)(h+1),profile==GUITAR?-.5f:-.75f);
        for(unsigned n=0;n<CANDIDATES;++n) _centers[n]=_reference*powf(2.0f,((float)(FIRST_MIDI+n)-69)/12);
        reset();
    }
    return true;
}
int NoteEstimator::nearest(float frequency,unsigned count) const {
    unsigned low=0,high=count;
    while(low<high) { unsigned mid=(low+high)/2;if(_peaks[mid].frequency<frequency) low=mid+1;else high=mid; }
    if(low && (low==count || frequency-_peaks[low-1].frequency<_peaks[low].frequency-frequency)) --low;
    return low<count?(int)low:-1;
}
void NoteEstimator::build(unsigned count,float offset) {
    const bool clean=count<=8;
    float tuningFactor=powf(2.0f,offset/1200);
    auto tolerance=[&](float f,float bin,unsigned h) {
        return clean?f*.001f+bin*.04f*(h+1):f*.006f+bin*.04f*(h+1);
    };
    for(unsigned n=0;n<CANDIDATES;++n) {
        auto& model=_hypotheses[n];model={};
        for(auto& p:model.peak) p=255;
        float center=_centers[n]*tuningFactor;
        int fundamental=nearest(center,count);
        model.observed=fundamental>=0 && fabsf(cents(_peaks[fundamental].frequency,center))<(fabsf(offset)>3?25:45);
        float frequency=model.observed?_peaks[fundamental].frequency:center;
        if(!model.observed) {
            // Compare competing missing-fundamental hypotheses. Each seed is
            // supported by its complete harmonic fit, not by one loud partial.
            float best=0;
            for(unsigned seed=2;seed<=8;++seed) {
                int p=nearest(center*seed,count);
                if(p<0 || fabsf(cents(_peaks[p].frequency/seed,center))>(fabsf(offset)>3?25:45)) continue;
                float f=_peaks[p].frequency/seed,score=0;unsigned matched=0;
                for(unsigned h=2;h<=HARMONICS && f*h<=4000;++h) {
                    int j=nearest(f*h,count);if(j<0) continue;
                    float error=fabsf(_peaks[j].frequency-f*h),allowed=tolerance(f*h,_peaks[j].binHz,h);
                    if(error<allowed) { score+=_target[j]*(1-error/allowed)/sqrtf((float)h);++matched; }
                }
                if(matched>=3 && score>best) { best=score;frequency=f; }
            }
        }
        model.frequency=frequency;
        // Fit limited string inharmonicity. High guitar/piano partials can be
        // stretched; assigning them new note names is not a timbre model.
        unsigned stretchChoice=0;
        if(!clean) {
            float best=-1;
            for(unsigned choice=0;choice<5;++choice) {
                float score=0;
                for(unsigned h=1;h<=HARMONICS && frequency*h<=4000;++h) {
                    float expected=frequency*h*_stretchFactors[choice][h-1];
                    int p=nearest(expected,count);if(p<0) continue;
                    float allowed=tolerance(expected,_peaks[p].binHz,h);
                    float fit=std::max(0.0f,1-fabsf(_peaks[p].frequency-expected)/allowed);
                    score+=(.5f+.5f*_target[p])*fit*_harmonics[h-1];
                }
                if(score>best) { best=score;model.stretch=STRETCH[choice];stretchChoice=choice; }
            }
        }
        float fundamentalLevel=model.observed?_target[fundamental]:.6f;
        for(unsigned h=1;h<=HARMONICS && frequency*h<=4000;++h) {
            float weight=_harmonics[h-1];
            float expected=frequency*h*_stretchFactors[stretchChoice][h-1];
            int p=nearest(expected,count);float kernel=0;
            if(p>=0) {
                float error=fabsf(_peaks[p].frequency-expected);
                float allowed=tolerance(expected,_peaks[p].binHz,h);
                kernel=std::max(0.0f,1-error/allowed);
                if(h==1 && model.observed) kernel=1;
            }
            if(kernel>0) {
                // Instrument/microphone responses can favor high partials.
                // Fit a bounded spectral envelope, rather than insisting every
                // note follows a 1/h roll-off and inventing new high notes.
                float measured=std::min(4.0f,_target[p]/std::max(.1f,fundamentalLevel));
                float adaptation=_profile==GUITAR?.8f:.55f;
                weight=weight*(1-adaptation)+adaptation*measured;
                model.peak[h-1]=(uint8_t)p;model.weight[h-1]=weight*kernel;
                model.quality[h-1]=(uint8_t)(kernel*255);
                model.norm+=weight*weight*kernel*kernel;++model.matches;
                if(kernel>=.55f) ++model.strongMatches;
            } else model.norm+=weight*weight*(h==1?.35f:.08f);
        }
        if(model.observed && model.strongMatches<3) for(unsigned h=1;h<HARMONICS;++h) {
            unsigned number=h+1;
            if((number&(number-1))==0 || model.peak[h]==255) continue;
            // One coincident odd partial is insufficient to consume another
            // independently observed note. Octaves do not add a pitch class.
            model.norm-=model.weight[h]*model.weight[h];
            model.norm+=.08f*_harmonics[h]*_harmonics[h];
            --model.matches;if(model.quality[h]>=140) --model.strongMatches;
            model.peak[h]=255;model.weight[h]=0;model.quality[h]=0;
        }
        if(!model.observed) {
            unsigned lowest=HARMONICS+1,divisor=0;
            for(unsigned h=0;h<HARMONICS;++h) if(model.peak[h]!=255) {
                lowest=std::min(lowest,h+1);divisor=gcd(divisor,h+1);
            }
            // Several chord tones can share a very low mathematical divisor.
            // That is not evidence of an unplayed, inaudible subharmonic.
            bool lowPair=(model.peak[1]!=255 && model.peak[2]!=255) ||
                         (model.peak[2]!=255 && model.peak[3]!=255);
            bool upperFamily=model.strongMatches>=5 && lowest<=5;
            if(divisor!=1 || (!lowPair && !upperFamily) || (lowest>3 && !upperFamily)) model.matches=0;
        }
    }
}
void NoteEstimator::solve(unsigned count) {
    for(unsigned p=0;p<count;++p) _residual[p]=_target[p];
    // Coordinate descent compares all fundamental hypotheses against the same
    // residual. Refitting can withdraw an earlier explanation of a peak.
    for(unsigned pass=0;pass<8;++pass) for(unsigned n=0;n<CANDIDATES;++n) {
        auto& model=_hypotheses[n];
        if(model.matches<(model.observed?1u:3u) || model.norm<=0) continue;
        float correlation=0;
        for(unsigned h=0;h<HARMONICS;++h) if(model.peak[h]!=255)
            correlation+=model.weight[h]*_residual[model.peak[h]];
        bool isolated=model.observed && model.matches==1 && count>=10 && _target[model.peak[0]]<(model.frequency<250?.5f:.2f);
        float penalty=model.observed?(isolated?.08f:.035f):.12f;
        float next=std::max(0.0f,model.coefficient+(correlation-penalty)/model.norm);
        float change=next-model.coefficient;model.coefficient=next;
        for(unsigned h=0;h<HARMONICS;++h) if(model.peak[h]!=255)
            _residual[model.peak[h]]-=change*model.weight[h];
    }
}
void NoteEstimator::estimate(unsigned count,float offset,float* raw) {
    build(count,offset);
    // A single coherent harmonic series (including a missing fundamental) must
    // not become a collection of chord tones simply by renaming its partials.
    int single=-1;float singleCoverage=0,totalPower=0;
    for(unsigned p=0;p<count;++p) totalPower+=_target[p]*_target[p];
    for(unsigned n=0;n<CANDIDATES;++n) {
        const auto& model=_hypotheses[n];if(model.matches<3) continue;
        float explained=0;unsigned supported=0,divisor=0;
        for(unsigned h=0;h<HARMONICS;++h) if(model.peak[h]!=255) {
            if(model.quality[h]<140) continue;
            unsigned p=model.peak[h];explained+=_target[p]*_target[p];++supported;divisor=gcd(divisor,h+1);
        }
        float coverage=explained/std::max(1e-12f,totalPower);
        if(supported>=3 && divisor==1 && coverage>.97f && coverage>singleCoverage) { single=(int)n;singleCoverage=coverage; }
    }
    if(single<0 && count>=10) for(unsigned n=0;n<CANDIDATES;++n) {
        auto& model=_hypotheses[n];
        if(!model.observed || model.quality[1]>=80 || model.quality[2]>=80) continue;
        unsigned upperDivisor=0,upperMatches=0;
        for(unsigned h=1;h<HARMONICS;++h) if(model.peak[h]!=255 && model.quality[h]>=140) {
            upperDivisor=gcd(upperDivisor,h+1);++upperMatches;
        }
        // A repeated upper family (e.g. partials 5,10,15) corroborates a
        // bright voice even when its second/third partials are absent.
        if(upperMatches>=2 && upperDivisor>1 && _target[model.peak[0]]>=.3f) continue;
        // Sparse high-order coincidences with other voices do not establish a
        // harmonic family. Retain the observed tone, without borrowing their
        // energy or claiming them as its overtones.
        model.norm=model.weight[0]*model.weight[0];model.matches=model.strongMatches=1;
        for(unsigned h=1;h<HARMONICS;++h) {
            model.peak[h]=255;model.weight[h]=0;model.quality[h]=0;
            if(model.frequency*(h+1)<=4000) model.norm+=.08f*_harmonics[h]*_harmonics[h];
        }
    }
    unsigned observedOwners[MAX_PEAKS]={};
    if(single<0) for(unsigned n=0;n<CANDIDATES;++n) {
        const auto& voice=_hypotheses[n];
        if(!voice.observed || voice.strongMatches<3 || (voice.quality[1]<140 && voice.quality[2]<140)) continue;
        for(unsigned h=0;h<HARMONICS;++h) if(voice.peak[h]!=255 && voice.quality[h]>=140)
            observedOwners[voice.peak[h]]|=1u<<((FIRST_MIDI+n)%12);
    }
    if(single<0) for(unsigned n=0;n<CANDIDATES;++n) {
        auto& model=_hypotheses[n];if(model.observed || model.matches<3) continue;
        bool observedPair=true,octaveEvidence=false;unsigned classes=0;
        for(unsigned h=1;h<=2;++h) {
            unsigned p=model.peak[h];bool observed=false;
            if(p!=255 && model.quality[h]>=140 && _target[p]>=.3f) for(unsigned other=0;other<CANDIDATES;++other) {
                const auto& voice=_hypotheses[other];
                if(voice.observed && voice.peak[0]==p) {
                    observed=true;classes|=1u<<((FIRST_MIDI+other)%12);
                    if(voice.quality[1]>=140) octaveEvidence=true;
                }
            }
            observedPair&=observed;
        }
        // A common divisor must compete with the actual low-order voices.
        // Distinct observed second/third partials with their own octave family
        // favor those voices. A single coherent series was handled above.
        float explained=0,total=0;unsigned owners=0;
        for(unsigned h=0;h<HARMONICS;++h) if(model.peak[h]!=255 && model.quality[h]>=140) {
            unsigned p=model.peak[h],evidence=observedOwners[p];float power=_target[p]*_target[p];
            total+=power;if(evidence) explained+=power;owners|=evidence;
        }
        bool richVoices=(owners&(owners-1)) && explained>.9f*total;
        if((observedPair && octaveEvidence && (classes&(classes-1))) || richVoices) model.matches=0;
    }
    solve(count);
    bool ownedPeaks[MAX_PEAKS]={},ownedByObserved[MAX_PEAKS]={};
    for(unsigned n=0;n<CANDIDATES;++n) {
        const auto& model=_hypotheses[n];float value=model.coefficient;
        if(model.observed && model.matches>=3 && model.peak[0]!=255 && _target[model.peak[0]]>=.3f)
            value+=.75f*std::max(0.0f,_residual[model.peak[0]]);
        if(single<0 && model.observed && model.strongMatches>=3 && model.peak[0]!=255 &&
           _target[model.peak[0]]>=.3f && (model.quality[1]>=140 || model.quality[2]>=140))
            value=std::max(value,.35f*_target[model.peak[0]]);
        if(model.observed && model.matches==1 && count>=10 && _target[model.peak[0]]<(model.frequency<250?.5f:.2f)) value*=.4f;
        if(single>=0) value=(int)n==single?1:0;
        else if(value>0) {
            // A proposed note needs evidence not wholly owned by a credible
            // lower family. Preserve real voices with independent partials.
            bool anchorFree=false;float independentPower=0,allPower=0;
            for(unsigned h=0;h<HARMONICS;++h) if(model.peak[h]!=255 && model.quality[h]>=140) {
                unsigned p=model.peak[h];float power=_target[p]*_target[p];allPower+=power;
                if(!ownedPeaks[p]) {
                    independentPower+=power;
                    if(h==0 || (!model.observed && h<=2)) anchorFree=true;
                }
            }
            // An inferred common divisor is weaker evidence than an observed
            // fundamental with its own low-order family. An actual observed
            // parent can still explain away one of its upper partials.
            if(model.observed && model.strongMatches>=3 && (model.quality[1]>=140 || model.quality[2]>=140) &&
               !ownedByObserved[model.peak[0]]) anchorFree=true;
            float independentFraction=independentPower/std::max(1e-12f,allPower);
            if(!anchorFree && independentFraction<.25f) value=0;
            else if(!anchorFree) value*=sqrtf(independentFraction);
        }
        // The coefficient is relative to the fundamental's level. Compare
        // explained voice energy instead, so a quiet fundamental with loud
        // upper partials is not scored as a quiet or missing musical note.
        if(single<0) value*=sqrtf(model.norm);
        raw[n]=value;
        bool credibleParent=model.observed && model.peak[0]!=255 ?
            (_target[model.peak[0]]>=.3f || (_target[model.peak[0]]>=.1f && model.strongMatches>=4 &&
             (model.quality[1]>=80 || model.quality[2]>=80))) : value>.2f;
        if(value>.06f && credibleParent && model.matches>=3) for(unsigned h=1;h<HARMONICS;++h)
            if(model.peak[h]!=255 && model.quality[h]>=80) {
                ownedPeaks[model.peak[h]]=true;
                if(model.observed) ownedByObserved[model.peak[h]]=true;
            }
    }
    _singleFundamental=single;
}
const NoteEstimator::Frame& NoteEstimator::process(const Peak* fast,unsigned fastCount,const Peak* bass,unsigned bassCount,
                                                 uint32_t now,float bassWindowMs,float rmsDb) {
    _frame={};
    float fastChroma[12]={},fastTotal=0;
    for(unsigned p=0;p<fastCount;++p) if(fast[p].frequency>=60 && fast[p].amplitude>0) {
        int midi=(int)lroundf(69+12*log2f(fast[p].frequency/_reference));
        fastChroma[(midi%12+12)%12]+=fast[p].amplitude;fastTotal+=fast[p].amplitude;
    }
    float flux=0;
    if(fastTotal>0) for(int pc=0;pc<12;++pc) { fastChroma[pc]/=fastTotal;flux+=std::max(0.0f,fastChroma[pc]-_previousFast[pc]); }
    bool transient=_history && rmsDb>-75 && (rmsDb>_previousDb+7 || flux>.38f);
    bool useBass=bassCount && !transient && (!_history || now-_onsetTime>=(uint32_t)bassWindowMs);
    _frame.bassUsed=useBass;
    unsigned count=0;
    for(unsigned p=0;p<fastCount && count<MAX_PEAKS;++p) if(!useBass || fast[p].frequency>=500) _peaks[count++]=fast[p];
    if(useBass) for(unsigned p=0;p<bassCount && count<MAX_PEAKS;++p) if(bass[p].frequency<500) _peaks[count++]=bass[p];
    std::sort(_peaks,_peaks+count,[](const Peak& a,const Peak& b){return a.frequency<b.frequency;});
    float maximum=0;for(unsigned p=0;p<count;++p) maximum=std::max(maximum,_peaks[p].amplitude);
    for(unsigned p=0;p<count;++p) _target[p]=sqrtf(_peaks[p].amplitude/std::max(1e-12f,maximum));
    float raw[CANDIDATES]={},maximumNote=0;
    estimate(count,0,raw);
    for(float value:raw) maximumNote=std::max(maximumNote,value);
    // Derive a shared offset from the independently supported fundamentals.
    float sumSin=0,sumCos=0,weight=0;unsigned classes=0;
    for(unsigned n=0;n<CANDIDATES;++n) if(raw[n]>maximumNote*.18f && raw[n]>.06f) {
        float center=_centers[n];
        float deviation=cents(_hypotheses[n].frequency,center);
        if(fabsf(deviation)>45) continue;
        sumSin+=raw[n]*sinf(deviation*2*PI/100);sumCos+=raw[n]*cosf(deviation*2*PI/100);weight+=raw[n];
        classes|=1u<<((FIRST_MIDI+n)%12);
    }
    unsigned classCount=0;for(unsigned bits=classes;bits;bits>>=1) classCount+=bits&1u;
    if(classCount>=3 && weight>0 && hypotf(sumSin,sumCos)>.9f*weight)
        _frame.tuningCents=atan2f(sumSin,sumCos)*100/(2*PI);
    if(fabsf(_frame.tuningCents)>3) {
        estimate(count,_frame.tuningCents,raw);maximumNote=0;
        for(float value:raw) maximumNote=std::max(maximumNote,value);
    }
    float musical[12]={},musicalTotal=0,musicalFlux=0;
    for(unsigned n=0;n<CANDIDATES;++n) musical[(FIRST_MIDI+n)%12]=std::max(musical[(FIRST_MIDI+n)%12],raw[n]);
    for(float x:musical) musicalTotal+=x;
    for(int pc=0;pc<12;++pc) { if(musicalTotal>0) musical[pc]/=musicalTotal;musicalFlux+=std::max(0.0f,musical[pc]-_previousNotes[pc]); }
    bool onset=_history && rmsDb>-75 && maximumNote>.15f && (rmsDb>_previousDb+7 || musicalFlux>.35f);
    if(onset) { _onsetTime=now;_haveOnset=true; }
    float dt=_history?(float)(now-_lastTime):100;dt=std::max(1.0f,std::min(500.0f,dt));
    for(unsigned n=0;n<CANDIDATES;++n) {
        float value=raw[n];
        if(rmsDb<-85) value=0;
        float tau=value>_tracked[n]?40.0f:(_profile==GUITAR?300.0f:90.0f);
        if(maximumNote<.06f || rmsDb<-85) tau=55;
        float alpha=1-expf(-dt/tau);
        _tracked[n]=(!_history || onset)?value:_tracked[n]+alpha*(value-_tracked[n]);
    }
    unsigned order[CANDIDATES];for(unsigned n=0;n<CANDIDATES;++n) order[n]=n;
    std::sort(order,order+CANDIDATES,[&](unsigned a,unsigned b){return _tracked[a]>_tracked[b];});
    float top=_tracked[order[0]];
    unsigned limit=_profile==GUITAR?6:MAX_NOTES;
    for(unsigned index=0;index<CANDIDATES && _frame.count<limit && rmsDb>-85;++index) {
        unsigned n=order[index];if(_tracked[n]<std::max(.065f,top*.18f)) continue;
        auto& note=_frame.notes[_frame.count++];const auto& model=_hypotheses[n];
        note.midi=FIRST_MIDI+n;note.frequency=model.frequency;note.strength=_tracked[n]/std::max(.001f,top);
        // Evidence score, not a probability: several coherent partials
        // corroborate a quiet voice more than an isolated observed peak.
        note.confidence=count<=8?1.0f:model.strongMatches>=3?.9f:model.strongMatches==2?.65f:.45f;
        note.observed=model.observed && raw[n]>.06f;
        note.current=raw[n]>.06f;
        _frame.chroma[note.midi%12]=std::max(_frame.chroma[note.midi%12],note.strength);
        _frame.currentChroma[note.midi%12]=std::max(_frame.currentChroma[note.midi%12],raw[n]/std::max(.001f,top));
        // Bass is a separately supported current fundamental, never the lowest
        // raw spectral peak or an old decaying temporal hypothesis.
        bool bassEvidence=model.observed && model.peak[0]!=255 ?
            (_target[model.peak[0]]>=.25f || (_target[model.peak[0]]>=.1f && model.strongMatches>=4 &&
              (model.quality[1]>=80 || model.quality[2]>=80))) : (int)n==_singleFundamental;
        if(raw[n]>std::max(.08f,maximumNote*.18f) && bassEvidence &&
           (_frame.bassMidi<0 || note.midi<_frame.bassMidi)) _frame.bassMidi=note.midi;
    }
    _frame.onset=onset;
    _frame.onsetAgeMs=_haveOnset?now-_onsetTime:UINT32_MAX;
    for(int pc=0;pc<12;++pc) _previousFast[pc]=fastChroma[pc];
    for(int pc=0;pc<12;++pc) _previousNotes[pc]=musical[pc];
    _previousDb=rmsDb;_lastTime=now;_history=true;
    return _frame;
}

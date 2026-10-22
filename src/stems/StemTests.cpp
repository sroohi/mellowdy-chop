#include "StemSeparator.h"
#include "SHA256.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numbers>

using namespace samplify;
namespace {
int checked=0,failed=0;
void check(bool value,const char* message) {
    ++checked;if(!value){++failed;std::cerr<<"FAIL: "<<message<<'\n';}
}
}
int main(int argc,char** argv) {
    try {
        detail::SHA256 hash;
        const std::string abc="abc";
        hash.update(reinterpret_cast<const std::uint8_t*>(abc.data()),abc.size());
        check(hash.finish()=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA-256 standard known-answer vector");
        if(argc!=2)throw std::runtime_error("Pass the verified native stem model path");
        check(StemSeparator::modelFileSha256(argv[1])==StemSeparator::expectedModelSha256(),"Official converted model matches pinned SHA-256");
        StemSeparator separator(argv[1]);
        std::atomic<bool> cancelled{true};
        bool rejected=false;
        try{separator.separate({},cancelled);}catch(const SeparationCancelled&){rejected=true;}
        check(rejected,"Cancellation is honored before inference");
        cancelled.store(false);
        AudioData constant;
        constant.sampleRate=22050;constant.channels={std::vector<float>(128,.125f)};
        const auto stable=separator.separate(constant,cancelled);
        check(stable[1].channels==constant.channels&&stable[0].channels[0][0]==0,"Constant source normalization is finite and mixture-preserving");

        AudioData input;input.sampleRate=22050;
        input.channels.assign(2,std::vector<float>(5512));
        std::uint32_t random=71;
        for(std::size_t i=0;i<input.frames();++i) {
            const double t=static_cast<double>(i)/input.sampleRate;
            random=random*1664525u+1013904223u;
            const double noise=static_cast<double>(random>>8u)/8388608.0-1;
            const auto sample=static_cast<float>(.22*std::sin(2*std::numbers::pi*220*t)
                +.15*std::sin(2*std::numbers::pi*110*t)*std::exp(-t*8)+.05*noise*std::exp(-t*30));
            input.channels[0][i]=sample;input.channels[1][i]=sample*.87f;
        }
        int callbacks=0;float lastProgress=0;bool monotonic=true;
        const auto start=std::chrono::steady_clock::now();
        const auto stems=separator.separate(input,cancelled,[&](float progress,const std::string&){
            ++callbacks;monotonic=monotonic&&progress>=lastProgress;lastProgress=progress;
        });
        check(callbacks>10&&lastProgress>=1,"Actual neural inference reports layer progress and completion");
        check(monotonic,"Stem progress is monotonic");
        double maxError=0,difference=0;
        for(std::size_t s=0;s<4;++s) {
            check(stems[s].valid()&&stems[s].sampleRate==input.sampleRate
                &&stems[s].frames()==input.frames()&&stems[s].channels.size()==2,"Neural stem output finite and aligned at original rate/layout");
            double energy=0;
            for(float sample:stems[s].channels[0])energy+=sample*sample;
            check(energy>1e-10,"Each neural model source contains nonzero predicted audio");
        }
        for(std::size_t c=0;c<2;++c)for(std::size_t i=0;i<input.frames();++i) {
            double sum=0;for(const auto& s:stems)sum+=s.channels[c][i];
            maxError=std::max(maxError,std::abs(sum-input.channels[c][i]));
            difference+=std::abs(stems[0].channels[c][i]-stems[3].channels[c][i]);
        }
        check(maxError<1e-5,"Four model stems reconstruct the original mixture");
        check(difference>.01,"Neural sources are distinct predictions");
        std::cout<<"Native model inference seconds: "
            <<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<'\n';
        bool interrupted=false;
        try {
            separator.separate(input,cancelled,[&](float progress,const std::string&){
                if(progress>.03f)cancelled.store(true);
            });
        } catch(const SeparationCancelled&) { interrupted=true; }
        check(interrupted,"Cancellation interrupts a running neural inference at a layer boundary");
    }catch(const std::exception& e){++failed;std::cerr<<"FAIL: "<<e.what()<<'\n';}
    std::cout<<checked<<" stem checks, "<<failed<<" failures\n";
    return failed?1:0;
}

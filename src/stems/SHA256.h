#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace samplify::detail {
// Streaming SHA-256 for validating the bundled model before its native parser
// is invoked. This avoids adding a crypto dylib or platform command dependency.
class SHA256 {
    std::array<std::uint32_t, 8> state { 0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };
    std::array<std::uint8_t, 64> pending {};
    std::uint64_t bytes = 0;
    std::size_t used = 0;
    static std::uint32_t rotate(std::uint32_t n, unsigned bits) { return (n >> bits) | (n << (32u - bits)); }
    void block(const std::uint8_t* data) {
        static constexpr std::array<std::uint32_t, 64> constants {
            0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
            0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
            0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
            0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
            0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
            0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
            0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
            0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
        };
        std::array<std::uint32_t,64> w {};
        for (std::size_t i=0;i<16;++i)
            w[i]=(std::uint32_t(data[4*i])<<24u)|(std::uint32_t(data[4*i+1])<<16u)
                |(std::uint32_t(data[4*i+2])<<8u)|std::uint32_t(data[4*i+3]);
        for (std::size_t i=16;i<w.size();++i) {
            auto x=w[i-15],y=w[i-2];
            w[i]=w[i-16]+(rotate(x,7)^rotate(x,18)^(x>>3u))+w[i-7]+(rotate(y,17)^rotate(y,19)^(y>>10u));
        }
        auto a=state[0],b=state[1],c=state[2],d=state[3],e=state[4],f=state[5],g=state[6],h=state[7];
        for (std::size_t i=0;i<w.size();++i) {
            const auto t1=h+(rotate(e,6)^rotate(e,11)^rotate(e,25))+((e&f)^((~e)&g))+constants[i]+w[i];
            const auto t2=(rotate(a,2)^rotate(a,13)^rotate(a,22))+((a&b)^(a&c)^(b&c));
            h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
        }
        state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d;state[4]+=e;state[5]+=f;state[6]+=g;state[7]+=h;
    }
public:
    void update(const std::uint8_t* data, std::size_t count) {
        bytes+=count;
        while (count>0) {
            pending[used++]=*data++;--count;
            if (used==pending.size()) { block(pending.data());used=0; }
        }
    }
    std::string finish() {
        const auto bits=bytes*8;
        pending[used++]=0x80;
        if (used>56) { while(used<64)pending[used++]=0;block(pending.data());used=0; }
        while(used<56)pending[used++]=0;
        for(unsigned i=0;i<8;++i)pending[56+i]=static_cast<std::uint8_t>(bits>>(56u-8u*i));
        block(pending.data());
        std::ostringstream output;output<<std::hex<<std::setfill('0');
        for(auto n:state)output<<std::setw(8)<<n;
        return output.str();
    }
};

inline std::string sha256File(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);
    if(!input)throw std::runtime_error("Stem model could not be opened: "+path.string());
    SHA256 hash;
    std::array<std::uint8_t,65536> buffer {};
    while(input) {
        input.read(reinterpret_cast<char*>(buffer.data()),static_cast<std::streamsize>(buffer.size()));
        hash.update(buffer.data(),static_cast<std::size_t>(input.gcount()));
    }
    if(!input.eof())throw std::runtime_error("Stem model could not be read completely");
    return hash.finish();
}
} // namespace samplify::detail

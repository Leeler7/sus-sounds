// Wav.h -- minimal WAV writer (16-bit stereo) + reader (16/24/32-bit PCM, float32,
// mono/stereo -> mono float). Test/demo only.
#pragma once
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>

inline void writeWavStereo(const std::string& path,
                           const std::vector<float>& L,
                           const std::vector<float>& R,
                           double sampleRate) {
    int n = (int)(L.size() < R.size() ? L.size() : R.size());
    uint32_t sr = (uint32_t)sampleRate;
    uint16_t ch = 2, bits = 16;
    uint32_t byteRate = sr * ch * (bits / 8);
    uint16_t blockAlign = ch * (bits / 8);
    uint32_t dataBytes = (uint32_t)n * blockAlign;

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };

    std::fwrite("RIFF", 1, 4, f); u32(36 + dataBytes); std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f); u32(16); u16(1); u16(ch); u32(sr); u32(byteRate); u16(blockAlign); u16(bits);
    std::fwrite("data", 1, 4, f); u32(dataBytes);

    for (int i = 0; i < n; ++i) {
        float l = L[i] < -1.f ? -1.f : (L[i] > 1.f ? 1.f : L[i]);
        float r = R[i] < -1.f ? -1.f : (R[i] > 1.f ? 1.f : R[i]);
        int16_t li = (int16_t)(l * 32767.0f);
        int16_t ri = (int16_t)(r * 32767.0f);
        std::fwrite(&li, 2, 1, f);
        std::fwrite(&ri, 2, 1, f);
    }
    std::fclose(f);
}

// Read a WAV into a mono float buffer (channels averaged). Returns empty on failure.
// Handles PCM 16/24/32-bit and IEEE float32; sets sampleRateOut.
inline std::vector<float> readWavMono(const std::string& path, double& sampleRateOut) {
    std::vector<float> mono;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return mono;
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz < 44) { std::fclose(f); return mono; }
    std::vector<uint8_t> b((size_t)sz);
    if (std::fread(b.data(), 1, (size_t)sz, f) != (size_t)sz) { std::fclose(f); return mono; }
    std::fclose(f);

    auto rd16 = [&](size_t o) { return (uint16_t)(b[o] | (b[o + 1] << 8)); };
    auto rd32 = [&](size_t o) { return (uint32_t)(b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)); };
    if (std::memcmp(&b[0], "RIFF", 4) != 0 || std::memcmp(&b[8], "WAVE", 4) != 0) return mono;

    uint16_t fmt = 1, ch = 2, bits = 16;
    uint32_t rate = 44100, dataOff = 0, dataLen = 0;
    size_t pos = 12;
    while (pos + 8 <= (size_t)sz) {
        uint32_t csz = rd32(pos + 4);
        size_t body = pos + 8;
        if (std::memcmp(&b[pos], "fmt ", 4) == 0) {
            fmt = rd16(body); ch = rd16(body + 2); rate = rd32(body + 4); bits = rd16(body + 14);
        } else if (std::memcmp(&b[pos], "data", 4) == 0) {
            dataOff = (uint32_t)body; dataLen = csz;
        }
        pos = body + csz + (csz & 1);
    }
    if (dataOff == 0 || ch == 0) return mono;

    sampleRateOut = rate;
    int bytes = bits / 8;
    size_t frameBytes = (size_t)bytes * ch;
    if (frameBytes == 0) return mono;
    size_t nframes = dataLen / frameBytes;
    mono.resize(nframes);
    for (size_t i = 0; i < nframes; ++i) {
        double acc = 0.0;
        for (int c = 0; c < ch; ++c) {
            size_t o = dataOff + i * frameBytes + (size_t)c * bytes;
            float s = 0.0f;
            if (fmt == 3 && bits == 32) { uint32_t u = rd32(o); float fv; std::memcpy(&fv, &u, 4); s = fv; }
            else if (bits == 16) { s = (int16_t)rd16(o) / 32768.0f; }
            else if (bits == 24) { int32_t v = b[o] | (b[o + 1] << 8) | (b[o + 2] << 16); if (v & 0x800000) v |= (int32_t)0xFF000000; s = v / 8388608.0f; }
            else if (bits == 32) { s = (int32_t)rd32(o) / 2147483648.0f; }
            acc += s;
        }
        mono[i] = (float)(acc / ch);
    }
    return mono;
}

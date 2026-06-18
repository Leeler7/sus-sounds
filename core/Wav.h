// Wav.h -- minimal 16-bit PCM stereo WAV writer (test/demo only).
#pragma once
#include <cstdio>
#include <cstdint>
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

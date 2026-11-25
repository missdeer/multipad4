#include "SimdText.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

int main() {
    std::string text;
    const std::string line = "ordinary open analysis\r\nnext\nlast\r";
    while (text.size() < 64 * 1024 * 1024) text += line;
    const auto staged = simd::AnalyzeLineEndings(text);
    volatile size_t checksum = 0;
    for (const bool baseline : {true, false}) {
        std::vector<double> samples;
        for (int run = 0; run < 21; ++run) {
            const auto start = std::chrono::steady_clock::now();
            volatile bool ascii = baseline ? simd::IsAscii(text) : false;
            const auto endings = baseline ? simd::AnalyzeLineEndings(text) : staged;
            if (endings.linesCount[0] != staged.linesCount[0] || endings.linesCount[1] != staged.linesCount[1] || endings.linesCount[2] != staged.linesCount[2])
                throw std::runtime_error("Histogram changed");
            checksum = endings.linesCount[0] + endings.linesCount[1] + endings.linesCount[2];
            (void)ascii;
            samples.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        }
        std::sort(samples.begin(), samples.end());
        std::printf("%s bytes=%zu median_ms=%.6f p95_ms=%.6f max_ms=%.6f repeated_scan_bytes=%zu checksum=%zu\n",
            baseline ? "before" : "after", text.size(), samples[10], samples[19], samples[20], baseline ? text.size() * 2 : 0, static_cast<size_t>(checksum));
    }
}

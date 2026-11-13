#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <stdexcept>
#include <memory>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <string>
#include "ScintillaTypes.h"
#include "ILexer.h"
#include "Debugging.h"
#include "VectorISA.h"
#include "Position.h"
#include "SplitVector.h"
#include "CellBuffer.h"

// Exercise line scanning/indexing directly, independently of the ATL host.
int main() {
    bool passed = true;
    for (bool large : {false, true}) {
        for (const char* ending : {"\n", "\r\n", "\r"}) {
            for (size_t repeat = 1; repeat <= 512; ++repeat) {
                std::string text;
                const std::string line = std::string("alpha ") + "\xE4\xB8\xAD" + ending;
                for (size_t index = 0; index < repeat; ++index) text += line;
                text += "last";
                Scintilla::Internal::CellBuffer buffer(false, large);
                bool sequence = false;
                buffer.InsertString(0, text.data(), text.size(), sequence);
                const auto count = buffer.Lines();
                const auto last = buffer.LineStart(repeat);
                if (count != static_cast<Sci::Position>(repeat + 1) ||
                    last != static_cast<Sci::Position>(repeat * line.size())) {
                    std::fprintf(stderr, "Line index: large=%d EOL=%u repeat=%zu lines=%lld expected=%zu last=%lld expected=%zu\n",
                        large, static_cast<unsigned char>(*ending), repeat, static_cast<long long>(count), repeat + 1,
                        static_cast<long long>(last), repeat * line.size());
                    passed = false;
                    break;
                }
            }
        }
    }
    return passed ? 0 : 1;
}

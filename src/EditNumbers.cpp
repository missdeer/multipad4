// Integer conversion from notepad4 Edit.cpp; see THIRD_PARTY.md.
#include "EditNumbers.h"
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
namespace {
int HexDigit(char character) {
    if (character >= '0' && character <= '9')
        return character - '0';
    if (character >= 'a' && character <= 'f')
        return character - 'a' + 10;
    if (character >= 'A' && character <= 'F')
        return character - 'A' + 10;
    return -1;
}
static int ConvertNumRadix(char *tch, uint64_t num, int radix) noexcept {
    switch (radix) {
    case 16:
        return sprintf(tch,
                       "0x%"
                       "llx",
                       num);

    case 10:
        return sprintf(tch,
                       "%"
                       "llu",
                       num);

    case 8: {
        char buf[32]{};
        unsigned index = static_cast<unsigned>(sizeof(buf)) - 2;
        unsigned length = 0;
        while (num) {
            const int bit = static_cast<int>(num & 7);
            num >>= 3;
            buf[index--] = static_cast<char>('0' + bit);
            ++length;
        }
        if (length == 0) {
            buf[index--] = '0';
            ++length;
        }
        buf[index--] = 'O';
        buf[index] = '0';
        length += 2;
        strcat(tch, buf + index);
        return length;
    } break;

    case 2: {
        char buf[80]{};
        unsigned index = static_cast<unsigned>(sizeof(buf)) - 2;
        unsigned length = 0;
        unsigned bit_count = 0;
        while (num) {
            const int bit = static_cast<int>(num & 1);
            num >>= 1;
            buf[index--] = static_cast<char>('0' + bit);
            ++bit_count;
            ++length;
            if (num && (bit_count & 7) == 0) {
                buf[index--] = '_';
                ++length;
            }
        }
        if (length == 0) {
            buf[index--] = '0';
            ++length;
        }
        buf[index--] = 'b';
        buf[index] = '0';
        length += 2;
        strcat(tch, buf + index);
        return length;
    } break;
    }
    return 0;
}

} // namespace
std::string ConvertNumbers(const std::string &input, int radix) {
    std::vector<char> buffer(input.size() * 6 + 32, 0);
    char *pszOut = buffer.data();
    const char *p = input.c_str();
    uint64_t value = 0;
    size_t cch = 0;
    while (*p) {
        if (*p == '0') {
            value = 0;
            p++;
            const char prefix = static_cast<char>(tolower(static_cast<unsigned char>(*p)));
            if (prefix == 'x' && radix != 16) {
                p++;
                while (*p) {
                    if (*p == '_') {
                        p++;
                    } else {
                        const int hex = HexDigit(*p);
                        if (hex < 0) {
                            break;
                        }
                        value = (value << 4) | hex;
                        p++;
                    }
                }
                cch += ConvertNumRadix(pszOut + cch, value, radix);
            } else if (prefix == 'o' && radix != 8) {
                p++;
                while (*p) {
                    if (*p >= '0' && *p <= '7') {
                        value <<= 3;
                        value += (*p++ - '0');
                    } else if (*p == '_') {
                        p++;
                    } else {
                        break;
                    }
                }
                cch += ConvertNumRadix(pszOut + cch, value, radix);
            } else if (prefix == 'b' && radix != 2) {
                p++;
                while (*p) {
                    if (*p == '0') {
                        value <<= 1;
                        p++;
                    } else if (*p == '1') {
                        value <<= 1;
                        value |= 1;
                        p++;
                    } else if (*p == '_') {
                        p++;
                    } else {
                        break;
                    }
                }
                cch += ConvertNumRadix(pszOut + cch, value, radix);
            } else if ((*p >= '0' && *p <= '9') && radix != 10) {
                value = *p++ - '0';
                while (*p) {
                    if (*p >= '0' && *p <= '9') {
                        value *= 10;
                        value += (*p++ - '0');
                    } else if (*p == '_') {
                        p++;
                    } else {
                        break;
                    }
                }
                cch += ConvertNumRadix(pszOut + cch, value, radix);
            } else {
                pszOut[cch++] = '0';
            }
        } else if ((*p >= '1' && *p <= '9') && radix != 10) {
            value = *p++ - '0';
            while (*p) {
                if (*p >= '0' && *p <= '9') {
                    value *= 10;
                    value += (*p++ - '0');
                } else if (*p == '_') {
                    p++;
                } else {
                    break;
                }
            }
            cch += ConvertNumRadix(pszOut + cch, value, radix);
        } else if (isalnum(static_cast<unsigned char>(*p)) || *p == '_') {
            // radix and number prefix matches, no conversion
            pszOut[cch++] = *p++;
            while (isalnum(static_cast<unsigned char>(*p)) || *p == '_') {
                pszOut[cch++] = *p++;
            }
        } else {
            pszOut[cch++] = *p++;
        }
    }
    return std::string(pszOut, cch);
}
std::string ModifyNumber(const std::string &input, bool increase) {
    if (input.empty() || input.size() >= 32)
        return input;
    const char *prefix = strpbrk(input.c_str(), "xX");
    int radix = prefix ? 16 : 10;
    char *end = nullptr;
    unsigned number = static_cast<unsigned>(strtoul(input.c_str(), &end, radix));
    if (end == input.c_str())
        return input;
    if (increase)
        ++number;
    else if (number)
        --number;
    int width = static_cast<int>(input.size()) - (prefix ? 2 : 0);
    char buffer[64]{};
    if (prefix)
        sprintf_s(buffer, *prefix == 'X' ? "%#0*X" : "%#0*x", width, number);
    else
        sprintf_s(buffer, "%0*u", width, number);
    return buffer;
}

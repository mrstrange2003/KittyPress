// lz77.cpp
#include "lz77.h"
#include <algorithm>
#include <cstring>

// naive LZ77: sliding window search, not optimized but simple and correct
std::vector<LZ77Token> lz77_compress(const std::vector<uint8_t> &data, size_t windowSize, size_t maxMatch) {
    std::vector<LZ77Token> tokens;
    size_t n = data.size();
    size_t i = 0;

    while (i < n) {
        size_t bestLen = 0;
        size_t bestOffset = 0;

        size_t start = (i > windowSize) ? (i - windowSize) : 0;
        for (size_t j = start; j < i; ++j) {
            size_t k = 0;
            while (k < maxMatch && i + k < n && data[j + k] == data[i + k]) ++k;
            if (k > bestLen) {
                bestLen = k;
                bestOffset = i - j;
            }
        }

        if (bestLen >= 3) {
            // Emit match token (offset, length)
            LZ77Token t;
            t.offset = static_cast<uint16_t>(bestOffset);
            t.length = static_cast<uint8_t>(bestLen);
            t.lit = 0; // unused
            tokens.push_back(t);
            i += bestLen;
        } else {
            // Emit literal token
            LZ77Token t;
            t.offset = 0;
            t.length = 0;
            t.lit = data[i];
            tokens.push_back(t);
            ++i;
        }
    }
    return tokens;
}

// Serialization format 
std::vector<uint8_t> lz77_serialize(const std::vector<LZ77Token> &tokens) {
    std::vector<uint8_t> out;
    out.reserve(tokens.size() * 3);
    for (const auto &t : tokens) {
        if (t.offset == 0 && t.length == 0) {
            out.push_back(0x00);
            out.push_back(t.lit);
        } else {
            out.push_back(0x01);
            out.push_back(static_cast<uint8_t>(t.offset & 0xFF));
            out.push_back(static_cast<uint8_t>((t.offset >> 8) & 0xFF));
            out.push_back(t.length);
        }
    }
    return out;
}

std::vector<LZ77Token> lz77_deserialize(const std::vector<uint8_t> &bytes) {
    std::vector<LZ77Token> tokens;
    size_t i = 0, n = bytes.size();
    while (i < n) {
        uint8_t tag = bytes[i++];
        if (tag == 0x00) {
            if (i >= n) break; // malformed, but break
            LZ77Token t; t.offset = 0; t.length = 0; t.lit = bytes[i++];
            tokens.push_back(t);
        } else if (tag == 0x01) {
            if (i + 2 >= n) break;
            uint16_t lo = bytes[i++];
            uint16_t hi = bytes[i++];
            uint16_t offset = (hi << 8) | lo;
            uint8_t length = bytes[i++];
            LZ77Token t; t.offset = offset; t.length = length; t.lit = 0;
            tokens.push_back(t);
        } else {
            // unknown tag — break for safety
            break;
        }
    }
    return tokens;
}

std::vector<uint8_t> lz77_decompress(const std::vector<LZ77Token> &tokens) {
    std::vector<uint8_t> out;
    for (const auto &t : tokens) {
        if (t.offset == 0 && t.length == 0) {
            out.push_back(t.lit);
        } else {
            size_t start = out.size() - t.offset;
            for (size_t k = 0; k < t.length; ++k) {
                out.push_back(out[start + k]);
            }
        }
    }
    return out;
}

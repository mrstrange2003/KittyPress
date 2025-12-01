// lz77.cpp
#include "lz77.h"
#include <algorithm>
#include <cstring>
#include <iostream>

// (serialize/deserialize/decompress)

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
            if (i >= n) break;
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
            break;
        }
    }
    return tokens;
}

std::vector<uint8_t> lz77_decompress(const std::vector<LZ77Token> &tokens) {
    std::vector<uint8_t> out;
    out.reserve(tokens.size() * 2);
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


// STREAMING COMPRESSOR IMPLEMENTATION ------------------------------------

inline uint32_t LZ77StreamCompressor::make_key(const uint8_t* p) {
    return (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | uint32_t(p[2]);
}

LZ77StreamCompressor::LZ77StreamCompressor(size_t w, size_t m)
        : windowSize(w), maxMatch(m), absolutePos(0) {
    dict.reserve(65536);
}

void LZ77StreamCompressor::feed(const std::vector<uint8_t>& chunk, bool isLast) {
    processChunk(chunk, isLast);
}

void LZ77StreamCompressor::processChunk(const std::vector<uint8_t>& chunk, bool /*isLast*/) {

    const size_t n = chunk.size();
    if (n == 0) return;

    const size_t MIN_MATCH = 3;
    const size_t KEY_LEN = 3;
    const size_t MAX_POS_PER_KEY = 64;

    size_t i = 0;

    while (i < n) {

        size_t bestLen = 0;
        size_t bestOffset = 0;

        if (i + KEY_LEN <= n && window.size() + (n - i) >= KEY_LEN) {
            uint32_t key = make_key(&chunk[i]);
            auto it = dict.find(key);

            if (it != dict.end()) {
                auto &dq = it->second;
                size_t tries = 0;
                const size_t MAX_TRIES = 32;

                for (auto rit = dq.rbegin(); rit != dq.rend() && tries < MAX_TRIES; ++rit, ++tries) {

                    size_t j = *rit;
                    size_t offset = absolutePos + i - j;
                    if (offset == 0 || offset > windowSize) continue;

                    size_t k = 0;
                    size_t limit = std::min(maxMatch, n - i);

                    while (k < limit) {

                        size_t posCandidate = j + k;
                        if (posCandidate >= absolutePos + window.size()) break;

                        uint8_t candidateByte = window[posCandidate - absolutePos];
                        uint8_t currentByte   = chunk[i + k];

                        if (candidateByte != currentByte) break;

                        ++k;
                    }

                    if (k > bestLen) {
                        bestLen = k;
                        bestOffset = offset;
                    }
                }
            }
        }


        // >>>>>>>>>>> LAZY MATCHING ADDED HERE <<<<<<<<<<

        if (bestLen >= MIN_MATCH) {

            size_t nextLen = 0;
            size_t nextOffset = 0;

            if (i + 1 < n && i + 1 + KEY_LEN <= n) {

                uint32_t key2 = make_key(&chunk[i + 1]);
                auto it2 = dict.find(key2);

                if (it2 != dict.end()) {
                    auto &dq2 = it2->second;
                    size_t tries = 0;
                    const size_t MAX_TRIES = 32;

                    for (auto rit = dq2.rbegin(); rit != dq2.rend() && tries < MAX_TRIES; ++rit, ++tries) {

                        size_t j2 = *rit;
                        size_t offset2 = absolutePos + (i + 1) - j2;
                        if (offset2 == 0 || offset2 > windowSize) continue;

                        size_t k2 = 0;
                        size_t limit2 = std::min(maxMatch, n - (i + 1));

                        while (k2 < limit2) {
                            size_t posCandidate = j2 + k2;
                            if (posCandidate >= absolutePos + window.size()) break;

                            uint8_t candidateByte = window[posCandidate - absolutePos];
                            uint8_t currentByte   = chunk[i + 1 + k2];

                            if (candidateByte != currentByte) break;
                            ++k2;
                        }

                        if (k2 > nextLen) {
                            nextLen = k2;
                            nextOffset = offset2;
                        }
                    }
                }

                // Lazy decision:
                if (nextLen > bestLen + 1) {
                    // Output literal, move one step
                    LZ77Token t{0,0,chunk[i]};
                    pendingTokens.push_back(t);

                    if (i + KEY_LEN <= n) {
                        uint32_t k = make_key(&chunk[i]);
                        auto &dq = dict[k];
                        dq.push_back(absolutePos + i);
                        if (dq.size() > MAX_POS_PER_KEY) dq.pop_front();
                    }

                    ++i;
                    continue;
                }
            }

            // Greedy case (use current match)
            if (bestOffset > 0xFFFF) bestOffset = 0xFFFF;
            if (bestLen > 0xFF) bestLen = 0xFF;

            LZ77Token t{ (uint16_t)bestOffset, (uint8_t)bestLen, 0 };
            pendingTokens.push_back(t);

            size_t end = i + bestLen;
            for (size_t p = i; p < end; ++p) {
                if (p + KEY_LEN <= n) {
                    uint32_t k = make_key(&chunk[p]);
                    auto &dq = dict[k];
                    dq.push_back(absolutePos + p);
                    if (dq.size() > MAX_POS_PER_KEY) dq.pop_front();
                }
            }

            i += bestLen;
            continue;
        }


        // Normal literal fallback
        LZ77Token t{0,0,chunk[i]};
        pendingTokens.push_back(t);

        if (i + KEY_LEN <= n) {
            uint32_t k = make_key(&chunk[i]);
            auto &dq = dict[k];
            dq.push_back(absolutePos + i);
            if (dq.size() > MAX_POS_PER_KEY) dq.pop_front();
        }

        ++i;
    }


    for (uint8_t b : chunk) window.push_back(b);
    while (window.size() > windowSize) window.pop_front();

    absolutePos += n;
}

std::vector<uint8_t> LZ77StreamCompressor::consumeOutput() {
    auto out = lz77_serialize(pendingTokens);
    pendingTokens.clear();
    return out;
}

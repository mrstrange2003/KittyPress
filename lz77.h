// lz77.h
#pragma once
#include <vector>
#include <cstdint>

// A simple token. If offset==0 && length==0 -> literal (value stored in 'lit')
// Otherwise it's a match: copy 'length' bytes from 'offset' bytes back.
struct LZ77Token {
    uint16_t offset; // 0 means literal
    uint8_t length;  // 0 means literal
    uint8_t lit;     // literal byte (valid only when offset==0 && length==0)
};

// Basic LZ77 API
std::vector<LZ77Token> lz77_compress(const std::vector<uint8_t> &data, size_t windowSize = 65535, size_t maxMatch = 255);
std::vector<uint8_t> lz77_serialize(const std::vector<LZ77Token> &tokens);   // produce a byte-stream representation
std::vector<LZ77Token> lz77_deserialize(const std::vector<uint8_t> &bytes);  // parse bytes back to tokens
std::vector<uint8_t> lz77_decompress(const std::vector<LZ77Token> &tokens);

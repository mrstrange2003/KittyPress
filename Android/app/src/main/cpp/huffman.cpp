// huffman.cpp  — zlib-backed compressor wrapper (KP03-compatible)
// Replace the previous huffman.cpp with this file.

#include "huffman.h"
#include "bitstream.h"
#include "kitty.h"
#include "lz77.h"

#include <zlib.h>
#include <iostream>
#include <vector>
#include <fstream>
#include <filesystem>
#include <cstdint>
#include <array>
#include <stdexcept>
#include <iterator>

using namespace std;
namespace fs = std::__fs::filesystem;

// Keep helper used by archive.cpp & others
static string makeFinalOutputPath(const string &baseOut, const string &storedExt) {
    fs::path p(baseOut);
    if (!storedExt.empty()) {
        if (p.extension().empty()) {
            p += storedExt;   // append extension (storedExt includes the '.' if present)
        }
    }
    return p.string();
}

// ---------- RAW store/restore (kept from before) ----------
void storeRawFile(const string &inputPath, const string &outputPath) {
    ifstream in(inputPath, ios::binary);
    if (!in.is_open()) throw runtime_error("Cannot open input file.");

    vector<uint8_t> buffer((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
    in.close();

    ofstream out(outputPath, ios::binary);
    if (!out.is_open()) throw runtime_error("Cannot open output file for writing.");

    // KP03 header
    out.write(KITTY_MAGIC_V3.c_str(), KITTY_MAGIC_V3.size());

    bool isCompressed = false;
    out.write(reinterpret_cast<const char*>(&isCompressed), sizeof(isCompressed));

    string ext = fs::path(inputPath).extension().string();
    uint64_t extLen = ext.size();
    out.write(reinterpret_cast<const char*>(&extLen), sizeof(extLen));
    if (extLen > 0) out.write(ext.c_str(), extLen);

    uint64_t rawSize = buffer.size();
    out.write(reinterpret_cast<const char*>(&rawSize), sizeof(rawSize));
    if (rawSize > 0) out.write(reinterpret_cast<const char*>(buffer.data()), rawSize);

    out.close();
}

void restoreRawFile(ifstream &inStream, const string &outputPath) {
    uint64_t rawSize;
    inStream.read(reinterpret_cast<char*>(&rawSize), sizeof(rawSize));
    if (!inStream.good()) throw runtime_error("Failed to read raw size.");
    vector<char> buffer;
    buffer.resize((size_t)rawSize);
    if (rawSize > 0) {
        inStream.read(reinterpret_cast<char*>(buffer.data()), rawSize);
        if ((uint64_t)inStream.gcount() != rawSize) throw runtime_error("Unexpected EOF while reading raw payload.");
    }
    ofstream out(outputPath, ios::binary);
    if (!out.is_open()) throw runtime_error("Cannot open output file for writing.");
    if (rawSize > 0) out.write(buffer.data(), rawSize);
    out.close();
}

// ---------- New compressFile / decompressFile using zlib ----------
void compressFile(const string &inputPath, const string &outputPath) {
    if (!fs::exists(inputPath)) throw runtime_error("Input not found.");
    ifstream in(inputPath, ios::binary);
    if (!in.is_open()) throw runtime_error("Cannot open input file.");

    // read entire file
    vector<uint8_t> src((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
    in.close();

    uint64_t originalSize = src.size();

    // attempt zlib compression
    // estimate upper bound for compressed size: compressBound
    uLong bound = compressBound((uLong)src.size());
    vector<uint8_t> compressed;
    compressed.resize(bound);

    uLongf destLen = bound;
    int zrc = compress2(compressed.data(), &destLen, src.data(), (uLong)src.size(), Z_BEST_COMPRESSION);
    if (zrc != Z_OK) {
        // fallback: store raw
        storeRawFile(inputPath, outputPath);
        return;
    }

    compressed.resize((size_t)destLen);

    // if compressed is not smaller, store raw file
    if (compressed.size() >= src.size()) {
        storeRawFile(inputPath, outputPath);
        return;
    }

    // Otherwise write KP03 header with compressed payload (compact header)
    ofstream out(outputPath, ios::binary);
    if (!out.is_open()) throw runtime_error("Cannot open output file for writing.");

    // magic
    out.write(KITTY_MAGIC_V3.c_str(), KITTY_MAGIC_V3.size());

    // compressed flag
    bool isCompressed = true;
    out.write(reinterpret_cast<const char*>(&isCompressed), sizeof(isCompressed));

    // extension
    string ext = fs::path(inputPath).extension().string();
    uint64_t extLen = ext.size();
    out.write(reinterpret_cast<const char*>(&extLen), sizeof(extLen));
    if (extLen > 0) out.write(ext.c_str(), extLen);

    // write compressed size (uint64) then compressed bytes
    uint64_t compSize = compressed.size();
    out.write(reinterpret_cast<const char*>(&compSize), sizeof(compSize));
    if (compSize > 0) out.write(reinterpret_cast<const char*>(compressed.data()), compSize);

    out.close();
}

void decompressFile(const string &inputPath, const string &outputPath) {
    ifstream in(inputPath, ios::binary);
    if (!in.is_open()) throw runtime_error("Cannot open input file.");

    // read magic
    string magic(4, '\0');
    in.read(&magic[0], 4);
    if (!in.good()) throw runtime_error("Failed to read file signature.");

    // KP01 / KP02 old pathways are not used now — keep simple KP03 semantics:
    if (magic != KITTY_MAGIC_V3) {
        // For backward compatibility: if KP01/KP02 present, we can throw
        throw runtime_error("Unsupported file signature (expected KITTY V3).");
    }

    bool isCompressed = false;
    in.read(reinterpret_cast<char*>(&isCompressed), sizeof(isCompressed));

    uint64_t extLen = 0;
    in.read(reinterpret_cast<char*>(&extLen), sizeof(extLen));
    string ext;
    if (extLen > 0) {
        ext.resize((size_t)extLen);
        in.read(&ext[0], (streamsize)extLen);
    }

    string finalOut = makeFinalOutputPath(outputPath, ext);

    if (!isCompressed) {
        // raw payload
        restoreRawFile(in, finalOut);
        in.close();
        return;
    }

    // compressed path: read compressed size, then compressed bytes
    uint64_t compSize = 0;
    in.read(reinterpret_cast<char*>(&compSize), sizeof(compSize));
    if (!in.good()) throw runtime_error("Failed to read compressed size.");

    vector<uint8_t> compBuf;
    compBuf.resize((size_t)compSize);
    if (compSize > 0) {
        in.read(reinterpret_cast<char*>(compBuf.data()), (streamsize)compSize);
        if ((uint64_t)in.gcount() != compSize) throw runtime_error("Unexpected EOF while reading compressed payload.");
    }
    in.close();

    // Attempt to uncompress. We don't know original size stored here; we will try to guess.
    // Strategy: try doubling buffer until uncompress() returns Z_OK.
    // Start with a reasonable estimate: 4x compressed size or 10x if small.
    uLongf guess = (uLongf)max<uint64_t>(compSize * 4, 1024);
    vector<uint8_t> outBuf;
    int zrc = Z_MEM_ERROR;
    for (int attempts = 0; attempts < 10; ++attempts) {
        try {
            outBuf.resize(guess);
        } catch (...) {
            throw runtime_error("Memory allocation failed during decompression.");
        }
        uLongf destLen = guess;
        zrc = uncompress(outBuf.data(), &destLen, compBuf.data(), (uLongf)compSize);
        if (zrc == Z_OK) {
            outBuf.resize(destLen);
            break;
        } else if (zrc == Z_BUF_ERROR) {
            // need bigger buffer
            guess *= 2;
            continue;
        } else {
            // other error
            break;
        }
    }

    if (zrc != Z_OK) {
        throw runtime_error(string("zlib uncompress failed: ") + to_string(zrc));
    }

    // Write the uncompressed bytes to finalOut
    ofstream of(finalOut, ios::binary);
    if (!of.is_open()) throw runtime_error("Cannot open output file for writing.");
    if (!outBuf.empty()) of.write(reinterpret_cast<const char*>(outBuf.data()), outBuf.size());
    of.close();
}

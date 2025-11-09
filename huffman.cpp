// huffman.cpp  (KP03 - integrates LZ77 + Huffman)
#include "huffman.h"
#include "bitstream.h"
#include "kitty.h"
#include "lz77.h"
#include <iostream>
#include <bitset>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <cstdint>

using namespace std;

// --- Huffman helpers (operate on byte streams) ---
void buildCodes(HuffmanNode* root, const string &str, unordered_map<unsigned char, string> &huffmanCode) {
    if (!root) return;
    if (!root->left && !root->right) {
        huffmanCode[root->ch] = (str.empty() ? "0" : str);
    }
    buildCodes(root->left, str + "0", huffmanCode);
    buildCodes(root->right, str + "1", huffmanCode);
}

void freeTree(HuffmanNode* root) {
    if (!root) return;
    freeTree(root->left);
    freeTree(root->right);
    delete root;
}

// ---------------- STORE RAW (KP02/KP03 store mode) ----------------
void storeRawFile(const string &inputPath, const string &outputPath) {
    ifstream in(inputPath, ios::binary);
    if (!in.is_open()) throw runtime_error("Cannot open input file.");

    vector<uint8_t> buffer((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
    in.close();

    ofstream out(outputPath, ios::binary);
    if (!out.is_open()) throw runtime_error("Cannot open output file for writing.");

    // write signature KP03 (we're writing KP03 format)
    out.write(KITTY_MAGIC_V3.c_str(), KITTY_MAGIC_V3.size());

    // isCompressed = false
    bool isCompressed = false;
    out.write(reinterpret_cast<const char*>(&isCompressed), sizeof(isCompressed));

    // write original extension
    string ext = filesystem::path(inputPath).extension().string();
    uint64_t extLen = ext.size();
    out.write(reinterpret_cast<const char*>(&extLen), sizeof(extLen));
    if (extLen > 0) out.write(ext.c_str(), extLen);

    // write raw size and raw bytes
    uint64_t rawSize = buffer.size();
    out.write(reinterpret_cast<const char*>(&rawSize), sizeof(rawSize));
    if (rawSize > 0)
        out.write(reinterpret_cast<const char*>(buffer.data()), rawSize);

    out.close();
}

// helper used when restoring raw from an already-opened stream (after signature & header parsed)
void restoreRawFile(std::ifstream &inStream, const string &outputPath) {
    uint64_t rawSize;
    inStream.read(reinterpret_cast<char*>(&rawSize), sizeof(rawSize));
    if (!inStream.good()) throw runtime_error("Failed to read raw size.");
    vector<char> buffer;
    buffer.resize(rawSize);
    if (rawSize > 0) {
        inStream.read(reinterpret_cast<char*>(buffer.data()), rawSize);
        if ((uint64_t)inStream.gcount() != rawSize) {
            throw runtime_error("Unexpected EOF while reading raw payload.");
        }
    }
    ofstream out(outputPath, ios::binary);
    if (!out.is_open()) throw runtime_error("Cannot open output file for writing.");
    if (rawSize > 0) out.write(buffer.data(), rawSize);
    out.close();
}

// ---------------- SMART COMPRESSION (KP03) ----------------
void compressFile(const string &inputPath, const string &outputPath) {
    ifstream in(inputPath, ios::binary);
    if (!in.is_open()) throw runtime_error("Cannot open input file.");

    vector<uint8_t> data((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
    in.close();
    if (data.empty()) throw runtime_error("Input file is empty.");

    // --- Step 1: Run LZ77 -> serialize tokens to bytes ---
    vector<LZ77Token> tokens = lz77_compress(data);
    vector<uint8_t> lz77_bytes = lz77_serialize(tokens); // vector<uint8_t>

    // Build frequency map over lz77_bytes
    unordered_map<unsigned char, int> freq;
    for (uint8_t b : lz77_bytes) freq[static_cast<unsigned char>(b)]++;

    // Build Huffman tree
    priority_queue<HuffmanNode*, vector<HuffmanNode*>, Compare> pq;
    for (auto &pair : freq)
        pq.push(new HuffmanNode(pair.first, pair.second));

    if (pq.empty()) {
        // no bytes? then just store raw
        storeRawFile(inputPath, outputPath);
        return;
    }

    while (pq.size() > 1) {
        HuffmanNode *left = pq.top(); pq.pop();
        HuffmanNode *right = pq.top(); pq.pop();
        HuffmanNode *node = new HuffmanNode(0, left->freq + right->freq);
        node->left = left; node->right = right;
        pq.push(node);
    }

    HuffmanNode *root = pq.top();
    unordered_map<unsigned char, string> huffmanCode;
    buildCodes(root, "", huffmanCode);

    // Encode using Huffman
    string encoded;
    encoded.reserve(lz77_bytes.size() * 8);
    for (uint8_t b : lz77_bytes) {
        unsigned char uc = static_cast<unsigned char>(b);
        encoded += huffmanCode[uc];
    }

    // Write compressed data to a temporary memory buffer (ostringstream)
    std::ostringstream tmp;
    // signature
    tmp.write(KITTY_MAGIC_V3.c_str(), KITTY_MAGIC_V3.size());
    // isCompressed = true
    bool isCompressed = true;
    tmp.write(reinterpret_cast<const char*>(&isCompressed), sizeof(isCompressed));
    // extension
    string ext = filesystem::path(inputPath).extension().string();
    uint64_t extLen = ext.size();
    tmp.write(reinterpret_cast<const char*>(&extLen), sizeof(extLen));
    if (extLen > 0) tmp.write(ext.c_str(), extLen);

    // Huffman map
    uint64_t mapSize = huffmanCode.size();
    tmp.write(reinterpret_cast<const char*>(&mapSize), sizeof(mapSize));
    for (auto &pair : huffmanCode) {
        unsigned char c = pair.first;
        string code = pair.second;
        uint64_t len = code.size();
        tmp.write(reinterpret_cast<const char*>(&c), sizeof(c));
        tmp.write(reinterpret_cast<const char*>(&len), sizeof(len));
        tmp.write(code.c_str(), len);
    }

    // encoded length (bits)
    uint64_t encodedLen = encoded.size();
    tmp.write(reinterpret_cast<const char*>(&encodedLen), sizeof(encodedLen));

    // write bitstream using BitWriter on std::ostringstream
    BitWriter writer(tmp);
    writer.writeBits(encoded);
    writer.flush();

    freeTree(root);

    // --- Step 2: Decide whether to keep compressed buffer or store raw ---
    string compressedData = tmp.str();
    size_t compressedSize = compressedData.size();
    size_t originalSize = data.size();

    ofstream outFile(outputPath, ios::binary);
    if (!outFile.is_open()) throw runtime_error("Cannot open output file for writing.");

    if (compressedSize < originalSize) {
        cout << "🐾 Smart Mode: Compression effective ("
             << fixed << setprecision(2)
             << 100.0 * (1.0 - (double)compressedSize / originalSize)
             << "% saved)\n";
        outFile.write(compressedData.c_str(), compressedData.size());
    } else {
        cout << "⚡ Smart Mode: Compression skipped (file too compact)\n";
        outFile.close();
        storeRawFile(inputPath, outputPath);
        return;
    }

    outFile.close();

    cout << "✅ Final size: " << compressedSize << " bytes (original " << originalSize << ")\n";
}

// ---------------- DECOMPRESSION (accepts KP01, KP02, KP03) ----------------
void decompressFile(const string &inputPath, const string &outputPath) {
    ifstream in(inputPath, ios::binary);
    if (!in.is_open()) throw runtime_error("Cannot open input file.");

    // Read the first 4 bytes (signature candidate)
    string magic(4, '\0');
    in.read(&magic[0], 4);
    if (!in) throw runtime_error("Failed to read file signature.");

    // --- KP01 (old) handling ---
    if (magic == KITTY_MAGIC_V1) {
        // older format: mapSize etc (assume 64-bit sizes were used)
        uint64_t mapSize;
        in.read(reinterpret_cast<char*>(&mapSize), sizeof(mapSize));
        unordered_map<unsigned char, string> huffmanCode;
        for (uint64_t i = 0; i < mapSize; ++i) {
            unsigned char c;
            uint64_t len;
            in.read(reinterpret_cast<char*>(&c), sizeof(c));
            in.read(reinterpret_cast<char*>(&len), sizeof(len));
            string code(len, '\0');
            in.read(&code[0], len);
            huffmanCode[c] = code;
        }
        uint64_t encodedLen;
        in.read(reinterpret_cast<char*>(&encodedLen), sizeof(encodedLen));
        BitReader reader(in);
        bool bit;
        string bitstream;
        bitstream.reserve(encodedLen);
        while (reader.readBit(bit)) bitstream += (bit ? '1' : '0');
        in.close();
        if (bitstream.size() > encodedLen) bitstream.resize(encodedLen);
        unordered_map<string, unsigned char> reverseCode;
        for (auto &p : huffmanCode) reverseCode[p.second] = p.first;
        string current;
        vector<char> decoded;
        for (char b : bitstream) {
            current += b;
            auto it = reverseCode.find(current);
            if (it != reverseCode.end()) {
                decoded.push_back((char)it->second);
                current.clear();
            }
        }
        ofstream out(outputPath, ios::binary);
        if (!out.is_open()) throw runtime_error("Cannot open output file for writing.");
        if (!decoded.empty()) out.write(decoded.data(), decoded.size());
        out.close();
        cout << "Decompressed (KP01) successfully → " << outputPath << endl;
        return;
    }

    // --- KP02 handling ---
    if (magic == KITTY_MAGIC_V2) {
        bool isCompressed = false;
        in.read(reinterpret_cast<char*>(&isCompressed), sizeof(isCompressed));
        uint64_t extLen = 0;
        in.read(reinterpret_cast<char*>(&extLen), sizeof(extLen));
        if (extLen > 0) {
            string ext; ext.resize(extLen);
            in.read(&ext[0], extLen);
        }
        if (!isCompressed) {
            restoreRawFile(in, outputPath);
            in.close();
            cout << "Restored raw file (KP02) → " << outputPath << endl;
            return;
        }
        // compressed path for KP02 (Huffman decode to bytes)
        uint64_t mapSize;
        in.read(reinterpret_cast<char*>(&mapSize), sizeof(mapSize));
        unordered_map<unsigned char, string> huffmanCode;
        for (uint64_t i = 0; i < mapSize; ++i) {
            unsigned char c; uint64_t len;
            in.read(reinterpret_cast<char*>(&c), sizeof(c));
            in.read(reinterpret_cast<char*>(&len), sizeof(len));
            string code(len, '\0');
            in.read(&code[0], len);
            huffmanCode[c] = code;
        }
        uint64_t encodedLen;
        in.read(reinterpret_cast<char*>(&encodedLen), sizeof(encodedLen));
        BitReader reader(in);
        bool bit;
        string bitstream;
        bitstream.reserve(encodedLen);
        while (reader.readBit(bit)) bitstream += (bit ? '1' : '0');
        in.close();
        if (bitstream.size() > encodedLen) bitstream.resize(encodedLen);
        unordered_map<string, unsigned char> reverseCode;
        for (auto &p : huffmanCode) reverseCode[p.second] = p.first;
        string current;
        vector<char> decoded;
        for (char b : bitstream) {
            current += b;
            auto it = reverseCode.find(current);
            if (it != reverseCode.end()) {
                decoded.push_back((char)it->second);
                current.clear();
            }
        }
        ofstream out(outputPath, ios::binary);
        if (!out.is_open()) throw runtime_error("Cannot open output file for writing.");
        if (!decoded.empty()) out.write(decoded.data(), decoded.size());
        out.close();
        cout << "Decompressed (KP02) successfully → " << outputPath << endl;
        return;
    }

    // --- KP03 (LZ77 + Huffman) handling ---
    if (magic != KITTY_MAGIC_V3) {
        throw runtime_error("Unknown or corrupted .kitty file (bad signature).");
    }

    // read isCompressed flag and ext
    bool isCompressed = false;
    in.read(reinterpret_cast<char*>(&isCompressed), sizeof(isCompressed));
    uint64_t extLen = 0;
    in.read(reinterpret_cast<char*>(&extLen), sizeof(extLen));
    string ext;
    if (extLen > 0) {
        ext.resize(extLen);
        in.read(&ext[0], extLen);
    }

    if (!isCompressed) {
        restoreRawFile(in, outputPath);
        in.close();
        cout << "Restored raw file (KP03) → " << outputPath << endl;
        return;
    }

    // compressed: read Huffman map
    uint64_t mapSize = 0;
    in.read(reinterpret_cast<char*>(&mapSize), sizeof(mapSize));
    unordered_map<unsigned char, string> huffmanCode;
    for (uint64_t i = 0; i < mapSize; ++i) {
        unsigned char c; uint64_t len;
        in.read(reinterpret_cast<char*>(&c), sizeof(c));
        in.read(reinterpret_cast<char*>(&len), sizeof(len));
        string code(len, '\0');
        in.read(&code[0], len);
        huffmanCode[c] = code;
    }

    // read encoded bitstream length
    uint64_t encodedLen = 0;
    in.read(reinterpret_cast<char*>(&encodedLen), sizeof(encodedLen));

    // read bitstream via BitReader
    // We need to read bytes until EOF for the bitstream (reader will return false at EOF)
    BitReader reader(in);
    bool bit;
    string bitstream;
    bitstream.reserve(encodedLen);
    while (reader.readBit(bit)) bitstream += (bit ? '1' : '0');
    in.close();
    if (bitstream.size() > encodedLen) bitstream.resize(encodedLen);

    // Huffman decode -> get bytes of serialized LZ77 tokens
    unordered_map<string, unsigned char> reverseCode;
    for (auto &p : huffmanCode) reverseCode[p.second] = p.first;
    string current;
    vector<uint8_t> tokenBytes;
    for (char b : bitstream) {
        current += b;
        auto it = reverseCode.find(current);
        if (it != reverseCode.end()) {
            tokenBytes.push_back(it->second);
            current.clear();
        }
    }

    // Deserialize tokens, then LZ77 decompress to recover original bytes
    auto tokens_out = lz77_deserialize(tokenBytes);
    auto original = lz77_decompress(tokens_out);

    ofstream out(outputPath, ios::binary);
    if (!out.is_open()) throw runtime_error("Cannot open output file for writing.");
    if (!original.empty()) out.write(reinterpret_cast<const char*>(original.data()), original.size());
    out.close();

    cout << "Decompressed (KP03) successfully → " << outputPath << endl;
}

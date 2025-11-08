// huffman.cpp
#include "huffman.h"
#include "bitstream.h"
#include "kitty.h"
#include <iostream>
#include <bitset>
#include <iomanip>
#include <filesystem>
#include <algorithm>
#include <cstdint>

using namespace std;

// Recursive traversal to generate binary codes
void buildCodes(HuffmanNode* root, const string &str, unordered_map<unsigned char, string> &huffmanCode) {
    if (!root)
        return;

    // Leaf node = actual byte
    if (!root->left && !root->right)
        huffmanCode[root->ch] = (str.empty() ? "0" : str); // handle single-symbol file

    buildCodes(root->left, str + "0", huffmanCode);
    buildCodes(root->right, str + "1", huffmanCode);
}

// Free allocated Huffman tree memory
void freeTree(HuffmanNode* root) {
    if (!root) return;
    freeTree(root->left);
    freeTree(root->right);
    delete root;
}

// ---------------- STORE RAW (KP02 store mode) ----------------
void storeRawFile(const string &inputPath, const string &outputPath) {
    ifstream in(inputPath, ios::binary);
    if (!in.is_open()) throw runtime_error("Cannot open input file.");

    // Read whole file into buffer
    vector<unsigned char> buffer((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
    in.close();

    ofstream out(outputPath, ios::binary);
    if (!out.is_open()) throw runtime_error("Cannot open output file for writing.");

    // Write KP02 signature
    out.write(KITTY_MAGIC_V2.c_str(), KITTY_MAGIC_V2.size());

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

// ---------------- COMPRESSION (writes KP02 with isCompressed = true) ----------------
void compressFile(const string &inputPath, const string &outputPath) {
    ifstream in(inputPath, ios::binary);
    if (!in.is_open()) throw runtime_error("Cannot open input file.");

    // Read entire file as bytes
    vector<unsigned char> data((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
    in.close();

    if (data.empty()) throw runtime_error("Input file is empty.");

    // Count frequencies for all 0..255 bytes
    unordered_map<unsigned char, int> freq;
    for (unsigned char b : data) freq[b]++;

    // Build min-heap (priority queue)
    priority_queue<HuffmanNode*, vector<HuffmanNode*>, Compare> pq;
    for (auto &pair : freq)
        pq.push(new HuffmanNode(pair.first, pair.second));

    // Build Huffman Tree
    while (pq.size() > 1) {
        HuffmanNode *left = pq.top(); pq.pop();
        HuffmanNode *right = pq.top(); pq.pop();

        HuffmanNode *node = new HuffmanNode(0, left->freq + right->freq);
        node->left = left;
        node->right = right;
        pq.push(node);
    }

    HuffmanNode *root = pq.top();

    // Generate codes
    unordered_map<unsigned char, string> huffmanCode;
    buildCodes(root, "", huffmanCode);

    // Encode the input bytes into a long bit-string
    string encoded;
    encoded.reserve(data.size() * 8);
    for (unsigned char b : data) encoded += huffmanCode[b];

    // Write to output file (.kitty) - KP02 format
    ofstream out(outputPath, ios::binary);
    if (!out.is_open()) throw runtime_error("Cannot open output file for writing.");

    // 1) signature
    out.write(KITTY_MAGIC_V2.c_str(), KITTY_MAGIC_V2.size());

    // 2) isCompressed flag true
    bool isCompressed = true;
    out.write(reinterpret_cast<const char*>(&isCompressed), sizeof(isCompressed));

    // 3) write original extension
    string ext = filesystem::path(inputPath).extension().string();
    uint64_t extLen = ext.size();
    out.write(reinterpret_cast<const char*>(&extLen), sizeof(extLen));
    if (extLen > 0) out.write(ext.c_str(), extLen);

    // 4) write Huffman map (mapSize + entries). Use uint64_t for sizes
    uint64_t mapSize = huffmanCode.size();
    out.write(reinterpret_cast<const char*>(&mapSize), sizeof(mapSize));
    for (auto &pair : huffmanCode) {
        unsigned char c = pair.first;
        string code = pair.second;
        uint64_t len = code.size();
        out.write(reinterpret_cast<const char*>(&c), sizeof(c));
        out.write(reinterpret_cast<const char*>(&len), sizeof(len));
        out.write(code.c_str(), len);
    }

    // 5) encoded bitstream length
    uint64_t encodedLen = encoded.size();
    out.write(reinterpret_cast<const char*>(&encodedLen), sizeof(encodedLen));

    // 6) write encoded bits via BitWriter
    BitWriter writer(out);
    writer.writeBits(encoded);
    writer.flush();

    out.close();
    freeTree(root);

    // Stats
    auto inputSize = filesystem::file_size(inputPath);
    auto outputSize = filesystem::file_size(outputPath);
    double ratio = 100.0 * (1.0 - (double)outputSize / inputSize);

    cout << "\nOriginal:   " << inputSize << " bytes" << endl;
    cout << "Compressed: " << outputSize << " bytes" << endl;
    cout << "Compression: " << fixed << setprecision(2) << ratio << "% 🐾" << endl;
}

// ---------------- DECOMPRESSION (accepts KP01 and KP02) ----------------
void decompressFile(const string &inputPath, const string &outputPath) {
    ifstream in(inputPath, ios::binary);
    if (!in.is_open()) throw runtime_error("Cannot open input file.");

    // Read first 4 bytes as signature candidate
    string magic(4, '\0');
    in.read(&magic[0], 4);
    if (!in) throw runtime_error("Failed to read file signature.");

    // If KP01 (old format), rewind and use old v1 logic
    if (magic == KITTY_MAGIC_V1) {
        // rewind to beginning (we already consumed 4 bytes)
        in.seekg(4, ios::beg); // position after "KP01"
        // Original KP01 format (what your earlier code expected) assumed no ext/isCompressed flag, and mapSize is next.
        // For simplicity, reuse prior logic by reading from after signature (i.e., exactly like old code expected),
        // but since the old code read mapSize immediately (no signature), we need to adapt: your old compressor didn't write KP01
        // at the start in your earlier version — if your old files do start with KP01, this code will work.
        // We'll now parse using the KP01 structure (mapSize, entries, encodedLen, bytes).
        // NOTE: If older files had no signature at all, adjust accordingly. But earlier we wrote KP01 as signature already.
        // Read mapSize
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
        // encoded length
        uint64_t encodedLen;
        in.read(reinterpret_cast<char*>(&encodedLen), sizeof(encodedLen));
        // read bytes (older format used raw bytes stored; use BitReader)
        BitReader reader(in);
        bool bit;
        string bitstream;
        bitstream.reserve(encodedLen);
        while (reader.readBit(bit)) bitstream += (bit ? '1' : '0');
        in.close();
        bitstream = bitstream.substr(0, encodedLen);
        unordered_map<string, unsigned char> reverseCode;
        for (auto &p : huffmanCode) reverseCode[p.second] = p.first;
        // decode
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
        // write out
        ofstream out(outputPath, ios::binary);
        if (!out.is_open()) throw runtime_error("Cannot open output file for writing.");
        if (!decoded.empty()) out.write(decoded.data(), decoded.size());
        out.close();
        cout << "Decompressed (KP01) successfully → " << outputPath << endl;
        return;
    }

    // If not KP01, check KP02
    if (magic != KITTY_MAGIC_V2) {
        throw runtime_error("Unknown or corrupted .kitty file (bad signature).");
    }

    // Now parse KP02 header:
    // isCompressed flag
    bool isCompressed = false;
    in.read(reinterpret_cast<char*>(&isCompressed), sizeof(isCompressed));
    // read extension
    uint64_t extLen = 0;
    in.read(reinterpret_cast<char*>(&extLen), sizeof(extLen));
    string ext;
    if (extLen > 0) {
        ext.resize(extLen);
        in.read(&ext[0], extLen);
    }

    // If stored raw, restore raw bytes and write to outputPath (or append ext)
    if (!isCompressed) {
        // There is rawSize + raw bytes next
        restoreRawFile(in, outputPath);
        in.close();
        cout << "Restored raw file → " << outputPath << endl;
        return;
    }

    // Otherwise isCompressed == true: read Huffman map and decode (KP02 compressed payload)
    uint64_t mapSize = 0;
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

    // encoded length
    uint64_t encodedLen = 0;
    in.read(reinterpret_cast<char*>(&encodedLen), sizeof(encodedLen));

    // read bitstream
    BitReader reader(in);
    bool bit;
    string bitstream;
    bitstream.reserve(encodedLen);
    while (reader.readBit(bit)) bitstream += (bit ? '1' : '0');
    in.close();

    if (bitstream.size() > encodedLen) bitstream.resize(encodedLen);

    // reverse map and decode
    unordered_map<string, unsigned char> reverseCode;
    for (auto &p : huffmanCode) reverseCode[p.second] = p.first;

    string current;
    vector<char> decoded;
    decoded.reserve(bitstream.size() / 8);
    for (char b : bitstream) {
        current += b;
        auto it = reverseCode.find(current);
        if (it != reverseCode.end()) {
            decoded.push_back((char)it->second);
            current.clear();
        }
    }

    // write decompressed file
    ofstream out(outputPath, ios::binary);
    if (!out.is_open()) throw runtime_error("Cannot open output file for writing.");
    if (!decoded.empty()) out.write(decoded.data(), decoded.size());
    out.close();

    cout << "Decompressed successfully → " << outputPath << endl;
}

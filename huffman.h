// huffman.h
#pragma once
#include <string>
#include <unordered_map>
#include <queue>
#include <vector>
#include <fstream>
#include <bitset>
#include <memory>
#include <cstdint>

// Use unsigned char for full 0..255 byte support
struct HuffmanNode {
    unsigned char ch;
    int freq;
    HuffmanNode *left;
    HuffmanNode *right;

    HuffmanNode(unsigned char c, int f) : ch(c), freq(f), left(nullptr), right(nullptr) {}
};

// Comparator for priority queue
struct Compare {
    bool operator()(HuffmanNode* l, HuffmanNode* r) {
        return l->freq > r->freq;
    }
};

// Function declarations
void compressFile(const std::string &inputPath, const std::string &outputPath); // writes KP02 with isCompressed = true
void decompressFile(const std::string &inputPath, const std::string &outputPath); // handles KP01 & KP02

// Helpers for storing raw files inside .kitty (KP02 with isCompressed = false)
void storeRawFile(const std::string &inputPath, const std::string &outputPath);
void restoreRawFile(std::ifstream &inStream, const std::string &outputPath);

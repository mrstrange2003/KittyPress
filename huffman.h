// huffman.h
#pragma once
#include <string>
#include <unordered_map>
#include <queue>
#include <vector>
#include <fstream>
#include <bitset>
#include <memory>

struct HuffmanNode {
    char ch;
    int freq;
    HuffmanNode *left;
    HuffmanNode *right;

    HuffmanNode(char c, int f) : ch(c), freq(f), left(nullptr), right(nullptr) {}
};

// Comparator for priority queue
struct Compare {
    bool operator()(HuffmanNode* l, HuffmanNode* r) {
        return l->freq > r->freq;
    }
};

// Function declarations
void compressFile(const std::string &inputPath, const std::string &outputPath);
void decompressFile(const std::string &inputPath, const std::string &outputPath);

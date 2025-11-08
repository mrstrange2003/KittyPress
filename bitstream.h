// bitstream.h
#pragma once
#include <fstream>
#include <vector>

class BitWriter {
    std::ofstream &out;
    unsigned char buffer;
    int bitCount;

public:
    BitWriter(std::ofstream &stream);
    void writeBit(bool bit);
    void writeBits(const std::string &bits);
    void flush();
};

class BitReader {
    std::ifstream &in;
    unsigned char buffer;
    int bitCount;

public:
    BitReader(std::ifstream &stream);
    bool readBit(bool &bit);
};

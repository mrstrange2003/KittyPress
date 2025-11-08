//main.cpp
#include <iostream>
#include <string>
#include <filesystem>
#include "huffman.h"  

using namespace std;

void printUsage() {
    cout << "\nKittyPress v1.0 🐾" << endl;
    cout << "A lightweight text compressor using Huffman coding.\n" << endl;
    cout << "Usage:" << endl;
    cout << "  kittypress compress <input.txt> <output.kitty>" << endl;
    cout << "  kittypress decompress <input.kitty> <output.txt>\n" << endl;
}

int main(int argc, char* argv[]) {
    cout << "KittyPress launched! argc=" << argc << endl;

    // Check minimum arguments
    if (argc < 4) {
        printUsage();
        return 1;
    }

    string mode = argv[1];
    string inputPath = argv[2];
    string outputPath = argv[3];

    // Check if input file exists
    if (!filesystem::exists(inputPath)) {
        cerr << "Error: Input file not found → " << inputPath << endl;
        return 1;
    }

    try {
        if (mode == "compress") {
            cout << "Compressing: " << inputPath << " → " << outputPath << endl;
            compressFile(inputPath, outputPath);   // defined in huffman.cpp
            cout << "Compression complete.\n";
        }
        else if (mode == "decompress") {
            cout << "Decompressing: " << inputPath << " → " << outputPath << endl;
            decompressFile(inputPath, outputPath); // defined in huffman.cpp
            cout << "Decompression complete.\n";
        }
        else {
            cerr << "Unknown command: " << mode << endl;
            printUsage();
            return 1;
        }
    }
    catch (const exception &e) {
        cerr << "Error: " << e.what() << endl;
        return 1;
    }

    cout << "[KittyPress] Done.\n";
    return 0;
}

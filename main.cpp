// main.cpp
#include <iostream>
#include <string>
#include <filesystem>
#include "huffman.h"

using namespace std;

void printUsage() {
    cout << "\nKittyPress v3 " << endl;
    cout << "A universal lossless archiver using LZ77 + Huffman compression.\n" << endl;
    cout << "Usage:" << endl;
    cout << "  kittypress compress <input> <output.kitty>" << endl;
    cout << "  kittypress decompress <input.kitty> <output>\n" << endl;
}

int main(int argc, char* argv[]) {
    cout << "KittyPress launched! argc=" << argc << endl;

    if (argc < 4) {
        printUsage();
        return 1;
    }

    string mode = argv[1];
    string inputPath = argv[2];
    string outputPath = argv[3];

    if (!filesystem::exists(inputPath)) {
        cerr << "Error: Input file not found → " << inputPath << endl;
        return 1;
    }

    try {
        if (mode == "compress") {
            cout << "Compressing: " << inputPath << " → " << outputPath << endl;
            cout << "Mode: Universal compression (LZ77 + Huffman)\n";

            // Always compress, regardless of file type
            compressFile(inputPath, outputPath);

            cout << "Compression complete.\n";
        }
        else if (mode == "decompress") {
            cout << "Decompressing: " << inputPath << " → " << outputPath << endl;
            decompressFile(inputPath, outputPath);
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

// main.cpp
#include <iostream>
#include <string>
#include <filesystem>
#include <algorithm>   
#include <vector>      
#include "huffman.h"

using namespace std;

void printUsage() {
    cout << "\nKittyPress v2 " << endl;
    cout << "A lightweight universal archiver (Huffman for text; store for media).\n" << endl;
    cout << "Usage:" << endl;
    cout << "  kittypress compress <input> <output.kitty>" << endl;
    cout << "  kittypress decompress <input.kitty> <output>\n" << endl;
}

bool isCompressibleExtension(const string &ext) {
    string e = ext;
    transform(e.begin(), e.end(), e.begin(), ::tolower);
    static const vector<string> compressExts = {
        ".txt", ".log", ".csv", ".json", ".xml", ".html", ".md",
        ".cpp", ".c", ".h", ".py", ".js", ".java", ".css", ".ts",
        ".tex", ".srt", ".vtt", ".ass", ".cfg", ".ini", ".yml"
    };
    return find(compressExts.begin(), compressExts.end(), e) != compressExts.end();
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
            string ext = filesystem::path(inputPath).extension().string();
            cout << "Compressing: " << inputPath << " → " << outputPath << endl;
            if (isCompressibleExtension(ext)) {
                cout << "Mode: Huffman compression (text/source-code)\n";
                compressFile(inputPath, outputPath);
            } else {
                cout << "Mode: Storing raw bytes (no compression)\n";
                storeRawFile(inputPath, outputPath);
                cout << "Stored without compression.\n";
            }
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

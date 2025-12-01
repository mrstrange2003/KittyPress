// archive.cpp
#include "archive.h"
#include "huffman.h"
#include "kitty.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <cstdint>

using namespace std;
namespace fs = std::__fs::filesystem;

static void gatherFiles(const fs::path& base, const fs::path& p,
                        vector<ArchiveInput>& list) {

    if (fs::is_directory(p)) {
        for (auto& e : fs::recursive_directory_iterator(p)) {
            if (fs::is_regular_file(e.path())) {
                string name = e.path().filename().string();
                string ext = e.path().extension().string();
                if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);

                list.push_back({
                                       e.path().string(),
                                       fs::relative(e.path(), base).string(),
                                       ext
                               });
            }
        }
    } else if (fs::is_regular_file(p)) {

        string name = p.filename().string();
        string ext = p.extension().string();
        if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);

        list.push_back({
                               p.string(),
                               name,
                               ext
                       });
    }
}


void createArchive(const vector<string>& inputs, const string& outputArchive) {
    vector<ArchiveInput> files;
    for (auto& in : inputs)
        gatherFiles(fs::absolute(in).parent_path(), fs::absolute(in), files);

    ofstream out(outputArchive, ios::binary);
    if (!out) throw runtime_error("Cannot open output archive");

    out.write(KITTY_MAGIC_V4.c_str(), KITTY_MAGIC_V4.size());
    uint8_t ver = 4;
    out.write((char*)&ver, 1);
    uint32_t count = (uint32_t)files.size();
    out.write((char*)&count, 4);

    cout << "Creating archive with " << count << " file(s)\n";

    for (auto& f : files) {
        ifstream in(f.absPath, ios::binary);
        if (!in) throw runtime_error("Cannot open input: " + f.absPath);

        vector<uint8_t> data((istreambuf_iterator<char>(in)),
                             istreambuf_iterator<char>());
        in.close();

        string tmpOut = f.absPath + ".tmpkitty";
        compressFile(f.absPath, tmpOut);

        ifstream comp(tmpOut, ios::binary);
        vector<uint8_t> stored((istreambuf_iterator<char>(comp)),
                               istreambuf_iterator<char>());
        comp.close();
        fs::remove(tmpOut);

        uint16_t pathLen = (uint16_t)f.relPath.size();
        uint8_t flags = 1;
        uint64_t origSize = data.size();
        uint64_t dataSize = stored.size();
        uint16_t extLen = (uint16_t)f.ext.size();

        out.write((char*)&pathLen, 2);
        out.write(f.relPath.c_str(), pathLen);
        out.write((char*)&flags, 1);
        out.write((char*)&origSize, 8);
        out.write((char*)&dataSize, 8);

        // NEW: store extension
        out.write((char*)&extLen, 2);
        if (extLen > 0) out.write(f.ext.c_str(), extLen);

        out.write((char*)stored.data(), dataSize);

        cout << "  + " << f.relPath << " (" << origSize << " → " << dataSize << ")\n";
    }

    out.close();
    cout << "Archive created: " << outputArchive << endl;
}



std::string extractArchive(const std::string& archivePath, const std::string& outputFolder) {
    std::ifstream in(archivePath, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open archive");

    std::string magic(4, '\0');
    in.read(&magic[0], 4);
    if (magic != KITTY_MAGIC_V4)
        throw std::runtime_error("Not a KP04 archive");

    uint8_t ver;
    in.read(reinterpret_cast<char*>(&ver), 1);
    uint32_t count;
    in.read(reinterpret_cast<char*>(&count), 4);

    struct Entry {
        std::string rel;
        std::string ext;        // stored extension (may be empty)
        uint8_t flags;
        uint64_t origSize;
        uint64_t dataSize;
        std::vector<uint8_t> buf;
    };

    std::vector<Entry> entries;
    entries.reserve(count);
    std::vector<std::string> relPaths;
    relPaths.reserve(count);

    for (uint32_t i = 0; i < count; ++i) {
        uint16_t pathLen;
        in.read(reinterpret_cast<char*>(&pathLen), 2);

        std::string rel(pathLen, '\0');
        in.read(&rel[0], pathLen);

        uint8_t flags;
        in.read(reinterpret_cast<char*>(&flags), 1);

        uint64_t origSize = 0, dataSize = 0;
        in.read(reinterpret_cast<char*>(&origSize), 8);
        in.read(reinterpret_cast<char*>(&dataSize), 8);

        // read stored extension
        uint16_t extLen = 0;
        in.read(reinterpret_cast<char*>(&extLen), 2);
        std::string ext(extLen, '\0');
        if (extLen > 0) in.read(&ext[0], extLen);

        std::vector<uint8_t> buf(dataSize);
        in.read(reinterpret_cast<char*>(buf.data()), dataSize);

        entries.push_back({ rel, ext, flags, origSize, dataSize, std::move(buf) });
        relPaths.push_back(rel);
    }

    in.close();

    // Decide extraction root
    std::string finalRootName;

    if (entries.empty()) {
        finalRootName = "KittyPress_Empty";
    } else if (entries.size() == 1) {
        // single entry -> create single file named KittyPress_<filename.ext>
        fs::path p(relPaths[0]);
        std::string filename = p.filename().string();

        // if extension was stored, ensure filename has it
        if (!entries[0].ext.empty()) {
            // replace extension with stored ext (to be safe)
            p.replace_extension("." + entries[0].ext);
            filename = p.filename().string();
        }

        finalRootName = "KittyPress_" + filename;
        fs::path outRoot = fs::path(outputFolder) / finalRootName;
        fs::create_directories(outRoot.parent_path());

        // extract single entry directly as the finalRootName path
        auto &e = entries[0];
        fs::path outPath = fs::path(outputFolder) / finalRootName;

        std::string tmp = outPath.string() + ".tmpkitty";
        std::ofstream tmpf(tmp, std::ios::binary);
        tmpf.write(reinterpret_cast<char*>(e.buf.data()), e.dataSize);
        tmpf.close();

        decompressFile(tmp, outPath.string());
        fs::remove(tmp);

        return finalRootName;
    } else {
        // multiple entries -> detect if all share a single top-level folder
        auto splitTop = [](const std::string &s) -> std::string {
            size_t pos = s.find('/');
            if (pos == std::string::npos) return "";
            return s.substr(0, pos);
        };

        // C++17-compatible starts_with helper
        auto starts_with = [](const std::string& s, const std::string& prefix) {
            return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
        };

        std::string firstTop = splitTop(relPaths[0]);
        bool allSameTop = !firstTop.empty();
        if (allSameTop) {
            for (size_t i = 1; i < relPaths.size(); ++i) {
                if (!starts_with(relPaths[i], firstTop + "/")) {
                    allSameTop = false;
                    break;
                }
            }
        }

        if (allSameTop) {
            finalRootName = "KittyPress_" + firstTop;
        } else {
            finalRootName = "KittyPress_Files";
        }
    }

    // Create extraction root directory
    fs::path rootOut = fs::path(outputFolder) / finalRootName;
    fs::create_directories(rootOut);

    // Extract every entry under rootOut preserving relative path. If stored extension present,
    // ensure output file has that extension.
    for (auto &e : entries) {
        fs::path relp(e.rel);
        fs::path outPath = rootOut / relp;

        // if entry has stored ext, replace extension
        if (!e.ext.empty()) {
            outPath.replace_extension("." + e.ext);
        }

        fs::create_directories(outPath.parent_path());

        std::string tmp = outPath.string() + ".tmpkitty";
        std::ofstream tmpf(tmp, std::ios::binary);
        tmpf.write(reinterpret_cast<char*>(e.buf.data()), e.dataSize);
        tmpf.close();

        decompressFile(tmp, outPath.string());
        fs::remove(tmp);
    }

    return finalRootName; // return root folder name
}

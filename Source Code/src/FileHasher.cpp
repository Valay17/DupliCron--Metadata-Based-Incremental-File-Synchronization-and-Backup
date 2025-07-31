#include "FileHasher.hpp"
#include "Logger.hpp"
#include "ConfigGlobal.hpp"
#include "Blake3/blake3.h"
#include <thread>
#include <future>
#include <cstring>
#include <vector>
#include <string>
#include <iostream>

void FileHasher::HashFiles(std::vector<FileInfo>& files)
{
    Log.Info(std::string("Starting hashing of ") + std::to_string(files.size()) + std::string(" files using ") + std::to_string(ConfigGlobal::ThreadCount) + std::string(" threads."));
    std::vector<std::future<void>> futures;

    size_t filesCount = files.size();
    size_t chunkSize = (filesCount + ConfigGlobal::ThreadCount - 1) / ConfigGlobal::ThreadCount;

    for (size_t t = 0; t < ConfigGlobal::ThreadCount; ++t) // Divide the files into chunks for multi-threaded hashing.
    {
        size_t start = t * chunkSize;
        if (start >= filesCount) break;

        size_t end = std::min(start + chunkSize, filesCount);

        futures.emplace_back(std::async(std::launch::async, [this, &files, start, end]() {
        for (size_t i = start; i < end; ++i)
        {
            HashSingleFile(files[i]);
        }
        }));
    }
    for (auto& f : futures) f.get();
}

void FileHasher::HashSingleFile(FileInfo& file)
{
    std::vector<uint8_t> buffer;

    const std::string& path = file.AbsolutePath;
    buffer.insert(buffer.end(), path.begin(), path.end());

    uint64_t size = file.Size;
    time_t mtime = file.MTime;

    const uint8_t* sizePtr = reinterpret_cast<const uint8_t*>(&size);
    buffer.insert(buffer.end(), sizePtr, sizePtr + sizeof(size));

    const uint8_t* mtimePtr = reinterpret_cast<const uint8_t*>(&mtime);
    buffer.insert(buffer.end(), mtimePtr, mtimePtr + sizeof(mtime));

    uint8_t outHash[16] = { 0 };
    
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, buffer.data(), buffer.size());
    blake3_hasher_finalize(&hasher, outHash, sizeof(outHash));

    std::memcpy(file.Hash.data(), outHash, sizeof(outHash));
}


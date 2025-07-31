#pragma once

#include <vector>
#include <cstdint>
#include "MetaDataCache.hpp"
#include "ConfigGlobal.hpp"

class FileHasher
{
public:
    void HashFiles(std::vector<FileInfo>& files);
    void HashSingleFile(FileInfo& file);

private:
    size_t ThreadCount = ConfigGlobal::ThreadCount;
};
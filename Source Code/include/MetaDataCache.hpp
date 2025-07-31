#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <array>
#include <mutex>
#include <cstdint>

#include "FileScanner.hpp"
#include "Logger.hpp"

struct FileInfo
{
    std::string AbsolutePath;
    uint64_t Size = 0;
    uint64_t MTime = 0;
    std::array<uint8_t, 16> Hash{};
    bool Visited = false;
    int MissCount = 0;
    //Can use Bitfields if you are adventurous enough(not using currently because saving 3 bytes per entry is not something I want to deal with)
    //Not to mention that since the string is not of fixed length, the rounding off during byte allocation may cause loss of bits
};

class MetaDataCache
{
public:

    MetaDataCache() = default;
    explicit MetaDataCache(const std::string& cacheFilePath);

    void UpdateCacheForSource(const std::string& sourcePath, const std::vector<ScannedFileInfo>& scannedFiles);
    void LoadIndex(std::unordered_map<std::string, uint32_t>& PathToID, std::unordered_map<uint32_t, std::string>& IDToPath);
    void MarkVisited(const std::string& path);

    bool Load(uint32_t MetaDataCacheBinFileNumber);
    bool Save(uint32_t MetaDataCacheBinFileNumber);
    bool LoadCopiedState();
    bool SaveCopiedState();

    bool HasEntry(const std::string& path) const;
    void UpdateEntry(const std::string& path, const FileInfo& info);

    void ResetCopiedFlags();
    void RemoveStaleEntries(int maxMissCount);

    void MarkCopied(uint32_t sourceId);
    bool IsCopied(uint32_t sourceId) const;
    
    const std::unordered_map<uint32_t, bool>& GetCopiedMap() const;
    std::unordered_map<std::string, FileInfo> GetAllEntries() const;
    FileInfo GetEntry(const std::string& path) const;
    uint32_t GetOrAddDestinationID();
    std::string GetPathFromSourceID(uint32_t sourceID);

private:
    
    mutable std::mutex MetaCacheMutex;

    std::string CacheFilePath;

    std::unordered_map<std::string, FileInfo> Entries;
    std::unordered_map<uint32_t, bool> IDCopiedFlag;

    void EnsureCacheDirExists();
    void LoadDestinationIndex(std::unordered_map<std::string, uint32_t>& PathToID, std::unordered_map<uint32_t, std::string>& IDToPath);
    void SaveDestinationIndex(const std::unordered_map<std::string, uint32_t>& PathToID);
    void SaveIndex(const std::unordered_map<std::string, uint32_t>& PathToID);
};
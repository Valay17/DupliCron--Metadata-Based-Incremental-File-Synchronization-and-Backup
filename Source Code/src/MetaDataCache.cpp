#include "MetaDataCache.hpp"
#include "FileHasher.hpp"
#include "SyncEngine.hpp"
#include "ConfigGlobal.hpp"
#include "FileCopier.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>

namespace FS = std::filesystem;
std::mutex IndexMutex;

template<typename T>
bool ReadBinary(std::ifstream& stream, T& value)
{
    return static_cast<bool>(stream.read(reinterpret_cast<char*>(&value), sizeof(T)));
}

template<typename T>
bool WriteBinary(std::ofstream& stream, const T& value)
{
    return static_cast<bool>(stream.write(reinterpret_cast<const char*>(&value), sizeof(T)));
}

MetaDataCache::MetaDataCache(const std::string& cacheFilePath) : CacheFilePath(cacheFilePath)
{
    EnsureCacheDirExists();
}

void MetaDataCache::EnsureCacheDirExists()
{
    auto dir = FS::path(CacheFilePath).parent_path();
    if (!dir.empty() && !FS::exists(dir))
    {
        try
        {
            FS::create_directories(dir);
        }
        catch (const FS::filesystem_error& e)
        {
            std::cerr << "MetadataCache: Failed to create cache directory: " << e.what() << "\n";
            Log.Error(std::string("[MetadataCache]: Failed to create cache directory : ") + e.what());
        }
    }
}

// Loads cache entries from the binary file into memory
bool MetaDataCache::Load(uint32_t MetaDataCacheBinFileNumber)
{
    std::lock_guard lock(MetaCacheMutex);
    Entries.clear();
    std::string MetaCacheLoadFilePath = (ConfigGlobal::DestinationCacheDir / (std::to_string(MetaDataCacheBinFileNumber) + ".bin")).string();
    std::ifstream file(MetaCacheLoadFilePath, std::ios::binary);
    if (!file)
    {
        Log.Info(std::string("[MetaDataCache::Load] Starting Fresh. No Cache File Found at: ") + MetaCacheLoadFilePath);
        // File missing is OK, just start fresh
        return true;
    }

    Log.Info(std::string("[MetaDataCache::Load] Loading from: ") + MetaCacheLoadFilePath);

    while (file)
    {
        uint32_t pathLen = 0;
        if (!ReadBinary(file, pathLen)) break;
        if (pathLen == 0 || pathLen > 4096) // sanity check max path length
        {
            Log.Info(std::string("[MetaDataCache::Load]: Invalid Path Length in Cache"));
            return false;
        }

        std::string path(pathLen, '\0');
        if (!file.read(&path[0], pathLen))
        {
            Log.Info(std::string("[MetaDataCache::Load]: Failed to Read Path String"));
            return false;
        }

        FileInfo info;
        info.AbsolutePath = std::move(path);

        if (!ReadBinary(file, info.Size)) return false;
        if (!ReadBinary(file, info.MTime)) return false;
        if (!file.read(reinterpret_cast<char*>(info.Hash.data()), info.Hash.size())) return false;
        if (!ReadBinary(file, info.Visited)) return false;
        if (!ReadBinary(file, info.MissCount)) return false;

        /*std::cout << "[MetaDataCache::Load] Loaded entry:\n";
        std::cout << "  Path: " << info.AbsolutePath << "\n";
        std::cout << "  Size: " << info.Size << "\n";
        std::cout << "  MTime: " << info.MTime << "\n";
        std::cout << "  Hash: ";
        for (unsigned char byte : info.Hash)
        {
            printf("%02X", byte);
        }
        std::cout << "\n";*/

        Entries.emplace(info.AbsolutePath, std::move(info));
    }
    Log.Info(std::string("[MetaDataCache::Load] Finished Loading ") + std::to_string(Entries.size()) + std::string(" entries."));
    return true;
}

// Serializes and writes all in-memory cache entries to the binary file
bool MetaDataCache::Save(uint32_t MetaDataCacheBinFileNumber)
{
    std::lock_guard lock(MetaCacheMutex);
    std::string MetaCacheSaveFilePath = (ConfigGlobal::DestinationCacheDir / (std::to_string(MetaDataCacheBinFileNumber) + ".bin")).string();
    std::ofstream file(MetaCacheSaveFilePath, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        Log.Error(std::string("[MetadataCache::Save]: Failed to Open Cache File for Writing: ") + MetaCacheSaveFilePath);
        return false;
    }

    for (const auto& [path, info] : Entries)
    {
        uint32_t pathLen = static_cast<uint32_t>(path.size());
        if (!WriteBinary(file, pathLen)) return false;
        if (!file.write(path.data(), pathLen)) return false;
        if (!WriteBinary(file, info.Size)) return false;
        if (!WriteBinary(file, info.MTime)) return false;
        if (!file.write(reinterpret_cast<const char*>(info.Hash.data()), info.Hash.size())) return false;
        if (!WriteBinary(file, info.Visited)) return false;
        if (!WriteBinary(file, info.MissCount)) return false;
    }
    file.flush();
    Log.Info(std::string("[MetaDataCache::Save] Successfully Saved Cache Entries: ") + MetaCacheSaveFilePath);
    return true;
}

bool MetaDataCache::HasEntry(const std::string& path) const
{
    std::lock_guard lock(MetaCacheMutex);
    return Entries.find(path) != Entries.end();
}

FileInfo MetaDataCache::GetEntry(const std::string& path) const
{
    std::lock_guard lock(MetaCacheMutex);
    auto it = Entries.find(path);
    return (it != Entries.end()) ? it->second : FileInfo{};
}

void MetaDataCache::MarkVisited(const std::string& path)
{
    std::lock_guard lock(MetaCacheMutex);
    /*std::cout << "[MarkVisited] Looking for: " << path << "\n";
    Log.Info(std::string("[MarkVisited] Marked as visited: ") + path);
    std::cout << "[MarkVisisted] Current cache keys:\n";
    for (const auto& [k, v] : Entries)
    {
        std::cout << "  -> " << k << "\n";
    }*/
    auto it = Entries.find(path);
    if (it != Entries.end())
    {
        it->second.Visited = true;
        it->second.MissCount = 0;
    }
}

void MetaDataCache::UpdateEntry(const std::string& path, const FileInfo& info)
{
    std::lock_guard lock(MetaCacheMutex);

    /*std::cout << "[UpdateEntry] Looking for: " << path << "\n";
    Log.Info(std::string("[UpdateEntry] Updated cache entry: ") + path);
    std::cout << "[UpdateEntry] Current cache keys:\n";
    for (const auto& [k, v] : Entries)
    {
        std::cout << "  -> " << v.Copied << "\n";
    }*/

    auto it = Entries.find(path);
    if (it != Entries.end())
    {
        it->second = info;
        it->second.Visited = true;
        it->second.MissCount = 0;
    }
    else
    {
        // Insert new entry
        FileInfo newInfo = info;
        newInfo.Visited = true;
        newInfo.MissCount = 0;
        Entries.emplace(path, std::move(newInfo));
    }
}

void MetaDataCache::RemoveStaleEntries(int maxMissCount)
{
    std::lock_guard lock(MetaCacheMutex);
    for (auto it = Entries.begin(); it != Entries.end(); )
    {
        if (!it->second.Visited)
        {
            it->second.MissCount++;
            if (it->second.MissCount > maxMissCount)
            {
                if (ConfigGlobal::DeleteStaleFromDest)
                {
                    FileCopier::DeleteStaleFromDestination(it->first);
                }
                Log.Info(std::string("[RemoveStaleEntries] Deleted Stale Entry: ") + it->first);
                it = Entries.erase(it);
                continue;
            }
        }
        else
        {
            it->second.MissCount = 0;
            it->second.Visited = false; // reset for next run
        }
        ++it;
    }
}
/*
void MetaDataCache::ResetVisitedFlags()
{
    for (auto& [path, info] : Entries)
    {
        info.Visited = false;
    }
}*/

std::unordered_map<std::string, FileInfo> MetaDataCache::GetAllEntries() const
{
    std::lock_guard lock(MetaCacheMutex);
    return Entries;
}

// Load copy status flags for BinIDs
bool MetaDataCache::LoadCopiedState()
{
    std::lock_guard lock(MetaCacheMutex);
    IDCopiedFlag.clear();

    std::ifstream file(ConfigGlobal::StateIndexFileName, std::ios::binary);

    if (!file)
    {
        // No file yet is not an error(first run)
        return true;
    }

    uint32_t count = 0;
    file.read(reinterpret_cast<char*>(&count), sizeof(count));
    if (!file)
    {
        return false;
    }

    for (uint32_t i = 0; i < count; ++i)
    {
        uint32_t id = 0;
        bool copied = false;

        file.read(reinterpret_cast<char*>(&id), sizeof(id));
        file.read(reinterpret_cast<char*>(&copied), sizeof(copied));

        if (!file)
        {
            return false;
        }

        IDCopiedFlag[id] = copied;
    }
    Log.Info(std::string("[LoadCopiedState] Loaded ") + std::to_string(IDCopiedFlag.size()) + std::string(" entries."));
    return true;
}

// Save copy status flags for BinIDs
bool MetaDataCache::SaveCopiedState()
{
    std::lock_guard lock(MetaCacheMutex);
    std::ofstream file(ConfigGlobal::StateIndexFileName, std::ios::binary | std::ios::trunc);
    if (!file) return false;

    uint32_t count = static_cast<uint32_t>(IDCopiedFlag.size());
    file.write(reinterpret_cast<const char*>(&count), sizeof(count));
    if (!file)
    {
        return false;
    }

    for (const auto& [id, copied] : IDCopiedFlag)
    {
        file.write(reinterpret_cast<const char*>(&id), sizeof(id));
        file.write(reinterpret_cast<const char*>(&copied), sizeof(copied));
        if (!file)
        {
            return false;
        }
    }
    Log.Info(std::string("[SaveCopiedState] Saved ") + std::to_string(IDCopiedFlag.size()) + std::string(" entries."));
    return true;
}

void MetaDataCache::ResetCopiedFlags()
{
    LoadCopiedState();
    {
        std::lock_guard lock(MetaCacheMutex);
        for (auto& entry : IDCopiedFlag)
        {
            entry.second = false;
        }
    }
    SaveCopiedState();
    Log.Info(std::string("[ResetCopiedFlags] Copy Flags Reset"));
}
/*
bool MetaDataCache::ResetCopiedFlagFor(uint32_t BinID)
{
    LoadCopiedState();
    bool updated = false;
    {
        std::lock_guard lock(MetaCacheMutex);

        auto it = IDCopiedFlag.find(BinID);
        if (it != IDCopiedFlag.end())
        {
            it->second = false;
            updated = true;
        }
    }
    
    if (updated)
    {
        Log.Info(std::string("[ResetCopiedFlagFor] Copy Flag Reset for BinID = ") + std::to_string(BinID));
        return SaveCopiedState();
    }
    // Optional: insert new ID with false
    // IDCopiedFlag[BinID] = false;
    // return SaveCopiedState();
    return false;
} */

void MetaDataCache::MarkCopied(uint32_t BinID)
{
    LoadCopiedState();
    {
        std::lock_guard lock(MetaCacheMutex);
        IDCopiedFlag[BinID] = true;
    }
    SaveCopiedState();
    Log.Info(std::string("[MarkCopied] Copy Flag Set to True for BinID = ") + std::to_string(BinID));
}

bool MetaDataCache::IsCopied(uint32_t BinID) const
{
    std::lock_guard lock(MetaCacheMutex);
    auto it = IDCopiedFlag.find(BinID);
    return it != IDCopiedFlag.end() && it->second;
}

const std::unordered_map<uint32_t, bool>& MetaDataCache::GetCopiedMap() const
{
    return IDCopiedFlag;
}

// Load Index storing Destination Path and Destination Cache Folder
void MetaDataCache::LoadDestinationIndex(std::unordered_map<std::string, uint32_t>& PathToID, std::unordered_map<uint32_t, std::string>& IDToPath)
{
    PathToID.clear();
    IDToPath.clear();

    std::ifstream file(ConfigGlobal::DestinationIndexFileName, std::ios::binary);
    if (!file) return;

    uint32_t count = 0;
    if (!ReadBinary(file, count)) return;

    for (uint32_t i = 0; i < count; ++i)
    {
        uint32_t id = 0;
        uint32_t pathLen = 0;

        if (!ReadBinary(file, id)) break;
        if (!ReadBinary(file, pathLen)) break;

        std::string path(pathLen, '\0');
        if (!file.read(&path[0], pathLen)) break;

        PathToID[path] = id;
        IDToPath[id] = path;
    }
    Log.Info(std::string("[LoadDestinationIndex] Loaded Index"));
}

// Save Index storing Destination Path and Destination Cache Folder
void MetaDataCache::SaveDestinationIndex(const std::unordered_map<std::string, uint32_t>& PathToID)
{
    FS::path indexPath = ConfigGlobal::DestinationIndexFileName;
    if (!FS::exists(indexPath.parent_path()))
    {
        FS::create_directories(indexPath.parent_path());
    }
    
    std::ofstream file(ConfigGlobal::DestinationIndexFileName, std::ios::binary | std::ios::trunc);
    if (!file) return;

    uint32_t count = static_cast<uint32_t>(PathToID.size());
    WriteBinary(file, count);

    for (const auto& [path, id] : PathToID)
    {
        WriteBinary(file, id);
        uint32_t len = static_cast<uint32_t>(path.size());
        WriteBinary(file, len);
        file.write(path.data(), len);
    }
    file.flush();
    Log.Info(std::string("[SaveDestinationIndex] Saved Index"));
}

uint32_t MetaDataCache::GetOrAddDestinationID()
{
    std::unordered_map<std::string, uint32_t> PathToID;
    std::unordered_map<uint32_t, std::string> IDToPath;
    LoadDestinationIndex(PathToID, IDToPath);

    uint32_t id = 0;
    if (PathToID.count(ConfigGlobal::DestinationPath))
    {
        id = PathToID[ConfigGlobal::DestinationPath];
    }
    else
    {
        id = static_cast<uint32_t>(PathToID.size() + 1);
        PathToID[ConfigGlobal::DestinationPath] = id;
        IDToPath[id] = ConfigGlobal::DestinationPath;
        SaveDestinationIndex(PathToID);
    }
    Log.Info(std::string("[DestinationID] ID assigned to Destination: ") + std::to_string(id));
    return id;
}

// Load Index storing Source Path and BinID
void MetaDataCache::LoadIndex(std::unordered_map<std::string, uint32_t>& PathToID,std::unordered_map<uint32_t, std::string>& IDToPath)
{
    PathToID.clear();
    IDToPath.clear();

    std::ifstream file(ConfigGlobal::IndexFileName, std::ios::binary);
    if (!file) return;

    uint32_t count = 0;
    if (!ReadBinary(file, count)) return;

    for (uint32_t i = 0; i < count; ++i)
    {
        uint32_t id = 0;
        uint32_t pathLen = 0;

        if (!ReadBinary(file, id)) break;
        if (!ReadBinary(file, pathLen)) break;

        std::string path(pathLen, '\0');
        if (!file.read(&path[0], pathLen)) break;

        PathToID[path] = id;
        IDToPath[id] = path;
    }
}

// Save Index storing Source Path and BinID
void MetaDataCache::SaveIndex(const std::unordered_map<std::string, uint32_t>& PathToID)
{
    std::ofstream file(ConfigGlobal::IndexFileName, std::ios::binary | std::ios::trunc);
    if (!file) return;

    uint32_t count = static_cast<uint32_t>(PathToID.size());
    WriteBinary(file, count);
    
    for (const auto& [path, id] : PathToID)
    {
        WriteBinary(file, id);
        uint32_t len = static_cast<uint32_t>(path.size());
        WriteBinary(file, len);
        file.write(path.data(), len);
    }
    file.flush();
}

std::string MetaDataCache::GetPathFromSourceID(uint32_t sourceID)
{
    std::unordered_map<std::string, uint32_t> PathToID;
    std::unordered_map<uint32_t, std::string> IDToPath;
    LoadIndex(PathToID, IDToPath);

    auto it = IDToPath.find(sourceID);
    if (it != IDToPath.end())
    {
        return it->second;
    }

    return {}; // Return empty string if not found
}

void MetaDataCache::UpdateCacheForSource(const std::string& sourcePath, const std::vector<ScannedFileInfo>& scannedFiles)
{
    // Assign ID if new
    uint32_t id = 0;
    {
        std::lock_guard lock(IndexMutex);
        std::unordered_map<std::string, uint32_t> PathToID;
        std::unordered_map<uint32_t, std::string> IDToPath;
        LoadIndex(PathToID, IDToPath);

        if (PathToID.count(sourcePath))
        {
            id = PathToID[sourcePath];
        }
        else
        {
            id = static_cast<uint32_t>(PathToID.size() + 1);
            PathToID[sourcePath] = id;
            IDToPath[id] = sourcePath;
            SaveIndex(PathToID);
        }
    }

    std::string cacheFilePath = (ConfigGlobal::DestinationCacheDir / (std::to_string(id) + ".bin")).string();

    MetaDataCache cache(cacheFilePath);
    if (!cache.Load(id))
    {
        Log.Info(std::string("[UpdateCacheForSource] Failed to Load Cache File ]") + cacheFilePath);
    }
    else
    {
        Log.Info(std::string("[UpdateCacheForSource] Cache Loaded Successfully."));
    }

    FileHasher Hasher;
    std::vector<FileInfo> freshFiles;
    for (const auto& file : scannedFiles)
    {
        FileInfo info;
        info.AbsolutePath = file.RelativePath;
        info.Size = file.Size;
        info.MTime = file.MTime;
        freshFiles.push_back(std::move(info));
    }
    Hasher.HashFiles(freshFiles);
    Log.Info(std::string("Completed Hashing for Source: ") + sourcePath);
    SyncEngine::Sync(std::move(freshFiles), cache, id);
}
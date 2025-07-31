#include "FailureDetect.hpp"
#include "ConfigParser.hpp"
#include "MetaDataCache.hpp"
#include "FileCopier.hpp"
#include "FileHasher.hpp"
#include "ConfigGlobal.hpp"
#include "FileScanner.hpp"
#include "Logger.hpp"

#include <iostream>
#include <queue>
#include <filesystem>
#include <fstream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace FailureDetect
{
    bool MarkFailure()
    {
        std::error_code ec;
        std::filesystem::remove(ConfigGlobal::SuccessFile, ec); // ignore error

        std::ofstream ofs(ConfigGlobal::FailureFile, std::ios::trunc);
        if (!ofs.good())
            return false;

#ifdef _WIN32
        DWORD attrs = GetFileAttributesW(ConfigGlobal::FailureFile.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES)
            return false;
        //Mark as Hidden File for Windows
        attrs |= FILE_ATTRIBUTE_HIDDEN;
        if (!SetFileAttributesW(ConfigGlobal::FailureFile.c_str(), attrs))
            return false;
#endif

        return true;
    }
    
    bool MarkSuccess()
    {
        std::error_code ec;
        std::filesystem::remove(ConfigGlobal::FailureFile, ec); // ignore error

        std::ofstream ofs(ConfigGlobal::SuccessFile, std::ios::trunc);
        if (!ofs.good())
            return false;

#ifdef _WIN32
        DWORD attrs = GetFileAttributesW(ConfigGlobal::SuccessFile.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES)
            return false;
        //Mark as Hidden File for Windows
        attrs |= FILE_ATTRIBUTE_HIDDEN;
        if (!SetFileAttributesW(ConfigGlobal::SuccessFile.c_str(), attrs))
            return false;
#endif

        return true;
    }
    
    bool WasLastSuccess()
    {
        return std::filesystem::exists(ConfigGlobal::SuccessFile);
    }
    
    bool WasLastFailure()
    {
        return std::filesystem::exists(ConfigGlobal::FailureFile);
    }

    void CheckCacheIntegrity()
    {
        MetaDataCache Meta(ConfigGlobal::CacheDir);
        ConfigGlobal::DestinationIndexFileName = std::filesystem::path(ConfigGlobal::CacheDir) / "DestinationIndex.bin";
        ConfigGlobal::DestinationID = Meta.GetOrAddDestinationID();

        ConfigGlobal::DestinationCacheDir = std::filesystem::path(ConfigGlobal::CacheDir) / std::to_string(ConfigGlobal::DestinationID);
        ConfigGlobal::StateIndexFileName = ConfigGlobal::DestinationCacheDir / "State.bin";
        ConfigGlobal::IndexFileName = ConfigGlobal::DestinationCacheDir / "Index.bin";
        ConfigGlobal::FailureFile = ConfigGlobal::DestinationCacheDir / ".Failure";
        ConfigGlobal::SuccessFile = ConfigGlobal::DestinationCacheDir / ".Success";

        if (!std::filesystem::exists(ConfigGlobal::DestinationCacheDir))
        {
            try
            {
                std::filesystem::create_directories(ConfigGlobal::DestinationCacheDir);
                Log.Info("Destination cache folder not found. Assuming this is the first run.");
            }
            catch (const std::filesystem::filesystem_error& e)
            {
                Log.Error(std::string("[UpdateCacheForSource]: Failed to Create Cache Directory: ") +
                    ConfigGlobal::DestinationCacheDir.string() + ": " + e.what());
                throw; // Fail and propagate error
            }
        }
        else
        {
            bool HasFailure = std::filesystem::exists(ConfigGlobal::FailureFile);
            bool HasSuccess = std::filesystem::exists(ConfigGlobal::SuccessFile);
            bool HasIndex = std::filesystem::exists(ConfigGlobal::IndexFileName);
            bool HasState = std::filesystem::exists(ConfigGlobal::StateIndexFileName);
            if (!(HasFailure || HasSuccess) || !HasIndex || !HasState)
            {
                Log.Error("Destination cache exists but is missing critical files (state/index/failure/success).");
                std::cerr << "[Error] Incomplete or corrupt cache folder detected.\n";

                if (ConfigGlobal::EnableCacheRestoreFromBackup)
                {
                    std::filesystem::path BackupPath = std::filesystem::path(ConfigGlobal::DestinationPath) / ".BackupCache";
                    if (std::filesystem::exists(BackupPath))
                    {
                        try
                        {
                            std::filesystem::copy(BackupPath, ConfigGlobal::DestinationCacheDir, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
                            Log.Info(std::string("[EnableCacheRestoreFromBackup] Restored Cache from Backup: ") + BackupPath.string());
                            std::cout << "Cache Successfully Restored from Backup.\n";
                        }
                        catch (const std::filesystem::filesystem_error& e)
                        {
                            Log.Error(std::string("[EnableCacheRestoreFromBackup] Failed to restore cache from backup: ") + std::string(e.what()));
                            std::cerr << "[Fatal] Could not recover from backup. Exiting.\n";
                            std::exit(EXIT_FAILURE);
                        }
                    }
                    else
                    {
                        Log.Error(std::string("Backup folder not found at: ") + BackupPath.string());
                        std::cerr << "[Fatal] No backup cache available. Exiting.\n";
                        std::exit(EXIT_FAILURE);
                    }
                }
                else
                {
                    std::cerr << "[Fatal] Recovery disabled and cache is corrupt. Exiting.\n";
                    std::exit(EXIT_FAILURE);
                }
            }
            else
            {
                Log.Info("Destination Cache Structure Verified.");
            }
        }
    }

    bool RunFailureRecovery()
    {
        FileScanner Scanner;
        ConfigParser Parser;
        MetaDataCache Meta;
        FileHasher Hash;
        FileCopier Copier;
        
        Parser.Reset();

        if (!Parser.Parse(ConfigGlobal::ConfigFile))
        {
            for (const auto& Error : Parser.GetErrors())
            {
                std::cerr << "Config Error: " << Error << "\n";
                Log.Error(Error);
            }
            std::cerr << "Check Errors and Fix Them, Exiting Sync. \n";
            Log.Error("Check Errors and Fix Them, Exiting Sync");
            return false;
        }
        for (const auto& Info : Parser.GetInfos())
        {
            std::cout << "Config Info: " << Info << "\n";
            Log.Info(Info);
        }

        Log.Info("Config Parsed Successfully.");
        std::cout << "Config Parsed Successfully.\n";

        std::unordered_map<std::string, uint32_t> PathToID;
        std::unordered_map<uint32_t, std::string> IDToPath;

        Meta.LoadIndex(PathToID, IDToPath);
        
        std::string stateFilePath = (std::filesystem::path(ConfigGlobal::DestinationCacheDir) / "State.bin").string();
        MetaDataCache FailCopyStateCache(stateFilePath);
        if (!FailCopyStateCache.LoadCopiedState())
        {
            std::cerr << "Failed to Load Copy State File.\n";
            Log.Error(std::string("[Recovery] Failed to load copy state file"));
            return false;
        }
        
        std::vector<std::pair<std::string, uint32_t>> FailPendingSources;
        for (const auto& sourcePath : Parser.GetSources())
        {
            auto it = PathToID.find(sourcePath);
            if (it == PathToID.end())
            {
                std::cerr << "Source not found in index: " << sourcePath << "\n";
                Log.Info(std::string("[Recovery] Source not found in index: ") + sourcePath);
                Log.Info(std::string("Check if source was present in previous run, if this is first run failure, then no issue, if not then delete all caches from folder because cache is corrupt "));
                continue;
            }
            uint32_t sourceId = it->second;

            if (FailCopyStateCache.GetCopiedMap().find(sourceId) == FailCopyStateCache.GetCopiedMap().end() || !FailCopyStateCache.IsCopied(sourceId))
            {
                FailPendingSources.emplace_back(sourcePath, sourceId);
                std::cout << "Pending Source : " << sourcePath << "\n";
                Log.Info(std::string("[Recovery] Pending Source: ") + sourcePath);
            }
            else
            {
                std::cout << "Source Fully Copied, Skipping: " << sourcePath << "\n";
                Log.Info(std::string("[Recovery] Source Fully Copied, Skipping: ") + sourcePath);
            }
        }

        bool overallSuccess = true;
        for (const auto& [sourcePath, sourceId] : FailPendingSources)
        {
            std::cout << "Working on: " << sourcePath << "\n";
            Log.Info(std::string("[Recovery] Working on: ") + sourcePath);

            std::string cacheFile = (std::filesystem::path(ConfigGlobal::DestinationCacheDir) / (std::to_string(sourceId) + ".bin")).string();
            MetaDataCache FailSourceCache(cacheFile);
            if (!FailSourceCache.Load(sourceId))
            {
                Log.Info(std::string("[Recovery] Failed to load cache for source: ") + sourcePath);
                overallSuccess = false;
                continue;
            }

            std::cout << "Scanning: " << sourcePath << std::endl;
            Scanner.SetExcludes(Parser.GetExcludes());
            Scanner.Scan(sourcePath);
            const auto& scannedFiles = Scanner.GetFiles();

            std::vector<FileInfo> freshFiles;
            for (const auto& scanned : scannedFiles)
            {
                FileInfo info;
                info.AbsolutePath = scanned.RelativePath;
                info.Size = scanned.Size;
                info.MTime = scanned.MTime;
                freshFiles.push_back(std::move(info));
            }

            Hash.HashFiles(freshFiles);
            Log.Info(std::string("Completed Hashing for Source: ") + sourcePath);
            std::queue<FileInfo> FailCopyQueue;

            for (const auto& file : freshFiles)
            {
                const std::string& absPath = file.AbsolutePath;

                bool isNew = !FailSourceCache.HasEntry(absPath);
                bool isChanged = false;

                if (!isNew)
                {
                    FileInfo cached = FailSourceCache.GetEntry(absPath);
                    if (cached.Hash != file.Hash)
                    {
                        isChanged = true;
                    }
                }
                if (isNew || isChanged)
                {
                    Log.Info(std::string("[Sync Engine] Added to HDDCopyQueue: ") + absPath);
                    FailCopyQueue.emplace(file);
                }
                else
                {
                    Log.Info(std::string("[Sync Engine] File Skipped: ") + absPath);
                }
            }
            
            while (!FailCopyQueue.empty())
            {
                const FileInfo& file = FailCopyQueue.front();
                std::string SourceTopRootPath = FailCopyStateCache.GetPathFromSourceID(sourceId);
                bool copySuccess = Copier.PerformFileCopy(file.AbsolutePath, SourceTopRootPath);
                FailCopyQueue.pop();
            }

            // Final flush and update copy state
            FailCopyStateCache.MarkCopied(sourceId);
            std::cout << "Source Copied Successfully: \" "<< sourcePath << " \" \n";
            Log.Info(std::string("[Recovery] Source Copied Successfully:") + sourcePath);
        }
        if (overallSuccess)
        {
            std::cout << "All Sources Recovered Successfully.\n";
            Log.Info(std::string("[Recovery] All Sources Recovered Successfully."));
            MarkSuccess();
            return true;
        }
        else
        {
            std::cerr << "Recovery completed with some errors. Please check logs for missing/failed files.\n";
            Log.Error(std::string("[Recovery] Recovery completed with some errors."));
            // Keep failure state so next run tries again
            return false;
        }
    }
}
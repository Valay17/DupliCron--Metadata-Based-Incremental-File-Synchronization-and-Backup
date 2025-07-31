#pragma once

#include <string>
#include <filesystem>

namespace ConfigGlobal
{
    extern uint32_t DestinationID;
    extern std::string DestinationPath;
    extern std::string ConfigFile;
    extern std::string LogDir;
    extern std::string CacheDir;
    extern std::string Mode;
    extern std::string DiskType;
    extern std::string SSDMode;
    extern bool DeleteStaleFromDest;
    extern bool EnableCacheRestoreFromBackup;
    extern bool EnableBackupCopyAfterRun;
    extern bool DestinationTopFolderInsteadOfFullPath;

    extern unsigned short int MaxLogFiles;
    extern unsigned short int ThreadCount;
    extern unsigned short int GodSpeedParallelSourcesCount;
    extern unsigned short int GodSpeedParallelFilesPerSourcesCount;
    extern unsigned short int ParallelFilesPerSourceCount;
    extern unsigned short int StaleEntries;

    extern std::filesystem::path DestinationCacheDir;
    extern std::filesystem::path DestinationIndexFileName;
    extern std::filesystem::path StateIndexFileName;
    extern std::filesystem::path IndexFileName;
    extern std::filesystem::path FailureFile;
    extern std::filesystem::path SuccessFile;

    void InitializeDefaults();
}
#include "ConfigGlobal.hpp"

namespace ConfigGlobal
{
    uint32_t DestinationID;
    std::string DestinationPath;
    std::string ConfigFile;
    std::string LogDir;
    std::string CacheDir;
    std::string Mode;
    std::string DiskType;
    std::string SSDMode;
    bool DeleteStaleFromDest;
    bool EnableCacheRestoreFromBackup;
    bool EnableBackupCopyAfterRun;
    bool DestinationTopFolderInsteadOfFullPath;
    
    unsigned short int MaxLogFiles;
    unsigned short int ThreadCount;
    unsigned short int GodSpeedParallelSourcesCount;
    unsigned short int GodSpeedParallelFilesPerSourcesCount;
    unsigned short int ParallelFilesPerSourceCount;
    unsigned short int StaleEntries;

    std::filesystem::path DestinationCacheDir;
    std::filesystem::path DestinationIndexFileName;
    std::filesystem::path StateIndexFileName;
    std::filesystem::path IndexFileName;
    std::filesystem::path FailureFile;
    std::filesystem::path SuccessFile;

    void InitializeDefaults()
    {
        ConfigFile = "Config.txt"; //Can be replaced by absolute path, will work just time, just remember to use escape character(\\) for each sub directory as this is a string
        LogDir = "Sync_Logs"; //Same as above
        CacheDir = "Meta_Cache"; //Same as above
        DestinationID = 0; //Do Not Touch
        Mode = "BG";
        ThreadCount = 2;
        DiskType = "HDD";
        SSDMode = "Balanced";
        GodSpeedParallelSourcesCount = 8;
        GodSpeedParallelFilesPerSourcesCount = 8;
        ParallelFilesPerSourceCount = 8;
        StaleEntries = 5;
        DeleteStaleFromDest = false;
        EnableCacheRestoreFromBackup = true;
        EnableBackupCopyAfterRun = true;
        DestinationTopFolderInsteadOfFullPath = false;
        MaxLogFiles = 10;
    }
}
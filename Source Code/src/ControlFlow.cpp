#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>

#include "ControlFlow.hpp"
#include "Logger.hpp"
#include "ConfigGlobal.hpp"
#include "ThreadPool.hpp"
#include "SyncEngine.hpp"
#include "FailureDetect.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

int ControlFlow::Run()
{
    Log.Init(ConfigGlobal::LogDir);
    std::cout << "Starting DupliCron \n";

    if (!Parser.Parse(ConfigGlobal::ConfigFile))
    {
        for (const auto& Error : Parser.GetErrors())
        {
            std::cerr << "Config Error: " << Error << "\n";
            Log.Error(Error);
        }
        std::cerr << "Check Errors and Fix Them, Exiting Sync";
        Log.Error("Check Errors and Fix Them, Exiting Sync");
        return 1;
    }
    Log.Info("Config Parsed Successfully.");
    std::cout << "Config Parsed Successfully.\n";

    if (!FailureDetect::WasLastFailure() && !FailureDetect::WasLastSuccess())
    {
        FailureDetect::MarkFailure();
    }
    else if (FailureDetect::WasLastFailure())
    {
        std::cout << "Previous sync run did not complete successfully.\n";
        std::cout << "To resume, please provide the same source paths used in the previous run.\n";
        std::cout << "Refer to the logs for detailed information on the sources and destination involved.\n";
        std::cout << "Once you have updated the config with previous sources and destination, please type 'Continue' to proceed or ctrl + c to exit : ";
        std::string input;
        while (true)
        {
            std::getline(std::cin, input);
            if (input == "Continue" || input == "continue")
            {
                break;
            }
            std::cout << "Invalid input. Please type 'Continue' to proceed:\n";
        }
        std::cout << "Detected Previous Sync Incomplete. Triggering Recovery Mode.\n";
        Log.Info(std::string("Previous Sync Incomplete. Triggering Recovery Mode."));
        if (FailureDetect::RunFailureRecovery())
        {
            FailureDetect::MarkSuccess();
            Log.Info(std::string("Recovery Completed Successfully. Exiting."));
            std::cout << "Recovery Completed Successfully. Exiting....\n";
            std::exit(1);
        }
        else
        {
            std::cerr << "Recovery FAILED. Please check logs and fix errors.\n";
            Log.Info(std::string("Recovery FAILED"));
            std::exit(1);
        }
    }
    else if(FailureDetect::WasLastSuccess())
    {
        std::cout << "Last Sync Status - Success.\n";
        Log.Info(std::string("Last sync completed successfully."));
        FailureDetect::MarkFailure();
    }

    Log.CleanupOldLogs();
    LogSourcesDestExcludes();

    for (const auto& Info : Parser.GetInfos())
    {
        std::cout << "Config Info: " << Info << "\n";
        Log.Info(Info);
    }

    Log.Info("Scanning Sources...");
    std::cout << "Scanning Sources...\n";

    ThreadPool Pool(ConfigGlobal::ThreadCount);
    std::unordered_map<std::string, std::vector<ScannedFileInfo>> PerSourceResults;
    std::mutex ResultMutex;

    for (const auto& Source : Parser.GetSources())
    {
        Pool.Submit([Source, &PerSourceResults, &ResultMutex, this]()
        {
            std::cout << "Scanning: " << Source << std::endl;
            FileScanner LocalScanner;
            LocalScanner.SetExcludes(Parser.GetExcludes());
            LocalScanner.Scan(Source);
            std::lock_guard<std::mutex> Lock(ResultMutex);
            PerSourceResults[Source] = std::move(LocalScanner.GetFiles());
        });
    }
    Pool.Join();

    Log.Info("Scanning Sources Complete");
    std::cout << "Scanning Source Complete\n";

    LogScannedFiles();
    Meta.ResetCopiedFlags();
    
    Log.Info("Initiating Copying...");
    std::cout << "Initiating Copying...\n";

    if (ConfigGlobal::DiskType == "HDD")
    {
        HDDCopy.Start();
        SyncEngine::SetHDDCopyQueue(&HDDCopy); // Let SyncEngine access the copy system

        for (const auto& [Source, Files] : PerSourceResults)
        {
            HDDCopy.IncrementPendingSources();
            Pool.Submit([Source, Files, this]() {Meta.UpdateCacheForSource(Source, Files);});
        }
        Pool.Join();
        HDDCopy.MarkAllSourcesSubmitted();
        HDDCopy.WaitUntilDone();
        HDDCopy.Stop();
    }
    else
    {
        SSDCopy.Initialize(ToSSDMode(ConfigGlobal::SSDMode));
        SSDCopy.Start();
        SyncEngine::SetSSDCopyQueue(&SSDCopy); // Let SyncEngine access the copy system

        for (const auto& [Source, Files] : PerSourceResults)
        {
            SSDCopy.IncrementPendingSources();
            Pool.Submit([Source, Files, this]() {Meta.UpdateCacheForSource(Source, Files); });
        }
        Pool.Join();
        SSDCopy.MarkAllSourcesSubmitted();
        SSDCopy.WaitUntilDone();
        SSDCopy.Stop();
    }

    Log.Info("Copying Procedure Completed");
    std::cout << "Copying Procedure Completed\n";

    FailureDetect::MarkSuccess();

    if (ConfigGlobal::EnableBackupCopyAfterRun)
    {
        std::filesystem::path BackupPath = std::filesystem::path(ConfigGlobal::DestinationPath) / ".BackupCache";

        try
        {
            if (std::filesystem::exists(BackupPath))
            {
                std::filesystem::remove_all(BackupPath);
                Log.Info(std::string("[EnableBackupCopyAfterRun] Cleared Existing Backup Folder: ") + BackupPath.string());
            }

            std::filesystem::copy(ConfigGlobal::DestinationCacheDir, BackupPath, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);

#ifdef _WIN32
            DWORD attrs = GetFileAttributesW(BackupPath.wstring().c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES)
            {
                attrs |= FILE_ATTRIBUTE_HIDDEN;
                if (!SetFileAttributesW(BackupPath.wstring().c_str(), attrs))
                {
                    Log.Error(std::string("[EnableCacheBackupToDestination] Could not mark backup folder as hidden: ") + BackupPath.string());
                }
            }
            else
            {
                Log.Error(std::string("[EnableCacheBackupToDestination] Could not retrieve attributes for: ") + BackupPath.string());
            }
#endif

            Log.Info(std::string("[EnableBackupCopyAfterRun] Cache Successfully Backed Up to: ") + BackupPath.string());
            std::cout << "Cache Successfully Backed Up to Destination.\n";
        }
        catch (const std::exception& e)
        {
            Log.Error(std::string("[EnableBackupCopyAfterRun] Failed to backup cache to destination: ") + std::string(e.what()));
            std::cerr << "Failed to backup cache to destination: " << e.what() << "\n";
        }
    }

    std::cout << "Logs Saved to : " << Log.CurrentLogFilePath << "\n";
    std::cout << "Sync Complete \n";
    return 0;
}

void ControlFlow::LogSourcesDestExcludes()
{
    Log.Info("Sources:");
    for (const auto& Source : Parser.GetSources())
    {
        Log.Info("  " + Source);
    }

    Log.Info("Destination:");
    Log.Info("  " + ConfigGlobal::DestinationPath);

    if (Parser.GetExcludes().size() > 0)
    {
        Log.Info("Excludes:");
        for (const auto& Exclude : Parser.GetExcludes())
        {
            Log.Info("  " + Exclude);
        }
    }
}

void ControlFlow::LogScannedFiles()
{
    const auto& ScannedFiles = Scanner.GetFiles();

    for (const auto& Entry : ScannedFiles)
    {
        bool IsExcluded = false;
        for (const auto& Exclude : Parser.GetExcludes())
        {
            if (Entry.RelativePath.find(Exclude) != std::string::npos)
            {
                IsExcluded = true;
                break;
            }
        }

        std::string LogLine = Entry.RelativePath + " | " + std::to_string(Entry.Size) + " bytes | mtime: " + std::to_string(Entry.MTime);
        if (IsExcluded)
        {
            LogLine += " [EXCLUDED]";
        }

        std::cout << LogLine << "\n";
        Log.Info("Scanned: " + LogLine);
    }
}
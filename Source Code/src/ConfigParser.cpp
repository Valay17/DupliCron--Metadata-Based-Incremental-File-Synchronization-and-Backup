#include <thread>
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <unordered_set>

#include "ConfigParser.hpp"
#include "FileCopier.hpp"
#include "ConfigGlobal.hpp"
#include "FailureDetect.hpp"

namespace FS = std::filesystem;

const std::vector<std::string> ConfigParser::GetSources() const
{
    return Sources;
}

const std::vector<std::string>& ConfigParser::GetExcludes() const
{
    return Excludes;
}

const std::vector<std::string>& ConfigParser::GetErrors() const
{
    return Errors;
}

const std::vector<std::string>& ConfigParser::GetInfos() const
{
    return Infos;
}

void ConfigParser::Reset()
{
    Sources.clear();
    Excludes.clear();
    ConfigGlobal::DestinationPath.clear();
    Errors.clear();
    Infos.clear();
    
    ConfigGlobal::InitializeDefaults();
}

void ConfigParser::AddError(const std::string& Message)
{
    Errors.push_back(Message);
}

void ConfigParser::AddInfo(const std::string& Message)
{
    Infos.push_back(Message);
}

bool ConfigParser::IsAbsolutePath(const std::string& Path)
{
#ifdef _WIN32
    if (Path.size() >= 4 && Path.compare(0, 4, R"(\\.\)") == 0)
    {
        return false;
    }

    if (Path.size() >= 4 && Path.compare(0, 4, "\\\\?\\") == 0)
    {
        // After \\?\, check what comes next:
        if (Path.size() >= 8 && Path.compare(4, 4, "UNC\\") == 0)
        {
            return true;
        }
        else if (Path.size() >= 6 && Path[5] == ':')
        {
            return true;
        }
        else
        {
            return false;
        }
    }

    if (Path.size() >= 3 && std::isalpha(static_cast<unsigned char>(Path[0])) && Path[1] == ':' && (Path[2] == '\\' || Path[2] == '/'))
    {
        return true;
    }

    if (Path.size() >= 2 && Path[0] == '\\' && Path[1] == '\\')
    {
        return true;
    }
    return false;
#else
    return !Path.empty() && Path[0] == '/';
#endif
}

bool ConfigParser::IsParentDirectory(const std::string& Parent, const std::string& Child)
{
    try
    {
        FS::path ParentPath(Parent);
        FS::path ChildPath(Child);

        auto ParentAbs = FS::absolute(ParentPath).lexically_normal();
        auto ChildAbs = FS::absolute(ChildPath).lexically_normal();

        auto ParentIt = ParentAbs.begin();
        auto ChildIt = ChildAbs.begin();

        for (; ParentIt != ParentAbs.end() && ChildIt != ChildAbs.end(); ++ParentIt, ++ChildIt)
        {
            if (*ParentIt != *ChildIt)
                return false;
        }
        return true;
    }
    catch (...)
    {
        // If any error parsing paths, consider false
        return false;
    }
}

bool ConfigParser::Parse(const std::string& FilePath)
{
    if (!std::filesystem::exists(FilePath))
    {
        AddError("Config file does not exist: " + FilePath);
        return false;
    }

    std::ifstream File(FilePath);
    if (!File.is_open())
    {
        AddError("Failed to open config file: " + FilePath);
        return false;
    }

    std::string Line;
    int LineNumber = 0;

    while (std::getline(File, Line))
    {
        LineNumber++;

        // Trim leading whitespace
        Line.erase(Line.begin(), std::find_if(Line.begin(), Line.end(),[](char Ch) { return !std::isspace(static_cast<unsigned char>(Ch)); }));
        // Trim trailing whitespace
        Line.erase(std::find_if(Line.rbegin(), Line.rend(),[](char Ch) { return !std::isspace(static_cast<unsigned char>(Ch)); }).base(), Line.end());
        
        if (Line.empty())
        {
            continue;
        }

        unsigned short int EqualPos = Line.find('=');
        if (EqualPos == std::string::npos)
        {
            AddError("Invalid format on line " + std::to_string(LineNumber) + ": No '=' found.");
            continue;
        }

        std::string Key = Line.substr(0, EqualPos);
        std::string Value = Line.substr(EqualPos + 1);

        Key.erase(std::remove_if(Key.begin(), Key.end(),[](char Ch) { return std::isspace(static_cast<unsigned char>(Ch)); }), Key.end());
        Value.erase(Value.begin(), std::find_if(Value.begin(), Value.end(),[](char Ch) { return !std::isspace(static_cast<unsigned char>(Ch)); }));
        Value.erase(std::find_if(Value.rbegin(), Value.rend(),[](char Ch) { return !std::isspace(static_cast<unsigned char>(Ch)); }).base(), Value.end());

        if (Key == "Source")
        {
            if (!IsAbsolutePath(Value))
            {
                AddError("Line " + std::to_string(LineNumber) + ": Source path is not absolute.");
                continue;
            }
            FS::path SourcePath(Value);
            std::error_code ec;
            if (!FS::exists(SourcePath, ec))
            {
                AddError("Line " + std::to_string(LineNumber) + ": Source path does not exist." + ec.message());
                continue;
            }
            if (!FS::is_directory(SourcePath) && !FS::is_regular_file(SourcePath))
            {
                AddError("Line " + std::to_string(LineNumber) + ": Source path is neither a file nor a directory.");
                continue;
            }
            
            bool conflictFound = false;
            for (const auto& ExistingSource : Sources)
            {
                if (IsParentDirectory(ExistingSource, Value))
                {
                    AddInfo("Line " + std::to_string(LineNumber) + ": Skipping source '" + Value + "' because parent directory '" + ExistingSource + "' is already added.");
                    conflictFound = true;
                    break;
                }
                else if (IsParentDirectory(Value, ExistingSource)) //Skip because possibility of duplicate files and double reads
                {
                    AddInfo("Line " + std::to_string(LineNumber) + ": Skipping parent directory '" + Value + "' because file '" + ExistingSource + "' is already added.");
                    conflictFound = true;
                    break;
                }
            }
            if (conflictFound)
            {
                continue;
            }

            if (std::find(Sources.begin(), Sources.end(), Value) != Sources.end())
            {
                AddInfo("Line " + std::to_string(LineNumber) + ": Duplicate source path '" + Value + "'. Ignored.");
                continue;
            }
            Sources.push_back(Value);
        }

        else if (Key == "Destination")
        {
            if (!IsAbsolutePath(Value))
            {
                AddError("Line " + std::to_string(LineNumber) + ": Destination path is not absolute.");
                continue;
            }
            if (!ConfigGlobal::DestinationPath.empty())
            {
                AddError("Line " + std::to_string(LineNumber) + ": Multiple destination entries found.");
                continue;
            }
            FS::path DestPath(Value);
            if (!std::filesystem::exists(DestPath))
            {
                AddError("Line " + std::to_string(LineNumber) + ": Destination path does not exist.");
                continue;
            }
            if (!std::filesystem::is_directory(DestPath))
            {
                AddError("Line " + std::to_string(LineNumber) + ": Destination path is not a directory.");
                continue;
            }
            ConfigGlobal::DestinationPath = Value;
            FailureDetect::CheckCacheIntegrity();
        }

        else if (Key == "Exclude")
        {
            if (!IsAbsolutePath(Value))
            {
                AddError("Line " + std::to_string(LineNumber) + ": Exclude path is not absolute.");
                continue;
            }
            if (std::find(Excludes.begin(), Excludes.end(), Value) != Excludes.end())
            {
                AddInfo("Line " + std::to_string(LineNumber) + ": Duplicate exclude path '" + Value + "'. Ignored.");
                continue;
            }
            Excludes.push_back(Value);
        }

        else if (Key == "Mode")
        {
            if (Value == "BG")
            {
                ConfigGlobal::Mode = "BG";
                ConfigGlobal::ThreadCount = 2;
                AddInfo("Mode set to 'BG' (Background). ThreadCount = 2");
            }
            else if (Value == "Inter")
            {
                ConfigGlobal::Mode = "Inter";
                ConfigGlobal::ThreadCount = 4;
                AddInfo("Mode set to 'Inter' (Intermediate). ThreadCount = 4");
            }
            else if (Value == "GodSpeed")
            {
                ConfigGlobal::Mode = "GodSpeed";
                ConfigGlobal::ThreadCount = static_cast<int>(std::thread::hardware_concurrency());
                if (ConfigGlobal::ThreadCount <= 0)
                {
                    ConfigGlobal::ThreadCount = 8;
                }
                AddInfo("Mode set to 'GodSpeed'. ThreadCount = " + std::to_string(ConfigGlobal::ThreadCount));
            }
            else
            {
                AddError("Line " + std::to_string(LineNumber) + ": Invalid Mode. Use 'BG' or 'Inter' or 'GodSpeed'.");
            }
        }

        else if (Key == "ThreadCount")
        {
            try
            {
                unsigned short int ValueNum = std::stoi(Value);
                if (ValueNum == 0)
                {
                    AddError("Line " + std::to_string(LineNumber) + ": ThreadCount must be greater than zero.");
                    continue;
                }
                ConfigGlobal::ThreadCount = ValueNum;
                AddInfo("ThreadCount set to " + std::to_string(ValueNum));
            }
            catch (...)
            {
                AddError("Line " + std::to_string(LineNumber) + ": Invalid number for ThreadCount.");
            }
        }

        else if (Key == "GodSpeedParallelFilesPerSourcesCount")
        {
            try
            {
                unsigned short int ValueNum = std::stoi(Value);
                if (ValueNum == 0)
                {
                    AddError("Line " + std::to_string(LineNumber) + ": GodSpeedParallelFilesPerSourcesCount must be greater than zero.");
                    continue;
                }
                ConfigGlobal::GodSpeedParallelFilesPerSourcesCount = ValueNum;
                AddInfo("GodSpeedParallelFilesPerSourcesCount set to " + std::to_string(ValueNum));
            }
            catch (...)
            {
                AddError("Line " + std::to_string(LineNumber) + ": Invalid number for GodSpeedParallelFilesPerSourcesCount.");
            }
        }

        else if (Key == "ParallelFilesPerSourceCount")
        {
            try
            {
                unsigned short int ValueNum = std::stoi(Value);
                if (ValueNum == 0)
                {
                    AddError("Line " + std::to_string(LineNumber) + ": ParallelFilesPerSourceCount must be greater than zero.");
                    continue;
                }
                ConfigGlobal::ParallelFilesPerSourceCount = ValueNum;
                AddInfo("ParallelFilesPerSourceCount set to " + std::to_string(ValueNum));
            }
            catch (...)
            {
                AddError("Line " + std::to_string(LineNumber) + ": Invalid number for ParallelFilesPerSourceCount.");
            }
        }

        else if (Key == "GodSpeedParallelSourcesCount")
        {
            try
            {
                unsigned short int ValueNum = std::stoi(Value);
                if (ValueNum == 0)
                {
                    AddError("Line " + std::to_string(LineNumber) + ": GodSpeedParallelSourcesCount must be greater than zero.");
                    continue;
                }
                ConfigGlobal::GodSpeedParallelSourcesCount = ValueNum;
                AddInfo("GodSpeedParallelSourcesCount set to " + std::to_string(ValueNum));
            }
            catch (...)
            {
                AddError("Line " + std::to_string(LineNumber) + ": Invalid number for GodSpeedParallelSourcesCount.");
            }
        }

        else if (Key == "DiskType")
        {
            if (Value == "SSD")
            {
                ConfigGlobal::DiskType = "SSD";
                AddInfo("DiskType set to 'SSD' (Disk Thrashing Prevention Mechanism Disabled).");
            }
            else if (Value == "HDD")
            {
                ConfigGlobal::DiskType = "HDD";
                AddInfo("DiskType set to 'HDD' (Disk Thrashing Prevention Mechanism Enabled).");
            }
            else
            {
                AddError("Line " + std::to_string(LineNumber) + ": Invalid Disktype. Use 'SSD' or 'HDD'.");
            }
        }

        else if (Key == "SSDMode")
        {
            if (Value == "Sequential")
            {
                ConfigGlobal::SSDMode = "Sequential";
                AddInfo("SSDMode set to 'Sequential'.");
            }
            else if (Value == "Parallel")
            {
                ConfigGlobal::SSDMode = "Parallel";
                AddInfo("SSDMode set to 'Parallel'.");
            }
            else if (Value == "Balanced")
            {
                ConfigGlobal::SSDMode = "Balanced";
                AddInfo("SSDMode set to 'Balanced'.");
            }
            else if (Value == "GodSpeed")
            {
                ConfigGlobal::SSDMode = "GodSpeed";
                AddInfo("SSDMode set to 'GodSpeed' (Performance Might Be Affected, Use with Caution).");
            }
            else
            {
                AddError("Line " + std::to_string(LineNumber) + ": Invalid SSDMode. Use 'Optimized' or 'Parallel' or 'Sequential' or 'GodspeedSSD'.");
            }
        }

        else if (Key == "DeleteStaleFromDest")
        {
            if (Value == "YES")
            {
                ConfigGlobal::DeleteStaleFromDest = true;
                AddInfo("IMPORTANT - ! Enabled Remove Stale Entries from Destination !");
            }
            else if (Value == "NO")
            {
                ConfigGlobal::DeleteStaleFromDest = false;
                AddInfo("Disabled Remove Stale Entries from Destination");
            }
            else
            {
                AddError("Line " + std::to_string(LineNumber) + ": Invalid Input. Use 'YES' or 'NO'.");
            }
        }

        else if (Key == "EnableBackupCopyAfterRun")
        {
            if (Value == "YES")
            {
                ConfigGlobal::EnableBackupCopyAfterRun = true;
                AddInfo("IMPORTANT - ! Enabled Cache Copy Backup to Destination !");
            }
            else if (Value == "NO")
            {
                ConfigGlobal::EnableBackupCopyAfterRun = false;
                AddInfo("Disabled Cache Copy Backup to Destination");
            }
            else
            {
                AddError("Line " + std::to_string(LineNumber) + ": Invalid Input. Use 'YES' or 'NO'.");
            }
        }

        else if (Key == "EnableCacheRestoreFromBackup")
        {
            if (Value == "YES")
            {
                ConfigGlobal::EnableCacheRestoreFromBackup = true;
                AddInfo("IMPORTANT - ! Enabled Restore Cache Backup from Destination !");
            }
            else if (Value == "NO")
            {
                ConfigGlobal::EnableCacheRestoreFromBackup = false;
                AddInfo("Disabled Restore Cache Backup from Destination");
            }
            else
            {
                AddError("Line " + std::to_string(LineNumber) + ": Invalid Input. Use 'YES' or 'NO'.");
            }
        }

        else if (Key == "DestinationTopFolderInsteadOfFullPath")
        {
            if (Value == "YES")
            {
                ConfigGlobal::DestinationTopFolderInsteadOfFullPath = true;
                AddInfo("The Destination will contain only the Top Level Source Folder Name, Full Source Paths will NOT be Preserved");
            }
            else if (Value == "NO")
            {
                ConfigGlobal::DestinationTopFolderInsteadOfFullPath = false;
                AddInfo("The Destination will preserve the Full Source Directory Path Structure.");
            }
            else
            {
                AddError("Line " + std::to_string(LineNumber) + ": Invalid Input. Use 'YES' or 'NO'.");
            }
        }

        else if (Key == "MaxLogFiles")
        {
            try
            {
                unsigned short int ValueNum = std::stoi(Value);
                if (ValueNum == 0)
                {
                    AddError("Line " + std::to_string(LineNumber) + ": MaxLogFiles must be greater than zero.");
                    continue;
                }
                ConfigGlobal::MaxLogFiles = ValueNum;
                AddInfo("MaxLogFiles set to " + std::to_string(ValueNum));
            }
            catch (...)
            {
                AddError("Line " + std::to_string(LineNumber) + ": Invalid number for MaxLogFiles. Select between 1 and 65,535");
            }
        }

        else if (Key == "StaleEntries")
        {
            try
            {
                unsigned short int ValueNum = std::stoi(Value);
                if (ValueNum == 0)
                {
                    AddError("Line " + std::to_string(LineNumber) + ": StaleEntries must be greater than zero.");
                    continue;
                }
                ConfigGlobal::StaleEntries = ValueNum;
                AddInfo("StaleEntries set to " + std::to_string(ValueNum));
            }
            catch (...)
            {
                AddError("Line " + std::to_string(LineNumber) + ": Invalid number for StaleEntries. Select between 1 and 65,535");
            }
        }

        else
        {
            AddError("Line " + std::to_string(LineNumber) + ": Unknown key '" + Key + "'.");
            continue;
        }
    }

    if (Sources.empty())
    {
        AddError("No source paths provided.");
    }

    if (ConfigGlobal::DestinationPath.empty())
    {
        AddError("No destination path provided.");
    }
    
    if (!ConfigGlobal::DestinationPath.empty())
    {
        if (ConfigGlobal::DestinationTopFolderInsteadOfFullPath == false)
        {
            FS::path DestAbs = FS::absolute(ConfigGlobal::DestinationPath).lexically_normal();

            for (const auto& Source : Sources)
            {
                FS::path SourceAbs = FS::absolute(Source).lexically_normal();

                if (SourceAbs == DestAbs)
                {
                    AddError("Source path '" + Source + "' is the same as the destination path.");
                }
                else if (IsParentDirectory(SourceAbs.string(), DestAbs.string()))
                {
                    AddError("Destination '" + DestAbs.string() + "' is inside source directory '" + SourceAbs.string() + "'. This is not allowed.");
                }
            }
        }
        else
        {
            FS::path DestAbs = FS::absolute(ConfigGlobal::DestinationPath).lexically_normal();
            std::unordered_set<std::string> UsedFinalNames;

            for (const auto& Source : Sources)
            {
                FS::path SourceAbs = FS::absolute(Source).lexically_normal();

                if (SourceAbs == DestAbs)
                {
                    AddError("Source path '" + Source + "' is the same as the destination path.");
                    continue;
                }

                if (IsParentDirectory(SourceAbs.string(), DestAbs.string()))
                {
                    AddError("Destination '" + DestAbs.string() + "' is inside source directory '" + SourceAbs.string() + "'. This is not allowed.");
                    continue;
                }

                std::string FinalName;
                if (FS::is_regular_file(SourceAbs))
                {
                    FinalName = SourceAbs.filename().string();
                }
                else
                {
                    FinalName = SourceAbs.filename().string();
                }

                if (!UsedFinalNames.insert(FinalName).second)
                {
                    AddError("Source '" + Source + "' results in duplicate name '" + FinalName + "' at destination.");
                }
            }
        }
    }
    return Errors.empty();  // Return false only if fatal errors present
}
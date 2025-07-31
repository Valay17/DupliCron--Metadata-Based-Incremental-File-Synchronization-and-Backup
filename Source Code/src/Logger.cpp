#include "Logger.hpp"
#include "ConfigGlobal.hpp"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <vector>
#include <algorithm>

Logger Log;
namespace FS = std::filesystem;

void Logger::Init(const std::string& logDir)
{
    if (!FS::exists(logDir))
    {
        FS::create_directory(logDir);
    }

    CurrentLogFilePath = ConfigGlobal::LogDir + "/Sync_Log" + GetTimestampForFilename() + ".txt";

    OpenLogFile(CurrentLogFilePath);

    Info("Sync Started at " + GetTimestamp());
}

Logger::~Logger()
{
    Info("Sync Complete at " + GetTimestamp());

    if (LogFile.is_open())
    {
        LogFile.close();
    }
}

void Logger::OpenLogFile(const std::string& FilePath)
{
    LogFile.open(FilePath, std::ios::out);

    if (!LogFile.is_open())
    {
        std::cerr << "Logger: Failed to open log file: " << FilePath << "\n";
    }
}

void Logger::CleanupOldLogs()
{
    std::vector<FS::directory_entry> Logs;

    for (const auto& Entry : FS::directory_iterator(ConfigGlobal::LogDir))
    {
        if (Entry.is_regular_file() && Entry.path().filename().string().find("Sync_Log") == 0)
        {
            Logs.push_back(Entry);
        }
    }

    if ((int)Logs.size() <= ConfigGlobal::MaxLogFiles)
    {
        return;
    }

    std::sort(Logs.begin(), Logs.end(), [](const FS::directory_entry& A, const FS::directory_entry& B)
    {
            return A.path().filename().string() < B.path().filename().string();
    });

    while ((int)Logs.size() > ConfigGlobal::MaxLogFiles)
    {
        try
        {
            FS::remove(Logs.front());
        }
        catch (...)
        {
            // ignore errors
        }
        Logs.erase(Logs.begin());
    }
}

void Logger::Log(LogLevel Level, const std::string& Message)
{
    std::lock_guard<std::mutex> Lock(LogWriteMutex);

    if (!LogFile.is_open())
    {
        return;
    }

    LogFile << "[" << GetTimestamp() << "]" << " [" << LevelToString(Level) << "] " << Message << "\n";
    LogFile.flush();
}

void Logger::Info(const std::string& Message)
{
    Log(LogLevel::INFO, Message);
}

void Logger::Error(const std::string& Message)
{
    Log(LogLevel::ERROR, Message);
}

std::string Logger::GetTimestampForFilename()
{
    auto Now = std::chrono::system_clock::now();
    std::time_t Time = std::chrono::system_clock::to_time_t(Now);
    std::tm Local{};

#ifdef _WIN32
    localtime_s(&Local, &Time);
#else
    localtime_r(&Time, &Local);
#endif

    std::ostringstream Stream;
    Stream << std::put_time(&Local, "%Y%m%d_%H%M%S");
    return Stream.str();
}

std::string Logger::GetTimestamp() const
{
    auto Now = std::chrono::system_clock::now();
    std::time_t Time = std::chrono::system_clock::to_time_t(Now);
    std::tm Local{};

#ifdef _WIN32
    localtime_s(&Local, &Time);
#else
    localtime_r(&Time, &Local);
#endif

    std::ostringstream Stream;
    Stream << std::put_time(&Local, "%Y-%m-%d %H:%M:%S"); // human-readable timestamp for logs
    return Stream.str();
}

std::string Logger::LevelToString(LogLevel Level) const
{
    switch (Level)
    {
    case LogLevel::INFO:  return "INFO";
    case LogLevel::ERROR: return "ERROR";
    default:              return "UNKNOWN";
    }
}
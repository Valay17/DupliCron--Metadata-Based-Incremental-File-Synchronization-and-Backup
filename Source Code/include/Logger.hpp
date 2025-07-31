#pragma once

#include <string>
#include <fstream>
#include <mutex>

enum class LogLevel
{
    INFO,
    ERROR
};

class Logger
{
public:
    Logger() = default;
    ~Logger();

    void Init(const std::string& logDir);
    void Log(LogLevel Level, const std::string& Message);
    void Info(const std::string& Message);
    void Error(const std::string& Message);
    void CleanupOldLogs();

    static std::string GetTimestampForFilename();

    std::string CurrentLogFilePath;

private:
    std::ofstream LogFile;
    std::mutex LogWriteMutex;

    std::string GetTimestamp() const;
    std::string LevelToString(LogLevel Level) const;

    void OpenLogFile(const std::string& FilePath);
};

extern Logger Log;
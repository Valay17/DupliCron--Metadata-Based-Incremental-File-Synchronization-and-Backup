#pragma once

#include <string>
#include "Logger.hpp"

class FileCopier
{
public:

#ifdef _WIN32
#else
    static bool CopyFileRangeSupported;
    static void CheckCopyFileRangeSupport();
#endif

    static bool PerformFileCopy(const std::string& sourcePath, const std::string& SourceTopRootPath);
    static void DeleteStaleFromDestination(const std::string& sourcePath);

private:

    static std::string SanitizePath(const std::string& absPath);
    static std::wstring EscapeRootDriveForCmd(const std::wstring& path);
    static void HandleCopyFailure(const std::string& FilePath, const std::string& Reason, int ErrorCode);
};
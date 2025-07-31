#include "FileCopier.hpp"
#include "ConfigGlobal.hpp"
#include "MetaDataCache.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>

#ifdef _WIN32
#include <Windows.h>
#include <thread>
#include <chrono>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstring>

bool FileCopier::CopyFileRangeSupported = true;

void FileCopier::CheckCopyFileRangeSupport()
{
    int srcFd = open("/dev/null", O_RDONLY);
    int destFd = open("/dev/null", O_WRONLY);
    if (srcFd < 0 || destFd < 0) {
        if (srcFd >= 0) close(srcFd);
        if (destFd >= 0) close(destFd);
        CopyFileRangeSupported = false;
        Log.Info(std::string("[File Copier] Copy_File_Range not supported"));
        return;
    }

    ssize_t result = copy_file_range(srcFd, nullptr, destFd, nullptr, 1, 0);
    CopyFileRangeSupported = (result >= 0 || errno != ENOSYS);

    close(srcFd);
    close(destFd);
}

namespace {
    struct CopyFileRangeInit
    {
        CopyFileRangeInit()
        {
            FileCopier::CheckCopyFileRangeSupport();
        }
    };

    static CopyFileRangeInit InitCopyFileRangeSupport;

    std::string escapeShellChars(const std::string& input) {
        std::string output;
        for (char c : input) {
            if (c == '$' || c == '\\' || c == '"' || c == '`') {
                output += '\\';  // escape shell special chars
            }
            output += c;
        }
        return output;
}
}
#endif

constexpr size_t LARGE_FILE_THRESHOLD = 2ULL * 1024 * 1024 * 1024; //ULL = Unsigned Long Long

bool FileCopier::PerformFileCopy(const std::string& sourcePath, const std::string& SourceTopRootPath)
{
    try
    {
        std::filesystem::path finalDestPath;

        if (ConfigGlobal::DestinationTopFolderInsteadOfFullPath == true)
        {
            std::filesystem::path FilePath(sourcePath);
            std::filesystem::path TopFolderRootPath(SourceTopRootPath);

            if (std::filesystem::is_regular_file(TopFolderRootPath))
            {
                std::string fileName = FilePath.filename().string();
                finalDestPath = std::filesystem::path(ConfigGlobal::DestinationPath) / fileName;
            }
            else
            {
                std::string TopRootFolderName = TopFolderRootPath.filename().string();
                std::filesystem::path relativePath = std::filesystem::relative(FilePath, TopFolderRootPath);
                finalDestPath = std::filesystem::path(ConfigGlobal::DestinationPath) / TopRootFolderName / relativePath;
            }
        }
        else
        {
            std::string sanitizedRelPath = SanitizePath(sourcePath);
            finalDestPath = std::filesystem::path(ConfigGlobal::DestinationPath) / sanitizedRelPath;
        }

        std::cout << "[COPY] " << sourcePath << " → " << finalDestPath << "\n";
        Log.Info(std::string("[FileCopier] Copying File: ") + sourcePath + std::string(" → ") + finalDestPath.string());
        std::filesystem::create_directories(finalDestPath.parent_path());
        uintmax_t fileSize = std::filesystem::file_size(sourcePath);
#ifdef _WIN32
        if (fileSize >= LARGE_FILE_THRESHOLD)
        {
            std::wstringstream cmd;

            std::wstring wSource = std::filesystem::path(sourcePath).parent_path().wstring();
            wSource = EscapeRootDriveForCmd(wSource);
            std::wstring wDest = finalDestPath.parent_path().wstring();
            std::wstring wFileName = std::filesystem::path(sourcePath).filename().wstring();

            cmd << L"robocopy \"" << wSource << L"\" \"" << wDest << L"\" \"" << wFileName << L"\" /R:2 /W:5 /NFL /NDL /NJH /MT:" << ConfigGlobal::ThreadCount;

            std::wcout << L"Using robocopy for large file: " << cmd.str() << L"\n";

            // Run robocopy and wait for it to finish
            int ret = _wsystem(cmd.str().c_str());
            if (ret >= 8) // robocopy exit codes >= 8 indicate failure
            {
                std::wcerr << L"[ERROR] robocopy failed with exit code: " << ret << L"\n";
                HandleCopyFailure(sourcePath, "RoboCopy failed", ret);
                return false;
            }
            return true;
        }
        else
        {
            std::wstring srcW(sourcePath.begin(), sourcePath.end());
            std::wstring dstW(finalDestPath.wstring());

            /*if (!std::filesystem::exists(sourcePath))
            {
                std::wcerr << L"[ERROR] Source file does not exist: " << srcW << L"\n";
                return false;
            }*/

            BOOL result = CopyFileExW(srcW.c_str(), dstW.c_str(), nullptr, nullptr, nullptr, COPY_FILE_COPY_SYMLINK);
            if (!result)
            {
                DWORD err = GetLastError();
                std::wcerr << L"[ERROR] CopyFileExW failed for " << dstW << L" (Error: " << err << L")\n";
                HandleCopyFailure(sourcePath, "CopyFileExW failed", err);
                return false;
            }
        }
#else
    std::string escapedDestPath = escapeShellChars(finalDestPath.string());

	int srcFd = open(sourcePath.c_str(), O_RDONLY);
        if (srcFd < 0)
        {
            std::cerr << "[ERROR] Failed to open source file: " << sourcePath << "\n";
            Log.Error(std::string("[FileCopier] Failed to Open Soruce File") + sourcePath);
            return false;
        }

        int destFd = open(finalDestPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (destFd < 0)
        {
            Log.Error(std::string("[FileCopier] Failed to Open/Close Destination File") + escapedDestPath);
            close(srcFd);
            return false;
        }

         if (fileSize >= LARGE_FILE_THRESHOLD)
        {
            // Use dd for content copy with progress
            std::string ddCmd = "dd if=\"" + escapeShellChars(sourcePath) + "\" of=\"" + escapedDestPath + "\" bs=4M status=progress conv=fsync";
            int ret = std::system(ddCmd.c_str());
            if (ret != 0)
            {
                std::cerr << "[ERROR] dd command failed with exit code: " << ret << "\n";
                close(srcFd);
                close(destFd);
                HandleCopyFailure(sourcePath, "dd failed", ret);
                return false;
            }

            // Copy metadata separately
            std::string cpAttrCmd = "cp --attributes-only --preserve=mode,ownership,timestamps \"" + escapeShellChars(sourcePath) + "\" \"" + escapedDestPath + "\"";
            ret = std::system(cpAttrCmd.c_str());
            if (ret != 0)
            {
                std::cerr << "[ERROR] cp attributes command failed with exit code: " << ret << "\n";
                close(srcFd);
                close(destFd);
                HandleCopyFailure(sourcePath, "cp attributes failed", ret);
                return false;
            }
        }
        else
        {
            if (CopyFileRangeSupported)
            {
                struct stat statBuf;
                fstat(srcFd, &statBuf);
                ssize_t copied = copy_file_range(srcFd, nullptr, destFd, nullptr, statBuf.st_size, 0);
                if (copied < 0)
                {
                        std::cerr << "[ERROR] copy_file_range failed: " << strerror(errno) << "\n";
                        close(srcFd);
                        close(destFd);
                        HandleCopyFailure(sourcePath, "copy faile range failed", errno);
                        return false;
                }
            }
            else
            {
                // fallback to cp to copy content+metadata
                std::string cpCmd = "cp --preserve=mode,ownership,timestamps \"" + escapeShellChars(sourcePath) + "\" \"" + escapedDestPath + "\"";
                int ret = std::system(cpCmd.c_str());
                if (ret != 0)
                {
                    std::cerr << "[ERROR] cp fallback command failed with exit code: " << ret << "\n";
                    close(srcFd);
                    close(destFd);
                    HandleCopyFailure(sourcePath, "cp failed", ret);
                    return false;
                }
            }
        }

        close(srcFd);
        close(destFd);
#endif
        return true;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "[EXCEPTION] CopyFile failed: " << ex.what() << "\n";
        HandleCopyFailure(sourcePath, ex.what(), -1);
        return false;
    }
}

// Removes special characters (like ':' or leading '/') from absolute paths to generate a sanitized, consistent path string for internal use.
std::string FileCopier::SanitizePath(const std::string& absPath)
{
    std::string result;
    bool isWindowsPath = false;
    if (absPath.size() > 2 && absPath[1] == ':' && absPath[2] == '/')
    {
        isWindowsPath = true;
        result += absPath[0]; // Keep the drive letter (e.g., 'C') only
        for (size_t i = 3; i < absPath.size(); ++i) // Copy remaining characters, skipping colons
        {
            char ch = absPath[i];
            if (ch != ':')
                result += ch;
        }
    }
    else
    {
        for (size_t i = 0; i < absPath.size(); ++i)
        {
            char ch = absPath[i];
            if (i == 0 && ch == '/') // Skip leading slash in Unix paths
                continue;
            if (ch != ':') // Remove any colons (just in case)
                result += ch;
        }
    }
    return result;
}

// If path is exactly like "S:\", append an extra backslash
std::wstring FileCopier::EscapeRootDriveForCmd(const std::wstring& path)
{
    if (path.size() == 3 && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/'))
        return path + L'\\';  // e.g. "S:\\" for robocopy

    return path;  // otherwise, return as-is
}

void FileCopier::DeleteStaleFromDestination(const std::string& sourcePath)
{
    try
    {
        std::filesystem::path DestPath = ConfigGlobal::DestinationPath;
        std::filesystem::path RelPath = SanitizePath(sourcePath);
        std::filesystem::path FullPath = DestPath / RelPath;
        
        std::error_code ec;
        if (std::filesystem::remove(FullPath, ec))
        {
            std::cout << "[Deleted Stale] " << FullPath << "\n";
            Log.Info(std::string("[DeleteStaleFromDest] Deleted File from Destination: ") + FullPath.string());
        }
        else
        {
            std::cerr << "[Delete Failed] " << FullPath << " - " << ec.message() << "\n";
            Log.Error(std::string("[DeleteStaleFromDest] Failed to Delete File from Destination: ") + FullPath.string() + std::string(" - ") + ec.message());
        }
    }
    catch (const std::exception& ex)
    {
        std::cerr << "[Exception] Deleting stale file: " << sourcePath << " - " << ex.what() << "\n";
        Log.Error(std::string("[DeleteStaleFromDest] Failed to Delete File from Destination: ") + sourcePath + std::string(" - ") + ex.what());
    }
}

void FileCopier::HandleCopyFailure(const std::string& FilePath, const std::string& Reason, int ErrorCode)
{
    Log.Error(std::string("[FileCopier] Copy Failed: ") + FilePath + std::string(" | Code: ") + std::to_string(ErrorCode) + std::string(" | Reason: ") + Reason);

    MetaDataCache Cache;
    Cache.LoadCopiedState();
    Cache.SaveCopiedState();
    std::cerr << "[NOTICE] The current sync state has been saved.\n" << "You can resume copying the remaining files by running the program again after resolving error.\n\n";
    Log.Info(std::string("Sync State Saved, file probably not copied due to I/O Problems"));
    std::exit(EXIT_FAILURE);
}
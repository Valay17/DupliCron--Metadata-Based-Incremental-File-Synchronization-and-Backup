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

inline std::filesystem::path NormalizeLongPath(const std::filesystem::path& path)
{
    std::wstring wpath = path.wstring();

    // Already normalized
    if (wpath.starts_with(L"\\\\?\\"))
        return path;

    constexpr size_t MAX_PATH_LIMIT = 260;

    if (wpath.size() >= MAX_PATH_LIMIT)
    {
        if (wpath.starts_with(L"\\\\"))
        {
            // UNC path: \\server\share → \\?\UNC\server\share
            return std::filesystem::path(L"\\\\?\\UNC\\" + wpath.substr(2));
        }
        else
        {
            // Local drive path: C:\folder\file → \\?\C:\folder\file
            return std::filesystem::path(L"\\\\?\\" + wpath);
        }
    }

    // Path shorter than MAX_PATH: return as-is
    return path;
}

inline std::filesystem::path RemoveLongPathPrefix(const std::filesystem::path& path)
{
    std::wstring wpath = path.wstring();

    constexpr wchar_t longPrefix[] = L"\\\\?\\";
    constexpr size_t longPrefixLen = 4;

    if (!wpath.starts_with(longPrefix))
    {
        return path;
    }

    if (wpath.size() > longPrefixLen + 3 && wpath.compare(longPrefixLen, 4, L"UNC\\") == 0)
    {
        std::wstring newPath = L"\\\\" + wpath.substr(longPrefixLen + 4);
        return std::filesystem::path(newPath);
    }
    else
    {
        return std::filesystem::path(wpath.substr(longPrefixLen));
    }
}

inline std::wstring UTF8ToUTF16(const std::string& utf8)
{
    if (utf8.empty())
        return std::wstring();

    int size_needed = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), nullptr, 0);
    std::wstring utf16(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), &utf16[0], size_needed);
    return utf16;
}

#else

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstring>

inline std::filesystem::path NormalizeLongPath(const std::filesystem::path& path)
{
    return path;
}

inline std::filesystem::path RemoveLongPathPrefix(const std::filesystem::path& path)
{
    return path;
}

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
                TopFolderRootPath = RemoveLongPathPrefix(TopFolderRootPath);
                FilePath = RemoveLongPathPrefix(FilePath);
                std::filesystem::path relativePath = std::filesystem::relative(FilePath, TopFolderRootPath);
                finalDestPath = std::filesystem::path(ConfigGlobal::DestinationPath) / TopRootFolderName / relativePath;
            }
        }
        else
        {
            std::string sanitizedRelPath = SanitizePath(sourcePath);
            finalDestPath = std::filesystem::path(ConfigGlobal::DestinationPath) / sanitizedRelPath;
        }

        std::cout << "[COPY] " << sourcePath << " → " << finalDestPath.string() << "\n";
        Log.Info(std::string("[FileCopier] Copying File: ") + sourcePath + std::string(" → ") + finalDestPath.string());
        
        std::filesystem::path normalizedDest = NormalizeLongPath(finalDestPath);
        std::filesystem::create_directories(normalizedDest.parent_path());
        
        uintmax_t fileSize = std::filesystem::file_size(sourcePath);

#ifdef _WIN32
        if (fileSize >= LARGE_FILE_THRESHOLD)
        {
            std::wstringstream cmd;

            std::string srcUtf8 = std::filesystem::path(sourcePath).parent_path().string();
            std::wstring wSource = UTF8ToUTF16(srcUtf8);
            wSource = RemoveLongPathPrefix(wSource);
            wSource = EscapeRootDriveForCmd(wSource);
            
            std::string destUtf8 = finalDestPath.parent_path().string();
            std::wstring wDest = UTF8ToUTF16(destUtf8);
            wDest = RemoveLongPathPrefix(wDest);
            wDest = EscapeRootDriveForCmd(wDest);
            
            std::string fileUtf8 = std::filesystem::path(sourcePath).filename().string();
            std::wstring wFileName = UTF8ToUTF16(fileUtf8);

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
            std::wstring srcW = UTF8ToUTF16(sourcePath);
            std::wstring dstW = UTF8ToUTF16(normalizedDest.string());
            
            //Redundant because alreadyc checking but keep commented in case there's some niche case that I kept this in the first place for
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
            //Alt implementation with this behavior:
            //Skips copy if same file name at source, does not overwrite(no comparisons done oher than file name)
            /*
            BOOL result = CopyFileExW(srcW.c_str(), dstW.c_str(), nullptr, nullptr, nullptr, COPY_FILE_FAIL_IF_EXISTS | COPY_FILE_COPY_SYMLINK);
            if (!result)
            {
                DWORD err = GetLastError();
                if (err == ERROR_FILE_EXISTS)
                {
                    // Destination file exists, skipping copy - not a fatal error
                    std::wcerr << L"[INFO] Destination file already exists, skipping copy: " << dstW << L"\n";
                    // Continue execution without returning false
                }
                else
                {
                    // Other errors: treat as failure
                    std::wcerr << L"[ERROR] CopyFileExW failed for " << dstW << L" (Error: " << err << L")\n";
                    HandleCopyFailure(sourcePath, "CopyFileExW failed", err);
                    return false;
                }
            }
            */
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
    std::filesystem::path input(absPath);
    std::string result;

#ifdef _WIN32
    std::string native = input.string();

    // Strip long path prefix
    if (native.starts_with(R"(\\?\UNC\)"))
    {
        result = "UNC/" + native.substr(8);  // Remove \\?\UNC\ prefix and add UNC/
    }
    else if (native.starts_with(R"(\\?\)"))
    {
        // Remove \\?\ prefix
        result = native.substr(4);

        // If it looks like "E:" (single letter + colon) at the start, strip colon
        if (result.size() >= 2 && result[1] == ':')
        {
            result = std::string(1, result[0]) + result.substr(2);  // Remove colon at pos 1
        }
    }
    else if (native.starts_with(R"(\\)"))
    {
        result = "UNC/" + native.substr(2); // Convert \\server\share → UNC/server/share
    }
    else if (native.size() > 2 && native[1] == ':' && (native[2] == '/' || native[2] == '\\'))
    {
        // C:/Users/ → C/Users/...
        result = std::string(1, native[0]) + native.substr(2);
    }
    else
    {
        result = native;
    }

#else
    result = input.generic_string();
    if (result.starts_with("/"))
        result = result.substr(1);
#endif
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
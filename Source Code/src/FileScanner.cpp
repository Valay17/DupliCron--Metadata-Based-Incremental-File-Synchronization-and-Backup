#include <iostream>
#include <filesystem>
#include <stack>

#include "FileScanner.hpp"
#include "TimeUtils.hpp"
#include "Logger.hpp"

namespace FS = std::filesystem;

#ifdef _WIN32

inline FS::path NormalizeLongPath(const FS::path& path)
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
            return FS::path(L"\\\\?\\UNC\\" + wpath.substr(2));
        }
        else
        {
            // Local drive path: C:\folder\file → \\?\C:\folder\file
            return FS::path(L"\\\\?\\" + wpath);
        }
    }

    // Path shorter than MAX_PATH: return as-is
    return path;
}

#else

inline FS::path NormalizeLongPath(const FS::path& path)
{
    return path;
}

#endif

const std::vector<ScannedFileInfo>& FileScanner::GetFiles() const
{
    return Files;
}

void FileScanner::Clear()
{
    Files.clear();
}

void FileScanner::SetExcludes(const std::vector<std::string>& ExcludePaths)
{
    Excludes = ExcludePaths;
}

bool FileScanner::IsExcluded(const FS::path& Path) const
{
    const std::string Abs = FS::absolute(Path).string();
    for (const auto& Exclude : Excludes)
    {
        if (Abs == Exclude)
        {
            return true;
        }
    }
    return false;
}

void FileScanner::Scan(const std::string& RootPath)
{
    FS::path Root(RootPath);
    Root = NormalizeLongPath(Root);
    try
    {
        if (!FS::exists(Root))
        {
            std::cerr << "Scan Error: Path does not exist: " << Root.string() << "\n";
            Log.Error("Scan: Path does not exist: " + Root.string());
            return;
        }
        if (IsExcluded(Root))
        {
            std::cerr << "Skipping excluded root path: " << Root.string() << "\n";
            Log.Error("Skipping excluded root path : " + Root.string());
            return;
        }
        if (FS::is_regular_file(Root)) // Single file case
        {
            ScannedFileInfo Info;
            Info.RelativePath = Root.string(); // Just the filename for single file
            Info.Size = FS::file_size(Root);
            Info.MTime = ToTimeT(FS::last_write_time(Root));
            Files.push_back(std::move(Info));
            return;
        }
        if (!FS::is_directory(Root))
        {
            std::cerr << "Scan Error: Path is neither a directory nor a file: " << Root.string() << "\n";
            Log.Error("Scan Error: Path is neither a directory nor a file: " + Root.string());
            return;
        }
        // Directory case
        ScanDirectoryIterative(Root);
    }
    catch (const FS::filesystem_error& e)
    {
        std::cerr << "Filesystem error during scan: " << e.what() << "\n";
        Log.Error(std::string("Filesystem error during scan: ") + e.what());
        std::cerr << "Path: " << e.path1() << "\n";
        Log.Error(std::string("Path: ") + e.path1().string());
    }
}

void FileScanner::ScanDirectoryIterative(const FS::path& Root)
{
    std::stack<FS::path> DirStack;
    DirStack.push(Root);
    while (!DirStack.empty())
    {
        FS::path Current = DirStack.top();
        DirStack.pop();

        if (IsExcluded(Current))
        {
            std::cerr << "Skipping Excluded Directory: " << Current << "\n";
            Log.Info(std::string("Skipping Excluded Directory: ") + Current.string());
            continue;
        }
        try
        {
            FS::path normCurrent = NormalizeLongPath(Current);
            for (const auto& Entry : FS::directory_iterator(normCurrent))
            {
                try
                {
                    FS::path AbsPath = Entry.path();
                    AbsPath = NormalizeLongPath(AbsPath);
                    // Skip symbolic links to avoid loops or unsupported files.
                    if (FS::is_symlink(Entry.symlink_status()))
                    {
                        Log.Info(std::string("Skipping SymLink: ") + AbsPath.string());
                        continue;
                    }
                    if (IsExcluded(AbsPath))
                    {
                        std::cerr << "Skipping Excluded Path: " << AbsPath << "\n";
                        Log.Info(std::string("Skipping Excluded Path: ") + AbsPath.string());
                        continue;
                    }
                    if (Entry.is_directory())
                    {
                        DirStack.push(AbsPath);
                    }
                    else if (Entry.is_regular_file())
                    {
                        ScannedFileInfo Info;
                        auto Temp = AbsPath.u8string();
                        Info.RelativePath = std::string(Temp.begin(), Temp.end());
                        Info.Size = Entry.file_size();
                        Info.MTime = ToTimeT(Entry.last_write_time());
                        Files.push_back(std::move(Info));
                    }
                }
                catch (const FS::filesystem_error& e)
                {
                    std::cerr << "Filesystem error accessing entry: " << e.what() << " Path: " << Entry.path() << "\n";
                    Log.Error(std::string("Filesystem error accessing entry: ") + e.what() + std::string(" Path: ") + Entry.path().string());
                }
            }
        }
        catch (const FS::filesystem_error& e)
        {
            std::cerr << "Filesystem error iterating directory: " << e.what() << " Path: " << Current << "\n";
            Log.Error(std::string("Filesystem error iterating directory: ") + e.what() + std::string(" Path: ") + Current.string());
        }
    }
}
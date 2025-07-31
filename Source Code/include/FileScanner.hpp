#pragma once

#include <string>
#include <vector>
#include <filesystem>

struct ScannedFileInfo
{
    std::string RelativePath;
    uintmax_t Size = 0;
    uint64_t MTime = 0;
};

class FileScanner
{
public:
    FileScanner() = default;

    void Clear();

    void Scan(const std::string& RootPath);
    void SetExcludes(const std::vector<std::string>& ExcludePaths);

    const std::vector<ScannedFileInfo>& GetFiles() const;

private:
    std::vector<ScannedFileInfo> Files;
    std::vector<std::string> Excludes;

    void ScanDirectoryIterative(const std::filesystem::path& Root);

    bool IsExcluded(const std::filesystem::path& Path) const;
};

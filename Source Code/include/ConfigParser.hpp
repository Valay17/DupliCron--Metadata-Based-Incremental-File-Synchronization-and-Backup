#pragma once

#include <string>
#include <vector>

class ConfigParser
{
public:
    ConfigParser() = default;
    bool Parse(const std::string& FilePath);

    const std::vector<std::string>& GetErrors() const;
    const std::vector<std::string>& GetInfos() const;
    const std::vector<std::string>& GetExcludes() const;
    const std::vector<std::string> GetSources() const;
    void Reset();

private:
    void AddError(const std::string& Message);
    void AddInfo(const std::string& Message);

    bool IsAbsolutePath(const std::string& Path);
    bool IsParentDirectory(const std::string& Parent, const std::string& Child);

    std::vector<std::string> Sources;
    std::vector<std::string> Excludes;
    std::vector<std::string> Errors;
    std::vector<std::string> Infos;
};
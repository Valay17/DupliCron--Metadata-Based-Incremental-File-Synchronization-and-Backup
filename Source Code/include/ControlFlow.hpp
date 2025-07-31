#pragma once

#include "FileScanner.hpp"
#include "ConfigParser.hpp"
#include "HDDCopyQueue.hpp"
#include "SSDCopyQueue.hpp"
#include "MetaDataCache.hpp"
#include "ConfigGlobal.hpp"

class ControlFlow
{
public:
    ControlFlow() = default;

    int Run();

private:
    FileScanner Scanner;
    ConfigParser Parser;
    MetaDataCache Meta;
    HDDCopyQueue HDDCopy;
    SSDCopyQueue SSDCopy;

    void LogSourcesDestExcludes();
    void LogScannedFiles();
};
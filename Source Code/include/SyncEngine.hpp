#pragma once

#include <string>
#include <vector>
#include "MetaDataCache.hpp"
#include "HDDCopyQueue.hpp"
#include "SSDCopyQueue.hpp"

class SyncEngine
{
public:

    static void SetHDDCopyQueue(HDDCopyQueue* manager);
    static void SetSSDCopyQueue(SSDCopyQueue* manager);
    static void Sync(std::vector<FileInfo> freshFiles,MetaDataCache& cache, uint32_t MetaDataCacheBinFileNumber);

private:

    static inline HDDCopyQueue* HDDCopyQueueInstance = nullptr;
    static inline SSDCopyQueue* SSDCopyQueueInstance = nullptr;
};
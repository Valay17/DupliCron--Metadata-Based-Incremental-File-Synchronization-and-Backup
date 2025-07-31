#include "SyncEngine.hpp"
#include "FileCopier.hpp"
#include "ConfigGlobal.hpp"
#include "Logger.hpp"

#include <filesystem>
#include <iostream>

void SyncEngine::SetHDDCopyQueue(HDDCopyQueue* manager)
{
	HDDCopyQueueInstance = manager;
}

void SyncEngine::SetSSDCopyQueue(SSDCopyQueue* manager)
{
	SSDCopyQueueInstance = manager;
}

constexpr size_t LARGE_FILE_THRESHOLD = 2ULL * 1024 * 1024 * 1024; // 2GB threshold

void SyncEngine::Sync(std::vector<FileInfo> freshFiles, MetaDataCache& cache, uint32_t MetaDataCacheBinFileNumber)
{
    if (ConfigGlobal::DiskType == "SSD")
    {
        std::queue<FileInfo> smallFilesQueue;
        std::queue<FileInfo> largeFilesQueue;
        bool hasFilesToCopy = false;
        
        switch (ToSSDMode(ConfigGlobal::SSDMode))
        {
        case SSDMode::Sequential:
            for (const auto& file : freshFiles)
            {
                const std::string& absPath = file.AbsolutePath;

                bool isNew = !cache.HasEntry(absPath);
                bool isChanged = false;

                if (!isNew)
                {
                    FileInfo cached = cache.GetEntry(absPath);
                    if (cached.Hash != file.Hash)
                    {
                        isChanged = true;
                    }
                }

                if (isNew || isChanged)
                {
                    hasFilesToCopy = true;
                    Log.Info(std::string("[Sync Engine] File marked for copy: ") + absPath);
                    largeFilesQueue.emplace(file);
                }
                else
                {
                    Log.Info(std::string("[Sync Engine] File skipped (up-to-date): ") + absPath);
                }

                cache.MarkVisited(absPath);
            }
            break;
        case SSDMode::Parallel:
        case SSDMode::GodSpeed:
            for (const auto& file : freshFiles)
            {
                const std::string& absPath = file.AbsolutePath;

                bool isNew = !cache.HasEntry(absPath);
                bool isChanged = false;

                if (!isNew)
                {
                    FileInfo cached = cache.GetEntry(absPath);
                    if (cached.Hash != file.Hash)
                    {
                        isChanged = true;
                    }
                }

                if (isNew || isChanged)
                {
                    hasFilesToCopy = true;
                    Log.Info(std::string("[Sync Engine] File marked for copy: ") + absPath);
                    smallFilesQueue.emplace(file);
                }
                else
                {
                    Log.Info(std::string("[Sync Engine] File skipped (up-to-date): ") + absPath);
                }

                cache.MarkVisited(absPath);
            }
            break;
        case SSDMode::Balanced:
            for (const auto& file : freshFiles)
            {
                const std::string& absPath = file.AbsolutePath;

                bool isNew = !cache.HasEntry(absPath);
                bool isChanged = false;

                if (!isNew)
                {
                    FileInfo cached = cache.GetEntry(absPath);
                    if (cached.Hash != file.Hash)
                    {
                        isChanged = true;
                    }
                }

                if (isNew || isChanged)
                {
                    hasFilesToCopy = true;
                    Log.Info(std::string("[Sync Engine] File marked for copy: ") + absPath);

                    try
                    {
                        if (file.Size < LARGE_FILE_THRESHOLD)
                            smallFilesQueue.emplace(file);
                        else
                            largeFilesQueue.emplace(file);
                    }
                    catch (const std::filesystem::filesystem_error& e)
                    {
                        Log.Error(std::string("[Sync Engine] Error getting file size for ") + absPath + " : " + e.what());
                        // Default to small queue to not block
                        smallFilesQueue.emplace(file);
                    }
                }
                else
                {
                    Log.Info(std::string("[Sync Engine] File skipped (up-to-date): ") + absPath);
                }

                cache.MarkVisited(absPath);
            }
            break;
        }

        if (hasFilesToCopy && SSDCopyQueueInstance)
        {
            Log.Info(std::string("[Sync Engine] Submitting copy queues for source ") + std::to_string(MetaDataCacheBinFileNumber) +
                std::string(" | Small files: ") + std::to_string(smallFilesQueue.size()) +
                std::string(" | Large files: ") + std::to_string(largeFilesQueue.size()));

            SSDCopyQueueInstance->SubmitCopyQueues(MetaDataCacheBinFileNumber, std::move(smallFilesQueue), std::move(largeFilesQueue), std::move(freshFiles));
        }
        else if (!hasFilesToCopy && SSDCopyQueueInstance)
        {
            SSDCopyQueueInstance->DecrementPendingSources();
            Log.Info(std::string("[Sync Engine] No files to copy for source ") + std::to_string(MetaDataCacheBinFileNumber));
            
            for (const auto& fileInfo : freshFiles)
            {
                cache.UpdateEntry(fileInfo.AbsolutePath, fileInfo);
            }

            cache.RemoveStaleEntries(ConfigGlobal::StaleEntries);

            if (!cache.Save(MetaDataCacheBinFileNumber))
            {
                Log.Error(std::string("[UpdateCacheForSource] Failed to Save Cache File Bin ID: ") + std::to_string(MetaDataCacheBinFileNumber));
            }
        }
    }
    else
	{
		std::queue<FileInfo> copyQueue;

		for (const auto& file : freshFiles)
		{
			const std::string& absPath = file.AbsolutePath;

			bool isNew = !cache.HasEntry(absPath);
			bool isChanged = false;

			if (!isNew)
			{
				FileInfo cached = cache.GetEntry(absPath);
				if (cached.Hash != file.Hash)
				{
					isChanged = true;
				}
			}
			if (isNew || isChanged)
			{
				Log.Info(std::string("[Sync Engine] Added to HDDCopyQueue: ") + absPath);
				copyQueue.emplace(file);
			}
			else
			{
				Log.Info(std::string("[Sync Engine] File Skipped: ") + absPath);
			}
			cache.MarkVisited(absPath);
		}
		if (!copyQueue.empty() && HDDCopyQueueInstance)
		{
			std::string& firstPath = copyQueue.front().AbsolutePath;
			Log.Info(std::string("[Sync Engine] Submitting Queue for Source: ") + firstPath + std::string(" | Files = ") + std::to_string(copyQueue.size()));
			HDDCopyQueueInstance->SubmitCopyQueue(MetaDataCacheBinFileNumber, std::move(copyQueue), std::move(freshFiles));
		}
		else if (copyQueue.empty() && HDDCopyQueueInstance)
		{
			HDDCopyQueueInstance->DecrementPendingSources();
            Log.Info(std::string("[Sync Engine] No files to copy for source ") + std::to_string(MetaDataCacheBinFileNumber));
            
            for (const auto& fileInfo : freshFiles)
            {
                cache.UpdateEntry(fileInfo.AbsolutePath, fileInfo);
            }

            cache.RemoveStaleEntries(ConfigGlobal::StaleEntries);

            if (!cache.Save(MetaDataCacheBinFileNumber))
            {
                Log.Error(std::string("[UpdateCacheForSource] Failed to Save Cache File Bin ID: ") + std::to_string(MetaDataCacheBinFileNumber));
            }
		}
	}
}
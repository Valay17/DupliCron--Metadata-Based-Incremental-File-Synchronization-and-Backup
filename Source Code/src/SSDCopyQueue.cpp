#include "SSDCopyQueue.hpp"
#include "ConfigGlobal.hpp"
#include "FileCopier.hpp"
#include <iostream>
#include <filesystem>

void SSDCopyQueue::Initialize(SSDMode SSDCopyMode)
{
    CopyMode = SSDCopyMode;
    SSDLargeFileThreadRunning = false;
    SSDPendingSources = 0;
    SSDAllSourcesSubmitted = false;
}

SSDCopyQueue::~SSDCopyQueue()
{
    Stop();
}

void SSDCopyQueue::Start()
{
    if (!SSDCopyStateCache.LoadCopiedState())
    {
        std::cerr << "[ERROR] Failed to load copy state file." << std::endl;
        Log.Error("[SSDCopyQueue] Failed to load copy state file.");
    }

    SSDLargeFileThreadRunning = true;
    SmallFileThreadPool = std::make_unique<ThreadPool>(ConfigGlobal::ParallelFilesPerSourceCount);

    if (CopyMode == SSDMode::Balanced || CopyMode == SSDMode::Sequential)
    {
        SSDLargeFileThread = std::thread(&SSDCopyQueue::LargeFileWorker, this);
    }
    else if(CopyMode == SSDMode::GodSpeed)
    {
        GodSpeedSourcePool = std::make_unique<ThreadPool>(ConfigGlobal::GodSpeedParallelSourcesCount);
    }
}

void SSDCopyQueue::Stop()
{
    {
        std::lock_guard<std::mutex> lock(SSDQueueMutex);
        SSDLargeFileThreadRunning = false;
    }
    SSDLargeQueueCV.notify_all();

    if (SSDLargeFileThread.joinable())
    {
        SSDLargeFileThread.join();
    }
}

void SSDCopyQueue::IncrementPendingSources()
{
    std::lock_guard<std::mutex> lock(SSDQueueMutex);
    ++SSDPendingSources;
}

void SSDCopyQueue::DecrementPendingSources()
{
    std::lock_guard<std::mutex> lock(SSDQueueMutex);
    if (SSDPendingSources > 0)
        --SSDPendingSources;
    SSDSourcesDoneCV.notify_all();
}

void SSDCopyQueue::MarkAllSourcesSubmitted()
{
    std::lock_guard<std::mutex> lock(SSDQueueMutex);
    SSDAllSourcesSubmitted = true;
    SSDSourcesDoneCV.notify_all();
}

void SSDCopyQueue::WaitUntilDone()
{
    std::unique_lock<std::mutex> lock(SSDQueueMutex);
    SSDSourcesDoneCV.wait(lock, [this]()
        {
            return SSDPendingSources == 0 && SSDLargeFileQueue.empty() && SSDAllSourcesSubmitted;
        });
}

void SSDCopyQueue::SubmitCopyQueues(uint32_t sourceID, std::queue<FileInfo>&& smallFiles, std::queue<FileInfo>&& largeFiles, std::vector<FileInfo> freshFiles)
{
    {
        std::lock_guard<std::mutex> lock(SSDQueueMutex);

        auto [it, inserted] = SSDSourceStatusMap.try_emplace(sourceID);

        if (inserted)
        {
            it->second.SmallDone.store(false);
            it->second.LargeDone.store(false);
        }
        it->second.FreshFiles = std::move(freshFiles);
    }
    Log.Info("[SSDCopyQueue] Submitting copy queues for source " + std::to_string(sourceID) + " small files: " + std::to_string(smallFiles.size()) + ", large files: " + std::to_string(largeFiles.size()));

    if (!smallFiles.empty())
    {
        switch (CopyMode)
        {
        case SSDMode::Sequential:
            MarkQueueDoneAndCheck(sourceID, true);
        break;

        case SSDMode::Parallel:
        case SSDMode::Balanced:
            ProcessSmallFiles(sourceID, std::move(smallFiles));
        break;

		case SSDMode::GodSpeed:
			std::vector<FileInfo> fileVec;
			size_t count = smallFiles.size();
			fileVec.reserve(count);
			while (!smallFiles.empty())
			{
				fileVec.push_back(std::move(smallFiles.front()));
				smallFiles.pop();
			}

			// Submit entire source copy job to GodSpeedSourcePool
			GodSpeedSourcePool->Submit([this, sourceID, fileVec = std::move(fileVec), freshFiles = std::move(freshFiles), largeFiles = std::move(largeFiles)]() mutable
		    {
				// Lock to safely access/modify per-source thread pools map
				std::shared_ptr<ThreadPool> perSourcePool;
				{
					std::lock_guard<std::mutex> lock(GodSpeedThreadPoolMapMutex);

					// Find or create per-source thread pool
					auto it = GodSpeedPerSourceThreadPools.find(sourceID);
					if (it == GodSpeedPerSourceThreadPools.end())
					{
						perSourcePool = std::make_shared<ThreadPool>(ConfigGlobal::GodSpeedParallelFilesPerSourcesCount);
						GodSpeedPerSourceThreadPools[sourceID] = perSourcePool;
					}
					else
					{
						perSourcePool = it->second;
					}
				}

				// Track how many files completed
				auto filesProcessed = std::make_shared<std::atomic<size_t>>(0);
				size_t totalFiles = fileVec.size();

				// Submit each small file copy to the per-source thread pool
				for (auto& file : fileVec)
				{
					perSourcePool->Submit([this, file, sourceID, filesProcessed, totalFiles]()
						{
                            std::string SourceTopRootPath = SSDCopyStateCache.GetPathFromSourceID(sourceID);
                            bool success = FileCopier::PerformFileCopy(file.AbsolutePath, SourceTopRootPath);
							if (!success)
							{
								Log.Error("[SSDCopyQueue] File copy failed (small files queue): " + file.AbsolutePath);
							}
							size_t done = ++(*filesProcessed);
							if (done == totalFiles)
							{
								MarkQueueDoneAndCheck(sourceID, true);
							}
						});
				}

				if (!largeFiles.empty())
				{
					MarkQueueDoneAndCheck(sourceID, false);
				}
				else
				{
					MarkQueueDoneAndCheck(sourceID, false);
				}
			});
			break;
		}
    }
    else
    {
        MarkQueueDoneAndCheck(sourceID, true);
    }

    if (!largeFiles.empty())
    {
        switch (CopyMode)
        {
        case SSDMode::Sequential:
        case SSDMode::Balanced:
        {
            std::lock_guard<std::mutex> lock(SSDQueueMutex);
            SSDLargeFileQueue.push(std::make_pair(sourceID, std::move(largeFiles)));
            SSDLargeQueueCV.notify_one();
        }
        break;

        case SSDMode::Parallel:
        case SSDMode::GodSpeed:
            MarkQueueDoneAndCheck(sourceID, false);
        break;
        }
    }
    else
    {
        MarkQueueDoneAndCheck(sourceID, false);
    }
}

void SSDCopyQueue::ProcessSmallFiles(uint32_t sourceID, std::queue<FileInfo>&& files)
{
    if (files.empty())
    {
        MarkQueueDoneAndCheck(sourceID, true);
        return;
    }

    size_t fileCount = files.size();

    Log.Info("[SSDCopyQueue] Processing " + std::to_string(fileCount) + " small files for source " + std::to_string(sourceID) + " in parallel.");

    std::vector<FileInfo> fileVec;
    fileVec.reserve(fileCount);
    while (!files.empty())
    {
        fileVec.push_back(std::move(files.front()));
        files.pop();
    }

    auto filesProcessed = std::make_shared<std::atomic<size_t>>(0);

    for (auto& file : fileVec)
    {
        SmallFileThreadPool->Submit([this, file, sourceID, filesProcessed, fileCount]() mutable {
            std::string SourceTopRootPath = SSDCopyStateCache.GetPathFromSourceID(sourceID);
            bool success = FileCopier::PerformFileCopy(file.AbsolutePath, SourceTopRootPath);
            if (success)
            {
                size_t done = ++(*filesProcessed);
                if (done == fileCount)
                {
                    MarkQueueDoneAndCheck(sourceID, true);
                }
            }
            else
            {
                Log.Error("[SSDCopyQueue] File copy failed (small files queue): " + file.AbsolutePath);

            }
        });
    }
}

void SSDCopyQueue::LargeFileWorker()
{
    while (true)
    {
        std::pair<uint32_t, std::queue<FileInfo>> currentLargeQueue;
        {
            std::unique_lock<std::mutex> lock(SSDLargeQueueMutex);
            SSDLargeQueueCV.wait(lock, [this]() { return !SSDLargeFileQueue.empty() || !SSDLargeFileThreadRunning; });

            if (!SSDLargeFileThreadRunning && SSDLargeFileQueue.empty())
            {
                return;
            }
            {
                std::lock_guard<std::mutex> lock2(SSDQueueMutex);
                if (SSDLargeFileQueue.empty())
                {
                    continue;
                }
                currentLargeQueue = std::move(SSDLargeFileQueue.front());
                SSDLargeFileQueue.pop();
            }
        }

        uint32_t sourceID = currentLargeQueue.first;
        std::queue<FileInfo>& fileQueue = currentLargeQueue.second;

        Log.Info("[SSDCopyQueue] Processing large files sequentially for source " + std::to_string(sourceID) + ", file count: " + std::to_string(fileQueue.size()));

        while (!fileQueue.empty())
        {
            FileInfo file = std::move(fileQueue.front());
            fileQueue.pop();

            std::string SourceTopRootPath = SSDCopyStateCache.GetPathFromSourceID(sourceID);
            bool success = FileCopier::PerformFileCopy(file.AbsolutePath, SourceTopRootPath);
            if (!success)
            {
                Log.Error("[SSDCopyQueue] File copy failed (large files queue): " + file.AbsolutePath);
            }
        }
        MarkQueueDoneAndCheck(sourceID, false);
    }
}

void SSDCopyQueue::MarkQueueDoneAndCheck(uint32_t sourceID, bool isSmallQueue)
{
    std::lock_guard<std::mutex> lock(SSDQueueMutex);

    auto it = SSDSourceStatusMap.find(sourceID);
    if (it == SSDSourceStatusMap.end())
    {
        Log.Error("[SSDCopyQueue] MarkQueueDoneAndCheck called for unknown source: " + std::to_string(sourceID));
        return;
    }

    SourceCopyStatus& status = it->second;

    if (isSmallQueue)
        status.SmallDone.store(true);
    else
        status.LargeDone.store(true);

    if (status.SmallDone.load() && status.LargeDone.load())
    {
        // Batch update cache after all files copied for source
        for (const auto& fileInfo : status.FreshFiles)
        {
            SSDCopyStateCache.UpdateEntry(fileInfo.AbsolutePath, fileInfo);
        }

        SSDCopyStateCache.RemoveStaleEntries(ConfigGlobal::StaleEntries);

        if (!SSDCopyStateCache.Save(sourceID))
        {
            Log.Error("[SSDCopyQueue] Failed to save cache for source: " + std::to_string(sourceID));
        }

        SSDCopyStateCache.MarkCopied(sourceID);

        SSDSourceStatusMap.erase(it);

        size_t remaining = --SSDPendingSources;
        if (remaining == 0 && SSDAllSourcesSubmitted)
        {
            SSDSourcesDoneCV.notify_all();
        }
    }
}
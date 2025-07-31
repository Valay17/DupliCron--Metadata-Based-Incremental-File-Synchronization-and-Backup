#include "HDDCopyQueue.hpp"
#include "ConfigGlobal.hpp"
#include "FileCopier.hpp"
#include <filesystem>
#include <iostream>

HDDCopyQueue::HDDCopyQueue(): HDDRunning(false)
{
}

HDDCopyQueue::~HDDCopyQueue()
{
    Stop();
}

void HDDCopyQueue::Start()
{
    if (!HDDCopyStateCache.LoadCopiedState())
    {
        std::cerr << "[ERROR] Failed to load state file for copy tracking.\n";
        Log.Error(std::string("[HDDCopyQueue - CopyStateCache] Failed to Load State File for Copy Tracking."));
    }
    HDDRunning = true;
    HDDCopyThread = std::thread(&HDDCopyQueue::CopyThreadLoop, this);
}

void HDDCopyQueue::Stop()
{
    {
        std::lock_guard<std::mutex> lock(HDDCQ_Mutex);
        HDDRunning = false;
    }
    HDD_CV.notify_all();

    if (HDDCopyThread.joinable())
    {
        HDDCopyThread.join();
    }
}

void HDDCopyQueue::IncrementPendingSources()
{
    std::lock_guard<std::mutex> lock(HDDCQ_Mutex);
    ++HDDPendingSources;
}

void HDDCopyQueue::DecrementPendingSources()
{
    std::lock_guard<std::mutex> lock(HDDCQ_Mutex);
    if (HDDPendingSources > 0)
        --HDDPendingSources;
    HDD_CV.notify_all();
}

void HDDCopyQueue::MarkAllSourcesSubmitted()
{
    std::lock_guard<std::mutex> lock(HDDCQ_Mutex);
    HDDAllSourcesSubmitted = true;
    HDD_CV.notify_all();
}

void HDDCopyQueue::WaitUntilDone()
{
    std::unique_lock<std::mutex> lock(HDDCQ_Mutex);
    HDD_CV.wait(lock, [this]() {return HDDPendingSources == 0 && HDDGlobalCopyQueue.empty() && HDDAllSourcesSubmitted; });
}

void HDDCopyQueue::SubmitCopyQueue(uint32_t BinID, std::queue<FileInfo>&& queue, std::vector<FileInfo>&& freshFiles)
{
    Log.Info(std::string("[HDDCopyQueue] Received Queue for Source BinID = ") + std::to_string(BinID) +
        std::string(" | Files: ") + std::to_string(queue.size()));

    {
        std::lock_guard<std::mutex> lock(HDDCQ_Mutex);
        CopyTask task;
        task.FileQueue = std::move(queue);
        task.FreshFiles = std::move(freshFiles);
        HDDGlobalCopyQueue.emplace(BinID, std::move(task));
    }
    HDD_CV.notify_one();
}


void HDDCopyQueue::CopyThreadLoop()
{
    while (HDDRunning)
    {
        std::unique_lock<std::mutex> lock(HDDCQ_Mutex);
        HDD_CV.wait(lock, [this]() { return !HDDGlobalCopyQueue.empty() || (HDDPendingSources == 0 && HDDAllSourcesSubmitted); });

        if (HDDGlobalCopyQueue.empty())
        {
            if (HDDPendingSources == 0 && HDDAllSourcesSubmitted)
                break;
            else
                continue;
        }

        auto [BinID, task] = std::move(HDDGlobalCopyQueue.front());
        std::queue<FileInfo>& FileQueue = task.FileQueue;
        std::vector<FileInfo>& FreshFiles = task.FreshFiles;
        HDDGlobalCopyQueue.pop();
        lock.unlock();

        std::unordered_set<std::string> SuccessSet;
        size_t OriginalFileCount = FileQueue.size();

        while (!FileQueue.empty())
        {
            FileInfo file = std::move(FileQueue.front());
            FileQueue.pop();

            std::string SourceTopRootPath = HDDCopyStateCache.GetPathFromSourceID(BinID);
            if (FileCopier::PerformFileCopy(file.AbsolutePath, SourceTopRootPath))
            {
                SuccessSet.insert(file.AbsolutePath);
            }
            else
            {
                Log.Error(std::string("[HDDCopyQueue] Copy failed for ") + file.AbsolutePath);
            }
        }
        if (SuccessSet.size() != OriginalFileCount)
        {
            Log.Error(std::string("[HDDCopyQueue] Not all files copied for BinID: ") + std::to_string(BinID));
            DecrementPendingSources();
            continue; // Skip marking as copied
        }
        HDDCopyStateCache.MarkCopied(BinID); // Mark as fully copied
        Log.Info(std::string("[HDDCopyQueue] All files Copied for BinID: ") + std::to_string(BinID));
        for (const auto& fileInfo : FreshFiles)
        {
            HDDCopyStateCache.UpdateEntry(fileInfo.AbsolutePath, fileInfo);
        }
        HDDCopyStateCache.RemoveStaleEntries(ConfigGlobal::StaleEntries);
        if (!HDDCopyStateCache.Save(BinID))
        {
            Log.Error(std::string("[UpdateCacheForSource] Failed to Save Cache File Bin ID: ") + std::to_string(BinID));
        }
        DecrementPendingSources();
    }
}
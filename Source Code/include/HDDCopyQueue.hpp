#pragma once

#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <utility>
#include "MetaDataCache.hpp"
#include "Logger.hpp"

struct CopyTask
{
    std::queue<FileInfo> FileQueue;
    std::vector<FileInfo> FreshFiles;
};

class HDDCopyQueue
{
public:
    HDDCopyQueue();
    ~HDDCopyQueue();

    // Non-copyable
    HDDCopyQueue(const HDDCopyQueue&) = delete;
    HDDCopyQueue& operator=(const HDDCopyQueue&) = delete;
    
    void Start();
    void Stop();

    void SubmitCopyQueue(uint32_t BinID, std::queue<FileInfo>&& queue, std::vector<FileInfo>&& freshFiles);
    void IncrementPendingSources();
    void DecrementPendingSources();
    void MarkAllSourcesSubmitted();
    void WaitUntilDone();

private:
    void CopyThreadLoop();

    MetaDataCache HDDCopyStateCache;

    std::queue<std::pair<uint32_t, CopyTask>> HDDGlobalCopyQueue;
    std::mutex HDDCQ_Mutex;
    std::condition_variable HDD_CV;
    std::thread HDDCopyThread;
    std::atomic<bool> HDDRunning;
    std::atomic<bool> HDDAllSourcesSubmitted = false;
    std::atomic<int> HDDPendingSources = 0;
};
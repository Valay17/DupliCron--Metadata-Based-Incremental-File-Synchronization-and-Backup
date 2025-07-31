#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>

#include "ThreadPool.hpp"
#include "MetaDataCache.hpp"
#include "Logger.hpp"

enum class SSDMode
{
    Sequential,
    Parallel,
    Balanced,
    GodSpeed
};

inline SSDMode ToSSDMode(const std::string& modeStr)
{
    static const std::unordered_map<std::string, SSDMode> ModeMap = {
        { "Sequential", SSDMode::Sequential },
        { "Parallel",   SSDMode::Parallel },
        { "Balanced",   SSDMode::Balanced },
        { "GodSpeed",   SSDMode::GodSpeed }
    };

    auto it = ModeMap.find(modeStr);
    return (it != ModeMap.end()) ? it->second : SSDMode::Balanced; // fallback
}

struct SourceCopyStatus
{
    std::atomic<bool> SmallDone{ false };
    std::atomic<bool> LargeDone{ false };
    std::mutex Mutex;
    std::vector<FileInfo> FreshFiles;
};

class SSDCopyQueue
{
public:
    SSDCopyQueue() = default;
    ~SSDCopyQueue();

    void Initialize(SSDMode SSDCopyMode);

    void Start();
    void Stop();
    void WaitUntilDone();

    void SubmitCopyQueues(uint32_t sourceID, std::queue<FileInfo>&& smallFiles, std::queue<FileInfo>&& largeFiles, std::vector<FileInfo> freshFiles);

    void IncrementPendingSources();
    void DecrementPendingSources();
    void MarkAllSourcesSubmitted();

private:
    
    SSDMode CopyMode;
    MetaDataCache SSDCopyStateCache;

    //Small File Queue
    std::unique_ptr<ThreadPool> SmallFileThreadPool;
    
    //GodSpeed
    std::unique_ptr<ThreadPool> GodSpeedSourcePool;
    std::unordered_map<uint32_t, std::shared_ptr<ThreadPool>> GodSpeedPerSourceThreadPools;
    std::mutex GodSpeedThreadPoolMapMutex;

    //Large File Queue
    std::mutex SSDLargeQueueMutex;
    std::condition_variable SSDLargeQueueCV;
    std::atomic<bool> SSDLargeFileThreadRunning;
    std::thread SSDLargeFileThread;
    std::queue<std::pair<uint32_t, std::queue<FileInfo>>> SSDLargeFileQueue;

    //State Completion
    std::mutex SSDQueueMutex;
    std::condition_variable SSDSourcesDoneCV;
    std::atomic<size_t> SSDPendingSources;
    bool SSDAllSourcesSubmitted;
    std::unordered_map<uint32_t, SourceCopyStatus> SSDSourceStatusMap;

    void LargeFileWorker();
    void ProcessSmallFiles(uint32_t sourceID, std::queue<FileInfo>&& files);
    void MarkQueueDoneAndCheck(uint32_t sourceID, bool isSmallQueue);
};
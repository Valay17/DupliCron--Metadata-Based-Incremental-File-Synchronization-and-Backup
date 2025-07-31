#pragma once

#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>

class ThreadPool
{
public:
    ThreadPool() = default;
    explicit ThreadPool(size_t ThreadCount);
    ~ThreadPool();

    void Submit(std::function<void()> Job);

    void Join();

private:
    std::vector<std::thread> Workers;
    std::queue<std::function<void()>> Jobs;

    std::mutex ThreadPoolMutex;
    std::condition_variable ThreadPool_CV;
    std::atomic<bool> ThreadPoolStop;
    std::atomic<int> ThreadPoolActiveJobs;

    void WorkerThread();
};
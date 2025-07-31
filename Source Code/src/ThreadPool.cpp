#include "ThreadPool.hpp"

ThreadPool::ThreadPool(size_t ThreadCount): ThreadPoolStop(false), ThreadPoolActiveJobs(0)
{
    for (size_t i = 0; i < ThreadCount; ++i)
    {
        Workers.emplace_back(&ThreadPool::WorkerThread, this);
    }
}

ThreadPool::~ThreadPool()
{
    {
        std::unique_lock<std::mutex> Lock(ThreadPoolMutex);
        ThreadPoolStop = true;
    }
    ThreadPool_CV.notify_all();
    for (std::thread& Worker : Workers)
    {
        if (Worker.joinable())
        {
            Worker.join();
        }
    }
}

void ThreadPool::Submit(std::function<void()> Job)
{
    {
        std::unique_lock<std::mutex> Lock(ThreadPoolMutex);
        Jobs.push(std::move(Job));
        ++ThreadPoolActiveJobs;
    }
    ThreadPool_CV.notify_one();
}

void ThreadPool::Join()
{
    while (true)
    {
        if (Jobs.empty() && ThreadPoolActiveJobs.load() == 0)
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void ThreadPool::WorkerThread()
{
    while (true)
    {
        std::function<void()> Job;
        {
            std::unique_lock<std::mutex> Lock(ThreadPoolMutex);
            ThreadPool_CV.wait(Lock, [this] { return ThreadPoolStop || !Jobs.empty(); });
            if (ThreadPoolStop && Jobs.empty())
            {
                return;
            }
            Job = std::move(Jobs.front());
            Jobs.pop();
        }
        Job();
        --ThreadPoolActiveJobs;
    }
}
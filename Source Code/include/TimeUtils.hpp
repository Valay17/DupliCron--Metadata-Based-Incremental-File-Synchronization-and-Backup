#include <filesystem>
#include <chrono>
#include <ctime>

//UNIX Time since Epoch
inline int64_t ToTimeT(std::filesystem::file_time_type FTime)
{
    using namespace std::chrono;
    return duration_cast<nanoseconds>(FTime.time_since_epoch()).count();
}
#pragma once

namespace FailureDetect
{
    bool MarkFailure();
    bool MarkSuccess();
    bool WasLastSuccess();
    bool WasLastFailure();
    bool RunFailureRecovery();
    void CheckCacheIntegrity();
}
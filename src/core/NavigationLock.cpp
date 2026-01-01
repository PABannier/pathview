#include "NavigationLock.h"

NavigationLock::NavigationLock()
    : isLocked_(false)
    , ownerUUID_("")
    , grantedTime_()
    , ttlMs_(0)
    , clientFd_(INVALID_SOCKET_VALUE)
{
}

bool NavigationLock::IsExpired() const {
    if (!isLocked_) {
        return false;
    }

    auto now = std::chrono::steady_clock::now();
    return (now - grantedTime_) >= ttlMs_;
}

bool NavigationLock::IsOwnedBy(const std::string& uuid) const {
    return isLocked_ && ownerUUID_ == uuid;
}

void NavigationLock::Reset() {
    isLocked_ = false;
    ownerUUID_ = "";
    grantedTime_ = std::chrono::steady_clock::time_point();
    ttlMs_ = std::chrono::milliseconds(0);
    clientFd_ = INVALID_SOCKET_VALUE;
}

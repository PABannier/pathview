#pragma once

#include <string>
#include <chrono>

// Cross-platform socket type
#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    using socket_t = SOCKET;
    #define INVALID_SOCKET_VALUE INVALID_SOCKET
#else
    using socket_t = int;
    #define INVALID_SOCKET_VALUE (-1)
#endif

/**
 * Navigation lock state
 * Prevents user input (keyboard + mouse) when locked by an external agent
 */
class NavigationLock {
public:
    NavigationLock();
    ~NavigationLock() = default;

    // Check if the lock has expired
    bool IsExpired() const;

    // Check if the lock is owned by the given UUID
    bool IsOwnedBy(const std::string& uuid) const;

    // Reset lock to unlocked state
    void Reset();

    bool IsLocked() const { return isLocked_; }
    const std::string& GetOwnerUUID() const { return ownerUUID_; }
    std::chrono::steady_clock::time_point GetGrantedTime() const { return grantedTime_; }
    std::chrono::milliseconds GetTTL() const { return ttlMs_; }
    socket_t GetClientFd() const { return clientFd_; }

    void SetLocked(bool locked) { isLocked_ = locked; }
    void SetOwnerUUID(const std::string& uuid) { ownerUUID_ = uuid; }
    void SetGrantedTime(std::chrono::steady_clock::time_point time) { grantedTime_ = time; }
    void SetTTL(std::chrono::milliseconds ttl) { ttlMs_ = ttl; }
    void SetClientFd(socket_t fd) { clientFd_ = fd; }

private:
    bool isLocked_;
    std::string ownerUUID_;  // UUID as string (36 chars)
    std::chrono::steady_clock::time_point grantedTime_;
    std::chrono::milliseconds ttlMs_;
    socket_t clientFd_;  // IPC client socket (-1/INVALID_SOCKET if none)
};

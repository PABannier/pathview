#pragma once

#include <string>
#include <vector>
#include <chrono>

namespace pathview {

/**
 * Status enum for action cards
 */
enum class ActionCardStatus {
    PENDING,      // Card created but not started
    IN_PROGRESS,  // Card is actively being worked on
    COMPLETED,    // Card finished successfully
    FAILED,       // Card failed with error
    CANCELLED     // Card was cancelled/aborted
};

/**
 * Log entry for incremental updates to action card
 */
struct ActionCardLogEntry {
    std::chrono::system_clock::time_point timestamp;
    std::string message;
    std::string level;  // "info", "warning", "error", "success"

    ActionCardLogEntry(const std::string& msg, const std::string& lvl = "info")
        : timestamp(std::chrono::system_clock::now())
        , message(msg)
        , level(lvl) {}
};

/**
 * Action Card model
 * Represents an AI-driven action with progress tracking
 */
struct ActionCard {
    std::string id;                                    // Unique identifier (UUID)
    std::string title;                                 // Short title
    ActionCardStatus status;                           // Current status
    std::string summary;                               // Brief description
    std::string reasoning;                             // Optional detailed reasoning (collapsible)
    std::chrono::system_clock::time_point createdAt;   // Creation timestamp
    std::chrono::system_clock::time_point updatedAt;   // Last update timestamp
    std::vector<ActionCardLogEntry> logEntries;        // Ordered log of events
    std::string ownerUUID;                             // UUID of agent/lock owner who created this

    ActionCard(const std::string& id_, const std::string& title_)
        : id(id_)
        , title(title_)
        , status(ActionCardStatus::PENDING)
        , createdAt(std::chrono::system_clock::now())
        , updatedAt(std::chrono::system_clock::now()) {}

    // Append a log entry and update timestamp
    void AppendLog(const std::string& message, const std::string& level = "info");

    // Update status and timestamp
    void UpdateStatus(ActionCardStatus newStatus);

    // Helper to get status as string
    static std::string StatusToString(ActionCardStatus status);
    static ActionCardStatus StringToStatus(const std::string& statusStr);
};

} // namespace pathview

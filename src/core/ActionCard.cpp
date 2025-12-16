#include "ActionCard.h"
#include <stdexcept>

namespace pathview {

void ActionCard::AppendLog(const std::string& message, const std::string& level) {
    logEntries.emplace_back(message, level);
    updatedAt = std::chrono::system_clock::now();
}

void ActionCard::UpdateStatus(ActionCardStatus newStatus) {
    status = newStatus;
    updatedAt = std::chrono::system_clock::now();
}

std::string ActionCard::StatusToString(ActionCardStatus status) {
    switch (status) {
        case ActionCardStatus::PENDING:
            return "pending";
        case ActionCardStatus::IN_PROGRESS:
            return "in_progress";
        case ActionCardStatus::COMPLETED:
            return "completed";
        case ActionCardStatus::FAILED:
            return "failed";
        case ActionCardStatus::CANCELLED:
            return "cancelled";
        default:
            return "unknown";
    }
}

ActionCardStatus ActionCard::StringToStatus(const std::string& statusStr) {
    if (statusStr == "pending") {
        return ActionCardStatus::PENDING;
    } else if (statusStr == "in_progress") {
        return ActionCardStatus::IN_PROGRESS;
    } else if (statusStr == "completed") {
        return ActionCardStatus::COMPLETED;
    } else if (statusStr == "failed") {
        return ActionCardStatus::FAILED;
    } else if (statusStr == "cancelled") {
        return ActionCardStatus::CANCELLED;
    } else {
        throw std::invalid_argument("Invalid action card status: " + statusStr);
    }
}

} // namespace pathview

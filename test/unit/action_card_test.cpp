#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include "../../src/core/ActionCard.h"

using namespace pathview;

class ActionCardTest : public ::testing::Test {
protected:
    ActionCard card{"test-id-123", "Test Action"};
};

TEST_F(ActionCardTest, InitialState) {
    EXPECT_EQ(card.id, "test-id-123");
    EXPECT_EQ(card.title, "Test Action");
    EXPECT_EQ(card.status, ActionCardStatus::PENDING);
    EXPECT_TRUE(card.summary.empty());
    EXPECT_TRUE(card.reasoning.empty());
    EXPECT_TRUE(card.logEntries.empty());
    EXPECT_TRUE(card.ownerUUID.empty());
}

TEST_F(ActionCardTest, StatusTransitions) {
    EXPECT_EQ(card.status, ActionCardStatus::PENDING);

    card.UpdateStatus(ActionCardStatus::IN_PROGRESS);
    EXPECT_EQ(card.status, ActionCardStatus::IN_PROGRESS);

    card.UpdateStatus(ActionCardStatus::COMPLETED);
    EXPECT_EQ(card.status, ActionCardStatus::COMPLETED);
}

TEST_F(ActionCardTest, LogAppending) {
    EXPECT_EQ(card.logEntries.size(), 0);

    card.AppendLog("First message", "info");
    EXPECT_EQ(card.logEntries.size(), 1);
    EXPECT_EQ(card.logEntries[0].message, "First message");
    EXPECT_EQ(card.logEntries[0].level, "info");

    card.AppendLog("Second message", "warning");
    EXPECT_EQ(card.logEntries.size(), 2);
    EXPECT_EQ(card.logEntries[1].message, "Second message");
    EXPECT_EQ(card.logEntries[1].level, "warning");
}

TEST_F(ActionCardTest, LogEntryTimestamps) {
    auto before = std::chrono::system_clock::now();
    card.AppendLog("Timed message");
    auto after = std::chrono::system_clock::now();

    ASSERT_EQ(card.logEntries.size(), 1);
    auto timestamp = card.logEntries[0].timestamp;

    EXPECT_GE(timestamp, before);
    EXPECT_LE(timestamp, after);
}

TEST_F(ActionCardTest, LogEntryOrdering) {
    card.AppendLog("First");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    card.AppendLog("Second");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    card.AppendLog("Third");

    ASSERT_EQ(card.logEntries.size(), 3);
    EXPECT_LT(card.logEntries[0].timestamp, card.logEntries[1].timestamp);
    EXPECT_LT(card.logEntries[1].timestamp, card.logEntries[2].timestamp);
}

TEST_F(ActionCardTest, UpdateTimestamp) {
    auto created = card.createdAt;

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    card.UpdateStatus(ActionCardStatus::IN_PROGRESS);

    EXPECT_GT(card.updatedAt, created);
}

TEST_F(ActionCardTest, StatusToStringConversion) {
    EXPECT_EQ(ActionCard::StatusToString(ActionCardStatus::PENDING), "pending");
    EXPECT_EQ(ActionCard::StatusToString(ActionCardStatus::IN_PROGRESS), "in_progress");
    EXPECT_EQ(ActionCard::StatusToString(ActionCardStatus::COMPLETED), "completed");
    EXPECT_EQ(ActionCard::StatusToString(ActionCardStatus::FAILED), "failed");
    EXPECT_EQ(ActionCard::StatusToString(ActionCardStatus::CANCELLED), "cancelled");
}

TEST_F(ActionCardTest, StringToStatusConversion) {
    EXPECT_EQ(ActionCard::StringToStatus("pending"), ActionCardStatus::PENDING);
    EXPECT_EQ(ActionCard::StringToStatus("in_progress"), ActionCardStatus::IN_PROGRESS);
    EXPECT_EQ(ActionCard::StringToStatus("completed"), ActionCardStatus::COMPLETED);
    EXPECT_EQ(ActionCard::StringToStatus("failed"), ActionCardStatus::FAILED);
    EXPECT_EQ(ActionCard::StringToStatus("cancelled"), ActionCardStatus::CANCELLED);
}

TEST_F(ActionCardTest, InvalidStatusString) {
    EXPECT_THROW(ActionCard::StringToStatus("invalid_status"), std::invalid_argument);
}

TEST_F(ActionCardTest, OwnershipTracking) {
    card.ownerUUID = "agent-uuid-abc-123";
    EXPECT_EQ(card.ownerUUID, "agent-uuid-abc-123");
}

TEST_F(ActionCardTest, ReasoningField) {
    card.reasoning = "This is the detailed reasoning for the action.";
    EXPECT_EQ(card.reasoning, "This is the detailed reasoning for the action.");
}

// Test action card storage in a container (simulating Application storage)
TEST(ActionCardStorageTest, VectorStorage) {
    std::vector<ActionCard> cards;

    cards.emplace_back("id-1", "Card 1");
    cards.emplace_back("id-2", "Card 2");
    cards.emplace_back("id-3", "Card 3");

    EXPECT_EQ(cards.size(), 3);
    EXPECT_EQ(cards[0].id, "id-1");
    EXPECT_EQ(cards[1].id, "id-2");
    EXPECT_EQ(cards[2].id, "id-3");
}

TEST(ActionCardStorageTest, FindById) {
    std::vector<ActionCard> cards;
    cards.emplace_back("id-1", "Card 1");
    cards.emplace_back("id-2", "Card 2");

    auto it = std::find_if(cards.begin(), cards.end(),
        [](const ActionCard& c) { return c.id == "id-2"; });

    ASSERT_NE(it, cards.end());
    EXPECT_EQ(it->title, "Card 2");
}

TEST(ActionCardStorageTest, MaxCardsLimit) {
    std::vector<ActionCard> cards;
    const int MAX_CARDS = 5;

    // Add cards beyond limit
    for (int i = 0; i < 10; i++) {
        ActionCard card("id-" + std::to_string(i), "Card " + std::to_string(i));

        // Simulate limit enforcement
        if (cards.size() >= MAX_CARDS) {
            // Remove oldest completed card
            auto it = std::find_if(cards.begin(), cards.end(),
                [](const ActionCard& c) {
                    return c.status == ActionCardStatus::COMPLETED;
                });
            if (it != cards.end()) {
                cards.erase(it);
            }
        }

        if (i < 5) {
            card.UpdateStatus(ActionCardStatus::COMPLETED);
        }
        cards.push_back(card);
    }

    // Should not exceed MAX_CARDS
    EXPECT_LE(cards.size(), MAX_CARDS);
}

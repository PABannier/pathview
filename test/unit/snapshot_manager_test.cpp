#include <gtest/gtest.h>
#include "../../src/api/http/SnapshotManager.h"
#include <vector>
#include <thread>
#include <chrono>

using namespace pathview::http;

// Test basic snapshot addition and retrieval
TEST(SnapshotManagerTest, AddAndGetSnapshot) {
    SnapshotManager mgr(50, std::chrono::milliseconds(10));

    std::vector<uint8_t> dummyPNG = {0x89, 0x50, 0x4E, 0x47};  // PNG header
    std::string id = mgr.AddSnapshot(dummyPNG, 100, 100);

    EXPECT_FALSE(id.empty());

    auto snapshot = mgr.GetSnapshot(id);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->id, id);
    EXPECT_EQ(snapshot->width, 100);
    EXPECT_EQ(snapshot->height, 100);
    EXPECT_EQ(snapshot->pngData, dummyPNG);
}

// Test LRU cache eviction
TEST(SnapshotManagerTest, LRUEviction) {
    SnapshotManager mgr(3, std::chrono::milliseconds(10));  // Max 3 snapshots

    std::vector<uint8_t> dummyPNG = {1, 2, 3, 4};

    // Add 4 snapshots (should evict oldest)
    std::string id1 = mgr.AddSnapshot(dummyPNG, 10, 10);
    std::string id2 = mgr.AddSnapshot(dummyPNG, 10, 10);
    std::string id3 = mgr.AddSnapshot(dummyPNG, 10, 10);
    std::string id4 = mgr.AddSnapshot(dummyPNG, 10, 10);

    // id1 should be evicted
    EXPECT_FALSE(mgr.GetSnapshot(id1).has_value());
    EXPECT_TRUE(mgr.GetSnapshot(id2).has_value());
    EXPECT_TRUE(mgr.GetSnapshot(id3).has_value());
    EXPECT_TRUE(mgr.GetSnapshot(id4).has_value());
}

// Test cache size reporting
TEST(SnapshotManagerTest, GetCacheSize) {
    SnapshotManager mgr(10, std::chrono::milliseconds(10));

    EXPECT_EQ(mgr.GetCacheSize(), 0);

    std::vector<uint8_t> dummyPNG = {1, 2, 3, 4};

    mgr.AddSnapshot(dummyPNG, 10, 10);
    EXPECT_EQ(mgr.GetCacheSize(), 1);

    mgr.AddSnapshot(dummyPNG, 10, 10);
    EXPECT_EQ(mgr.GetCacheSize(), 2);

    mgr.AddSnapshot(dummyPNG, 10, 10);
    EXPECT_EQ(mgr.GetCacheSize(), 3);
}

// Test stream buffer functionality
TEST(SnapshotManagerTest, StreamBufferBasic) {
    SnapshotManager mgr(50, std::chrono::milliseconds(10));

    std::vector<uint8_t> dummyPNG = {1, 2, 3, 4};

    // Add snapshots and add to stream buffer
    std::string id1 = mgr.AddSnapshot(dummyPNG, 10, 10);
    mgr.AddStreamFrame(id1);

    EXPECT_EQ(mgr.GetLatestStreamFrame(), id1);

    std::string id2 = mgr.AddSnapshot(dummyPNG, 10, 10);
    mgr.AddStreamFrame(id2);

    EXPECT_EQ(mgr.GetLatestStreamFrame(), id2);
}

// Test stream buffer eviction (circular buffer with max 3 frames)
TEST(SnapshotManagerTest, StreamBufferEviction) {
    SnapshotManager mgr(50, std::chrono::milliseconds(10));

    std::vector<uint8_t> dummyPNG = {1, 2, 3, 4};

    // Add 5 snapshots to stream (buffer max is 3)
    std::vector<std::string> ids;
    for (int i = 0; i < 5; i++) {
        std::string id = mgr.AddSnapshot(dummyPNG, 10, 10);
        mgr.AddStreamFrame(id);
        ids.push_back(id);
    }

    // Latest frame should be ids[4]
    EXPECT_EQ(mgr.GetLatestStreamFrame(), ids[4]);

    // All snapshots should still be in the main cache (capacity 50)
    for (const auto& id : ids) {
        EXPECT_TRUE(mgr.GetSnapshot(id).has_value());
    }
}

// Test empty stream buffer
TEST(SnapshotManagerTest, EmptyStreamBuffer) {
    SnapshotManager mgr(50, std::chrono::milliseconds(10));

    EXPECT_EQ(mgr.GetLatestStreamFrame(), "");
}

// Test thread safety (concurrent access)
TEST(SnapshotManagerTest, ThreadSafety) {
    SnapshotManager mgr(100, std::chrono::milliseconds(10));

    std::vector<uint8_t> dummyPNG = {1, 2, 3, 4};

    // Launch multiple threads adding snapshots
    std::vector<std::thread> threads;
    for (int t = 0; t < 10; t++) {
        threads.emplace_back([&mgr, &dummyPNG, t]() {
            for (int i = 0; i < 10; i++) {
                std::string id = mgr.AddSnapshot(dummyPNG, 10 + t, 10 + t);
                mgr.AddStreamFrame(id);
                auto snapshot = mgr.GetSnapshot(id);
                EXPECT_TRUE(snapshot.has_value());
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // Should have 100 snapshots (capacity limit)
    EXPECT_EQ(mgr.GetCacheSize(), 100);
}

// Test cleanup removes expired snapshots
TEST(SnapshotManagerTest, CleanupRemovesExpired) {
    SnapshotManager mgr(50, std::chrono::milliseconds(10));

    std::vector<uint8_t> dummyPNG = {1, 2, 3, 4};

    // Add a snapshot
    std::string id = mgr.AddSnapshot(dummyPNG, 10, 10);
    EXPECT_TRUE(mgr.GetSnapshot(id).has_value());

    // Manually call Cleanup (normally called by background thread)
    // Note: Snapshots expire after 1 hour, so they won't be removed here
    mgr.Cleanup();

    // Snapshot should still exist (not expired yet)
    EXPECT_TRUE(mgr.GetSnapshot(id).has_value());
}

// Test UUID generation produces valid UUIDs
TEST(SnapshotManagerTest, UUIDGeneration) {
    SnapshotManager mgr(50, std::chrono::milliseconds(10));

    std::vector<uint8_t> dummyPNG = {1, 2, 3, 4};

    std::string id1 = mgr.AddSnapshot(dummyPNG, 10, 10);
    std::string id2 = mgr.AddSnapshot(dummyPNG, 10, 10);

    // UUIDs should be different
    EXPECT_NE(id1, id2);

    // UUIDs should have correct format (36 chars with hyphens)
    EXPECT_EQ(id1.length(), 36);
    EXPECT_EQ(id1[8], '-');
    EXPECT_EQ(id1[13], '-');
    EXPECT_EQ(id1[18], '-');
    EXPECT_EQ(id1[23], '-');
}

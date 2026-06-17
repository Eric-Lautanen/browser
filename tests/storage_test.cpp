#include "test_framework.hpp"
#include "utility.hpp"
#include "../net/storage.hpp"

using namespace browser;
using namespace browser::net;

TEST(storage_set_get, {
    auto& s = Storage::local_storage("test_origin_1");
    s.clear();

    ASSERT(s.set_item("key1", "value1").is_ok());
    auto val = s.get_item("key1");
    ASSERT(val.has_value());
    ASSERT_EQ(*val, "value1");
})

TEST(storage_get_missing, {
    auto& s = Storage::local_storage("test_origin_2");
    s.clear();

    auto val = s.get_item("nonexistent");
    ASSERT(!val.has_value());
})

TEST(storage_remove, {
    auto& s = Storage::local_storage("test_origin_3");
    s.clear();

    s.set_item("key1", "value1");
    s.remove_item("key1");
    auto val = s.get_item("key1");
    ASSERT(!val.has_value());
})

TEST(storage_clear, {
    auto& s = Storage::local_storage("test_origin_4");
    s.clear();

    s.set_item("a", "1");
    s.set_item("b", "2");
    s.clear();
    ASSERT_EQ(s.length(), 0u);
})

TEST(storage_length, {
    auto& s = Storage::local_storage("test_origin_5");
    s.clear();

    ASSERT_EQ(s.length(), 0u);
    s.set_item("x", "1");
    ASSERT_EQ(s.length(), 1u);
    s.set_item("y", "2");
    ASSERT_EQ(s.length(), 2u);
})

TEST(storage_key_by_index, {
    auto& s = Storage::local_storage("test_origin_6");
    s.clear();

    s.set_item("first", "a");
    s.set_item("second", "b");
    s.set_item("third", "c");

    ASSERT_EQ(s.key(0), "first");
    ASSERT_EQ(s.key(1), "second");
    ASSERT_EQ(s.key(2), "third");
    ASSERT_EQ(s.key(3), "");
})

TEST(storage_update_value, {
    auto& s = Storage::local_storage("test_origin_7");
    s.clear();

    s.set_item("key", "old");
    s.set_item("key", "new");
    auto val = s.get_item("key");
    ASSERT(val.has_value());
    ASSERT_EQ(*val, "new");
    ASSERT_EQ(s.length(), 1u);
})

TEST(storage_5mb_limit, {
    auto& s = Storage::local_storage("test_origin_8");
    s.clear();

    // Fill with ~4.5MB should succeed
    std::string big(4 * 1024 * 1024, 'x');
    ASSERT(s.set_item("big", big).is_ok());

    // Adding more data should fail (over 5MB)
    std::string big2(2 * 1024 * 1024, 'y');
    ASSERT(s.set_item("big2", big2).is_err());
})

TEST(storage_persistence, {
    Storage s1("persist_test");
    ASSERT(s1.set_item("persist_key", "persist_value").is_ok());
    ASSERT(s1.save_to_file("test_storage.bin").is_ok());

    Storage s2("persist_test");
    ASSERT(s2.load_from_file("test_storage.bin").is_ok());
    auto val = s2.get_item("persist_key");
    ASSERT(val.has_value());
    ASSERT_EQ(*val, "persist_value");

    std::remove("test_storage.bin");
})

TEST(storage_session, {
    auto& s = Storage::session_storage("session_origin");
    s.clear();

    s.set_item("session_key", "session_val");
    ASSERT_EQ(s.length(), 1u);
    auto val = s.get_item("session_key");
    ASSERT(val.has_value());
    ASSERT_EQ(*val, "session_val");
})

TEST(storage_remove_missing, {
    auto& s = Storage::local_storage("test_origin_9");
    s.clear();

    // Should not crash
    s.remove_item("does_not_exist");
    ASSERT_EQ(s.length(), 0u);
})

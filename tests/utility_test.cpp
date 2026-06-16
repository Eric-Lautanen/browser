#include "test_framework.hpp"
#include "utility.hpp"
#include <memory>

TEST(result_ok, {
    browser::Result<int> r(42);
    ASSERT(r.is_ok());
    ASSERT_EQ(r.unwrap(), 42);
})

TEST(result_err, {
    browser::Result<int> r(std::string("err"));
    ASSERT(r.is_err());
    ASSERT(r.unwrap_err() == "err");
})

TEST(result_move_ctor, {
    browser::Result<int> r1(42);
    browser::Result<int> r2(std::move(r1));
    ASSERT(r2.is_ok());
    ASSERT_EQ(r2.unwrap(), 42);
})

TEST(result_move_assign, {
    browser::Result<int> r1(42);
    browser::Result<int> r2(std::string("old"));
    r2 = std::move(r1);
    ASSERT(r2.is_ok());
    ASSERT_EQ(r2.unwrap(), 42);
})

TEST(result_move_unique, {
    auto p = std::make_unique<int>(42);
    browser::Result<std::unique_ptr<int>> r(std::move(p));
    ASSERT(r.is_ok());
    ASSERT_EQ(*r.unwrap(), 42);
})

TEST(result_move_unique_assign, {
    auto p1 = std::make_unique<int>(42);
    browser::Result<std::unique_ptr<int>> r1(std::move(p1));
    auto p2 = std::make_unique<int>(99);
    browser::Result<std::unique_ptr<int>> r2(std::move(p2));
    r2 = std::move(r1);
    ASSERT(r2.is_ok());
    ASSERT_EQ(*r2.unwrap(), 42);
})

TEST(result_void_ok, {
    browser::Result<void> r;
    ASSERT(r.is_ok());
    ASSERT(!r.is_err());
})

TEST(result_void_err, {
    browser::Result<void> r(std::string("error"));
    ASSERT(r.is_err());
    ASSERT(!r.is_ok());
    ASSERT(r.unwrap_err() == "error");
})

TEST(result_void_move_ok, {
    browser::Result<void> r1;
    browser::Result<void> r2(std::move(r1));
    ASSERT(r2.is_ok());
})

TEST(result_void_move_err, {
    browser::Result<void> r1(std::string("error"));
    browser::Result<void> r2(std::move(r1));
    ASSERT(r2.is_err());
    ASSERT(r2.unwrap_err() == "error");
})

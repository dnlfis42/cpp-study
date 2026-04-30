#include "shandle/v01/shared_handle.hpp"

#include <gtest/gtest.h>

#include <utility>

using shandle::v01::make_handle;
using shandle::v01::SharedHandle;

TEST(SharedHandle, DefaultIsNull) {
    SharedHandle<int> h;
    EXPECT_FALSE(h);
    EXPECT_EQ(h.get(), nullptr);
    EXPECT_EQ(h.use_count(), 0);
}

TEST(SharedHandle, MakeHandleIsValid) {
    auto h = make_handle<int>(42);
    EXPECT_TRUE(h);
    EXPECT_NE(h.get(), nullptr);
    EXPECT_EQ(*h, 42);
    EXPECT_EQ(h.use_count(), 1);
}

TEST(SharedHandle, Deref) {
    struct Point {
        int x;
        int y;
    };
    auto h = make_handle<Point>(1, 2);
    EXPECT_EQ(h->x, 1);
    EXPECT_EQ(h->y, 2);
    EXPECT_EQ((*h).x, 1);
}

TEST(SharedHandle, CopyIncreasesRefcount) {
    auto h1 = make_handle<int>(7);
    auto h2 = h1;
    EXPECT_EQ(h1.use_count(), 2);
    EXPECT_EQ(h2.use_count(), 2);
    EXPECT_EQ(h1.get(), h2.get());
}

TEST(SharedHandle, CopyAssignIncreasesRefcount) {
    auto h1 = make_handle<int>(7);
    SharedHandle<int> h2;
    h2 = h1;
    EXPECT_EQ(h1.use_count(), 2);
    EXPECT_EQ(h2.use_count(), 2);
    EXPECT_EQ(h1.get(), h2.get());
}

TEST(SharedHandle, MoveTransfersOwnership) {
    auto h1 = make_handle<int>(7);
    int* raw = h1.get();
    auto h2 = std::move(h1);
    EXPECT_FALSE(h1);
    EXPECT_TRUE(h2);
    EXPECT_EQ(h2.get(), raw);
    EXPECT_EQ(h2.use_count(), 1);
}

TEST(SharedHandle, MoveAssignTransfersOwnership) {
    auto h1 = make_handle<int>(7);
    int* raw = h1.get();
    SharedHandle<int> h2;
    h2 = std::move(h1);
    EXPECT_FALSE(h1);
    EXPECT_EQ(h2.get(), raw);
    EXPECT_EQ(h2.use_count(), 1);
}

TEST(SharedHandle, ScopeDestroyDecrementsRefcount) {
    auto h1 = make_handle<int>(0);
    {
        auto h2 = h1;
        EXPECT_EQ(h1.use_count(), 2);
    }
    EXPECT_EQ(h1.use_count(), 1);
}

TEST(SharedHandle, DestructorCalledOnLastRelease) {
    struct Tracked {
        int* counter;
        explicit Tracked(int* c) : counter{c} {}
        ~Tracked() { ++(*counter); }
    };

    int dtor_count = 0;
    {
        auto h1 = make_handle<Tracked>(&dtor_count);
        {
            auto h2 = h1;
            EXPECT_EQ(dtor_count, 0);
        }
        EXPECT_EQ(dtor_count, 0);
    }
    EXPECT_EQ(dtor_count, 1);
}

TEST(SharedHandle, SelfCopyAssign) {
    auto h = make_handle<int>(42);
    int* raw = h.get();
    auto& ref = h;
    h = ref;
    EXPECT_EQ(h.get(), raw);
    EXPECT_EQ(h.use_count(), 1);
    EXPECT_EQ(*h, 42);
}

TEST(SharedHandle, NonDefaultConstructible) {
    struct NoDefault {
        int x;
        explicit NoDefault(int v) : x{v} {}
    };
    auto h = make_handle<NoDefault>(55);
    EXPECT_EQ(h->x, 55);
}

TEST(SharedHandle, CopyAssignReplacesExisting) {
    auto h1 = make_handle<int>(1);
    auto h2 = make_handle<int>(2);
    int* raw2 = h2.get();
    h1 = h2;
    EXPECT_EQ(h1.get(), raw2);
    EXPECT_EQ(h1.use_count(), 2);
    EXPECT_EQ(*h1, 2);
}

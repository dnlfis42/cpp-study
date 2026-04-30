#include "shandle/v02/shared_handle.hpp"

#include <gtest/gtest.h>

#include <utility>

using shandle::v02::IntrusiveBase;
using shandle::v02::SharedHandle;
using shandle::v02::make_handle;

namespace {

struct Item : IntrusiveBase {
    int value;
    explicit Item(int v) : value{v} {}
};

} // namespace

TEST(SharedHandle, DefaultIsNull) {
    SharedHandle<Item> h;
    EXPECT_FALSE(h);
    EXPECT_EQ(h.get(), nullptr);
    EXPECT_EQ(h.use_count(), 0);
}

TEST(SharedHandle, MakeHandleIsValid) {
    auto h = make_handle<Item>(42);
    EXPECT_TRUE(h);
    EXPECT_NE(h.get(), nullptr);
    EXPECT_EQ(h->value, 42);
    EXPECT_EQ(h.use_count(), 1);
}

TEST(SharedHandle, Deref) {
    auto h = make_handle<Item>(7);
    EXPECT_EQ((*h).value, 7);
    EXPECT_EQ(h->value, 7);
}

TEST(SharedHandle, CopyIncreasesRefcount) {
    auto h1 = make_handle<Item>(1);
    auto h2 = h1;
    EXPECT_EQ(h1.use_count(), 2);
    EXPECT_EQ(h2.use_count(), 2);
    EXPECT_EQ(h1.get(), h2.get());
}

TEST(SharedHandle, CopyAssignIncreasesRefcount) {
    auto h1 = make_handle<Item>(1);
    SharedHandle<Item> h2;
    h2 = h1;
    EXPECT_EQ(h1.use_count(), 2);
    EXPECT_EQ(h2.use_count(), 2);
}

TEST(SharedHandle, MoveTransfersOwnership) {
    auto h1 = make_handle<Item>(1);
    Item* raw = h1.get();
    auto h2 = std::move(h1);
    EXPECT_FALSE(h1);
    EXPECT_EQ(h2.get(), raw);
    EXPECT_EQ(h2.use_count(), 1);
}

TEST(SharedHandle, ScopeDestroyDecrementsRefcount) {
    auto h1 = make_handle<Item>(0);
    {
        auto h2 = h1;
        EXPECT_EQ(h1.use_count(), 2);
    }
    EXPECT_EQ(h1.use_count(), 1);
}

TEST(SharedHandle, DestructorCalledOnLastRelease) {
    struct Tracked : IntrusiveBase {
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
    auto h = make_handle<Item>(42);
    Item* raw = h.get();
    auto& ref = h;
    h = ref;
    EXPECT_EQ(h.get(), raw);
    EXPECT_EQ(h.use_count(), 1);
}

TEST(SharedHandle, ConceptRejectsNonIntrusive) {
    struct Plain { int x; };
    EXPECT_FALSE((shandle::v02::Intrusive<Plain>));
    EXPECT_TRUE((shandle::v02::Intrusive<Item>));
}

#include "dbus_test_env.h"

#include <cstring>
#include <string>

namespace {

constexpr IARM_EventId_t kEventA = 1;
constexpr IARM_EventId_t kEventB = 2;

struct EventCapture {
    CallbackLatch latch;
    std::string owner;
    IARM_EventId_t id = -1;
    int value = 0;
    std::string traceparent;
};

EventCapture *g_captureA = nullptr;
EventCapture *g_captureB = nullptr;

void Record(EventCapture *capture, const char *owner, IARM_EventId_t id, void *data, size_t len)
{
    if (capture == nullptr) {
        return;
    }
    capture->owner = owner;
    capture->id = id;
    if (len == sizeof(int)) {
        std::memcpy(&capture->value, data, sizeof(int));
    }
    const char *tp = IARM_Bus_GetTraceparent();
    capture->traceparent = tp ? tp : "";
    capture->latch.Signal();
}

void HandlerA(const char *owner, IARM_EventId_t id, void *data, size_t len)
{
    Record(g_captureA, owner, id, data, len);
}

void HandlerB(const char *owner, IARM_EventId_t id, void *data, size_t len)
{
    Record(g_captureB, owner, id, data, len);
}

const char kTraceparent[] = "00-0123456789abcdef0123456789abcdef-0123456789abcdef-01";

class IarmBusEventTest : public IarmBusClientTest {
protected:
    void SetUp() override
    {
        IarmBusClientTest::SetUp();
        g_captureA = &captureA_;
        g_captureB = &captureB_;
        ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_RegisterEvent(4));
    }

    void TearDown() override
    {
        IarmBusClientTest::TearDown();
        g_captureA = nullptr;
        g_captureB = nullptr;
    }

    EventCapture captureA_;
    EventCapture captureB_;
};

}  // namespace

TEST_F(IarmBusEventTest, BroadcastIsDeliveredToAllHandlers)
{
    ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_RegisterEventHandler(kClientName, kEventA, HandlerA));
    ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_RegisterEventHandler(kClientName, kEventA, HandlerB));

    int value = 7;
    ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_BroadcastEvent(kClientName, kEventA, &value, sizeof(value)));
    ASSERT_TRUE(captureA_.latch.WaitFor(1));
    ASSERT_TRUE(captureB_.latch.WaitFor(1));
    EXPECT_EQ(kClientName, captureA_.owner);
    EXPECT_EQ(kEventA, captureA_.id);
    EXPECT_EQ(7, captureA_.value);
    EXPECT_TRUE(captureA_.traceparent.empty());
    EXPECT_EQ(7, captureB_.value);

    EXPECT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_UnRegisterEventHandler(kClientName, kEventA));
    EXPECT_EQ(IARM_RESULT_INVALID_PARAM, IARM_Bus_UnRegisterEventHandler(kClientName, kEventA));
}

TEST_F(IarmBusEventTest, TraceparentTravelsWithEvent)
{
    ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_RegisterEventHandler(kClientName, kEventA, HandlerA));

    int value = 11;
    IARM_Bus_SetTraceparent(kTraceparent);
    ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_BroadcastEvent(kClientName, kEventA, &value, sizeof(value)));
    ASSERT_TRUE(captureA_.latch.WaitFor(1));
    EXPECT_EQ(kTraceparent, captureA_.traceparent);
    EXPECT_EQ(11, captureA_.value);

    EXPECT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_UnRegisterEventHandler(kClientName, kEventA));
}

TEST_F(IarmBusEventTest, HandlersOnlyReceiveMatchingEvents)
{
    ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_RegisterEventHandler(kClientName, kEventA, HandlerA));
    ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_RegisterEventHandler("OtherOwner", kEventA, HandlerB));

    int value = 1;
    ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_BroadcastEvent(kClientName, kEventB, &value, sizeof(value)));
    value = 2;
    ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_BroadcastEvent(kClientName, kEventA, &value, sizeof(value)));

    // Signals arrive in order, so once the second one lands the first was already filtered.
    ASSERT_TRUE(captureA_.latch.WaitFor(1));
    EXPECT_EQ(1, captureA_.latch.Count());
    EXPECT_EQ(2, captureA_.value);
    EXPECT_EQ(0, captureB_.latch.Count());

    EXPECT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_UnRegisterEventHandler(kClientName, kEventA));
    EXPECT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_UnRegisterEventHandler("OtherOwner", kEventA));
}

TEST_F(IarmBusEventTest, RemovingOneHandlerKeepsTheOther)
{
    ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_RegisterEventHandler(kClientName, kEventA, HandlerA));
    ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_RegisterEventHandler(kClientName, kEventA, HandlerB));
    EXPECT_EQ(IARM_RESULT_INVALID_PARAM, IARM_Bus_RegisterEventHandler(kClientName, kEventA, HandlerA));

    EXPECT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_RemoveEventHandler(kClientName, kEventA, HandlerA));
    EXPECT_EQ(IARM_RESULT_INVALID_PARAM, IARM_Bus_RemoveEventHandler(kClientName, kEventA, HandlerA));

    int value = 3;
    ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_BroadcastEvent(kClientName, kEventA, &value, sizeof(value)));
    ASSERT_TRUE(captureB_.latch.WaitFor(1));
    EXPECT_EQ(0, captureA_.latch.Count());

    EXPECT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_RemoveEventHandler(kClientName, kEventA, HandlerB));
    EXPECT_EQ(IARM_RESULT_INVALID_PARAM, IARM_Bus_UnRegisterEventHandler(kClientName, kEventA));
}

TEST_F(IarmBusEventTest, InvalidArgumentsAreRejected)
{
    const std::string longOwner(IARM_MAX_NAME_LEN + 1, 'x');
    int value = 0;

    EXPECT_EQ(IARM_RESULT_INVALID_PARAM, IARM_Bus_RegisterEventHandler(kClientName, kEventA, nullptr));
    EXPECT_EQ(IARM_RESULT_INVALID_PARAM, IARM_Bus_RemoveEventHandler(kClientName, kEventA, nullptr));
    EXPECT_EQ(IARM_RESULT_INVALID_PARAM, IARM_Bus_BroadcastEvent(kClientName, kEventA, nullptr, 0));
    EXPECT_EQ(IARM_RESULT_INVALID_PARAM,
              IARM_Bus_BroadcastEvent(longOwner.c_str(), kEventA, &value, sizeof(value)));
    EXPECT_EQ(IARM_RESULT_INVALID_PARAM, IARM_Bus_BroadcastEvent(kClientName, 0x1FF, &value, sizeof(value)));
}

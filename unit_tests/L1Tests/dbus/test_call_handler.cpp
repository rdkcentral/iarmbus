#include <gtest/gtest.h>

#include <cstring>
#include <stdexcept>
#include <vector>

#include <dbus/dbus.h>

#include "libIARMCore.h"

DBusHandlerResult dbusCallHandler(DBusConnection *connection, DBusMessage *msg, void *user_data);

namespace {

// Layout mirrors of IARM_UICall_t / IARM_UIEvent_t in core/libIARM-dbus.c.
struct FakeCallInfo {
    void *cctx;
    char callName[IARM_MAX_NAME_LEN];
    IARM_Call_t handler;
    void *callCtx;
};

struct FakeEventInfo {
    void *cctx;
    IARM_EventId_t eventId;
    IARM_Listener_t listener;
    char ownerName[IARM_MAX_NAME_LEN];
    void *callCtx;
};

constexpr int kIarmHeaderSize = 8 + sizeof(size_t);
constexpr IARM_EventId_t kEventId = 3;
constexpr const char *kOwner = "HandlerOwner";
constexpr const char *kMethod = "HandlerMethod";

// Zeroed stand-in for IARM_Ctx_t; only memberName is read and it stays empty.
alignas(16) char g_fakeCtx[4096];

int g_listenerCalls = 0;
int g_handlerCalls = 0;

void CountingListener(void *, void *) { ++g_listenerCalls; }
void ThrowingListener(void *, void *) { throw std::runtime_error("listener failure"); }
void UnknownThrowingListener(void *, void *) { throw 42; }

void CountingHandler(void *, unsigned long, void *, void *) { ++g_handlerCalls; }
void ThrowingHandler(void *, unsigned long, void *, void *) { throw std::runtime_error("handler failure"); }

DBusMessage *NewSignal()
{
    return dbus_message_new_signal("/iarm/signal/Object", "iarm.signal.Type", "Sender");
}

DBusMessage *NewCall(const char *iface, const char *member)
{
    return dbus_message_new_method_call("process.iarm.Peer", "/iarm/method/Object", iface, member);
}

class Message {
public:
    explicit Message(DBusMessage *msg) : msg_(msg) { dbus_message_iter_init_append(msg_, &iter_); }
    ~Message() { dbus_message_unref(msg_); }
    Message(const Message &) = delete;
    Message &operator=(const Message &) = delete;

    Message &U32(dbus_uint32_t value)
    {
        dbus_message_iter_append_basic(&iter_, DBUS_TYPE_UINT32, &value);
        return *this;
    }

    Message &Str(const char *value)
    {
        dbus_message_iter_append_basic(&iter_, DBUS_TYPE_STRING, &value);
        return *this;
    }

    Message &Bytes(size_t count)
    {
        std::vector<unsigned char> data(count, 0x5a);
        const unsigned char *ptr = data.data();
        DBusMessageIter array;
        dbus_message_iter_open_container(&iter_, DBUS_TYPE_ARRAY, "y", &array);
        dbus_message_iter_append_fixed_array(&array, DBUS_TYPE_BYTE, &ptr, static_cast<int>(count));
        dbus_message_iter_close_container(&iter_, &array);
        return *this;
    }

    DBusHandlerResult Dispatch(void *userData) { return dbusCallHandler(nullptr, msg_, userData); }

private:
    DBusMessage *msg_;
    DBusMessageIter iter_;
};

FakeEventInfo MakeEvent(IARM_Listener_t listener)
{
    FakeEventInfo info{};
    info.cctx = g_fakeCtx;
    info.eventId = kEventId;
    info.listener = listener;
    std::strncpy(info.ownerName, kOwner, sizeof(info.ownerName) - 1);
    return info;
}

FakeCallInfo MakeCall(IARM_Call_t handler)
{
    FakeCallInfo info{};
    info.cctx = g_fakeCtx;
    std::strncpy(info.callName, kMethod, sizeof(info.callName) - 1);
    info.handler = handler;
    return info;
}

}  // namespace

TEST(IARMDbusCallHandler, NullUserDataIsHandled)
{
    EXPECT_EQ(DBUS_HANDLER_RESULT_HANDLED, Message(NewSignal()).Dispatch(nullptr));
}

TEST(IARMDbusCallHandler, SignalWithoutContextIsHandled)
{
    FakeEventInfo info = MakeEvent(CountingListener);
    info.cctx = nullptr;
    EXPECT_EQ(DBUS_HANDLER_RESULT_HANDLED, Message(NewSignal()).U32(kEventId).Dispatch(&info));
}

TEST(IARMDbusCallHandler, MalformedSignalsAreIgnored)
{
    g_listenerCalls = 0;
    FakeEventInfo info = MakeEvent(CountingListener);

    EXPECT_EQ(DBUS_HANDLER_RESULT_NOT_YET_HANDLED, Message(NewSignal()).Dispatch(&info));
    EXPECT_EQ(DBUS_HANDLER_RESULT_NOT_YET_HANDLED, Message(NewSignal()).Str(kOwner).Dispatch(&info));
    EXPECT_EQ(DBUS_HANDLER_RESULT_NOT_YET_HANDLED, Message(NewSignal()).U32(kEventId + 1).Dispatch(&info));
    EXPECT_EQ(DBUS_HANDLER_RESULT_NOT_YET_HANDLED, Message(NewSignal()).U32(kEventId).U32(0).Dispatch(&info));
    EXPECT_EQ(DBUS_HANDLER_RESULT_NOT_YET_HANDLED,
              Message(NewSignal()).U32(kEventId).Str("SomeoneElse").Dispatch(&info));
    EXPECT_EQ(DBUS_HANDLER_RESULT_NOT_YET_HANDLED,
              Message(NewSignal()).U32(kEventId).Str(kOwner).Str("size").Dispatch(&info));
    EXPECT_EQ(DBUS_HANDLER_RESULT_NOT_YET_HANDLED,
              Message(NewSignal()).U32(kEventId).Str(kOwner).U32(4).U32(0).Dispatch(&info));
    EXPECT_EQ(0, g_listenerCalls);
}

TEST(IARMDbusCallHandler, WellFormedSignalReachesListener)
{
    g_listenerCalls = 0;
    FakeEventInfo info = MakeEvent(CountingListener);
    Message(NewSignal()).U32(kEventId).Str(kOwner).U32(4).Bytes(4).Dispatch(&info);
    EXPECT_EQ(1, g_listenerCalls);
}

TEST(IARMDbusCallHandler, ListenerExceptionsAreContained)
{
    FakeEventInfo info = MakeEvent(ThrowingListener);
    EXPECT_EQ(DBUS_HANDLER_RESULT_HANDLED, Message(NewSignal()).U32(kEventId).Str(kOwner).U32(4).Bytes(4).Dispatch(&info));

    info.listener = UnknownThrowingListener;
    EXPECT_EQ(DBUS_HANDLER_RESULT_HANDLED, Message(NewSignal()).U32(kEventId).Str(kOwner).U32(4).Bytes(4).Dispatch(&info));
}

TEST(IARMDbusCallHandler, MalformedMethodCallsAreIgnored)
{
    g_handlerCalls = 0;
    FakeCallInfo info = MakeCall(CountingHandler);

    EXPECT_EQ(DBUS_HANDLER_RESULT_NOT_YET_HANDLED, Message(NewCall("iarm.method.Type", kMethod)).Dispatch(&info));
    EXPECT_EQ(DBUS_HANDLER_RESULT_NOT_YET_HANDLED, Message(NewCall("iarm.method.Type", kMethod)).Str("x").Dispatch(&info));
    EXPECT_EQ(DBUS_HANDLER_RESULT_NOT_YET_HANDLED,
              Message(NewCall("iarm.method.Type", kMethod)).U32(4).U32(0).Dispatch(&info));
    EXPECT_EQ(DBUS_HANDLER_RESULT_NOT_YET_HANDLED,
              Message(NewCall("iarm.method.Type", "OtherMethod")).U32(4).Bytes(kIarmHeaderSize + 4).Dispatch(&info));
    EXPECT_EQ(0, g_handlerCalls);
}

TEST(IARMDbusCallHandler, ForeignInterfaceIsSwallowed)
{
    FakeCallInfo info = MakeCall(CountingHandler);
    EXPECT_EQ(DBUS_HANDLER_RESULT_HANDLED, Message(NewCall("org.example.Other", kMethod)).Dispatch(&info));
}

TEST(IARMDbusCallHandler, WellFormedMethodCallReachesHandler)
{
    g_handlerCalls = 0;
    FakeCallInfo info = MakeCall(CountingHandler);
    EXPECT_EQ(DBUS_HANDLER_RESULT_HANDLED,
              Message(NewCall("iarm.method.Type", kMethod)).U32(kIarmHeaderSize + 4).Bytes(kIarmHeaderSize + 4).Dispatch(&info));
    EXPECT_EQ(1, g_handlerCalls);
}

TEST(IARMDbusCallHandler, MissingHandlerIsHandled)
{
    FakeCallInfo info = MakeCall(nullptr);
    EXPECT_EQ(DBUS_HANDLER_RESULT_HANDLED,
              Message(NewCall("iarm.method.Type", kMethod)).U32(kIarmHeaderSize + 4).Bytes(kIarmHeaderSize + 4).Dispatch(&info));
}

TEST(IARMDbusCallHandler, HandlerExceptionsAreContained)
{
    FakeCallInfo info = MakeCall(ThrowingHandler);
    EXPECT_EQ(DBUS_HANDLER_RESULT_HANDLED,
              Message(NewCall("iarm.method.Type", kMethod)).U32(kIarmHeaderSize + 4).Bytes(kIarmHeaderSize + 4).Dispatch(&info));
}

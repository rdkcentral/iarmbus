#include "dbus_test_env.h"

#include <cstring>
#include <string>

#include "iarm_tp.h"
#include "libIARMCore.h"

namespace {

struct EchoParam {
    int value;
    char text[16];
};

std::string g_handlerTraceparent;
int g_noArgCalls = 0;

IARM_Result_t EchoHandler(void *arg)
{
    auto *param = static_cast<EchoParam *>(arg);
    const char *tp = IARM_Bus_GetTraceparent();
    g_handlerTraceparent = tp ? tp : "";
    if (param->value < 0) {
        return IARM_RESULT_INVALID_PARAM;
    }
    param->value *= 2;
    std::strcpy(param->text, "pong");
    return IARM_RESULT_SUCCESS;
}

IARM_Result_t NoArgHandler(void *)
{
    ++g_noArgCalls;
    return IARM_RESULT_SUCCESS;
}

void NoopCoreCall(void *, unsigned long, void *, void *) {}

const char kTraceparent[] = "00-0123456789abcdef0123456789abcdef-0123456789abcdef-01";

}  // namespace

TEST_F(IarmBusClientTest, CallRoundTripUpdatesArgument)
{
    ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_RegisterCall("Echo", EchoHandler));

    EchoParam param{21, "ping"};
    ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_Call(kClientName, "Echo", &param, sizeof(param)));
    EXPECT_EQ(42, param.value);
    EXPECT_STREQ("pong", param.text);
    EXPECT_TRUE(g_handlerTraceparent.empty());
}

TEST_F(IarmBusClientTest, ProviderErrorIsReturnedToCaller)
{
    ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_RegisterCall("Echo", EchoHandler));

    EchoParam param{-1, "ping"};
    EXPECT_EQ(IARM_RESULT_INVALID_PARAM, IARM_Bus_Call(kClientName, "Echo", &param, sizeof(param)));
}

TEST_F(IarmBusClientTest, CallWithoutArgumentReachesProvider)
{
    g_noArgCalls = 0;
    ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_RegisterCall("NoArg", NoArgHandler));

    EXPECT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_Call(kClientName, "NoArg", nullptr, 0));
    EXPECT_EQ(1, g_noArgCalls);
}

TEST_F(IarmBusClientTest, CallToUnknownMemberFails)
{
    EchoParam param{1, "ping"};
    EXPECT_EQ(IARM_RESULT_INVALID_STATE, IARM_Bus_Call("NoSuchMember", "Echo", &param, sizeof(param)));
    EXPECT_EQ(IARM_RESULT_INVALID_STATE,
              IARM_Bus_Call_with_IPCTimeout("NoSuchMember", "Echo", &param, sizeof(param), 1000));
}

TEST_F(IarmBusClientTest, CallWithExplicitTimeouts)
{
    ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_RegisterCall("Echo", EchoHandler));

    EchoParam param{5, "ping"};
    ASSERT_EQ(IARM_RESULT_SUCCESS,
              IARM_Bus_Call_with_IPCTimeout(kClientName, "Echo", &param, sizeof(param), 1000));
    EXPECT_EQ(10, param.value);

    ASSERT_EQ(IARM_RESULT_SUCCESS,
              IARM_Bus_Call_with_IPCTimeout(kClientName, "Echo", &param, sizeof(param),
                                            IARM_METHOD_IPC_TIMEOUT_INFINITE));
    EXPECT_EQ(20, param.value);

    param.value = -1;
    EXPECT_EQ(IARM_RESULT_INVALID_PARAM,
              IARM_Bus_Call_with_IPCTimeout(kClientName, "Echo", &param, sizeof(param), 1000));
}

TEST_F(IarmBusClientTest, TraceparentIsDeliveredToProviderOnce)
{
    ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_RegisterCall("Echo", EchoHandler));

    EchoParam param{3, "ping"};
    IARM_Bus_SetTraceparent(kTraceparent);
    ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_Call(kClientName, "Echo", &param, sizeof(param)));
    EXPECT_EQ(kTraceparent, g_handlerTraceparent);
    EXPECT_EQ(6, param.value);
    EXPECT_STREQ("pong", param.text);

    ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_Call(kClientName, "Echo", &param, sizeof(param)));
    EXPECT_TRUE(g_handlerTraceparent.empty());
    EXPECT_EQ(12, param.value);
}

TEST_F(IarmBusClientTest, RegisterCallRejectsInvalidNames)
{
    const std::string longName(IARM_MAX_NAME_LEN, 'x');
    EXPECT_EQ(IARM_RESULT_INVALID_PARAM, IARM_Bus_RegisterCall(nullptr, EchoHandler));
    EXPECT_EQ(IARM_RESULT_INVALID_PARAM, IARM_Bus_RegisterCall(longName.c_str(), EchoHandler));
}

TEST_F(IarmBusClientTest, IsConnectedReflectsDaemonRegistration)
{
    int registered = 0;
    EXPECT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_IsConnected(kClientName, &registered));
    EXPECT_EQ(1, registered);

    EXPECT_EQ(IARM_RESULT_IPCCORE_FAIL, IARM_Bus_IsConnected("NoSuchMember", &registered));
    EXPECT_EQ(0, registered);
}

TEST_F(IarmBusClientTest, InitAndConnectTwiceAreRejected)
{
    EXPECT_EQ(IARM_RESULT_INVALID_STATE, IARM_Bus_Init(kClientName));
    EXPECT_EQ(IARM_RESULT_INVALID_STATE, IARM_Bus_Connect());
}

TEST_F(IarmBusClientTest, CoreRejectsInvalidArguments)
{
    const std::string longName(IARM_MAX_NAME_LEN, 'x');
    int registered = 0;
    int ret = 0;

    EXPECT_EQ(IARM_RESULT_INVALID_PARAM, IARM_RegisterCall(nullptr, "m", NoopCoreCall, nullptr));
    EXPECT_EQ(IARM_RESULT_INVALID_PARAM, IARM_RegisterCall(longName.c_str(), "m", NoopCoreCall, nullptr));
    EXPECT_EQ(IARM_RESULT_INVALID_PARAM, IARM_RegisterCall(kClientName, longName.c_str(), NoopCoreCall, nullptr));

    EXPECT_EQ(IARM_RESULT_INVALID_PARAM, IARM_IsCallRegistered(nullptr, "m", &registered));
    EXPECT_EQ(IARM_RESULT_INVALID_PARAM, IARM_IsCallRegistered(longName.c_str(), "m", &registered));
    EXPECT_EQ(IARM_RESULT_INVALID_PARAM, IARM_IsCallRegistered(kClientName, longName.c_str(), &registered));
    EXPECT_EQ(IARM_RESULT_SUCCESS, IARM_IsCallRegistered(kClientName, "m", &registered));
    EXPECT_EQ(1, registered);

    EXPECT_EQ(IARM_RESULT_INVALID_PARAM, IARM_CallWithTimeout(nullptr, "m", nullptr, 100, &ret));
    EXPECT_EQ(IARM_RESULT_INVALID_PARAM, IARM_CallReturn(nullptr, "m", nullptr, 0, nullptr));
    EXPECT_EQ(IARM_RESULT_INVALID_PARAM, IARM_NotifyEvent(kClientName, 0x1FF, nullptr));
    EXPECT_EQ(IARM_RESULT_INVALID_PARAM, IARM_UnRegisterListner(kClientName, 0x1FF));
    EXPECT_EQ(IARM_RESULT_INVALID_PARAM, IARM_UnRegisterListner(kClientName, 7));
}

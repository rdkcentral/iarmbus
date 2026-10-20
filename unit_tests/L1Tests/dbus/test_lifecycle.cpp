#include "dbus_test_env.h"

#include <cstdio>
#include <fstream>
#include <string>

#include <unistd.h>

#include "iarmUtil.h"
#include "libIARMCore.h"
#include "libIBusDaemon.h"

void *IARM_GetContext(void);

namespace {

std::string g_logs;

void CaptureLog(const char *message)
{
    g_logs += message;
}

IARM_Result_t NoopCall(void *)
{
    return IARM_RESULT_SUCCESS;
}

void NoopEvent(const char *, IARM_EventId_t, void *, size_t) {}

}  // namespace

TEST(IarmBusLifecycle, OperationsBeforeInitAreRejected)
{
    EXPECT_EQ(IARM_RESULT_INVALID_STATE, IARM_Bus_Connect());
    EXPECT_EQ(IARM_RESULT_INVALID_STATE, IARM_Bus_Disconnect());
    EXPECT_EQ(IARM_RESULT_INVALID_PARAM, IARM_Bus_Term());
}

TEST(IarmBusLifecycle, InitRejectsOverlongName)
{
    const std::string longName(IARM_MAX_NAME_LEN, 'x');
    EXPECT_EQ(IARM_RESULT_INVALID_PARAM, IARM_Bus_Init(longName.c_str()));

    ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_Init(kClientName));
    EXPECT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_Term());
}

TEST(IarmBusLifecycle, OperationsBeforeConnectAreRejected)
{
    ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_Init(kClientName));

    int value = 0;
    int registered = 1;
    IARM_Bus_CommonAPI_PowerPreChange_Param_t power{IARM_BUS_PWRMGR_POWERSTATE_ON, IARM_BUS_PWRMGR_POWERSTATE_STANDBY};
    IARM_Bus_CommonAPI_ResChange_Param_t res{1920, 1080};

    EXPECT_EQ(IARM_RESULT_INVALID_STATE, IARM_Bus_Disconnect());
    EXPECT_EQ(IARM_RESULT_INVALID_STATE, IARM_Bus_Call(kClientName, "Echo", &value, sizeof(value)));
    EXPECT_EQ(IARM_RESULT_INVALID_STATE,
              IARM_Bus_Call_with_IPCTimeout(kClientName, "Echo", &value, sizeof(value), 1000));
    EXPECT_EQ(IARM_RESULT_INVALID_STATE, IARM_Bus_BroadcastEvent(kClientName, 1, &value, sizeof(value)));
    EXPECT_EQ(IARM_RESULT_INVALID_STATE, IARM_Bus_RegisterEvent(1));
    EXPECT_EQ(IARM_RESULT_INVALID_STATE, IARM_Bus_RegisterCall("Echo", NoopCall));
    EXPECT_EQ(IARM_RESULT_INVALID_STATE, IARM_Bus_RegisterEventHandler(kClientName, 1, NoopEvent));
    EXPECT_EQ(IARM_RESULT_INVALID_STATE, IARM_Bus_UnRegisterEventHandler(kClientName, 1));
    EXPECT_EQ(IARM_RESULT_INVALID_STATE, IARM_Bus_RemoveEventHandler(kClientName, 1, NoopEvent));
    EXPECT_EQ(IARM_RESULT_INVALID_STATE, IARM_Bus_IsConnected(kClientName, &registered));
    EXPECT_EQ(0, registered);
    EXPECT_EQ(IARM_RESULT_INVALID_STATE, IARM_BusDaemon_RequestOwnership(IARM_BUS_RESOURCE_FOCUS));
    EXPECT_EQ(IARM_RESULT_INVALID_STATE, IARM_BusDaemon_ReleaseOwnership(IARM_BUS_RESOURCE_FOCUS));
    EXPECT_EQ(IARM_RESULT_INVALID_STATE, IARM_BusDaemon_PowerPrechange(power));
    EXPECT_EQ(IARM_RESULT_INVALID_STATE, IARM_BusDaemon_DeepSleepWakeup(power));
    EXPECT_EQ(IARM_RESULT_INVALID_STATE, IARM_BusDaemon_ResolutionPrechange(res));
    EXPECT_EQ(IARM_RESULT_INVALID_STATE, IARM_BusDaemon_ResolutionPostchange(res));

    EXPECT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_Term());
}

TEST(IarmBusLifecycle, LogCallbackReceivesLibraryMessages)
{
    g_logs.clear();
    IARM_Bus_RegisterForLog(CaptureLog);
    EXPECT_EQ(IARM_RESULT_INVALID_PARAM, IARM_Bus_Term());
    IARM_Bus_RegisterForLog(nullptr);
    EXPECT_NE(std::string::npos, g_logs.find("NOT INITD"));
}

TEST(IarmBusLifecycle, WritePIDFileRecordsCurrentProcess)
{
    const std::string path = DbusTestWorkDir() + "/l1.pid";
    IARM_Bus_WritePIDFile(path.c_str());

    long pid = 0;
    std::ifstream(path) >> pid;
    EXPECT_EQ(static_cast<long>(getpid()), pid);
    std::remove(path.c_str());

    // Unwritable location exercises the error path without crashing.
    IARM_Bus_WritePIDFile("/nonexistent-iarm-dir/l1.pid");
    // Opens fine but every write fails with ENOSPC.
    IARM_Bus_WritePIDFile("/dev/full");
}

TEST(IarmCoreLifecycle, DirectCoreCallsValidateState)
{
    EXPECT_EQ(nullptr, IARM_GetContext());
    EXPECT_EQ(IARM_RESULT_INVALID_PARAM, IARM_Term());
    EXPECT_EQ(IARM_RESULT_INVALID_PARAM, IARM_Init(nullptr, kClientName));
    EXPECT_EQ(IARM_RESULT_INVALID_PARAM, IARM_Init(IARM_BUS_NAME, nullptr));

    ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_Init(kClientName));
    EXPECT_NE(nullptr, IARM_GetContext());
    EXPECT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_Term());
    EXPECT_EQ(nullptr, IARM_GetContext());
}

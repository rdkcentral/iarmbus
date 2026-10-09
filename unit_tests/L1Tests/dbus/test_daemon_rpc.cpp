#include "dbus_test_env.h"

#include <cstring>

#include "libIBusDaemon.h"
#include "libIBusDaemonInternal.h"

namespace {

constexpr const char *kGhostName = "L1Ghost";

struct PreChangeCapture {
    int powerCalls = 0;
    int deepSleepCalls = 0;
    int resPreCalls = 0;
    int resPostCalls = 0;
    int sysModeCalls = 0;
    IARM_Bus_CommonAPI_PowerPreChange_Param_t power{};
    IARM_Bus_CommonAPI_PowerPreChange_Param_t deepSleep{};
    IARM_Bus_CommonAPI_ResChange_Param_t resPre{};
    IARM_Bus_CommonAPI_ResChange_Param_t resPost{};
    IARM_Bus_CommonAPI_SysModeChange_Param_t sysMode{};
} g_preChange;

IARM_Result_t OnPowerPreChange(void *arg)
{
    ++g_preChange.powerCalls;
    std::memcpy(&g_preChange.power, arg, sizeof(g_preChange.power));
    return IARM_RESULT_SUCCESS;
}

IARM_Result_t OnDeepSleepWakeup(void *arg)
{
    ++g_preChange.deepSleepCalls;
    std::memcpy(&g_preChange.deepSleep, arg, sizeof(g_preChange.deepSleep));
    return IARM_RESULT_SUCCESS;
}

IARM_Result_t OnResolutionPreChange(void *arg)
{
    ++g_preChange.resPreCalls;
    std::memcpy(&g_preChange.resPre, arg, sizeof(g_preChange.resPre));
    return IARM_RESULT_SUCCESS;
}

IARM_Result_t OnResolutionPostChange(void *arg)
{
    ++g_preChange.resPostCalls;
    std::memcpy(&g_preChange.resPost, arg, sizeof(g_preChange.resPost));
    return IARM_RESULT_SUCCESS;
}

IARM_Result_t OnSysModeChange(void *arg)
{
    ++g_preChange.sysModeCalls;
    std::memcpy(&g_preChange.sysMode, arg, sizeof(g_preChange.sysMode));
    return IARM_RESULT_SUCCESS;
}

CallbackLatch *g_resourceLatch = nullptr;
IARM_Bus_ResrcType_t g_availableResource = IARM_BUS_RESOURCE_MAX;

void OnResourceAvailable(const char *, IARM_EventId_t, void *data, size_t)
{
    g_availableResource = static_cast<IARM_Bus_EventData_t *>(data)->resrcType;
    if (g_resourceLatch) {
        g_resourceLatch->Signal();
    }
}

IARM_Result_t CallDaemon(const char *method, void *arg, size_t len)
{
    return IARM_Bus_Call(IARM_BUS_DAEMON_NAME, method, arg, len);
}

IARM_Bus_Member_t MakeMember(const char *name)
{
    IARM_Bus_Member_t member{};
    std::strncpy(member.selfName, name, sizeof(member.selfName) - 1);
    return member;
}

IARM_Result_t RegisterPreChange(const char *owner, const char *method)
{
    IARM_Bus_Daemon_RegisterPreChange_Param_t param{};
    std::strncpy(param.ownerName, owner, sizeof(param.ownerName) - 1);
    std::strncpy(param.methodName, method, sizeof(param.methodName) - 1);
    return CallDaemon(IARM_BUS_DAEMON_API_RegisterPreChange, &param, sizeof(param));
}

class IarmBusDaemonTest : public IarmBusClientTest {};

}  // namespace

TEST_F(IarmBusDaemonTest, MemberRegistrationLifecycle)
{
    IARM_Bus_Member_t empty = MakeMember("");
    EXPECT_EQ(IARM_RESULT_INVALID_PARAM, CallDaemon(IARM_BUS_DAEMON_API_RegisterMember, &empty, sizeof(empty)));

    IARM_Bus_Member_t ghost = MakeMember(kGhostName);
    ASSERT_EQ(IARM_RESULT_SUCCESS, CallDaemon(IARM_BUS_DAEMON_API_RegisterMember, &ghost, sizeof(ghost)));
    // Re-registering replaces the stale instance.
    ASSERT_EQ(IARM_RESULT_SUCCESS, CallDaemon(IARM_BUS_DAEMON_API_RegisterMember, &ghost, sizeof(ghost)));

    int registered = 0;
    EXPECT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_IsConnected(kGhostName, &registered));
    EXPECT_EQ(1, registered);

    EXPECT_EQ(IARM_RESULT_SUCCESS, CallDaemon(IARM_BUS_DAEMON_API_UnRegisterMember, &ghost, sizeof(ghost)));
    EXPECT_EQ(IARM_RESULT_INVALID_STATE, CallDaemon(IARM_BUS_DAEMON_API_UnRegisterMember, &ghost, sizeof(ghost)));
    EXPECT_EQ(IARM_RESULT_IPCCORE_FAIL, IARM_Bus_IsConnected(kGhostName, &registered));
}

TEST_F(IarmBusDaemonTest, OwnershipCanBeRequestedAndReleased)
{
    CallbackLatch latch;
    g_resourceLatch = &latch;
    ASSERT_EQ(IARM_RESULT_SUCCESS,
              IARM_Bus_RegisterEventHandler(IARM_BUS_DAEMON_NAME, IARM_BUS_EVENT_RESOURCEAVAILABLE, OnResourceAvailable));

    ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_BusDaemon_RequestOwnership(IARM_BUS_RESOURCE_FOCUS));
    EXPECT_EQ(IARM_RESULT_INVALID_STATE, IARM_BusDaemon_RequestOwnership(IARM_BUS_RESOURCE_FOCUS));

    ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_BusDaemon_ReleaseOwnership(IARM_BUS_RESOURCE_FOCUS));
    ASSERT_TRUE(latch.WaitFor(1));
    EXPECT_EQ(IARM_BUS_RESOURCE_FOCUS, g_availableResource);
    EXPECT_EQ(IARM_RESULT_INVALID_STATE, IARM_BusDaemon_ReleaseOwnership(IARM_BUS_RESOURCE_FOCUS));

    EXPECT_EQ(IARM_RESULT_INVALID_PARAM, IARM_BusDaemon_RequestOwnership(IARM_BUS_RESOURCE_MAX));
    EXPECT_EQ(IARM_RESULT_INVALID_PARAM, IARM_BusDaemon_ReleaseOwnership(IARM_BUS_RESOURCE_MAX));

    EXPECT_EQ(IARM_RESULT_SUCCESS,
              IARM_Bus_UnRegisterEventHandler(IARM_BUS_DAEMON_NAME, IARM_BUS_EVENT_RESOURCEAVAILABLE));
    g_resourceLatch = nullptr;
}

TEST_F(IarmBusDaemonTest, DaemonRejectsInvalidResourceInRawRequest)
{
    IARM_Bus_Daemon_RequestOwnership_Param_t request{};
    request.requestor = MakeMember(kClientName);
    request.resrcType = IARM_BUS_RESOURCE_MAX;
    EXPECT_EQ(IARM_RESULT_INVALID_PARAM, CallDaemon(IARM_BUS_DAEMON_API_RequestOwnership, &request, sizeof(request)));

    IARM_Bus_Daemon_ReleaseOwnership_Param_t release{};
    release.requestor = MakeMember(kClientName);
    release.resrcType = IARM_BUS_RESOURCE_MAX;
    EXPECT_EQ(IARM_RESULT_INVALID_PARAM, CallDaemon(IARM_BUS_DAEMON_API_ReleaseOwnership, &release, sizeof(release)));
}

TEST_F(IarmBusDaemonTest, ContestedOwnershipAndForcedRelease)
{
    IARM_Bus_Member_t ghost = MakeMember(kGhostName);
    ASSERT_EQ(IARM_RESULT_SUCCESS, CallDaemon(IARM_BUS_DAEMON_API_RegisterMember, &ghost, sizeof(ghost)));

    IARM_Bus_Daemon_RequestOwnership_Param_t request{};
    request.requestor = ghost;
    request.resrcType = IARM_BUS_RESOURCE_DECODER_0;
    ASSERT_EQ(IARM_RESULT_SUCCESS, CallDaemon(IARM_BUS_DAEMON_API_RequestOwnership, &request, sizeof(request)));

    // The ghost owner cannot be reached to release, so the request is refused.
    EXPECT_NE(IARM_RESULT_SUCCESS, IARM_BusDaemon_RequestOwnership(IARM_BUS_RESOURCE_DECODER_0));

    // Unregistering the owner force-releases the resource.
    ASSERT_EQ(IARM_RESULT_SUCCESS, CallDaemon(IARM_BUS_DAEMON_API_UnRegisterMember, &ghost, sizeof(ghost)));
    EXPECT_EQ(IARM_RESULT_SUCCESS, IARM_BusDaemon_RequestOwnership(IARM_BUS_RESOURCE_DECODER_0));
    EXPECT_EQ(IARM_RESULT_SUCCESS, IARM_BusDaemon_ReleaseOwnership(IARM_BUS_RESOURCE_DECODER_0));
}

TEST_F(IarmBusDaemonTest, PreChangeNotificationsReachRegisteredMember)
{
    g_preChange = PreChangeCapture{};
    ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_RegisterCall(IARM_BUS_COMMON_API_PowerPreChange, OnPowerPreChange));
    ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_RegisterCall(IARM_BUS_COMMON_API_DeepSleepWakeup, OnDeepSleepWakeup));
    ASSERT_EQ(IARM_RESULT_SUCCESS,
              IARM_Bus_RegisterCall(IARM_BUS_COMMON_API_ResolutionPreChange, OnResolutionPreChange));
    ASSERT_EQ(IARM_RESULT_SUCCESS,
              IARM_Bus_RegisterCall(IARM_BUS_COMMON_API_ResolutionPostChange, OnResolutionPostChange));
    ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_RegisterCall(IARM_BUS_COMMON_API_SysModeChange, OnSysModeChange));
    // Duplicate registration is ignored by the daemon.
    EXPECT_EQ(IARM_RESULT_SUCCESS, RegisterPreChange(kClientName, IARM_BUS_COMMON_API_PowerPreChange));

    IARM_Bus_CommonAPI_PowerPreChange_Param_t power{IARM_BUS_PWRMGR_POWERSTATE_STANDBY, IARM_BUS_PWRMGR_POWERSTATE_ON};
    EXPECT_EQ(IARM_RESULT_SUCCESS, IARM_BusDaemon_PowerPrechange(power));
    EXPECT_EQ(1, g_preChange.powerCalls);
    EXPECT_EQ(IARM_BUS_PWRMGR_POWERSTATE_STANDBY, g_preChange.power.newState);
    EXPECT_EQ(IARM_BUS_PWRMGR_POWERSTATE_ON, g_preChange.power.curState);

    IARM_Bus_CommonAPI_PowerPreChange_Param_t wake{IARM_BUS_PWRMGR_POWERSTATE_ON,
                                                   IARM_BUS_PWRMGR_POWERSTATE_STANDBY_DEEP_SLEEP};
    EXPECT_EQ(IARM_RESULT_SUCCESS, IARM_BusDaemon_DeepSleepWakeup(wake));
    EXPECT_EQ(1, g_preChange.deepSleepCalls);
    EXPECT_EQ(IARM_BUS_PWRMGR_POWERSTATE_STANDBY_DEEP_SLEEP, g_preChange.deepSleep.curState);

    IARM_Bus_CommonAPI_ResChange_Param_t res{1920, 1080};
    EXPECT_EQ(IARM_RESULT_SUCCESS, IARM_BusDaemon_ResolutionPrechange(res));
    EXPECT_EQ(1, g_preChange.resPreCalls);
    EXPECT_EQ(1920, g_preChange.resPre.width);
    EXPECT_EQ(1080, g_preChange.resPre.height);

    res = {1280, 720};
    EXPECT_EQ(IARM_RESULT_SUCCESS, IARM_BusDaemon_ResolutionPostchange(res));
    EXPECT_EQ(1, g_preChange.resPostCalls);
    EXPECT_EQ(1280, g_preChange.resPost.width);
    EXPECT_EQ(720, g_preChange.resPost.height);

    IARM_Bus_Daemon_SysModeChange_Param_t mode{IARM_BUS_SYS_MODE_NORMAL, IARM_BUS_SYS_MODE_EAS};
    EXPECT_EQ(IARM_RESULT_SUCCESS, CallDaemon(IARM_BUS_DAEMON_API_SysModeChange, &mode, sizeof(mode)));
    EXPECT_EQ(1, g_preChange.sysModeCalls);
    EXPECT_EQ(IARM_BUS_SYS_MODE_EAS, g_preChange.sysMode.newMode);
}

TEST_F(IarmBusDaemonTest, PreChangeToUnreachableMemberDoesNotFail)
{
    IARM_Bus_Member_t ghost = MakeMember(kGhostName);
    ASSERT_EQ(IARM_RESULT_SUCCESS, CallDaemon(IARM_BUS_DAEMON_API_RegisterMember, &ghost, sizeof(ghost)));
    for (const char *method : {IARM_BUS_COMMON_API_PowerPreChange, IARM_BUS_COMMON_API_DeepSleepWakeup,
                               IARM_BUS_COMMON_API_ResolutionPreChange, IARM_BUS_COMMON_API_ResolutionPostChange,
                               IARM_BUS_COMMON_API_SysModeChange}) {
        ASSERT_EQ(IARM_RESULT_SUCCESS, RegisterPreChange(kGhostName, method));
    }

    IARM_Bus_CommonAPI_PowerPreChange_Param_t power{IARM_BUS_PWRMGR_POWERSTATE_STANDBY, IARM_BUS_PWRMGR_POWERSTATE_ON};
    IARM_Bus_CommonAPI_ResChange_Param_t res{640, 480};
    IARM_Bus_Daemon_SysModeChange_Param_t mode{IARM_BUS_SYS_MODE_NORMAL, IARM_BUS_SYS_MODE_WAREHOUSE};
    EXPECT_EQ(IARM_RESULT_SUCCESS, IARM_BusDaemon_PowerPrechange(power));
    EXPECT_EQ(IARM_RESULT_SUCCESS, IARM_BusDaemon_DeepSleepWakeup(power));
    EXPECT_EQ(IARM_RESULT_SUCCESS, IARM_BusDaemon_ResolutionPrechange(res));
    EXPECT_EQ(IARM_RESULT_SUCCESS, IARM_BusDaemon_ResolutionPostchange(res));
    EXPECT_EQ(IARM_RESULT_SUCCESS, CallDaemon(IARM_BUS_DAEMON_API_SysModeChange, &mode, sizeof(mode)));

    EXPECT_EQ(IARM_RESULT_SUCCESS, CallDaemon(IARM_BUS_DAEMON_API_UnRegisterMember, &ghost, sizeof(ghost)));
}

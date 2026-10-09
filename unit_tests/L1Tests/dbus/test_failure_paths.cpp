#include "dbus_test_env.h"

#include <cstdlib>
#include <cstring>

#include <sys/wait.h>
#include <unistd.h>

#include "libIBusDaemon.h"
#include "libIBusDaemonInternal.h"

namespace {

constexpr const char *kPeerName = "RawPeer";

IARM_Result_t NoopCall(void *)
{
    return IARM_RESULT_SUCCESS;
}

}  // namespace

TEST_F(IarmBusClientTest, MalformedRepliesAreRejected)
{
    RawDbusPeer peer(kPeerName);
    ASSERT_TRUE(peer.OwnsName());

    int value = 1;
    EXPECT_EQ(IARM_RESULT_INVALID_PARAM, IARM_Bus_Call(kPeerName, "EmptyReply", &value, sizeof(value)));
    EXPECT_EQ(IARM_RESULT_INVALID_PARAM, IARM_Bus_Call(kPeerName, "StringReply", &value, sizeof(value)));
    EXPECT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_Call(kPeerName, "Echo", &value, sizeof(value)));
}

TEST_F(IarmBusClientTest, OwnerThatReleasesHandsResourceOver)
{
    RawDbusPeer peer(kPeerName);
    ASSERT_TRUE(peer.OwnsName());

    IARM_Bus_Member_t member{};
    std::strncpy(member.selfName, kPeerName, sizeof(member.selfName) - 1);
    ASSERT_EQ(IARM_RESULT_SUCCESS,
              IARM_Bus_Call(IARM_BUS_DAEMON_NAME, IARM_BUS_DAEMON_API_RegisterMember, &member, sizeof(member)));

    IARM_Bus_Daemon_RequestOwnership_Param_t request{};
    request.requestor = member;
    request.resrcType = IARM_BUS_RESOURCE_PLANE_0;
    ASSERT_EQ(IARM_RESULT_SUCCESS,
              IARM_Bus_Call(IARM_BUS_DAEMON_NAME, IARM_BUS_DAEMON_API_RequestOwnership, &request, sizeof(request)));

    // The daemon asks RawPeer to release, which it acknowledges, so ownership moves to us.
    EXPECT_EQ(IARM_RESULT_SUCCESS, IARM_BusDaemon_RequestOwnership(IARM_BUS_RESOURCE_PLANE_0));
    EXPECT_EQ(IARM_RESULT_SUCCESS, IARM_BusDaemon_ReleaseOwnership(IARM_BUS_RESOURCE_PLANE_0));

    EXPECT_EQ(IARM_RESULT_SUCCESS,
              IARM_Bus_Call(IARM_BUS_DAEMON_NAME, IARM_BUS_DAEMON_API_UnRegisterMember, &member, sizeof(member)));
}

TEST_F(IarmBusClientTest, InvalidMethodNameIsRejected)
{
    int value = 1;
    EXPECT_NE(IARM_RESULT_SUCCESS, IARM_Bus_Call(kClientName, "not-a-valid-member", &value, sizeof(value)));
}

TEST(IarmBusInitFailures, BusNameHeldByAnotherConnection)
{
    // Each squatter blocks one of the three names IARM_Init requests, in order.
    for (const char *squatted : {"Squatter1", "Squatter2.Event", "Squatter3.Method"}) {
        SCOPED_TRACE(squatted);
        RawDbusPeer squatter(squatted);
        ASSERT_TRUE(squatter.OwnsName());
        const std::string member = std::string(squatted).substr(0, 9);
        EXPECT_EQ(IARM_RESULT_IPCCORE_FAIL, IARM_Bus_Init(member.c_str()));
    }
}

TEST(IarmBusInitFailures, BusUnreachable)
{
    // libdbus caches the bus address per process, so try the bad address in a fresh child.
    fflush(nullptr);
    const pid_t child = fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
        setenv("DBUS_SYSTEM_BUS_ADDRESS", "unix:path=/nonexistent-iarm-dir/bus", 1);
        const IARM_Result_t result = IARM_Bus_Init(kClientName);
        exit(result == IARM_RESULT_IPCCORE_FAIL ? 0 : (result == IARM_RESULT_SUCCESS ? 2 : 1));
    }
    int status = 0;
    ASSERT_EQ(child, waitpid(child, &status, 0));
    ASSERT_TRUE(WIFEXITED(status));
    if (WEXITSTATUS(status) == 2) {
        GTEST_SKIP() << "bus address already cached in this process; run this test on its own";
    }
    EXPECT_EQ(0, WEXITSTATUS(status));
}

TEST(IarmBusInitFailures, InvalidMemberNameBreaksBroadcast)
{
    // Hyphens are legal in bus names but not in the signal member IARM derives from it.
    ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_Init("L1-Client"));
    ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_Connect());
    ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_RegisterEvent(2));

    int value = 1;
    EXPECT_EQ(IARM_RESULT_INVALID_PARAM, IARM_Bus_BroadcastEvent("L1-Client", 1, &value, sizeof(value)));

    EXPECT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_Disconnect());
    EXPECT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_Term());
}

TEST(IarmBusDaemonUnavailable, ClientCallsReportDaemonFailures)
{
    ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_Init(kClientName));
    ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_Connect());
    StopIarmDaemon();

    int registered = 1;
    IARM_Bus_CommonAPI_PowerPreChange_Param_t power{IARM_BUS_PWRMGR_POWERSTATE_ON, IARM_BUS_PWRMGR_POWERSTATE_STANDBY};
    IARM_Bus_CommonAPI_ResChange_Param_t res{1280, 720};

    EXPECT_EQ(IARM_RESULT_INVALID_STATE, IARM_Bus_IsConnected(kClientName, &registered));
    EXPECT_EQ(0, registered);
    EXPECT_EQ(IARM_RESULT_INVALID_STATE, IARM_BusDaemon_PowerPrechange(power));
    EXPECT_EQ(IARM_RESULT_INVALID_STATE, IARM_BusDaemon_DeepSleepWakeup(power));
    EXPECT_EQ(IARM_RESULT_INVALID_STATE, IARM_BusDaemon_ResolutionPrechange(res));
    EXPECT_EQ(IARM_RESULT_INVALID_STATE, IARM_BusDaemon_ResolutionPostchange(res));
    EXPECT_EQ(IARM_RESULT_INVALID_STATE, IARM_BusDaemon_RequestOwnership(IARM_BUS_RESOURCE_FOCUS));
    EXPECT_EQ(IARM_RESULT_INVALID_STATE, IARM_BusDaemon_ReleaseOwnership(IARM_BUS_RESOURCE_FOCUS));
    // The call registers locally even though the daemon cannot record the pre-change hook.
    EXPECT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_RegisterCall(IARM_BUS_COMMON_API_PowerPreChange, NoopCall));

    EXPECT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_Disconnect());
    EXPECT_EQ(IARM_RESULT_INVALID_STATE, IARM_Bus_Connect());
    EXPECT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_Term());

    ASSERT_NO_FATAL_FAILURE(StartIarmDaemon());
}

TEST(IarmBusDaemonProcess, HandlesTrailingDebugConfigAndSigint)
{
    StopIarmDaemon(SIGINT);
    ASSERT_NO_FATAL_FAILURE(StartIarmDaemon({"--debugconfig"}));

    ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_Init(kClientName));
    ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_Connect());
    int registered = 0;
    EXPECT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_IsConnected(kClientName, &registered));
    EXPECT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_Disconnect());
    EXPECT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_Term());
}

TEST(IarmBusDaemonProcess, LegacyMainServesClients)
{
    StopIarmDaemon();
    for (const auto &args : {std::vector<std::string>{"--debugconfig", "/dev/null"},
                             std::vector<std::string>{"--debugconfig"}}) {
        ASSERT_NO_FATAL_FAILURE(StartLegacyIarmDaemon(args));

        ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_Init(kClientName));
        ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_Connect());
        int registered = 0;
        EXPECT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_IsConnected(kClientName, &registered));
        EXPECT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_Disconnect());
        EXPECT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_Term());

        StopIarmDaemon(args.size() == 1 ? SIGINT : SIGTERM);
    }
    ASSERT_NO_FATAL_FAILURE(StartIarmDaemon());
}

#include <gtest/gtest.h>

#include "libIARM.h"
#include "libIBus.h"
#include "libIBusDaemon.h"

extern IARM_Result_t IARM_Bus_DaemonStart(int argc, char *argv[]);
extern IARM_Result_t IARM_Bus_DaemonStop(void);

extern int connectCalls;
extern int disconnectCalls;
extern int eventRegistrationCalls;
extern int methodRegistrationCalls;

TEST(IARMBusDaemon, StartsAndStopsWithRegisteredMethods)
{
    connectCalls = 0;
    disconnectCalls = 0;
    eventRegistrationCalls = 0;
    methodRegistrationCalls = 0;

    ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_DaemonStart(0, nullptr));
    EXPECT_EQ(1, connectCalls);
    EXPECT_EQ(1, eventRegistrationCalls);
    EXPECT_EQ(11, methodRegistrationCalls);
    EXPECT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_DaemonStop());
    EXPECT_EQ(1, disconnectCalls);
}
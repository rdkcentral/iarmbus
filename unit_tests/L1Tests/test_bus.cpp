#include <gtest/gtest.h>

#include <string>

#include "iarm_tp.h"
#include "libIBus.h"

extern "C" IARM_Result_t __real_IARM_Bus_RegisterEvent(IARM_EventId_t);
extern "C" IARM_Result_t __real_IARM_Bus_Disconnect(void);

extern IARM_Result_t initResult;
extern IARM_Result_t termResult;
extern int initCalls;
extern int termCalls;

TEST(IARMBusLifecycle, InitializationFailureCanBeRetried)
{
    initCalls = 0;
    initResult = IARM_RESULT_IPCCORE_FAIL;
    EXPECT_EQ(IARM_RESULT_IPCCORE_FAIL, IARM_Bus_Init("l1-client"));
    EXPECT_EQ(1, initCalls);

    initResult = IARM_RESULT_SUCCESS;
    ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_Init("l1-client"));
    EXPECT_EQ(2, initCalls);
    EXPECT_EQ(IARM_RESULT_INVALID_STATE, IARM_Bus_Init("l1-client"));
    EXPECT_EQ(2, initCalls);
    EXPECT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_Term());
}

TEST(IARMBusLifecycle, DisconnectedOperationsReturnInvalidState)
{
    initResult = IARM_RESULT_SUCCESS;
    ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_Init("l1-client"));

    EXPECT_EQ(IARM_RESULT_INVALID_STATE, __real_IARM_Bus_RegisterEvent(2));
    EXPECT_EQ(IARM_RESULT_INVALID_STATE, IARM_Bus_Call("other", "method", nullptr, 0));
    EXPECT_EQ(IARM_RESULT_INVALID_STATE, __real_IARM_Bus_Disconnect());
    EXPECT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_Term());
}

TEST(IARMBusLifecycle, TerminationPassesThroughCoreFailure)
{
    termCalls = 0;
    termResult = IARM_RESULT_IPCCORE_FAIL;
    ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_Init("l1-client"));
    EXPECT_EQ(IARM_RESULT_IPCCORE_FAIL, IARM_Bus_Term());
    EXPECT_EQ(1, termCalls);
    termResult = IARM_RESULT_SUCCESS;
    EXPECT_EQ(IARM_RESULT_INVALID_PARAM, IARM_Bus_Term());
}

TEST(IARMBusTraceparent, InvalidInputClearsPendingValue)
{
    EXPECT_EQ(nullptr, IARM_Bus_GetTraceparent());
    IARM_Bus_SetTraceparent("invalid");
    EXPECT_EQ(nullptr, IARM_Bus_GetTraceparent());
    IARM_Bus_SetTraceparent(nullptr);
    EXPECT_EQ(nullptr, IARM_Bus_GetTraceparent());
}

TEST(IARMBusTraceparent, ValidatesEnvelopeFormat)
{
    const std::string valid =
        "00-0123456789abcdef0123456789abcdef-0123456789abcdef-01";
    ASSERT_EQ(55u, valid.size());
    EXPECT_EQ(1, iarm_tp_valid(valid.c_str()));
    EXPECT_EQ(0, iarm_tp_valid(nullptr));
    EXPECT_EQ(0, iarm_tp_valid("00-short"));

    std::string invalid = valid;
    invalid[35] = 'x';
    EXPECT_EQ(0, iarm_tp_valid(invalid.c_str()));
    invalid = valid;
    invalid[0] = '1';
    EXPECT_EQ(0, iarm_tp_valid(invalid.c_str()));
}
#include <gtest/gtest.h>

#include <cstring>

#include "libIARMCore.h"

extern int mallocLocalCount;
extern int freeLocalCount;

TEST(IARMCoreMemory, ProcessLocalAllocationAndFree)
{
    const int allocations = mallocLocalCount;
    const int frees = freeLocalCount;
    void *payload = nullptr;

    ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Malloc(IARM_MEMTYPE_PROCESSLOCAL, 32, &payload));
    ASSERT_NE(nullptr, payload);
    EXPECT_EQ(allocations + 1, mallocLocalCount);
    std::memset(payload, 0x5a, 32);
    EXPECT_EQ(IARM_RESULT_SUCCESS, IARM_Free(IARM_MEMTYPE_PROCESSLOCAL, payload));
    EXPECT_EQ(frees + 1, freeLocalCount);
}

TEST(IARMCoreMemory, ThreadLocalAllocationAndNullFree)
{
    const int allocations = mallocLocalCount;
    const int frees = freeLocalCount;
    void *payload = nullptr;

    ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Malloc(IARM_MEMTYPE_THREADLOCAL, 8, &payload));
    ASSERT_NE(nullptr, payload);
    std::memset(payload, 0x5a, 8);
    EXPECT_EQ(IARM_RESULT_SUCCESS, IARM_Free(IARM_MEMTYPE_THREADLOCAL, payload));
    EXPECT_EQ(IARM_RESULT_SUCCESS, IARM_Free(IARM_MEMTYPE_PROCESSLOCAL, nullptr));
    EXPECT_EQ(allocations, mallocLocalCount);
    EXPECT_EQ(frees, freeLocalCount);
}

TEST(IARMCoreState, UninitializedCallsRejectRegistrationAndRpc)
{
    int registered = 0;

    EXPECT_EQ(IARM_RESULT_INVALID_PARAM, IARM_RegisterEvent("l1-client", 2));
    EXPECT_EQ(IARM_RESULT_INVALID_PARAM,
              IARM_RegisterCall("l1-client", "method", nullptr, nullptr));
    EXPECT_EQ(IARM_RESULT_INVALID_PARAM,
              IARM_IsCallRegistered("l1-client", "method", &registered));
    EXPECT_EQ(IARM_RESULT_INVALID_PARAM,
              IARM_CallWithTimeout("l1-client", "method", nullptr, 1, nullptr));
}
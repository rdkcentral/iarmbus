#pragma once

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>

#include "libIARM.h"
#include "libIBus.h"

constexpr const char *kClientName = "L1Client";

// Scratch directory owned by the private dbus-daemon started for this process.
const std::string &DbusTestWorkDir();

// Counts callbacks delivered on IARM's dispatch thread so tests can wait for them.
class CallbackLatch {
public:
    void Signal()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++count_;
        cond_.notify_all();
    }

    bool WaitFor(int expected, std::chrono::milliseconds timeout = std::chrono::seconds(3))
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return cond_.wait_for(lock, timeout, [&] { return count_ >= expected; });
    }

    int Count()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return count_;
    }

private:
    std::mutex mutex_;
    std::condition_variable cond_;
    int count_ = 0;
};

class IarmBusClientTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_Init(kClientName));
        ASSERT_EQ(IARM_RESULT_SUCCESS, IARM_Bus_Connect());
    }

    void TearDown() override
    {
        IARM_Bus_Disconnect();
        IARM_Bus_Term();
    }
};

#pragma once

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <mutex>
#include <thread>
#include <vector>

#include <dbus/dbus.h>
#include <string>

#include "libIARM.h"
#include "libIBus.h"

constexpr const char *kClientName = "L1Client";

// Scratch directory owned by the private dbus-daemon started for this process.
const std::string &DbusTestWorkDir();

// Control the instrumented iarmbusd; StartIarmDaemon uses gtest assertions.
void StopIarmDaemon(int signal = SIGTERM);
void StartIarmDaemon(const std::vector<std::string> &args = {"--debugconfig", "/dev/null"});
// Same, but launches the core/IARMDaemonMain.c build; stop it with StopIarmDaemon.
void StartLegacyIarmDaemon(const std::vector<std::string> &args);

// A bare libdbus connection that owns process.iarm.<name> and answers IARM
// method calls from its own thread, used to fake peers and malformed replies.
class RawDbusPeer {
public:
    explicit RawDbusPeer(const std::string &name);
    ~RawDbusPeer();
    bool OwnsName() const { return owned_; }

private:
    void Run();
    DBusConnection *conn_ = nullptr;
    bool owned_ = false;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

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

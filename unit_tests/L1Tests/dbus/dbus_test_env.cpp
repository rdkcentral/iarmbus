#include "dbus_test_env.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include <pthread.h>
#include <sys/wait.h>
#include <unistd.h>

extern IARM_Result_t IARM_Bus_DaemonStart(int argc, char *argv[]);
extern IARM_Result_t IARM_Bus_DaemonStop(void);

namespace {

std::string g_workDir;

void StopChild(pid_t pid)
{
    if (pid <= 0) {
        return;
    }
    kill(pid, SIGTERM);
    for (int i = 0; i < 100; ++i) {
        if (waitpid(pid, nullptr, WNOHANG) == pid) {
            return;
        }
        usleep(50 * 1000);
    }
    kill(pid, SIGKILL);
    waitpid(pid, nullptr, 0);
}

// Starts a private dbus-daemon and an IARM bus daemon child so the real
// libIARM/libIBus DBus paths run without touching the host system bus.
class DbusBusEnvironment : public ::testing::Environment {
public:
    void SetUp() override
    {
        char dir[] = "/tmp/iarm-l1-XXXXXX";
        ASSERT_NE(nullptr, mkdtemp(dir));
        g_workDir = dir;

        const std::string conf = g_workDir + "/bus.conf";
        std::ofstream(conf)
            << "<!DOCTYPE busconfig PUBLIC \"-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN\"\n"
               " \"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd\">\n"
               "<busconfig>\n"
               "  <type>session</type>\n"
               "  <listen>unix:path=" << g_workDir << "/bus</listen>\n"
               "  <auth>EXTERNAL</auth>\n"
               "  <policy context=\"default\">\n"
               "    <allow user=\"*\"/>\n"
               "    <allow own=\"*\"/>\n"
               "    <allow send_destination=\"*\" eavesdrop=\"true\"/>\n"
               "    <allow eavesdrop=\"true\"/>\n"
               "  </policy>\n"
               "</busconfig>\n";

        ASSERT_NO_FATAL_FAILURE(StartDbusDaemon(conf));
        ASSERT_NO_FATAL_FAILURE(StartIarmDaemon());
    }

    void TearDown() override
    {
        StopChild(iarmDaemonPid_);
        StopChild(dbusDaemonPid_);
        if (!g_workDir.empty()) {
            std::remove((g_workDir + "/bus.conf").c_str());
            std::remove((g_workDir + "/bus").c_str());
            rmdir(g_workDir.c_str());
        }
    }

private:
    void StartDbusDaemon(const std::string &conf)
    {
        int out[2];
        ASSERT_EQ(0, pipe(out));
        fflush(nullptr);
        dbusDaemonPid_ = fork();
        ASSERT_GE(dbusDaemonPid_, 0);
        if (dbusDaemonPid_ == 0) {
            dup2(out[1], STDOUT_FILENO);
            close(out[0]);
            close(out[1]);
            const std::string confArg = "--config-file=" + conf;
            execlp("dbus-daemon", "dbus-daemon", "--nofork", "--nopidfile",
                   "--print-address", confArg.c_str(), (char *)nullptr);
            _exit(127);
        }
        close(out[1]);

        std::string address;
        char c;
        while (read(out[0], &c, 1) == 1 && c != '\n') {
            address += c;
        }
        close(out[0]);
        ASSERT_FALSE(address.empty()) << "dbus-daemon failed to start (is the 'dbus' package installed?)";
        setenv("DBUS_SYSTEM_BUS_ADDRESS", address.c_str(), 1);
    }

    void StartIarmDaemon()
    {
        int ready[2];
        ASSERT_EQ(0, pipe(ready));
        fflush(nullptr);
        iarmDaemonPid_ = fork();
        ASSERT_GE(iarmDaemonPid_, 0);
        if (iarmDaemonPid_ == 0) {
            close(ready[0]);
            sigset_t set;
            sigemptyset(&set);
            sigaddset(&set, SIGTERM);
            pthread_sigmask(SIG_BLOCK, &set, nullptr);

            IARM_Bus_DaemonStart(0, nullptr);
            const char ok = 1;
            (void)!write(ready[1], &ok, 1);
            close(ready[1]);

            int sig = 0;
            sigwait(&set, &sig);
            IARM_Bus_DaemonStop();
            // exit() rather than _exit() so gcov flushes the daemon's coverage.
            exit(0);
        }
        close(ready[1]);
        char ok = 0;
        ASSERT_EQ(1, read(ready[0], &ok, 1)) << "IARM daemon child exited before becoming ready";
        close(ready[0]);
    }

    pid_t dbusDaemonPid_ = -1;
    pid_t iarmDaemonPid_ = -1;
};

::testing::Environment *const g_env = ::testing::AddGlobalTestEnvironment(new DbusBusEnvironment);

}  // namespace

const std::string &DbusTestWorkDir()
{
    return g_workDir;
}

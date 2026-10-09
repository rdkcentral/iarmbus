#include "dbus_test_env.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

std::string g_workDir;
pid_t g_iarmDaemonPid = -1;

void StopChild(pid_t pid, int signal = SIGTERM)
{
    if (pid <= 0) {
        return;
    }
    kill(pid, signal);
    for (int i = 0; i < 100; ++i) {
        if (waitpid(pid, nullptr, WNOHANG) == pid) {
            return;
        }
        usleep(50 * 1000);
    }
    kill(pid, SIGKILL);
    waitpid(pid, nullptr, 0);
}

// Starts a private dbus-daemon and the instrumented iarmbusd so the real
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

        // Invalid-name tests rely on libdbus returning NULL instead of aborting.
        setenv("DBUS_FATAL_WARNINGS", "0", 1);
        ASSERT_NO_FATAL_FAILURE(StartDbusDaemon(conf));
        ASSERT_NO_FATAL_FAILURE(StartIarmDaemon());
    }

    void TearDown() override
    {
        StopIarmDaemon();
        StopChild(dbusDaemonPid_);
        if (!g_workDir.empty()) {
            std::remove((g_workDir + "/bus.conf").c_str());
            std::remove((g_workDir + "/bus").c_str());
            std::remove((g_workDir + "/iarmbusd.log").c_str());
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

    pid_t dbusDaemonPid_ = -1;
};

::testing::Environment *const g_env = ::testing::AddGlobalTestEnvironment(new DbusBusEnvironment);

}  // namespace

const std::string &DbusTestWorkDir()
{
    return g_workDir;
}

void StopIarmDaemon(int signal)
{
    StopChild(g_iarmDaemonPid, signal);
    g_iarmDaemonPid = -1;
}

void StartIarmDaemon(const std::vector<std::string> &args)
{
    const std::string logPath = g_workDir + "/iarmbusd.log";
    // Remove before forking so readiness polling can never see a previous daemon's log.
    std::remove(logPath.c_str());
    fflush(nullptr);
    g_iarmDaemonPid = fork();
    ASSERT_GE(g_iarmDaemonPid, 0);
    if (g_iarmDaemonPid == 0) {
        const int log = open(logPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        dup2(log, STDOUT_FILENO);
        close(log);
        std::vector<char *> argv{const_cast<char *>(IARMBUSD_PATH)};
        for (const std::string &arg : args) {
            argv.push_back(const_cast<char *>(arg.c_str()));
        }
        argv.push_back(nullptr);
        execv(IARMBUSD_PATH, argv.data());
        _exit(127);
    }

    // iarmbusd logs "servers Entering" once before and once after IARM_Bus_DaemonStart.
    for (int i = 0; i < 250; ++i) {
        std::ifstream in(logPath);
        std::stringstream text;
        text << in.rdbuf();
        const std::string log = text.str();
        const size_t first = log.find("servers Entering");
        if (first != std::string::npos && log.find("servers Entering", first + 1) != std::string::npos) {
            return;
        }
        if (waitpid(g_iarmDaemonPid, nullptr, WNOHANG) == g_iarmDaemonPid) {
            g_iarmDaemonPid = -1;
            FAIL() << "iarmbusd exited during startup:\n" << log;
        }
        usleep(20 * 1000);
    }
    FAIL() << "iarmbusd did not become ready";
}

RawDbusPeer::RawDbusPeer(const std::string &name)
{
    dbus_threads_init_default();
    DBusError err;
    dbus_error_init(&err);
    conn_ = dbus_bus_get_private(DBUS_BUS_SYSTEM, &err);
    dbus_error_free(&err);
    if (conn_ == nullptr) {
        return;
    }
    dbus_connection_set_exit_on_disconnect(conn_, FALSE);
    // No ALLOW_REPLACEMENT, so IARM_Init's REPLACE_EXISTING request gets queued instead.
    const std::string busName = "process.iarm." + name;
    owned_ = dbus_bus_request_name(conn_, busName.c_str(), DBUS_NAME_FLAG_DO_NOT_QUEUE, &err) ==
             DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER;
    dbus_error_free(&err);
    thread_ = std::thread([this] { Run(); });
}

RawDbusPeer::~RawDbusPeer()
{
    stop_ = true;
    if (thread_.joinable()) {
        thread_.join();
    }
    if (conn_ != nullptr) {
        dbus_connection_close(conn_);
        dbus_connection_unref(conn_);
    }
}

// Replies to "EmptyReply" with no arguments, "StringReply" with a string, and
// anything else the way IARM_CallReturn does (int32 result + payload bytes).
void RawDbusPeer::Run()
{
    constexpr int kIarmHeaderSize = 8 + sizeof(size_t);
    while (!stop_ && dbus_connection_read_write(conn_, 50)) {
        DBusMessage *msg;
        while ((msg = dbus_connection_pop_message(conn_)) != nullptr) {
            if (dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_METHOD_CALL &&
                dbus_message_has_interface(msg, "iarm.method.Type")) {
                DBusMessage *reply = dbus_message_new_method_return(msg);
                const char *member = dbus_message_get_member(msg);
                if (std::strcmp(member, "StringReply") == 0) {
                    const char *text = "unexpected";
                    dbus_message_append_args(reply, DBUS_TYPE_STRING, &text, DBUS_TYPE_INVALID);
                } else if (std::strcmp(member, "EmptyReply") != 0) {
                    DBusMessageIter args, array;
                    const unsigned char *bytes = nullptr;
                    int len = 0;
                    if (dbus_message_iter_init(msg, &args) && dbus_message_iter_next(&args) &&
                        dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_ARRAY) {
                        dbus_message_iter_recurse(&args, &array);
                        dbus_message_iter_get_fixed_array(&array, &bytes, &len);
                    }
                    const dbus_int32_t result = 0;
                    const unsigned char *payload = len > kIarmHeaderSize ? bytes + kIarmHeaderSize : bytes;
                    const int payloadLen = len > kIarmHeaderSize ? len - kIarmHeaderSize : 0;
                    dbus_message_append_args(reply, DBUS_TYPE_INT32, &result, DBUS_TYPE_ARRAY, DBUS_TYPE_BYTE,
                                             &payload, payloadLen, DBUS_TYPE_INVALID);
                }
                dbus_connection_send(conn_, reply, nullptr);
                dbus_connection_flush(conn_);
                dbus_message_unref(reply);
            }
            dbus_message_unref(msg);
        }
    }
}

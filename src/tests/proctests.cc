#include <cassert>
#include <iostream>
#include <list>
#include <utility>
#include <string>

#include "service.h"
#include "proc-service.h"

// Tests of process-service related functionality.
//
// These tests work mostly by completely mocking out the base_process_service class. The mock
// implementations can be found in test-baseproc.cc.

// Friend interface to access base_process_service private/protected members.
class base_process_service_test
{
    public:
    static void exec_succeeded(base_process_service *bsp)
    {
        bsp->waiting_for_execstat = false;
        bsp->exec_succeeded();
    }

    static void handle_exit(base_process_service *bsp, int exit_status)
    {
        bsp->pid = -1;
        bsp->handle_exit_status(exit_status);
    }
};

namespace bp_sys {
    // last signal sent:
    extern int last_sig_sent;
    extern pid_t last_forked_pid;
}

static void init_service_defaults(process_service &ps)
{
    ps.set_restart_interval(time_val(10,0), 3);
    ps.set_restart_delay(time_val(0, 200000000)); // 200 milliseconds
    ps.set_stop_timeout(time_val(10,0));
    ps.set_start_timeout(time_val(10,0));
    ps.set_start_interruptible(false);
}

// Regular service start
void test_proc_service_start()
{
    using namespace std;

    service_set sset;

    string command = "test-command";
    list<pair<unsigned,unsigned>> command_offsets;
    command_offsets.emplace_back(0, command.length());
    std::list<prelim_dep> depends;

    process_service p = process_service(&sset, "testproc", std::move(command), command_offsets, depends);
    init_service_defaults(p);

    p.start(true);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STARTING);

    base_process_service_test::exec_succeeded(&p);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STARTED);
}

// Unexpected termination
void test_proc_unexpected_term()
{
    using namespace std;

    service_set sset;

    string command = "test-command";
    list<pair<unsigned,unsigned>> command_offsets;
    command_offsets.emplace_back(0, command.length());
    std::list<prelim_dep> depends;

    process_service p = process_service(&sset, "testproc", std::move(command), command_offsets, depends);
    p.start(true);
    sset.process_queues();

    base_process_service_test::exec_succeeded(&p);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STARTED);

    base_process_service_test::handle_exit(&p, 0);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STOPPED);
}

// Termination via stop request
void test_term_via_stop()
{
    using namespace std;

    service_set sset;

    string command = "test-command";
    list<pair<unsigned,unsigned>> command_offsets;
    command_offsets.emplace_back(0, command.length());
    std::list<prelim_dep> depends;

    process_service p = process_service(&sset, "testproc", std::move(command), command_offsets, depends);
    init_service_defaults(p);

    p.start(true);
    sset.process_queues();

    base_process_service_test::exec_succeeded(&p);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STARTED);

    p.stop(true);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STOPPING);

    base_process_service_test::handle_exit(&p, 0);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STOPPED);
}

// Time-out during start
void test_proc_start_timeout()
{
    using namespace std;

    service_set sset;

    string command = "test-command";
    list<pair<unsigned,unsigned>> command_offsets;
    command_offsets.emplace_back(0, command.length());
    std::list<prelim_dep> depends;

    process_service p = process_service(&sset, "testproc", std::move(command), command_offsets, depends);
    init_service_defaults(p);

    p.start(true);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STARTING);

    p.timer_expired();
    sset.process_queues();

    assert(p.get_state() == service_state_t::STOPPING);

    base_process_service_test::handle_exit(&p, 0);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STOPPED);
}

// Test stop timeout
void test_proc_stop_timeout()
{
    using namespace std;

    service_set sset;

    string command = "test-command";
    list<pair<unsigned,unsigned>> command_offsets;
    command_offsets.emplace_back(0, command.length());
    std::list<prelim_dep> depends;

    process_service p = process_service(&sset, "testproc", std::move(command), command_offsets, depends);
    init_service_defaults(p);

    p.start(true);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STARTING);

    base_process_service_test::exec_succeeded(&p);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STARTED);

    p.stop(true);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STOPPING);
    assert(bp_sys::last_sig_sent == SIGTERM);

    p.timer_expired();
    sset.process_queues();

    // kill signal (SIGKILL) should have been sent; process not dead until it's dead, however
    assert(p.get_state() == service_state_t::STOPPING);
    assert(bp_sys::last_sig_sent == SIGKILL);

    base_process_service_test::handle_exit(&p, 0);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STOPPED);
}

// Smooth recovery
void test_proc_smooth_recovery1()
{
    using namespace std;

    service_set sset;

    string command = "test-command";
    list<pair<unsigned,unsigned>> command_offsets;
    command_offsets.emplace_back(0, command.length());
    std::list<prelim_dep> depends;

    process_service p = process_service(&sset, "testproc", std::move(command), command_offsets, depends);
    init_service_defaults(p);
    p.set_smooth_recovery(true);

    p.start(true);
    sset.process_queues();

    base_process_service_test::exec_succeeded(&p);
    sset.process_queues();

    pid_t first_instance = bp_sys::last_forked_pid;

    assert(p.get_state() == service_state_t::STARTED);

    base_process_service_test::handle_exit(&p, 0);
    sset.process_queues();

    // since time hasn't been changed, we expect that the process has not yet been re-launched:
    assert(first_instance == bp_sys::last_forked_pid);
    assert(p.get_state() == service_state_t::STARTED);

    p.timer_expired();
    sset.process_queues();

    // Now a new process should've been launched:
    assert(first_instance + 1 == bp_sys::last_forked_pid);
    assert(p.get_state() == service_state_t::STARTED);
}

// Smooth recovery without restart delay
void test_proc_smooth_recovery2()
{
    using namespace std;

    service_set sset;

    string command = "test-command";
    list<pair<unsigned,unsigned>> command_offsets;
    command_offsets.emplace_back(0, command.length());
    std::list<prelim_dep> depends;

    process_service p = process_service(&sset, "testproc", std::move(command), command_offsets, depends);
    init_service_defaults(p);
    p.set_smooth_recovery(true);
    p.set_restart_delay(time_val(0, 0));

    p.start(true);
    sset.process_queues();

    base_process_service_test::exec_succeeded(&p);
    sset.process_queues();

    pid_t first_instance = bp_sys::last_forked_pid;

    assert(p.get_state() == service_state_t::STARTED);

    base_process_service_test::handle_exit(&p, 0);
    sset.process_queues();

    // no restart delay, process should restart immediately:
    assert(first_instance + 1 == bp_sys::last_forked_pid);
    assert(p.get_state() == service_state_t::STARTED);
}

#define RUN_TEST(name, spacing) \
    std::cout << #name "..." spacing; \
    name(); \
    std::cout << "PASSED" << std::endl;

int main(int argc, char **argv)
{
    RUN_TEST(test_proc_service_start, "   ");
    RUN_TEST(test_proc_unexpected_term, " ");
    RUN_TEST(test_term_via_stop, "        ");
    RUN_TEST(test_proc_start_timeout, "   ");
    RUN_TEST(test_proc_stop_timeout, "    ");
    RUN_TEST(test_proc_smooth_recovery1, "");
    RUN_TEST(test_proc_smooth_recovery2, "");
}

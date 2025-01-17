/* Generated from orogen/lib/orogen/templates/tasks/Task.cpp */

#include "Task.hpp"
#include <base-logging/Logging.hpp>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

using namespace linux_gpios;
using namespace std;

Task::Task(string const& name)
    : TaskBase(name)
{
}

Task::Task(string const& name, RTT::ExecutionEngine* engine)
    : TaskBase(name, engine)
{
}

Task::~Task()
{
}

/// The following lines are template definitions for the various state machine
// hooks defined by Orocos::RTT. See Task.hpp for more detailed
// documentation about them.

bool Task::configureHook()
{
    if (!TaskBase::configureHook())
        return false;

    m_write_configuration = _w_configuration.get();
    if (!m_write_configuration.defaults.empty()) {
        if (m_write_configuration.defaults.size() != m_write_configuration.ids.size()) {
            LOG_ERROR_S << "defaults array and ids array have different sizes in write "
                           "configuration"
                        << std::endl;
            return false;
        }
    }
    m_write_fds = openGPIOs(m_write_configuration.ids, O_WRONLY, _sysfs_gpio_path.get());
    m_command.states.resize(m_write_fds.size());
    m_read_fds = openGPIOs(_r_configuration.get().ids, O_RDONLY, _sysfs_gpio_path.get());
    m_state.states.resize(m_read_fds.size());
    return true;
}

bool Task::startHook()
{
    if (!TaskBase::startHook())
        return false;

    auto now = base::Time::now();
    for (size_t i = 0; i < m_read_fds.size(); ++i) {
        bool value = readGPIO(m_read_fds[i]);
        m_state.states[i].time = now;
        m_state.states[i].data = value;
    }
    _r_states.write(m_state);

    writeDefaults();
    return true;
}

void Task::updateHook()
{
    bool should_output = !_edge_triggered_output.get();
    should_output = handleWriteSide() || should_output;

    auto now = base::Time::now();
    for (size_t i = 0; i < m_read_fds.size(); ++i) {
        int fd = m_read_fds[i];
        bool value = readGPIO(fd);
        if (m_state.states[i].data != value) {
            should_output = true;
            m_state.states[i].time = now;
            m_state.states[i].data = value;
        }
    }
    if (should_output) {
        m_state.time = now;
        _r_states.write(m_state);
    }
    TaskBase::updateHook();
}

bool Task::handleWriteSide()
{
    auto now = base::Time::now();
    auto flow = _w_commands.read(m_command, false);
    if (flow == RTT::NoData) {
        writeDefaults();
        return false;
    }
    else if (flow == RTT::OldData) {
        if (m_command.time + m_write_configuration.timeout < now) {
            writeDefaults();
        }
        return false;
    }

    while (flow == RTT::NewData) {
        if (m_command.states.size() != m_write_fds.size()) {
            exception(UNEXPECTED_COMMAND_SIZE);
            return false;
        }

        // Update the time for timeout/defaults handling
        m_command.time = now;
        for (size_t i = 0; i < m_write_fds.size(); ++i) {
            writeGPIO(m_write_fds[i], m_command.states[i].data);
        }
        flow = _w_commands.read(m_command, false);
    }
    return true;
}

void Task::writeDefaults()
{
    if (m_write_configuration.defaults.empty()) {
        return;
    }

    for (size_t i = 0; i < m_write_fds.size(); ++i) {
        writeGPIO(m_write_fds[i], m_write_configuration.defaults[i]);
    }
}

void Task::errorHook()
{
    TaskBase::errorHook();
}
void Task::stopHook()
{
    TaskBase::stopHook();
}
void Task::cleanupHook()
{
    closeAll();
    TaskBase::cleanupHook();
}

namespace {
    struct CloseGuard {
        vector<int> fds;
        CloseGuard()
        {
        }
        ~CloseGuard()
        {
            for (int fd : fds)
                close(fd);
        }
        void push_back(int fd)
        {
            fds.push_back(fd);
        }
        vector<int> release()
        {
            vector<int> temp(move(fds));
            fds.clear();
            return temp;
        }
    };
}

std::vector<int> Task::openGPIOs(std::vector<int32_t> const& ids,
    int mode,
    string const& sysfs_root_path)
{
    CloseGuard guard;
    for (int id : ids) {
        string sysfs_path = sysfs_root_path + "/gpio" + to_string(id) + "/value";
        int fd = open(sysfs_path.c_str(), mode);
        if (fd == -1) {
            throw runtime_error("Failed to open " + sysfs_path + ": " + strerror(errno));
        }
        guard.push_back(fd);
    }
    return guard.release();
}

void Task::closeAll()
{
    for (int fd : m_write_fds) {
        close(fd);
    }
    m_write_fds.clear();

    for (int fd : m_read_fds) {
        close(fd);
    }
    m_read_fds.clear();
}

void Task::writeGPIO(int fd, bool value)
{
    lseek(fd, 0, SEEK_SET);
    char buf = value ? '1' : '0';
    int ret = write(fd, &buf, 1);
    if (ret != 1) {
        exception(IO_ERROR);
        throw std::runtime_error("failed to write GPIO status");
    }
}

bool Task::readGPIO(int fd)
{
    lseek(fd, 0, SEEK_SET);
    char buf;
    int ret = read(fd, &buf, 1);
    if (ret != 1) {
        exception(IO_ERROR);
        throw std::runtime_error("failed to read GPIO status");
    }
    else
        return buf == '1';
}

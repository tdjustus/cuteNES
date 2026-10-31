#pragma once

// A child process with pipes on stdin and stdout, the way an MCP client
// launches a stdio server. stderr is inherited, so the server's log shows
// in the test output.

#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
// Without it windows.h brings in winsock.h, which clashes with the
// winsock2.h that httplib.h includes after it.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace cutenes::mcp::test {

class ChildProcess {
public:
    ChildProcess() = default;
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;

    ~ChildProcess()
    {
        if (running()) {
            closeStdin();
            if (!waitExit(std::chrono::seconds(10))) {
                kill();
            }
        }
        closeStdin();
        if (m_reader.joinable()) m_reader.join();
#ifdef _WIN32
        if (m_stdout) CloseHandle(m_stdout);
        if (m_process) CloseHandle(m_process);
#else
        if (m_stdout >= 0) ::close(m_stdout);
#endif
    }

    bool start(const std::string& exe, const std::vector<std::string>& args)
    {
#ifdef _WIN32
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        HANDLE inRead = nullptr, outWrite = nullptr;
        if (!CreatePipe(&inRead, &m_stdin, &sa, 0) || !CreatePipe(&m_stdout, &outWrite, &sa, 0)) {
            return false;
        }
        SetHandleInformation(m_stdin, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(m_stdout, HANDLE_FLAG_INHERIT, 0);
        std::wstring cmd = L"\"" + std::filesystem::path(exe).wstring() + L"\"";
        for (const auto& a : args) {
            cmd += L" \"" + std::filesystem::path(a).wstring() + L"\"";
        }
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = inRead;
        si.hStdOutput = outWrite;
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        PROCESS_INFORMATION pi{};
        const BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, 0, nullptr,
            nullptr, &si, &pi);
        CloseHandle(inRead);
        CloseHandle(outWrite);
        if (!ok) return false;
        CloseHandle(pi.hThread);
        m_process = pi.hProcess;
#else
        // A write to a server that already exited must fail, not kill the test.
        std::signal(SIGPIPE, SIG_IGN);
        int in[2], out[2];
        if (::pipe(in) != 0 || ::pipe(out) != 0) return false;
        for (int fd : {in[0], in[1], out[0], out[1]}) {
            ::fcntl(fd, F_SETFD, FD_CLOEXEC); // dup2 clears it on 0 and 1
        }
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, in[0], 0);
        posix_spawn_file_actions_adddup2(&actions, out[1], 1);
        std::vector<std::string> all{exe};
        all.insert(all.end(), args.begin(), args.end());
        std::vector<char*> argv;
        for (auto& a : all) argv.push_back(a.data());
        argv.push_back(nullptr);
        const int rc = posix_spawn(&m_pid, exe.c_str(), &actions, nullptr, argv.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        ::close(in[0]);
        ::close(out[1]);
        m_stdin = in[1];
        m_stdout = out[0];
        if (rc != 0) {
            m_pid = -1;
            return false;
        }
#endif
        m_running = true;
        m_reader = std::thread([this] { readLoop(); });
        return true;
    }

    bool writeLine(const std::string& line)
    {
        const std::string data = line + "\n";
#ifdef _WIN32
        DWORD written = 0;
        return m_stdin && WriteFile(m_stdin, data.data(), static_cast<DWORD>(data.size()), &written,
                              nullptr)
            && written == data.size();
#else
        return m_stdin >= 0 && ::write(m_stdin, data.data(), data.size()) == static_cast<ssize_t>(data.size());
#endif
    }

    // The client's shutdown signal (MCP stdio "Shutdown").
    void closeStdin()
    {
#ifdef _WIN32
        if (m_stdin) {
            CloseHandle(m_stdin);
            m_stdin = nullptr;
        }
#else
        if (m_stdin >= 0) {
            ::close(m_stdin);
            m_stdin = -1;
        }
#endif
    }

    // The next stdout line, or nullopt at end of output or after `timeout`.
    std::optional<std::string> readLine(std::chrono::milliseconds timeout)
    {
        std::unique_lock lock(m_mutex);
        m_cv.wait_for(lock, timeout, [&] { return !m_lines.empty() || m_eof; });
        if (m_lines.empty()) return std::nullopt;
        std::string line = std::move(m_lines.front());
        m_lines.pop_front();
        return line;
    }

    // The exit code, or nullopt if still running after `timeout`.
    std::optional<int> waitExit(std::chrono::milliseconds timeout)
    {
        if (!m_running) return m_exitCode;
#ifdef _WIN32
        if (WaitForSingleObject(m_process, static_cast<DWORD>(timeout.count())) != WAIT_OBJECT_0) {
            return std::nullopt;
        }
        DWORD code = 0;
        GetExitCodeProcess(m_process, &code);
        m_exitCode = static_cast<int>(code);
#else
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        int status = 0;
        while (::waitpid(m_pid, &status, WNOHANG) == 0) {
            if (std::chrono::steady_clock::now() > deadline) return std::nullopt;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        m_exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -WTERMSIG(status);
#endif
        m_running = false;
        return m_exitCode;
    }

    void kill()
    {
        if (!m_running) return;
#ifdef _WIN32
        TerminateProcess(m_process, 1);
#else
        ::kill(m_pid, SIGKILL);
#endif
        waitExit(std::chrono::seconds(10));
    }

    bool running() const { return m_running; }

private:
    void readLoop()
    {
        std::string pending;
        char buffer[4096];
        for (;;) {
#ifdef _WIN32
            DWORD n = 0;
            if (!ReadFile(m_stdout, buffer, sizeof(buffer), &n, nullptr) || n == 0) break;
#else
            const ssize_t n = ::read(m_stdout, buffer, sizeof(buffer));
            if (n <= 0) break;
#endif
            pending.append(buffer, static_cast<std::size_t>(n));
            std::size_t nl;
            while ((nl = pending.find('\n')) != std::string::npos) {
                std::lock_guard lock(m_mutex);
                m_lines.push_back(pending.substr(0, nl));
                pending.erase(0, nl + 1);
                m_cv.notify_all();
            }
        }
        std::lock_guard lock(m_mutex);
        if (!pending.empty()) m_lines.push_back(pending); // a line without its newline
        m_eof = true;
        m_cv.notify_all();
    }

#ifdef _WIN32
    HANDLE m_stdin = nullptr;
    HANDLE m_stdout = nullptr;
    HANDLE m_process = nullptr;
#else
    int m_stdin = -1;
    int m_stdout = -1;
    pid_t m_pid = -1;
#endif
    bool m_running = false;
    std::optional<int> m_exitCode;
    std::thread m_reader;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<std::string> m_lines;
    bool m_eof = false;
};

} // namespace cutenes::mcp::test

#include "legion/core/interfaces.hpp"

#include <chrono>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <poll.h>
#include <signal.h>
#include <sstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace legion::core {

namespace {

std::string trim_right(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) {
        s.pop_back();
    }
    return s;
}

class PosixSysfsReader final : public ISysfsReader {
public:
    bool exists(const std::string& path) const override {
        std::error_code ec;
        return std::filesystem::exists(path, ec);
    }

    std::optional<std::string> read_text(const std::string& path) const override {
        std::ifstream file(path);
        if (!file.is_open()) {
            return std::nullopt;
        }
        std::ostringstream ss;
        ss << file.rdbuf();
        if (file.bad()) {
            return std::nullopt;
        }
        return trim_right(ss.str());
    }

    std::vector<std::string> list_dir(const std::string& path) const override {
        std::vector<std::string> entries;
        std::error_code ec;
        if (!std::filesystem::exists(path, ec) || !std::filesystem::is_directory(path, ec)) {
            return entries;
        }
        for (const auto& entry : std::filesystem::directory_iterator(path, ec)) {
            if (ec) break;
            entries.push_back(entry.path().filename().string());
        }
        return entries;
    }
};

class PosixProcessRunner final : public IProcessRunner {
public:
    ProcessResult run(const std::vector<std::string>& args, int timeout_ms) const override {
        ProcessResult result;
        if (args.empty()) {
            result.spawn_failed = true;
            return result;
        }

        int stdout_pipe[2];
        int stderr_pipe[2];
        if (pipe(stdout_pipe) != 0) {
            result.spawn_failed = true;
            return result;
        }
        if (pipe(stderr_pipe) != 0) {
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);
            result.spawn_failed = true;
            return result;
        }

        pid_t pid = fork();
        if (pid < 0) {
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);
            close(stderr_pipe[0]);
            close(stderr_pipe[1]);
            result.spawn_failed = true;
            return result;
        }

        if (pid == 0) {
            dup2(stdout_pipe[1], STDOUT_FILENO);
            dup2(stderr_pipe[1], STDERR_FILENO);
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);
            close(stderr_pipe[0]);
            close(stderr_pipe[1]);

            std::vector<char*> argv;
            argv.reserve(args.size() + 1);
            for (const auto& arg : args) {
                argv.push_back(const_cast<char*>(arg.c_str()));
            }
            argv.push_back(nullptr);

            execvp(argv[0], argv.data());
            _exit(127);
        }

        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        fcntl(stdout_pipe[0], F_SETFL, O_NONBLOCK);
        fcntl(stderr_pipe[0], F_SETFL, O_NONBLOCK);

        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        bool out_open = true;
        bool err_open = true;

        while (out_open || err_open) {
            auto now = std::chrono::steady_clock::now();
            if (now >= deadline) {
                result.timed_out = true;
                kill(pid, SIGKILL);
                break;
            }
            int remaining_ms = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());

            struct pollfd fds[2];
            nfds_t nfds = 0;
            if (out_open) {
                fds[nfds++] = {stdout_pipe[0], POLLIN, 0};
            }
            if (err_open) {
                fds[nfds++] = {stderr_pipe[0], POLLIN, 0};
            }

            int ret = poll(fds, nfds, std::min(remaining_ms, 100));
            if (ret < 0 && errno == EINTR) continue;

            char buf[4096];
            if (out_open) {
                ssize_t n = read(stdout_pipe[0], buf, sizeof(buf));
                if (n > 0) {
                    result.stdout_text.append(buf, static_cast<size_t>(n));
                } else if (n == 0) {
                    out_open = false;
                }
            }
            if (err_open) {
                ssize_t n = read(stderr_pipe[0], buf, sizeof(buf));
                if (n > 0) {
                    result.stderr_text.append(buf, static_cast<size_t>(n));
                } else if (n == 0) {
                    err_open = false;
                }
            }
        }

        close(stdout_pipe[0]);
        close(stderr_pipe[0]);

        int status = 0;
        if (waitpid(pid, &status, 0) > 0 && WIFEXITED(status)) {
            result.exit_code = WEXITSTATUS(status);
        }
        return result;
    }
};

}  // namespace

std::unique_ptr<ISysfsReader> create_posix_sysfs_reader() {
    return std::make_unique<PosixSysfsReader>();
}

std::unique_ptr<IProcessRunner> create_posix_process_runner() {
    return std::make_unique<PosixProcessRunner>();
}

}  // namespace legion::core

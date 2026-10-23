#include "ocppi/cli/Process.hpp"

#include <errno.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

int runProcess(const std::string &binaryPath,
               const std::vector<std::string> &args, std::string &output)
{
        constexpr auto bufferSize = 1024;
        auto cArgs = std::make_unique<const char *[]>(args.size() + 2);

        cArgs[0] = binaryPath.data();
        for (std::size_t i = 1; i <= args.size(); ++i) {
                cArgs[i] = args[i - 1].data();
        }
        cArgs[args.size() + 1] = nullptr;

        int pipes[2];
        if (::pipe(pipes) == -1) {
                throw std::system_error(errno, std::generic_category(), "pipe");
        }

        pid_t childId{ -1 };
        childId = fork();
        if (childId == -1) {
                throw std::system_error(errno, std::generic_category(), "fork");
        }

        if (childId == 0) {
                ::close(pipes[0]);
                ::dup2(pipes[1], 1);

                execvp(binaryPath.data(), (char **)(cArgs.get()));
                // Reachable only in the forked child. Throwing here unwinds into
                // the caller's catch block, which turns the child into a second
                // copy of the parent instead of a failing process.
                ::_exit(EXIT_FAILURE);
        }

        ::close(pipes[1]);
        std::array<char, bufferSize> buffer{};

        ssize_t readCount{ 0 };
        while (true) {
                readCount = read(pipes[0], buffer.data(), bufferSize);
                if (readCount == -1) {
                        if (errno == EINTR) {
                                continue;
                        }

                        throw std::system_error(errno, std::generic_category(),
                                                "read");
                }

                if (readCount == 0) {
                        break;
                }

                output.append(buffer.data(), readCount);
        }

        int status{ 0 };
        int interruptTimes = 0;
        while (::waitpid(childId, &status, 0) == -1) {
                if (errno == EINTR) {
                        interruptTimes < 2 ? ++interruptTimes :
                                             kill(childId, SIGKILL);
                        continue;
                }

                ::close(pipes[0]);
                throw std::system_error(errno, std::generic_category(),
                                        "wait: " + std::to_string(errno));
        }

        ::close(pipes[0]);
        // wait() stores the raw status word; callers expect an exit code, so an
        // exit status of 1 came back as 0x0100.
        if (WIFEXITED(status)) {
                return WEXITSTATUS(status);
        }

        if (WIFSIGNALED(status)) {
                return 128 + WTERMSIG(status);
        }

        return EXIT_FAILURE;
}

int runProcess(const std::string &binaryPath,
               const std::vector<std::string> &args)
{
        auto cArgs = std::make_unique<const char *[]>(args.size() + 2);

        cArgs[0] = binaryPath.data();
        for (std::size_t i = 1; i <= args.size(); ++i) {
                cArgs[i] = args[i - 1].data();
        }
        cArgs[args.size() + 1] = nullptr;

        pid_t childId{ -1 };
        childId = fork();
        if (childId == -1) {
                throw std::system_error(errno, std::generic_category(), "fork");
        }

        if (childId == 0) {
                execvp(binaryPath.data(), (char **)(cArgs.get()));
                // Reachable only in the forked child. Throwing here unwinds into
                // the caller's catch block, which turns the child into a second
                // copy of the parent instead of a failing process.
                ::_exit(EXIT_FAILURE);
        }

        int status{ 0 };
        int interruptTimes = 0;
        while (::waitpid(childId, &status, 0) == -1) {
                if (errno == EINTR) {
                        interruptTimes < 2 ? ++interruptTimes :
                                             kill(childId, SIGKILL);
                        continue;
                }

                throw std::system_error(errno, std::generic_category(),
                                        "wait: " + std::to_string(errno));
        }

        if (WIFEXITED(status)) {
                return WEXITSTATUS(status);
        }

        if (WIFSIGNALED(status)) {
                return 128 + WTERMSIG(status);
        }

        return EXIT_FAILURE;
}

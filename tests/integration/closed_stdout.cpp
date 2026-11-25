#include <cerrno>
#include <fcntl.h>
#include <iostream>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

namespace {

int check(char *executable, char *argument) {
    int descriptors[2]{};
    if (pipe(descriptors) != 0) {
        return 1;
    }
    close(descriptors[0]);

    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        close(descriptors[1]);
        return 1;
    }
    pid_t child = 0;
    char *arguments[]{executable, argument, nullptr};
    const bool started =
        posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0) == 0 &&
        posix_spawn_file_actions_adddup2(&actions, descriptors[1], STDOUT_FILENO) == 0 &&
        posix_spawn_file_actions_addclose(&actions, descriptors[1]) == 0 &&
        posix_spawn(&child, executable, &actions, nullptr, arguments, environ) == 0;
    posix_spawn_file_actions_destroy(&actions);
    close(descriptors[1]);
    if (!started) {
        return 1;
    }
    int status = 0;
    pid_t waited = 0;
    do {
        waited = waitpid(child, &status, 0);
    } while (waited == -1 && errno == EINTR);
    if (waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 1) {
        return 0;
    }
    std::cerr << "Expected exit 1 with closed stdout, got wait status " << status << '\n';
    return 1;
}

} // namespace

int main(int argc, char *argv[]) {
    if (argc != 2) {
        return 2;
    }
    char help[] = "--help";
    char version[] = "--version";
    return check(argv[1], nullptr) || check(argv[1], help) || check(argv[1], version);
}

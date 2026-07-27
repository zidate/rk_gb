#include "AbUpdate.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int AbUpdateApply(const char *package_path, bool reboot_after_success)
{
    static char program[] = "/oem/usr/bin/rk_ota";
    static char misc_arg[] = "--misc=update";
    static char partition_arg[] = "--partition=all";
    static char reboot_arg[] = "--reboot";
    char tar_arg[PATH_MAX + sizeof("--tar_path=")];
    char *argv[6];
    const int length = snprintf(tar_arg, sizeof(tar_arg), "--tar_path=%s", package_path ? package_path : "");
    if (package_path == NULL || package_path[0] == '\0' || length < 0 ||
        static_cast<size_t>(length) >= sizeof(tar_arg))
    {
        return -1;
    }

    argv[0] = program;
    argv[1] = misc_arg;
    argv[2] = tar_arg;
    argv[3] = partition_arg;
    argv[4] = reboot_after_success ? reboot_arg : NULL;
    argv[5] = NULL;

    const pid_t pid = fork();
    if (pid < 0)
    {
        return -1;
    }
    if (pid == 0)
    {
        execv(program, argv);
        _exit(127);
    }

    int status = 0;
    pid_t waited;
    do
    {
        waited = waitpid(pid, &status, 0);
    } while (waited < 0 && errno == EINTR);

    if (waited != pid || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
    {
        return -1;
    }
    return 0;
}

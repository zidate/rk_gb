/* 将已校验的 ota.bin 交给板端 rk_ota，并等待升级结果。 */

#include "AbUpdate.h"
#include "OtaPackage.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef AB_UPDATE_FORK
#define AB_UPDATE_FORK fork
#endif
#ifndef AB_UPDATE_EXECV
#define AB_UPDATE_EXECV execv
#endif
#ifndef AB_UPDATE_WAITPID
#define AB_UPDATE_WAITPID waitpid
#endif
#ifndef AB_UPDATE_EXIT
#define AB_UPDATE_EXIT _exit
#endif

int AbUpdateApply(const char *package_path, bool reboot_after_success)
{
    static char program[] = "/oem/usr/bin/rk_ota";
    static char misc_arg[] = "--misc=update";
    static char partition_arg[] = "--partition=all";
    static char reboot_arg[] = "--reboot";
    char tar_path[PATH_MAX];
    char tar_arg[PATH_MAX + sizeof("--tar_path=")];
    char *argv[6];
    if (OtaPackageCreateTar(package_path, tar_path, sizeof(tar_path)) != 0)
    {
        return -1;
    }
    const int length = snprintf(tar_arg, sizeof(tar_arg), "--tar_path=%s", tar_path);
    if (length < 0 ||
        static_cast<size_t>(length) >= sizeof(tar_arg))
    {
        unlink(tar_path);
        return -1;
    }

    argv[0] = program;
    argv[1] = misc_arg;
    argv[2] = tar_arg;
    argv[3] = partition_arg;
    argv[4] = reboot_after_success ? reboot_arg : NULL;
    argv[5] = NULL;

    const pid_t pid = AB_UPDATE_FORK();
    if (pid < 0)
    {
        unlink(tar_path);
        return -1;
    }
    if (pid == 0)
    {
        AB_UPDATE_EXECV(program, argv);
        AB_UPDATE_EXIT(127);
    }

    int status = 0;
    pid_t waited;
    do
    {
        waited = AB_UPDATE_WAITPID(pid, &status, 0);
    } while (waited < 0 && errno == EINTR);

    unlink(tar_path);
    if (waited != pid || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
    {
        return -1;
    }
    return 0;
}

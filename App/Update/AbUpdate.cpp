/* 将已校验的 ota.bin 交给板端 rk_ota，并等待升级结果。 */

#include "AbUpdate.h"
#include "OtaPackage.h"

#include <errno.h>
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

namespace {

const char kImageDirectory[] = "/tmp";
volatile int g_update_running = 0;

void ReleaseUpgradeLock()
{
    __sync_lock_release(&g_update_running);
}

}  // 匿名命名空间

int AbUpdateApply(const char *package_path, bool reboot_after_success)
{
    static char program[] = "/oem/usr/bin/rk_ota";
    static char misc_arg[] = "--misc=update";
    static char save_arg[] = "--save_dir=/tmp";
    static char partition_arg[] = "--partition=all";
    static char reboot_arg[] = "--reboot";
    char *argv[6];
    if (__sync_lock_test_and_set(&g_update_running, 1) != 0)
    {
        return -1;
    }
    OtaPackageRemoveImages(kImageDirectory);
    if (OtaPackageExtractImages(package_path, kImageDirectory) != 0)
    {
        ReleaseUpgradeLock();
        return -1;
    }

    argv[0] = program;
    argv[1] = misc_arg;
    argv[2] = save_arg;
    argv[3] = partition_arg;
    argv[4] = reboot_after_success ? reboot_arg : NULL;
    argv[5] = NULL;

    const pid_t pid = AB_UPDATE_FORK();
    if (pid < 0)
    {
        OtaPackageRemoveImages(kImageDirectory);
        ReleaseUpgradeLock();
        return -1;
    }
    if (pid == 0)
    {
        AB_UPDATE_EXECV(program, argv);
        OtaPackageRemoveImages(kImageDirectory);
        ReleaseUpgradeLock();
        AB_UPDATE_EXIT(127);
    }

    int status = 0;
    pid_t waited;
    do
    {
        waited = AB_UPDATE_WAITPID(pid, &status, 0);
    } while (waited < 0 && errno == EINTR);

    OtaPackageRemoveImages(kImageDirectory);
    ReleaseUpgradeLock();
    if (waited != pid || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
    {
        return -1;
    }
    return 0;
}

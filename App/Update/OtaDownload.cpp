/* OTA 下载边界：不经过 shell，校验 MD5 后才发布正式文件。 */

#include "OtaDownload.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern "C" {
#include <libavutil/md5.h>
#include <libavutil/mem.h>
}

namespace {

bool NormalizeMd5(const char *source, char output[33])
{
    size_t count = 0;
    if (source == NULL)
        return false;
    for (size_t i = 0; i < 128U; ++i) {
        if (source[i] == '\0') {
            output[count] = '\0';
            return count == 32U;
        }
        const unsigned char value = static_cast<unsigned char>(source[i]);
        if (isspace(value))
            continue;
        if (!isxdigit(value) || count >= 32U)
            return false;
        output[count++] = static_cast<char>(tolower(value));
    }
    return false;
}

int WaitForChild(pid_t pid)
{
    int status = 0;
    pid_t waited;
    do {
        waited = waitpid(pid, &status, 0);
    } while (waited < 0 && errno == EINTR);
    return waited == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}

int DownloadByCurl(const char *url, const char *output_path)
{
    const pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        char *const arguments[] = {
            const_cast<char *>("curl"),
            const_cast<char *>("--fail"),
            const_cast<char *>("--location"),
            const_cast<char *>("--silent"),
            const_cast<char *>("--show-error"),
            const_cast<char *>("--connect-timeout"),
            const_cast<char *>("15"),
            const_cast<char *>("--max-time"),
            const_cast<char *>("600"),
            const_cast<char *>("--output"),
            const_cast<char *>(output_path),
            const_cast<char *>("--"),
            const_cast<char *>(url),
            NULL,
        };
        execvp(arguments[0], arguments);
        _exit(127);
    }
    return WaitForChild(pid);
}

int CalculateFileMd5(const char *path, char output[33], uint32_t *file_size)
{
    unsigned char buffer[16U * 1024U];
    unsigned char digest[16];
    struct stat st;
    int fd = -1;
    struct AVMD5 *context = NULL;
    int result = -1;

    fd = open(path, O_RDONLY
#ifdef O_CLOEXEC
              | O_CLOEXEC
#endif
#ifdef O_NOFOLLOW
              | O_NOFOLLOW
#endif
    );
    if (fd < 0 || fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) ||
        st.st_size <= 0 || static_cast<uint64_t>(st.st_size) > UINT32_MAX)
        goto done;
    context = av_md5_alloc();
    if (context == NULL)
        goto done;
    av_md5_init(context);
    while (true) {
        const ssize_t count = read(fd, buffer, sizeof(buffer));
        if (count > 0) {
            av_md5_update(context, buffer, static_cast<int>(count));
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0)
            goto done;
        break;
    }
    av_md5_final(context, digest);
    if (fsync(fd) != 0)
        goto done;
    for (size_t i = 0; i < sizeof(digest); ++i)
        snprintf(output + i * 2U, 3U, "%02x", digest[i]);
    output[32] = '\0';
    *file_size = static_cast<uint32_t>(st.st_size);
    result = 0;

done:
    av_free(context);
    if (fd >= 0)
        close(fd);
    return result;
}

}  // 匿名命名空间

int OtaDownloadFile(const char *url,
                    const char *temporary_path,
                    const char *final_path,
                    const char *expected_md5,
                    uint32_t *file_size)
{
    char normalized_expected[33];
    char actual_md5[33];
    uint32_t downloaded_size = 0;
    if (url == NULL || url[0] == '\0' || temporary_path == NULL ||
        temporary_path[0] == '\0' || final_path == NULL || final_path[0] == '\0' ||
        file_size == NULL || !NormalizeMd5(expected_md5, normalized_expected))
        return -1;

    *file_size = 0;
    unlink(temporary_path);
    unlink(final_path);
    if (DownloadByCurl(url, temporary_path) != 0 ||
        CalculateFileMd5(temporary_path, actual_md5, &downloaded_size) != 0 ||
        strcmp(actual_md5, normalized_expected) != 0 ||
        rename(temporary_path, final_path) != 0) {
        unlink(temporary_path);
        unlink(final_path);
        return -1;
    }
    *file_size = downloaded_size;
    return 0;
}

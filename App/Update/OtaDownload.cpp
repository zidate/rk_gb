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
#include <unistd.h>

#include <curl/curl.h>

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

bool EnsureCurlInitialized()
{
    static const CURLcode result = curl_global_init(CURL_GLOBAL_DEFAULT);
    return result == CURLE_OK;
}

int DownloadByCurl(const char *url, const char *output_path)
{
    FILE *output = NULL;
    CURL *handle = NULL;
    CURLcode result = CURLE_FAILED_INIT;
    char error_buffer[CURL_ERROR_SIZE] = {0};

    if (!EnsureCurlInitialized())
        return -1;
    output = fopen(output_path, "wb");
    handle = curl_easy_init();
    if (output == NULL || handle == NULL)
        goto done;

    if (curl_easy_setopt(handle, CURLOPT_URL, url) != CURLE_OK ||
        curl_easy_setopt(handle, CURLOPT_WRITEDATA, output) != CURLE_OK ||
        curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, error_buffer) != CURLE_OK ||
        curl_easy_setopt(handle, CURLOPT_FAILONERROR, 1L) != CURLE_OK ||
        curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L) != CURLE_OK ||
        curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 15L) != CURLE_OK ||
        curl_easy_setopt(handle, CURLOPT_TIMEOUT, 600L) != CURLE_OK ||
        curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L) != CURLE_OK ||
        curl_easy_setopt(handle, CURLOPT_PROTOCOLS_STR, "http,https") != CURLE_OK ||
        curl_easy_setopt(handle, CURLOPT_REDIR_PROTOCOLS_STR, "http,https") != CURLE_OK)
        goto done;

    result = curl_easy_perform(handle);
    if (result != CURLE_OK)
        fprintf(stderr, "[OtaDownload] download failed: %s\n",
                error_buffer[0] != '\0' ? error_buffer : curl_easy_strerror(result));

done:
    if (handle != NULL)
        curl_easy_cleanup(handle);
    if (output != NULL && (fflush(output) != 0 || fsync(fileno(output)) != 0))
        result = CURLE_WRITE_ERROR;
    if (output != NULL && fclose(output) != 0)
        result = CURLE_WRITE_ERROR;
    return result == CURLE_OK ? 0 : -1;
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

/* 校验 RV1106 OTA 容器，并事务性解析固定的 boot/rootfs/oem 镜像。 */

#include "OtaPackage.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace {

const size_t kPackageHeaderSize = 32;
const size_t kImageHeaderSize = 12;
const uint32_t kPackageMagic = 0xABCD1234U;
const uint32_t kExpectedImageMask = 0x7U;
const uint32_t kMaxPackagePayloadSize = 0x2E00024U;
const size_t kIoBufferSize = 16U * 1024U;

struct ExpectedImage {
    uint32_t type;
    uint32_t start;
    uint32_t max_size;
    const char *name;
};

const ExpectedImage kExpectedImages[] = {
    {4U, 0x0240000U, 0x0400000U, "boot.img"},
    {5U, 0x0A40000U, 0x0A00000U, "rootfs.img"},
    {6U, 0x1E40000U, 0x2000000U, "oem.img"},
};

const size_t kExpectedImageCount = sizeof(kExpectedImages) / sizeof(kExpectedImages[0]);

struct ImageEntry {
    off_t offset;
    uint32_t size;
    const char *name;
};

struct CrcTable {
    uint32_t values[256];

    CrcTable()
    {
        for (uint32_t i = 0; i < 256U; ++i) {
            uint32_t value = i;
            for (int bit = 0; bit < 8; ++bit)
                value = (value >> 1) ^ ((value & 1U) ? 0xEDB88320U : 0U);
            values[i] = value;
        }
    }
};

uint32_t UpdateCrc32(uint32_t crc, const unsigned char *data, size_t size)
{
    static const CrcTable table;
    crc = ~crc;
    for (size_t i = 0; i < size; ++i)
        crc = (crc >> 8) ^ table.values[(crc ^ data[i]) & 0xffU];
    return ~crc;
}

uint32_t ReadBe32(const unsigned char *data)
{
    return (static_cast<uint32_t>(data[0]) << 24) |
           (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) |
           static_cast<uint32_t>(data[3]);
}

bool ReadExact(int fd, void *buffer, size_t size)
{
    unsigned char *target = static_cast<unsigned char *>(buffer);
    size_t completed = 0;
    while (completed < size) {
        const ssize_t count = read(fd, target + completed, size - completed);
        if (count > 0) {
            completed += static_cast<size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        return false;
    }
    return true;
}

bool WriteExact(int fd, const void *buffer, size_t size)
{
    const unsigned char *source = static_cast<const unsigned char *>(buffer);
    size_t completed = 0;
    while (completed < size) {
        const ssize_t count = write(fd, source + completed, size - completed);
        if (count > 0) {
            completed += static_cast<size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        return false;
    }
    return true;
}

int ExpectedImageIndex(uint32_t type)
{
    for (size_t i = 0; i < kExpectedImageCount; ++i) {
        if (kExpectedImages[i].type == type)
            return static_cast<int>(i);
    }
    return -1;
}

bool ValidatePlatform(const unsigned char *header)
{
    static const char platform[] = "rv1106";
    if (memcmp(header, platform, sizeof(platform)) != 0)
        return false;
    for (size_t i = sizeof(platform); i < 20U; ++i) {
        if (header[i] != 0U)
            return false;
    }
    return true;
}

bool ValidatePayloadCrc(int fd, uint32_t package_size, uint32_t expected_crc)
{
    unsigned char buffer[kIoBufferSize];
    uint32_t remaining = package_size;
    uint32_t crc = 0;
    if (lseek(fd, static_cast<off_t>(kPackageHeaderSize), SEEK_SET) < 0)
        return false;
    while (remaining > 0U) {
        size_t chunk = sizeof(buffer);
        if (chunk > remaining)
            chunk = remaining;
        if (!ReadExact(fd, buffer, chunk))
            return false;
        crc = UpdateCrc32(crc, buffer, chunk);
        remaining -= static_cast<uint32_t>(chunk);
    }
    return crc == expected_crc;
}

bool ParseImages(int fd, uint32_t package_size, ImageEntry *entries)
{
    uint32_t remaining = package_size;
    uint32_t seen = 0;
    if (lseek(fd, static_cast<off_t>(kPackageHeaderSize), SEEK_SET) < 0)
        return false;
    while (remaining > 0U) {
        unsigned char header[kImageHeaderSize];
        if (remaining < kImageHeaderSize || !ReadExact(fd, header, sizeof(header)))
            return false;
        const uint32_t type = ReadBe32(header);
        const uint32_t image_size = ReadBe32(header + 4);
        const uint32_t start = ReadBe32(header + 8);
        const int index = ExpectedImageIndex(type);
        if (index < 0 || (seen & (1U << index)) != 0U || image_size == 0U ||
            (image_size & 3U) != 0U || image_size > remaining - kImageHeaderSize ||
            start != kExpectedImages[index].start || image_size > kExpectedImages[index].max_size)
            return false;
        entries[index].offset = lseek(fd, 0, SEEK_CUR);
        entries[index].size = image_size;
        entries[index].name = kExpectedImages[index].name;
        if (entries[index].offset < 0 || lseek(fd, static_cast<off_t>(image_size), SEEK_CUR) < 0)
            return false;
        seen |= 1U << index;
        remaining -= static_cast<uint32_t>(kImageHeaderSize) + image_size;
    }
    return seen == kExpectedImageMask;
}

bool JoinPath(const char *directory, const char *name, char *path, size_t path_size)
{
    const int length = snprintf(path, path_size, "%s/%s", directory, name);
    return length > 0 && static_cast<size_t>(length) < path_size;
}

bool BuildTempPath(const char *directory, const char *name, char *path, size_t path_size)
{
    const int length = snprintf(path, path_size, "%s/.%s.XXXXXX", directory, name);
    return length > 0 && static_cast<size_t>(length) < path_size;
}

bool CopyImage(int package_fd, int output_fd, const ImageEntry &entry)
{
    unsigned char buffer[kIoBufferSize];
    uint32_t remaining = entry.size;
    if (lseek(package_fd, entry.offset, SEEK_SET) < 0)
        return false;
    while (remaining > 0U) {
        size_t chunk = sizeof(buffer);
        if (chunk > remaining)
            chunk = remaining;
        if (!ReadExact(package_fd, buffer, chunk) || !WriteExact(output_fd, buffer, chunk))
            return false;
        remaining -= static_cast<uint32_t>(chunk);
    }
    return fchmod(output_fd, 0644) == 0 && fsync(output_fd) == 0;
}

int OpenPackage(const char *package_path)
{
    int flags = O_RDONLY;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    return open(package_path, flags);
}

void RemovePaths(char paths[][PATH_MAX], size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        if (paths[i][0] != '\0')
            unlink(paths[i]);
    }
}

}  // 匿名命名空间

void OtaPackageRemoveImages(const char *output_dir)
{
    if (output_dir == NULL || output_dir[0] == '\0')
        return;
    for (size_t i = 0; i < kExpectedImageCount; ++i) {
        char path[PATH_MAX];
        if (JoinPath(output_dir, kExpectedImages[i].name, path, sizeof(path)))
            unlink(path);
    }
}

int OtaPackageExtractImages(const char *package_path, const char *output_dir)
{
    unsigned char header[kPackageHeaderSize];
    ImageEntry entries[kExpectedImageCount];
    char temp_paths[kExpectedImageCount][PATH_MAX];
    char final_paths[kExpectedImageCount][PATH_MAX];
    struct stat package_stat;
    struct stat directory_stat;
    int package_fd = -1;
    int directory_fd = -1;
    int result = -1;
    bool output_directory_valid = false;
    uint32_t magic = 0;
    uint32_t expected_crc = 0;
    uint32_t package_size = 0;

    memset(entries, 0, sizeof(entries));
    memset(temp_paths, 0, sizeof(temp_paths));
    memset(final_paths, 0, sizeof(final_paths));
    if (package_path == NULL || package_path[0] == '\0' ||
        output_dir == NULL || output_dir[0] == '\0')
        return -1;

    directory_fd = open(output_dir, O_RDONLY
#ifdef O_DIRECTORY
                        | O_DIRECTORY
#endif
#ifdef O_CLOEXEC
                        | O_CLOEXEC
#endif
#ifdef O_NOFOLLOW
                        | O_NOFOLLOW
#endif
    );
    if (directory_fd < 0 || fstat(directory_fd, &directory_stat) != 0 ||
        !S_ISDIR(directory_stat.st_mode)) {
        fprintf(stderr, "OtaPackage: invalid output directory: %s\n", output_dir);
        goto done;
    }
    output_directory_valid = true;
    for (size_t i = 0; i < kExpectedImageCount; ++i) {
        if (!JoinPath(output_dir, kExpectedImages[i].name,
                      final_paths[i], sizeof(final_paths[i])))
            goto done;
    }
    OtaPackageRemoveImages(output_dir);

    package_fd = OpenPackage(package_path);
    if (package_fd < 0 || fstat(package_fd, &package_stat) != 0 ||
        !S_ISREG(package_stat.st_mode) ||
        package_stat.st_size < static_cast<off_t>(kPackageHeaderSize) ||
        lseek(package_fd, 0, SEEK_SET) < 0 || !ReadExact(package_fd, header, sizeof(header))) {
        fprintf(stderr, "OtaPackage: cannot read a regular OTA package: %s\n", package_path);
        goto done;
    }

    magic = ReadBe32(header + 20);
    expected_crc = ReadBe32(header + 24);
    package_size = ReadBe32(header + 28);
    if (!ValidatePlatform(header) || magic != kPackageMagic || package_size == 0U ||
        package_size > kMaxPackagePayloadSize ||
        static_cast<uint64_t>(package_stat.st_size) != kPackageHeaderSize + package_size) {
        fprintf(stderr, "OtaPackage: invalid platform, magic, or package size: %s\n", package_path);
        goto done;
    }
    if (!ValidatePayloadCrc(package_fd, package_size, expected_crc)) {
        fprintf(stderr, "OtaPackage: payload CRC32 mismatch: %s\n", package_path);
        goto done;
    }
    if (!ParseImages(package_fd, package_size, entries)) {
        fprintf(stderr, "OtaPackage: invalid boot/rootfs/oem image table: %s\n", package_path);
        goto done;
    }

    for (size_t i = 0; i < kExpectedImageCount; ++i) {
        if (!BuildTempPath(output_dir, entries[i].name, temp_paths[i], sizeof(temp_paths[i])))
            goto done;
        const int output_fd = mkstemp(temp_paths[i]);
        if (output_fd < 0) {
            fprintf(stderr, "OtaPackage: cannot create temporary %s\n", entries[i].name);
            goto done;
        }
        const bool copied = CopyImage(package_fd, output_fd, entries[i]);
        const bool closed = close(output_fd) == 0;
        if (!copied || !closed) {
            fprintf(stderr, "OtaPackage: cannot extract %s\n", entries[i].name);
            goto done;
        }
    }

    for (size_t i = 0; i < kExpectedImageCount; ++i) {
        if (rename(temp_paths[i], final_paths[i]) != 0) {
            fprintf(stderr, "OtaPackage: cannot publish %s\n", entries[i].name);
            OtaPackageRemoveImages(output_dir);
            goto done;
        }
        temp_paths[i][0] = '\0';
    }
    if (fsync(directory_fd) != 0) {
        OtaPackageRemoveImages(output_dir);
        goto done;
    }
    result = 0;

done:
    RemovePaths(temp_paths, kExpectedImageCount);
    if (result != 0 && output_directory_valid)
        OtaPackageRemoveImages(output_dir);
    if (package_fd >= 0)
        close(package_fd);
    if (directory_fd >= 0)
        close(directory_fd);
    return result;
}

/* 校验 RV1106 OTA 容器，并把固定镜像成员转换成临时 USTAR。 */

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
    for (size_t i = 0; i < sizeof(kExpectedImages) / sizeof(kExpectedImages[0]); ++i) {
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

bool WriteOctal(char *field, size_t field_size, uint64_t value)
{
    const int digits = static_cast<int>(field_size - 1U);
    const int length = snprintf(field, field_size, "%0*llo", digits,
                                static_cast<unsigned long long>(value));
    return length == digits;
}

bool WriteTarHeader(int output_fd, const ImageEntry &entry)
{
    unsigned char header[512];
    memset(header, 0, sizeof(header));
    if (strlen(entry.name) >= 100U)
        return false;
    memcpy(header, entry.name, strlen(entry.name));
    if (!WriteOctal(reinterpret_cast<char *>(header + 100), 8, 0644U) ||
        !WriteOctal(reinterpret_cast<char *>(header + 108), 8, 0U) ||
        !WriteOctal(reinterpret_cast<char *>(header + 116), 8, 0U) ||
        !WriteOctal(reinterpret_cast<char *>(header + 124), 12, entry.size) ||
        !WriteOctal(reinterpret_cast<char *>(header + 136), 12, 0U))
        return false;
    memset(header + 148, ' ', 8);
    header[156] = '0';
    memcpy(header + 257, "ustar", 5);
    memcpy(header + 263, "00", 2);
    unsigned int checksum = 0;
    for (size_t i = 0; i < sizeof(header); ++i)
        checksum += header[i];
    const int checksum_length = snprintf(reinterpret_cast<char *>(header + 148), 8,
                                         "%06o", checksum);
    if (checksum_length != 6)
        return false;
    header[154] = '\0';
    header[155] = ' ';
    return WriteExact(output_fd, header, sizeof(header));
}

bool CopyTarImage(int package_fd, int output_fd, const ImageEntry &entry)
{
    unsigned char buffer[kIoBufferSize];
    uint32_t remaining = entry.size;
    if (lseek(package_fd, entry.offset, SEEK_SET) < 0 || !WriteTarHeader(output_fd, entry))
        return false;
    while (remaining > 0U) {
        size_t chunk = sizeof(buffer);
        if (chunk > remaining)
            chunk = remaining;
        if (!ReadExact(package_fd, buffer, chunk) || !WriteExact(output_fd, buffer, chunk))
            return false;
        remaining -= static_cast<uint32_t>(chunk);
    }
    const size_t padding = (512U - (entry.size % 512U)) % 512U;
    if (padding != 0U) {
        memset(buffer, 0, padding);
        if (!WriteExact(output_fd, buffer, padding))
            return false;
    }
    return true;
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

}  // 匿名命名空间

int OtaPackageCreateTar(const char *package_path, char *tar_path, size_t tar_path_size)
{
    static const char temp_template[] = "/tmp/rk_ota_payload_XXXXXX";
    unsigned char header[kPackageHeaderSize];
    ImageEntry entries[sizeof(kExpectedImages) / sizeof(kExpectedImages[0])];
    struct stat st;
    char temp_path[sizeof(temp_template)];
    int package_fd = -1;
    int output_fd = -1;
    int result = -1;
    uint32_t magic = 0;
    uint32_t expected_crc = 0;
    uint32_t package_size = 0;

    if (package_path == NULL || package_path[0] == '\0' || tar_path == NULL ||
        tar_path_size < sizeof(temp_template))
        return -1;
    memset(entries, 0, sizeof(entries));
    temp_path[0] = '\0';
    package_fd = OpenPackage(package_path);
    if (package_fd < 0 || fstat(package_fd, &st) != 0 || !S_ISREG(st.st_mode) ||
        st.st_size < static_cast<off_t>(kPackageHeaderSize) ||
        lseek(package_fd, 0, SEEK_SET) < 0 || !ReadExact(package_fd, header, sizeof(header))) {
        fprintf(stderr, "OtaPackage: cannot read a regular OTA package: %s\n", package_path);
        goto done;
    }
    magic = ReadBe32(header + 20);
    expected_crc = ReadBe32(header + 24);
    package_size = ReadBe32(header + 28);
    if (!ValidatePlatform(header) || magic != kPackageMagic || package_size == 0U ||
        package_size > kMaxPackagePayloadSize ||
        static_cast<uint64_t>(st.st_size) != kPackageHeaderSize + package_size) {
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

    memcpy(temp_path, temp_template, sizeof(temp_template));
    output_fd = mkstemp(temp_path);
    if (output_fd < 0) {
        fprintf(stderr, "OtaPackage: cannot create temporary rk_ota archive\n");
        goto done;
    }
    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); ++i) {
        if (!CopyTarImage(package_fd, output_fd, entries[i])) {
            fprintf(stderr, "OtaPackage: cannot write temporary member %s\n", entries[i].name);
            goto done;
        }
    }
    {
        unsigned char trailer[1024];
        memset(trailer, 0, sizeof(trailer));
        if (!WriteExact(output_fd, trailer, sizeof(trailer)) || fsync(output_fd) != 0)
            goto done;
    }
    if (close(output_fd) != 0) {
        output_fd = -1;
        goto done;
    }
    output_fd = -1;
    memcpy(tar_path, temp_path, sizeof(temp_template));
    result = 0;

done:
    if (package_fd >= 0)
        close(package_fd);
    if (output_fd >= 0)
        close(output_fd);
    if (result != 0 && temp_path[0] == '/')
        unlink(temp_path);
    return result;
}

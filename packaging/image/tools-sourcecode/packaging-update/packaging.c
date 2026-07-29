/* 生成设备端升级程序使用的 RV1106 OTA 容器。 */

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PACKAGE_HEADER_SIZE 32U
#define IMAGE_HEADER_SIZE 12U
#define PACKAGE_MAGIC 0xABCD1234U
#define PLATFORM_NAME "rv1106"
#define COPY_BUFFER_SIZE (64U * 1024U)

typedef struct {
    const char *section;
    const char *expected_file;
    uint32_t image_type;
    uint32_t start;
    uint32_t partition_size;
    char file[256];
    unsigned int fields;
} ImageSpec;

static uint32_t g_crc_table[256];
static int g_crc_table_ready;

static void crc32_make_table(void)
{
    uint32_t i;
    for (i = 0; i < 256U; ++i) {
        uint32_t value = i;
        int bit;
        for (bit = 0; bit < 8; ++bit)
            value = (value >> 1) ^ ((value & 1U) ? 0xEDB88320U : 0U);
        g_crc_table[i] = value;
    }
    g_crc_table_ready = 1;
}

static uint32_t crc32_update(uint32_t crc, const unsigned char *data, size_t size)
{
    size_t i;
    if (!g_crc_table_ready)
        crc32_make_table();
    crc = ~crc;
    for (i = 0; i < size; ++i)
        crc = (crc >> 8) ^ g_crc_table[(crc ^ data[i]) & 0xffU];
    return ~crc;
}

static void put_be32(unsigned char *target, uint32_t value)
{
    target[0] = (unsigned char)(value >> 24);
    target[1] = (unsigned char)(value >> 16);
    target[2] = (unsigned char)(value >> 8);
    target[3] = (unsigned char)value;
}

static char *trim(char *text)
{
    char *end;
    while (*text != '\0' && isspace((unsigned char)*text))
        ++text;
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1]))
        --end;
    *end = '\0';
    return text;
}

static int parse_u32(const char *text, uint32_t *value)
{
    char *end = NULL;
    unsigned long long parsed;
    errno = 0;
    parsed = strtoull(text, &end, 0);
    if (errno != 0 || end == text || *trim(end) != '\0' || parsed > UINT32_MAX)
        return -1;
    *value = (uint32_t)parsed;
    return 0;
}

static int find_image(const ImageSpec *images, const char *section)
{
    int i;
    for (i = 0; i < 3; ++i) {
        if (strcmp(images[i].section, section) == 0)
            return i;
    }
    return -1;
}

static int parse_ini(const char *path, ImageSpec *images)
{
    FILE *fp = fopen(path, "r");
    char line[512];
    char section[32] = "";
    uint32_t flash_size = 0;
    uint32_t sector_size = 0;
    unsigned int global_fields = 0;
    unsigned int line_number = 0;
    int result = -1;

    if (fp == NULL) {
        fprintf(stderr, "packaging-update: cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        char *key;
        char *value;
        char *separator;
        char *text;
        int image_index;
        ++line_number;
        if (strchr(line, '\n') == NULL && !feof(fp)) {
            fprintf(stderr, "packaging-update: line %u is too long\n", line_number);
            goto done;
        }
        text = trim(line);
        if (*text == '\0' || *text == '#' || *text == ';')
            continue;
        if (*text == '[') {
            char *close = strchr(text + 1, ']');
            if (close == NULL || *trim(close + 1) != '\0') {
                fprintf(stderr, "packaging-update: invalid section at line %u\n", line_number);
                goto done;
            }
            *close = '\0';
            text = trim(text + 1);
            if (strcmp(text, "global") != 0 && find_image(images, text) < 0) {
                fprintf(stderr, "packaging-update: unsupported section [%s]\n", text);
                goto done;
            }
            if (strlen(text) >= sizeof(section)) {
                fprintf(stderr, "packaging-update: section name is too long\n");
                goto done;
            }
            strcpy(section, text);
            continue;
        }
        separator = strchr(text, '=');
        if (separator == NULL || section[0] == '\0') {
            fprintf(stderr, "packaging-update: invalid entry at line %u\n", line_number);
            goto done;
        }
        *separator = '\0';
        key = trim(text);
        value = trim(separator + 1);
        if (strcmp(section, "global") == 0) {
            if (strcmp(key, "flash_size") == 0) {
                if ((global_fields & 1U) || parse_u32(value, &flash_size) != 0)
                    goto invalid_value;
                global_fields |= 1U;
            } else if (strcmp(key, "sector_size") == 0) {
                if ((global_fields & 2U) || parse_u32(value, &sector_size) != 0)
                    goto invalid_value;
                global_fields |= 2U;
            }
            continue;
        }
        image_index = find_image(images, section);
        if (image_index < 0)
            goto invalid_value;
        if (strcmp(key, "start") == 0) {
            if ((images[image_index].fields & 1U) ||
                parse_u32(value, &images[image_index].start) != 0)
                goto invalid_value;
            images[image_index].fields |= 1U;
        } else if (strcmp(key, "size") == 0) {
            if ((images[image_index].fields & 2U) ||
                parse_u32(value, &images[image_index].partition_size) != 0)
                goto invalid_value;
            images[image_index].fields |= 2U;
        } else if (strcmp(key, "file") == 0) {
            if ((images[image_index].fields & 4U) || strlen(value) >= sizeof(images[image_index].file))
                goto invalid_value;
            strcpy(images[image_index].file, value);
            images[image_index].fields |= 4U;
        }
        continue;

invalid_value:
        fprintf(stderr, "packaging-update: invalid %s.%s at line %u\n", section, key, line_number);
        goto done;
    }

    if (ferror(fp) || global_fields != 3U || flash_size == 0U || sector_size == 0U) {
        fprintf(stderr, "packaging-update: incomplete global configuration\n");
        goto done;
    }
    {
        int i;
        for (i = 0; i < 3; ++i) {
            uint64_t end;
            if (images[i].fields != 7U || images[i].partition_size == 0U ||
                strcmp(images[i].file, images[i].expected_file) != 0) {
                fprintf(stderr, "packaging-update: incomplete or unexpected [%s] configuration\n",
                        images[i].section);
                goto done;
            }
            end = (uint64_t)images[i].start + images[i].partition_size;
            if (images[i].start % sector_size != 0U || end > flash_size) {
                fprintf(stderr, "packaging-update: invalid [%s] partition range\n", images[i].section);
                goto done;
            }
        }
    }
    result = 0;

done:
    fclose(fp);
    return result;
}

static int write_payload(FILE *output, const void *data, size_t size, uint32_t *crc)
{
    if (fwrite(data, 1, size, output) != size)
        return -1;
    *crc = crc32_update(*crc, (const unsigned char *)data, size);
    return 0;
}

static int append_image(FILE *output, const ImageSpec *image, uint32_t *crc,
                        uint32_t *package_size)
{
    struct stat st;
    FILE *input = NULL;
    unsigned char header[IMAGE_HEADER_SIZE];
    unsigned char buffer[COPY_BUFFER_SIZE];
    uint64_t aligned_size;
    uint64_t total_size;
    uint64_t copied = 0;
    int result = -1;

    if (stat(image->file, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0) {
        fprintf(stderr, "packaging-update: invalid image %s\n", image->file);
        return -1;
    }
    aligned_size = ((uint64_t)st.st_size + 3U) & ~UINT64_C(3);
    if (aligned_size > image->partition_size || aligned_size > UINT32_MAX) {
        fprintf(stderr, "packaging-update: %s exceeds its partition (size=%llu limit=%u)\n",
                image->file, (unsigned long long)aligned_size, image->partition_size);
        return -1;
    }
    total_size = IMAGE_HEADER_SIZE + aligned_size;
    if (total_size > UINT32_MAX - *package_size) {
        fprintf(stderr, "packaging-update: OTA payload is too large\n");
        return -1;
    }
    input = fopen(image->file, "rb");
    if (input == NULL) {
        fprintf(stderr, "packaging-update: cannot open %s: %s\n", image->file, strerror(errno));
        return -1;
    }

    put_be32(header, image->image_type);
    put_be32(header + 4, (uint32_t)aligned_size);
    put_be32(header + 8, image->start);
    if (write_payload(output, header, sizeof(header), crc) != 0)
        goto done;
    while (copied < (uint64_t)st.st_size) {
        size_t wanted = sizeof(buffer);
        size_t count;
        if ((uint64_t)wanted > (uint64_t)st.st_size - copied)
            wanted = (size_t)((uint64_t)st.st_size - copied);
        count = fread(buffer, 1, wanted, input);
        if (count != wanted || write_payload(output, buffer, count, crc) != 0)
            goto done;
        copied += count;
    }
    if (aligned_size > copied) {
        size_t padding = (size_t)(aligned_size - copied);
        memset(buffer, 0xff, padding);
        if (write_payload(output, buffer, padding, crc) != 0)
            goto done;
    }
    *package_size += (uint32_t)total_size;
    result = 0;

done:
    if (fclose(input) != 0)
        result = -1;
    if (result != 0)
        fprintf(stderr, "packaging-update: failed to append %s\n", image->file);
    return result;
}

int main(int argc, char **argv)
{
    ImageSpec images[3] = {
        {"boot", "boot.img", 4U, 0U, 0U, "", 0U},
        {"rootfs", "rootfs.img", 5U, 0U, 0U, "", 0U},
        {"oem", "oem.img", 6U, 0U, 0U, "", 0U},
    };
    unsigned char package_header[PACKAGE_HEADER_SIZE];
    uint32_t package_crc = 0;
    uint32_t package_size = 0;
    FILE *output = NULL;
    int result = EXIT_FAILURE;
    int i;

    if (argc != 3) {
        fprintf(stderr, "Usage: %s upgrade.ini ota.bin\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (parse_ini(argv[1], images) != 0)
        return EXIT_FAILURE;
    output = fopen(argv[2], "wb+");
    if (output == NULL) {
        fprintf(stderr, "packaging-update: cannot create %s: %s\n", argv[2], strerror(errno));
        return EXIT_FAILURE;
    }
    memset(package_header, 0, sizeof(package_header));
    if (fwrite(package_header, 1, sizeof(package_header), output) != sizeof(package_header))
        goto done;
    for (i = 0; i < 3; ++i) {
        if (append_image(output, &images[i], &package_crc, &package_size) != 0)
            goto done;
    }
    memcpy(package_header, PLATFORM_NAME, strlen(PLATFORM_NAME));
    put_be32(package_header + 20, PACKAGE_MAGIC);
    put_be32(package_header + 24, package_crc);
    put_be32(package_header + 28, package_size);
    if (fseek(output, 0, SEEK_SET) != 0 ||
        fwrite(package_header, 1, sizeof(package_header), output) != sizeof(package_header) ||
        fflush(output) != 0)
        goto done;
    result = EXIT_SUCCESS;
    printf("packaging-update: created %s (%u payload bytes, crc32=%08x)\n",
           argv[2], package_size, package_crc);

done:
    if (fclose(output) != 0)
        result = EXIT_FAILURE;
    if (result != EXIT_SUCCESS) {
        unlink(argv[2]);
        fprintf(stderr, "packaging-update: failed to create %s\n", argv[2]);
    }
    return result;
}

#define _FILE_OFFSET_BITS 64

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define FLASH_SIZE 0x8000000ULL
#define FLASH_ALIGNMENT 0x20000ULL
#define COPY_BUFFER_SIZE 0x20000U

static const char *const partition_sections[] = {
    "env", "idblock", "uboot", "boot_a", "boot_b",
    "rootfs_a", "rootfs_b", "oem_a", "oem_b",
    "reserved", "misc", "userdata",
};

struct partition {
    const char *name;
    uint64_t start;
    uint64_t size;
    int has_start;
    int has_size;
    char file[256];
};

struct layout {
    uint64_t flash_size;
    uint64_t sector_size;
    int has_flash_size;
    int has_sector_size;
    struct partition partitions[sizeof(partition_sections) /
                                sizeof(partition_sections[0])];
};

static char *trim(char *text)
{
    char *end;

    while (isspace((unsigned char)*text))
        text++;
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1]))
        *--end = '\0';
    return text;
}

static int parse_u64(const char *text, uint64_t *value)
{
    char *end;
    unsigned long long parsed;

    errno = 0;
    parsed = strtoull(text, &end, 0);
    if (errno || end == text || *trim(end) != '\0')
        return -1;
    *value = (uint64_t)parsed;
    return 0;
}

static int partition_index(const char *name)
{
    size_t i;

    for (i = 0; i < sizeof(partition_sections) / sizeof(partition_sections[0]); i++) {
        if (!strcmp(name, partition_sections[i]))
            return (int)i;
    }
    return -1;
}

static int parse_layout(const char *path, struct layout *layout)
{
    FILE *input;
    char line[1024];
    int section = -2;
    size_t i;

    memset(layout, 0, sizeof(*layout));
    for (i = 0; i < sizeof(partition_sections) / sizeof(partition_sections[0]); i++)
        layout->partitions[i].name = partition_sections[i];

    input = fopen(path, "r");
    if (!input) {
        fprintf(stderr, "open %s failed: %s\n", path, strerror(errno));
        return -1;
    }

    while (fgets(line, sizeof(line), input)) {
        char *key;
        char *value;
        char *separator;
        char *text = trim(line);

        if (!*text || *text == '#' || *text == ';')
            continue;
        if (*text == '[') {
            char *close = strchr(text + 1, ']');
            if (!close || *trim(close + 1) != '\0')
                goto invalid;
            *close = '\0';
            if (!strcmp(text + 1, "global"))
                section = -1;
            else
                section = partition_index(text + 1);
            if (section < -1)
                goto invalid;
            continue;
        }

        separator = strchr(text, '=');
        if (!separator || section == -2)
            goto invalid;
        *separator = '\0';
        key = trim(text);
        value = trim(separator + 1);

        if (section == -1) {
            if (!strcmp(key, "flash_size")) {
                if (layout->has_flash_size || parse_u64(value, &layout->flash_size))
                    goto invalid;
                layout->has_flash_size = 1;
            } else if (!strcmp(key, "sector_size")) {
                if (layout->has_sector_size || parse_u64(value, &layout->sector_size))
                    goto invalid;
                layout->has_sector_size = 1;
            } else {
                goto invalid;
            }
        } else {
            struct partition *part = &layout->partitions[section];
            if (!strcmp(key, "start")) {
                if (part->has_start || parse_u64(value, &part->start))
                    goto invalid;
                part->has_start = 1;
            } else if (!strcmp(key, "size")) {
                if (part->has_size || parse_u64(value, &part->size))
                    goto invalid;
                part->has_size = 1;
            } else if (!strcmp(key, "file")) {
                if (strlen(value) >= sizeof(part->file))
                    goto invalid;
                strcpy(part->file, value);
            } else if (strcmp(key, "type")) {
                goto invalid;
            }
        }
    }

    if (ferror(input))
        goto invalid;
    fclose(input);
    return 0;

invalid:
    fprintf(stderr, "invalid layout near: %s", line);
    fclose(input);
    return -1;
}

static int validate_layout(const struct layout *layout)
{
    uint64_t previous_end = 0;
    size_t i;

    if (!layout->has_flash_size || !layout->has_sector_size ||
        layout->flash_size != FLASH_SIZE || layout->sector_size != FLASH_ALIGNMENT)
        return -1;

    for (i = 0; i < sizeof(partition_sections) / sizeof(partition_sections[0]); i++) {
        const struct partition *part = &layout->partitions[i];
        if (!part->has_start || !part->has_size || !part->size ||
            part->start % FLASH_ALIGNMENT || part->size % FLASH_ALIGNMENT ||
            part->size > FLASH_SIZE || part->start < previous_end ||
            part->start > FLASH_SIZE - part->size)
            return -1;
        previous_end = part->start + part->size;
    }
    return previous_end == FLASH_SIZE ? 0 : -1;
}

static int fill_output(FILE *output, unsigned char *buffer)
{
    uint64_t written = 0;

    memset(buffer, 0xff, COPY_BUFFER_SIZE);
    while (written < FLASH_SIZE) {
        size_t chunk = FLASH_SIZE - written > COPY_BUFFER_SIZE ?
                       COPY_BUFFER_SIZE : (size_t)(FLASH_SIZE - written);
        if (fwrite(buffer, 1, chunk, output) != chunk)
            return -1;
        written += chunk;
    }
    return 0;
}

static int write_partition(FILE *output, const struct partition *part,
                           unsigned char *buffer)
{
    FILE *input;
    struct stat info;
    uint64_t copied = 0;

    if (!part->file[0])
        return 0;
    if (stat(part->file, &info) || info.st_size < 0 ||
        (uint64_t)info.st_size > part->size) {
        fprintf(stderr, "%s exceeds %s or cannot be read\n", part->file, part->name);
        return -1;
    }
    input = fopen(part->file, "rb");
    if (!input)
        return -1;
    if (fseeko(output, (off_t)part->start, SEEK_SET)) {
        fclose(input);
        return -1;
    }
    while (copied < (uint64_t)info.st_size) {
        size_t chunk = (uint64_t)info.st_size - copied > COPY_BUFFER_SIZE ?
                       COPY_BUFFER_SIZE : (size_t)((uint64_t)info.st_size - copied);
        if (fread(buffer, 1, chunk, input) != chunk ||
            fwrite(buffer, 1, chunk, output) != chunk) {
            fclose(input);
            return -1;
        }
        copied += chunk;
    }
    return fclose(input);
}

int main(int argc, char **argv)
{
    struct layout layout;
    unsigned char *buffer = NULL;
    FILE *output = NULL;
    size_t i;
    int result = 1;

    if (argc != 3) {
        fprintf(stderr, "usage: %s <partition.ini> <raw.bin>\n", argv[0]);
        return 2;
    }
    if (parse_layout(argv[1], &layout) || validate_layout(&layout)) {
        fprintf(stderr, "layout validation failed\n");
        return 1;
    }

    buffer = malloc(COPY_BUFFER_SIZE);
    output = fopen(argv[2], "wb+");
    if (!buffer || !output || fill_output(output, buffer))
        goto out;
    for (i = 0; i < sizeof(partition_sections) / sizeof(partition_sections[0]); i++) {
        if (write_partition(output, &layout.partitions[i], buffer))
            goto out;
    }
    if (fflush(output) || fseeko(output, 0, SEEK_END) ||
        (uint64_t)ftello(output) != FLASH_SIZE)
        goto out;
    result = 0;

out:
    if (output && fclose(output))
        result = 1;
    free(buffer);
    if (result)
        remove(argv[2]);
    return result;
}

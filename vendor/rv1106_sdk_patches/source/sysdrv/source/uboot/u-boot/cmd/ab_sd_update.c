// SPDX-License-Identifier: GPL-2.0+
/* Validate RV1106 A/B update images on the SD card before flash access. */

#include <common.h>
#include <command.h>
#include <fs.h>
#include <malloc.h>
#include <mapmem.h>
#include <mtd.h>
#include <part.h>
#include <android_ab.h>
#include <android_avb/avb_ab_flow.h>
#include <android_avb/avb_ops_user.h>
#include <linux/bitops.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/spinand.h>

#define AB_SD_ENV BIT(0)
#define AB_SD_IDBLOCK BIT(1)
#define AB_SD_UBOOT BIT(2)
#define AB_SD_BOOT BIT(3)
#define AB_SD_ROOTFS BIT(4)
#define AB_SD_OEM BIT(5)
#define AB_SD_SLOT_TRIO (AB_SD_BOOT | AB_SD_ROOTFS | AB_SD_OEM)
#define AB_SD_SLOT_SWITCH_TRIGGER AB_SD_OEM
#define AB_SD_KIB(n) ((loff_t)(n) * 1024)
#define AB_SD_MIB(n) ((loff_t)(n) * 1024 * 1024)
#define AB_SD_ENV_OFFSET 0
#define AB_SD_IDBLOCK_OFFSET AB_SD_KIB(256)
#define AB_SD_UBOOT_OFFSET (AB_SD_IDBLOCK_OFFSET + AB_SD_MIB(1))

enum ab_sd_image_index {
	AB_SD_IMAGE_ENV,
	AB_SD_IMAGE_IDBLOCK,
	AB_SD_IMAGE_UBOOT,
	AB_SD_IMAGE_BOOT,
	AB_SD_IMAGE_ROOTFS,
	AB_SD_IMAGE_OEM,
};

struct ab_sd_image {
	const char *filename;
	const char *partition;
	u32 present_bit;
	loff_t max_size;
	loff_t raw_offset;
	bool slot_scoped;
};

static const struct ab_sd_image ab_sd_images[] = {
	{ "env.img", "env", AB_SD_ENV, AB_SD_KIB(256), AB_SD_ENV_OFFSET, false },
	{ "idblock.img", "idblock", AB_SD_IDBLOCK, AB_SD_MIB(1),
	  AB_SD_IDBLOCK_OFFSET, false },
	{ "uboot.img", "uboot", AB_SD_UBOOT, AB_SD_MIB(1),
	  AB_SD_UBOOT_OFFSET, false },
	{ "boot.img", "boot", AB_SD_BOOT, AB_SD_MIB(4), 0, true },
	{ "rootfs.img", "rootfs", AB_SD_ROOTFS, AB_SD_MIB(10), 0, true },
	{ "oem.img", "oem", AB_SD_OEM, AB_SD_MIB(32), 0, true },
};

static bool ab_sd_should_switch(u32 present)
{
	return (present & AB_SD_SLOT_SWITCH_TRIGGER) != 0;
}

static void ab_sd_get_target_suffix(char *target_suffix, size_t target_len)
{
	char current_suffix[3] = { 0 };

	if (!ab_get_slot_suffix(current_suffix)) {
		if (!strcmp(current_suffix, "_a")) {
			strlcpy(target_suffix, "_b", target_len);
			return;
		}
		if (!strcmp(current_suffix, "_b")) {
			strlcpy(target_suffix, "_a", target_len);
			return;
		}
	}

	strlcpy(target_suffix, "_a", target_len);
}

static void ab_sd_get_target(const struct ab_sd_image *image,
			     const char *target_suffix, char *target,
			     size_t target_len)
{
	if (image->slot_scoped)
		snprintf(target, target_len, "%s%s", image->partition, target_suffix);
	else
		strlcpy(target, image->partition, target_len);
}

static int ab_sd_good_capacity(struct mtd_info *mtd, loff_t range_start,
			       loff_t range_size, u64 *good_capacity)
{
	u64 capacity = 0;
	loff_t range_end = range_start + range_size;
	loff_t offset;
	int bad;

	for (offset = range_start; offset < range_end;
	     offset += mtd->erasesize) {
		bad = mtd_block_isbad(mtd, offset);
		if (bad < 0) {
			printf("Failed to check %s block 0x%llx (err = %d)\n",
			       mtd->name, (unsigned long long)offset, bad);
			return bad;
		}
		if (bad > 0)
			continue;
		capacity += mtd->erasesize;
	}

	*good_capacity = capacity;
	return 0;
}

static int ab_sd_write_image(const struct ab_sd_image *image,
			     loff_t image_size, const char *target_suffix,
			     u8 *eraseblock_buf, u8 *verify_buf)
{
	char target[sizeof("rootfs_b")];
	const char *mtd_name;
	struct mtd_info *mtd;
	u64 good_capacity;
	u64 required;
	loff_t file_offset = 0;
	loff_t range_start;
	loff_t range_size;
	loff_t range_end;
	loff_t offset;
	int ret;

	ab_sd_get_target(image, target_suffix, target, sizeof(target));
	mtd_name = image->slot_scoped ? target : "spi-nand0";
	mtd = get_mtd_device_nm(mtd_name);
	if (IS_ERR_OR_NULL(mtd)) {
		printf("MTD device %s for %s not found (err = %ld)\n",
		       mtd_name, target,
		       PTR_ERR(mtd));
		return CMD_RET_FAILURE;
	}

	range_start = image->slot_scoped ? 0 : image->raw_offset;
	range_size = image->slot_scoped ? mtd->size : image->max_size;
	if (range_start < 0 || range_start > mtd->size || range_size <= 0 ||
	    range_size > mtd->size - range_start ||
	    !IS_ALIGNED(range_start, mtd->erasesize) ||
	    !IS_ALIGNED(range_size, mtd->erasesize)) {
		printf("Invalid %s range 0x%llx+0x%llx on %s (size 0x%llx)\n",
		       target, (unsigned long long)range_start,
		       (unsigned long long)range_size, mtd_name,
		       (unsigned long long)mtd->size);
		ret = -EINVAL;
		goto out;
	}
	range_end = range_start + range_size;

	ret = ab_sd_good_capacity(mtd, range_start, range_size, &good_capacity);
	if (ret)
		goto out;
	required = ALIGN((u64)image_size, mtd->writesize);
	if (required > good_capacity) {
		printf("Image %s needs %llu good bytes, %s has %llu\n",
		       image->filename, (unsigned long long)required, target,
		       (unsigned long long)good_capacity);
		ret = -ENOSPC;
		goto out;
	}

	for (offset = range_start; offset < range_end;
	     offset += mtd->erasesize) {
		struct erase_info erase = { 0 };
		loff_t chunk_len;
		loff_t actread = 0;
		size_t page_len;
		size_t page_offset;
		int bad;

		bad = mtd_block_isbad(mtd, offset);
		if (bad < 0) {
			printf("Failed to check %s block 0x%llx (err = %d)\n",
			       target, (unsigned long long)offset, bad);
			ret = bad;
			goto out;
		}
		if (bad > 0)
			continue;

		erase.mtd = mtd;
		erase.addr = offset;
		erase.len = mtd->erasesize;
		ret = mtd_erase(mtd, &erase);
		if (ret) {
			printf("Failed to erase %s block 0x%llx (err = %d)\n",
			       target, (unsigned long long)offset, ret);
			goto out;
		}

		if (file_offset >= image_size)
			continue;

		chunk_len = min_t(loff_t, image_size - file_offset,
				      mtd->erasesize);
		page_len = ALIGN(chunk_len, mtd->writesize);
		memset(eraseblock_buf, 0xff, mtd->erasesize);
		/* fs_read() closes the filesystem, so reselect it for each chunk. */
		ret = fs_set_blk_dev("mmc", "1", FS_TYPE_FAT);
		if (ret) {
			printf("Failed to select SD FAT while reading %s (err = %d)\n",
			       image->filename, ret);
			goto out;
		}
		ret = fs_read(image->filename, map_to_sysmem(eraseblock_buf),
			      file_offset, chunk_len, &actread);
		if (ret || actread != chunk_len) {
			printf("Failed to read %s at %lld (%lld/%lld, err = %d)\n",
			       image->filename, (long long)file_offset,
			       (long long)actread, (long long)chunk_len, ret);
			ret = ret ? ret : -EIO;
			goto out;
		}
		if (page_len > chunk_len)
			memset(eraseblock_buf + chunk_len, 0xff, page_len - chunk_len);

		for (page_offset = 0; page_offset < page_len;
		     page_offset += mtd->writesize) {
			struct mtd_oob_ops ops = { 0 };
			size_t retlen = 0;

			ops.mode = MTD_OPS_AUTO_OOB;
			ops.len = mtd->writesize;
			ops.datbuf = eraseblock_buf + page_offset;
			ret = mtd_write_oob(mtd, offset + page_offset, &ops);
			if (ret || ops.retlen != mtd->writesize) {
				printf("Failed to write %s page 0x%llx (err = %d)\n",
				       target,
				       (unsigned long long)(offset + page_offset),
				       ret);
				ret = ret ? ret : -EIO;
				goto out;
			}

			ret = mtd_read(mtd, offset + page_offset, mtd->writesize,
				       &retlen, verify_buf);
			if (ret || retlen != mtd->writesize ||
			    memcmp(verify_buf, eraseblock_buf + page_offset, mtd->writesize)) {
				printf("Failed to verify %s page 0x%llx (err = %d)\n",
				       target,
				       (unsigned long long)(offset + page_offset),
				       ret);
				ret = ret ? ret : -EIO;
				goto out;
			}
		}

		file_offset += chunk_len;
	}

	if (file_offset != image_size) {
		ret = -ENOSPC;
		goto out;
	}

	printf("Wrote and verified %s to %s\n", image->filename, target);

out:
	put_mtd_device(mtd);
	return ret ? CMD_RET_FAILURE : CMD_RET_SUCCESS;
}

static int ab_sd_write_present_slot_images(u32 present,
					   const loff_t *image_sizes,
					   const char *target_suffix,
					   u8 *eraseblock_buf, u8 *verify_buf)
{
	int ret;

	if (present & AB_SD_BOOT) {
		ret = ab_sd_write_image(&ab_sd_images[AB_SD_IMAGE_BOOT],
					 image_sizes[AB_SD_IMAGE_BOOT],
					 target_suffix, eraseblock_buf, verify_buf);
		if (ret)
			return ret;
	}
	if (present & AB_SD_ROOTFS) {
		ret = ab_sd_write_image(&ab_sd_images[AB_SD_IMAGE_ROOTFS],
					 image_sizes[AB_SD_IMAGE_ROOTFS],
					 target_suffix, eraseblock_buf, verify_buf);
		if (ret)
			return ret;
	}
	if (present & AB_SD_OEM) {
		ret = ab_sd_write_image(&ab_sd_images[AB_SD_IMAGE_OEM],
					 image_sizes[AB_SD_IMAGE_OEM],
					 target_suffix, eraseblock_buf, verify_buf);
		if (ret)
			return ret;
	}

	return CMD_RET_SUCCESS;
}

static int ab_sd_prepare_ubi_image(const struct ab_sd_image *image,
				   const char *target_suffix)
{
	char command[sizeof("ubi part rootfs_b")];
	char target[sizeof("rootfs_b")];
	int detach_ret;
	int ret;

	ab_sd_get_target(image, target_suffix, target, sizeof(target));
	snprintf(command, sizeof(command), "ubi part %s", target);
	ret = run_command(command, 0);
	detach_ret = run_command("ubi detach", 0);
	if (ret) {
		printf("Failed to initialize UBI partition %s (err = %d)\n",
		       target, ret);
		return CMD_RET_FAILURE;
	}
	if (detach_ret) {
		printf("Failed to detach UBI partition %s (err = %d)\n",
		       target, detach_ret);
		return CMD_RET_FAILURE;
	}

	printf("Initialized UBI partition %s before restoring protection\n",
	       target);
	return CMD_RET_SUCCESS;
}

static int ab_sd_prepare_present_ubi_images(u32 present,
					    const char *target_suffix)
{
	int ret;

	if (present & AB_SD_ROOTFS) {
		ret = ab_sd_prepare_ubi_image(
			&ab_sd_images[AB_SD_IMAGE_ROOTFS], target_suffix);
		if (ret)
			return ret;
	}
	if (present & AB_SD_OEM) {
		ret = ab_sd_prepare_ubi_image(&ab_sd_images[AB_SD_IMAGE_OEM],
					      target_suffix);
		if (ret)
			return ret;
	}

	return CMD_RET_SUCCESS;
}

static int ab_sd_write_present_fixed_images(u32 present,
					    const loff_t *image_sizes,
					    const char *target_suffix,
					    u8 *eraseblock_buf, u8 *verify_buf)
{
	static const enum ab_sd_image_index write_order[] = {
		AB_SD_IMAGE_ENV,
		AB_SD_IMAGE_UBOOT,
		AB_SD_IMAGE_IDBLOCK,
	};
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(write_order); i++) {
		const struct ab_sd_image *image = &ab_sd_images[write_order[i]];

		if (!(present & image->present_bit))
			continue;
		ret = ab_sd_write_image(image, image_sizes[write_order[i]],
					 target_suffix, eraseblock_buf, verify_buf);
		if (ret)
			return ret;
	}

	return CMD_RET_SUCCESS;
}

static int ab_sd_activate_slot(const char *target_suffix)
{
	AvbOps *ops;
	unsigned int slot_number;
	int ret;

	slot_number = !strcmp(target_suffix, "_b") ? 1 : 0;
	ops = avb_ops_user_new();
	if (!ops) {
		printf("Failed to allocate AVB operations\n");
		return CMD_RET_FAILURE;
	}

	ret = avb_ab_mark_slot_active(ops->ab_ops, slot_number);
	if (ret)
		printf("Failed to activate slot %s (err = %d)\n",
		       target_suffix, ret);
	else
		printf("Activated A/B slot %s for next boot\n", target_suffix);
	avb_ops_user_free(ops);
	return ret ? CMD_RET_FAILURE : CMD_RET_SUCCESS;
}

static int ab_sd_run_transaction(u32 present, const loff_t *image_sizes,
				 const char *target_suffix)
{
	struct mtd_info *master;
	struct spinand_device *spinand;
	u8 *eraseblock_buf = NULL;
	u8 *verify_buf = NULL;
	int relock_ret;
	int ret;

	mtd_probe_devices();
	master = get_mtd_device_nm("spi-nand0");
	if (IS_ERR_OR_NULL(master)) {
		printf("SPI NAND master not found (err = %ld)\n", PTR_ERR(master));
		return CMD_RET_FAILURE;
	}
	spinand = mtd_to_spinand(master);

	eraseblock_buf = memalign(ARCH_DMA_MINALIGN, master->erasesize);
	verify_buf = memalign(ARCH_DMA_MINALIGN, master->writesize);
	if (!eraseblock_buf || !verify_buf) {
		printf("Failed to allocate SPI NAND write buffers\n");
		ret = -ENOMEM;
		goto out;
	}

	ret = spinand_set_block_lock(spinand, BL_ALL_UNLOCKED);
	if (ret) {
		printf("Failed to unlock SPI NAND protection (err = %d)\n", ret);
		goto relock;
	}

	ret = ab_sd_write_present_slot_images(present, image_sizes, target_suffix,
					      eraseblock_buf, verify_buf);
	if (ret)
		goto relock;

	ret = ab_sd_prepare_present_ubi_images(present, target_suffix);
	if (ret)
		goto relock;

	ret = ab_sd_write_present_fixed_images(present, image_sizes,
					       target_suffix, eraseblock_buf,
					       verify_buf);
	if (ret)
		goto relock;

relock:
	relock_ret = spinand_set_block_lock(spinand, BL_LOWER_3_4_LOCKED);
	if (relock_ret) {
		printf("Failed to restore SPI NAND protection (err = %d)\n",
		       relock_ret);
		ret = relock_ret;
	}

out:
	free(eraseblock_buf);
	free(verify_buf);
	put_mtd_device(master);
	if (ret)
		return CMD_RET_FAILURE;

	if (ab_sd_should_switch(present))
		ret = ab_sd_activate_slot(target_suffix);
	return ret;
}

static int do_ab_sd_update(cmd_tbl_t *cmdtp, int flag, int argc,
			   char * const argv[])
{
	struct blk_desc *desc;
	char target_suffix[3];
	loff_t image_sizes[ARRAY_SIZE(ab_sd_images)] = { 0 };
	u32 present = 0;
	unsigned int i;
	int saved_part_type;
	int ret;

	(void)cmdtp;
	(void)flag;
	(void)argc;
	(void)argv;

	desc = blk_get_devnum_by_type(IF_TYPE_MMC, 1);
	if (!desc) {
		printf("Failed to get SD block descriptor\n");
		return CMD_RET_FAILURE;
	}

	/*
	 * The Rockchip block layer may leave the removable card classified as
	 * RKPARM.  The SD update media uses an MBR/FAT partition, as does the
	 * vendor script updater, so force DOS parsing only for this command.
	 */
	saved_part_type = desc->part_type;
	desc->part_type = PART_TYPE_DOS;

	ab_sd_get_target_suffix(target_suffix, sizeof(target_suffix));
	for (i = 0; i < ARRAY_SIZE(ab_sd_images); i++) {
		const struct ab_sd_image *image = &ab_sd_images[i];
		char target[sizeof("rootfs_b")];
		loff_t size = 0;
		int exists;

		/*
		 * fs_exists() closes the filesystem and resets fs_type, so select
		 * the FAT partition again before every filesystem operation.
		 */
		ret = fs_set_blk_dev("mmc", "1", FS_TYPE_FAT);
		if (ret) {
			printf("Failed to select SD FAT before probing %s (err = %d)\n",
			       image->filename, ret);
			ret = CMD_RET_FAILURE;
			goto out_restore;
		}

		exists = fs_exists(image->filename);
		if (exists < 0) {
			printf("Failed to probe %s (err = %d)\n", image->filename,
			       exists);
			ret = CMD_RET_FAILURE;
			goto out_restore;
		}
		if (!exists)
			continue;

		ret = fs_set_blk_dev("mmc", "1", FS_TYPE_FAT);
		if (ret) {
			printf("Failed to select SD FAT before sizing %s (err = %d)\n",
			       image->filename, ret);
			ret = CMD_RET_FAILURE;
			goto out_restore;
		}

		ret = fs_size(image->filename, &size);
		if (ret || size <= 0 || size > image->max_size) {
			printf("Invalid %s size %lld (max %lld, err = %d)\n",
			       image->filename, (long long)size,
			       (long long)image->max_size, ret);
			ret = CMD_RET_FAILURE;
			goto out_restore;
		}

		ab_sd_get_target(image, target_suffix, target, sizeof(target));
		image_sizes[i] = size;
		present |= image->present_bit;
		printf("Validated %s (%lld bytes) for %s\n", image->filename,
		       (long long)size, target);
	}

	printf("SD update %s; target slot %s; present mask 0x%x\n",
	       (present & AB_SD_SLOT_SWITCH_TRIGGER) ? "contains oem" :
	       "without oem",
	       target_suffix, present);
	if (!present)
		ret = CMD_RET_SUCCESS;
	else
		ret = ab_sd_run_transaction(present, image_sizes, target_suffix);

out_restore:
	desc->part_type = saved_part_type;
	return ret;
}

U_BOOT_CMD(ab_sd_update, 1, 1, do_ab_sd_update,
	   "validate RV1106 A/B images on SD before flash access", "");

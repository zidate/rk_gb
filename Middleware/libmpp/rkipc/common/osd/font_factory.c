// Copyright 2021 Rockchip Electronics Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "font_factory.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "font_factory.c"

static pthread_mutex_t g_font_mutex = PTHREAD_MUTEX_INITIALIZER;

FT_Library library_;
FT_Face face_;
FT_GlyphSlot slot_;
FT_Vector pen_;

double font_angle_;
int font_size_;
unsigned int font_color_,bg_font_color_;
unsigned int font_input_color_;
char *font_path_[128];
unsigned int color_index_;
unsigned int trans_index_;

static void draw_argb8888_buffer_with_color(unsigned int *buffer, int buf_w, int buf_h,
                                            unsigned int font_color);

static unsigned int font_color_to_bgra(unsigned int font_color) {
	unsigned int color = 0x000000FF;

	color |= font_color >> 8 & 0x0000FF00;  // R
	color |= font_color << 8 & 0x00FF0000;  // G
	color |= font_color << 24 & 0xFF000000; // B

	return color;
}

int create_font(const char *font_path, int font_size) {
	pthread_mutex_lock(&g_font_mutex);
	FT_Init_FreeType(&library_);
	if (!library_) {
		LOG_ERROR("FT_Init_FreeType fail\n");
		pthread_mutex_unlock(&g_font_mutex);
		return -1;
	}
	FT_New_Face(library_, font_path, 0, &face_);
	if (!face_) {
		LOG_ERROR("please check font_path %s\n", font_path);
		FT_Done_FreeType(library_);
		library_ = NULL;
		pthread_mutex_unlock(&g_font_mutex);
		return -1;
	}

	FT_Set_Pixel_Sizes(face_, font_size, 0);
	font_size_ = font_size;
	memcpy(font_path_, font_path, strlen(font_path));
	slot_ = face_->glyph;
	pthread_mutex_unlock(&g_font_mutex);

	return 0;
}

int destroy_font() {
	pthread_mutex_lock(&g_font_mutex);
	if (face_) {
		FT_Done_Face(face_);
		face_ = NULL;
	}
	if (library_) {
		FT_Done_FreeType(library_);
		library_ = NULL;
	}
	pthread_mutex_unlock(&g_font_mutex);

	return 0;
}

int set_font_size(int font_size) {
	if (!face_) {
		LOG_ERROR("face_ is null\n");
		return -1;
	}
	pthread_mutex_lock(&g_font_mutex);
	FT_Set_Pixel_Sizes(face_, font_size, font_size);
	// FT_Set_Char_Size(face_, font_size * 64, font_size * 64, 0, 0);
	font_size_ = font_size;
	pthread_mutex_unlock(&g_font_mutex);

	return 0;
}

int get_font_size() { return font_size_; }

unsigned int set_font_color(unsigned int font_color) {
	pthread_mutex_lock(&g_font_mutex);
	font_input_color_ = font_color;
	// argb → bgra
	font_color_ = font_color_to_bgra(font_color);

	// 字体画布背景保持全透明，只保留字形像素。
	bg_font_color_ = 0x00000000;

	// LOG_INFO("font_color is %08x, font_color_ is %08x\n", font_color,
	// font_color_);
	// unsigned char cR = font_color_ >> 16 & 0xFF;
	// unsigned char cG = font_color_ >> 8 & 0xFF;
	// unsigned char cB = font_color_ >> 0 & 0xFF;
	// color_index_ = find_color(rgb888_palette_table, PALETTE_TABLE_LEN, cR, cG, cB);
	pthread_mutex_unlock(&g_font_mutex);

	return 0;
}

unsigned int get_font_color() { return font_color_; }

// void DrawYuvMapBuffer(unsigned char *buffer, int buf_w, int buf_h) {
// 	int i, j, p, q;
// 	int left = slot_->bitmap_left;
// 	int top = (face_->size->metrics.ascender >> 6) - slot_->bitmap_top;
// 	int right = left + slot_->bitmap.width;
// 	int bottom = top + slot_->bitmap.rows;

// 	for (j = top, q = 0; j < bottom; j++, q++) {
// 		int offset = j * buf_w;
// 		int bmp_offset = q * slot_->bitmap.width;
// 		for (i = left, p = 0; i < right; i++, p++) {
// 			if (i < 0 || j < 0 || i >= buf_w || j >= buf_h)
// 				continue;
// 			if (slot_->bitmap.buffer[bmp_offset + p]) {
// 				printf("1");
// 				buffer[offset + i] = color_index_;
// 			} else {
// 				printf("2");
// 				buffer[offset + i] = trans_index_;
// 			}
// 			if (!((i + 1) % 16))
// 				printf("\n");
// 		}
// 	}
// }

void draw_argb8888_buffer(unsigned int *buffer, int buf_w, int buf_h) {
	draw_argb8888_buffer_with_color(buffer, buf_w, buf_h, font_color_);
}

static void draw_argb8888_buffer_with_color(unsigned int *buffer, int buf_w, int buf_h,
                                            unsigned int font_color) {
	int i, j, p, q;
	int left = slot_->bitmap_left;
	int top = (face_->size->metrics.ascender >> 6) - slot_->bitmap_top;
	int right = left + slot_->bitmap.width;
	int bottom = top + slot_->bitmap.rows;
	// LOG_DEBUG("top is %d, bottom is %d, left is %d, right is %d\n", top,
	// bottom, left, right);

	for (j = top, q = 0; j < bottom; j++, q++) {
		int offset = j * buf_w;
		int bmp_offset = q * slot_->bitmap.width;
		for (i = left, p = 0; i < right; i++, p++) {
			if (i < 0 || j < 0 || i >= buf_w || j >= buf_h)
				continue;
			// LOG_INFO("bmp_offset + p is %d\n", bmp_offset + p);
			if (slot_->bitmap.buffer[bmp_offset + p]) {
				// printf("0");
				buffer[offset + i] = font_color;
			} else {
				// printf(".");
				buffer[offset + i] = bg_font_color_; //0x00000000 白色全透明
			}
			// if (!((i + 1) % slot_->bitmap.width))
			// printf("\n");
		}
	}
}

void draw_argb8888_wchar(unsigned char *buffer, int buf_w, int buf_h, const wchar_t wch) {
	pthread_mutex_lock(&g_font_mutex);
	if (!face_) {
		LOG_INFO("please check font_path %s\n", *font_path_);
		pthread_mutex_unlock(&g_font_mutex);
		return;
	}
	FT_Error error;
	FT_Set_Transform(face_, NULL, &pen_);
	error = FT_Load_Char(face_, wch, FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP);
	FT_Render_Glyph(slot_, FT_RENDER_MODE_NORMAL); // 8bit per pixel
	if (error) {
		LOG_DEBUG("FT_Load_Char error\n");
		pthread_mutex_unlock(&g_font_mutex);
		return;
	}
	// DrawYuvMapBuffer(buffer, buf_w, buf_h);
	// LOG_DEBUG("slot_->bitmap.rows is %d\n", slot_->bitmap.rows);
	// LOG_DEBUG("slot_->bitmap.width is %d\n", slot_->bitmap.width);
	// LOG_DEBUG("slot_->bitmap.pitch is %d\n", slot_->bitmap.pitch);
	// LOG_DEBUG("slot_->bitmap.pixel_mode is %d\n", slot_->bitmap.pixel_mode);
	// LOG_DEBUG("slot_->bitmap.buffer addr is %p\n", slot_->bitmap.buffer);
	draw_argb8888_buffer((unsigned int *)buffer, buf_w, buf_h);
	pthread_mutex_unlock(&g_font_mutex);
}

void draw_argb8888_text(unsigned char *buffer, int buf_w, int buf_h, const wchar_t *wstr) {
	draw_argb8888_text_with_color_callback(buffer, buf_w, buf_h, wstr, NULL, NULL);
}

void draw_argb8888_text_with_color_callback(unsigned char *buffer, int buf_w, int buf_h,
                                            const wchar_t *wstr, void *userdata,
                                            font_color_judge_cb cb) {
	if (wstr == NULL) {
		LOG_ERROR("wstr is NULL\n");
		return;
	}
	pthread_mutex_lock(&g_font_mutex);
	if (!face_) {
		LOG_INFO("please check font_path %s\n", *font_path_);
		pthread_mutex_unlock(&g_font_mutex);
		return;
	}
	unsigned int BG_COLOR  = bg_font_color_;
	// 绘制透明背景
	unsigned int* imagePtr = (unsigned int*)buffer;
    for (int k = 0; k < buf_w * buf_h; k++ ) {
        *imagePtr++  = BG_COLOR;
    }
	int len = wcslen(wstr);
	// wprintf("wstr is %ls\n", wstr);
	// LOG_DEBUG("len is %d\n", len);
	pen_.x = 0;
	pen_.y = 0;
	for (int i = 0; i < len; i++) {
		FT_Error error;
		unsigned int draw_color = font_color_;

		FT_Set_Transform(face_, NULL, &pen_);
		error = FT_Load_Char(face_, wstr[i], FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP);
		if (error) {
			LOG_DEBUG("FT_Load_Char error\n");
			continue;
		}
		FT_Render_Glyph(slot_, FT_RENDER_MODE_NORMAL); // 8bit per pixel
		if (cb) {
			int left = slot_->bitmap_left;
			int top = (face_->size->metrics.ascender >> 6) - slot_->bitmap_top;
			int width = slot_->advance.x > 0 ? slot_->advance.x / 64 : slot_->bitmap.width;
			int height = slot_->bitmap.rows;
			if (width < (int)slot_->bitmap.width)
				width = slot_->bitmap.width;
			if (height <= 0)
				height = font_size_;
			draw_color = font_color_to_bgra(
				cb(userdata, left, top, width, height, font_input_color_));
		}
		draw_argb8888_buffer_with_color((unsigned int *)buffer, buf_w, buf_h, draw_color);
		pen_.x += slot_->advance.x;
		pen_.y += slot_->advance.y;
	}
	pthread_mutex_unlock(&g_font_mutex);
	// save_argb8888_to_bmp(buffer, buf_w, buf_h);
}

int wstr_get_actual_advance_x(const wchar_t *wstr) {
	if (wstr == NULL) {
		LOG_ERROR("wstr is NULL\n");
		return -1;
	}
	int len = wcslen(wstr);
	pen_.x = 0;
	pen_.y = 0;
	pthread_mutex_lock(&g_font_mutex);
	if (!face_) {
		LOG_INFO("please check font_path %s\n", *font_path_);
		pthread_mutex_unlock(&g_font_mutex);
		return -1;
	}
	FT_Error error;
	for (int i = 0; i < len; i++) {
		FT_Set_Transform(face_, NULL, &pen_);
		error = FT_Load_Char(face_, wstr[i], FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP);
		pen_.x += slot_->advance.x;
		pen_.y += slot_->advance.y;
		// LOG_INFO("slot_->advance.x is %d\n", slot_->advance.x);
	}
	pthread_mutex_unlock(&g_font_mutex);
	return pen_.x / 64; // 26.6 Cartesian pixels, 64 = 2^6
}

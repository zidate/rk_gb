#ifndef __RKIPC_AUDIO_H_
#define __RKIPC_AUDIO_H_
// Copyright 2022 Rockchip Electronics Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
int rkipc_ao_init();
int rkipc_ao_deinit();
int rkipc_ao_write(unsigned char *data, int data_len);
int rkipc_audio_init();
int rkipc_audio_deinit();
// export api
int rk_audio_restart();
int rk_audio_get_bit_rate(int stream_id, int *value);
int rk_audio_set_bit_rate(int stream_id, int value);
int rk_audio_get_sample_rate(int stream_id, int *value);
int rk_audio_set_sample_rate(int stream_id, int value);
int rk_audio_get_volume(int stream_id, int *value);
int rk_audio_set_volume(int stream_id, int value);
int rk_audio_get_enable_vqe(int stream_id, int *value);
int rk_audio_set_enable_vqe(int stream_id, int value);
int rk_audio_get_encode_type(int stream_id, const char **value);
int rk_audio_set_encode_type(int stream_id, const char *value);

int rk_audio_set_ai_volume(int ai);
int rk_audio_set_ao_volume(int ao);


void rk_audio_set_mic_enable(int en);
#if 0
//aac
int rk_audio_aac_encode_deinit();
int rk_audio_aac_encode_init();
int rk_audio_aac_encode(char * inbuf,int inbuf_size, char * outbuf,int outbuf_size);
#endif

#endif //__RKIPC_AUDIO_H_
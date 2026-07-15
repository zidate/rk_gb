#include "mp4_muxing.h"
#include "Log/DebugDef.h"



bool CMp4Muxer::m_bIsInitedLib = false;

void CMp4Muxer::InitLib()
{
	if( false == m_bIsInitedLib )
	{
		avcodec_register_all();
		av_register_all();

		m_bIsInitedLib = true;
	}
}

CMp4Muxer::CMp4Muxer()
{
	memset(&m_stMp4Ctx, 0, sizeof(m_stMp4Ctx));
}

CMp4Muxer::~CMp4Muxer()
{

}

int CMp4Muxer::copy_codec_params_4_video(AVCodecContext *i_codec, AVCodecContext *o_codec)
{
	uint64_t extra_size = (uint64_t)i_codec->extradata_size
		+ /*FF_INPUT_BUFFER_PADDING_SIZE*/32;

	o_codec->codec_id = i_codec->codec_id;
	o_codec->codec_type = i_codec->codec_type;
	o_codec->bit_rate       = i_codec->bit_rate;
	o_codec->rc_max_rate    = i_codec->rc_max_rate;
	o_codec->rc_buffer_size = i_codec->rc_buffer_size;
	o_codec->field_order    = i_codec->field_order;
	o_codec->flags          = i_codec->flags;

	o_codec->extradata      = (uint8_t *)av_mallocz(extra_size);
	if (!(o_codec->extradata))
		return  -ENOMEM;
	if (i_codec->extradata_size > 0)
		memcpy(o_codec->extradata, i_codec->extradata, i_codec->extradata_size);
	o_codec->extradata_size = i_codec->extradata_size;

	o_codec->bits_per_coded_sample  = i_codec->bits_per_coded_sample;
	o_codec->time_base = i_codec->time_base;

	o_codec->pix_fmt            = i_codec->pix_fmt;
	o_codec->width              = i_codec->width;
	o_codec->height             = i_codec->height;
	o_codec->has_b_frames       = i_codec->has_b_frames;

	/* TODO: get more paramaters from i_codec */

	return 0;
}

int CMp4Muxer::copy_codec_params_4_audio(AVCodecContext *i_codec, AVCodecContext *o_codec)
{
	uint64_t extra_size = (uint64_t)i_codec->extradata_size
		+ /*FF_INPUT_BUFFER_PADDING_SIZE*/32;

	o_codec->codec_id = i_codec->codec_id;
	o_codec->codec_type = i_codec->codec_type;
	o_codec->bit_rate       = i_codec->bit_rate;
	o_codec->rc_max_rate    = i_codec->rc_max_rate;
	o_codec->rc_buffer_size = i_codec->rc_buffer_size;
	o_codec->field_order    = i_codec->field_order;

	o_codec->extradata      = (uint8_t *)av_mallocz(extra_size);
	if (!(o_codec->extradata))
		return  -ENOMEM;
	if (i_codec->extradata_size > 0)
		memcpy(o_codec->extradata, i_codec->extradata, i_codec->extradata_size);
	o_codec->extradata_size = i_codec->extradata_size;

	o_codec->bits_per_coded_sample  = i_codec->bits_per_coded_sample;
	o_codec->bits_per_raw_sample  = i_codec->bits_per_raw_sample;
	o_codec->time_base = i_codec->time_base;

	o_codec->flags          = i_codec->flags;

	o_codec->channel_layout		= i_codec->channel_layout;
	o_codec->sample_rate		= i_codec->sample_rate;
	o_codec->channels			= i_codec->channels;
	o_codec->frame_size		= i_codec->frame_size;
	o_codec->audio_service_type	= i_codec->audio_service_type;
	o_codec->block_align		= i_codec->block_align;
	o_codec->sample_fmt		= i_codec->sample_fmt;

	/* TODO: get more paramaters from i_codec */

	return 0;
}

AVStream* CMp4Muxer::get_video_stream(AVFormatContext *ctx)
{
	int i = 0;  

	for (i = 0; i < ctx->nb_streams; i++) {
		if (AVMEDIA_TYPE_VIDEO == ctx->streams[i]->codec->codec_type)
			return ctx->streams[i];
	}

	return NULL;
}

AVStream* CMp4Muxer::get_audio_stream(AVFormatContext *ctx)
{
	int i = 0;

	for (i = 0; i < ctx->nb_streams; i++) {
		if (AVMEDIA_TYPE_AUDIO == ctx->streams[i]->codec->codec_type)
			return ctx->streams[i];
	}

	return NULL;
}


int CMp4Muxer::mp4_mux_init(AVCodecContext *v_codec, AVCodecContext *a_codec, int v_frame_rate, const char *outMP4FileName)
{
	//Input AVFormatContext and Output AVFormatContext
	AVFormatContext *ofmt_ctx = NULL;
	AVOutputFormat  *oformat = NULL;
	AVStream *stream = NULL;
	AVCodec *codec = NULL;
	const char *muxer = "mp4";
	int ret;
	int err_code = -1;
	AVBSFContext *a_bsf_ctx = NULL;
	AVDictionary *opt = NULL;

	m_stMp4Ctx.a_bsf_ctx = NULL;
	m_stMp4Ctx.ofmt_ctx = NULL;
	m_stMp4Ctx.v_start_time = AV_NOPTS_VALUE;
	m_stMp4Ctx.a_start_time = AV_NOPTS_VALUE;
	m_stMp4Ctx.wait_IDR = 1;

	if (v_codec == NULL)
		return -1;

	oformat = av_guess_format(muxer, NULL, "");
	if (!oformat) {
		printf("cat not get muxer for:%s\n", muxer);
		return -1;
	}

	ret = avformat_alloc_output_context2(&ofmt_ctx, oformat, muxer, outMP4FileName);
	if (ret < 0)
		goto fail;

	do {
		if (!v_codec)
			break;

		/* codec may be NULL */
		codec = avcodec_find_encoder(v_codec->codec_id);
		stream = avformat_new_stream(ofmt_ctx, codec);
		if (!stream) {
			printf("alloc stream fail\n");
			goto fail;
		}

		stream->id = ofmt_ctx->nb_streams - 1;
		
		ret = copy_codec_params_4_video(v_codec, stream->codec);

		ret = avcodec_parameters_from_context(stream->codecpar, v_codec);
		printf("vedio avcodec_parameters_from_context ret : %d\n", ret);

		
		stream->avg_frame_rate.num = v_frame_rate;
		stream->avg_frame_rate.den = 1;
		stream->r_frame_rate = stream->avg_frame_rate;
		ofmt_ctx->video_codec_id = v_codec->codec_id;
		if (ret) {
			printf("copy codec params fail\n");
			goto fail;
		}
	} while (0);

	do {
		if (!a_codec)
			break;

		/* codec may be NULL */
		codec = avcodec_find_encoder(a_codec->codec_id);
		stream = avformat_new_stream(ofmt_ctx, codec);
		if (!stream) {
			printf("alloc stream fail\n");
			goto fail;
		}

		stream->id = ofmt_ctx->nb_streams - 1;
		
		ret = copy_codec_params_4_audio(a_codec, stream->codec);
		if (ret) {
			printf("copy codec params fail\n");
			goto fail;
		}

		ret = avcodec_parameters_from_context(stream->codecpar, a_codec);
		printf("audio avcodec_parameters_from_context ret : %d\n", ret);


		ofmt_ctx->audio_codec_id = a_codec->codec_id;

		#if 01
		if (AV_CODEC_ID_AAC == a_codec->codec_id)
		{
			const AVBitStreamFilter *filter = av_bsf_get_by_name("aac_adtstoasc");
			if(!filter)
			{
				printf("av_bsf_get_by_name fail\n");
				goto fail;
			}
			
			ret = av_bsf_alloc(filter, &a_bsf_ctx);
			if ( 0 != ret )
			{
				printf("av_bsf_alloc fail\n");
				goto fail;
			}
			m_stMp4Ctx.a_bsf_ctx = a_bsf_ctx;
		}
		#endif
	} while (0);

	do {
		int64_t fflags = 0;
		av_opt_get_int(ofmt_ctx, "fflags", 0, &fflags);
		fflags &= ~((int64_t)AVFMT_FLAG_FLUSH_PACKETS);
		av_opt_set_int(ofmt_ctx, "fflags", fflags, 0);
	} while (0);

	av_dump_format(ofmt_ctx, 0, outMP4FileName, 1);
	
	//Open output file
	if (!(ofmt_ctx->flags & AVFMT_NOFILE))
	{
		if (avio_open(&ofmt_ctx->pb, outMP4FileName, AVIO_FLAG_WRITE) < 0)
		{
			printf("Could not open output file '%s'\n", outMP4FileName);
			err_code = -255;
			goto fail;
		}
	}
	ret = av_dict_set(&opt, "movflags", "faststart+empty_moov", 0);
	printf("av_dict_set faststart+empty_moov. ret: %d\n", ret);
	//Write file header
	ret = avformat_write_header(ofmt_ctx, &opt);
	av_dict_free(&opt);
	if (ret < 0)
	{
		printf("Error occurred when opening output file %d\n", ret);
		goto fail;
	}
	
	m_stMp4Ctx.ofmt_ctx = ofmt_ctx;
	pthread_mutex_init(&m_stMp4Ctx.mutex, NULL);

	return 0;
fail:
	if (ofmt_ctx && ofmt_ctx->pb)
		avio_close(ofmt_ctx->pb);

	if (a_bsf_ctx)
		av_bsf_free(&a_bsf_ctx);

	if (ofmt_ctx)
		avformat_free_context(ofmt_ctx);

	return err_code;
}


int CMp4Muxer::mp4_mux_uninit(void)
{
	pthread_mutex_lock(&m_stMp4Ctx.mutex);

	if (m_stMp4Ctx.ofmt_ctx && m_stMp4Ctx.ofmt_ctx->pb)
	{
		av_write_trailer(m_stMp4Ctx.ofmt_ctx);
		avio_close(m_stMp4Ctx.ofmt_ctx->pb);
		m_stMp4Ctx.ofmt_ctx->pb = NULL;
	}

	if (m_stMp4Ctx.a_bsf_ctx)
	{
		av_bsf_free(&(m_stMp4Ctx.a_bsf_ctx));
		m_stMp4Ctx.a_bsf_ctx = NULL;
	}
	if (m_stMp4Ctx.ofmt_ctx)
	{
		avformat_free_context(m_stMp4Ctx.ofmt_ctx);
		m_stMp4Ctx.ofmt_ctx = NULL;
	}
	pthread_mutex_unlock(&m_stMp4Ctx.mutex);

	pthread_mutex_destroy(&m_stMp4Ctx.mutex);

	memset(&m_stMp4Ctx, 0, sizeof(m_stMp4Ctx));

	return 0;
}

int CMp4Muxer::mp4_mux_write(AVPacket *packet, enum AVMediaType codec_type)
{
	int ret = 0;
	int err_code = 0;
	AVStream *out_stream = NULL;
	AVRational in_time_base = AV_TIME_BASE_Q;

	if (pthread_mutex_lock(&m_stMp4Ctx.mutex) != 0)
		return -1;

	if (m_stMp4Ctx.ofmt_ctx == NULL)
	{
		pthread_mutex_unlock(&m_stMp4Ctx.mutex);
		return -2;
	}

	if (codec_type == AVMEDIA_TYPE_VIDEO)
	{
		out_stream = get_video_stream(m_stMp4Ctx.ofmt_ctx);

		if (AV_NOPTS_VALUE == m_stMp4Ctx.v_start_time)
		{
			if (AV_NOPTS_VALUE != packet->pts)
			{
				m_stMp4Ctx.v_start_time = packet->pts;
			}
			else if (AV_NOPTS_VALUE != packet->dts)
			{
				m_stMp4Ctx.v_start_time = packet->dts;
			} 
			else
			{
				printf("Invalid PTS and DTS\n");
				pthread_mutex_unlock(&m_stMp4Ctx.mutex);
				return -3;
			}
		}

		packet->pts -= m_stMp4Ctx.v_start_time;
		packet->pts = av_rescale_q(packet->pts,
				in_time_base, out_stream->time_base);
		packet->dts =  packet->pts;

		packet->stream_index = out_stream->id;

		if (!m_stMp4Ctx.wait_IDR)
		{
			// av_interleaved_write_frame 会缓存pkg, 然后按照dts排序, 并且会释放pkg
			// av_write_frame 不做任何判断直接写到输出问价中
			// 按照录像文件修复的思路, 所以要用 av_write_frame
//			ret = av_interleaved_write_frame(m_stMp4Ctx.ofmt_ctx, packet);
			ret = av_write_frame(m_stMp4Ctx.ofmt_ctx, packet);
			if (ret < 0)
				err_code = -255;
		}
		else
		{
			if (packet->flags & AV_PKT_FLAG_KEY)
			{
				m_stMp4Ctx.wait_IDR = 0;
//				ret = av_interleaved_write_frame(m_stMp4Ctx.ofmt_ctx, packet);
				ret = av_write_frame(m_stMp4Ctx.ofmt_ctx, packet);
				if (ret < 0)
					err_code = -255;
			}
		}
	} 
	else
	{
		out_stream = get_audio_stream(m_stMp4Ctx.ofmt_ctx);

		if (AV_NOPTS_VALUE == m_stMp4Ctx.a_start_time)
		{
			if (AV_NOPTS_VALUE != packet->pts)
			{
				m_stMp4Ctx.a_start_time = packet->pts;
			}
			else if (AV_NOPTS_VALUE != packet->dts)
			{
				m_stMp4Ctx.a_start_time = packet->dts;
			}
			else
			{
				printf("Invalid PTS and DTS\n");
				pthread_mutex_unlock(&m_stMp4Ctx.mutex);
				return -4;
			}
		}

		packet->pts -= m_stMp4Ctx.a_start_time;
		packet->pts = av_rescale_q(packet->pts,
				in_time_base, out_stream->time_base);
		packet->dts =  packet->pts;
		packet->stream_index = out_stream->id;

		if (m_stMp4Ctx.a_bsf_ctx)
		{
			ret = av_bsf_send_packet(m_stMp4Ctx.a_bsf_ctx, packet);
			if ( 0 != ret )
			{
				printf("av_bsf_send_packet failed. ret : %d\n", ret);
				pthread_mutex_unlock(&m_stMp4Ctx.mutex);
				return -5;
			}
			ret = av_bsf_receive_packet(m_stMp4Ctx.a_bsf_ctx, packet);
			if ( 0 != ret )
			{
				printf("av_bsf_receive_packet failed. ret : %d\n", ret);
				pthread_mutex_unlock(&m_stMp4Ctx.mutex);
				return -6;
			}
		}
//		ret = av_interleaved_write_frame(m_stMp4Ctx.ofmt_ctx, packet);
		ret = av_write_frame(m_stMp4Ctx.ofmt_ctx, packet);
		if (ret < 0)
			err_code = -255;
	}

	pthread_mutex_unlock(&m_stMp4Ctx.mutex);

	return err_code;
}

bool CMp4Muxer::IsOpenFile()
{
	if( m_stMp4Ctx.ofmt_ctx && m_stMp4Ctx.ofmt_ctx->pb )
		return true;

	return false;
}



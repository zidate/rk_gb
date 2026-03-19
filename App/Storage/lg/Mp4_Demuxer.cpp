#include <sys/time.h>
#include "PAL/Audio_coder.h"
#include "Mp4_Demuxer.h"

#define PB_AUDIO_ENC_PCM 		0
#define PB_AUDIO_ENC_ALAW 		1
#define PB_AUDIO_ENC_ULAW 		2
#define PB_AUDIO_ENC_AAC 		3

#define PB_AUDIO_ENC_TYPE  		PB_AUDIO_ENC_PCM //PB_AUDIO_ENC_ULAW //PB_AUDIO_ENC_PCM


CMp4Demuxer::CMp4Demuxer()
{
	m_pAVFmtCtx = NULL;
	m_pAVPacket = NULL;

	m_pAVCodecCtx_pcm 	= NULL;
	m_pSwrCtx_pcm 		= NULL;
	m_pAVFrame_pcm 		= NULL;
	m_iPcmBufferSize 	= 0;
	m_iPcmBufferNbSamples = 0;
	m_pPcmBuffer 		= NULL;
	m_iPcmChannelNb 	= 0;
	m_iPcmSampleFmt 	= AV_SAMPLE_FMT_NONE;

	m_pAVBSFC_h264 = NULL;
	m_pAVBSFC_aac = NULL;

	m_iVideoindex = -1;
	m_iAudioindex = -1;

	memset(&m_stAAC_ADTS_Param, 0, sizeof(m_stAAC_ADTS_Param));
}

CMp4Demuxer::~CMp4Demuxer()
{
	Close();
}



int CMp4Demuxer::Decode_AAC_extradata(AAC_ADTS_Param_s *pstAAC_ADTS_Param, unsigned char *pExtradata, int iExtraDataSize)
{
	int aot, aotext, samfreindex;
	int i, channelconfig;
	unsigned char *p = pExtradata;
	if (!pstAAC_ADTS_Param || !pExtradata || iExtraDataSize<2)
	{
		return -1;
	}
	aot = (p[0]>>3)&0x1f;
	if (aot == 31)
	{
		aotext = (p[0]<<3 | (p[1]>>5)) & 0x3f;
		aot = 32 + aotext;
		samfreindex = (p[1]>>1) & 0x0f;
		if (samfreindex == 0x0f)
		{
			channelconfig = ((p[4]<<3) | (p[5]>>5)) & 0x0f;
		}
		else
		{
			channelconfig = ((p[1]<<3)|(p[2]>>5)) & 0x0f;
		}
	}
	else
	{
		samfreindex = ((p[0]<<1)|p[1]>>7) & 0x0f;
		if (samfreindex == 0x0f)
		{
			channelconfig = (p[4]>>3) & 0x0f;
		}
		else
		{
			channelconfig = (p[1]>>3) & 0x0f;
		}
	}
#ifdef AOT_PROFILE_CTRL
	if (aot < 2)
		aot = 2;
#endif
	pstAAC_ADTS_Param->audio_object_type = aot;
	pstAAC_ADTS_Param->sampling_frequency_index = samfreindex;
	pstAAC_ADTS_Param->channel_configuration = channelconfig;
	
	return 0;
}


int CMp4Demuxer::Create_AAC_ADTS_Context(AAC_ADTS_Param_s *pstAAC_ADTS_Param, int frame_data_len, unsigned char *pbuf, int bufsize)
{
	if( NULL == pstAAC_ADTS_Param || NULL == pbuf || bufsize < 7 )
		return -1;
	
	// fill in ADTS data
	pbuf[0] = 0xFF;
	pbuf[1] = 0xF1;
	pbuf[2] = (((pstAAC_ADTS_Param->audio_object_type - 1) << 6) + (pstAAC_ADTS_Param->sampling_frequency_index << 2) + (pstAAC_ADTS_Param->channel_configuration >> 2));
	pbuf[3] = (((pstAAC_ADTS_Param->channel_configuration & 3) << 6) + ((frame_data_len+7) >> 11));
	pbuf[4] = (((frame_data_len+7) & 0x7FF) >> 3);
	pbuf[5] = ((((frame_data_len+7) & 7) << 5) + 0x1F);
	pbuf[6] = 0xFC;

	return 7;
}



/*
 *@return 0 if OK, < 0 on error
 */
int CMp4Demuxer::Open(const char *pFile, STORAGE_VIDEO_ENC_TYPE_E eVideoEncType)
{
	int ret;
	const char *name = NULL;
	AVCodecContext *videoCodecctx = NULL;
	AVCodecContext *audioCodecCtx = NULL;

	if (STORAGE_VIDEO_ENC_H264 == eVideoEncType)
		name = "h264_mp4toannexb";
	else
		name = "hevc_mp4toannexb";
	
	ret = avformat_open_input(&m_pAVFmtCtx, pFile, NULL, NULL);
	if(ret < 0)
	{
		printf("open fmtctx error. ret : %d\n", ret);

		char errStr[128] = "";
		av_strerror(ret, errStr, 128);
		printf("err : %s\n", errStr);
		
		goto fail;
	}

	ret = avformat_find_stream_info(m_pAVFmtCtx, NULL);
	if(ret < 0)
	{
		printf("find stream info. ret : %d\n", ret);
		goto fail;
	}
	
	printf("stream num is %d\n", m_pAVFmtCtx->nb_streams);
	printf("stream start_time : %lld\n", m_pAVFmtCtx->start_time);
	printf("stream duration : %lld\n", m_pAVFmtCtx->duration);

    m_pAVPacket = av_packet_alloc();
    if (!m_pAVPacket)
	{
		printf("av_packet_alloc failed.\n");
		goto fail;
    }

	//<shang> 2020.05.30 	修复第一次打开的录像文件有视频和音频, 第二次打开的录像文件只有视频时发生段错误的问题
	m_iVideoindex = -1;
	m_iAudioindex = -1;
	//<shang> 2020.05.30 end
	
	for(int i=0; i<m_pAVFmtCtx->nb_streams; i++)
	{
		if(m_pAVFmtCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO && m_iAudioindex == -1)
		{
			m_iAudioindex = i; 
		}
		else if(m_pAVFmtCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO && m_iVideoindex == -1)
		{
			m_iVideoindex = i;
		}
	}
	printf("videoindex is %d\n", m_iVideoindex);
	printf("audioindex is %d\n", m_iAudioindex);

	if( (-1 == m_iAudioindex) && (-1 == m_iVideoindex) )
	{
		printf("have not found audio and video stream.\n");
		goto fail;
	}

	if( -1 != m_iVideoindex )
		printf("video stream time base : %d / %d\n", m_pAVFmtCtx->streams[m_iVideoindex]->time_base.num, m_pAVFmtCtx->streams[m_iVideoindex]->time_base.den);
	if( -1 != m_iAudioindex )
		printf("audio stream time base : %d / %d\n", m_pAVFmtCtx->streams[m_iAudioindex]->time_base.num, m_pAVFmtCtx->streams[m_iAudioindex]->time_base.den);

	if( -1 != m_iVideoindex )
	{
		videoCodecctx = m_pAVFmtCtx->streams[m_iVideoindex]->codec;
		
		printf("video codec time base : %d / %d\n", videoCodecctx->time_base.num, videoCodecctx->time_base.den);

		printf("video codec id : %d\n", videoCodecctx->codec_id);	
		printf("video extradata_size : %d\n", videoCodecctx->extradata_size);
		/*
		printf("video extradata : ");
		for( int i = 0; i < videoCodecctx->extradata_size; i++ )
		{
			printf(" %02x", videoCodecctx->extradata[i]);
		}
		printf("\n");
		*/
	}


	if( -1 != m_iAudioindex )
	{
		audioCodecCtx = m_pAVFmtCtx->streams[m_iAudioindex]->codec;

		printf("audio codec time base : %d / %d\n", audioCodecCtx->time_base.num, audioCodecCtx->time_base.den);

		printf("audio codec id : %d\n", audioCodecCtx->codec_id);
		printf("audio sample_rate : %d\n", audioCodecCtx->sample_rate);
		printf("audio channels : %d\n", audioCodecCtx->channels);
		
		printf("audio extradata_size : %d\n", audioCodecCtx->extradata_size);
		/*
		printf("audio extradata : ");
		for( int i = 0; i < audioCodecCtx->extradata_size; i++ )
		{
			printf(" %02x", audioCodecCtx->extradata[i]);
		}
		printf("\n");
		*/

		if (AV_CODEC_ID_AAC == audioCodecCtx->codec_id)
		{
			ret = Decode_AAC_extradata(&m_stAAC_ADTS_Param, audioCodecCtx->extradata, audioCodecCtx->extradata_size);
			if( ret < 0 )
			{
				printf("aac_decode_extradata failed.\n");
				goto fail;
			}
			printf("audio_object_type        : %d\n", m_stAAC_ADTS_Param.audio_object_type);
			printf("sampling_frequency_index : %d\n", m_stAAC_ADTS_Param.sampling_frequency_index);
			printf("channel_configuration    : %d\n", m_stAAC_ADTS_Param.channel_configuration);


			enum AVSampleFormat in_sample_fmt;
			int in_sample_rate;
			int out_sample_rate;
			uint64_t in_ch_layout;
			uint64_t out_ch_layout;
			
			
			int ret, stream_index;
			AVStream *st;
			AVCodec *dec = NULL;
			AVDictionary *opts = NULL;
			

			{
				st = m_pAVFmtCtx->streams[m_iAudioindex];
			
				/* find decoder for the stream */
				dec = avcodec_find_decoder(st->codecpar->codec_id);
				if (!dec)
				{
					fprintf(stderr, "Failed to find %s codec\n", av_get_media_type_string(AVMEDIA_TYPE_AUDIO));
					goto fail;
				}
			
				/* Allocate a codec context for the decoder */
				m_pAVCodecCtx_pcm = avcodec_alloc_context3(dec);
				if (!m_pAVCodecCtx_pcm)
				{
					fprintf(stderr, "Failed to allocate the %s codec context\n", av_get_media_type_string(AVMEDIA_TYPE_AUDIO));
					goto fail;
				}
			
				/* Copy codec parameters from input stream to output codec context */
				if ((ret = avcodec_parameters_to_context(m_pAVCodecCtx_pcm, st->codecpar)) < 0)
				{
					fprintf(stderr, "Failed to copy %s codec parameters to decoder context\n", av_get_media_type_string(AVMEDIA_TYPE_AUDIO));
					goto fail;
				}
			
				/* Init the decoders, with or without reference counting */
				av_dict_set(&opts, "refcounted_frames", "0", 0);
				if ((ret = avcodec_open2(m_pAVCodecCtx_pcm, dec, &opts)) < 0)
				{
					fprintf(stderr, "Failed to open %s codec\n", av_get_media_type_string(AVMEDIA_TYPE_AUDIO));
					goto fail;
				}

				m_pSwrCtx_pcm = swr_alloc();
				if( NULL == m_pSwrCtx_pcm )
				{
					printf("swr_alloc failed!\n");
					goto fail;
				}
				
				//重采样设置选项
				//输入的采样格式
				in_sample_fmt = m_pAVCodecCtx_pcm->sample_fmt;
				
				//输出的采样格式 16bit PCM
				m_iPcmSampleFmt = AV_SAMPLE_FMT_S16;
				
				//输入的采样率
				in_sample_rate = m_pAVCodecCtx_pcm->sample_rate;
				
				//输出的采样率
				out_sample_rate = m_pAVCodecCtx_pcm->sample_rate;
				
				//如果指定输出的采样率与pCodeCtx->sample_rate相差很大时，swr_convert输出的pcm有噪音，需要使用AVAudioFifo做分包处理。
				//输入的声道布局
				in_ch_layout = m_pAVCodecCtx_pcm->channel_layout;
				printf("in_ch_layout : %llu\n", in_ch_layout);
				printf("in channel nb : %d\n", av_get_channel_layout_nb_channels(in_ch_layout));
				
				//输出的声道布局
				out_ch_layout = AV_CH_LAYOUT_MONO; 	//AV_CH_LAYOUT_MONO，单通道；AV_CH_LAYOUT_STEREO,双声道
				
				if( NULL == swr_alloc_set_opts(m_pSwrCtx_pcm, out_ch_layout, (enum AVSampleFormat)m_iPcmSampleFmt, out_sample_rate, in_ch_layout, in_sample_fmt, in_sample_rate, 0, NULL) )
				{
					printf("swr_alloc_set_opts failed!\n");
					goto fail;
				}

				ret = swr_init(m_pSwrCtx_pcm);
				if( 0 != ret )
				{
					printf("swr_alloc failed!\n");
					goto fail;
				}

				//获取输出的声道个数
				m_iPcmChannelNb = av_get_channel_layout_nb_channels(out_ch_layout);
				printf("out_channel_nb : %d\n", m_iPcmChannelNb);
				
				//存储pcm数据
				m_iPcmBufferSize = (m_iPcmChannelNb*out_sample_rate*16)/8.0/4; 		//1/4秒的数据大小
				m_iPcmBufferNbSamples = m_iPcmBufferSize / av_get_bytes_per_sample((enum AVSampleFormat)m_iPcmSampleFmt) / m_iPcmChannelNb; 	//buffer可以存放的样本数
				m_pPcmBuffer = (uint8_t *) av_malloc(m_iPcmBufferSize);
			}
			
			m_pAVFrame_pcm = av_frame_alloc();
			if (!m_pAVFrame_pcm )
			{
				fprintf(stderr, "Could not allocate frame\n");
				goto fail;
			}
		}
	}

	
	m_pAVBSFC_h264 = av_bitstream_filter_init(name);
//	m_pAVBSFC_aac = av_bitstream_filter_init("aac_adtstoasc");
//	if(!m_pAVBSFC_h264 || !m_pAVBSFC_aac)
	if( !m_pAVBSFC_h264 )
	{
//		printf("av_bitstream_filter_init h264_mp4toannexb or aac_adtstoasc failed.\n");
		printf("av_bitstream_filter_init %s failed.\n", name);
		goto fail;
	}

	return 0;
	
fail:
	if( m_pAVFmtCtx )
		avformat_close_input(&m_pAVFmtCtx);
	m_pAVFmtCtx = NULL;
	
	if( m_pAVBSFC_h264 )
		av_bitstream_filter_close(m_pAVBSFC_h264);
	m_pAVBSFC_h264 = NULL;
	
	if( m_pAVBSFC_aac )
		av_bitstream_filter_close(m_pAVBSFC_aac);
	m_pAVBSFC_aac = NULL;

	return -1;
}


/*
 *@param iPercent seek文件的百分比, 范围[0, 100]
 *@return >= 0 on success, < 0 on error
 */

int CMp4Demuxer::Seek(float fPercent)
{
	if( NULL == m_pAVFmtCtx )
		return -1;

	int ret;
	long long iSeekTime;
	long long iFileStartTime = m_pAVFmtCtx->start_time;
	long long iFileDuration = m_pAVFmtCtx->duration;

	iSeekTime = (iFileDuration * fPercent / 100) + iFileStartTime;
	
	printf("iFileStartTime : %lld, iFileDuration : %lld, fPercent : %f, iSeekTime : %lld\n", iFileStartTime, iFileDuration, fPercent, iSeekTime);
	
	return av_seek_frame(m_pAVFmtCtx, /*videoindex*/-1, iSeekTime, AVSEEK_FLAG_BACKWARD /*| AVSEEK_FLAG_ANY*/);
}

/*
 *@return >= 0 on success, < 0 on error
 */

int CMp4Demuxer::Seek(long long llTime)
{
	if( NULL == m_pAVFmtCtx )
		return -1;
	
	printf("llTime : %lld\n", llTime);
	printf("iFileStartTime : %lld, iFileDuration : %lld\n", m_pAVFmtCtx->start_time, m_pAVFmtCtx->duration);
	
	return av_seek_frame(m_pAVFmtCtx, /*videoindex*/-1, llTime, AVSEEK_FLAG_BACKWARD /*| AVSEEK_FLAG_ANY*/);
}



/*
 *@return 成功返回数据帧的实际长度, < 0 on error
 */
int CMp4Demuxer::Read(unsigned char *pBuffer, int iBufferSize, Mp4DemuxerFrameInfo_s *pFrameInfo)
{
	if( NULL == m_pAVFmtCtx )
		return -1;

	if( NULL == pBuffer || iBufferSize <= 0 )
		return -1;

	int ret;
	unsigned char aac_adts[7] = {0};

	while (1)
	{
		int iDataLen = 0;

		ret = av_read_frame(m_pAVFmtCtx, m_pAVPacket);
		if( 0 != ret )
		{
			break;
		}

		if(m_pAVPacket->stream_index == m_iAudioindex)
		{
//			printf("audio-------pts : %lld,    dts : %lld,	  duration : %lld\n", m_pAVPacket->pts, m_pAVPacket->dts, m_pAVPacket->duration);

//			printf("audio av_rescale_q pts : %lld\n",  av_rescale_q(m_pAVPacket->pts, m_pAVFmtCtx->streams[m_iAudioindex]->time_base, AV_TIME_BASE_Q));


#if 0 		//直接返回aac数据
			ret = Create_AAC_ADTS_Context(&m_stAAC_ADTS_Param, m_pAVPacket->size, aac_adts, sizeof(aac_adts));
			if ( ret > 0 )
			{
				if( iBufferSize < ret + m_pAVPacket->size )
				{
					av_packet_unref(m_pAVPacket);
					return -1;
				}

				memcpy(pBuffer, aac_adts, ret);
				memcpy(pBuffer+ret, m_pAVPacket->data, m_pAVPacket->size);

				pFrameInfo->iStreamType = 2;
				pFrameInfo->ullTimestamp = av_rescale_q(m_pAVPacket->pts, m_pAVFmtCtx->streams[m_iAudioindex]->time_base, AV_TIME_BASE_Q) / 1000;

				iDataLen = ret + m_pAVPacket->size;
			}

#else		//解码aac, 再编码成G711A / G711U

			/*
			printf("audio frame len : %d\n", m_pAVPacket->size);
			unsigned long long last_timestamp = 0, now_timestamp = 0;
			struct timeval tv = {0};
			
			gettimeofday(&tv, NULL);
			last_timestamp = ((unsigned long long)tv.tv_sec * 1000 + tv.tv_usec / 1000);
			*/
			
			pFrameInfo->iStreamType = 2;
			pFrameInfo->ullTimestamp = av_rescale_q(m_pAVPacket->pts, m_pAVFmtCtx->streams[m_iAudioindex]->time_base, AV_TIME_BASE_Q) / 1000;

			if (AV_CODEC_ID_AAC == m_pAVFmtCtx->streams[m_iAudioindex]->codec->codec_id)
			{
				#if(PB_AUDIO_ENC_TYPE == PB_AUDIO_ENC_AAC)
				ret = Create_AAC_ADTS_Context(&m_stAAC_ADTS_Param, m_pAVPacket->size, aac_adts, sizeof(aac_adts));
				if ( ret > 0 )
				{
					if( iBufferSize < ret + m_pAVPacket->size )
					{
						fprintf(stderr, "error! demuxer aac buffer not enough. buffer size[%d], data size[%d]\n", iBufferSize, ret + m_pAVPacket->size);
						av_packet_unref(m_pAVPacket);
						return -1;
					}
				
					memcpy(pBuffer, aac_adts, ret);
					memcpy(pBuffer+ret, m_pAVPacket->data, m_pAVPacket->size);
								
					iDataLen = ret + m_pAVPacket->size;
				}
				#else
				do
				{
					int got_frame;
					/* decode audio frame */
					ret = avcodec_decode_audio4(m_pAVCodecCtx_pcm, m_pAVFrame_pcm, &got_frame, m_pAVPacket);
					if (ret < 0)
					{
						fprintf(stderr, "Error decoding audio frame\n");
						av_packet_unref(m_pAVPacket);
						return -1;
					}
					
					if (got_frame)
					{
						/*
						gettimeofday(&tv, NULL);
						now_timestamp = ((unsigned long long)tv.tv_sec * 1000 + tv.tv_usec / 1000);
						printf("decode audio take %llu ms\n", (now_timestamp-last_timestamp));
						*/
						
						int output_nb_samples = swr_convert(m_pSwrCtx_pcm, &m_pPcmBuffer, m_iPcmBufferNbSamples,(const uint8_t **)m_pAVFrame_pcm->data, m_pAVFrame_pcm->nb_samples);
						
						int out_buffer_size = av_samples_get_buffer_size(NULL, m_iPcmChannelNb, output_nb_samples, (enum AVSampleFormat)m_iPcmSampleFmt, 1);

						#if(PB_AUDIO_ENC_TYPE == PB_AUDIO_ENC_PCM)
						memcpy(pBuffer+iDataLen, m_pPcmBuffer, out_buffer_size);
						int OutAudioLen = out_buffer_size;
						#elif(PB_AUDIO_ENC_TYPE == PB_AUDIO_ENC_ALAW)
						int OutAudioLen = DG_encode_g711a((char *)m_pPcmBuffer, (char *)pBuffer+iDataLen, out_buffer_size);
						#elif(PB_AUDIO_ENC_TYPE == PB_AUDIO_ENC_ULAW)
						int OutAudioLen = DG_encode_g711u((char *)m_pPcmBuffer, (char *)pBuffer+iDataLen, out_buffer_size);
						#endif
											
						iDataLen += OutAudioLen;
					}
					
					m_pAVPacket->data += ret;
					m_pAVPacket->size -= ret;
				}while(m_pAVPacket->size > 0);
				#endif
			}
			else
			{
				if (iBufferSize < m_pAVPacket->size)
				{
					av_packet_unref(m_pAVPacket);
					return -1;
				}
//				iDataLen = m_pAVPacket->size;
//				memcpy(pBuffer, m_pAVPacket->data, m_pAVPacket->size);
				iDataLen = DG_decode_g711u((char *)m_pAVPacket->data, (char *)pBuffer, m_pAVPacket->size);
			}

			/*
			gettimeofday(&tv, NULL);
			now_timestamp = ((unsigned long long)tv.tv_sec * 1000 + tv.tv_usec / 1000);
			printf("encode audio take %llu ms\n", (now_timestamp-last_timestamp));
			*/

#endif
			
		}
		else if(m_pAVPacket->stream_index == m_iVideoindex)
		{
//			printf("video frame ---------------%s\n", (m_pAVPacket->flags & AV_PKT_FLAG_KEY) ? "I" : "PB");
//			printf("video-------pts : %lld,    dts : %lld,	  durartion : %lld\n", m_pAVPacket->pts, m_pAVPacket->dts, m_pAVPacket->duration);
			
//			printf("video av_rescale_q pts : %lld\n",  av_rescale_q(m_pAVPacket->pts, m_pAVFmtCtx->streams[m_iVideoindex]->time_base, AV_TIME_BASE_Q));

			int iTmpLen = 0;
			unsigned char *pTmpBuffer = NULL;
			ret = av_bitstream_filter_filter(m_pAVBSFC_h264, m_pAVFmtCtx->streams[m_iVideoindex]->codec, NULL, &pTmpBuffer, &iTmpLen, m_pAVPacket->data, m_pAVPacket->size, 
												m_pAVPacket->flags & AV_PKT_FLAG_KEY);
//			printf("video av_bitstream_filter_filter ret : %d\n", ret); 	//av_bitstream_filter_filter eturn 1

			if( iBufferSize < iTmpLen )
			{
				printf("demux video buffer not enough. buffer=%d frame=%d key=%d pts=%lld\n",
				       iBufferSize,
				       iTmpLen,
				       (m_pAVPacket->flags & AV_PKT_FLAG_KEY) ? 1 : 0,
				       (long long)m_pAVPacket->pts);
				av_free(pTmpBuffer);
				av_packet_unref(m_pAVPacket);
				return -1;
			}

			memcpy(pBuffer, pTmpBuffer, iTmpLen);
			av_free(pTmpBuffer);
			
			pFrameInfo->iStreamType = 1;
			pFrameInfo->iFrameType = (m_pAVPacket->flags & AV_PKT_FLAG_KEY) ? 1 : 2;
			pFrameInfo->ullTimestamp = av_rescale_q(m_pAVPacket->pts, m_pAVFmtCtx->streams[m_iVideoindex]->time_base, AV_TIME_BASE_Q) / 1000;

			iDataLen = iTmpLen;

/*
			{
#include <sys/time.h>
			    static unsigned long long last_timestamp = 0, now_timestamp = 0;
				struct timeval tv = {0};

				gettimeofday(&tv, NULL);
				now_timestamp = ((unsigned long long)tv.tv_sec * 1000 + tv.tv_usec / 1000);

				printf("read mp4 video frame interal : %lld ms\n", now_timestamp-last_timestamp);
				last_timestamp = now_timestamp;

			}
*/
			
		}

		{
			const int packetStreamIndex = m_pAVPacket->stream_index;
			const long long packetPts = (long long)m_pAVPacket->pts;
			const int packetSize = m_pAVPacket->size;
			av_packet_unref(m_pAVPacket);

			if (iDataLen > 0)
			{
				return iDataLen;
			}

			printf("demux skip empty packet stream_index=%d pts=%lld size=%d\n",
			       packetStreamIndex,
			       packetPts,
			       packetSize);
		}
	}

	if( ret < 0 )
	{
		char errStr[128] = "";
		av_strerror(ret, errStr, sizeof(errStr));
		printf("demux read end/error ret=%d err=%s\n", ret, errStr);
	}

	return -1;
}

int CMp4Demuxer::Close()
{
	if( m_pAVFmtCtx )
		avformat_close_input(&m_pAVFmtCtx);
	m_pAVFmtCtx = NULL;

	if( m_pAVPacket )
		av_packet_free(&m_pAVPacket);
	m_pAVPacket = NULL;
	
	if( m_pAVBSFC_h264 )
		av_bitstream_filter_close(m_pAVBSFC_h264);
	m_pAVBSFC_h264 = NULL;
	
	if( m_pAVBSFC_aac )
		av_bitstream_filter_close(m_pAVBSFC_aac);
	m_pAVBSFC_aac = NULL;

	if( m_pAVCodecCtx_pcm )
	    avcodec_free_context(&m_pAVCodecCtx_pcm);
	m_pAVCodecCtx_pcm = NULL;
	
	if( m_pSwrCtx_pcm )
		swr_free(&m_pSwrCtx_pcm);
	m_pSwrCtx_pcm = NULL;

	if( m_pAVFrame_pcm )
	    av_frame_free(&m_pAVFrame_pcm);
	m_pAVFrame_pcm = NULL;

	if( m_pPcmBuffer )
		av_free(m_pPcmBuffer);
	m_pPcmBuffer = NULL;
	
	return 0;
}

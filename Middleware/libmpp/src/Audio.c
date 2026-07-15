#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>
#include <pthread.h>
#include <sys/time.h>
#include <math.h>
#include "PAL/MW_Common.h"
#include "PAL/Audio.h"

//#include "fdk-aac/aacenc_lib.h"
#include "PAL/libdmc.h"

#include "log.h"
#include "audio.h"

////////////////////////////////////////////////////////////////////
/// 以下是接口////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////

int AudioCreate(void)
{
	MSG("AudioCreate start rkipc_audio_init\n");
    rkipc_audio_init();
    MSG("AudioCreate start rkipc_ao_init\n");
    rkipc_ao_init();
}

int AudioDestory(void)
{
	MSG("AudioCreate start rkipc_audio_deinit\n");
    rkipc_audio_deinit();
    MSG("AudioCreate start rkipc_ao_deinit\n");
    rkipc_ao_deinit();

	return 0;

}

void AudioMicEnable(int en)
{
	rk_audio_set_mic_enable(en);
}

int AudioSwitch(unsigned int dwType, unsigned int dwChannel)
{
	return 0;
}

int AudioOutStart(void)
{
	return 0;
}

int AudioOutStop(void)
{
	return 0;
}

void AudioOutPutBuf(unsigned char *src, int size)
{
	// MSG("size is %d\n",size);
	rkipc_ao_write(src, size);
}

int AudioOutSetFormat(int coderType)
{
	return 0;
}

int AudioGetFormat(AUDIOIN_FORMAT *pFormat, int iMax)
{
	pFormat->BitRate = 8 * 16;
	pFormat->EncodeType = PCM_8TO16BIT;
	pFormat->SampleBit = 16;
	pFormat->SampleRate = 8000;
	return 0;
}

int AudioInGetChannels(void)
{
	return 0;
}

int AudioInCreate(int iChannel, int playVol, int captureVol)
{
	return 0;
}

int AudioInDestroy(int iChannel)
{
	return 0;
}

int AudioInStart(int iChannel)
{
	return 0;
}

int AudioInStop(int iChannel)
{
	return 0;
}

int AudioEncodeStart(int iChannel)
{
	return 0;
}

int AudioEncodeStop(int iChannel)
{
	return 0;
}

int AudioInSetFormat(int iChannel, AUDIOIN_FORMAT *pFormat)
{
	return 0;
}

int FrontAudioInSetVolume(int iChannel, unsigned int dwLVolume, unsigned int dwRVolume)
{
	return 0;
}

int AudioInGetCaps(AUDIOIN_CAPS *pCaps)
{
	return 0;
}

int AudioSetVolume(int dir, int cap, int play)
{
	if (0 == dir)
	{
		rk_audio_set_ao_volume(play);
	}
	else if  (1 == dir)
	{
		rk_audio_set_ai_volume(cap);
	}
	else
	{
		EMSG("AudioSetVolume unkown dir=%d\n",dir);
	}

	return 0;
}
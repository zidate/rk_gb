#include <list>
#include <deque>
#include <vector>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <errno.h>
#include <sys/sysinfo.h>
#include <sys/times.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <mtd/mtd-user.h>

#include "Common.h"
#include "Inifile.h"

int DeviceMode_g = 0; 		//0:正常模式， 1:测试模式


//判断是否为目录
static bool is_dir(const char *path)
{
    struct stat statbuf;
    if(lstat(path, &statbuf) ==0)//lstat返回文件的信息，文件信息存放在stat结构中
    {
        return S_ISDIR(statbuf.st_mode) != 0;//S_ISDIR宏，判断文件类型是否为目录
    }
    return false;
}

//判断是否为常规文件
static bool is_file(const char *path)
{
    struct stat statbuf;
    if(lstat(path, &statbuf) ==0)
        return S_ISREG(statbuf.st_mode) != 0;//判断文件是否为常规文件
    return false;
}

//判断是否是特殊目录
static bool is_special_dir(const char *path)
{
    return strcmp(path, ".") == 0 || strcmp(path, "..") == 0;
}

//生成完整的文件路径
static void get_file_path(const char *path, const char *file_name,  char *file_path)
{
    strcpy(file_path, path);
    if(file_path[strlen(path) - 1] != '/')
        strcat(file_path, "/");
    strcat(file_path, file_name);
}

void delete_file(const char *path)
{
    DIR *dir;
    dirent *dir_info;
    char file_path[PATH_MAX];
    if(is_file(path))
    {
        remove(path);
        return;
    }
    if(is_dir(path))
    {
        if((dir = opendir(path)) == NULL)
            return;
        while((dir_info = readdir(dir)) != NULL)
        {
            get_file_path(path, dir_info->d_name, file_path);
            if(is_special_dir(dir_info->d_name))
                continue;
            delete_file(file_path);
            rmdir(file_path);
        }
    }
}

void NormalRestart()
{
	printf("\033[1;32m  NormalRestart   \033[0m\n");
	g_StorageManager->StopRec();
	sync();
	CTime::sleep(3*1000);
	CFeedDog::instance()->stop();
	IMagicBox::instance()->reboot();
}

void AbnormalRestart()
{
	printf("\033[1;32m  AbnormalRestart   \033[0m\n");
	//记录一下，此处为主动重启，下次启动后不再播放系统启动中的语音
	START_PROCESS("sh", "sh", "-c", "touch " REBOOT_FLAG, NULL);
	g_StorageManager->StopRec();
	sync();
	CTime::sleep(3*1000);
	CFeedDog::instance()->stop();
	IMagicBox::instance()->reboot();
}

void SystemReset()
{
	printf("\033[1;32m  SystemReset   \033[0m\n");
	//红色指示快闪
    g_IndicatorLight.setLightStatus(CIndicatorLight::ENUM_POWER_INDICATOR_LIGHT_FAST_FLICKER);
    //联网指示灯灭
    g_IndicatorLight.setLightStatus(CIndicatorLight::ENUM_LINK_INDICATOR_LIGHT_ALWAYS_OFF);
	delete_file(CONFIG_DIR"/Json/");
//	delete_file(IPC_APP_STORAGE_PATH);

	START_PROCESS("sh", "sh", "-c", "rm -rf " CONFIG_DIR"/Json/", NULL);
//	START_PROCESS("sh", "sh", "-c", "rm -rf " IPC_APP_STORAGE_PATH, NULL);
	START_PROCESS("sh", "sh", "-c", "touch " VOICE_PROMPT_FLAG, NULL);
    g_StorageManager->StopRec();
	
    int timeout = 15;
	AppErr("aoPlayForReboot play start\n");
	CAudioPrompt::AudioFileParm audioFile;
	audioFile.strFileName = AUDIO_FILE_DEV_RESET;
	audioFile.type = 0;
	g_AudioPrompt.aoPlayForReboot(audioFile);

	while(1 == g_AudioPrompt.getPlayStatus())
	{
		timeout --;
		printf("timeout=%d\n",timeout);
		if (timeout < 0 )
			break;
		sleep(1);
	}
	g_AudioPrompt.stop();
	AppErr("aoPlayForReboot play end timeout=%d\n",timeout);

    
	sync();
	CTime::sleep(3*1000);
	CFeedDog::instance()->stop();
	IMagicBox::instance()->reboot();
}

bool CreateDetachedThread(char *threadName,funcThreadRoute route, void *param, bool scope)
{
	int ret;
	pthread_t thread;
	pthread_attr_t attr;
	pthread_attr_init( &attr);
	if (scope)
	{
		pthread_attr_setscope( &attr, PTHREAD_SCOPE_SYSTEM);
	}
	pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED);
	ret = (pthread_create( &thread, &attr, route, param) == 0 );
	pthread_setname_np(thread, threadName);
	pthread_attr_destroy( &attr);
	return ret;
}

bool CreateDetachedThreadEx(char *threadName, funcThreadRoute route, void *param, bool scope)
{
	int ret;
	pthread_t thread;
	pthread_attr_t attr;
	pthread_attr_init( &attr);
	if (scope)
	{
		pthread_attr_setscope( &attr, PTHREAD_SCOPE_SYSTEM);
	}
	pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED);
	ret = (pthread_create( &thread, &attr, route, param) == 0 );
	pthread_setname_np(thread, threadName);
	pthread_attr_destroy( &attr);
	return ret;
}


bool CreateDefaultThread(pthread_t *tid, funcThreadRoute route, void *param)
{
	int ret;
	ret = (pthread_create( tid, NULL, route, param) == 0 );
	return ret;
}

int dg_save_file(const char *path, unsigned char *buffer, int size)
{
	int ret;
	int write_count = 0;
	FILE *fp = NULL;
	
	if( !path || !buffer || size < 0 )
		return -1;
	
	fp = fopen(path, "w");
	if( !fp )
		return -1;
	
	while(write_count < size)
	{
		ret = fwrite(buffer+write_count, 1, size-write_count, fp);
		if( ret <= 0 )
			break;
		write_count += ret;
	}
	fflush(fp);
	fclose(fp);

	return write_count;
}

int dg_read_file(const char *path, unsigned char *buffer, int size)
{
	int ret;
	int read_count = 0;
	FILE *fp = NULL;

	if( !path || !buffer || size < 0 )
		return -1;
	
	fp = fopen(path, "r");
	if( !fp )
		return -1;
	
	while(read_count < size)
	{
		ret = fread(buffer+read_count, 1, size-read_count, fp);
		if( ret <= 0 )
			break;
		read_count += ret;
	}
	fclose(fp);

	return read_count;
}

static int32_t FindStartCode(const char *buf)
{
    if(buf[0] != 0x0 || buf[1] != 0x0 ){
        return 0;
    }

    if(buf[2] == 0x1){
        return 3;
    } else if(buf[2] == 0x0){
        if(buf[3] == 0x1){
            return 4;
        } else {
            return 0;
        }
    } else {
        return 0;
    }
    return 0;
}

typedef struct{
	int mtd_index;
	int mtd_size;
	int mtd_erasesize;
}MtdDevInfo_s;

static int FlashRead(const MtdDevInfo_s *pstMtddev, unsigned char *buf, int cnt)
{
	int ret = -1;
	int fd = -1;
	char path[128];
	int err_code = -1;
	
	if( !pstMtddev || !buf || (cnt <= 0) )
	{
		printf("param error!\n");
		return -1;
	}

	snprintf(path, sizeof(path), "/dev/mtd%d", pstMtddev->mtd_index);
	printf("path:%s\n", path);
	fd = open(path, O_RDWR);
	if (fd  < 0)
	{
		printf("Open %s error\n", path);
		return -1;
	}

	lseek(fd, 0, SEEK_SET);
	ret = read(fd, buf, cnt);
	if (ret < 0)
	{
		printf("FlashRead Error\n");
	}

	close(fd);
	return ret;
}

static int FlashWrite(const MtdDevInfo_s *pstMtddev, unsigned char *buf, int cnt)
{
	int ret;
	int sec_count = 0;
	int i = 0;
	struct erase_info_user erase_info;
	int fd = -1;
	char path[128];
	int err_code = -1;

	if( !pstMtddev || !buf || (cnt <= 0) || (cnt > pstMtddev->mtd_size) )
	{
		printf("param error!\n");
		return -1;
	}
	
	snprintf(path, sizeof(path), "/dev/mtd%d", pstMtddev->mtd_index);
	printf("path:%s\n", path);
	fd = open(path, O_RDWR);
	if (fd  < 0)
	{
		printf("Open %s error\n", path);
		return -1;
	}

	sec_count = (cnt + (pstMtddev->mtd_erasesize - 1)) / pstMtddev->mtd_erasesize;
	for(i = 0; i < sec_count; i++)
	{
		erase_info.start = i * pstMtddev->mtd_erasesize;
		erase_info.length = pstMtddev->mtd_erasesize;
		if (ioctl(fd, MEMERASE, &erase_info) == -1)
		{
			printf("Mem Erase Failed\n");
			goto EXIT;
		}
	}

	lseek(fd, 0, SEEK_SET);
	ret = write(fd, buf, cnt);
	if (ret != cnt)
	{
		printf("FlashWrite Error\n");
	}
	err_code = ret;
	
EXIT:
	close(fd);
	
	return err_code;
}

int SaveLicenseToFlash(unsigned char *buffer, int size)
{
	MtdDevInfo_s stMtddev = {6, 0x10000, 0x10000};
	int ret = FlashWrite(&stMtddev, buffer, size);
	return ret;
}

int ReadLicenseFromFlash(unsigned char *buffer, int size)
{
	MtdDevInfo_s stMtddev = {6, 0x1000, 0x1000};
	int ret = FlashRead(&stMtddev, buffer, size);
	return ret;
}

// int32_t ReadOneNaluH265FromBuf(const char *buffer, uint32_t nBufferSize, uint32_t offSet, NALU_H265_t* pNalu)
// {
//     uint32_t start = 0;
//     if( offSet < nBufferSize) {
//         start = FindStartCode(buffer + offSet);
//         if(start != 0) {
//             uint32_t pos = start;
//             while (offSet + pos < nBufferSize) {
//                 if(buffer[offSet + pos++] == 0x00 &&
//                     buffer[offSet + pos++] == 0x00 &&
//                     buffer[offSet + pos++] == 0x00 &&
//                     buffer[offSet + pos++] == 0x01
//                     ) {
//                     break;
//                 }
//             }

//             if(offSet + pos == nBufferSize){
//                 pNalu->len = pos - start;
//             } else {
//                 pNalu->len = (pos - 4) - start;
//             }

//             pNalu->buf =(char*)&buffer[offSet + start];
//             pNalu->forbidden_bit = pNalu->buf[0] & 0x80;
//             pNalu->naltype = pNalu->buf[0] & 0x7e; // 6 bit


//             return (pNalu->len + start);
//         }
//     }

//     return 0;
// }

// int32_t ReadOneNaluFromBuf(const char *buffer, uint32_t nBufferSize, uint32_t offSet, NALU_t* pNalu)
// {
//     uint32_t start = 0;
//     if( offSet < nBufferSize) {
//         start = FindStartCode(buffer + offSet);
//         if(start != 0) {
//             uint32_t pos = start;
//             while (offSet + pos < nBufferSize) {
//                 if(buffer[offSet + pos++] == 0x00 &&
//                     buffer[offSet + pos++] == 0x00 &&
//                     buffer[offSet + pos++] == 0x00 &&
//                     buffer[offSet + pos++] == 0x01
//                     ) {
//                     break;
//                 }
//             }

//             if(offSet + pos == nBufferSize){
//                 pNalu->len = pos - start;
//             } else {
//                 pNalu->len = (pos - 4) - start;
//             }

//             pNalu->buf =(char*)&buffer[offSet + start];
//             pNalu->forbidden_bit = pNalu->buf[0] & 0x80;
//             pNalu->nal_reference_idc = pNalu->buf[0] & 0x60; // 2 bit
//             pNalu->nal_unit_type = (pNalu->buf[0]) & 0x1f;// 5 bit

//             return (pNalu->len + start);
//         }
//     }

//     return 0;
// }

// #include <iostream>

// static inline bool is_base64(unsigned char c) {
//     return (isalnum(c) || (c == '+') || (c == '/'));
// }
// std::string base64_encode(unsigned char const* bytes_to_encode, unsigned int in_len) {
//     const std::string base64_chars = 
//             "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
//             "abcdefghijklmnopqrstuvwxyz"
//             "0123456789+/";
//     std::string ret;
//     int i = 0;
//     int j = 0;
//     unsigned char char_array_3[3];
//     unsigned char char_array_4[4];

//     while (in_len--) {
//         char_array_3[i++] = *(bytes_to_encode++);
//         if (i == 3) {
//             char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
//             char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
//             char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
//             char_array_4[3] = char_array_3[2] & 0x3f;

//             for(i = 0; (i <4) ; i++)
//                 ret += base64_chars[char_array_4[i]];
//             i = 0;
//         }
//     }
 
//     if (i)
//     {
//         for(j = i; j < 3; j++)
//             char_array_3[j] = '\0';
 
//         char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
//         char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
//         char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
//         char_array_4[3] = char_array_3[2] & 0x3f;
 
//         for (j = 0; (j < i + 1); j++)
//             ret += base64_chars[char_array_4[j]];
 
//         while(i++ < 3)
//             ret += '=';
//     }
 
//     return ret;
 
// }
 
// std::string base64_decode(std::string const& encoded_string) {
//     const std::string base64_chars = 
//         "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
//         "abcdefghijklmnopqrstuvwxyz"
//         "0123456789+/";
//     int in_len = encoded_string.size();
//     int i = 0;
//     int j = 0;
//     int in_ = 0;
//     unsigned char char_array_4[4], char_array_3[3];
//     std::string ret;
 
//     while (in_len-- && ( encoded_string[in_] != '=') && is_base64(encoded_string[in_])) {
//         char_array_4[i++] = encoded_string[in_]; in_++;
//         if (i ==4) {
//             for (i = 0; i <4; i++)
//                 char_array_4[i] = base64_chars.find(char_array_4[i]);
 
//             char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
//             char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
//             char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
 
//             for (i = 0; (i < 3); i++)
//                 ret += char_array_3[i];
//             i = 0;
//         }
//     }
 
//     if (i) {
//         for (j = i; j <4; j++)
//             char_array_4[j] = 0;
 
//         for (j = 0; j <4; j++)
//             char_array_4[j] = base64_chars.find(char_array_4[j]);
 
//         char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
//         char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
//         char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
 
//         for (j = 0; (j < i - 1); j++)
//             ret += char_array_3[j];
//     }
 
//     return ret;
// }
// {
//     const std::string s = "qq:1637132810" ;
 
//     std::string encoded = base64_encode(reinterpret_cast<const unsigned char*>(s.c_str()), s.length());
//     std::string decoded = base64_decode(encoded);
 
//     std::cout << "encoded: " << encoded << std::endl;
//     std::cout << "decoded: " << decoded << std::endl;
 
//     return 0;
// }
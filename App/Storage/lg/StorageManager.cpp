#include "StorageManager.h"
#include "DiskManager.h"

#include "Log/DebugDef.h"
#include "PAL/Capture.h"
#include "Infra/Time.h"
#include "Infra/Timer.h"

#include "Common.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <string.h>


#pragma pack(1)
typedef struct{
	unsigned long long timestamp;
	int size;
	int type; 	// 0-I帧 1-P帧 2-audio
} RecoverParam_S;
#pragma pack()


static FILE *my_fopen_file(const char *record_path)
{
	FILE *fp = NULL;
	char buff[128] = {0};

	if( !record_path )
		return NULL;
	
	snprintf(buff, sizeof(buff), "%s-recover", record_path);

	fp = fopen(buff, "w");
	return fp;
}

int disk_fread_file(FILE *fp, char *buffer, int size)
{
	int ret;
	int read_count = 0;

	if( !fp || !buffer || size < 0 )
		return -1;
	
	while(read_count < size)
	{
		ret = fread(buffer+read_count, 1, size-read_count, fp);
		if( ret <= 0 )
			break;
		read_count += ret;
	}

	return read_count;
}

int disk_fwrite_file(FILE *fp, char *buffer, int size)
{
	int ret;
	int write_count = 0;
	
	if( !fp || !buffer || size < 0 )
		return -1;
	
	while(write_count < size)
	{
		ret = fwrite(buffer+write_count, 1, size-write_count, fp);
		if( ret <= 0 )
			break;
		write_count += ret;
	}
//	fflush(fp);

	return write_count;
}

int disk_read_file(int fd, char *buffer, int size)
{
	int ret;
	int read_count = 0;
	
	if( fd < 0 || !buffer || size < 0 )
		return -1;
	
	while(read_count < size)
	{
		ret = read(fd, buffer+read_count, size-read_count);
		if( ret <= 0 )
			break;
		read_count += ret;
	}

	return read_count;
}

int disk_write_file(int fd, char *buffer, int size)
{
	int ret;
	int write_count = 0;
	
	if( fd < 0 || !buffer || size < 0 )
		return -1;
	
	while(write_count < size)
	{
		ret = write(fd, buffer+write_count, size-write_count);
		if (-1 == ret)
		{
			if (EIO == errno) //#define EIO 5 /* I/O error */
				return -255;
			break;
		}
		write_count += ret;
	}

	return write_count;
}


static void *thread_playback(void *arg)
{
	prctl(PR_SET_NAME, "thread_playback");
	
	if( NULL == arg )
		return NULL;

	int index;
	CStorageManager *pCStorageManager = NULL;
	playback_thread_param_s *pThreadParam = (playback_thread_param_s *)arg;
	if( NULL == pThreadParam->p )
	{
		goto end;
	}	

	index = pThreadParam->index;
	pCStorageManager = (CStorageManager *)(pThreadParam->p);
	
	if( pThreadParam )
	{
		delete pThreadParam;
		pThreadParam = NULL;
	}

	pCStorageManager->PlaybackProc(index);

end:
	if( pThreadParam )
		delete pThreadParam;
	
	return NULL;
}

static void *thread_download(void *arg)
{
	prctl(PR_SET_NAME, "thread_download");
	
	if( NULL == arg )
		return NULL;

	int index;
	CStorageManager *pCStorageManager = NULL;
	playback_thread_param_s *pThreadParam = (playback_thread_param_s *)arg;
	if( NULL == pThreadParam->p )
	{
		goto end;
	}	

	index = pThreadParam->index;
	pCStorageManager = (CStorageManager *)(pThreadParam->p);
	
	if( pThreadParam )
	{
		delete pThreadParam;
		pThreadParam = NULL;
	}

	pCStorageManager->DownloadProc(index);

end:
	if( pThreadParam )
		delete pThreadParam;
	
	return NULL;
}

static void *thread_recover_record(void *arg)
{
	prctl(PR_SET_NAME, "thread_recover_record");
	if( NULL == arg )
		return NULL;

	CStorageManager *pCStorageManager = NULL;
	RecoverThradParam_S stParam;
	RecoverThradParam_S *pThreadParam = (RecoverThradParam_S *)arg;

	memcpy(&stParam, pThreadParam, sizeof(RecoverThradParam_S));
	
	delete pThreadParam;
	pThreadParam = NULL;

	pCStorageManager = (CStorageManager *)(stParam.p);
	pCStorageManager->DoRecoverRcdProc(&stParam);
	
	return NULL;
}





CStorageManager::CStorageManager() : CThread("CStorageManager", 50)
{
	m_bInit = false;
	
	CMp4Muxer::InitLib();
	
	memset(m_arrSpsPps, 0, sizeof(m_arrSpsPps));
	m_iSpsPpsLen = 0;
	
	m_bDiskFull = false;
		
	m_bCycleRecordFlag = false;
	m_iRecordMode = DISK_RECORD_MODE_CLOSE;
	m_bStartRecord = false;
	m_bRecordWorking = false;
	m_bStartAlarmRecord = false;

	m_mapAlarmRecordCount.clear();

	memset(m_arrStPlaybackManager, 0, sizeof(m_arrStPlaybackManager));

	//视频参数

	m_iWidth = 3840;
	m_iHeight = 1080;

	
    //CaptureGetResolution(0,&m_iWidth,&m_iHeight);
    printf("CStorageManager main_video : %d,%d\n", m_iWidth,m_iHeight);


	m_iBitRate = 1024*1024;
	m_iFrameRate = 15;
	m_iGop = 30;
	m_eVideoEncType = STORAGE_VIDEO_ENC_H265;
}

CStorageManager::~CStorageManager()
{

}

CStorageManager* CStorageManager::defaultStorageManager()
{
	static CStorageManager* p = NULL;
	static CMutex			s_mutex;
	
	CGuard guard(s_mutex);
	if( NULL == p )
	{
		p = new CStorageManager();
	}

	return p;
}

int CStorageManager::Init()
{
	if( m_bInit )
		return 0;
	AppErr("storage init.");
	
	char path[128] = __STORAGE_SD_MOUNT_PATH__"/DCIM/";
	//DCIM	
	DIR *pDir = opendir(path);
	if( NULL == pDir )
	{
		mkdir(path, 0777);
	}
	else
	{
		closedir(pDir);
	}

	//读取spspps
	do_read_sps_pps();
	
	//删除空的录像目录
	//不再使用CleanEmptyRecordDir();
			
	m_bStartAlarmRecord = false;
	
	m_mapAlarmRecordCount.clear();

	m_avStreamRingBuffer.Init(m_iBitRate/1024, m_iFrameRate, 5);

	m_mapRecordFile.clear();
	m_mapRecordTime.clear();

	//扫描SD卡中的录像文件, 生成索引表
	CreateRecordFileIndex();

	//修复未正常结束的mp4文件
	RecoverTmpRecordFile();

	//检查SD卡是否满
	Int32 i32ret;
	UInt64 u64DiskFreeSize;
	i32ret = CDiskManager::defaultStorageManager()->GetDiskcapacity(NULL, NULL, &u64DiskFreeSize);
	AppErr("Disk Free Size : %llu\n", u64DiskFreeSize);
	if( (0 == i32ret) && (u64DiskFreeSize < RECORD_RESERVE_MEMORY_SIZE_BYTES) )
	{
		m_bDiskFull = true;
	}

	memset(m_arrStPlaybackManager, 0, sizeof(m_arrStPlaybackManager));
	pthread_mutex_init(&m_mutexPlaybackManager, NULL);

	memset(m_arStDownloadManager, 0, sizeof(m_arStDownloadManager));
	pthread_mutex_init(&m_mutexDownloadManager, NULL);

	//启动录像线程
	start();

	m_bInit = true;

	return 0;
}

int CStorageManager::UnInit()
{
	if( false == m_bInit )
		return 0;
	AppErr("storage uninit.");
	
	//停止录像线程
//	if( isRunning() )
	{
		stop();
	}
	
	m_bStartAlarmRecord = false;
	
	//m_avStreamRingBuffer.UnInit(); //2023-02-22 注释，不反复申请，不然容易产生内存碎片，触发OOM
	
	pthread_mutex_lock(&m_mutexPlaybackManager);
	//停止所有回放线程
	for( int i = 0; i < MAX_PLAYBACK_NUM; i++ )	
	{
		if( true == m_arrStPlaybackManager[i].bThreadRunningFlag )
		{
			m_arrStPlaybackManager[i].bEnablePlay = false;
			m_arrStPlaybackManager[i].bThreadRunningFlag = false;
			
			pthread_mutex_unlock(&m_mutexPlaybackManager);
			void* result;
			pthread_join(m_arrStPlaybackManager[i].threadPlayback_tid, &result);
			pthread_mutex_lock(&m_mutexPlaybackManager);
		}
	}
	memset(m_arrStPlaybackManager, 0, sizeof(m_arrStPlaybackManager));
	
	pthread_mutex_unlock(&m_mutexPlaybackManager);

	pthread_mutex_destroy(&m_mutexPlaybackManager);

	//停止所有下载线程
	for( int i = 0; i < MAX_PLAYBACK_NUM; i++ )	
	{
		StopDownload(i);
	}
	memset(m_arStDownloadManager, 0, sizeof(m_arStDownloadManager));
	pthread_mutex_destroy(&m_mutexDownloadManager);
	
	m_mapAlarmRecordCount.clear();

	TRecordFileMap::iterator it;
	for( it = m_mapRecordFile.begin(); it != m_mapRecordFile.end(); it++ )
	{
		TRecordFileInfoList *pListRecordFileInfo = it->second;
		if( NULL != pListRecordFileInfo )
			delete pListRecordFileInfo;
		
		it->second = NULL;
	}
	m_mapRecordFile.clear();
	m_mapRecordTime.clear();
	
	//清空spspps
	memset(m_arrSpsPps, 0, sizeof(m_arrSpsPps));
	m_iSpsPpsLen = 0;

	m_bInit = false;

	return 0;
}

int CStorageManager::FreeRingBuffer()
{
	m_avStreamRingBuffer.UnInit();
}

int CStorageManager::StartRecord(int iRecordMode, bool bCycleRecord)
{
	if( iRecordMode < DISK_RECORD_MODE_CLOSE || iRecordMode > DISK_RECORD_MODE_FULLTIME )
		return -1;

	AppInfo("start record mode : %d\n", iRecordMode);
	m_iRecordMode = iRecordMode;
	m_bCycleRecordFlag = bCycleRecord;
	
	m_bStartRecord = true;

	return 0;
}

int CStorageManager::StopRecord()
{	
	m_iRecordMode = DISK_RECORD_MODE_CLOSE;
	
	m_bStartRecord = false;
	
	return 0;
}

void CStorageManager::SetRecordMode(int mode)
{
	m_iRecordMode = mode;
}

void CStorageManager::TriggerAlarmRecord()
{
	m_bStartAlarmRecord = true;
}

void CStorageManager::ClearAlarmRecord()
{
	m_bStartAlarmRecord = false;
}

#define RECORD_DBG_PRNT_TIME
/// 线程执行体
static char record_buffer_g[STORAGE_MAX_FRAME_SIZE];
void CStorageManager::ThreadProc()
{
	int ret;
	int retCode;
	int iFrameSize;
	bool bNeedIFrame = false;
	char record_file_path[128] = "";
	stream_data_header_S stStreamDataHeader = {0};
	char *pData = NULL;
	
	UInt64 u64FileStartTime_ms; 			//当前录像文件的开始时间
	UInt64 u64Last_I_FrameTimestamp_ms; 	//上一个I帧的时间戳
	UInt64 u64LastFrameTimestamp_ms; 		//上一个帧的时间戳
	char arrCurRecordFilePath[128]; 		//当前录像文件路径

	bool bFullTimeRcdWriteFirstIFrame = false;
	int iRecvDataTime = 0; //取到音视频数据的系统运行时间

	bool bLastAlarmStatus = false;
	record_file_info_s stAlarmRecordTimeInfo = {0};								

#ifdef RECORD_DBG_PRNT_TIME
	unsigned long long s_ms;
	unsigned long long e_ms;
	unsigned long long duration_ms;
#endif

//	pData = new char[STORAGE_MAX_FRAME_SIZE];
	pData = record_buffer_g;
	if( NULL == pData )
	{
		AppErr("new char[%d] failed!\n", STORAGE_MAX_FRAME_SIZE);
		goto end;
	}

	while( m_bLoop )
	{
		if( false == m_bStartRecord ) 	//未开启录像
		{
			m_bRecordWorking = false;
			sleep(2);
			continue;
		}

		if( true == m_bDiskFull  )
		{
			if( false == m_bCycleRecordFlag )
			{
				m_bRecordWorking = false;
				//SD卡已满, 循环录像开关未打开
				goto end;
			}
			else
			{
				//SD卡已满, 循环录像开关已开, 需要删除最早的录像文件
				DeleteOrriginalRecordFile_2();
			}
		}

		if( DISK_RECORD_MODE_CLOSE == m_iRecordMode ) 				//关闭录像
		{
			m_bRecordWorking = false;
			while( m_bLoop )
			{
				if( DISK_RECORD_MODE_CLOSE != m_iRecordMode )
					break;
				sleep(2);
			}
				
		}
		else if( DISK_RECORD_MODE_ALARM == m_iRecordMode ) 			//报警录像
		{
			m_bRecordWorking = true;
			
			UInt64 i64CurSysTime_ms;
			struct timeval tv = {0};
			bool bIsOpenMp4File = false;
			UInt32 i32TmpDateIndex;
			int rcd_file_num;
			TAlarmRecordCountMap::iterator it;

			m_bStartAlarmRecord = false;
			
			while( m_bLoop && (DISK_RECORD_MODE_ALARM == m_iRecordMode) )
			{
				i32TmpDateIndex = GetDateIndex(time(NULL));
				rcd_file_num = GetTodayRecNum();
				AppInfo("rcd_file_num : %d\n", rcd_file_num);
				if( rcd_file_num > /*ALARM_RECORD_MAX_NUM_PER_DAY*/STORAGE_MAX_FILE_PER_DAY ) 			//今天报警录像达到上限
				{
					sleep(2);
					continue;
				}

				if( true == m_bStartAlarmRecord )
				{
					gettimeofday(&tv, NULL);
					i64CurSysTime_ms = ((unsigned long long)tv.tv_sec * 1000 + tv.tv_usec / 1000) - ALARM_PRERECORD_TIME_MS - 500; 		//<shang>
					m_avStreamRingBuffer.DiscardDataOnTime(i64CurSysTime_ms); 		//丢弃 ringbuffer 中 i64CurSysTime_ms 之前最近的 I 帧之前的数据
					
					while( m_bLoop && m_bStartAlarmRecord && DISK_RECORD_MODE_ALARM == m_iRecordMode )
					{
						iFrameSize = m_avStreamRingBuffer.GetData( &stStreamDataHeader, pData, STORAGE_MAX_FRAME_SIZE );
						if( iFrameSize <= 0 )
						{
							usleep(20*1000);
							continue;
						}
						iRecvDataTime = GetSystemUptime_s();
						
						if( (1 != stStreamDataHeader.iStreamType) || (1 != stStreamDataHeader.iFrameType) ) 	//不是视频帧或者不是I帧
						{
							usleep(20*1000);
							continue;
						}

						//H264需要sps pps
						if( STORAGE_VIDEO_ENC_H264 == m_eVideoEncType && (m_iSpsPpsLen <= 0) )
						{
							for(int i = 0; i < iFrameSize && i < sizeof(m_arrSpsPps); i++)
							{
								if( (0x00 == pData[i]) && (0x00 == pData[i+1]) && (0x00 == pData[i+2]) && (0x01 == pData[i+3]) && (0x5 == (pData[i+4]&0x1F)) )
								{
									if (m_iSpsPpsLen != i || memcmp(m_arrSpsPps, pData, m_iSpsPpsLen))
									{
										memcpy(m_arrSpsPps, pData, i);
										m_iSpsPpsLen = i;
										do_save_sps_pps();
									}
									/* printf("sps pps : ");
									for(i = 0; i < m_iSpsPpsLen; i++)
										printf("%02x ", (unsigned char)m_arrSpsPps[i]);
									printf("\n"); */
									break;
								}
							}
						}
							
						u64FileStartTime_ms = stStreamDataHeader.ullTimestamp;			//文件开始时间
						
						GetRecordFilePath(stStreamDataHeader.ullTimestamp, record_file_path, 128, DISK_RECORD_MODE_ALARM);
						
						InitMp4Muxer(record_file_path);
						snprintf(arrCurRecordFilePath, sizeof(arrCurRecordFilePath), record_file_path);
					
						//写入第一帧数据
						WriteToMp4Muxer(&u64FileStartTime_ms, 1, 1, &stStreamDataHeader.ullTimestamp, (UInt8 *)pData, iFrameSize);
						
						u64Last_I_FrameTimestamp_ms = stStreamDataHeader.ullTimestamp;	//上一个I帧的时间戳
						u64LastFrameTimestamp_ms = stStreamDataHeader.ullTimestamp;		//上一个帧的时间戳

						bIsOpenMp4File = true;
						
						break;
					}
				}

				while( m_bLoop && m_bStartAlarmRecord && bIsOpenMp4File && DISK_RECORD_MODE_ALARM == m_iRecordMode )
				{
					iFrameSize = m_avStreamRingBuffer.GetData( &stStreamDataHeader, pData, STORAGE_MAX_FRAME_SIZE );
					if( iFrameSize <= 0 )
					{
						if (GetSystemUptime_s() > (iRecvDataTime + STORAGE_MAX_FRAME_TIME_DIFF/1000))
							break; //长时间没有数据，关闭当前文件
						usleep(20*1000);
						continue;
					}
					iRecvDataTime = GetSystemUptime_s();
					
					if( 2 == stStreamDataHeader.iStreamType )		//audio
					{
						WriteToMp4Muxer(&u64FileStartTime_ms, 2, 0, &stStreamDataHeader.ullTimestamp, (UInt8 *)pData, iFrameSize);			
					}
					else if( 1 == stStreamDataHeader.iStreamType )	//video
					{

						//录像断续问题，有2个原因。1是编码器出流是断续的；2是写卡速率太慢导致ringbuffer的覆盖。
						//不能直接注释这里的帧超时切文件功能去规避这两个问题，只能调大 STORAGE_MAX_FRAME_TIME_DIFF 尽量减少影响。
						//因为如果出现校时前后时间跨度大的话就会有问题，比如跨天，校时前后两帧之间是空白的，APP搜索回放就会有问题
						#if 01
						if( labs(stStreamDataHeader.ullTimestamp - u64LastFrameTimestamp_ms) > STORAGE_MAX_FRAME_TIME_DIFF )	//可能校时了
						{
							break;
						}
						#endif

						
						if( 1 == stStreamDataHeader.iFrameType ) 	//I帧, 需要判断是否满足切换文件的条件
						{
							//主要是判断跨天, 跨天则拆分成两个文件
							ret = FilterFIle(&u64FileStartTime_ms, &u64LastFrameTimestamp_ms, &stStreamDataHeader.ullTimestamp);
							if( ret > 0 )
							{
								bIsOpenMp4File = false;
								
								//关闭当前文件
								UnInitMp4Muxer();
								
								//获取当前录像文件的实际名字
								GetFilePathBaseOnTimestamp(&u64FileStartTime_ms, &u64LastFrameTimestamp_ms, record_file_path, 128, DISK_RECORD_MODE_ALARM);
								
								//改名
								AppErr("rename %s to %s\n", arrCurRecordFilePath, record_file_path);
								rename(arrCurRecordFilePath, record_file_path);
																
								//把新文件添加到索引表
								char strPath[128];
								GetDateIndexFilePath(u64FileStartTime_ms/1000, strPath);
								record_file_info_s stRecordFileInfo = {0};								
								stRecordFileInfo.iStartTime = u64FileStartTime_ms/1000;
								stRecordFileInfo.iEndTime = u64LastFrameTimestamp_ms/1000;
								stRecordFileInfo.iRecType = 1;
								InsertlRecordFileIndex(strPath, &stRecordFileInfo);								
								
								//检查SD卡是否满
								UInt64 u64DiskFreeSize;
								ret = CDiskManager::defaultStorageManager()->GetDiskcapacity(NULL, NULL, &u64DiskFreeSize);
								if( (0 == ret) && (u64DiskFreeSize < RECORD_RESERVE_MEMORY_SIZE_BYTES) )
								{
									m_bDiskFull = true;
									if( false == m_bCycleRecordFlag )
									{
										//SD卡已满, 循环录像开关未打开
										goto end;
									}
									else
									{
										//SD卡已满, 循环录像开关已开, 需要删除最早的一个录像文件
										DeleteOrriginalRecordFile_2();
									}
								}

								//打开新文件
								u64FileStartTime_ms = stStreamDataHeader.ullTimestamp;			//文件开始时间
								
								GetRecordFilePath(stStreamDataHeader.ullTimestamp, record_file_path, 128, DISK_RECORD_MODE_ALARM);
								InitMp4Muxer(record_file_path);
								snprintf(arrCurRecordFilePath, sizeof(arrCurRecordFilePath), record_file_path);
								
								//写入第一帧数据
								WriteToMp4Muxer(&u64FileStartTime_ms, 1, 1, &stStreamDataHeader.ullTimestamp, (UInt8 *)pData, iFrameSize);
								
								u64Last_I_FrameTimestamp_ms = stStreamDataHeader.ullTimestamp;	//上一个I帧的时间戳
								u64LastFrameTimestamp_ms = stStreamDataHeader.ullTimestamp;		//上一个帧的时间戳
								
								bIsOpenMp4File = true;
							}
							else
							{
								WriteToMp4Muxer(&u64FileStartTime_ms, 1, 1, &stStreamDataHeader.ullTimestamp, (UInt8 *)pData, iFrameSize);
								
								u64Last_I_FrameTimestamp_ms = stStreamDataHeader.ullTimestamp;	//上一个I帧的时间戳
							}
						}
						else
						{
							WriteToMp4Muxer(&u64FileStartTime_ms, 1, 2, &stStreamDataHeader.ullTimestamp, (UInt8 *)pData, iFrameSize);
						}
						
						u64LastFrameTimestamp_ms = stStreamDataHeader.ullTimestamp;		//上一个帧的时间戳			
					}
				}

				if( bIsOpenMp4File )
				{
					bIsOpenMp4File = false;
					
					//关闭当前文件
					UnInitMp4Muxer();
					
					//获取当前录像文件的实际名字
					GetFilePathBaseOnTimestamp(&u64FileStartTime_ms, &u64LastFrameTimestamp_ms, record_file_path, 128, DISK_RECORD_MODE_ALARM);
					
					//改名
					AppErr("rename %s to %s\n", arrCurRecordFilePath, record_file_path);
					rename(arrCurRecordFilePath, record_file_path);
										
					//把新文件添加到索引表
					char strPath[128];
					GetDateIndexFilePath(u64FileStartTime_ms/1000, strPath);
					record_file_info_s stRecordFileInfo = {0};
					stRecordFileInfo.iStartTime = u64FileStartTime_ms/1000;
					stRecordFileInfo.iEndTime = u64LastFrameTimestamp_ms/1000;
					stRecordFileInfo.iRecType = 1;						
					InsertlRecordFileIndex(strPath, &stRecordFileInfo); 												
					
					//检查SD卡是否满
					UInt64 u64DiskFreeSize;
					ret = CDiskManager::defaultStorageManager()->GetDiskcapacity(NULL, NULL, &u64DiskFreeSize);
					if( (0 == ret) && (u64DiskFreeSize < RECORD_RESERVE_MEMORY_SIZE_BYTES) )
					{
						m_bDiskFull = true;
						if( false == m_bCycleRecordFlag )
						{
							//SD卡已满, 循环录像开关未打开
							goto end;
						}
						else
						{
							//SD卡已满, 循环录像开关已开, 需要删除最早的一个录像文件
							DeleteOrriginalRecordFile_2();
						}
					}
					
//					m_bStartAlarmRecord = false; 帧间隔超过 STORAGE_MAX_FRAME_TIME_DIFF 时，应该继续录像，所以注释
				}
				

				/***************************切换录像模式的时候, 在这里退出报警录像********************************/
				if( DISK_RECORD_MODE_ALARM != m_iRecordMode )
				{
					break;
				}
				/***************************切换录像模式的时候, 在这里退出报警录像********************************/

				usleep(500000);
			}
		}
		else if( DISK_RECORD_MODE_FULLTIME == m_iRecordMode ) 		//全天录像
		{
			m_bRecordWorking = true;
			m_bStartAlarmRecord = false;
			bFullTimeRcdWriteFirstIFrame = false;
			bLastAlarmStatus = false;
			
			//获取第一个I帧, 打开第一个文件
			while( m_bLoop )
			{
				/***************************切换录像模式的时候, 在这里退出全天录像********************************/
				if( DISK_RECORD_MODE_FULLTIME != m_iRecordMode )
				{
					break;
				}
				/***************************切换录像模式的时候, 在这里退出全天录像********************************/

				iFrameSize = m_avStreamRingBuffer.GetData( &stStreamDataHeader, pData, STORAGE_MAX_FRAME_SIZE );
				if( iFrameSize <= 0 )
				{
					usleep(20*1000);
					continue;
				}
				iRecvDataTime = GetSystemUptime_s();

				if( (1 != stStreamDataHeader.iStreamType) || (1 != stStreamDataHeader.iFrameType) ) 	//不是视频帧或者不是I帧
				{
					usleep(20*1000);
					continue;
				}

				//H264需要sps pps
				if( STORAGE_VIDEO_ENC_H264 == m_eVideoEncType && (m_iSpsPpsLen <= 0) )
				{
					for(int i = 0; i < iFrameSize && i < sizeof(m_arrSpsPps); i++)
					{
						if( (0x00 == pData[i]) && (0x00 == pData[i+1]) && (0x00 == pData[i+2]) && (0x01 == pData[i+3]) && (0x5 == (pData[i+4]&0x1F)) )
						{
							if (m_iSpsPpsLen != i || memcmp(m_arrSpsPps, pData, m_iSpsPpsLen))
							{
								memcpy(m_arrSpsPps, pData, i);
								m_iSpsPpsLen = i;
								do_save_sps_pps();
							}				
							/* printf("sps pps : ");
							for(i = 0; i < m_iSpsPpsLen; i++)
								printf("%02x ", (unsigned char)m_arrSpsPps[i]);
							printf("\n"); */
							break;
						}
					}
				}

				u64FileStartTime_ms = stStreamDataHeader.ullTimestamp; 			//文件开始时间
				
				GetRecordFilePath(stStreamDataHeader.ullTimestamp, record_file_path, 128, DISK_RECORD_MODE_FULLTIME);
				
				InitMp4Muxer(record_file_path);
				snprintf(arrCurRecordFilePath, sizeof(arrCurRecordFilePath), record_file_path);
			
				//写入第一帧数据
				WriteToMp4Muxer(&u64FileStartTime_ms, 1, 1, &stStreamDataHeader.ullTimestamp, (UInt8 *)pData, iFrameSize);
				
				u64Last_I_FrameTimestamp_ms = stStreamDataHeader.ullTimestamp;	//上一个I帧的时间戳
				u64LastFrameTimestamp_ms = stStreamDataHeader.ullTimestamp;		//上一个帧的时间戳

				bFullTimeRcdWriteFirstIFrame = true;
				
				break;
			}

			/***************************切换录像模式的时候, 在这里退出全天录像********************************/
			if( DISK_RECORD_MODE_FULLTIME != m_iRecordMode )
			{
				continue;
			}
			/***************************切换录像模式的时候, 在这里退出全天录像********************************/

			while( m_bLoop )
			{
				if (DISK_RECORD_MODE_FULLTIME == m_iRecordMode)
				{
					iFrameSize = m_avStreamRingBuffer.GetData( &stStreamDataHeader, pData, STORAGE_MAX_FRAME_SIZE );
					if( iFrameSize <= 0 )
					{
						if (GetSystemUptime_s() > (iRecvDataTime + STORAGE_MAX_FRAME_TIME_DIFF/1000))
						{
							//长时间没有数据，关闭当前文件
							if (true == bFullTimeRcdWriteFirstIFrame)
							{
								/*关闭录像时，防止ringbuffer没有数据（因为record.cpp中可能会不投流），强制走视频分支并退出*/
								memset(&stStreamDataHeader, 0, sizeof(stStreamDataHeader));
								//暂时用 stStreamDataHeader.ullTimestamp = 0 表示退出全时录像关闭录像文件
								stStreamDataHeader.iStreamType = 1;
							}
							else
							{
								break;
							}
						}
						else
						{
							usleep(20*1000);
							continue;
						}
					}
					iRecvDataTime = GetSystemUptime_s();
				}
				else
				{
					if (true == bFullTimeRcdWriteFirstIFrame)
					{
						/*关闭录像时，防止ringbuffer没有数据（因为record.cpp中可能会不投流），强制走视频分支并退出*/
						memset(&stStreamDataHeader, 0, sizeof(stStreamDataHeader));
						//暂时用 stStreamDataHeader.ullTimestamp = 0 表示退出全时录像关闭录像文件
						stStreamDataHeader.iStreamType = 1;
					}
					else
					{
						break;
					}
				}

				if( 2 == stStreamDataHeader.iStreamType ) 		//audio
				{
#ifdef RECORD_DBG_PRNT_TIME
					s_ms = GetSystemUptime_ms();
#endif
					WriteToMp4Muxer(&u64FileStartTime_ms, 2, 0, &stStreamDataHeader.ullTimestamp, (UInt8 *)pData, iFrameSize);

#ifdef RECORD_DBG_PRNT_TIME
					e_ms = GetSystemUptime_ms();
					duration_ms = e_ms - s_ms;
					if (duration_ms > 1000)
						printf("record ---> audio   takes %lld ms\n", duration_ms);
#endif
				}
				else if( 1 == stStreamDataHeader.iStreamType ) 	//video
				{
					//录像断续问题，有2个原因。1是编码器出流是断续的；2是写卡速率太慢导致ringbuffer的覆盖。
					//不能直接注释这里的帧超时切文件功能去规避这两个问题，只能调大 STORAGE_MAX_FRAME_TIME_DIFF 尽量减少影响。
					//因为如果出现校时前后时间跨度大的话就会有问题，比如跨天，校时前后两帧之间是空白的，APP搜索回放就会有问题
					#if 01
					if( (0 == stStreamDataHeader.ullTimestamp) || 
						(labs(stStreamDataHeader.ullTimestamp - u64LastFrameTimestamp_ms) > STORAGE_MAX_FRAME_TIME_DIFF ))	//退出全时录像或者可能校时了
					{
						//关闭当前文件
						UnInitMp4Muxer();
						
						//获取当前录像文件的实际名字
						GetFilePathBaseOnTimestamp(&u64FileStartTime_ms, &u64LastFrameTimestamp_ms, record_file_path, 128, DISK_RECORD_MODE_FULLTIME);
						
						//改名
						AppErr("rename %s to %s\n", arrCurRecordFilePath, record_file_path);
						rename(arrCurRecordFilePath, record_file_path);
												
						//把新文件添加到索引表
						char strPath[128];
						GetDateIndexFilePath(u64FileStartTime_ms/1000, strPath);
						record_file_info_s stRecordFileInfo = {0};							
						stRecordFileInfo.iStartTime = u64FileStartTime_ms/1000;
						stRecordFileInfo.iEndTime = u64LastFrameTimestamp_ms/1000;
						stRecordFileInfo.iRecType = 0;
						InsertlRecordFileIndex(strPath, &stRecordFileInfo); 																		
						
						//检查SD卡是否满
						UInt64 u64DiskFreeSize;
						ret = CDiskManager::defaultStorageManager()->GetDiskcapacity(NULL, NULL, &u64DiskFreeSize);
						if( (0 == ret) && (u64DiskFreeSize < RECORD_RESERVE_MEMORY_SIZE_BYTES) )
						{
							m_bDiskFull = true;
							if( false == m_bCycleRecordFlag )
							{
								//SD卡已满, 循环录像开关未打开
								goto end;
							}
							else
							{
								//SD卡已满, 循环录像开关已开, 需要删除最早的一个录像文件
								DeleteOrriginalRecordFile_2();
							}
						}
						if (true == bLastAlarmStatus)
						{							
							stAlarmRecordTimeInfo.iEndTime = u64LastFrameTimestamp_ms/1000;;
							stAlarmRecordTimeInfo.iRecType = 1;
							GetAlarmRecIndexFilePath(stAlarmRecordTimeInfo.iStartTime, strPath);
							InsertlRecordFileIndex(strPath, &stAlarmRecordTimeInfo);
							bLastAlarmStatus = false;
						}

						break; 		//重新等待I帧再开始录像
					}
					#endif
										
					if( 1 == stStreamDataHeader.iFrameType ) 	//I帧, 需要判断是否满足切换文件的条件
					{
						retCode = FilterFIle(&u64FileStartTime_ms, &u64LastFrameTimestamp_ms, &stStreamDataHeader.ullTimestamp);
						if( (retCode > 0) || (DISK_RECORD_MODE_FULLTIME != m_iRecordMode) ) 		//切换文件, 关闭当前文件, 打开新文件
						{
							//关闭当前文件
							UnInitMp4Muxer();

							//获取当前录像文件的实际名字
							GetFilePathBaseOnTimestamp(&u64FileStartTime_ms, &u64LastFrameTimestamp_ms, record_file_path, 128, DISK_RECORD_MODE_FULLTIME);

							//改名
							AppErr("rename %s to %s\n", arrCurRecordFilePath, record_file_path);
							rename(arrCurRecordFilePath, record_file_path);

							//把新文件添加到索引表
							char strPath[128];
							GetDateIndexFilePath(u64FileStartTime_ms/1000, strPath);
							record_file_info_s stRecordFileInfo = {0};
							stRecordFileInfo.iStartTime = u64FileStartTime_ms/1000;
							stRecordFileInfo.iEndTime = u64LastFrameTimestamp_ms/1000;
							stRecordFileInfo.iRecType = 0;
							InsertlRecordFileIndex(strPath, &stRecordFileInfo);

							//检查SD卡是否满
							UInt64 u64DiskFreeSize;
							ret = CDiskManager::defaultStorageManager()->GetDiskcapacity(NULL, NULL, &u64DiskFreeSize);
							if( (0 == ret) && (u64DiskFreeSize < RECORD_RESERVE_MEMORY_SIZE_BYTES) )
							{
								m_bDiskFull = true;
								if( false == m_bCycleRecordFlag )
								{
									//SD卡已满, 循环录像开关未打开
									goto end;
								}
								else
								{
									//SD卡已满, 循环录像开关已开, 需要删除最早的一个录像文件
									DeleteOrriginalRecordFile_2();
								}
							}
							if (2 != retCode && true == bLastAlarmStatus)
							{							
								stAlarmRecordTimeInfo.iEndTime = u64LastFrameTimestamp_ms/1000;;
								stAlarmRecordTimeInfo.iRecType = 1;
								GetAlarmRecIndexFilePath(stAlarmRecordTimeInfo.iStartTime, strPath);
								InsertlRecordFileIndex(strPath, &stAlarmRecordTimeInfo);
								bLastAlarmStatus = false;
							}


							/***************************切换录像模式的时候, 在这里退出全天录像********************************/
							if( DISK_RECORD_MODE_FULLTIME != m_iRecordMode )
							{
								break;
							}
							/***************************切换录像模式的时候, 在这里退出全天录像********************************/

							
							u64FileStartTime_ms = stStreamDataHeader.ullTimestamp;			//文件开始时间
							
							GetRecordFilePath(stStreamDataHeader.ullTimestamp, record_file_path, 128, DISK_RECORD_MODE_FULLTIME);
							InitMp4Muxer(record_file_path);
							snprintf(arrCurRecordFilePath, sizeof(arrCurRecordFilePath), record_file_path);
						}

#ifdef RECORD_DBG_PRNT_TIME
						s_ms = GetSystemUptime_ms();
#endif
						WriteToMp4Muxer(&u64FileStartTime_ms, 1, 1, &stStreamDataHeader.ullTimestamp, (UInt8 *)pData, iFrameSize);
#ifdef RECORD_DBG_PRNT_TIME
						e_ms = GetSystemUptime_ms();
						duration_ms = e_ms - s_ms;
						if (duration_ms > 1000)
							printf("record ---> video I takes %lld ms\n", duration_ms);
#endif

						
						u64Last_I_FrameTimestamp_ms = stStreamDataHeader.ullTimestamp;	//上一个I帧的时间戳
					}
					else
					{
#ifdef RECORD_DBG_PRNT_TIME
						s_ms = GetSystemUptime_ms();
#endif
						WriteToMp4Muxer(&u64FileStartTime_ms, 1, 2, &stStreamDataHeader.ullTimestamp, (UInt8 *)pData, iFrameSize);
#ifdef RECORD_DBG_PRNT_TIME
						e_ms = GetSystemUptime_ms();
						duration_ms = e_ms - s_ms;
						if (duration_ms > 1000)
							printf("record ---> video P takes %lld ms\n", duration_ms);
#endif
					}
					
					u64LastFrameTimestamp_ms = stStreamDataHeader.ullTimestamp;		//上一个帧的时间戳			
					
					if (false == bLastAlarmStatus && true == m_bStartAlarmRecord)
					{
						bLastAlarmStatus = true;
						stAlarmRecordTimeInfo.iStartTime = stStreamDataHeader.ullTimestamp/1000;
					}
					else if (true == bLastAlarmStatus && false == m_bStartAlarmRecord)
					{
						char strPath[128];
						stAlarmRecordTimeInfo.iEndTime = stStreamDataHeader.ullTimestamp/1000;;
						stAlarmRecordTimeInfo.iRecType = 1;
						GetAlarmRecIndexFilePath(stAlarmRecordTimeInfo.iStartTime, strPath);
						InsertlRecordFileIndex(strPath, &stAlarmRecordTimeInfo);
						bLastAlarmStatus = false;
					}
				}
			}
		}
	}
end:
//	if( pData )
//		delete [] pData;

	UnInitMp4Muxer();
	
	m_bRecordWorking = false;
}

/// 开启
bool CStorageManager::start()
{
	return CreateThread();
}

/// 停止
bool CStorageManager::stop()
{
	return DestroyThread(true);
}

/// 线程是否正在运行
bool CStorageManager::isRunning()
{
	return (!(IsThreadOver()));
}


//写录像数据
Int32 CStorageManager::WriteFrameData(unsigned char *pData, UInt32 uiSize, void *param)
{
	if( (NULL == pData) || (0 == uiSize) || (NULL == param) )
		return -1;

	int ret;
	stream_data_header_S *p_stream_data_header = (stream_data_header_S *)param;
	
	ret = m_avStreamRingBuffer.PutData( p_stream_data_header, (char *)pData, (int)uiSize);

//	printf("record ringbuffer stream duration : %d\n", m_avStreamRingBuffer.GetStreamDuration());

	return ret;
}

//清除录像缓存数据
Int32 CStorageManager::ClearFrameBuffer()
{
	int ret;
	
	ret = m_avStreamRingBuffer.ClearData();

	printf("clear record ringbuffer.\n");

	return ret;
}

int CStorageManager::GetTodayRecNum()
{
	int ret;
	int num = 0;
	time_t t = time(0); 
	struct tm tm = {0};
	char strPath[128];
	struct stat statbuf;
	
	localtime_r(&t, &tm );
	snprintf(strPath, sizeof(strPath), __STORAGE_SD_MOUNT_PATH__"/DCIM/%04d/%02d/%02d/index", 
		tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday);

	ret = lstat(strPath, &statbuf);
	if( 0 == ret )
	{
		num = statbuf.st_size / sizeof(record_file_info_s);
	}
	
	return num;
}

/*
 * @mode 1-year, 2-month, 3-day
 */
int CStorageManager::ConvertDateStrToInt(int mode, const char *strDate)
{
	int ret = -1;
	
	if( 1 == mode )
	{
		if( 4 != strlen(strDate) )
			return -1;
		for(int i = 0; strDate[i] != '\0'; i++)
		{
			if( strDate[i] < '0' || strDate[i] > '9' )
				return -1;
		}
		ret = atoi(strDate);

		return ret;
	}
	else if( 2 == mode || 3 == mode )
	{
		if( 2 != strlen(strDate) )
			return -1;
		for(int i = 0; strDate[i] != '\0'; i++)
		{
			if( strDate[i] < '0' || strDate[i] > '9' )
				return -1;
		}
		ret = atoi(strDate);

		return ret;
	}

	return ret;
}

int CStorageManager::ConvertYMDHMSToSecondInt(const char *pStrYMDHMS)
{
	char strTmp[8] = "";
	struct tm tm = {0};
	
	if( 14 != strlen(pStrYMDHMS ) )
		return -1;

	for(int i = 0; pStrYMDHMS[i] != '\0'; i++)
	{
		if( pStrYMDHMS[i] < '0' || pStrYMDHMS[i] > '9' )
			return -1;
	}

	//取年
	memset(strTmp, 0, sizeof(strTmp));
	strncpy(strTmp, pStrYMDHMS, 4);
	
	tm.tm_year = atoi(strTmp);
	tm.tm_year -= 1900;
	
	//取月
	memset(strTmp, 0, sizeof(strTmp));
	strncpy(strTmp, pStrYMDHMS+4, 2);
	
	tm.tm_mon = atoi(strTmp);
	tm.tm_mon -= 1;

	//取日
	memset(strTmp, 0, sizeof(strTmp));
	strncpy(strTmp, pStrYMDHMS+6, 2);
	
	tm.tm_mday = atoi(strTmp);

	//取时
	memset(strTmp, 0, sizeof(strTmp));
	strncpy(strTmp, pStrYMDHMS+8, 2);
	
	tm.tm_hour = atoi(strTmp);
	
	//取分
	memset(strTmp, 0, sizeof(strTmp));
	strncpy(strTmp, pStrYMDHMS+10, 2);
	
	tm.tm_min = atoi(strTmp);
	
	//取秒
	memset(strTmp, 0, sizeof(strTmp));
	strncpy(strTmp, pStrYMDHMS+12, 2);
	
	tm.tm_sec = atoi(strTmp);

	return mktime(&tm);
}

int CStorageManager::ParseRecordFile(const char *pStrFileName, int *piStatTime, int *piEndTime)
{
	int ret, len, iS_time, iE_time;
	char *pTmp_1 = NULL, *pTmp_2 = NULL;
	char strTmp[16] = "";

	if( !pStrFileName || strlen(pStrFileName) < 33 || !piStatTime || !piEndTime )
		return -1;

	len = strlen(pStrFileName);
	if( strncmp(pStrFileName+(len-4), ".mp4", 4) != 0 )
		return -2;
	
	pTmp_1 = strchr((char *)pStrFileName, '-');
	if( NULL == pTmp_1 )
		return -3;

	if( 14 != (pTmp_1-pStrFileName) )
		return -4;

	memset(strTmp, 0 , sizeof(strTmp));
	strncpy(strTmp, pStrFileName, 14);
	ret = ConvertYMDHMSToSecondInt(strTmp);
	if( ret < 0 )
		return -5;

	iS_time = ret;

	pTmp_1++; 	//跳到'-'的下一个字符

	pTmp_2 = strchr((char *)pStrFileName, '_');
	if( NULL == pTmp_2 )
	{
		pTmp_2 = strchr((char *)pStrFileName, '.');
		if( NULL == pTmp_2 )
			return -6;
	}

//	printf("pTmp_1 : %p, pTmp_1[0] : %c\n", pTmp_1, pTmp_1[0]);
//	printf("pTmp_2 : %p, pTmp_2[0] : %c\n", pTmp_2, pTmp_2[0]);
	if( 14 != (pTmp_2-pTmp_1) )
		return -7;
	
	memset(strTmp, 0 , sizeof(strTmp));
	strncpy(strTmp, pTmp_1, 14);
	ret = ConvertYMDHMSToSecondInt(strTmp);
	if( ret < 0 )
		return -8;
	
	iE_time = ret;

	*piStatTime = iS_time;
	*piEndTime = iE_time;

	return 0;
}

int CStorageManager::CheckIsTmpRecordFile(char *pStrFileName, unsigned long long *pullStartTimestamp, int *piRcdType)
{
	int iRcdType;
	char *pTmp_1 = NULL, *pTmp_2 = NULL;
	char strTmp[24] = "";

	if( !pStrFileName || strlen(pStrFileName) < 9 || !pullStartTimestamp || !piRcdType )
		return -1;

	if( strncmp(pStrFileName, "tmp_", 4) != 0 || 
		strncmp(pStrFileName+(strlen(pStrFileName)-4), ".mp4", 4) != 0)
		return -2;
	
	pTmp_1 = pStrFileName + strlen("tmp_");
	pTmp_2 = strchr(pTmp_1, '_');
	if( NULL == pTmp_2 )
	{
		pTmp_2 = strchr(pTmp_1, '.');
		if( NULL == pTmp_2 )
			return -3;
		iRcdType = DISK_RECORD_MODE_FULLTIME;
	}
	else
		iRcdType = DISK_RECORD_MODE_ALARM;

	strncpy(strTmp, pTmp_1, pTmp_2-pTmp_1);
	sscanf(strTmp, "%llu", pullStartTimestamp);

	*piRcdType = iRcdType;

	return 0;
}

/////////////////////////////////////////////////////////////////// 修复录像临时用 ///////////////////////////////////////////////////////////////////
int InitMp4Muxer_tmp(CMp4Muxer *pcMp4Muxer, const char *record_file_path, const char *spspps, int spspps_len, 
						int width, int height, int frame_rate, int bit_rate, int gop, STORAGE_VIDEO_ENC_TYPE_E m_eVideoEncType)
{
	AppInfo("record_file_path : %s\n", record_file_path);
	int ret;
	AVCodecContext v_codec, a_codec;

	memset(&v_codec, 0, sizeof(v_codec));
	memset(&a_codec, 0, sizeof(a_codec));

	if ( true == pcMp4Muxer->IsOpenFile() )
	{
		return -1;
	}
	
	v_codec.codec_type = AVMEDIA_TYPE_VIDEO;
	if (STORAGE_VIDEO_ENC_H264 == m_eVideoEncType)
		v_codec.codec_id = AV_CODEC_ID_H264;
	else
		v_codec.codec_id = AV_CODEC_ID_H265;
	v_codec.width = width;
	v_codec.height = height;
	v_codec.bit_rate = bit_rate;
	v_codec.flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	/* emit one intra frame every twelve frames at most */
	v_codec.gop_size = gop;
	v_codec.qmin =	20;
	v_codec.qmax =	51;
	v_codec.time_base = AV_TIME_BASE_Q;
	if ((spspps_len > 0) && (NULL != spspps))
	{
		/* extradata != NULL，mp4文件中nula开始码会被替换为nula长度 */
		v_codec.extradata = (uint8_t *)av_malloc(spspps_len);
		memcpy(v_codec.extradata, spspps, spspps_len);
		v_codec.extradata_size = spspps_len;
	}
	else
	{
		v_codec.extradata = NULL;
		v_codec.extradata_size = 0;
	}
	
	
	a_codec.codec_type	= AVMEDIA_TYPE_AUDIO;
	a_codec.sample_rate = 8000;
	a_codec.codec_id = AV_CODEC_ID_PCM_MULAW;//AV_CODEC_ID_AAC;
	a_codec.channels = 1;
	a_codec.extradata = NULL;
	a_codec.extradata_size = 0;
	a_codec.time_base = AV_TIME_BASE_Q;
	a_codec.flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	a_codec.sample_fmt = AV_SAMPLE_FMT_S16;
	a_codec.frame_size = 8000/8;
	a_codec.delay = 22;

	return pcMp4Muxer->mp4_mux_init(&v_codec, &a_codec, frame_rate, record_file_path);
}

int UnInitMp4Muxer_tmp(CMp4Muxer *pcMp4Muxer)
{
	if ( false == pcMp4Muxer->IsOpenFile() )
	{
		return -1;
	}
	
	return pcMp4Muxer->mp4_mux_uninit();
}

int WriteToMp4Muxer_tmp(CMp4Muxer *pcMp4Muxer, UInt64 *pu64FileStartTime_ms, int iStreamType, int iFrameType, UInt64 *pu64FrameTimestamp_ms, UInt8 *pFrameData, int iFrameSize)
{
	int ret;
	AVPacket packet;
	UInt64 u64Interval;
	
	if ( false == pcMp4Muxer->IsOpenFile() )
	{
		return -1;
	}

	if( 2 == iStreamType )		//audio
	{
		u64Interval = *pu64FrameTimestamp_ms - *pu64FileStartTime_ms;
//		printf("audio ---- %llu = %llu - %llu\n", u64Interval, *pu64FrameTimestamp_ms, *pu64FileStartTime_ms);

		av_init_packet(&packet);
		packet.data = (uint8_t *)pFrameData;	
		packet.pts = u64Interval * 1000; 	//转换成 us
		packet.dts = packet.pts;
//		printf("audio-------------------pts : %lld, dts : %lld\n", packet.pts, packet.dts);
		packet.size = iFrameSize;
	
		ret = pcMp4Muxer->mp4_mux_write(&packet, AVMEDIA_TYPE_AUDIO);	
		
		// 如果是用 av_write_frame , 则必须手动调用 av_packet_unref 释放 pkg , 不然会造成内存泄露
		// 如果是用 av_interleaved_write_frame , 则不需要, av_interleaved_write_frame 内部会自己释放
		av_packet_unref(&packet);		
	}
	else if( 1 == iStreamType )	//video
	{
		u64Interval = *pu64FrameTimestamp_ms - *pu64FileStartTime_ms;
//		printf("video ---- %llu = %llu - %llu\n", u64Interval, *pu64FrameTimestamp_ms, *pu64FileStartTime_ms);

		av_init_packet(&packet);
		packet.data = (uint8_t *)pFrameData; 
		packet.pts = u64Interval * 1000;  //转换成 us
		packet.dts = packet.pts;	
//		printf("video-------------------pts : %lld, dts : %lld\n", packet.pts, packet.dts);
		packet.size = iFrameSize;
		
		if( 1 == iFrameType )	//I帧
			packet.flags |= AV_PKT_FLAG_KEY;
				
		ret = pcMp4Muxer->mp4_mux_write(&packet, AVMEDIA_TYPE_VIDEO);

		// 如果是用 av_write_frame , 则必须手动调用 av_packet_unref 释放 pkg , 不然会造成内存泄露
		// 如果是用 av_interleaved_write_frame , 则不需要, av_interleaved_write_frame 内部会自己释放
		av_packet_unref(&packet);		
	}

	return 0;
}
/////////////////////////////////////////////////////////////////// 修复录像临时用 ///////////////////////////////////////////////////////////////////
int CStorageManager::RecoverRecordFile(unsigned long long *pullFileStartTime_ms, char *strOldPath, char *strNewPath, unsigned long long *pullFileEndTime_ms)
{
	int ret;
	int result = -1;
	struct stat statbuf;
	unsigned char *pPos = NULL;
	unsigned char buff[STORAGE_MAX_FRAME_SIZE];
	unsigned char *recover_buff = NULL;
	bool bFoundMdat = false;
	unsigned int uiFrameNum = 0;
	char ch;
	FILE *fp = NULL;
	int len;
	int iRecoverFileLen;
	char strRecoverFile[128];
	FILE *fp_recover = NULL;
	RecoverParam_S *pstRecoverParam = NULL;
	unsigned long long ullLastFrameTimestamp = 0;
	char h264_start_code[4] = {0x00, 0x00, 0x00, 0x01};
	CMp4Muxer cMp4Muxer;

	if( !strOldPath || !strNewPath || !pullFileEndTime_ms )
		return -1;

	snprintf(strRecoverFile, 128, "%s-recover", strOldPath);

	ret = lstat(strRecoverFile, &statbuf);
	if( 0 != ret )
		return -1;

	iRecoverFileLen = statbuf.st_size;
	recover_buff = (unsigned char *)malloc(iRecoverFileLen);
	if( NULL == recover_buff )
		goto end;
	
	fp_recover = fopen(strRecoverFile, "r");
	if( !fp_recover )
		goto end;

	iRecoverFileLen = disk_fread_file(fp_recover, (char *)recover_buff, iRecoverFileLen);
	if( iRecoverFileLen <= 0 )
	{
		AppErr("read recover file failed.\n");
		goto end;
	}
			
	fp = fopen(strOldPath, "rb");
	if( !fp )
	{
		AppErr("open mp4 file failed.\n");
		goto end;
	}

	//查找"mdat"的位置
	while(1)
	{
		ret = fread(&ch, 1, 1, fp);
		if( 1 == ret )
		{
			if('m' == ch)
			{
				ret = fread(&ch, 1, 1, fp);
				if( 1 == ret )
				{
					if('d' == ch)
					{
						ret = fread(&ch, 1, 1, fp);
						if( 1 == ret )
						{
							if('a' == ch)
							{
								ret = fread(&ch, 1, 1, fp);
								if( 1 == ret )
								{
									if('t' == ch)
									{
										AppInfo("found mdat. pos : %x.\n", ftell(fp) - 4);
										bFoundMdat = true;
										break;
									}
									else
										continue;
								}
								else
									break;
							}
							else
								continue;
						}
						else
							break;
					}
					else
						continue;
				}
				else
					break;
			}
			else
				continue;
		}
		else
			break;
	}

	if( false == bFoundMdat )
	{
		AppErr("not found mdat.\n");
		goto end;
	}

	ret = InitMp4Muxer_tmp(&cMp4Muxer, strNewPath, m_arrSpsPps, m_iSpsPpsLen, 
							m_iWidth, m_iHeight, m_iFrameRate, m_iBitRate, m_iGop, m_eVideoEncType);
	if( 0 != ret )
	{
		AppErr("InitMp4Muxer failed.\n");
		goto end;
	}

	uiFrameNum = 0;
	len = 0;
	while( len <= (iRecoverFileLen-sizeof(RecoverParam_S)) )
	{
		pstRecoverParam = (RecoverParam_S *)(recover_buff+len);
		len += sizeof(RecoverParam_S);
		
//		printf("type [%d], size : %d\n", pstRecoverParam->type, pstRecoverParam->size);
		if( (pstRecoverParam->size > STORAGE_MAX_FRAME_SIZE) )
			break;

		if( ((0 == pstRecoverParam->type) || (1 == pstRecoverParam->type)) && 
			(ullLastFrameTimestamp > pstRecoverParam->timestamp) )
			break;

		if( 0 == pstRecoverParam->type )
		{
			ret = disk_fread_file(fp, (char *)buff, pstRecoverParam->size);
//			printf("frame[%u], read frame size : %d\n", uiFrameNum+1, ret);
			if( ret != pstRecoverParam->size )
				break;

//			printf("timestamp : %llu\n", pstRecoverParam->timestamp);
			if (STORAGE_VIDEO_ENC_H264 == m_eVideoEncType)
			{
				//因为mp4文件中h264的开始码（00 00 00 01）被改成了nalu的长度了
				memcpy(buff, m_arrSpsPps, m_iSpsPpsLen);
				memcpy(buff+m_iSpsPpsLen, h264_start_code, 4);
			}
			WriteToMp4Muxer_tmp(&cMp4Muxer, pullFileStartTime_ms, 1, 1, &pstRecoverParam->timestamp, (UInt8 *)buff, pstRecoverParam->size);
			ullLastFrameTimestamp = pstRecoverParam->timestamp;
			uiFrameNum++;
		}
		else if( 1 == pstRecoverParam->type )
		{
			ret = disk_fread_file(fp, (char *)buff, pstRecoverParam->size);
//			printf("frame[%u], read frame size : %d\n", uiFrameNum+1, ret);
			if( ret != pstRecoverParam->size )
				break;
			
//			printf("timestamp : %llu\n", pstRecoverParam->timestamp);
			if (STORAGE_VIDEO_ENC_H264 == m_eVideoEncType)
			{
				//因为mp4文件中h264的开始码（00 00 00 01）被改成了nalu的长度了
				memcpy(buff, h264_start_code, 4);
			}
			WriteToMp4Muxer_tmp(&cMp4Muxer, pullFileStartTime_ms, 1, 2, &pstRecoverParam->timestamp, (UInt8 *)buff, pstRecoverParam->size);
			ullLastFrameTimestamp = pstRecoverParam->timestamp;
			uiFrameNum++;
		}
		else //( 2 == stRecoverParam.type )
		{
			#if 0
			ret = disk_fread_file(fp, (char *)buff+7, pstRecoverParam->size-7);
//			printf("frame[%u], read frame size : %d\n", uiFrameNum+1, ret);
			if( ret != pstRecoverParam->size-7 )
				break;
			
			unsigned short frame_len = (unsigned short)(pstRecoverParam->size);
			frame_len &= 0x1FFF;
			//aac adts头
			buff[0] = 0xFF;
			buff[1] = 0xF9;
			buff[2] = 0x6C;
			buff[3] = 0x40 | (frame_len >> 11);
			buff[4] = (frame_len >> 3);
			buff[5] = (frame_len << 5) | 0x1F;
			buff[6] = 0xFC;
			#else
			ret = disk_fread_file(fp, (char *)buff, pstRecoverParam->size);
//			printf("frame[%u], read frame size : %d\n", uiFrameNum+1, ret);
			if (ret != pstRecoverParam->size)
				break;
			#endif

//			printf("timestamp : %llu\n", pstRecoverParam->timestamp);
			WriteToMp4Muxer_tmp(&cMp4Muxer, pullFileStartTime_ms, 2, 0, &pstRecoverParam->timestamp, (UInt8 *)buff, pstRecoverParam->size);
//<shang>			ullLastFrameTimestamp = pstRecoverParam->timestamp;
			uiFrameNum++;
		}
	}
	
	//关闭当前文件
	UnInitMp4Muxer_tmp(&cMp4Muxer);

	if( 0 != ullLastFrameTimestamp )
	{
		*pullFileEndTime_ms = ullLastFrameTimestamp;
		result = 0;
	}
end:
	if( fp_recover )
		fclose(fp_recover);
	fp_recover = NULL;
	
	if( fp )
		fclose(fp);
	fp = NULL;

	if(recover_buff)
		free(recover_buff);
	recover_buff = NULL;
	
	return result;
}

void CStorageManager::DoRecoverRcdProc(RecoverThradParam_S *pstParam)
{
	int ret;
	int iStartTime;
	int date_index;
	unsigned long long ullStartTimestamp, ullEndTimestamp;
	record_file_info_s stRecordFileInfo = {0};
	char strPath[128] = {0};
	char strOldPath[128] = {0};
	char strNewPath[128] = {0};
	
	snprintf(strOldPath, sizeof(strOldPath), "%s%s", pstParam->filePath, pstParam->fileName);
	snprintf(strNewPath, sizeof(strNewPath), "%s-new", strOldPath);

	date_index = pstParam->date_index;
	ullStartTimestamp = pstParam->ullStartTimestamp;


	/* debug --->*/
	struct timeval tv = {0};
	Int64 i64StartRecover_ms, i64EndRecover_ms;
	gettimeofday(&tv, NULL);
	i64StartRecover_ms = ((unsigned long long)tv.tv_sec * 1000 + tv.tv_usec / 1000);
	/* <--- debug */
	
	ret = RecoverRecordFile(&ullStartTimestamp, strOldPath, strNewPath, &ullEndTimestamp);
	
	/* debug --->*/
	gettimeofday(&tv, NULL);
	i64EndRecover_ms = ((unsigned long long)tv.tv_sec * 1000 + tv.tv_usec / 1000);
	AppErr("recover tmp mp4 file take %lld ms\n", (i64EndRecover_ms-i64StartRecover_ms));
	/* <--- debug */
	
	//删除临时录像文件
	AppErr("delete record file : %s\n", strOldPath);
	remove(strOldPath);
	snprintf(strPath, 128, "%s-recover", strOldPath);
	AppErr("delete record file : %s\n", strPath);
	remove(strPath);	
	
	if( (0 == ret) && (ullEndTimestamp > ullStartTimestamp) )
	{
		//获取当前录像文件的实际名字
		GetFilePathBaseOnTimestamp(&ullStartTimestamp, &ullEndTimestamp, strPath, sizeof(strPath), pstParam->iRecordMode);
		
		//改名
		AppErr("rename %s to %s\n", strNewPath, strPath);
		rename(strNewPath, strPath);

		snprintf(strPath, sizeof(strPath), "%s/index", pstParam->filePath);
		stRecordFileInfo.iStartTime = ullStartTimestamp / 1000;
		stRecordFileInfo.iEndTime = ullEndTimestamp / 1000;
		if( strstr(pstParam->fileName, "_ALARM") )
			stRecordFileInfo.iRecType = 1;
		else
			stRecordFileInfo.iRecType = 0;
		InsertlRecordFileIndex(strPath, &stRecordFileInfo);
	}
	
	AppErr("delete record file : %s\n", strNewPath);
	remove(strNewPath);	
}

int dir_filter_year(const struct dirent *dir)
{
	if( !(dir->d_type & DT_DIR) )
	{
		return 0;
	}
	
	if( strcmp(".", dir->d_name) == 0 || strcmp("..", dir->d_name) == 0 )
	{
		return 0;
	}
	
	if( 4 != strlen(dir->d_name) )
		return 0;
	for(int i = 0; dir->d_name[i] != '\0'; i++)
	{
		if( dir->d_name[i] < '0' || dir->d_name[i] > '9' )
			return 0;
	}
	
	return 1;
}
int dir_filter_month(const struct dirent *dir)
{
	if( !(dir->d_type & DT_DIR) )
	{
		return 0;
	}
	
	if( strcmp(".", dir->d_name) == 0 || strcmp("..", dir->d_name) == 0 )
	{
		return 0;
	}

	if( 2 != strlen(dir->d_name) )
		return 0;

	if( strcmp(dir->d_name, "01") < 0 || 
		strcmp(dir->d_name, "12") > 0 )
		return 0;
	
	return 1;
}

int dir_filter_day(const struct dirent *dir)
{
	if( !(dir->d_type & DT_DIR) )
	{
		return 0;
	}
	
	if( strcmp(".", dir->d_name) == 0 || strcmp("..", dir->d_name) == 0 )
	{
		return 0;
	}

	if( 2 != strlen(dir->d_name) )
		return 0;

	if( strcmp(dir->d_name, "01") < 0 || 
		strcmp(dir->d_name, "31") > 0 )
		return 0;
	
	return 1;
}

int dir_filter_4(const struct dirent *dir)
{
	if( !(dir->d_type & DT_DIR) )
	{
		return 0;
	}
	
	if( strcmp(dir->d_name, "Alarm") != 0 && strcmp(dir->d_name, "Timing") != 0 )
	{
		return 0;
	}
	
	return 1;
}

int dir_filter_file(const struct dirent *dir)
{
	#if 0
	if( dir->d_type & DT_DIR )
	{
		return 0;
	}
		
	return 1;
	#else
	if( !(dir->d_type & DT_REG) )
	{
		return 0;
	}
		
	return 1;
	#endif
}

int dir_filter_tmp_file(const struct dirent *dir)
{
	if (!(dir->d_type & DT_REG))
	{
		return 0;
	}

	if (strncmp(dir->d_name, "tmp_", strlen("tmp_")))
	{
		return 0;
	}

	return 1;
}

int dir_compare(const struct dirent **dir_1, const struct dirent **dir_2)
{
	return strcmp(dir_1[0]->d_name, dir_2[0]->d_name);
}

int dir_compare_desc(const struct dirent **dir_1, const struct dirent **dir_2)
{
	return strcmp(dir_2[0]->d_name, dir_1[0]->d_name);
}


//创建录像文件索引
static record_file_info_s s_astCreateRecIndexInfo[STORAGE_MAX_FILE_PER_DAY];
int CStorageManager::CreateRecordFileIndex()
{
	int ret;
	char strPath[128] = "";
	char strIndexPath[128] = "";
	int iYear, iMonth, iDay;
    struct dirent **ppYearEntry = NULL, **ppMonthEntry = NULL, **ppDayEntry = NULL;
	int iYearIndex = 0, iMonthIndex = 0, iDayIndex = 0;

    struct dirent **entry = NULL;
    int iEntryIndex = 0;

	record_file_info_s stRecordFileInfo = {0};

	int file_count = 0;
	struct timeval tv_before;
	struct timeval tv_after;
	gettimeofday(&tv_before, NULL);

	CGuard guard(m_mutex);

	snprintf(strPath, sizeof(strPath), __STORAGE_SD_MOUNT_PATH__"/DCIM/");
	int nEntry = scandir(strPath, &entry, dir_filter_year, dir_compare);
	iEntryIndex = 0;
	while(iEntryIndex < nEntry)
    {
		iYear = ConvertDateStrToInt(1, entry[iEntryIndex]->d_name);
		if( iYear > 0 )
		{
			snprintf(strPath, sizeof(strPath), __STORAGE_SD_MOUNT_PATH__"/DCIM/%s/", entry[iEntryIndex]->d_name);
			int nYear = scandir(strPath, &ppYearEntry, dir_filter_month, dir_compare);
			iYearIndex = 0;
			while(iYearIndex < nYear)				
			{
				iMonth = ConvertDateStrToInt(2, ppYearEntry[iYearIndex]->d_name);
				if( iMonth > 0 )
				{
					snprintf(strPath, sizeof(strPath), __STORAGE_SD_MOUNT_PATH__"/DCIM/%s/%s", entry[iEntryIndex]->d_name, ppYearEntry[iYearIndex]->d_name);
					int nMonth = scandir(strPath, &ppMonthEntry, dir_filter_day, dir_compare);
					iMonthIndex = 0;
					while(iMonthIndex < nMonth)
					{
						iDay = ConvertDateStrToInt(3, ppMonthEntry[iMonthIndex]->d_name);
						if( iDay > 0 )
						{
							#if 0
							snprintf(strPath, sizeof(strPath), __STORAGE_SD_MOUNT_PATH__"/DCIM/%s/%s/%s/index", entry[iEntryIndex]->d_name, ppYearEntry[iYearIndex]->d_name, ppMonthEntry[iMonthIndex]->d_name);
							int fd_index = open(strPath, O_WRONLY | O_CREAT | O_TRUNC);
							if (-1 == fd_index)
							{
								free(ppMonthEntry[iMonthIndex]);
								iMonthIndex++;
								continue;
							}
							#else
							int pos = 0;
							int file_size = 0;
							int index_data_num = 0;
							int index_data_max_num = STORAGE_MAX_FILE_PER_DAY;
							record_file_info_s *pastCreateRecIndexInfo = s_astCreateRecIndexInfo;
							snprintf(strIndexPath, sizeof(strIndexPath), __STORAGE_SD_MOUNT_PATH__"/DCIM/%s/%s/%s/index", entry[iEntryIndex]->d_name, ppYearEntry[iYearIndex]->d_name, ppMonthEntry[iMonthIndex]->d_name);
							int fd_index = open(strIndexPath, O_RDONLY);
							if (-1 != fd_index)
							{
								ret = disk_read_file(fd_index, (char *)&s_astCreateRecIndexInfo,	sizeof(s_astCreateRecIndexInfo));
								if (ret > 0)
								{
									//printf("read %s ret: %d\n", strPath, ret);
									file_size = ret;
									index_data_num = ret / sizeof(record_file_info_s);
								}
								close(fd_index);
							}
							//AppErr("index_data_num: %d\n", index_data_num);
							while(0 == pastCreateRecIndexInfo->iStartTime && pos < index_data_num)
							{
								pos++;
								pastCreateRecIndexInfo++;
								index_data_max_num--;
							}
							//printf("invalid pos: %d\n", pos);
							index_data_num -= pos;
							#endif
							
							snprintf(strPath, sizeof(strPath), __STORAGE_SD_MOUNT_PATH__"/DCIM/%s/%s/%s/", entry[iEntryIndex]->d_name, ppYearEntry[iYearIndex]->d_name, ppMonthEntry[iMonthIndex]->d_name);
							int nDay = scandir(strPath, &ppDayEntry, dir_filter_file, dir_compare);
							iDayIndex = 0;
							pos = 0;
							bool bNeedWrite = false;
							while(iDayIndex < nDay)									
							{
								memset(&stRecordFileInfo, 0, sizeof(stRecordFileInfo));
								
								ret = ParseRecordFile(ppDayEntry[iDayIndex]->d_name, &stRecordFileInfo.iStartTime, &stRecordFileInfo.iEndTime);
								if( (0 == ret) && (stRecordFileInfo.iEndTime >= stRecordFileInfo.iStartTime) )
								{
									if( strstr(ppDayEntry[iDayIndex]->d_name, "_ALARM") )
										stRecordFileInfo.iRecType = 1;
									else
										stRecordFileInfo.iRecType = 0;

									#if 0
									//disk_write_file(fd_index, (char *)&stRecordFileInfo, sizeof(stRecordFileInfo));
									if(pos < 1000)
										memcpy(&s_astCreateRecIndexInfo[pos++], &stRecordFileInfo, sizeof(stRecordFileInfo));
									#else
									if(pos < index_data_max_num)
									{
										if (bNeedWrite || 
											memcmp(&pastCreateRecIndexInfo[pos], &stRecordFileInfo, sizeof(stRecordFileInfo)))
										{
											memcpy(&pastCreateRecIndexInfo[pos], &stRecordFileInfo, sizeof(stRecordFileInfo));
											bNeedWrite = true;
										}
										pos++;
									}
									#endif
									file_count++;
								}
								
								free(ppDayEntry[iDayIndex]);
								iDayIndex++;
								usleep(500); //ale
							}

							#if 0
							disk_write_file(fd_index, (char *)s_astCreateRecIndexInfo, sizeof(stRecordFileInfo)*pos);
							fdatasync(fd_index);
							close(fd_index);
							#else
							if (bNeedWrite)
							{
								//lseek(fd_index, 0, SEEK_SET);
								fd_index = open(strIndexPath, O_WRONLY | O_CREAT | O_TRUNC);
								if (-1 != fd_index)
								{
									disk_write_file(fd_index, (char *)pastCreateRecIndexInfo, sizeof(stRecordFileInfo)*pos);
									fdatasync(fd_index);
									close(fd_index);
								}
							}
							#endif

							if(ppDayEntry)
								free(ppDayEntry);									
						}
						
						free(ppMonthEntry[iMonthIndex]);
						iMonthIndex++;

					}

					if(ppMonthEntry)
						free(ppMonthEntry);
				}
				
				
				free(ppYearEntry[iYearIndex]);
				iYearIndex++;
			}

			if(ppYearEntry)
				free(ppYearEntry);
		}
		
		
		free(entry[iEntryIndex]);
		iEntryIndex++;
    }

	if(entry)
		free(entry);
	
	gettimeofday(&tv_after, NULL);
	unsigned long long s_ms = (unsigned long long)tv_before.tv_sec * 1000 + tv_before.tv_usec / 1000;
	unsigned long long e_ms = (unsigned long long)tv_after.tv_sec * 1000 + tv_after.tv_usec / 1000;
	printf("CreateRecordFileIndex ---> file num: %d,	takes %lld ms\n", file_count, (e_ms - s_ms));

	return 0;
}

int CStorageManager::RecoverTmpRecordFile()
{
	int ret;
	char strPath[128] = "";
	int iYear, iMonth, iDay;
	int iTmpDateIndex;
    struct dirent **ppYearEntry = NULL, **ppMonthEntry = NULL, **ppDayEntry = NULL;
	int iYearIndex = 0, iMonthIndex = 0, iDayIndex = 0;
	struct stat statbuf;

    struct dirent **entry = NULL;
    int iEntryIndex = 0;

	//如果是H264编码，并且没有sps pps的话，不执行修复操作
	if ((STORAGE_VIDEO_ENC_H264 == m_eVideoEncType) && (m_iSpsPpsLen <= 0))
	{
		return -1;
	}

	snprintf(strPath, sizeof(strPath), __STORAGE_SD_MOUNT_PATH__"/DCIM/");
	int nEntry = scandir(strPath, &entry, dir_filter_year, dir_compare_desc);
	iEntryIndex = 0;
	while(iEntryIndex < nEntry)
    {
    	if (iEntryIndex != 0)
    	{
			free(entry[iEntryIndex]);
			iEntryIndex++;
			continue;
		}
		iYear = ConvertDateStrToInt(1, entry[iEntryIndex]->d_name);
		if( iYear > 0 )
		{
			snprintf(strPath, sizeof(strPath), __STORAGE_SD_MOUNT_PATH__"/DCIM/%s/", entry[iEntryIndex]->d_name);
			int nYear = scandir(strPath, &ppYearEntry, dir_filter_month, dir_compare_desc);
			iYearIndex = 0;
			while(iYearIndex < nYear)
			{
				if (iYearIndex != 0)
				{
					free(ppYearEntry[iYearIndex]);
					iYearIndex++;
					continue;
				}
				iMonth = ConvertDateStrToInt(2, ppYearEntry[iYearIndex]->d_name);
				if( iMonth > 0 )
				{
					snprintf(strPath, sizeof(strPath), __STORAGE_SD_MOUNT_PATH__"/DCIM/%s/%s", entry[iEntryIndex]->d_name, ppYearEntry[iYearIndex]->d_name);
					int nMonth = scandir(strPath, &ppMonthEntry, dir_filter_day, dir_compare_desc);
					iMonthIndex = 0;
					while(iMonthIndex < nMonth)
					{
						if (iMonthIndex != 0)
						{
							free(ppMonthEntry[iMonthIndex]);
							iMonthIndex++;
							continue;
						}
						iDay = ConvertDateStrToInt(3, ppMonthEntry[iMonthIndex]->d_name);
						if( iDay > 0 )
						{							
							snprintf(strPath, sizeof(strPath), __STORAGE_SD_MOUNT_PATH__"/DCIM/%s/%s/%s/", entry[iEntryIndex]->d_name, ppYearEntry[iYearIndex]->d_name, ppMonthEntry[iMonthIndex]->d_name);
							int nDay = scandir(strPath, &ppDayEntry, dir_filter_tmp_file, dir_compare);
							iDayIndex = 0;
							while(iDayIndex < nDay)
							{
								int iRcdType;
								unsigned long long ullStartTimestamp, ullEndTimestamp;
								ret = CheckIsTmpRecordFile(ppDayEntry[iDayIndex]->d_name, &ullStartTimestamp, &iRcdType);
								if( 0 == ret )
								{
									RecoverThradParam_S *pstParam = new RecoverThradParam_S;
									if( pstParam )
									{
										pstParam->p = this;
										pstParam->iRecordMode = iRcdType;
										pstParam->date_index = iTmpDateIndex;
										pstParam->ullStartTimestamp = ullStartTimestamp;
										snprintf(pstParam->filePath, sizeof(pstParam->filePath), __STORAGE_SD_MOUNT_PATH__ "/DCIM/%s/%s/%s/", entry[iEntryIndex]->d_name, ppYearEntry[iYearIndex]->d_name, ppMonthEntry[iMonthIndex]->d_name);
										snprintf(pstParam->fileName, sizeof(pstParam->fileName), "%s", ppDayEntry[iDayIndex]->d_name);
										printf("recover : %s/%s\n", pstParam->filePath, pstParam->fileName);
										CreateDetachedThread((char*)"thread_recover_record",thread_recover_record, (void *)pstParam, true);
									}
								}
								
								free(ppDayEntry[iDayIndex]);
								iDayIndex++;
							}

							if(ppDayEntry)
								free(ppDayEntry);									
						}
						
						free(ppMonthEntry[iMonthIndex]);
						iMonthIndex++;

					}

					if(ppMonthEntry)
						free(ppMonthEntry);
				}
				
				
				free(ppYearEntry[iYearIndex]);
				iYearIndex++;
			}

			if(ppYearEntry)
				free(ppYearEntry);
		}
		
		
		free(entry[iEntryIndex]);
		iEntryIndex++;
    }

	if(entry)
		free(entry);

	return 0;
}

int CStorageManager::GetLastFrameTimestamp(const char *strPath)
{
	int ret;
	int num;
	int start_pos;
	int file_size = 0;
	int timestamp = 0;
	RecoverParam_S stRecoverParam;
	
	int fd_index = open(strPath, O_RDONLY);
	if (-1 == fd_index)
	{
		AppErr("open %s failed. errno: %d\n", strPath, errno);
		goto end;
	}

	file_size = lseek(fd_index, 0, SEEK_END);
	if (-1 == file_size)
	{
		AppErr("lseek %s failed. errno: %d\n", strPath, errno);
		goto end;
	}

	num = file_size / sizeof(RecoverParam_S);
	start_pos = num > 0 ? ((num-1) * sizeof(RecoverParam_S)) : 0;
	AppInfo("num: %d, start_pos: %d\n", num, start_pos);
	ret = lseek(fd_index, start_pos, SEEK_SET);
	if (-1 == ret)
	{
		AppErr("lseek %s failed. errno: %d\n", strPath, errno);
		goto end;
	}

	ret = disk_read_file(fd_index, (char *)&stRecoverParam,  sizeof(stRecoverParam));
	if (ret != sizeof(stRecoverParam))
	{
		AppErr("read failed. ret: %d\n", ret);
		goto end;
	}
	AppInfo("timestamp: %llu, size: %d, type: %d\n", stRecoverParam.timestamp, stRecoverParam.size, stRecoverParam.type);
	timestamp = stRecoverParam.timestamp / 1000;

end:
	if (-1 != fd_index)
		close(fd_index);
	return timestamp;
}
int CStorageManager::GetTmpRecordFileLatestTime()
{
	int ret;
	bool bFound = false;
	int timestamp = 0;
	char strPath[128] = "";
	int iYear, iMonth, iDay;
    struct dirent **ppYearEntry = NULL, **ppMonthEntry = NULL, **ppDayEntry = NULL;
	int iYearIndex = 0, iMonthIndex = 0, iDayIndex = 0;

    struct dirent **entry = NULL;
    int iEntryIndex = 0;

	snprintf(strPath, sizeof(strPath), __STORAGE_SD_MOUNT_PATH__"/DCIM/");
	int nEntry = scandir(strPath, &entry, dir_filter_year, dir_compare_desc);
	iEntryIndex = 0;
	while(iEntryIndex < nEntry)
    {
    	if (iEntryIndex != 0)
    	{
			free(entry[iEntryIndex]);
			iEntryIndex++;
			continue;
		}
		iYear = ConvertDateStrToInt(1, entry[iEntryIndex]->d_name);
		if( iYear > 0 )
		{
			snprintf(strPath, sizeof(strPath), __STORAGE_SD_MOUNT_PATH__"/DCIM/%s/", entry[iEntryIndex]->d_name);
			int nYear = scandir(strPath, &ppYearEntry, dir_filter_month, dir_compare_desc);
			iYearIndex = 0;
			while(iYearIndex < nYear)
			{
				if (iYearIndex != 0)
				{
					free(ppYearEntry[iYearIndex]);
					iYearIndex++;
					continue;
				}
				iMonth = ConvertDateStrToInt(2, ppYearEntry[iYearIndex]->d_name);
				if( iMonth > 0 )
				{
					snprintf(strPath, sizeof(strPath), __STORAGE_SD_MOUNT_PATH__"/DCIM/%s/%s", entry[iEntryIndex]->d_name, ppYearEntry[iYearIndex]->d_name);
					int nMonth = scandir(strPath, &ppMonthEntry, dir_filter_day, dir_compare_desc);
					iMonthIndex = 0;
					while(iMonthIndex < nMonth)
					{
						if (iMonthIndex != 0)
						{
							free(ppMonthEntry[iMonthIndex]);
							iMonthIndex++;
							continue;
						}
						iDay = ConvertDateStrToInt(3, ppMonthEntry[iMonthIndex]->d_name);
						if( iDay > 0 )
						{							
							snprintf(strPath, sizeof(strPath), __STORAGE_SD_MOUNT_PATH__"/DCIM/%s/%s/%s/", entry[iEntryIndex]->d_name, ppYearEntry[iYearIndex]->d_name, ppMonthEntry[iMonthIndex]->d_name);
							int nDay = scandir(strPath, &ppDayEntry, dir_filter_tmp_file, dir_compare);
							iDayIndex = 0;
							while(iDayIndex < nDay)
							{
								if (false == bFound && strstr(ppDayEntry[iDayIndex]->d_name, "-recover"))
								{
									snprintf(strPath, sizeof(strPath), __STORAGE_SD_MOUNT_PATH__"/DCIM/%s/%s/%s/%s", 
												entry[iEntryIndex]->d_name, ppYearEntry[iYearIndex]->d_name, 
												ppMonthEntry[iMonthIndex]->d_name, ppDayEntry[iDayIndex]->d_name);
									ret = GetLastFrameTimestamp(strPath);
									AppInfo("timestamp: %d\n", ret);
									if (ret > 0)
									{
										timestamp = ret;
										bFound = true;
									}
								}
								
								free(ppDayEntry[iDayIndex]);
								iDayIndex++;
							}

							if(ppDayEntry)
								free(ppDayEntry);									
						}
						
						free(ppMonthEntry[iMonthIndex]);
						iMonthIndex++;

					}

					if(ppMonthEntry)
						free(ppMonthEntry);
				}
				
				
				free(ppYearEntry[iYearIndex]);
				iYearIndex++;
			}

			if(ppYearEntry)
				free(ppYearEntry);
		}
		
		
		free(entry[iEntryIndex]);
		iEntryIndex++;
    }

	if(entry)
		free(entry);

	return timestamp;
}


//删除空的录像目录
void CStorageManager::CleanEmptyRecordDir()
{
	int ret;
	char strPath[128] = "";
	int iYear, iMonth, iDay;
	DIR *pDirYear = NULL, *pDirMonth = NULL, *pDirDay = NULL;
	struct dirent *pYearEntry = NULL, *pMonthEntry = NULL, *pDayEntry = NULL;

	DIR * dir = NULL; //录像根目录
	struct dirent * entry = NULL;

	if( ( dir = opendir(__STORAGE_SD_MOUNT_PATH__"/DCIM/") ) ==  NULL )
		return ;
	
	while( ( entry = readdir(dir) ) != NULL )
	{
		if(entry->d_type & DT_DIR)
		{
			if( strcmp(".", entry->d_name) == 0 || strcmp("..", entry->d_name) == 0 )
			{
				continue;
			}
			
			iYear = ConvertDateStrToInt(1, entry->d_name);
			if( iYear > 0 )
			{
				bool bIsYearDirEmpty = true;
				char strYearPath[128] = "";
				snprintf(strYearPath, sizeof(strYearPath), __STORAGE_SD_MOUNT_PATH__"/DCIM/%s/", entry->d_name);
				if( ( pDirYear = opendir(strYearPath) ) ==	NULL )
				{
					continue;
				}
				while( ( pYearEntry = readdir(pDirYear) ) != NULL )
				{
					if(pYearEntry->d_type & DT_DIR)
					{
						if( strcmp(".", pYearEntry->d_name) == 0 || strcmp("..", pYearEntry->d_name) == 0 )
						{
							continue;
						}
						
						iMonth = ConvertDateStrToInt(2, pYearEntry->d_name);
						if( iMonth > 0 )
						{
							bool bIsMonDirEmpty = true;
							char strMonPath[128] = "";
							snprintf(strMonPath, sizeof(strMonPath), __STORAGE_SD_MOUNT_PATH__"/DCIM/%s/%s", entry->d_name, pYearEntry->d_name);
							if( ( pDirMonth = opendir(strMonPath) ) == NULL )
							{
								bIsYearDirEmpty = false; 	//不轻易删除, 只有确保真正为空时才删除
								continue;
							}
							
							while( ( pMonthEntry = readdir(pDirMonth) ) != NULL )
							{
								if(pMonthEntry->d_type & DT_DIR)
								{
									if( strcmp(".", pMonthEntry->d_name) == 0 || strcmp("..", pMonthEntry->d_name) == 0 )
									{
										continue;
									}

									iDay = ConvertDateStrToInt(3, pMonthEntry->d_name);
									if( iDay > 0 )
									{
										bool bIsDayDirEmpty = true;
										char strDayPath[128] = "";
										snprintf(strDayPath, sizeof(strDayPath), __STORAGE_SD_MOUNT_PATH__"/DCIM/%s/%s/%s/", entry->d_name, pYearEntry->d_name, pMonthEntry->d_name);
										if( ( pDirDay = opendir(strDayPath) ) == NULL )
										{
											bIsMonDirEmpty = false; 	//不轻易删除, 只有确保真正为空时才删除
											continue;
										}
										
										while( ( pDayEntry = readdir(pDirDay) ) != NULL )
										{
											if(pDayEntry->d_type & DT_REG)
											{
												bIsDayDirEmpty =false;
												break;
											}
										}
										closedir(pDirDay);	//

										//如果天文件夹没有文件, 则删除该文件夹
										if( true == bIsDayDirEmpty )
										{
											//删除文件夹
											char cmd[128] = "";
											snprintf(cmd, 128, "rm -rf %s", strDayPath);
											AppErr("remove %s\n", strDayPath);
//											__STORAGE_SYSTEM__(cmd);
											__STORAGE_SYSTEM__("sh", "sh", "-c", cmd, NULL);
										}
										else
										{
											bIsMonDirEmpty = false; 	//只要有一个就不是空的了
										}
									}
								}
								
							}
							closedir(pDirMonth);	//

							//月文件夹
							if( true == bIsMonDirEmpty )
							{
								//删除文件夹
								char cmd[128] = "";
								snprintf(cmd, 128, "rm -rf %s", strMonPath);
								AppErr("remove %s\n", strMonPath);
//								__STORAGE_SYSTEM__(cmd);
								__STORAGE_SYSTEM__("sh", "sh", "-c", cmd, NULL);
							}
							else
							{
								bIsYearDirEmpty = false; 	//只要有一个就不是空的了
							}
						}
						
					}
				}
				closedir(pDirYear); //
				
				//年文件夹
				if( true == bIsYearDirEmpty )
				{
					//删除文件夹
					char cmd[128] = "";
					snprintf(cmd, 128, "rm -rf %s", strYearPath);
					AppErr("remove %s\n", strYearPath);
//					__STORAGE_SYSTEM__(cmd);
					__STORAGE_SYSTEM__("sh", "sh", "-c", cmd, NULL);
				}
			}
			
		}
	}
//	  chdir("..");
	closedir(dir);
}

static record_file_info_s s_astInsertRcdFileInfo[STORAGE_MAX_FILE_PER_DAY];
Int32 CStorageManager::InsertlRecordFileIndex(const char *strPath, record_file_info_s *pstRecordFileInfo)
{
	CGuard guard(m_mutex);

	int ret;
	int len;
	int pos;
	int file_num;
	int file_size = 0;
	record_file_info_s stLatestRcdFileInfo;
	
	int fd_index = open(strPath, O_RDWR | O_CREAT /*| O_APPEND*/); /*O_APPEND 每次写操作都会写到文件尾，所以不加*/
	if (-1 == fd_index)
	{
		AppErr("open %s failed. errno: %d\n", strPath, errno);
		return -1;
	}
	
	file_size = lseek(fd_index, 0, SEEK_END);
	if (-1 == file_size)
	{
		AppErr("lseek %s failed. errno: %d\n", strPath, errno);
		close(fd_index);
		return -1;
	}

	len = file_size % sizeof(record_file_info_s);
	if (len != 0)
	{
		AppErr("index file size: %d\n", file_size);
		file_size -= len;
	}

	if (file_size >= (sizeof(record_file_info_s)*STORAGE_MAX_FILE_PER_DAY)) //录像文件个数达到设定上限
	{
		printf("record file num limit[%d].\n", STORAGE_MAX_FILE_PER_DAY);
		return -1;
	}

	if (0 == file_size)
	{
		lseek(fd_index, 0, SEEK_SET);
		ret = disk_write_file(fd_index, (char *)pstRecordFileInfo, sizeof(record_file_info_s));
		fdatasync(fd_index);
		close(fd_index);
		return (ret == sizeof(record_file_info_s)) ? 0 : -1;
	}
	
	lseek(fd_index, file_size-sizeof(record_file_info_s), SEEK_SET);
	ret = disk_read_file(fd_index, (char *)&stLatestRcdFileInfo,  sizeof(stLatestRcdFileInfo));
	if (ret != sizeof(stLatestRcdFileInfo))
	{
		AppErr("read failed. ret: %d\n", ret);
		close(fd_index);
		return -1;
	}

	if (pstRecordFileInfo->iStartTime >= stLatestRcdFileInfo.iStartTime)
	{
		lseek(fd_index, file_size, SEEK_SET);
		ret = disk_write_file(fd_index, (char *)pstRecordFileInfo, sizeof(record_file_info_s));
		fdatasync(fd_index);
		close(fd_index);
		return (ret == sizeof(record_file_info_s)) ? 0 : -1;
	}

	//非正常情况，按开始时间排序插入
	lseek(fd_index, 0, SEEK_SET);
	file_size = file_size > sizeof(s_astInsertRcdFileInfo) ? sizeof(s_astInsertRcdFileInfo) : file_size;
	ret = disk_read_file(fd_index, (char *)&s_astInsertRcdFileInfo,  file_size);
	if (ret != file_size)
	{
		AppErr("read failed. ret: %d\n", ret);
		close(fd_index);
		return -1;
	}

	pos = 0;
	file_num = file_size / sizeof(record_file_info_s);
	AppErr("insert to middle. file_num : %d\n", file_num);
	for (int i = 0; i < file_num; i++)
	{
		if (0 == s_astInsertRcdFileInfo[i].iStartTime)
			continue;
		if (pstRecordFileInfo->iStartTime < s_astInsertRcdFileInfo[i].iStartTime)
		{
			pos = i;
			break;
		}
	}
	AppErr("insert to middle. pos : %d\n", pos);
	
	lseek(fd_index, 0, SEEK_SET);
	ret = disk_write_file(fd_index, (char *)&s_astInsertRcdFileInfo[0], pos * sizeof(record_file_info_s));
	if (ret != pos * sizeof(record_file_info_s))
	{
		AppErr("write failed. ret: %d\n", ret);
		close(fd_index);
		return -1;
	}
	ret = disk_write_file(fd_index, (char *)pstRecordFileInfo, sizeof(record_file_info_s));
	if (ret != sizeof(record_file_info_s))
	{
		AppErr("write failed. ret: %d\n", ret);
		close(fd_index);
		return -1;
	}
	ret = disk_write_file(fd_index, (char *)&s_astInsertRcdFileInfo[pos], (file_num-pos) * sizeof(record_file_info_s));
	if (ret != (file_num-pos) * sizeof(record_file_info_s))
	{
		AppErr("write failed. ret: %d\n", ret);
		close(fd_index);
		return -1;
	}
	
	fdatasync(fd_index);
	close(fd_index);
	return 0;
}

//循环录像, SD卡满时, 删除最早的录像文件
Int32 CStorageManager::DeleteOrriginalRecordFile()
{
	//假设索引表中已按时间排序

	//是否需要加锁 ???

	bool bFinished = false;
	
	CGuard guard(m_mutex);
	
	TRecordFileMap::iterator it, it_next, it_tmp;
	for( it = m_mapRecordFile.begin(); it != m_mapRecordFile.end(); )
	{
		it_tmp = it;
		it_next = ++it_tmp;
		
		int ret;
		UInt64 u64DiskFreeSize;
		TRecordFileInfoList *pListRecordFileInfo = it->second;
		TRecordFileInfoList::iterator list_it, list_it_next, list_it_tmp;
		for( list_it = pListRecordFileInfo->begin(); list_it != pListRecordFileInfo->end(); )
		{
			list_it_tmp = list_it;
			list_it_next = ++list_it_tmp;

			ret = CDiskManager::defaultStorageManager()->GetDiskcapacity(NULL, NULL, &u64DiskFreeSize);
			if( (0 == ret) && (u64DiskFreeSize < RECORD_RESERVE_MEMORY_SIZE_BYTES) )
			{
				int iTmp = it->first;
				char path[128] = "";

				struct tm tmStart = {0};
				struct tm tmEnd = {0};
				int iStartTime, iEndTime;

				iStartTime = list_it->iStartTime;
				iEndTime = list_it->iEndTime;
				localtime_r((time_t*)&iStartTime, &tmStart);
				localtime_r((time_t*)&iEndTime, &tmEnd);
				

				if( 0 == list_it->iRecType )
				{
					snprintf(path, sizeof(path), __STORAGE_SD_MOUNT_PATH__"/DCIM/%04d/%02d/%02d/%04d%02d%02d%02d%02d%02d-%04d%02d%02d%02d%02d%02d.mp4", 
													iTmp/10000, (iTmp%10000)/100, iTmp%100, 
													tmStart.tm_year+1900, tmStart.tm_mon+1, tmStart.tm_mday, tmStart.tm_hour, tmStart.tm_min, tmStart.tm_sec, 
													tmEnd.tm_year+1900, tmEnd.tm_mon+1, tmEnd.tm_mday, tmEnd.tm_hour, tmEnd.tm_min, tmEnd.tm_sec);
				}
				else
				{
					snprintf(path, sizeof(path), __STORAGE_SD_MOUNT_PATH__"/DCIM/%04d/%02d/%02d/%04d%02d%02d%02d%02d%02d-%04d%02d%02d%02d%02d%02d_ALARM.mp4", 
													iTmp/10000, (iTmp%10000)/100, iTmp%100, 
													tmStart.tm_year+1900, tmStart.tm_mon+1, tmStart.tm_mday, tmStart.tm_hour, tmStart.tm_min, tmStart.tm_sec, 
													tmEnd.tm_year+1900, tmEnd.tm_mon+1, tmEnd.tm_mday, tmEnd.tm_hour, tmEnd.tm_min, tmEnd.tm_sec);
				}
				
				AppErr("delete record file : %s\n", path);
				remove(path);

				//在有录像时间段中, 删除这一段的录像时间记录
				DeleteRecordTimeList(list_it->iStartTime, list_it->iEndTime);
								
				pListRecordFileInfo->erase(list_it);

				list_it = list_it_next;
			}
			else
			{
//				list_it++;
				bFinished = true;
				break;
			}
		}

		//检车文件夹是否空
		if( 0 == pListRecordFileInfo->size() )
		{
			int iTmp = it->first;
			char path[128] = "";
			char cmd[128] = "";
			snprintf(path, 128, __STORAGE_SD_MOUNT_PATH__"/DCIM/%04d/%02d/%02d", iTmp/10000, (iTmp%10000)/100, iTmp%100);
			snprintf(cmd, 128, "rm -rf %s", path);
			//删除文件夹
//			__STORAGE_SYSTEM__(cmd);
			__STORAGE_SYSTEM__("sh", "sh", "-c", cmd, NULL);

			//删除文件索引list
			delete pListRecordFileInfo;
			pListRecordFileInfo = NULL;
			it->second = NULL;

			m_mapRecordFile.erase(it);
			it = it_next;
		}
		else
		{
			it++;
		}

		if( true == bFinished )
		{
			return 0;
		}
	}
	
	return -1;
}

//循环录像, SD卡满时, 删除最早的录像文件
Int32 CStorageManager::DeleteOrriginalRecordFile_2()
{
	CGuard guard(m_mutex);
	
	int ret;
	bool bFinish = false;
	char strPath[128] = "";
	int iYear, iMonth, iDay;
    struct dirent **ppYearEntry = NULL, **ppMonthEntry = NULL, **ppDayEntry = NULL;
	int iYearIndex = 0, iMonthIndex = 0, iDayIndex = 0;

    struct dirent **entry = NULL;
    int iEntryIndex = 0;

	record_file_info_s stRecordFileInfo = {0};

	snprintf(strPath, sizeof(strPath), __STORAGE_SD_MOUNT_PATH__"/DCIM/");
	int nEntry = scandir(strPath, &entry, dir_filter_year, dir_compare);
	iEntryIndex = 0;
	while (iEntryIndex < nEntry)
    {
		iYear = ConvertDateStrToInt(1, entry[iEntryIndex]->d_name);
		if (false == bFinish && iYear > 0)
		{
			snprintf(strPath, sizeof(strPath), __STORAGE_SD_MOUNT_PATH__"/DCIM/%s/", entry[iEntryIndex]->d_name);
			int nYear = scandir(strPath, &ppYearEntry, dir_filter_month, dir_compare);
			iYearIndex = 0;
			while (iYearIndex < nYear)				
			{
				iMonth = ConvertDateStrToInt(2, ppYearEntry[iYearIndex]->d_name);
				if (false == bFinish && iMonth > 0)
				{
					snprintf(strPath, sizeof(strPath), __STORAGE_SD_MOUNT_PATH__"/DCIM/%s/%s", entry[iEntryIndex]->d_name, ppYearEntry[iYearIndex]->d_name);
					int nMonth = scandir(strPath, &ppMonthEntry, dir_filter_day, dir_compare);
					iMonthIndex = 0;
					while (iMonthIndex < nMonth)
					{
						iDay = ConvertDateStrToInt(3, ppMonthEntry[iMonthIndex]->d_name);
						if (false == bFinish && iDay > 0)
						{
							snprintf(strPath, sizeof(strPath), __STORAGE_SD_MOUNT_PATH__"/DCIM/%s/%s/%s/index", entry[iEntryIndex]->d_name, ppYearEntry[iYearIndex]->d_name, ppMonthEntry[iMonthIndex]->d_name);
							int fd_index = open(strPath, O_RDWR);
							if (-1 == fd_index)
							{
								free(ppMonthEntry[iMonthIndex]);
								iMonthIndex++;
								continue;
							}
							
							while(1)									
							{
								memset(&stRecordFileInfo, 0, sizeof(stRecordFileInfo));
								ret = disk_read_file(fd_index, (char *)&stRecordFileInfo,  sizeof(stRecordFileInfo));
								if (ret != sizeof(stRecordFileInfo))
									break;
								if (0 == stRecordFileInfo.iStartTime)
									continue;
								record_file_info_s stEmpty;
								memset(&stEmpty, 0, sizeof(stEmpty));
								lseek(fd_index, -sizeof(record_file_info_s), SEEK_CUR);
								disk_write_file(fd_index, (char *)&stEmpty,  sizeof(record_file_info_s));
								fdatasync(fd_index);
								
								char path[128] = "";
								
								struct tm tmStart = {0};
								struct tm tmEnd = {0};
								int iStartTime, iEndTime;
								
								iStartTime = stRecordFileInfo.iStartTime;
								iEndTime = stRecordFileInfo.iEndTime;
								localtime_r((time_t*)&iStartTime, &tmStart);
								localtime_r((time_t*)&iEndTime, &tmEnd);
								
								if( 0 == stRecordFileInfo.iRecType )
								{
									snprintf(path, sizeof(path), __STORAGE_SD_MOUNT_PATH__"/DCIM/%04d/%02d/%02d/%04d%02d%02d%02d%02d%02d-%04d%02d%02d%02d%02d%02d.mp4", 
																	tmStart.tm_year+1900, tmStart.tm_mon+1, tmStart.tm_mday, 
																	tmStart.tm_year+1900, tmStart.tm_mon+1, tmStart.tm_mday, tmStart.tm_hour, tmStart.tm_min, tmStart.tm_sec, 
																	tmEnd.tm_year+1900, tmEnd.tm_mon+1, tmEnd.tm_mday, tmEnd.tm_hour, tmEnd.tm_min, tmEnd.tm_sec);
								}
								else
								{
									snprintf(path, sizeof(path), __STORAGE_SD_MOUNT_PATH__"/DCIM/%04d/%02d/%02d/%04d%02d%02d%02d%02d%02d-%04d%02d%02d%02d%02d%02d_ALARM.mp4", 
																	tmStart.tm_year+1900, tmStart.tm_mon+1, tmStart.tm_mday, 
																	tmStart.tm_year+1900, tmStart.tm_mon+1, tmStart.tm_mday, tmStart.tm_hour, tmStart.tm_min, tmStart.tm_sec, 
																	tmEnd.tm_year+1900, tmEnd.tm_mon+1, tmEnd.tm_mday, tmEnd.tm_hour, tmEnd.tm_min, tmEnd.tm_sec);
								}
								
								AppErr("delete record file : %s\n", path);
								remove(path);
																								
								UInt64 u64DiskFreeSize;
								ret = CDiskManager::defaultStorageManager()->GetDiskcapacity(NULL, NULL, &u64DiskFreeSize);
								if( (0 == ret) && (u64DiskFreeSize > RECORD_RESERVE_MEMORY_SIZE_BYTES) )
								{
									bFinish = true;
									break;
								}
							}
							close(fd_index);

							if (false == bFinish)
							{
								//删除文件夹
								char cmd[128] = "";
								snprintf(cmd, 128, "rm -rf " __STORAGE_SD_MOUNT_PATH__ "/DCIM/%s/%s/%s", 
										entry[iEntryIndex]->d_name, ppYearEntry[iYearIndex]->d_name, ppMonthEntry[iMonthIndex]->d_name);
								AppErr("remove cmd: %s\n", cmd);
								__STORAGE_SYSTEM__("sh", "sh", "-c", cmd, NULL);
							}
						}
						
						free(ppMonthEntry[iMonthIndex]);
						iMonthIndex++;

					}

					if(ppMonthEntry)
						free(ppMonthEntry);

					if (false == bFinish)
					{
						//删除文件夹
						char cmd[128] = "";
						snprintf(cmd, 128, "rm -rf " __STORAGE_SD_MOUNT_PATH__ "/DCIM/%s/%s", 
								entry[iEntryIndex]->d_name, ppYearEntry[iYearIndex]->d_name);
						AppErr("remove cmd: %s\n", cmd);
						__STORAGE_SYSTEM__("sh", "sh", "-c", cmd, NULL);
					}					
				}
				
				free(ppYearEntry[iYearIndex]);
				iYearIndex++;
			}

			if(ppYearEntry)
				free(ppYearEntry);
			
			if (false == bFinish)
			{
				//删除文件夹
				char cmd[128] = "";
				snprintf(cmd, 128, "rm -rf " __STORAGE_SD_MOUNT_PATH__ "/DCIM/%s", entry[iEntryIndex]->d_name);
				AppErr("remove cmd: %s\n", cmd);
				__STORAGE_SYSTEM__("sh", "sh", "-c", cmd, NULL);
			}					
		}
		
		free(entry[iEntryIndex]);
		iEntryIndex++;
    }

	if(entry)
		free(entry);

	return 0;
}

//默认时间段已按序排列, 并且时间轴一直往前, 如果时间随意切换会造成紊乱
//仅支持跨一天, 不支持跨多天
Int32 CStorageManager::InsertRecordTimeList(Int32 iStartTime, Int32 iEndTime)
{
	if( iStartTime > iEndTime )
		return -1;
	
	#if 0 	//print debug info
	{
		struct tm tm_s = {0};
		struct tm tm_e = {0};
		localtime_r((time_t*)&iStartTime, &tm_s);
		localtime_r((time_t*)&iEndTime, &tm_e);
		printf("insert %04d-%02d-%02d %02d:%02d:%02d  --  %04d-%02d-%02d %02d:%02d:%02d\n", 
			tm_s.tm_year+1900, tm_s.tm_mon+1, tm_s.tm_mday, tm_s.tm_hour, tm_s.tm_min, tm_s.tm_sec, 
			tm_e.tm_year+1900, tm_e.tm_mon+1, tm_e.tm_mday, tm_e.tm_hour, tm_e.tm_min, tm_e.tm_sec);
	}
	#endif

	int start_date_index = GetDateIndex(iStartTime);
	int end_date_index = GetDateIndex(iEndTime);
//	printf("start_date_index : %d, end_date_index : %d\n", start_date_index, end_date_index);
	Int32 iDayFinalSec = iEndTime;
	
	if( start_date_index != end_date_index )
	{
		SystemTime stDayFinalTime;
		stDayFinalTime.year 	= start_date_index/10000;
		stDayFinalTime.month	= (start_date_index%10000)/100;
		stDayFinalTime.day		= start_date_index%100;
		stDayFinalTime.hour 	= 23;
		stDayFinalTime.minute	= 59;
		stDayFinalTime.second	= 59;
		
		iDayFinalSec = SystemTimeToSecond(&stDayFinalTime);
		iDayFinalSec += 1;	//再加 1 s
//		printf("iDayFinalSec : %d\n", iDayFinalSec);
	}
	
	record_file_time_s stRecordFileTime = {0};

	CGuard guard(m_mutex_RcTime);

	TRecordFileTimeMap::iterator map_it;
	map_it = m_mapRecordTime.find(start_date_index);
	if( map_it == m_mapRecordTime.end() )
	{
		TRecordFileTimeList listRecordTime;
		
		stRecordFileTime.iStartTime = iStartTime;
		stRecordFileTime.iEndTime = iDayFinalSec;
		listRecordTime.push_back(stRecordFileTime);
		m_mapRecordTime[start_date_index] = listRecordTime;		
	}
	else
	{
		TRecordFileTimeList &listRecordTime = (map_it->second);

		if ( listRecordTime.size() == 0 )
		{
			stRecordFileTime.iStartTime = iStartTime;
			stRecordFileTime.iEndTime = iDayFinalSec;
			listRecordTime.push_back(stRecordFileTime);
		}
		else
		{
			TRecordFileTimeList::iterator list_it = listRecordTime.end();
			list_it--;	//指向最后一个元素
		
#if 0	//print debug info
			{
				struct tm tm_s = {0};
				struct tm tm_e = {0};
				localtime_r((time_t*)&(list_it->iStartTime), &tm_s);
				localtime_r((time_t*)&(list_it->iEndTime), &tm_e);
				printf("last time %04d-%02d-%02d %02d:%02d:%02d  --  %04d-%02d-%02d %02d:%02d:%02d\n", 
					tm_s.tm_year+1900, tm_s.tm_mon+1, tm_s.tm_mday, tm_s.tm_hour, tm_s.tm_min, tm_s.tm_sec, 
					tm_e.tm_year+1900, tm_e.tm_mon+1, tm_e.tm_mday, tm_e.tm_hour, tm_e.tm_min, tm_e.tm_sec);
			}
#endif
		
			if( iStartTime < list_it->iEndTime )
			{
				if( iDayFinalSec > list_it->iEndTime )
					list_it->iEndTime = iDayFinalSec;
			}
			else
			{
				if ( (iStartTime == list_it->iEndTime) || (iStartTime == list_it->iEndTime+1) )
				{
					if( iDayFinalSec > list_it->iEndTime )
						list_it->iEndTime = iDayFinalSec;
				}
				else
				{
					stRecordFileTime.iStartTime = iStartTime;
					stRecordFileTime.iEndTime = iDayFinalSec;
					listRecordTime.push_back(stRecordFileTime);
				}
			}
		}		
	}
	
	if( start_date_index != end_date_index )
	{
		map_it = m_mapRecordTime.find(end_date_index);
		if( map_it == m_mapRecordTime.end() )
		{
			TRecordFileTimeList listRecordTime;
			stRecordFileTime.iStartTime = iDayFinalSec;
			stRecordFileTime.iEndTime = iEndTime;
			listRecordTime.push_back(stRecordFileTime);
			m_mapRecordTime[end_date_index] = listRecordTime;
		}
		else
		{
			//时间轴一直往前,不应该存在这种情况
			printf("InsertRecordTimeList --- err.\n");
		}
	}

	return 0;
}

Int32 CStorageManager::DeleteRecordTimeList(Int32 iStartTime, Int32 iEndTime)
{
	int start_date_index = GetDateIndex(iStartTime);
	int end_date_index = GetDateIndex(iEndTime);
	Int32 iDayFinalSec = iEndTime;
	
	if( start_date_index != end_date_index )
	{
		SystemTime stDayFinalTime;
		stDayFinalTime.year 	= start_date_index/10000;
		stDayFinalTime.month	= (start_date_index%10000)/100;
		stDayFinalTime.day		= start_date_index%100;
		stDayFinalTime.hour 	= 23;
		stDayFinalTime.minute	= 59;
		stDayFinalTime.second	= 59;
		
		iDayFinalSec = SystemTimeToSecond(&stDayFinalTime);
		iDayFinalSec += 1;	//再加 1 s
		printf("iDayFinalSec : %d\n", iDayFinalSec);
	}

	CGuard guard(m_mutex_RcTime);

	TRecordFileTimeMap::iterator map_it;
	map_it = m_mapRecordTime.find(start_date_index);
	if( map_it != m_mapRecordTime.end() )
	{
		TRecordFileTimeList &listRecordTime = (map_it->second);
		
		TRecordFileTimeList::iterator list_it, list_it_next, list_it_tmp;
		for( list_it = listRecordTime.begin(); list_it != listRecordTime.end(); )
		{
			list_it_tmp = list_it;
			list_it_next = ++list_it_tmp;
	
			if( list_it->iStartTime > iDayFinalSec )
				break;
	
			if( iStartTime >= list_it->iStartTime && iDayFinalSec <= list_it->iEndTime )
			{
				if( iStartTime == list_it->iStartTime && iDayFinalSec == list_it->iEndTime )
				{
					//erase
					listRecordTime.erase(list_it);
				}
				else
				{
					if( iStartTime == list_it->iStartTime )
					{
						list_it->iStartTime = iDayFinalSec;
					}
					else if( iDayFinalSec == list_it->iEndTime )
					{
						list_it->iEndTime = iStartTime;
					}
					else// if( iStartTime > it->iStartTime && iEndTime < it->iEndTime )
					{
						record_file_time_s stRecordFileTime = {0};
						stRecordFileTime.iStartTime = iDayFinalSec;
						stRecordFileTime.iEndTime = list_it->iEndTime;
						listRecordTime.insert(list_it, stRecordFileTime);
					
						list_it->iEndTime = iStartTime;
					}
				}
			}
			else
			{
				if( list_it->iStartTime < iStartTime && list_it->iEndTime > iStartTime )
				{
					list_it->iEndTime = iStartTime;
				}
				else if( list_it->iStartTime >= iStartTime && list_it->iEndTime <= iDayFinalSec )
				{
					//erase
					listRecordTime.erase(list_it);
				}
				else if( list_it->iStartTime < iDayFinalSec && list_it->iEndTime > iDayFinalSec )
				{
					list_it->iStartTime = iDayFinalSec;
				}
			}
			list_it = list_it_next;
		}
		if( listRecordTime.size() == 0 )
		{
			m_mapRecordTime.erase(map_it);
		}
	}

	if( start_date_index != end_date_index )
	{
		TRecordFileTimeMap::iterator map_it;
		map_it = m_mapRecordTime.find(end_date_index);
		if( map_it != m_mapRecordTime.end() )
		{
			TRecordFileTimeList &listRecordTime = (map_it->second);
			
			TRecordFileTimeList::iterator list_it, list_it_next, list_it_tmp;
			for( list_it = listRecordTime.begin(); list_it != listRecordTime.end(); )
			{
				list_it_tmp = list_it;
				list_it_next = ++list_it_tmp;

				if( list_it->iStartTime > iEndTime )
					break;

				if( iDayFinalSec >= list_it->iStartTime && iEndTime <= list_it->iEndTime )
				{
					if( iDayFinalSec == list_it->iStartTime && iEndTime == list_it->iEndTime )
					{
						//erase
						listRecordTime.erase(list_it);
					}
					else
					{
						if( iDayFinalSec == list_it->iStartTime )
						{
							list_it->iStartTime = iEndTime;
						}
						else if( iEndTime == list_it->iEndTime )
						{
							list_it->iEndTime = iDayFinalSec;
						}
						else// if( iStartTime > it->iStartTime && iEndTime < it->iEndTime )
						{
							record_file_time_s stRecordFileTime = {0};
							stRecordFileTime.iStartTime = iEndTime;
							stRecordFileTime.iEndTime = list_it->iEndTime;
							listRecordTime.insert(list_it, stRecordFileTime);
						
							list_it->iEndTime = iDayFinalSec;
						}
					}
				}
				else
				{
					if( list_it->iStartTime < iDayFinalSec && list_it->iEndTime > iDayFinalSec )
					{
						list_it->iEndTime = iDayFinalSec;
					}
					else if( list_it->iStartTime >= iDayFinalSec && list_it->iEndTime <= iEndTime )
					{
						//erase
						listRecordTime.erase(list_it);
					}
					else if( list_it->iStartTime < iEndTime && list_it->iEndTime > iEndTime )
					{
						list_it->iStartTime = iEndTime;
					}
				}
				list_it = list_it_next;
			}
			if( listRecordTime.size() == 0 )
			{
				m_mapRecordTime.erase(map_it);
			}
		}
	}
	

	return -1;
}



void CStorageManager::GetRecordFilePath(unsigned long long time_ms, char *buffer, int bufferSize, int iRecordMode)
{
	int time_sec = time_ms / 1000;
	struct tm tm = {0};
	DIR *pDir = NULL;
	char tmpbuffer[64];
	char path[128] = __STORAGE_SD_MOUNT_PATH__"/DCIM/";

	localtime_r((time_t*)&time_sec, &tm);

	//DCIM	
	pDir = opendir(path);
	if( NULL == pDir )
	{
		mkdir(path, 0777);
	}
	else
	{
		closedir(pDir);
	}

	//year
	snprintf(tmpbuffer, 64, "%04d/", tm.tm_year+1900);
	strcat(path, tmpbuffer);
	
	pDir = opendir(path);
	if( NULL == pDir )
	{
		mkdir(path, 0777);
	}
	else
	{
		closedir(pDir);
	}
	
	//month
	snprintf(tmpbuffer, 64, "%02d/", tm.tm_mon+1);
	strcat(path, tmpbuffer);
	
	pDir = opendir(path);
	if( NULL == pDir )
	{
		mkdir(path, 0777);
	}
	else
	{
		closedir(pDir);
	}
	
	//day
	snprintf(tmpbuffer, 64, "%02d/", tm.tm_mday);
	strcat(path, tmpbuffer);
	
	pDir = opendir(path);
	if( NULL == pDir )
	{
		mkdir(path, 0777);
	}
	else
	{
		closedir(pDir);
	}

	if( DISK_RECORD_MODE_FULLTIME == iRecordMode )
		snprintf(buffer, bufferSize, "%stmp_%llu.mp4", path, time_ms);
	else// if( DISK_RECORD_MODE_ALARM == iRecordMode )
		snprintf(buffer, bufferSize, "%stmp_%llu_ALARM.mp4", path, time_ms);
}


//filter
/* 
 @return 	<0 出错
 			=0 不需要切换文件
 			>0 1-跨天切换 	2-超过文件限制时长切换
*/
int CStorageManager::FilterFIle(const UInt64 *pu64FileStartTime_ms, const UInt64 *pu64LastFrameTimestamp_ms, const UInt64 *pu64NewFrameTimestamp_ms)
{
	struct tm tm_1 = {0};
	struct tm tm_2 = {0};
	time_t t_1, t_2;
	
	t_1 = *pu64FileStartTime_ms/1000;
	t_2 = *pu64NewFrameTimestamp_ms/1000;

	//先判断是否跨天
	localtime_r((time_t*)&t_1, &tm_1);
	localtime_r((time_t*)&t_2, &tm_2);

/*
	printf("pu64FrameTimestamp_ms : %llu, pu64FileStartTime_ms : %llu, pu64Last_I_FrameTimestamp_ms : %llu\n",
			*pu64FrameTimestamp_ms, *pu64FileStartTime_ms, *pu64Last_I_FrameTimestamp_ms);
	printf("tm_1 : %04d-%02d-%02d %02d:%02d:%02d, tm_2 : %04d-%02d-%02d %02d:%02d:%02d\n", 
			tm_1.tm_year+1900, tm_1.tm_mon+1, tm_1.tm_mday, tm_1.tm_hour, tm_1.tm_min, tm_1.tm_sec, 
			tm_2.tm_year+1900, tm_2.tm_mon+1, tm_2.tm_mday, tm_2.tm_hour, tm_2.tm_min, tm_2.tm_sec);
*/

	if( tm_1.tm_mday != tm_2.tm_mday ) 	//跨天
	{
//		printf("FilterFIle ---> 1\n");
		return 1;
	}

	//检查是否超过文件限制时长
	if ( ((*pu64LastFrameTimestamp_ms - *pu64FileStartTime_ms) /1000) > RECORD_FILE_DURATION_SECONDS )
	{
//		printf("FilterFIle ---> 2\n");
		return 2;
	}
//	printf("FilterFIle ---> 0\n");

	return 0;
}

int CStorageManager::GetFilePathBaseOnTimestamp(UInt64 *pu64StartTimestamp_ms, UInt64 *pu64EndTimestamp_ms, char *buffer_file_path, int bufferSize, int iRecordMode)
{
	struct tm tm_1 = {0};
	struct tm tm_2 = {0};
	time_t t_1, t_2;
	char buffer[128] = "";
	
	
	t_1 = *pu64StartTimestamp_ms/1000;
	t_2 = *pu64EndTimestamp_ms/1000;

	if( (NULL == buffer_file_path) || (bufferSize <= 0) )
		return -1;

	localtime_r((time_t*)&t_1, &tm_1);
	localtime_r((time_t*)&t_2, &tm_2);

	if( DISK_RECORD_MODE_FULLTIME == iRecordMode )
	{
		snprintf(buffer, sizeof(buffer), __STORAGE_SD_MOUNT_PATH__"/DCIM/%04d/%02d/%02d/%04d%02d%02d%02d%02d%02d-%04d%02d%02d%02d%02d%02d.mp4", 
										tm_1.tm_year+1900, tm_1.tm_mon+1, tm_1.tm_mday, 
										tm_1.tm_year+1900, tm_1.tm_mon+1, tm_1.tm_mday, tm_1.tm_hour, tm_1.tm_min, tm_1.tm_sec, 
										tm_2.tm_year+1900, tm_2.tm_mon+1, tm_2.tm_mday, tm_2.tm_hour, tm_2.tm_min, tm_2.tm_sec);
	}
	else if( DISK_RECORD_MODE_ALARM == iRecordMode )
	{
		snprintf(buffer, sizeof(buffer), __STORAGE_SD_MOUNT_PATH__"/DCIM/%04d/%02d/%02d/%04d%02d%02d%02d%02d%02d-%04d%02d%02d%02d%02d%02d_ALARM.mp4", 
										tm_1.tm_year+1900, tm_1.tm_mon+1, tm_1.tm_mday, 
										tm_1.tm_year+1900, tm_1.tm_mon+1, tm_1.tm_mday, tm_1.tm_hour, tm_1.tm_min, tm_1.tm_sec, 
										tm_2.tm_year+1900, tm_2.tm_mon+1, tm_2.tm_mday, tm_2.tm_hour, tm_2.tm_min, tm_2.tm_sec);
	}
	else
		return -2;

	if( bufferSize < (strlen(buffer) + 1) )
		return -3;

	strcpy(buffer_file_path, buffer);

	return 0;
}

int CStorageManager::GetDateIndex(int iUnixTimestamp)
{
	int date_index;
	struct tm tm = {0};
	
	localtime_r((time_t*)&iUnixTimestamp, &tm );
	date_index = (tm.tm_year+1900)*10000 + (tm.tm_mon+1) * 100 + tm.tm_mday;
	
	return date_index;
}

int CStorageManager::GetDateIndexFilePath(int iUnixTimestamp, char *path)
{
	struct tm tm = {0};
	
	localtime_r((time_t*)&iUnixTimestamp, &tm );
	snprintf(path, 128, __STORAGE_SD_MOUNT_PATH__"/DCIM/%04d/%02d/%02d/index", 
		tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday);
	
	return 0;
}

int CStorageManager::GetAlarmRecIndexFilePath(int iUnixTimestamp, char *path)
{
	struct tm tm = {0};
	
	localtime_r((time_t*)&iUnixTimestamp, &tm );
	snprintf(path, 128, __STORAGE_SD_MOUNT_PATH__"/DCIM/%04d/%02d/%02d/alarm_index", 
		tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday);
	
	return 0;
}

int CStorageManager::do_save_sps_pps()
{
	int ret;
	int fd;
	char path[128];
	snprintf(path, sizeof(path), __STORAGE_SD_MOUNT_PATH__"/DCIM/spspps");
	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC);
	if (-1 == fd)
	{
		return -1;
	}
	ret = disk_write_file(fd, m_arrSpsPps, m_iSpsPpsLen);
	if (ret != m_iSpsPpsLen)
	{
		return -1;
	}
	fdatasync(fd);
	close(fd);
	
	return 0;
}

int CStorageManager::do_read_sps_pps()
{
	int ret;
	int fd;
	char path[128];

	if (STORAGE_VIDEO_ENC_H265 == m_eVideoEncType) //H265不需要
	{
		m_iSpsPpsLen = 0;
		return 0;
	}
	
	snprintf(path, sizeof(path), __STORAGE_SD_MOUNT_PATH__"/DCIM/spspps");
	fd = open(path, O_RDONLY);
	if (-1 == fd)
	{
		return -1;
	}
	ret = disk_read_file(fd, m_arrSpsPps, sizeof(m_arrSpsPps));
	if (ret < 0)
	{
		m_iSpsPpsLen = 0;
	}
	else
		m_iSpsPpsLen = ret;
	
	close(fd);
	
	return 0;
}

int CStorageManager::InitMp4Muxer(const char *record_file_path)
{
	AppInfo("record_file_path : %s\n", record_file_path);
	int ret;
	AVCodecContext v_codec, a_codec;

	memset(&v_codec, 0, sizeof(v_codec));
	memset(&a_codec, 0, sizeof(a_codec));

	if ( true == m_Mp4Muxer.IsOpenFile() )
	{
		return -1;
	}
	
	v_codec.codec_type = AVMEDIA_TYPE_VIDEO;
	if (STORAGE_VIDEO_ENC_H264 == m_eVideoEncType)
		v_codec.codec_id = AV_CODEC_ID_H264;
	else
		v_codec.codec_id = AV_CODEC_ID_H265;
	v_codec.width = m_iWidth;
	v_codec.height = m_iHeight;
	v_codec.bit_rate = m_iBitRate;
	v_codec.flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	/* emit one intra frame every twelve frames at most */
	v_codec.gop_size = m_iGop;
	v_codec.qmin =	20;
	v_codec.qmax =	51;
	v_codec.time_base = AV_TIME_BASE_Q;
	v_codec.extradata = NULL;
	v_codec.extradata_size = 0;
	if ((m_iSpsPpsLen > 0) && (NULL != m_arrSpsPps))
	{
		/* extradata != NULL，mp4文件中nula开始码会被替换为nula长度 */
		v_codec.extradata = (uint8_t *)av_malloc(m_iSpsPpsLen);
		memcpy(v_codec.extradata, m_arrSpsPps, m_iSpsPpsLen);
		v_codec.extradata_size = m_iSpsPpsLen;
	}
	else
	{
		v_codec.extradata = NULL;
		v_codec.extradata_size = 0;
	}
	
	
	a_codec.codec_type	= AVMEDIA_TYPE_AUDIO;
	a_codec.sample_rate = 8000;
	a_codec.codec_id = AV_CODEC_ID_PCM_MULAW;//AV_CODEC_ID_AAC;
	a_codec.channels = 1;
	a_codec.extradata = NULL;
	a_codec.extradata_size = 0;
	a_codec.time_base = AV_TIME_BASE_Q;
	a_codec.flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	a_codec.sample_fmt = AV_SAMPLE_FMT_S16;
	a_codec.frame_size = 8000/8;
	a_codec.delay = 22;

	ret = m_Mp4Muxer.mp4_mux_init(&v_codec, &a_codec, m_iFrameRate, record_file_path);
	if( 0 != ret )
	{
		return ret;
	}
	
	snprintf(m_strRecoverFile, 128, "%s-recover", record_file_path);
//	m_fpRecover = fopen(m_strRecoverFile, "w");
	m_fdRecover = open(m_strRecoverFile, O_WRONLY | O_CREAT | O_TRUNC);
	if (-1 == m_fdRecover)
	{
		printf("open %s fail. errno: %d\n", m_strRecoverFile, errno);
		if (ENOENT == errno || //#define ENOENT 2 /* No such file or directory */
			EIO == errno) //#define EIO 5 /* I/O error */
			CDiskManager::defaultStorageManager()->SetReadWriteErrorFlag(true);
	}

	return ret;
}

int CStorageManager::UnInitMp4Muxer()
{
	if ( false == m_Mp4Muxer.IsOpenFile() )
	{
		return -1;
	}

	/*if( m_fpRecover )
	{
		fclose(m_fpRecover);
		m_fpRecover = NULL;

		remove(m_strRecoverFile);
		memset(m_strRecoverFile, 0, 128);
	}*/
	if( -1 != m_fdRecover )
	{
		//fdatasync(m_fdRecover);
		close(m_fdRecover);
		m_fdRecover = -1;
	
		remove(m_strRecoverFile);
		memset(m_strRecoverFile, 0, 128);
	}
	
	return m_Mp4Muxer.mp4_mux_uninit();
}

int CStorageManager::WriteToMp4Muxer(UInt64 *pu64FileStartTime_ms, int iStreamType, int iFrameType, UInt64 *pu64FrameTimestamp_ms, UInt8 *pFrameData, int iFrameSize)
{
	int ret;
	AVPacket packet;
	UInt64 u64Interval;
	
	if ( false == m_Mp4Muxer.IsOpenFile() )
	{
		return -1;
	}

	if( 2 == iStreamType )		//audio
	{
		u64Interval = *pu64FrameTimestamp_ms - *pu64FileStartTime_ms;
//		printf("audio ---- %llu = %llu - %llu\n", u64Interval, *pu64FrameTimestamp_ms, *pu64FileStartTime_ms);

		av_init_packet(&packet);
		packet.data = (uint8_t *)pFrameData;	
		packet.pts = u64Interval * 1000; 	//转换成 us
		packet.dts = packet.pts;
//		printf("audio-------------------pts : %lld, dts : %lld\n", packet.pts, packet.dts);
		packet.size = iFrameSize;
	
		ret = m_Mp4Muxer.mp4_mux_write(&packet, AVMEDIA_TYPE_AUDIO);	
		if( 0 == ret )
		{
			RecoverParam_S stParam;
			stParam.timestamp = *pu64FrameTimestamp_ms;
			stParam.size = iFrameSize;
			stParam.type = 2;
//			disk_fwrite_file(m_fpRecover, (char *)&stParam, sizeof(stParam));
			ret = disk_write_file(m_fdRecover, (char *)&stParam, sizeof(stParam));
			if (-255 == ret) //IO错误当做卡异常处理
				CDiskManager::defaultStorageManager()->SetReadWriteErrorFlag(true);
		}
		
		// 如果是用 av_write_frame , 则必须手动调用 av_packet_unref 释放 pkg , 不然会造成内存泄露
		// 如果是用 av_interleaved_write_frame , 则不需要, av_interleaved_write_frame 内部会自己释放
		av_packet_unref(&packet);		
	}
	else if( 1 == iStreamType )	//video
	{
		u64Interval = *pu64FrameTimestamp_ms - *pu64FileStartTime_ms;
//		printf("video ---- %llu = %llu - %llu\n", u64Interval, *pu64FrameTimestamp_ms, *pu64FileStartTime_ms);

		av_init_packet(&packet);
		packet.data = (uint8_t *)pFrameData; 
		packet.pts = u64Interval * 1000;  //转换成 us
		packet.dts = packet.pts;	
//		printf("video-------------------pts : %lld, dts : %lld\n", packet.pts, packet.dts);
		packet.size = iFrameSize;
		
		if( 1 == iFrameType )	//I帧
			packet.flags |= AV_PKT_FLAG_KEY;
				
		ret = m_Mp4Muxer.mp4_mux_write(&packet, AVMEDIA_TYPE_VIDEO);
		if( 0 == ret )		
		{
			RecoverParam_S stParam;
			stParam.timestamp = *pu64FrameTimestamp_ms;
			stParam.size = iFrameSize;
			stParam.type = ( 1 == iFrameType )? 0:1;
//			disk_fwrite_file(m_fpRecover, (char *)&stParam, sizeof(stParam));
			ret = disk_write_file(m_fdRecover, (char *)&stParam, sizeof(stParam));
			if (-255 == ret) //IO错误当做卡异常处理
				CDiskManager::defaultStorageManager()->SetReadWriteErrorFlag(true);
		}

		// 如果是用 av_write_frame , 则必须手动调用 av_packet_unref 释放 pkg , 不然会造成内存泄露
		// 如果是用 av_interleaved_write_frame , 则不需要, av_interleaved_write_frame 内部会自己释放
		av_packet_unref(&packet);		
	}

	return 0;
}


////////////////////////////////////////////////////////回放部分//////////////////////////////////////////////////////////////////

Int32 CStorageManager::SearchRecord(record_file_time_s *pStRecordTime, Int32 buffer_size)
{
	int i = 0;

	CGuard guard(m_mutex_RcTime);

	TRecordFileTimeMap::iterator map_it;
	for( map_it = m_mapRecordTime.begin(); map_it != m_mapRecordTime.end(); map_it++ )
	{
		TRecordFileTimeList &listRecordTime = (map_it->second);
		TRecordFileTimeList::iterator list_it;
		for( list_it = listRecordTime.begin(); (list_it != listRecordTime.end()) && (i < buffer_size); list_it++, i++ )
		{
			pStRecordTime[i].iStartTime = list_it->iStartTime;
			pStRecordTime[i].iEndTime = list_it->iEndTime;
		}
		if( i >= buffer_size )
			break;
	}
	
	return i;
}

Int32 CStorageManager::SearchRecord(Int32 iStartTime, Int32 iEndTime, TRecordFileTimeList &listRecordTimeInfo)
{
	static record_file_info_s s_astSchRcdFileInfo2[STORAGE_MAX_FILE_PER_DAY];
	
	printf("SearchRecord ---> iStartTime : %d, iEndTime : %d\n", iStartTime, iEndTime);
	int ret;
	int file_num = 0;
	int start_date_index = GetDateIndex(iStartTime);
	int end_date_index = GetDateIndex(iEndTime);

	int year_begin = start_date_index / 10000;
	int month_begin = (start_date_index % 10000) / 100;
	int day_begin = start_date_index % 100;
	int year_end = end_date_index / 10000;
	int month_end = (end_date_index % 10000) / 100;
	int day_end = end_date_index % 100;
	printf("begin time[%04d-%02d-%02d], end time[%04d-%02d-%02d]\n", year_begin, month_begin, day_begin, year_end, month_end, day_end);
	
	for (int i = year_begin; i <= year_end; i++)
	{
		for (int j = month_begin; j <= month_end && j <= 12; j++)
		{
			for (int k = day_begin; k <= day_end && k <= 31; k++)
			{
				file_num = 0;
				//先把文件内容读出来再处理
				{
					CGuard guard(m_mutex);
					
					char strPath[128];
					snprintf(strPath, sizeof(strPath), __STORAGE_SD_MOUNT_PATH__"/DCIM/%04d/%02d/%02d/index", i, j, k);
					int fd_index = open(strPath, O_RDONLY);
					if (-1 == fd_index)
					{
						continue;
					}
					ret = disk_read_file(fd_index, (char *)&s_astSchRcdFileInfo2,	sizeof(s_astSchRcdFileInfo2));
					if (ret <= 0)
					{
						AppErr("read %s failed. ret: %d\n", strPath, ret);
						close(fd_index);
						continue;
					}
					close(fd_index);
					file_num = ret / sizeof(record_file_info_s);
				}
				for (int n = 0; n < file_num; n++)
				{
					if (0 == s_astSchRcdFileInfo2[n].iStartTime)
						continue;
					if( (iStartTime > s_astSchRcdFileInfo2[n].iEndTime) || (iEndTime < s_astSchRcdFileInfo2[n].iStartTime) )
						continue;
					if (0 == listRecordTimeInfo.size())
					{
						record_file_time_s stRecordTime;
						memcpy(&stRecordTime, &s_astSchRcdFileInfo2[n], sizeof(record_file_time_s));
						listRecordTimeInfo.push_back(stRecordTime);
					}
					else
					{
						TRecordFileTimeList::iterator it = listRecordTimeInfo.end();
						it--;
						if (1 == it->iRecType || 1 == s_astSchRcdFileInfo2[n].iRecType ||
							s_astSchRcdFileInfo2[n].iStartTime > (it->iEndTime + 1))
						{
							record_file_time_s stRecordTime;
							memcpy(&stRecordTime, &s_astSchRcdFileInfo2[n], sizeof(record_file_time_s));
							listRecordTimeInfo.push_back(stRecordTime);
						}
						else
						{
							if (s_astSchRcdFileInfo2[n].iEndTime > it->iEndTime)
								it->iEndTime = s_astSchRcdFileInfo2[n].iEndTime;
						}
					}
				}
			}
		}
	}
	
	return listRecordTimeInfo.size();
}

Int32 CStorageManager::SearchRecord(Int32 year, Int32 month, Int32 day, TRecordFileTimeList &listRecordTimeInfo, TRecordFileTimeList &listAlarmRecordTimeInfo)
{
	static record_file_info_s s_astSchRcdFileInfo[STORAGE_MAX_FILE_PER_DAY];
	
	printf("SearchRecord ---> %d-%d-%d\n", year, month, day);
	int date_index = year*10000 + month*100 + day;
	printf("date_index : %d\n", date_index);
	
	int ret;
	int file_num = 0;

	//先把文件内容读出来再处理
	{
		CGuard guard(m_mutex);
		
		char strPath[128];
		snprintf(strPath, sizeof(strPath), __STORAGE_SD_MOUNT_PATH__"/DCIM/%04d/%02d/%02d/index", year, month, day);
		int fd_index = open(strPath, O_RDONLY);
		if (-1 == fd_index)
		{
			AppErr("open %s failed.\n", strPath);
			return 0;
		}
		ret = disk_read_file(fd_index, (char *)&s_astSchRcdFileInfo,  sizeof(s_astSchRcdFileInfo));
		if (ret <= 0)
		{
			AppErr("read %s failed. ret: %d\n", strPath, ret);
			close(fd_index);
			return 0;
		}
		close(fd_index);
		file_num = ret / sizeof(record_file_info_s);
	}

	for (int i = 0; i < file_num; i++)
	{
		if (0 == s_astSchRcdFileInfo[i].iStartTime)
			continue;

		if (0 == listRecordTimeInfo.size())
		{
			record_file_time_s stRecordTime;
			memcpy(&stRecordTime, &s_astSchRcdFileInfo[i], sizeof(record_file_time_s));
			listRecordTimeInfo.push_back(stRecordTime);
		}
		else
		{
			TRecordFileTimeList::iterator it = listRecordTimeInfo.end();
			it--;
			if (it->iRecType != s_astSchRcdFileInfo[i].iRecType || s_astSchRcdFileInfo[i].iStartTime > (it->iEndTime+1) )
			{
				record_file_time_s stRecordTime;
				memcpy(&stRecordTime, &s_astSchRcdFileInfo[i], sizeof(record_file_time_s));
				listRecordTimeInfo.push_back(stRecordTime);
			}
			else
			{
				if (s_astSchRcdFileInfo[i].iEndTime > it->iEndTime)
					it->iEndTime = s_astSchRcdFileInfo[i].iEndTime;
			}
		}
	}

	//////////////////////////////////////////////////////////////////
	file_num = 0;
	//先把文件内容读出来再处理
	do
	{
		CGuard guard(m_mutex);
		
		char strPath[128];
		snprintf(strPath, sizeof(strPath), __STORAGE_SD_MOUNT_PATH__"/DCIM/%04d/%02d/%02d/alarm_index", year, month, day);
		int fd_index = open(strPath, O_RDONLY);
		if (-1 == fd_index)
		{
			AppErr("open %s failed.\n", strPath);
			break;
		}
		ret = disk_read_file(fd_index, (char *)&s_astSchRcdFileInfo,  sizeof(s_astSchRcdFileInfo));
		if (ret <= 0)
		{
			AppErr("read %s failed. ret: %d\n", strPath, ret);
			close(fd_index);
			break;
		}
		close(fd_index);
		file_num = ret / sizeof(record_file_info_s);
	}while (0);
	for (int i = 0; i < file_num; i++)
	{
		if (0 == s_astSchRcdFileInfo[i].iStartTime)
			continue;

		listAlarmRecordTimeInfo.push_back(s_astSchRcdFileInfo[i]);
	}
	
	return listRecordTimeInfo.size();
}

Int32 CStorageManager::CheckRecordOnMonth(Int32 iYear, Int32 iMonth)
{
	int iHaveRecordDay = 0; 		//有回放的天数, 一共有32位，每1位表示对应天数是否有数据，最右边一位表示第0天。
	
	char strPath[128] = "";
    struct dirent **ppMonthEntry = NULL;
	int iMonthIndex = 0;

	snprintf(strPath, sizeof(strPath), __STORAGE_SD_MOUNT_PATH__"/DCIM/%04d/%02d", iYear, iMonth);
	int nMonth = scandir(strPath, &ppMonthEntry, dir_filter_day, dir_compare);
	iMonthIndex = 0;
	while (iMonthIndex < nMonth)
	{
		int day = ConvertDateStrToInt(3, ppMonthEntry[iMonthIndex]->d_name);
		if ((day > 0) && (day < 32))
		{
			struct stat statbuf;
			snprintf(strPath, sizeof(strPath), __STORAGE_SD_MOUNT_PATH__"/DCIM/%04d/%02d/%s/index", iYear, iMonth, ppMonthEntry[iMonthIndex]->d_name);
			int ret = lstat(strPath, &statbuf);
			if (0 == ret && statbuf.st_size > 0)
			{
				iHaveRecordDay |= 0x01 << day;
			}
		}
		
		free(ppMonthEntry[iMonthIndex]);
		iMonthIndex++;
	}

	if(ppMonthEntry)
		free(ppMonthEntry);

	return iHaveRecordDay;
}

static unsigned char playback_buffer_g[STORAGE_MAX_FRAME_SIZE];
static record_file_info_s s_astPbRcdFileInfo[STORAGE_MAX_FILE_PER_DAY];
void CStorageManager::PlaybackProc(int index)
{	
	playback_manager_s *pPlayManager = &m_arrStPlaybackManager[index];

//	if( false == pPlayManager->bUsed )
//		return ;		

	int ret;
	bool bNeedSeek = false;
	char strRecordFileParentDir[128] = "";
	char strRecordFilePath[128] = "";

	unsigned char *pBuffer = NULL;
	
	record_file_info_s stRecordFileInfo = {0};

	Int64 i64LastFrameTimestamp_ms, i64LastFrameProcTime_ms, i64CurSysTime_ms, i64FrameInteral_ms;
	Int64 i64WaitTime_ms;
	struct timeval tv = {0};
	
	CMp4Demuxer mp4_demuxer;
	Mp4DemuxerFrameInfo_s stMp4DemuxerFrameInfo;
	
//	pBuffer = new unsigned char[STORAGE_MAX_FRAME_SIZE];
	pBuffer = playback_buffer_g;

	if( NULL == pBuffer )
	{
		return ;
	}

	while( pPlayManager->bThreadRunningFlag )
	{
		if( false == pPlayManager->bEnablePlay ) 		//此时没有文件需要播放
		{
//			printf("playback[%d] --- idel.\n", index);
			pPlayManager->bIdel = true;
			usleep(200000); 		//200ms
			continue;
		}

		pPlayManager->bIdel = false;

		printf("playback start time : %d, end time : %d, bSeekFlag[%d], seek time : %d\n", 
				pPlayManager->iStartTime, pPlayManager->iEndTime, pPlayManager->bSeekFlag, pPlayManager->iSeekTime);

		if( pPlayManager->iStartTime > pPlayManager->iEndTime )
		{
			pthread_mutex_lock(&m_mutexPlaybackManager);
			pPlayManager->bEnablePlay = false;
			pthread_mutex_unlock(&m_mutexPlaybackManager);
			continue;
		}
		
		//通过开始时间和结束时间定位到首个播放的文件位置			
		time_t t_start, t_end;
		struct tm tm_start = {0};
		struct tm tm_end = {0};
		int date_index_start, date_index_end;
		
		t_start = pPlayManager->iStartTime;						
		localtime_r((time_t*)&t_start, &tm_start);
		date_index_start = (tm_start.tm_year+1900)*10000 + (tm_start.tm_mon+1) * 100 + tm_start.tm_mday;
		printf("date_index_start : %d\n", date_index_start);
		
		t_end = pPlayManager->iEndTime;
		localtime_r((time_t*)&t_end, &tm_end);
		date_index_end = (tm_end.tm_year+1900)*10000 + (tm_end.tm_mon+1) * 100 + tm_end.tm_mday;
		printf("date_index_end : %d\n", date_index_end);

		memset(s_astPbRcdFileInfo, 0, sizeof(s_astPbRcdFileInfo));

		//查找开始文件位置, 如果没有找到包含开始时间的文件, 则顺位往后移
		int index_file_size = 0;
		int rec_file_num = 0;
		record_file_info_s *pastPbRcdInfo = NULL;
		//先把文件内容读出来再处理
		{
			CGuard guard(m_mutex);
			char strPath[128];
			snprintf(strPath, sizeof(strPath), __STORAGE_SD_MOUNT_PATH__"/DCIM/%04d/%02d/%02d/index", 
				tm_start.tm_year+1900, tm_start.tm_mon+1, tm_start.tm_mday);
			//printf("file: %s\n", strPath);
			int fd_index = open(strPath, O_RDONLY);
			if (-1 != fd_index)
			{
				index_file_size = disk_read_file(fd_index, (char *)s_astPbRcdFileInfo, sizeof(s_astPbRcdFileInfo));
				AppInfo("disk_read_file, ret: %d\n", index_file_size);
				close(fd_index);
			}
		}
		if (index_file_size > 0)
		{
			rec_file_num = index_file_size / sizeof(record_file_info_s);
			AppInfo("rec_file_num: %d\n", rec_file_num);
			int pos = 0;
			while(0 == s_astPbRcdFileInfo[pos].iStartTime && pos < rec_file_num)
				pos++;
			AppInfo("pos: %d\n", pos);
			if (pos < rec_file_num)
				pastPbRcdInfo = &s_astPbRcdFileInfo[pos];
			rec_file_num -= pos;
		}
		if (rec_file_num <= 0 || !pastPbRcdInfo)
		{
			AppErr("rec_file_num: %d, pastPbRcdInfo: %p\n", rec_file_num, pastPbRcdInfo);
			pthread_mutex_lock(&m_mutexPlaybackManager);
			pPlayManager->bEnablePlay = false;
			pthread_mutex_unlock(&m_mutexPlaybackManager);
			continue;
		}
		
		bool bPlaybackFirtFile = true;
		bool bSeekToAnotherFile = false;
		for( int i = 0; /*i < rec_file_num && */pPlayManager->bThreadRunningFlag && pPlayManager->bEnablePlay; /*i++*/)
		{
			//第一个文件
			if( true == bPlaybackFirtFile )
			{
				//printf("pastPbRcdInfo[%d].iEndTime = %d, pPlayManager->iStartTime = %d\n", i, pastPbRcdInfo[i].iEndTime, pPlayManager->iStartTime);
				if( pastPbRcdInfo[i].iEndTime <= pPlayManager->iStartTime )
				{
					i++;
					if( i >= rec_file_num )
						break;
					continue;
				}
			}

			i64LastFrameTimestamp_ms = 0;
			i64LastFrameProcTime_ms = 0;

			if (true == pPlayManager->bSeekFlag)
			{
				for( i = 0; i < rec_file_num; i++ )
				{
					if( pPlayManager->iSeekTime < pastPbRcdInfo[i].iStartTime )
					{
						pthread_mutex_lock(&m_mutexPlaybackManager);
						pPlayManager->bSeekFlag = false;
						pthread_mutex_unlock(&m_mutexPlaybackManager);
						break;
					}
					else if( (pPlayManager->iSeekTime >= pastPbRcdInfo[i].iStartTime) && (pPlayManager->iSeekTime <= pastPbRcdInfo[i].iEndTime) )
					{
						bSeekToAnotherFile = true;
						pthread_mutex_lock(&m_mutexPlaybackManager);
						pPlayManager->bSeekFlag = false;
						pthread_mutex_unlock(&m_mutexPlaybackManager);
						break;
					}
					else
					{
						int j = i+1;
						if( j < rec_file_num )
						{
							if( (pPlayManager->iSeekTime > pastPbRcdInfo[i].iEndTime) && (pPlayManager->iSeekTime < pastPbRcdInfo[j].iStartTime) )
							{
								pthread_mutex_lock(&m_mutexPlaybackManager);
								pPlayManager->bSeekFlag = false;
								pthread_mutex_unlock(&m_mutexPlaybackManager);
								break;
							}
						}
					}
				}
				if( i >= rec_file_num )
				{
					pthread_mutex_lock(&m_mutexPlaybackManager);
					pPlayManager->bSeekFlag = false;
					pPlayManager->bEnablePlay = false;
					pthread_mutex_unlock(&m_mutexPlaybackManager);
					break;
				}
			}

			//printf("pastPbRcdInfo[%d].iStartTime = %d, pPlayManager->iEndTime = %d\n", i, pastPbRcdInfo[i].iStartTime, pPlayManager->iEndTime);
			if( pastPbRcdInfo[i].iStartTime > pPlayManager->iEndTime ) 	//超出播放的时间范围
				i = rec_file_num;
			
			if( i == rec_file_num) 	//自动播放到结尾,不退出循环,再次拖动进度条可以继续播放
			{
				usleep(200000); 		//200ms
				continue;
			}
			
			stRecordFileInfo = pastPbRcdInfo[i];

			
			//获取目录
			{
				struct tm tmStart = {0};
				struct tm tmEnd = {0};

				localtime_r((time_t*)&stRecordFileInfo.iStartTime, &tmStart);
				localtime_r((time_t*)&stRecordFileInfo.iEndTime, &tmEnd);
				

				if( 0 == stRecordFileInfo.iRecType )
				{
					snprintf(strRecordFilePath, sizeof(strRecordFilePath), __STORAGE_SD_MOUNT_PATH__"/DCIM/%04d/%02d/%02d/%04d%02d%02d%02d%02d%02d-%04d%02d%02d%02d%02d%02d.mp4", 
													tmStart.tm_year+1900, tmStart.tm_mon+1, tmStart.tm_mday, 
													tmStart.tm_year+1900, tmStart.tm_mon+1, tmStart.tm_mday, tmStart.tm_hour, tmStart.tm_min, tmStart.tm_sec, 
													tmEnd.tm_year+1900, tmEnd.tm_mon+1, tmEnd.tm_mday, tmEnd.tm_hour, tmEnd.tm_min, tmEnd.tm_sec);
				}
				else
				{
					snprintf(strRecordFilePath, sizeof(strRecordFilePath), __STORAGE_SD_MOUNT_PATH__"/DCIM/%04d/%02d/%02d/%04d%02d%02d%02d%02d%02d-%04d%02d%02d%02d%02d%02d_ALARM.mp4", 
													tmStart.tm_year+1900, tmStart.tm_mon+1, tmStart.tm_mday, 
													tmStart.tm_year+1900, tmStart.tm_mon+1, tmStart.tm_mday, tmStart.tm_hour, tmStart.tm_min, tmStart.tm_sec, 
													tmEnd.tm_year+1900, tmEnd.tm_mon+1, tmEnd.tm_mday, tmEnd.tm_hour, tmEnd.tm_min, tmEnd.tm_sec);
				}
				AppErr("[record playback] open %s\n", strRecordFilePath);
			}

			{//for debug
				gettimeofday(&tv, NULL);
				i64CurSysTime_ms = ((unsigned long long)tv.tv_sec * 1000 + tv.tv_usec / 1000);
			}


			ret = mp4_demuxer.Open(strRecordFilePath, m_eVideoEncType);

			{//for debug
				gettimeofday(&tv, NULL);
				i64WaitTime_ms = ((unsigned long long)tv.tv_sec * 1000 + tv.tv_usec / 1000) - i64CurSysTime_ms;
				printf("open mp4 file take %lld ms\n", i64WaitTime_ms);
			}


			
			if( 0 == ret )
			{
				//第一个文件
				if( true == bPlaybackFirtFile )
				{
					bPlaybackFirtFile = false;
					//seek到开始播放时间点
					if( pPlayManager->iStartTime > stRecordFileInfo.iStartTime )
					{
						#if 0
						float fPercent = (pPlayManager->iStartTime - stRecordFileInfo.iStartTime) * 100 / (stRecordFileInfo.iEndTime - stRecordFileInfo.iStartTime);
						mp4_demuxer.Seek(fPercent);
						#else
						mp4_demuxer.Seek((Int64)((pPlayManager->iStartTime - stRecordFileInfo.iStartTime)*1000000));
						#endif
					}
				}

				//seek到其他文件
				if( true == bSeekToAnotherFile )
				{
					bSeekToAnotherFile = false;
					if( pPlayManager->iSeekTime > stRecordFileInfo.iStartTime )
					{
						mp4_demuxer.Seek((Int64)((pPlayManager->iSeekTime - stRecordFileInfo.iStartTime)*1000000));
					}
				}
				
				while( pPlayManager->bThreadRunningFlag && pPlayManager->bEnablePlay )
				{
					if( true == pPlayManager->bSeekFlag )
					{
						if( (pPlayManager->iSeekTime >= stRecordFileInfo.iStartTime) && (pPlayManager->iSeekTime <= stRecordFileInfo.iEndTime) ) 	//文件内seek
						{
							pthread_mutex_lock(&m_mutexPlaybackManager);
							pPlayManager->bSeekFlag = false;
							pthread_mutex_unlock(&m_mutexPlaybackManager);
						
							//重置时间戳
							i64LastFrameTimestamp_ms = 0;
							i64LastFrameProcTime_ms = 0;
						
							/////////////////////注意时间/////////////////////////
							ret = mp4_demuxer.Seek((long long)(pPlayManager->iSeekTime - stRecordFileInfo.iStartTime) * 1000000);	/////////////seek
							printf("playback seek ret : %d\n", ret);
						}
						else 		//seek到其他文件
						{
							break;
						}
					}

					if( true == pPlayManager->bPause )
					{
						usleep(100000); 		//100ms
						continue;
					}

					ret = mp4_demuxer.Read(pBuffer, STORAGE_MAX_FRAME_SIZE, &stMp4DemuxerFrameInfo);
					if( ret <= 0 )
					{
						break;
					}

					//不作此判断，因为有可能最后一个文件有几秒钟是第二天的，让文件播完
					#if 0
					if( stRecordFileInfo.iStartTime + (stMp4DemuxerFrameInfo.ullTimestamp/1000) > pPlayManager->iEndTime )
					{
						break; 		//播放完成
					}
					#endif

					if( 1 == stMp4DemuxerFrameInfo.iStreamType )
					{
						i64FrameInteral_ms = stMp4DemuxerFrameInfo.ullTimestamp - i64LastFrameTimestamp_ms;
						gettimeofday(&tv, NULL);
						i64CurSysTime_ms = ((unsigned long long)tv.tv_sec * 1000 + tv.tv_usec / 1000);
						
						i64WaitTime_ms = i64FrameInteral_ms - (i64CurSysTime_ms - i64LastFrameProcTime_ms);
						if( i64WaitTime_ms > 0 )
						{
//							printf("wait %lld ms...\n", i64WaitTime_ms);
							if( i64WaitTime_ms > 1000 )
							{
								printf("playback i64WaitTime_ms[%lld] err! set to 1000ms.\n");
								i64WaitTime_ms = 1000;
							}
							i64WaitTime_ms /= pPlayManager->fSpeed;
//							printf("wait %lld ms...\n", i64WaitTime_ms);
							struct timeval t;
							t.tv_sec = i64WaitTime_ms / 1000;
							t.tv_usec = (i64WaitTime_ms % 1000) * 1000;
							while(select(0, NULL, NULL, NULL, &t) != 0);
						}
						else
						{
							printf("playback proc frame timeout...\n");
						}

						gettimeofday(&tv, NULL);
						i64LastFrameProcTime_ms = ((unsigned long long)tv.tv_sec * 1000 + tv.tv_usec / 1000);
						i64LastFrameTimestamp_ms = stMp4DemuxerFrameInfo.ullTimestamp;
					}
					
					//转换为录制时的真实时间
					stMp4DemuxerFrameInfo.ullTimestamp += (unsigned long long)stRecordFileInfo.iStartTime * 1000;

//						printf("stRecordFileInfo.iStartTime : %d\n", stRecordFileInfo.iStartTime);
//						printf("stMp4DemuxerFrameInfo.ullTimestamp : %llu\n", stMp4DemuxerFrameInfo.ullTimestamp);
					pPlayManager->PlaybackProc(pBuffer, ret, &stMp4DemuxerFrameInfo, pPlayManager->pParam);
				}
			}
			mp4_demuxer.Close();
			
			i++;
		}
		
		//回放结束
		{
			pthread_mutex_lock(&m_mutexPlaybackManager);
			pPlayManager->bEnablePlay = false;
			pthread_mutex_unlock(&m_mutexPlaybackManager);
		
			pPlayManager->PlaybackProc(NULL, 0, NULL, pPlayManager->pParam);			
		}				
		
	}

end:

//	if( pBuffer )
//		delete [] pBuffer;
	
	return ;
}


//启动回放
Int32 CStorageManager::StartPlayback(Int32 iStartTime, Int32 iEndTime, StoragePlaybackDownloadProc proc, void *param)
{
	if( ( iStartTime < 0 ) || (iStartTime > iEndTime) )
		return -1;
	
	int i;
	int ret = -1;
	playback_thread_param_s *pThreadParam = NULL;
	pthread_mutex_lock(&m_mutexPlaybackManager);

	for( i = 0; i < MAX_PLAYBACK_NUM; i++ )	
	{
		if( false == m_arrStPlaybackManager[i].bEnablePlay )
		{
			m_arrStPlaybackManager[i].iStartTime = iStartTime;
			m_arrStPlaybackManager[i].iEndTime = iEndTime;
			
			m_arrStPlaybackManager[i].PlaybackProc = proc;
			m_arrStPlaybackManager[i].pParam = param;
			
			m_arrStPlaybackManager[i].fSpeed = STORAGE_DEF_PB_SPEED;
			m_arrStPlaybackManager[i].bPause = false;
			m_arrStPlaybackManager[i].bSeekFlag = false;
			m_arrStPlaybackManager[i].bEnablePlay = true;


			if( false == m_arrStPlaybackManager[i].bThreadRunningFlag )
			{
				m_arrStPlaybackManager[i].bThreadRunningFlag = true;
				
				pThreadParam = new playback_thread_param_s;
				if( NULL == pThreadParam )
				{
					ret = -1;
					goto falied;
				}

				pThreadParam->p = this;
				pThreadParam->index = i;

				//创建线程			
				ret = pthread_create( &m_arrStPlaybackManager[i].threadPlayback_tid, NULL, thread_playback, pThreadParam);
				if( 0 != ret )
				{
					ret = -1;
					goto falied;
				}
			}

			ret = i;
			break;
		}
	}
	if (i >= MAX_PLAYBACK_NUM)
		ret = -2;
	
	pthread_mutex_unlock(&m_mutexPlaybackManager);
	return ret;

falied:
	
	m_arrStPlaybackManager[i].fSpeed = STORAGE_DEF_PB_SPEED;
	m_arrStPlaybackManager[i].bSeekFlag = false;
	m_arrStPlaybackManager[i].bEnablePlay = false;
	m_arrStPlaybackManager[i].bThreadRunningFlag = false;
	
	pthread_mutex_unlock(&m_mutexPlaybackManager);

	if( pThreadParam )
		delete pThreadParam;

	return ret;
}

Int32 CStorageManager::StopPlayback(Int32 iPlayHandle)
{
	if( iPlayHandle < 0 || iPlayHandle >= MAX_PLAYBACK_NUM )
		return -1;
	
	pthread_mutex_lock(&m_mutexPlaybackManager);
	
	m_arrStPlaybackManager[iPlayHandle].fSpeed = STORAGE_DEF_PB_SPEED;
	m_arrStPlaybackManager[iPlayHandle].bPause = false;
	m_arrStPlaybackManager[iPlayHandle].bSeekFlag = false;
	m_arrStPlaybackManager[iPlayHandle].bEnablePlay = false;
	
	pthread_mutex_unlock(&m_mutexPlaybackManager);

	//等待线程进入空闲状态
	int count = 5*20;
	while(count > 0)
	{
		if( m_arrStPlaybackManager[iPlayHandle].bIdel )
			break;
		count--;
		usleep(50000);
	}

	return 0;
}

Int32 CStorageManager::SeekTime(Int32 iPlayHandle, Int32 iStartTime)
{
	if( iPlayHandle < 0 || iPlayHandle >= MAX_PLAYBACK_NUM )
		return -1;

	pthread_mutex_lock(&m_mutexPlaybackManager);

	if( m_arrStPlaybackManager[iPlayHandle].bEnablePlay )
	{
		m_arrStPlaybackManager[iPlayHandle].iSeekTime = iStartTime;
		m_arrStPlaybackManager[iPlayHandle].bSeekFlag = true;
		m_arrStPlaybackManager[iPlayHandle].bPause = false;
	}
	
	pthread_mutex_unlock(&m_mutexPlaybackManager);

	return 0;
}

/*
 *@param bPause true 暂停, false 取消暂停
 */
Int32 CStorageManager::PausePlaybackOnFile(Int32 iPlayHandle, bool bPause)
{
	if( iPlayHandle < 0 || iPlayHandle >= MAX_PLAYBACK_NUM )
		return -1;

	pthread_mutex_lock(&m_mutexPlaybackManager);

	if( m_arrStPlaybackManager[iPlayHandle].bEnablePlay )
	{
		m_arrStPlaybackManager[iPlayHandle].bPause = bPause;
	}

	pthread_mutex_unlock(&m_mutexPlaybackManager);
	
	return 0;
}

/*
 * 设置回放倍速
 *@param fSpeed 回放倍速, >=STORAGE_MIN_PB_SPEED && <=STORAGE_MAX_PB_SPEED
 */
Int32 CStorageManager::SetPlaybackSpeed(Int32 iPlayHandle, float fSpeed)
{
	if( iPlayHandle < 0 || iPlayHandle >= MAX_PLAYBACK_NUM || 
		fSpeed < STORAGE_MIN_PB_SPEED || fSpeed > STORAGE_MAX_PB_SPEED )
		return -1;

	pthread_mutex_lock(&m_mutexPlaybackManager);

	if( m_arrStPlaybackManager[iPlayHandle].bEnablePlay )
	{
		m_arrStPlaybackManager[iPlayHandle].fSpeed = fSpeed;
	}

	pthread_mutex_unlock(&m_mutexPlaybackManager);
	
	return 0;
}


////////////////////////////////////////////////////////录像删除//////////////////////////////////////////////////////////////////
/*
 *按天删除录像
 */
Int32 CStorageManager::DeleteRecordByDay(Int32 year, Int32 month, Int32 day)
{
	int del_date_index = year*10000 + month*100 + day;
	int cur_date_index = GetDateIndex(time(0));
	bool bNeedRecoverRecordMode = false;
	int iOldRecordMode = m_iRecordMode;

	printf("del record ---> %d-%d-%d\n", year, month, day);
	
	//删除当天录像
	if( del_date_index == cur_date_index )
	{
		if( DISK_RECORD_MODE_CLOSE != m_iRecordMode ) 	//正在录像, 要先停止录像
		{
			m_iRecordMode = DISK_RECORD_MODE_CLOSE;
			int count = 100;
			while( (count-- > 0) && m_bRecordWorking)
				usleep(50000);
			if( count <= 0 )
			{
				printf("suspend record fail.\n");
				return -1; 	//超时退出
			}
			bNeedRecoverRecordMode = true;
		}
	}

	//删除录像文件
	{
		char path[128];
		char cmd[128];
		snprintf(path, sizeof(path), __STORAGE_SD_MOUNT_PATH__"/DCIM/%04d/%02d/%02d/", year, month, day);
		snprintf(cmd, sizeof(cmd), "rm -rf %s", path);
		printf("cmd : %s\n", cmd);
		__STORAGE_SYSTEM__("sh", "sh", "-c", cmd, NULL);
		printf("remove record dir[%s]\n", path);
	}

	#if 0
	//删除录像文件索引
	{
		CGuard guard(m_mutex);
		
		TRecordFileMap::iterator it;
		it = m_mapRecordFile.find(del_date_index);
		if( it != m_mapRecordFile.end() )
		{
			printf("found mapRecordFile[%d]\n", del_date_index);
			TRecordFileInfoList *pListRecordFileInfo = it->second;
			if( pListRecordFileInfo )
			{
				//删除文件索引list
				delete pListRecordFileInfo;
				pListRecordFileInfo = NULL;
				it->second = NULL;
			}
			m_mapRecordFile.erase(it);
		}
		printf("remove mapRecordFile.\n");
	}
	
	//删除录像文件时间记录
	{
		CGuard guard(m_mutex_RcTime);
		
		TRecordFileTimeMap::iterator map_it;
		map_it = m_mapRecordTime.find(del_date_index);
		if( map_it != m_mapRecordTime.end() )
		{
			printf("found mapRecordTime[%d]\n", del_date_index);
			m_mapRecordTime.erase(map_it);
		}
		printf("remove mapRecordTime.\n");
	}
	#endif

	//恢复录像模式
	if( bNeedRecoverRecordMode )
		m_iRecordMode = iOldRecordMode;
	
	return 0;
}

////////////////////////////////////////////////////////录像下载//////////////////////////////////////////////////////////////////

static unsigned char download_buffer_g[STORAGE_MAX_FRAME_SIZE];
static record_file_info_s s_astDlRcdFileInfo[STORAGE_MAX_FILE_PER_DAY];
void CStorageManager::DownloadProc(int index)
{	
	download_manager_s *pDownloadManager = &m_arStDownloadManager[index];

	int ret;
	char strRecordFileParentDir[128] = "";
	char strRecordFilePath[128] = "";

	unsigned char *pBuffer = NULL;
	
	record_file_info_s stRecordFileInfo = {0};

	Int64 i64LastFrameTimestamp_ms, i64LastFrameProcTime_ms, i64CurSysTime_ms, i64FrameInteral_ms;
	Int64 i64WaitTime_ms;
	struct timeval tv = {0};
	
	CMp4Demuxer mp4_demuxer;
	Mp4DemuxerFrameInfo_s stMp4DemuxerFrameInfo;
	
//	pBuffer = new unsigned char[STORAGE_MAX_FRAME_SIZE];
	pBuffer = download_buffer_g;

	if( NULL == pBuffer )
	{
		return ;
	}
	if( pDownloadManager->iStartTime > pDownloadManager->iEndTime )
	{
		return ;
	}

	printf("download start time : %d, end time : %d\n", pDownloadManager->iStartTime, pDownloadManager->iEndTime);

	
	//通过开始时间和结束时间定位到首个播放的文件位置			
	time_t t_start, t_end;
	struct tm tm_start = {0};
	struct tm tm_end = {0};
	int date_index_start, date_index_end;
	
	t_start = pDownloadManager->iStartTime;						
	localtime_r((time_t*)&t_start, &tm_start);
	date_index_start = (tm_start.tm_year+1900)*10000 + (tm_start.tm_mon+1) * 100 + tm_start.tm_mday;
	printf("date_index_start : %d\n", date_index_start);
	
	t_end = pDownloadManager->iEndTime;						
	localtime_r((time_t*)&t_end, &tm_end);
	date_index_end = (tm_end.tm_year+1900)*10000 + (tm_end.tm_mon+1) * 100 + tm_end.tm_mday;
	printf("date_index_end : %d\n", date_index_end);


	//查找开始文件位置, 如果没有找到包含开始时间的文件, 则顺位往后移
	int index_file_size = 0;
	int rec_file_num = 0;
	record_file_info_s *pastPbRcdInfo = NULL;
	//先把文件内容读出来再处理
	{
		CGuard guard(m_mutex);
		char strPath[128];
		snprintf(strPath, sizeof(strPath), __STORAGE_SD_MOUNT_PATH__"/DCIM/%04d/%02d/%02d/index", 
			tm_start.tm_year+1900, tm_start.tm_mon+1, tm_start.tm_mday);
		int fd_index = open(strPath, O_RDONLY);
		if (-1 != fd_index)
		{
			index_file_size = disk_read_file(fd_index, (char *)s_astDlRcdFileInfo, sizeof(s_astDlRcdFileInfo));
			close(fd_index);
		}
	}
	if (index_file_size > 0)
	{
		rec_file_num = index_file_size / sizeof(record_file_info_s);
		int pos = 0;
		while(0 == s_astDlRcdFileInfo[pos].iStartTime && pos < rec_file_num)
			pos++;
		rec_file_num -= pos;
		if (pos < rec_file_num)
			pastPbRcdInfo = &s_astDlRcdFileInfo[pos];
	}
	if (rec_file_num <= 0 || !pastPbRcdInfo)
	{
		return ;	//找不到录像文件
	}

	bool bPlaybackFirtFile = true;
	for( int i = 0; i < rec_file_num && pDownloadManager->bThreadRunningFlag; i++)
	{			
		//第一个文件
		if( true == bPlaybackFirtFile )
		{
			if( pastPbRcdInfo[i].iEndTime <= pDownloadManager->iStartTime )
			{
				continue;
			}
		}

		i64LastFrameTimestamp_ms = 0;
		i64LastFrameProcTime_ms = 0;			

		if( pastPbRcdInfo[i].iStartTime > pDownloadManager->iEndTime ) 	//超出下载的时间范围
			break;
		
		stRecordFileInfo = pastPbRcdInfo[i];

		
		//获取目录
		{
			struct tm tmStart = {0};
			struct tm tmEnd = {0};

			localtime_r((time_t*)&stRecordFileInfo.iStartTime, &tmStart);
			localtime_r((time_t*)&stRecordFileInfo.iEndTime, &tmEnd);
			

			if( 0 == stRecordFileInfo.iRecType )
			{
				snprintf(strRecordFilePath, sizeof(strRecordFilePath), __STORAGE_SD_MOUNT_PATH__"/DCIM/%04d/%02d/%02d/%04d%02d%02d%02d%02d%02d-%04d%02d%02d%02d%02d%02d.mp4", 
												tmStart.tm_year+1900, tmStart.tm_mon+1, tmStart.tm_mday, 
												tmStart.tm_year+1900, tmStart.tm_mon+1, tmStart.tm_mday, tmStart.tm_hour, tmStart.tm_min, tmStart.tm_sec, 
												tmEnd.tm_year+1900, tmEnd.tm_mon+1, tmEnd.tm_mday, tmEnd.tm_hour, tmEnd.tm_min, tmEnd.tm_sec);
			}
			else
			{
				snprintf(strRecordFilePath, sizeof(strRecordFilePath), __STORAGE_SD_MOUNT_PATH__"/DCIM/%04d/%02d/%02d/%04d%02d%02d%02d%02d%02d-%04d%02d%02d%02d%02d%02d_ALARM.mp4", 
												tmStart.tm_year+1900, tmStart.tm_mon+1, tmStart.tm_mday, 
												tmStart.tm_year+1900, tmStart.tm_mon+1, tmStart.tm_mday, tmStart.tm_hour, tmStart.tm_min, tmStart.tm_sec, 
												tmEnd.tm_year+1900, tmEnd.tm_mon+1, tmEnd.tm_mday, tmEnd.tm_hour, tmEnd.tm_min, tmEnd.tm_sec);
			}
			AppErr("[record playback] open %s\n", strRecordFilePath);
		}

		{//for debug
			gettimeofday(&tv, NULL);
			i64CurSysTime_ms = ((unsigned long long)tv.tv_sec * 1000 + tv.tv_usec / 1000);
		}


		ret = mp4_demuxer.Open(strRecordFilePath, m_eVideoEncType);

		{//for debug
			gettimeofday(&tv, NULL);
			i64WaitTime_ms = ((unsigned long long)tv.tv_sec * 1000 + tv.tv_usec / 1000) - i64CurSysTime_ms;
			printf("open mp4 file take %lld ms\n", i64WaitTime_ms);
		}


		
		if( 0 == ret )
		{
			//第一个文件
			if( true == bPlaybackFirtFile )
			{
				bPlaybackFirtFile = false;
				//seek到开始下载时间点
				if( pDownloadManager->iStartTime > stRecordFileInfo.iStartTime )
				{
					#if 0
					float fPercent = (pDownloadManager->iStartTime - stRecordFileInfo.iStartTime) * 100 / (stRecordFileInfo.iEndTime - stRecordFileInfo.iStartTime);
					mp4_demuxer.Seek(fPercent);
					#else
					mp4_demuxer.Seek((Int64)((pDownloadManager->iStartTime - stRecordFileInfo.iStartTime)*1000000));
					#endif
				}
			}
			
			while( pDownloadManager->bThreadRunningFlag )
			{					
				if( true == pDownloadManager->bPause )
				{
					usleep(100000); 		//100ms
					continue;
				}

				ret = mp4_demuxer.Read(pBuffer, STORAGE_MAX_FRAME_SIZE, &stMp4DemuxerFrameInfo);
				if( ret <= 0 )
				{
					break;
				}

				if( stRecordFileInfo.iStartTime + (stMp4DemuxerFrameInfo.ullTimestamp/1000) > pDownloadManager->iEndTime )
				{
					break; 		//下载完成
				}

				/*
				if( 1 == stMp4DemuxerFrameInfo.iStreamType )
				{
					i64FrameInteral_ms = stMp4DemuxerFrameInfo.ullTimestamp - i64LastFrameTimestamp_ms;
					gettimeofday(&tv, NULL);
					i64CurSysTime_ms = ((unsigned long long)tv.tv_sec * 1000 + tv.tv_usec / 1000);
					
					i64WaitTime_ms = i64FrameInteral_ms - (i64CurSysTime_ms - i64LastFrameProcTime_ms);
					if( i64WaitTime_ms > 0 )
					{
//								printf("wait %lld ms...\n", i64WaitTime_ms);
						//<shang> 后期要考虑修改一下, 如果异常导致 i64WaitTime_ms 很大会有问题
						struct timeval t;
						t.tv_sec = i64WaitTime_ms / 1000;
						t.tv_usec = (i64WaitTime_ms % 1000) * 1000;
						while(select(0, NULL, NULL, NULL, &t) != 0);
					}
					else
					{
						printf("playback proc frame timeout...\n");
					}

					gettimeofday(&tv, NULL);
					i64LastFrameProcTime_ms = ((unsigned long long)tv.tv_sec * 1000 + tv.tv_usec / 1000);
					i64LastFrameTimestamp_ms = stMp4DemuxerFrameInfo.ullTimestamp;
				}
				*/
				
				//转换为录制时的真实时间
				stMp4DemuxerFrameInfo.ullTimestamp += (unsigned long long)stRecordFileInfo.iStartTime * 1000;

//						printf("stRecordFileInfo.iStartTime : %d\n", stRecordFileInfo.iStartTime);
//						printf("stMp4DemuxerFrameInfo.ullTimestamp : %llu\n", stMp4DemuxerFrameInfo.ullTimestamp);
				pDownloadManager->DownloadProc(pBuffer, ret, &stMp4DemuxerFrameInfo, pDownloadManager->pParam);
			}
		}
		mp4_demuxer.Close();			
	}
	
	//下载结束
	{		
		pDownloadManager->DownloadProc(NULL, 0, NULL, pDownloadManager->pParam);			
	}				
	
end:

//	if( pBuffer )
//		delete [] pBuffer;
	
	return ;
}


//启动下载
Int32 CStorageManager::StartDownload(Int32 iStartTime, Int32 iEndTime, StoragePlaybackDownloadProc proc, void *param)
{	
	if( ( iStartTime < 0 ) || (iStartTime > iEndTime) )
		return -1;
	
	int i;
	int ret = -1;
	playback_thread_param_s *pThreadParam = NULL;
	pthread_mutex_lock(&m_mutexDownloadManager);

	for( i = 0; i < MAX_PLAYBACK_NUM; i++ )	
	{
		if( false == m_arStDownloadManager[i].bThreadRunningFlag )
		{
			m_arStDownloadManager[i].iStartTime = iStartTime;
			m_arStDownloadManager[i].iEndTime = iEndTime;
			
			m_arStDownloadManager[i].DownloadProc = proc;
			m_arStDownloadManager[i].pParam = param;
			
			m_arStDownloadManager[i].bPause = false;
			m_arStDownloadManager[i].bThreadRunningFlag = true;
			
			pThreadParam = new playback_thread_param_s;
			if( NULL == pThreadParam )
			{
				ret = -1;
				goto falied;
			}

			pThreadParam->p = this;
			pThreadParam->index = i;

			//创建线程			
			ret = pthread_create( &m_arStDownloadManager[i].threadDownload_tid, NULL, thread_download, pThreadParam);
			if( 0 != ret )
			{
				ret = -1;
				goto falied;
			}

			ret = i;
			break;
		}
	}
	
	pthread_mutex_unlock(&m_mutexDownloadManager);
	return ret;

falied:
	
	m_arStDownloadManager[i].bThreadRunningFlag = false;
	
	pthread_mutex_unlock(&m_mutexDownloadManager);

	if( pThreadParam )
		delete pThreadParam;

	return ret;
}

Int32 CStorageManager::StopDownload(Int32 iDownloadHandle)
{
	if( iDownloadHandle < 0 || iDownloadHandle >= MAX_PLAYBACK_NUM )
		return -1;
	
	pthread_mutex_lock(&m_mutexDownloadManager);
	
	m_arStDownloadManager[iDownloadHandle].bPause = false;

	//等待下载线程退出
	if( m_arStDownloadManager[iDownloadHandle].bThreadRunningFlag )
	{
		m_arStDownloadManager[iDownloadHandle].bThreadRunningFlag = false;

		void *result;
		pthread_join(m_arStDownloadManager[iDownloadHandle].threadDownload_tid, &result);
		printf("record download thread finish.\n");
	}
	
	pthread_mutex_unlock(&m_mutexDownloadManager);

	return 0;
}

/*
 *@param bPause true 暂停, false 取消暂停
 */
Int32 CStorageManager::PauseDownload(Int32 iDownloadHandle, bool bPause)
{
	if( iDownloadHandle < 0 || iDownloadHandle >= MAX_PLAYBACK_NUM )
		return -1;

	pthread_mutex_lock(&m_mutexDownloadManager);
	
	m_arStDownloadManager[iDownloadHandle].bPause = bPause;

	pthread_mutex_unlock(&m_mutexDownloadManager);
	
	return 0;
}

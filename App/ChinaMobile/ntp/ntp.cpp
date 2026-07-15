#include <time.h>
#include "TPBase/TPSocket/TPSocket.h"
#include "Common.h"
#include "../dev_log.h"


//static char *pNTP_server[] = {"210.72.145.44", "202.120.2.101", "182.92.12.11", "120.25.115.19", "133.100.11.8", "132.163.4.101", "131.107.1.10", "207.126.98.204"};
static char *pNTP_server[] = {"182.92.12.11", "120.25.115.19", "133.100.11.8", "132.163.4.101", "131.107.1.10", "207.126.98.204"};

#define JAN_1970    0x83aa7e80 					/* 2208988800 seconds (1970 - 1900)*/
#define NTPFRAC(x) ( 4294*(x) + ( (1981*(x))>>11 ) )
#define USEC(x) (((x) >> 12 ) - 759 * ((( (x) >> 10 ) + 32768 ) >> 16 ))


static int s_time_calibrated = 0;


extern socket_t tp_udp_connect(const char * hostip, const port_t port);

static void *thread_ntp(void *args)
{
	int iSocket = -1;
	int iRet = -1;
	uint uiDataBuffer[12] = {0};
	SystemTime szSystemTime;
	struct tm *ptmTime;	
	struct timeval tvTimeout;
	time_t stNow,stNTPTime;
	fd_set ReadSet;
//	char cHost[16] = {0};
	struct in_addr inaddr = {0};
	unsigned short server_port = 123;

#if 0
	iRet = tp_gethostbyname(m_cCfgNtp.Server.ServerName, &inaddr);
	if(iRet < 0)
	{
		strcpy(cHost, m_cCfgNtp.Server.ServerName);
	}
	else
	{
		strncpy(cHost, inet_ntoa(inaddr), 16);
	}
#else
	//ntp1.aliyun.com
//<shang>	strcpy(cHost, "182.92.12.11");

	
#endif

	while (1)
	{
		for( int i = 0; i < sizeof(pNTP_server)/sizeof(char *); i++ )
		{
			NsDebug("NTPD: NTP host[%s], port[%d]\n", pNTP_server[i], server_port) ; 
			iSocket = tp_udp_connect(pNTP_server[i], server_port);
			if (iSocket < 0)
			{
				tracepoint();
				continue;
			}	

			memset(uiDataBuffer,0,sizeof(uiDataBuffer));
			uiDataBuffer[0] = htonl ((0 << 30) | (3 << 27) | (3 << 24) | (0 << 16) | (4 << 8) | ((-6) & 0xff));
			uiDataBuffer[1] = htonl(1<<16); 
			uiDataBuffer[2] = htonl(1<<16); 
			stNow = time(NULL);
			uiDataBuffer[10] = htonl((unsigned long)stNow + JAN_1970);
			uiDataBuffer[11] = htonl(0);
			
			iRet = tp_writen(iSocket, uiDataBuffer, sizeof(uiDataBuffer));
			if (iRet <= 0)
			{
				tracepoint();
				tp_close(iSocket);
				continue;
			}
			
			tvTimeout.tv_sec = 2;
			tvTimeout.tv_usec = 0;
			FD_ZERO(&ReadSet);
			FD_SET(iSocket, &ReadSet);
			iRet = select(iSocket + 1, &ReadSet, NULL, NULL, &tvTimeout);
			if (iRet <= 0)
			{
				NsErr("NTPD: Recv Packet Timeout!\n");
				tp_close(iSocket);
				continue;
			}
			
			if (FD_ISSET(iSocket, &ReadSet))
			{
				memset(uiDataBuffer,0,sizeof(uiDataBuffer));
				iRet = recv(iSocket,(char *)uiDataBuffer, sizeof(uiDataBuffer), 0);
				if (iRet == sizeof(uiDataBuffer))
				{
					stNow = time(NULL);

#define DataOffset(i) ntohl(((uint *)uiDataBuffer)[i])
					if ((DataOffset(0) >> 27 & 0x07) < 3 
						|| ((DataOffset(0) >> 24 & 0x07) != 4 
								&&(DataOffset(0) >> 24 & 0x07) != 5))
					{
						NsErr("NTPD: Rec Packet error!\n");
						tp_close(iSocket);
						continue;
					}

					stNTPTime  = DataOffset(10) - JAN_1970;
					stNTPTime += ( (stNow + JAN_1970 - DataOffset(6))-(DataOffset(10) - DataOffset(8)) )/2 ;
//					stNTPTime += 8 * 60 * 60;
					// 设置时区
					const char *str_time_zone = "CST-8";
					if(setenv("TZ", str_time_zone, 1) != 0)
					{
						printf("init timezone %s error\n", str_time_zone);
					}
					else
					{
						printf("init timezone ok\n");
					}
					tzset();
					printf("daylight : %d, timezone : %d, tzname[0] : %s, tzname[1] : %s\n", daylight, timezone, tzname[0], tzname[1]);
#undef DataOffset

					struct	timeval tv;
					gettimeofday(&tv, NULL);
					if( (abs(tv.tv_sec - stNTPTime) >= 5) )
					{
						//校时
						time_t old_sec = tv.tv_sec;
						printf("tv_sec = %ld\n", tv.tv_sec);
						printf("stNTPTime = %ld\n", stNTPTime);
						tv.tv_sec = stNTPTime;
						tv.tv_usec = 0;
						settimeofday(&tv, NULL);
						dev_log_write(DEV_LOG_INFO, DEV_EVENT_TIME, "NTP time calibrated, offset=%lds", stNTPTime - old_sec);
					}

					s_time_calibrated = 1;

					ptmTime = gmtime(&stNTPTime);
					szSystemTime.second  = (int) ptmTime->tm_sec;
					szSystemTime.minute  = (int) ptmTime->tm_min;
					szSystemTime.hour    = (int) ptmTime->tm_hour;
					szSystemTime.day     = (int) ptmTime->tm_mday;
					szSystemTime.month   = (int) (ptmTime->tm_mon + 1);
					szSystemTime.year    = (int) (ptmTime->tm_year + 1900);/*current year-1900,调整下*/
					szSystemTime.wday   = (int) ptmTime->tm_wday;
#if 0
		            //夏令时时间调整
		            if(IsInDST(szSystemTime))
		            {
		                TimeAdd(&szSystemTime, &szSystemTime, 3600);
		            }
#endif
					NsErr("NTPD: UpdateTime -- %d:%d:%d-%d:%d:%d\n",
						szSystemTime.year,szSystemTime.month,szSystemTime.day,
						szSystemTime.hour,szSystemTime.minute,szSystemTime.second);



					tp_close(iSocket);
					break;
				}
			}
			
			tp_close(iSocket);
		}

		if (s_time_calibrated)
			sleep(5*60);
		else
			sleep(2);
	}
	return NULL;
}

int start_ntp()
{
	CreateDetachedThread((char*)"ntp", thread_ntp, (void *)NULL, true);
	
	AppErr("NTPD: Start OK...\n");
	return 0;
}

int is_ntp_calibrated()
{
	return s_time_calibrated;
}




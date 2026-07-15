#include "onvif.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/prctl.h>
#include <hyBaseType.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "commdef.h"
#include "xml_node.h"
#include "commdef.h"

#define TAG_LEN 64

typedef struct {
	char buff[1024*8];
	int  buffLen;
	char tag[TAG_LEN];
	char messageId[128];
	char types[128];
	char typesXml[1024];
	char typesDevXml[1024];
	char srcIP[64];
	unsigned short port;
	XMLN *xmlData;
	XMLN *xmlHeader;
	XMLN *xmlBody;
} ONVIF_DISCOVERY;


#define ONVIF_BROADCAST_IP "239.255.255.250"


static  void *onvifDiscoveryThread(void *data);
int onvifDiscoverInit(void);
int onvifDiscoveryProcess(int sockFd) ;
int discoveryRequest(ONVIF_DISCOVERY *onvifData, int sockFd);
int discoveryParseData(ONVIF_DISCOVERY *onvifData);
int replyOnvifDiscovery(ONVIF_DISCOVERY *onvifData, int sockFd);
int buildDiscoveryResponseXml(ONVIF_DISCOVERY *onvifData, int sockFd);
const char * onvif_uuid_create();



static int 	 g_DiscoverySockFd = -1;
static HBOOL g_bExitDiscoveryThread = HFALSE;
static pthread_t g_onvifDiscoveryPthreadTid = 0;

void StopOnvifDiscoveryPthread(void)
{
	if(g_onvifDiscoveryPthreadTid)
	{
		g_bExitDiscoveryThread = HTRUE;
		if(g_DiscoverySockFd > 0)
			shutdown(g_DiscoverySockFd,SHUT_RDWR);
		pthread_join(g_onvifDiscoveryPthreadTid,NULL);
		g_onvifDiscoveryPthreadTid = 0;
	}
}
int StartOnvifDiscoveryPthread(void)
{
	int nRet;
	if(g_onvifDiscoveryPthreadTid == 0)
	{
		g_bExitDiscoveryThread = HFALSE;
		nRet = pthread_create(&g_onvifDiscoveryPthreadTid, NULL,onvifDiscoveryThread, NULL);
		if (nRet != 0) 
			{
				ERROR_LOG("create onvif pthread error :%d!\n",errno); 
			}
			
	}

	return nRet;
}

static void *onvifDiscoveryThread(void *data)
{
	
	int nRet;
	prctl(PR_SET_NAME, "onvifDiscovery");
	ERROR_LOG("Start onvif discovery thread :%d\n",g_bExitDiscoveryThread);
	g_DiscoverySockFd = onvifDiscoverInit();
	if (g_DiscoverySockFd < 0) {
		ERROR_LOG("onvifDiscoery init error!\n");
		return NULL;
	}
	while(g_bExitDiscoveryThread == HFALSE) {
		fd_set			stReadSet;
		struct timeval	stTimeVal; 
		FD_ZERO(&stReadSet); 
		FD_SET(g_DiscoverySockFd, &stReadSet); 
		stTimeVal.tv_sec  = ONVIF_SOCK_TIMEOUT_SEC; 
		stTimeVal.tv_usec = 200000;
		nRet = select(g_DiscoverySockFd+1, &stReadSet, NULL, NULL,&stTimeVal);		
		if(nRet <= 0)
			continue;
		nRet = onvifDiscoveryProcess(g_DiscoverySockFd) ;
		if (nRet < 0) 
			continue;
//		ERROR_LOG("discovery process successful!\n");
	}
	ERROR_LOG("Exit onvif discovery thread \n");
	close(g_DiscoverySockFd);
	g_DiscoverySockFd = -1;
	return NULL;
}

HINT32 onvifDiscoverInit(void)
{
	HINT32 opt = 1;
	HINT32 len = 65535;
	int fd;
	HINT32 nRet;
    struct sockaddr_in addr;
    struct ip_mreq mcast;
    
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0)	{
		ERROR_LOG("create socket error!\n");
		return -1;
	}	

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(3702);
	
	nRet = bind(fd,(struct sockaddr *)&addr,sizeof(addr));
	if (nRet < 0) {
		ERROR_LOG("Bind udp socket fail\n");
		close(fd);
		return -1;
	}

	nRet = setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (char*)&len, sizeof(int));
	if (nRet < 0){
		ERROR_LOG("setsockopt SO_SNDBUF error!\n");
	}

	nRet = setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (char*)&len, sizeof(int));
	if (nRet < 0) {
		ERROR_LOG("setsockopt SO_RCVBUF error!\n");
	}
    
	/* reuse socket addr */
	nRet = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
    if (nRet < 0) {  
        ERROR_LOG("setsockopt SO_REUSEADDR error!\n");
    }  

	mcast.imr_multiaddr.s_addr = inet_addr(ONVIF_BROADCAST_IP);
	mcast.imr_interface.s_addr = htonl(INADDR_ANY);
	nRet = setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&mcast, sizeof(mcast));
	if (nRet < 0) {
		ERROR_LOG("setsockopt IP_ADD_MEMBERSHIP error! errno: %d\n", errno);
		return -1;
	}

	return fd;
}

/*
 * 该函数暂不可
 */
int onvifHello(int sockFd)
{
	int i;
	int nRet;
    int rlen;
    int mlen;
	char temp[1024];
    char buff[1024*10];
	int buffLen;
    struct sockaddr_in addr;
	char localIP[32];
	HUINT16 port;

    mlen = sizeof(buff);
    
    nRet = sprintf(buff,
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
		"<SOAP-ENV:Envelope xmlns:SOAP-ENV=\"http://www.w3.org/2003/05/soap-envelope\" "
		"xmlns:SOAP-ENC =\"http://www.w3.org/2003/05/soap-encoding\" "
		"xmlns:wsa=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\" "
		"xmlns:d=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\" "
		"xmlns:dn=\"http://www.onvif.org/ver10/network/wsdl\" "		
		"xmlns:ds=\"http://www.w3.org/2000/09/xmldsig#\" "
		"xmlns:tt=\"http://www.onvif.org/ver10/schema\" "
		"xmlns:tds=\"http://www.onvif.org/ver10/device/wsdl\">");
	if (nRet < 0) return -1;

	nRet = sprintf(temp,"<SOAP-ENV:Header>"
	    "<wsa:MessageID>urn:uuid:%s</wsa:MessageID>"
	    "<wsa:RelatesTo>http://schemas.xmlsoap.org/ws/2004/08/addressing/role/anonymous</wsa:RelatesTo>"
	    "<wsa:To s:mustUnderstand=\"true\">urn:schemas-xmlsoap-org:ws:2005:04:discovery</wsa:To>"
	    "<wsa:Action s:mustUnderstand=\"true\">http://schemas.xmlsoap.org/ws/2005/04/discovery/Hello</wsa:Action>"
		"</SOAP-ENV:Header>", onvif_uuid_create());
	if (nRet < 0) return -1;
	strncat(buff, temp, strlen(temp));

	nRet = sprintf(temp, "<SOAP-ENV:Body><d:Hello>"
            "<wsa:EndpointReference>"
                "<wsa:Address>urn:uuid:812b12f7-45a0-11b5-8404-c056e3a2e020</wsa:Address>"
            "</wsa:EndpointReference>"
            "<d:Types>dn:NetworkVideoTransmitter</d:Types>");
	if (nRet < 0) return -1;
	strncat(buff, temp, strlen(temp));
            
   	strncat(buff, "<d:Scopes></d:Scopes>", strlen("<d:Scopes></d:Scopes>"));
	
   	nRet = getLocalIp(HTRUE, localIP);
	if (nRet < 0) return -1;
	port = getOnvifPort(HFALSE);//hySysCfgGet()->sOnvifParam.nOnvifPort;
	if (nRet < 0) return -1;
	
   	nRet = sprintf(temp, "<d:XAddrs>http://%s:%d/onvif/device_service</d:XAddrs>"
        "<d:MetadataVersion>1</d:MetadataVersion></d:Hello></SOAP-ENV:Body></SOAP-ENV:Envelope>", localIP, port);
	if (nRet < 0) return -1;
	strncat(buff, temp, strlen(temp));
	
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(ONVIF_BROADCAST_IP);
	addr.sin_port = htons(3702);

	buffLen = strlen(buff);
    rlen = sendto(sockFd, buff, buffLen, 0, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));
	if (rlen != buffLen) {
		ERROR_LOG("onvif_hello::rlen = %d, slen = %d\r\n", rlen, buffLen);
	}
	
}

const char * onvif_uuid_create()
{
	static char uuid[256];
	char  chSerialNumber[128] = {0};
	GetDeviceSerialNumber(chSerialNumber,sizeof(chSerialNumber));
	sprintf(uuid,"2419d68a-2dd2-21b2-a205-%s",chSerialNumber);	
	return uuid;
}

int onvifDiscoveryProcess(int sockFd) 
{
	struct sockaddr_in clientAddr;
	socklen_t addrLen;
	int recvLen;
	int nRet;
	ONVIF_DISCOVERY onvifData;
	
	memset(&onvifData, 0, sizeof(ONVIF_DISCOVERY));

	addrLen = (socklen_t)sizeof(clientAddr);
	onvifData.buffLen = recvfrom(sockFd, onvifData.buff, sizeof(onvifData.buff), 0, (struct sockaddr *)&clientAddr, &addrLen);
	if (onvifData.buffLen <= 0) {
		if (errno != EAGAIN)ERROR_LOG("receive data error!\n");
		return -1;
	}

	strcpy(onvifData.srcIP, inet_ntoa(clientAddr.sin_addr));	
	onvifData.port = clientAddr.sin_port;	

	nRet = discoveryParseData(&onvifData);
	if (nRet < 0) {
		ERROR_LOG("parse onvif discovery data error!\n");
		xml_node_del(onvifData.xmlData);
		onvifData.xmlData = NULL;
		return -1;
	}

	nRet = discoveryRequest(&onvifData,sockFd); 
	if (nRet < 0) {
		ERROR_LOG("reply onvif discovery error!\n");
		xml_node_del(onvifData.xmlData);
		onvifData.xmlData = NULL;
		return -1;
	}
	xml_node_del(onvifData.xmlData);
	onvifData.xmlData = NULL;

	return 0;
}

int discoveryRequest(ONVIF_DISCOVERY *onvifData, int sockFd)
{
	if (soap_strcmp(onvifData->tag, "Probe") == 0)
		return replyOnvifDiscovery(onvifData, sockFd);
}

int discoveryParseData(ONVIF_DISCOVERY *onvifData)
{	
	
	onvifData->xmlData = xxx_hxml_parse(onvifData->buff, onvifData->buffLen);
	if (onvifData->xmlData == NULL || onvifData->xmlData == NULL) {
		ERROR_LOG("parse xml data error!\n");
		return -1;
	}
	
	onvifData->xmlHeader = xml_node_soap_get(onvifData->xmlData, "Header");
	if (onvifData->xmlHeader == NULL) {
		ERROR_LOG("get xml header error!\n");
		xml_node_del(onvifData->xmlData);
		onvifData->xmlData = NULL;
		return -1;
	}

	onvifData->xmlBody = xml_node_soap_get(onvifData->xmlData, "Body");
	if (onvifData->xmlBody == NULL) {
		ERROR_LOG("get xml body error!\n");
		xml_node_del(onvifData->xmlData);
		onvifData->xmlData = NULL;
		return -1;
	}

	if (strlen(onvifData->xmlBody->f_child->name) > TAG_LEN-1) {
		ERROR_LOG("tag too long\n");
	} else {
		strcpy(onvifData->tag,onvifData->xmlBody->f_child->name);
	}

	return 0;
}
/*
 *	替换字符串中指定的字符，字符串最长长度为1024
 */
static int replaceWordOfString(char *words, char *befor, char *after)
{
		char afterString[1024];
		int strLen = strlen(words);
		int beforLen = strlen(befor);
		int len = 0;
		char *ptrTemp = NULL;
		char *replacePtr = words;

		memset(afterString, 0, sizeof(afterString));
		ptrTemp = befor;

		while (1) {
				ptrTemp = strstr(replacePtr, befor);
				if (ptrTemp == NULL) break;

				len = ptrTemp - replacePtr;
				strncat(afterString, replacePtr, len);
				strcat(afterString, after);
				replacePtr += len;
				replacePtr += beforLen;
		}

		strcat(afterString, replacePtr);
		memset(words, 0, strLen);
		strcpy(words, afterString);
		//printf("%s\n", afterString);

		return 0;
}
int replyOnvifDiscovery(ONVIF_DISCOVERY *onvifData, int sockFd)
{	
	XMLN *messageId = NULL;
	XMLN *probe = NULL;
	XMLN *types = NULL;
	char *tempptr = NULL;
	int i;
	
	messageId = xml_node_soap_get(onvifData->xmlHeader, "MessageID"); 
	if (messageId && messageId->data)
	{
		strcpy(onvifData->messageId, messageId->data);
	} else {
		return -1;
	}

	do {
		memset(onvifData->typesDevXml, 0, sizeof(onvifData->typesDevXml));
		probe = xml_node_soap_get(onvifData->xmlBody, "Probe"); 
		if (probe == NULL || probe->f_child == NULL) break;
		types = xml_node_soap_get(probe, "Types"); 
		if (types != NULL && types->data != NULL)
		{
			strcpy(onvifData->types, types->data);
			//printf("%s\n%s\n", onvifData->types, types->data);
			replaceWordOfString(onvifData->types, "dp0", "tdn");
			replaceWordOfString(onvifData->types, "dp1", "_0");
			if (types->l_attrib == NULL || types->l_attrib->data == NULL) break;
			strcpy(onvifData->typesXml, types->f_attrib->next->data);
			//printf("name:%s\n",types->f_attrib->next->name);
			sprintf(onvifData->typesDevXml, "xmlns:_0 =\"%s\"", types->l_attrib->data);
			//printf("types:%s\n typexXml:%s\n typesDevXml:%s\n", onvifData->types, onvifData->typesXml, onvifData->typesDevXml);
		} else {
			printf("get types error!\n");
			break;
		}
		return buildDiscoveryResponseXml(onvifData, sockFd);
	} while(0);

	strcpy(onvifData->types, "_0:Device");
	strcpy(onvifData->typesXml, "http://www.onvif.org/ver10/network/wsdl");
	strcpy(onvifData->typesDevXml, "xmlns:_0 =\"http://www.onvif.org/ver10/device/wsdl\"");
		
	return buildDiscoveryResponseXml(onvifData, sockFd);
}

int buildDiscoveryResponseXml(ONVIF_DISCOVERY *onvifData, int sockFd)
{
	char buffer[1024 * 10]; 
	char temp[1024];
	int sendLen;
    struct sockaddr_in addr;
	char localIP[32];
	HUINT16 port;
	int nRet;
	
	nRet = sprintf(buffer, "<?xml version=\"1.0\" encoding=\"UTF-8\"?> "
		"<SOAP-ENV:Envelope xmlns:SOAP-ENV=\"http://www.w3.org/2003/05/soap-envelope\" "
		"xmlns:SOAP-ENC=\"http://www.w3.org/2003/05/soap-encoding\" "
		"xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
		"xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\" "
		"xmlns:wsa=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\" "
		"xmlns:wsdd=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\" "
		"xmlns:vfdis=\"http://www.onvif.org/ver10/network/wsdl/RemoteDiscoveryBinding\" "
		"xmlns:vfdis2=\"http://www.onvif.org/ver10/network/wsdl/DiscoveryLookupBinding\" "
		"xmlns:tdn=\"%s\">",onvifData->typesXml);
	if (nRet < 0) return -1;

	nRet = sprintf(temp,"<SOAP-ENV:Header>"
		"<wsa:MessageID>uuid:%s</wsa:MessageID>"
		"<wsa:RelatesTo>%s</wsa:RelatesTo>"
		"<wsa:ReplyTo SOAP-ENV:mustUnderstand=\"true\">"
		"<wsa:Address>http://schemas.xmlsoap.org/ws/2004/08/addressing/role/anonymous</wsa:Address>"
		"</wsa:ReplyTo>"
		"<wsa:To SOAP-ENV:mustUnderstand=\"true\">http://schemas.xmlsoap.org/ws/2004/08/addressing/role/anonymous</wsa:To>"
		"<wsa:Action SOAP-ENV:mustUnderstand=\"true\">http://schemas.xmlsoap.org/ws/2005/04/discovery/ProbeMatches</wsa:Action>"
		"</SOAP-ENV:Header>", onvif_uuid_create(), onvifData->messageId);
	if (nRet < 0) return -1;
	strncat(buffer, temp, strlen(temp));
	
	nRet = sprintf(temp, "<SOAP-ENV:Body>"
	    "<wsdd:ProbeMatches>"
		"<wsdd:ProbeMatch %s>"
		"<wsa:EndpointReference>"
		"<wsa:Address>urn:uuid:%s</wsa:Address>"
		"<wsa:ReferenceProperties></wsa:ReferenceProperties>"
		"<wsa:ReferenceParameters></wsa:ReferenceParameters>"
		"<wsa:PortType>ttl</wsa:PortType>"
		"</wsa:EndpointReference>"
		"<wsdd:Types>%s</wsdd:Types>",onvifData->typesDevXml, onvif_uuid_create(), onvifData->types);
	if (nRet < 0) return -1;
	strncat(buffer, temp, strlen(temp));

	nRet = getLocalIp(HTRUE, localIP);
	if (nRet < 0) return -1;
	port = getOnvifPort(HFALSE);//hySysCfgGet()->sOnvifParam.nOnvifPort;
	if (nRet < 0) return -1;
	
	nRet = sprintf(temp,
		"<wsdd:Scopes> onvif://www.onvif.org/Profile/Streaming onvif://www.onvif.org/Model/631GA onvif://www.onvif.org/Name/IPCAM onvif://www.onvif.org/location/country/china</wsdd:Scopes>"
		"<wsdd:XAddrs>http://%s:%d/onvif/device_service</wsdd:XAddrs>"
		"<wsdd:MetadataVersion>1</wsdd:MetadataVersion>"
		"</wsdd:ProbeMatch></wsdd:ProbeMatches></SOAP-ENV:Body></SOAP-ENV:Envelope>", localIP, port);
	if (nRet < 0) return -1;
	strncat(buffer, temp, strlen(temp));


	// send to received addr
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(onvifData->srcIP);
	addr.sin_port = onvifData->port;
	
	//printf("send to client:%s \n",buffer);
	sendLen = sendto(sockFd, buffer, strlen(buffer), 0, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));
	if (sendLen != strlen(buffer))	{
		printf("onvif_probe_rly::rlen = %d,slen = %d,ip=%s\n", sendLen, strlen(buffer), onvifData->srcIP);
	}
	return 0;
}

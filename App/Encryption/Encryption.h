#ifndef __ENCRYPTION_H__
#define __ENCRYPTION_H__

#include "Common.h"
typedef struct{
	unsigned int HEAD;
	unsigned short PN;
	unsigned char PC;
	unsigned char PT;
	unsigned char PF;
	unsigned char HT;
	unsigned short CID;
	unsigned int PD;
	unsigned short SN;
	unsigned char MAC[6];
	unsigned char TUYA_PID[17]; 	//涂鸦 PRODUCT ID //16 bytes
	unsigned char TUYA_UID[21]; 	//涂鸦 UUID 		//20 bytes
	unsigned char TUYA_AUTHKEY[33]; //涂鸦 AUTH KEY	//32 bytes
	unsigned char TUTK_UID[21]; 	//TUTK UID		//20 bytes
	unsigned char CM_CODE[22]; 	//移动串码 21字符
	unsigned char DevMode;
	unsigned char reserved[3];
}DEVICE_INFO_FROM_EEPROM_S;

typedef struct
{
	unsigned int force; //是否强制烧录 	0-否 	非0-是
	unsigned char license[256];
}SMsgAVIoctrlBurnLicenseReq;

class CEncryption
{
public:
	
	PATTERN_SINGLETON_DECLARE(CEncryption);

	int Init();

	DEVICE_INFO_FROM_EEPROM_S & GetDevInfoFromEEPROM();
    int BurnLicenseReqProc(SMsgAVIoctrlBurnLicenseReq *pstBurnLicenseReq);
	int BurnLicense_NewProductProcess(const char *license, unsigned int length, const char *compare_pid);//add on 2024.12.28
    int GetLicenseSavaType();
	int ChangeTuyapidBySdcard();	//通过卡配的方式更换涂鸦pid add on 2024.12.02
	bool ClearLicense();			//add on 2024.12.31
	bool ChangeTuyapidByProduce(const unsigned char *pid,int pidLen);	//通过产测的方式更换涂鸦pidadd on 2024.12.31
private:
	/// 加载配置文件
	int  LoadFile();
    bool ParseLicense(unsigned char *hwid, DEVICE_INFO_FROM_EEPROM_S *pstOut);
    bool CheckDevLicense();
    bool CheckFLASHTest();
private:
	DEVICE_INFO_FROM_EEPROM_S m_devInfoFromEEPROM;
    int m_iLicenseSavaType; 	//设备license保存类型 	0-未烧录 	1-24C02 	2-flash
    char HWID_g[256]; 		//设备license
};

#define g_EncryptionHandle (*CEncryption::instance())
#define DevInfoFromEEPROM_g CEncryption::instance()->GetDevInfoFromEEPROM()


#endif// __ENCRYPTION_H__


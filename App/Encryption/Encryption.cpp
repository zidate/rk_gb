#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "Encryption.h"

static const unsigned int crc32tab[] = {
0x00000000L, 0x77073096L, 0xee0e612cL, 0x990951baL,
0x076dc419L, 0x706af48fL, 0xe963a535L, 0x9e6495a3L,
0x0edb8832L, 0x79dcb8a4L, 0xe0d5e91eL, 0x97d2d988L,
0x09b64c2bL, 0x7eb17cbdL, 0xe7b82d07L, 0x90bf1d91L,
0x1db71064L, 0x6ab020f2L, 0xf3b97148L, 0x84be41deL,
0x1adad47dL, 0x6ddde4ebL, 0xf4d4b551L, 0x83d385c7L,
0x136c9856L, 0x646ba8c0L, 0xfd62f97aL, 0x8a65c9ecL,
0x14015c4fL, 0x63066cd9L, 0xfa0f3d63L, 0x8d080df5L,
0x3b6e20c8L, 0x4c69105eL, 0xd56041e4L, 0xa2677172L,
0x3c03e4d1L, 0x4b04d447L, 0xd20d85fdL, 0xa50ab56bL,
0x35b5a8faL, 0x42b2986cL, 0xdbbbc9d6L, 0xacbcf940L,
0x32d86ce3L, 0x45df5c75L, 0xdcd60dcfL, 0xabd13d59L,
0x26d930acL, 0x51de003aL, 0xc8d75180L, 0xbfd06116L,
0x21b4f4b5L, 0x56b3c423L, 0xcfba9599L, 0xb8bda50fL,
0x2802b89eL, 0x5f058808L, 0xc60cd9b2L, 0xb10be924L,
0x2f6f7c87L, 0x58684c11L, 0xc1611dabL, 0xb6662d3dL,
0x76dc4190L, 0x01db7106L, 0x98d220bcL, 0xefd5102aL,
0x71b18589L, 0x06b6b51fL, 0x9fbfe4a5L, 0xe8b8d433L,
0x7807c9a2L, 0x0f00f934L, 0x9609a88eL, 0xe10e9818L,
0x7f6a0dbbL, 0x086d3d2dL, 0x91646c97L, 0xe6635c01L,
0x6b6b51f4L, 0x1c6c6162L, 0x856530d8L, 0xf262004eL,
0x6c0695edL, 0x1b01a57bL, 0x8208f4c1L, 0xf50fc457L,
0x65b0d9c6L, 0x12b7e950L, 0x8bbeb8eaL, 0xfcb9887cL,
0x62dd1ddfL, 0x15da2d49L, 0x8cd37cf3L, 0xfbd44c65L,
0x4db26158L, 0x3ab551ceL, 0xa3bc0074L, 0xd4bb30e2L,
0x4adfa541L, 0x3dd895d7L, 0xa4d1c46dL, 0xd3d6f4fbL,
0x4369e96aL, 0x346ed9fcL, 0xad678846L, 0xda60b8d0L,
0x44042d73L, 0x33031de5L, 0xaa0a4c5fL, 0xdd0d7cc9L,
0x5005713cL, 0x270241aaL, 0xbe0b1010L, 0xc90c2086L,
0x5768b525L, 0x206f85b3L, 0xb966d409L, 0xce61e49fL,
0x5edef90eL, 0x29d9c998L, 0xb0d09822L, 0xc7d7a8b4L,
0x59b33d17L, 0x2eb40d81L, 0xb7bd5c3bL, 0xc0ba6cadL,
0xedb88320L, 0x9abfb3b6L, 0x03b6e20cL, 0x74b1d29aL,
0xead54739L, 0x9dd277afL, 0x04db2615L, 0x73dc1683L,
0xe3630b12L, 0x94643b84L, 0x0d6d6a3eL, 0x7a6a5aa8L,
0xe40ecf0bL, 0x9309ff9dL, 0x0a00ae27L, 0x7d079eb1L,
0xf00f9344L, 0x8708a3d2L, 0x1e01f268L, 0x6906c2feL,
0xf762575dL, 0x806567cbL, 0x196c3671L, 0x6e6b06e7L,
0xfed41b76L, 0x89d32be0L, 0x10da7a5aL, 0x67dd4accL,
0xf9b9df6fL, 0x8ebeeff9L, 0x17b7be43L, 0x60b08ed5L,
0xd6d6a3e8L, 0xa1d1937eL, 0x38d8c2c4L, 0x4fdff252L,
0xd1bb67f1L, 0xa6bc5767L, 0x3fb506ddL, 0x48b2364bL,
0xd80d2bdaL, 0xaf0a1b4cL, 0x36034af6L, 0x41047a60L,
0xdf60efc3L, 0xa867df55L, 0x316e8eefL, 0x4669be79L,
0xcb61b38cL, 0xbc66831aL, 0x256fd2a0L, 0x5268e236L,
0xcc0c7795L, 0xbb0b4703L, 0x220216b9L, 0x5505262fL,
0xc5ba3bbeL, 0xb2bd0b28L, 0x2bb45a92L, 0x5cb36a04L,
0xc2d7ffa7L, 0xb5d0cf31L, 0x2cd99e8bL, 0x5bdeae1dL,
0x9b64c2b0L, 0xec63f226L, 0x756aa39cL, 0x026d930aL,
0x9c0906a9L, 0xeb0e363fL, 0x72076785L, 0x05005713L,
0x95bf4a82L, 0xe2b87a14L, 0x7bb12baeL, 0x0cb61b38L,
0x92d28e9bL, 0xe5d5be0dL, 0x7cdcefb7L, 0x0bdbdf21L,
0x86d3d2d4L, 0xf1d4e242L, 0x68ddb3f8L, 0x1fda836eL,
0x81be16cdL, 0xf6b9265bL, 0x6fb077e1L, 0x18b74777L,
0x88085ae6L, 0xff0f6a70L, 0x66063bcaL, 0x11010b5cL,
0x8f659effL, 0xf862ae69L, 0x616bffd3L, 0x166ccf45L,
0xa00ae278L, 0xd70dd2eeL, 0x4e048354L, 0x3903b3c2L,
0xa7672661L, 0xd06016f7L, 0x4969474dL, 0x3e6e77dbL,
0xaed16a4aL, 0xd9d65adcL, 0x40df0b66L, 0x37d83bf0L,
0xa9bcae53L, 0xdebb9ec5L, 0x47b2cf7fL, 0x30b5ffe9L,
0xbdbdf21cL, 0xcabac28aL, 0x53b39330L, 0x24b4a3a6L,
0xbad03605L, 0xcdd70693L, 0x54de5729L, 0x23d967bfL,
0xb3667a2eL, 0xc4614ab8L, 0x5d681b02L, 0x2a6f2b94L,
0xb40bbe37L, 0xc30c8ea1L, 0x5a05df1bL, 0x2d02ef8dL 
};

unsigned int crc32( const unsigned char *buf, unsigned int size)
{
	unsigned int i, crc;
	crc = 0xFFFFFFFF;
	for (i = 0; i < size; i++)
		crc = crc32tab[(crc ^ buf[i]) & 0xff] ^ (crc >> 8);
	return crc^0xFFFFFFFF;
}

PATTERN_SINGLETON_IMPLEMENT(CEncryption);

CEncryption::CEncryption()
{
	memset(&m_devInfoFromEEPROM,0,sizeof(DEVICE_INFO_FROM_EEPROM_S));
    memset(HWID_g,0,256);
	m_iLicenseSavaType = 0;
}

CEncryption::~CEncryption()
{

}

int CEncryption::Init()
{
	return LoadFile();
}

DEVICE_INFO_FROM_EEPROM_S & CEncryption::GetDevInfoFromEEPROM()
{
	return m_devInfoFromEEPROM;
}

int CEncryption::GetLicenseSavaType()
{
    return m_iLicenseSavaType;
}

int CEncryption::ChangeTuyapidBySdcard()
{
	int ret = 0;
	bool bres;
	int dgfile_ret = 0;
	uchar hwid[256] = {'\0'};
	uchar buffer[256] = {'\0'};//用于保存pid文件内容
	//有些芯片按上述初始化结果可能数组不全为0，所以需要清零
	memset(hwid,0,sizeof(hwid));
	memset(buffer,0,sizeof(buffer));
	
	uchar customInfoLen; 	//自定义数据长度
	uchar crc32Flag; 		//是否带crc32校验码标识 如果是0xAA的话表示带crc32校验
	uint crc32CheckCode;	//校验码
	FILE *file;
	int length;
	int wait_mount = 0;
	printf("\033[1;36m ======================ChangeTuyapidBySdcard start----------------- \033[0m\n");
	//0.挂载sdcard
	//START_PROCESS("sh", "sh", "-c", "mount " MMCP1" " MOUNT_DIR, NULL);

	//结合前面的函数，连续的mount/umount的操作可能会影响文件的判断
	//最多等到500ms
	while(/*access(TUYA_NEW_PID_FILE, F_OK) != 0 &&*/ wait_mount < 10)
	{
		printf("\033[1;36m  new_pid not exit    \033[0m\n");
		usleep(50 * 1000);
		wait_mount++;
	}

	//1.判断sdcard pid文件是否存在
	if (access(TUYA_NEW_PID_FILE, F_OK) == 0)
	{
		
		//2.读取pid文件内容
		file = fopen(TUYA_NEW_PID_FILE, "r");
		if (file == NULL) 
		{
			printf("Failed to open file.\n");
			ret = -1;
			goto end;
		}

		length = fread(buffer, 1, sizeof(buffer), file);
		fclose(file);
		printf("\033[1;36m length = [%d],pid = %s  \033[0m\n",length,buffer);


		//3.判断pid文件内容是否合法 16个字符 
		if (length != 16) 
		{
			printf("The length of content is not 16,content is invalid.\n");
			printf("\033[1;36m length = [%d],pid = %s  \033[0m\n",length,buffer);
			ret = -2;
			goto end;
    	}
		//只有数字和小写字母
		for (int i = 0; i < length; i++) 
		{
			if((buffer[i] >= 97 && buffer[i] <= 122)|| (buffer[i] >= 48 && buffer[i] <= 57))
			{
				printf("%c",buffer[i]);
				if(i >= 15)
				{
					printf("\n");
				}
			}
			else
			{
				printf("The content is not  number or lowercase letter.\n");
				ret = -3;
				goto end;
			}
    	}


		//4.比较pid内容跟当前的pid是否一致,则不用做处理
		if (memcmp(buffer, m_devInfoFromEEPROM.TUYA_PID, 16) == 0)
		{
			printf("\033[1;35m  new_pid is same    \033[0m\n");
			ret = 0;
			goto end;
		}
		else//pid不一致，更新pid
		{//5.参考产测的更换pid流程
			printf("\033[1;33m --------------------change pid  \033[0m\n");
			//a.读取24c02加密信息
			memset(hwid, 0, sizeof(hwid));
			
			bres = IMagicBox::instance()->getHWID(hwid, sizeof(hwid));
			if( bres == false)//读取24c02失败
			{
				printf("\033[1;35m --------------------read hwid error----24c02  \033[0m\n");
				memset(hwid, 0, sizeof(hwid));
				dgfile_ret = dg_read_file(DEV_INFO, hwid, 256);//读取配置分区的文件
				if(dgfile_ret <= 0)
				{
					printf("\033[1;35m --------------------read hwid error----DEV_INFO  \033[0m\n");
					dgfile_ret = 0;
					memset(hwid, 0, sizeof(hwid));
					dgfile_ret = dg_read_file(DEV_INFO_BK, hwid, 256);//读取配置分区的备份文件
					if(dgfile_ret <= 0)
					{
						dgfile_ret = 0;
						printf("\033[1;35m --------------------read hwid error----DEV_INFO_BK  \033[0m\n");
						ret = -4;
					}
				}
			}
			if(bres || (dgfile_ret > 0))//读取文件成功
			{
				printf("\033[1;36m --------------------read hwid success  \033[0m\n");
				//b.解密
				_DG_AES_decrypt((char *)hwid, sizeof(hwid));

				//c.拷贝涂鸦 PRODUCT ID 到加密字符串中
				memcpy(&hwid[100], buffer, 16);	
				
				//自定义数据长度
				customInfoLen = hwid[27]; 	
				//是否带crc32校验码标识 如果是0xAA的话表示带crc32校验
				crc32Flag = hwid[28+customInfoLen+1]; 	
				//d.crc32校验码
				if (0xAA == crc32Flag)
				{
					printf("\033[1;33m --------------------need crc32 \033[0m\n");
					crc32CheckCode = crc32(hwid, 28+customInfoLen+1);
					hwid[28+customInfoLen+2] = crc32CheckCode >> 24;
					hwid[28+customInfoLen+3] = crc32CheckCode >> 16;
					hwid[28+customInfoLen+4] = crc32CheckCode >> 8;
					hwid[28+customInfoLen+5] = crc32CheckCode;
				}

				//e.加密
				_DG_AES_encrypt((char *)hwid,  sizeof(hwid));
				//f.保存
				
				if(bres)
				{
					//保存到24c02
					IMagicBox::instance()->setHWID(hwid, sizeof(hwid));
					printf("\033[1;33m --------------------save success---24c02 \033[0m\n");
				}
				//保存到flash,由于没有专门用于保存的Flash分区，故注释掉
				//当前系统没有配置license分区，不能往里面写数据，避免将其他分区的重要数据删除导致数据缺失
				//保存到data分区
				dg_save_file(DEV_INFO, hwid, 256);
				dg_save_file(DEV_INFO_BK, hwid, 256);
				sync();
				CheckDevLicense();//重新读取加密信息(程序的逻辑这样子设计是没什么问题的，只要重新加载后，如果加密异常就没法子正常跑就可以了 add on 2025.02.07)
			}
			

		}
	}
end:
	//6.卸载sdcard
    // START_PROCESS("sh", "sh", "-c", "umount -l " MOUNT_DIR, NULL);
	printf("\033[1;36m ======================ChangeTuyapidBySdcard  end---------------\033[0m\n");
	return ret;
}


/**
 * @brief 根据生产信息更改Tuya PID
 *
 * 该函数根据生产信息更改Tuya PID，并更新设备的硬件ID信息。
 *
 * @param pid 指向PID信息的指针
 * @param pidLen PID信息的长度
 *
 * @return 如果更改成功，则返回true；否则返回false
 */
bool CEncryption::ChangeTuyapidByProduce(const unsigned char *pid,int pidLen)
{
	unsigned char hwid[256] = {0};
	memset(hwid, 0, sizeof(hwid));//有些芯片按上述初始化结果可能数组不全为0，所以需要清零

	unsigned char customInfoLen; 	//自定义数据长度
	unsigned char crc32Flag; 		//是否带crc32校验码标识 如果是0xAA的话表示带crc32校验
	unsigned int crc32CheckCode;	//校验码
	bool ret = false;


	//检查pid是否一致
	if (memcmp(pid, m_devInfoFromEEPROM.TUYA_PID, 16) == 0)
	{
		printf("\033[1;35m  new_pid is same    \033[0m\n");
		ret = true;
		goto change_pid_exit;
	}
	else//pid不一致，更新pid
	{
		memcpy(hwid, HWID_g, sizeof(HWID_g));//复制加密信息

		//解密
		_DG_AES_decrypt((char *)hwid,  sizeof(hwid));
				
		memcpy(&hwid[100], pid, pidLen);	//将新的pid写入

		customInfoLen = hwid[27]; 	//自定义数据长度
		crc32Flag = hwid[28+customInfoLen+1]; 	//是否带crc32校验码标识 如果是0xAA的话表示带crc32校验
		if (0xAA == crc32Flag)
		{
			crc32CheckCode = crc32(hwid, 28+customInfoLen+1);
			hwid[28+customInfoLen+2] = crc32CheckCode >> 24;
			hwid[28+customInfoLen+3] = crc32CheckCode >> 16;
			hwid[28+customInfoLen+4] = crc32CheckCode >> 8;
			hwid[28+customInfoLen+5] = crc32CheckCode;
		}

		//加密
		_DG_AES_encrypt((char *)hwid, sizeof(hwid));
		
		//先烧到24C02
		IMagicBox::instance()->setHWID(hwid, sizeof(hwid));
		//同时会备份到flash中
		dg_save_file(DEV_INFO, hwid, sizeof(hwid));
		dg_save_file(DEV_INFO_BK, hwid, sizeof(hwid));
		
		sync();
		usleep(50 * 1000);

		ret = CheckDevLicense();

	}
change_pid_exit:
	return ret;


}




/**
 * @brief 清除许可证信息
 *
 * 该函数用于清除设备上的许可证信息。
 *
 * @return 如果清除成功返回true，否则返回false。
 */
bool CEncryption::ClearLicense()
{
	bool ret = false;
	bool result = false;
	unsigned char hwid[256] = {0};
	memset(hwid,0,sizeof(hwid));//有些芯片按上述初始化结果可能数组不全为0，所以需要清零
	//清掉24C02
	IMagicBox::instance()->setHWID(hwid, sizeof(hwid));

	//同时清掉flash中的
	//当前系统没有配置license分区，不能往里面写数据，避免将其他分区的重要数据删除导致数据缺失
	//2.data分区
	START_PROCESS("sh", "sh", "-c", "rm -f "DEV_INFO, NULL);
	START_PROCESS("sh", "sh", "-c", "rm -f "DEV_INFO_BK, NULL);

	sync();//同步	

	ret = CheckDevLicense();//重新读取加密信息
	if(false == ret)//检测License失败——号清除成功，清零相应的变量
	{
		//清零相应的变量
		memset(HWID_g, 0, sizeof(HWID_g));
		memset(&m_devInfoFromEEPROM, 0, sizeof(m_devInfoFromEEPROM));
		m_iLicenseSavaType = 0;
		result = true;
	}

	return result;
}


int CEncryption::LoadFile()
{
#if 1
    CheckDevLicense();
//	ChangeTuyapidBySdcard();//add on 0927
	// memset(m_devInfoFromEEPROM.TUYA_PID, 0, sizeof(m_devInfoFromEEPROM.TUYA_PID));
	// memcpy(m_devInfoFromEEPROM.TUYA_PID, "sxrmiqf8p2aaiu4x", 16);
#else
    CheckFLASHTest();
#endif

	return 0;
}
int CEncryption::BurnLicenseReqProc(SMsgAVIoctrlBurnLicenseReq *pstBurnLicenseReq)
{
	int result;
	bool bRes;
	uchar hwid[256] = {0};
	DEVICE_INFO_FROM_EEPROM_S stDevLicense;

	if( memcmp(pstBurnLicenseReq->license, HWID_g, 128) == 0 )	//目前license只用了不到128个字节, 也只往24C02里写了128个字节, 所以只比较128个字节就够了
	{
		//license相同, 返回成功
		AppErr("license is same.\n");
		result = 0;
		goto burn_exit;
	}

	if( (0 == ProductCof_g.forceBurnLicense) && (0 == pstBurnLicenseReq->force) && (0 != m_iLicenseSavaType) )
	{
		AppErr("already burned...\n");
		result = 100;
		goto burn_exit;
	}

	//解密
	memcpy(hwid, pstBurnLicenseReq->license, 256);
	_DG_AES_decrypt((char *)hwid, sizeof(hwid));

	bRes = ParseLicense(hwid, &stDevLicense);
	if( false == bRes ) 						//license与设备版本不匹配
	{
		AppErr( "license is not math\n" );
		result = 110;
		goto burn_exit;
	}

	//24C02
	IMagicBox::instance()->setHWID(pstBurnLicenseReq->license,sizeof(pstBurnLicenseReq->license));

	//配置分区文件
	dg_save_file(DEV_INFO, pstBurnLicenseReq->license, sizeof(pstBurnLicenseReq->license));
	dg_save_file(DEV_INFO_BK, pstBurnLicenseReq->license, sizeof(pstBurnLicenseReq->license));
	usleep(50000);

	//读取检查校验EEPROM中的内容
	if (true == CheckDevLicense())
	{
		AppErr( "checkDevLicense succ\n" );
		// m_iLicenseSavaType = 2;//此处不应该设置值，因为CheckDevLicense里面已经设置了，且可能存在24C02的情况，不能直接给成2
		result = 0;
	}
	else
	{
		AppErr( "checkDevLicense error\n" );
		result = 10;
	}
burn_exit:
	return result;
}


//add on 2024.12.28

/**
 * @brief 烧录新产品的许可证
 *
 * 该函数用于烧录新产品的许可证，并验证许可证的有效性。
 *
 * @param license 许可证字符串
 * @param length 许可证字符串的长度
 * @param compare_pid 需要比较的PID字符串
 *
 * @return 返回值表示烧录结果：
 *         - 0: 成功
 *         - -1: 该设备已经烧录过uuid
 *         - -2: 许可证中的PID与输入的PID不一致
 *         - -3: 许可证与设备版本不匹配
 *         - -4: 烧录后校验失败
 */
int CEncryption::BurnLicense_NewProductProcess(const char *license, unsigned int length, const char *compare_pid)
{
	// printf("\035[7m  ----111----len:%d    ----------------data:%s, compare_pid:%s\033[0m\n", length, license, compare_pid); 
	printf("\035[7m  ----111----len:%d     compare_pid:%s\033[0m\n", length,  compare_pid); 

	int result;
	bool bRes;
	uchar hwid[256] = {0};
	uchar license_pid[20] = {0};
	DEVICE_INFO_FROM_EEPROM_S stDevLicense;

	if( (memcmp(license, HWID_g, 128) == 0))	//目前license只用了不到128个字节, 也只往24C02里写了128个字节, 所以只比较128个字节就够了
	{
		//license相同, 返回成功
		AppErr("license is same.\n");
		result = 0;
		goto burn_exit;
	}

	if( 0 != m_iLicenseSavaType )//此号已经烧录过
	{
		AppErr("already burned...iLicenseSavaType_g:%d\n",m_iLicenseSavaType);
		result = -1;
		goto burn_exit;
	}

	//解密
	memcpy(hwid, license, 256);
	_DG_AES_decrypt((char *)hwid, sizeof(hwid));

	//获取PID
	memcpy(license_pid, &hwid[100], 16);
	// printf("\033[1;33m    license_pid:%s       \033[0m\n",license_pid); 

	//校验加密中的pid与产测工具中配置的pid是否一致
	if(0 != (memcmp(license_pid, (const unsigned char*)compare_pid, 16)))
	{
		AppErr("license_pid and compare_pid is not same...\n");
        result = -2;
        goto burn_exit;
	}

	//解析license内容是否与设备版本匹配
	bRes = ParseLicense(hwid, &stDevLicense);
	if( false == bRes ) 						//license与设备版本不匹配
	{
		AppErr( "license is not math\n" );
		result = -3;
		goto burn_exit;
	}

	//前面解析 内容可能会被修改,重新获取license内容
	//这部分逻辑是正常的，只是前面用hwid来存解密之后的数据，当前用来存放license add on 2025.01.14 注释
	memset(hwid, 0, sizeof(hwid));
	memcpy(hwid, license, 256);

	//先烧到24C02
	IMagicBox::instance()->setHWID(hwid, length);
	
	// 同时会备份到flash中
	// SaveLicenseToFlash(hwid, length); //双目枪以及安保灯没有Flash, 所以不烧Flash
	//配置分区文件
	dg_save_file(DEV_INFO, hwid, length);
	dg_save_file(DEV_INFO_BK, hwid, length);
	
	usleep(500*1000);

	//读取检查校验EEPROM中的内容
	if (true == CheckDevLicense())
	{
		AppErr( "checkDevLicense succ\n" );
		// m_iLicenseSavaType = 2;//此处不应该设置值，因为CheckDevLicense里面已经设置了，且可能存在24C02的情况，不能直接给成2
		result = 0;
	}
	else								//出错
	{
		AppErr( "checkDevLicense error\n" );
		result = -4;
		// s_bBurnFial = true;
	}

burn_exit:
	return result;
}

// 读取检查校验设备加密
bool CEncryption::CheckDevLicense()
{
	int ret;
	bool bres;
	bool bHave24C02 = false;
	bool bHavePartition = false;

	uchar hwid[256] = {0};
	uchar hwid_src[256] = {0};
	DEVICE_INFO_FROM_EEPROM_S stDeviceLicense = {0};

	AppErr("get from eeprom.\n");
	memset(hwid, 0, sizeof(hwid));
	memset(hwid_src, 0, sizeof(hwid_src));
	memset(&stDeviceLicense, 0, sizeof(stDeviceLicense));
	bres = IMagicBox::instance()->getHWID(hwid, sizeof(hwid));
	if (true == bres) // 读取成功,表示有24C02
	{
		bHave24C02 = true;
		memcpy(hwid_src, hwid, 256);

		// 解密
		_DG_AES_decrypt((char *)hwid, sizeof(hwid));
		// 解析
		bres = ParseLicense(hwid, &stDeviceLicense);
		if (true == bres)
		{
			memcpy(&m_devInfoFromEEPROM, &stDeviceLicense, sizeof(DEVICE_INFO_FROM_EEPROM_S));

			memcpy(HWID_g, hwid_src, 256);
			m_iLicenseSavaType = 1;

			// 检查备份
			if (access(DEV_INFO, F_OK) == 0)//加密文件存在则读取加密文件的内容, 对比是否一致，不一致则重新写入加密文件
			{
				uchar hwid_flash[256] = {0};
				dg_read_file(DEV_INFO, hwid_flash, 256);
				if (memcmp(hwid_src, hwid_flash, 256) != 0)
					dg_save_file(DEV_INFO, hwid_src, 256);
			}
			else//加密文件不存在则写入加密文件
			{
				dg_save_file(DEV_INFO, hwid_src, 256);
			}
			if (access(DEV_INFO_BK, F_OK) == 0)
			{
				uchar hwid_flash[256] = {0};
				dg_read_file(DEV_INFO_BK, hwid_flash, 256);
				if (memcmp(hwid_src, hwid_flash, 256) != 0)
					dg_save_file(DEV_INFO_BK, hwid_src, 256);
			}
			else
			{
				dg_save_file(DEV_INFO_BK, hwid_src, 256);
			}

			return true;
		}
	}

	AppErr("get from %s.\n", DEV_INFO);
	memset(hwid, 0, sizeof(hwid));
	memset(hwid_src, 0, sizeof(hwid_src));
	memset(&stDeviceLicense, 0, sizeof(stDeviceLicense));
	ret = dg_read_file(DEV_INFO, hwid, 256);
	if (ret > 0)
	{
		memcpy(hwid_src, hwid, 256);

		// 解密
		_DG_AES_decrypt((char *)hwid, sizeof(hwid));
		// 解析
		bres = ParseLicense(hwid, &stDeviceLicense);
		if (true == bres)
		{
			memcpy(&m_devInfoFromEEPROM, &stDeviceLicense, sizeof(DEVICE_INFO_FROM_EEPROM_S));

			memcpy(HWID_g, hwid_src, 256);
			m_iLicenseSavaType = 2;

			// 检查备份
			if (access(DEV_INFO_BK, F_OK) == 0)
			{
				uchar hwid_flash[256] = {0};
				dg_read_file(DEV_INFO_BK, hwid_flash, 256);
				if (memcmp(hwid_src, hwid_flash, 256) != 0)
					dg_save_file(DEV_INFO_BK, hwid_src, 256);
			}
			else
			{
				dg_save_file(DEV_INFO_BK, hwid_src, 256);
			}
			if (bHave24C02)
				IMagicBox::instance()->setHWID(hwid_src, sizeof(hwid_src));

			if (bHavePartition)
				SaveLicenseToFlash(hwid_src, sizeof(hwid_src));

			return true;
		}
	}

	AppErr("get from %s.\n", DEV_INFO_BK);
	memset(hwid, 0, sizeof(hwid));
	memset(hwid_src, 0, sizeof(hwid_src));
	memset(&stDeviceLicense, 0, sizeof(stDeviceLicense));
	ret = dg_read_file(DEV_INFO_BK, hwid, 256);
	if (ret > 0)
	{
		memcpy(hwid_src, hwid, 256);

		// 解密
		_DG_AES_decrypt((char *)hwid, sizeof(hwid));
		// 解析
		bres = ParseLicense(hwid, &stDeviceLicense);
		if (true == bres)
		{
			memcpy(&m_devInfoFromEEPROM, &stDeviceLicense, sizeof(DEVICE_INFO_FROM_EEPROM_S));

			memcpy(HWID_g, hwid_src, 256);
			m_iLicenseSavaType = 2;

			// 检查备份
			dg_save_file(DEV_INFO, hwid_src, 256);

			if (bHave24C02)
				IMagicBox::instance()->setHWID(hwid_src, sizeof(hwid_src));

			if (bHavePartition)
				SaveLicenseToFlash(hwid_src, sizeof(hwid_src));

			return true;
		}
	}

	return false;
}

bool CEncryption::ParseLicense(unsigned char *hwid, DEVICE_INFO_FROM_EEPROM_S *pstOut)
{
	static int ccc = 0;
	unsigned int HEAD = hwid[0] << 24; 	//头
	HEAD |= hwid[1] << 16;
	HEAD |= hwid[2] << 8;
	HEAD |= hwid[3];
	pstOut->HEAD = HEAD;
	
	unsigned char mode = hwid[4]; 	//MODE
	pstOut->DevMode = mode;
	
	unsigned short PN = hwid[5] << 8; 	//PID
	PN |= hwid[6];
	unsigned char PC = hwid[7];
	unsigned char PT = hwid[8];
	unsigned char PF = hwid[9];
	unsigned char HT = hwid[10];
	unsigned short CID = hwid[11] << 8;
	CID |= hwid[12];

	pstOut->PN = PN;
	pstOut->PC = PC;
	pstOut->PT = PT;
	pstOut->PF = PF;
	pstOut->HT = HT;
	pstOut->CID = CID;
	
	unsigned int PD = hwid[13] << 24; 	//生产时间
	PD |= hwid[14] << 16;
	PD |= hwid[15] << 8;
	PD |= hwid[16];	
	pstOut->PD = PD;
	
	unsigned short SN = hwid[17] << 8;
	SN |=  hwid[18];
	pstOut->SN = SN;

	unsigned char mac[6] = {0};
	memcpy(mac, hwid+19, 6);
	memcpy(pstOut->MAC, mac, 6);
	
	unsigned char customInfoLen = hwid[27]; 	//自定义数据长度

	memset(pstOut->CM_CODE, 0, sizeof(pstOut->CM_CODE));
	memcpy(pstOut->CM_CODE, &hwid[28], 21); 	//移动串码 21字符
	
//	memset(pstOut->TUYA_AUTHKEY, 0, sizeof(pstOut->TUYA_AUTHKEY));
//	memcpy(pstOut->TUYA_AUTHKEY, &hwid[48], 32); 	//涂鸦 AUTH KEY	//32 bytes
//	
//	memset(pstOut->TUYA_UID, 0, sizeof(pstOut->TUYA_UID));
//	memcpy(pstOut->TUYA_UID, &hwid[80], 20); 	//涂鸦 UUID 		//20 bytes
//
//	memset(pstOut->TUYA_PID, 0, sizeof(pstOut->TUYA_PID));
//	memcpy(pstOut->TUYA_PID, &hwid[100], 16); 	//涂鸦 PRODUCT ID //16 bytes

	unsigned char eofFlag = hwid[28+customInfoLen]; 	//结束符

	unsigned char crc32Flag = hwid[28+customInfoLen+1]; 	//是否带crc32校验码标识 如果是0xAA的话表示带crc32校验
	unsigned int crc32CheckCode = hwid[28+customInfoLen+2] << 24; 	//校验码
	crc32CheckCode |= hwid[28+customInfoLen+3] << 16;
	crc32CheckCode |= hwid[28+customInfoLen+4] << 8;
	crc32CheckCode |= hwid[28+customInfoLen+5];	
	
	AppInfo("============================================\n");
	AppInfo("HEAD : %02x %02x %02x %02x\n", hwid[0], hwid[1], hwid[2], hwid[3]);
	AppInfo("MODE : %02x\n", pstOut->DevMode);
	AppInfo("PN : %04x\n", PN);
	AppInfo("PC : %02x\n", PC);
	AppInfo("PT : %02x\n", PT);
	AppInfo("PF : %02x\n", PF);
	AppInfo("HT : %02x\n", HT);
	AppInfo("CID : %04x\n", CID);
	AppInfo("PD : %02x %02x %02x %02x, %u\n", hwid[11], hwid[12], hwid[13], hwid[14], PD);
	struct tm *ptm = gmtime((time_t*)&PD);
	AppInfo("%04d-%02d-%02d %02d:%02d:%02d\n", ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday, ptm->tm_hour, ptm->tm_min, ptm->tm_sec);
	AppInfo("SN : %u\n", SN);
	AppInfo("MAC : %02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	AppInfo("CustomInfoLen : %d\n", hwid[27]);
	AppInfo("CRC_FLAG : %d\n", crc32Flag);
//	AppInfo("TUYA_PID : %s\n", pstOut->TUYA_PID);
//	AppInfo("TUYA_UID : %s\n", pstOut->TUYA_UID);
//	AppInfo("TUYA_AUTHKEY : %s\n", pstOut->TUYA_AUTHKEY);
	AppInfo("CM_CODE : %s\n", pstOut->CM_CODE);
	AppInfo("============================================\n");

	//HEAD
	if (0xABCD1234 != HEAD)
	{
		AppErr("EEPROM ---head error!!!!\n");
		return false;
	}

	//涂鸦---Y804 应该为88字节	
	//custom info len
	if( 88 != customInfoLen )
	{
		AppErr("EEPROM ---custom info len error!!!!\n");
		return false;
	}

	if (0x0705 != PN)  		//产品标号
	{
		AppErr("EEPROM ---PN error!!!!\n");
		return false;
	}

	//产品类型和型号
	if( (0x00 != PC) || (0x09 != PF) || ((0x04 != HT) && (0x05 != HT) && (0x06 != HT)) ) 		//PID部分
	{
		AppErr("EEPROM ---PC or PF or HT error!!!!\n");
		return false;
	}

	//结束符
	if( 0xFF != eofFlag )
	{
		AppErr("EEPROM ---EOF error!!!!\n");
		return false;
	}

	if (crc32Flag != 0xAA)
	{
//		//旧协议头，无校验码，判断pid、uid、authkey是否是数字和字母
//		for (int i = 0; i < 32; i++)
//		{
//			if (((pstOut->TUYA_AUTHKEY[i] < '0') || (pstOut->TUYA_AUTHKEY[i] > '9')) && 
//				((pstOut->TUYA_AUTHKEY[i] < 'A') || (pstOut->TUYA_AUTHKEY[i] > 'Z')) && 
//				((pstOut->TUYA_AUTHKEY[i] < 'a') || (pstOut->TUYA_AUTHKEY[i] > 'z')))
//			{
//				AppErr("EEPROM ---tuya authkey error!!!!\n");
//				return false;
//			}
//		}
//		for (int i = 0; i < 20; i++)
//		{
//			if (((pstOut->TUYA_UID[i] < '0') || (pstOut->TUYA_UID[i] > '9')) && 
//				((pstOut->TUYA_UID[i] < 'A') || (pstOut->TUYA_UID[i] > 'Z')) && 
//				((pstOut->TUYA_UID[i] < 'a') || (pstOut->TUYA_UID[i] > 'z')))
//			{
//				AppErr("EEPROM ---tuya uuid error!!!!\n");
//				return false;
//			}
//		}
//		for (int i = 0; i < 16; i++)
//		{
//			if (((pstOut->TUYA_PID[i] < '0') || (pstOut->TUYA_PID[i] > '9')) && 
//				((pstOut->TUYA_PID[i] < 'A') || (pstOut->TUYA_PID[i] > 'Z')) && 
//				((pstOut->TUYA_PID[i] < 'a') || (pstOut->TUYA_PID[i] > 'z')))
//			{
//				AppErr("EEPROM ---tuya pid error!!!!\n");
//				return false;
//			}
//		}
	}
	else
	{
		//新协议增加了crc32校验码
		unsigned int code = crc32(hwid, 28+customInfoLen+1);
		printf("crc32code: %08x %08x\n", code,crc32CheckCode);
		if (code != crc32CheckCode)
		{
			AppErr("EEPROM ---crc32 error!!!!\n");
			return false;
		}
	}


	return true;
}

// 读取检查校验flash 配置分区中的内容
bool CEncryption::CheckFLASHTest()
{
	m_devInfoFromEEPROM.PN = 0x0701;
	m_devInfoFromEEPROM.PC = 0x00;
	m_devInfoFromEEPROM.PT = 0x03;
	m_devInfoFromEEPROM.PF = 0x00;
	m_devInfoFromEEPROM.HT = 0x01;
	m_devInfoFromEEPROM.CID = 0xff;
	m_devInfoFromEEPROM.PD = 0;
	m_devInfoFromEEPROM.SN = 0;

	unsigned char mac[6] = {0};
	memcpy(m_devInfoFromEEPROM.MAC, mac, 6);

	memset(m_devInfoFromEEPROM.TUYA_AUTHKEY, 0, sizeof(m_devInfoFromEEPROM.TUYA_AUTHKEY));
	memcpy(m_devInfoFromEEPROM.TUYA_AUTHKEY, "BI6eHxy9D3DJOD9GuNILneq79Zy5wqdY", 32); // 涂鸦 AUTH KEY	//32 bytes
	memset(m_devInfoFromEEPROM.TUYA_UID, 0, sizeof(m_devInfoFromEEPROM.TUYA_UID));
	memcpy(m_devInfoFromEEPROM.TUYA_UID, "dgcx06399919905981b5", 20); // 涂鸦 UUID 		//20 bytes
	memset(m_devInfoFromEEPROM.TUYA_PID, 0, sizeof(m_devInfoFromEEPROM.TUYA_PID));
	//memcpy(m_devInfoFromEEPROM.TUYA_PID, "4i5ofuwbb6xxslhw", 16);//励国双目安保灯
	memcpy(m_devInfoFromEEPROM.TUYA_PID, "sxrmiqf8p2aaiu4x", 16);//双目枪
	
	AppInfo("============================================\n");

	AppInfo("MODE : %02x\n", m_devInfoFromEEPROM.DevMode);
	AppInfo("PN : %04x\n", m_devInfoFromEEPROM.PN);
	AppInfo("PC : %02x\n", m_devInfoFromEEPROM.PC);
	AppInfo("PT : %02x\n", m_devInfoFromEEPROM.PT);
	AppInfo("PF : %02x\n", m_devInfoFromEEPROM.PF);
	AppInfo("HT : %02x\n", m_devInfoFromEEPROM.HT);
	AppInfo("CID : %04x\n", m_devInfoFromEEPROM.CID);
	AppInfo("TUYA_PID : %s\n", m_devInfoFromEEPROM.TUYA_PID);
	AppInfo("TUYA_UID : %s\n", m_devInfoFromEEPROM.TUYA_UID);
	AppInfo("TUYA_AUTHKEY : %s\n", m_devInfoFromEEPROM.TUYA_AUTHKEY);
	AppInfo("TUTK_UID : %s\n", m_devInfoFromEEPROM.TUTK_UID);
	AppInfo("============================================\n");

	m_iLicenseSavaType = 2;

	return true;
}
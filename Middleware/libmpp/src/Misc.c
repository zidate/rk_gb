#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <getopt.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <poll.h>
#include "PAL/MW_Common.h"

//平台库头文件
#include "isp.h"
#include "common.h"
#include "rk_gpio.h"
#include "rk_pwm.h"
#include "video.h"


static void my_usleep(unsigned long usec)
{
    struct timeval tv;
    tv.tv_sec = usec / 1000000;
    tv.tv_usec = usec % 1000000;

    int err;
    do {
        err = select(0, NULL, NULL, NULL, &tv);
    } while(err < 0 && errno == EINTR);
}


static int switch_bw_mode()
{
	rk_isp_set_night_to_day_v10(0,"night");

	return 0;
}

static int switch_color_mode()
{
	rk_isp_set_night_to_day_v10(0,"day");

	return 0;
}

static int fpir = -1;
static int pir_ready = 0;
static int pir_init = 0;
#define PIR_MAGIC         'p'
#define PIR_SET_GPIO           _IOW(PIR_MAGIC, 0xa0, struct PIR_GPIO )
#define PIR_SET_RUN            _IOW(PIR_MAGIC, 0xa1, int )  //0-stop 1-start
#define PIR_SET_SEN            _IOW(PIR_MAGIC, 0xa2, int )  //0-低 1-中 2-高
#define PIR_CHECK_READY        _IOW(PIR_MAGIC, 0xa3, int)   //check pir if is ready  1-not 0-ready

struct PIR_GPIO
{
    int gpio_out;
    int gpio_sen;
};

static int PirIsReady()
{
	if (-1 == fpir)
		return -1;
	
	int is_ready = 1;
	if (ioctl(fpir, PIR_CHECK_READY, &is_ready) < 0)
	{
		EMSG("check pir is ready error!\n");
		return -1;
	}
	if (1 == is_ready)
		return 0;
	
	return 1;
}

static int PirInit()
{
	fpir = open("/dev/pir", O_RDWR | O_NONBLOCK);
	if(fpir < 0)
	{
		perror("open error!\n");
		return -1;
	}

	return 0;
}

static int PirDeInit()
{
	if (fpir == -1)
	{
		return -1;
	}

	int run = 0;
	if(ioctl(fpir, PIR_SET_RUN, &run) < 0)
	{
		EMSG("set pir stop error!\n");
		return -1;
	}
	else
	{
		MSG("set pir stop ok\n");
	}

	close(fpir);
	fpir = -1;
	
	return 0;
}

static int PirCheckInit()
{
	if (pir_init != 1)
	{
		int ret = -1;
		ret = PirInit();
		if (0 == ret)
		{
			pir_init = 1;
		}
		else
		{
			pir_init = 0;
		}
	}

	return pir_init;
}

static int PirCheckReady()
{
	if (pir_ready != 1)
	{
		pir_ready = PirIsReady();
	}

	return pir_ready;
}

//===========================================//

int SystemRest()
{
	static int pro = 0;
	uint32_t gpio_num = GPIO(RK_GPIO2,RK_PA1);
	if(pro == 0)
	{
		pro = 1;
		rk_gpio_export_direction(gpio_num,GPIO_DIRECTION_INPUT);
	}
    return rk_gpio_get_value(gpio_num);
	// rk_gpio_unexport(gpionum);
}

int SystemSetDay(void)
{
	return switch_color_mode();
}

int SystemSetNight(void)
{
	return switch_bw_mode();
}

int SystemSetDayColor(void)
{
	return switch_color_mode();
}

int SystemSetNightColor(void)
{
	return switch_bw_mode();
}

int SystemSetColor(int value)
{
	//value= 0;白天
	//value = 1;黑夜
	static int color_mode = -1;
	if ((value != 0)  && (value != 1)) {
		return -1;
	}

	if (color_mode != value) 
	{

		color_mode = value;

		if (0 == value)
		{
			switch_color_mode();
		}
		else
		{
			switch_bw_mode();
		}
	}
	else 
	{
		return 1;
	}

	return 0;
}

int CaptureSetISPMode(int iMode)
{
	//return rv1106_dual_ipc_SystemSetColor(iMode);

	//value= 0;白天
	//value = 1;黑夜
	static int isp_mode = -1;
	if ((iMode != 0)  && (iMode != 1)) {
		return -1;
	}

	if (isp_mode != iMode) 
	{
		isp_mode = iMode;

		if (0 == iMode)
		{
			rk_isp_set_scenario_v10(0,"day");
		}
		else
		{
			rk_isp_set_scenario_v10(0,"night");
		}
	}
	else 
	{
		return 1;
	}

	return 0;
}

int SystemSetIrcut(int value)
{
	uint32_t gpio_a =GPIO(RK_GPIO1,RK_PA3);
	uint32_t gpio_b =GPIO(RK_GPIO1,RK_PA4);
	static int s_value = -1;
	static int pro = 0;
	if(0 == pro)
	{
		pro = 1;
		rk_gpio_export_direction(gpio_a, GPIO_DIRECTION_OUTPUT);
		rk_gpio_export_direction(gpio_b, GPIO_DIRECTION_OUTPUT);

	}

	if (s_value == value)
		return 0;
	
	s_value = value;

	if(0 == value)
	{
		rk_gpio_set_value(gpio_a, 0);
		rk_gpio_set_value(gpio_b, 1);
		usleep(200000);
		rk_gpio_set_value(gpio_a, 0);
		rk_gpio_set_value(gpio_b, 0);

	}
	else
	{
		rk_gpio_set_value(gpio_a, 1);
		rk_gpio_set_value(gpio_b, 0);
		usleep(200000);
		rk_gpio_set_value(gpio_a, 0);
		rk_gpio_set_value(gpio_b, 0);
	}

	// rk_gpio_unexport(gpio_a);
	// rk_gpio_unexport(gpio_b);


	return 0;
}

int SystemReadAdc()
{
	//cat /sys/bus/iio/devices/iio\:device0/in_voltage1_raw 

	FILE *fp;
	int vlaue = 0;
	char buf[64]={0};	 
	char cmd[128];

	strcpy(cmd, "cat /sys/bus/iio/devices/iio\:device0/in_voltage1_raw");
	fp = popen(cmd, "r");	
	if(NULL == fp)	 
	{	
		perror("popen error");
		return -1;	 
	}
	if (fgets(buf, sizeof(buf), fp) != NULL)
	{
		vlaue = atoi(buf);
	}
	else
	{
		perror("fgets error");
	}
	
	pclose(fp);   

	return vlaue;
}

int SystemPirInit(void)
{
#ifdef RC0240_LGV10
	printf("PirCheckInit -> 0\n");
	if (PirCheckInit() != 1)
	{
		EMSG("pir is not init\n");
		return -1;
	}
	printf("PirCheckInit -> 1\n");

	if (PirCheckReady() != 1)
	{
		EMSG("pir is not ready\n");
		return -1;
	}
	printf("PirCheckReady -> 1\n");

	return 0;
#else
	return 0;
#endif
}

int SystemPirDeInit(void)
{
#ifdef RC0240_LGV10
	PirDeInit();
	return 0;
#else
	return 0;
#endif
}

int SystemPirDet(void)
{
#ifdef RC0240_LGV10
	static int pro = 0;
	if(pro == 0)
	{
		pro = 1;

		int run = 1;
		if(ioctl(fpir, PIR_SET_RUN, &run) < 0)
		{
			EMSG("set pir run error!\n");
			return -1;
		}
		else
		{
			MSG("set pir run ok\n");
		}
	}
	
	int ret;
    struct pollfd fds;//定义一个pollfd结构体key_fds
    unsigned int val = 0;
    memset(&fds,0,sizeof(fds));

    fds.fd = fpir;//文件
    fds.events = POLLIN;//poll直接返回需要的条件

	ret = poll((struct pollfd *)&fds, 1, 200);//调用sys_poll系统调用，如果200毫秒内没有产生POLLIN事件，那么返回，如果有POLLIN事件，直接返回
	if(!ret)
	{
//		 printf("get pir out time out\n");
		return -1;
	}
	else
	{
		if(fds.revents == POLLIN)//如果返回的值是POLLIN，说明有数据POLL才返回的
		{
			ret = read(fpir, &val, sizeof(val));//读取pir输出值
			if (ret <=0)
			{
				EMSG("read pir out value error\n");
				return -1;
			}
			
//			 printf("get status:0x%x\n",val);
			return val;
		}
	}
    
	return -1;
#else
	return 0;
#endif
}

int SystemSetPirSen(int val,int option)//设置pir灵敏度
{
#ifdef RC0240_LGV10
	static int s_v = -1;
	int value = val;
	if (s_v == val)
	{
		return 0;
	}
	s_v = val;
	#if 0
	int run = 0;
	//临时停止pir检测 ，设置灵敏度会导致pir输出有波形
	if(ioctl(fpir, PIR_SET_RUN, &run) < 0)
	{
		printf("set pir run error!\n");
		return -1;
	}
	else
	{
		printf("set pir run  ok\n");
	}
	#endif
	//设置灵敏度
	if(ioctl(fpir, PIR_SET_SEN, &value) < 0)
	{
		EMSG("set pir sen error!\n");
		return -1;
	}
	else
	{
		MSG("set pir sen ok\n");
		return 0;
	}
	#if 0
	//开启pir检测
	run = 1;
	if(ioctl(fpir, PIR_SET_RUN, &run) < 0)
	{
		printf("set pir run error!\n");
		return -1;
	}
	else
	{
		printf("set pir run  ok\n");
	}
	#endif
#else
	return 0;
#endif
}

int SystemSetLight_Power(int value)
{
	uint32_t gpio_led_r = GPIO(RK_GPIO2,RK_PA4);//WiFi枪机
	static int pro = 0;
	static int s_value = -1;
	if(0 == pro)
	{
		pro = 1;
		rk_gpio_export_direction(gpio_led_r, GPIO_DIRECTION_OUTPUT);
	}

	if (s_value == value)
		return 0;
	
	s_value = value;

	if(0 == value)
	{
		rk_gpio_set_value(gpio_led_r, 0);
	}
	else
	{
		rk_gpio_set_value(gpio_led_r, 1);
	}
	// rk_gpio_unexport(gpio_led_r);
	return 0;
}

int SystemSetLight_Link(int value)
{
	uint32_t gpio_led_b = GPIO(RK_GPIO2,RK_PA3);//WiFi枪机
	static int pro = 0;
	static int s_value = -1;
	if(0 == pro)
	{
		pro = 1;
		rk_gpio_export_direction(gpio_led_b, GPIO_DIRECTION_OUTPUT);
	}

	if (s_value == value)
		return 0;
	
	s_value = value;


	if(0 == value)
	{
		rk_gpio_set_value(gpio_led_b, 0);
	}
	else
	{
		rk_gpio_set_value(gpio_led_b, 1);
	}
	// rk_gpio_unexport(gpio_led_b);
	return 0;
}

int SystemSetLight_Epi(int value)
{
	uint32_t gpio_led_act = GPIO(RK_GPIO0,RK_PA5);
	static int pro = 0;
	static int s_value = -1;
	if(0 == pro)
	{
		pro = 1;
		rk_gpio_export_direction(gpio_led_act, GPIO_DIRECTION_OUTPUT);
	}

	if (s_value == value)
		return 0;
	
	s_value = value;


	if(0 == value)
	{
		rk_gpio_set_value(gpio_led_act, 0);
	}
	else
	{
		rk_gpio_set_value(gpio_led_act, 1);
	}
	// rk_gpio_unexport(gpio_led_act);
	return 0;
}

/// 设置speak 开关 	0:开 	1:关
int SystemAutioCtl(int value)
{
	uint32_t gpio_num = GPIO(RK_GPIO0,RK_PA0);
	static int pro = 0;
	static int s_value = -1;
	if(0 == pro)
	{
		pro = 1;
		rk_gpio_export_direction(gpio_num, GPIO_DIRECTION_OUTPUT);
	}

	if (s_value == value)
		return 0;
	
	s_value = value;


	if(0 == value)
	{
		rk_gpio_set_value(gpio_num, 0);
	}
	else
	{
		rk_gpio_set_value(gpio_num, 1);
	}
	// rk_gpio_unexport(gpio_num);
	return 0;
}

extern int dayornight;
int SystemGetAE()
{
	return dayornight;
}

int SystemSetInfraredLamp(int value) //红外灯
{
	uint32_t gpio_led_ir = GPIO(RK_GPIO1,RK_PB0);
	static int pro = 0;
	static int s_value = -1;
	if(0 == pro)
	{
		pro = 1;
		rk_gpio_export_direction(gpio_led_ir, GPIO_DIRECTION_OUTPUT);
	}

	if (s_value == value)
		return 0;
	
	s_value = value;


	if(0 == value)
	{
		rk_gpio_set_value(gpio_led_ir, 0);
	}
	else
	{
		rk_gpio_set_value(gpio_led_ir, 1);
	}
	// rk_gpio_unexport(gpio_led_ir);
	return 0;
}

/// 设置wifi 开关 	0:开 	1:关
int SystemWifiPwrCtl(int value)
{
	uint32_t gpio_num = GPIO(RK_GPIO0,RK_PA4);
	static int pro = 0;
	static int s_value = -1;
	if(0 == pro)
	{
		pro = 1;
		rk_gpio_export_direction(gpio_num, GPIO_DIRECTION_OUTPUT);
	}

	if (s_value == value)
		return 0;
	
	s_value = value;


	if(0 == value)
	{
		rk_gpio_set_value(gpio_num, 0);
	}
	else
	{
		rk_gpio_set_value(gpio_num, 1);
	}
	// rk_gpio_unexport(gpio_num);
	return 0;
}

// 设置白光灯 IO
int SystemSetIncandescentLamp(int value)
{
	return 0;
	uint32_t gpio_num = GPIO(RK_GPIO1,RK_PB1);
	// LOG_INFO("\n\nSystemSetIncandescentLamp value:%d \n\n",value);
	static int pro = 0;
	static int s_value = -1;
	if(0 == pro)
	{
		pro = 1;
		rk_gpio_export_direction(gpio_num, GPIO_DIRECTION_OUTPUT);
	}

	if (s_value == value)
		return 0;
	
	s_value = value;


	if(0 == value)
	{
		rk_gpio_set_value(gpio_num, 0);
	}
	else
	{
		rk_gpio_set_value(gpio_num, 1);
	}
	// rk_gpio_unexport(gpio_num);
	return 0;
}

// 设置白光灯 PWM
static int SystemInitIncandescentLamppro = 0;
int SystemInitIncandescentLamp()
{
	return 0;
}

int SystemUnInitIncandescentLamp()
{
	return 0;
}

int SystemSetIncandescentLampLumi(int duty,int period_white)
{
	int ret;
	uint32_t pwm;
	pwm = 7;
	period_white = 1000000;

	//单位：纳秒
	float fduty = duty;//100-duty;
	LOG_INFO("aSystemSetIncandescentLampLumi duty=%d,period_white=%d\n",duty,period_white);
	fduty = period_white*(fduty/100);
	duty = fduty;
	LOG_INFO("bSystemSetIncandescentLampLumi duty=%d,period_white=%d\n",duty,period_white);

#if 0
	ret = rk_pwm_set_duty(pwm, duty);
	if (ret)
	{
		LOG_INFO("pwm%d rk_pwm_set_duty %d error %d\n", pwm, duty,ret);
		return -1;
	}
	ret = rk_pwm_set_period(pwm, period_white);
	if (ret)
	{
		LOG_INFO("pwm%d rk_pwm_set_period %d error %d\n", pwm, period_white,ret);
		return -1;
	}
	ret = rk_pwm_set_polarity(pwm, PWM_POLARITY_NORMAL);
	if (ret)
	{
		LOG_INFO("pwm%d rk_pwm_set_polarity %d error %d\n", pwm, PWM_POLARITY_NORMAL,ret);
		return -1;
	}
	ret = rk_pwm_set_enable(pwm, true);
	if (ret)
	{
		LOG_INFO("pwm%d rk_pwm_set_enable error %d\n", pwm,ret);
		return -1;
	}
#else
	if(SystemInitIncandescentLamppro == 1)
	{
		// LOG_INFO("pwm%d rk_pwm_deinit\n", pwm);
		ret = rk_pwm_deinit(pwm);
		if (ret)
		{
			LOG_INFO("pwm%d rk_pwm_deinit error %d\n", pwm,ret);
			return ret;
		}
		SystemInitIncandescentLamppro = 0;
	}
	// LOG_INFO("pwm%d rk_pwm_init\n", pwm);
	ret = rk_pwm_init(pwm, period_white, duty, PWM_POLARITY_NORMAL);
	if (ret) {
		LOG_INFO("pwm%d init failed %d\n", pwm, ret);
		return ret;
	}
	SystemInitIncandescentLamppro = 1;
	// LOG_INFO("pwm%d rk_pwm_set_enable\n", pwm);
	ret = rk_pwm_set_enable(pwm, true);
	if (ret)
	{
		LOG_INFO("pwm%d rk_pwm_set_enable error %d\n", pwm,ret);
		return ret;
	}
#endif
	return 0;
}
int SystemSetIncandescentLampSwitch(int enable)
{
	return 0;
}

int SystemInitIncandescentLamp_yellow()
{
	return 0;
}

int SystemUnInitIncandescentLamp_yellow()
{
	return 0;
}

int SystemSetIncandescentLampLumi_yellow(int duty,int period_white)
{
	return 0;
}
int SystemSetIncandescentLampSwitch_yellow(int enable)
{
	return 0;
}

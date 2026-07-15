// Copyright 2021 Rockchip Electronics Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "rockiva.h"
#include "common.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "rockiva.c"
int g_rotation = 0;
int rockiva_door_open_flag = 0;
static int rockiva_start_flag = 0;
static hvn_detect_callback human_detect_cb = NULL;
static hvn_detect_callback vehicle_detect_cb = NULL;
static hvn_detect_callback non_vehicle_detect_cb = NULL;
static hvn_motion_tracker_callback motion_tracker_cb = NULL;

static RockIvaInfo rkba_info = 	{	0,90,
									{	1,
										{4,{{0,0},{9999,0},{9999,9999},{0,9999}}}
									}
								};

static int doRectanglesIntersect(RockIvaRectangle rect1, RockIvaRectangle rect2);

pthread_mutex_t g_rknn_list_mutex = PTHREAD_MUTEX_INITIALIZER;
RockIvaHandle rkba_handle;
RockIvaBaTaskParams initParams;
RockIvaInitParam globalParams;
int rockit_run_flag = 0;
static void *rockiva_signal = NULL;
rknn_list *rknn_list_;

void create_rknn_list(rknn_list **s) {
	pthread_mutex_lock(&g_rknn_list_mutex);
	if (*s != NULL) {
		pthread_mutex_unlock(&g_rknn_list_mutex);
		return;
	}
	*s = (rknn_list *)malloc(sizeof(rknn_list));
	(*s)->top = NULL;
	(*s)->size = 0;
	pthread_mutex_unlock(&g_rknn_list_mutex);
	printf("create rknn_list success\n");
}

void destory_rknn_list(rknn_list **s) {
	pthread_mutex_lock(&g_rknn_list_mutex);
	Node *t = NULL;
	if (*s == NULL) {
		pthread_mutex_unlock(&g_rknn_list_mutex);
		return;
	}
	while ((*s)->top) {
		t = (*s)->top;
		(*s)->top = t->next;
		free(t);
	}
	free(*s);
	*s = NULL;
	pthread_mutex_unlock(&g_rknn_list_mutex);
}

void rknn_list_push(rknn_list *s, long long timeval, RockIvaBaResult ba_result) {
	pthread_mutex_lock(&g_rknn_list_mutex);
	Node *t = NULL;
	t = (Node *)malloc(sizeof(Node));
	t->timeval = timeval;
	t->ba_result = ba_result;
	if (s->top == NULL) {
		s->top = t;
		t->next = NULL;
	} else {
		t->next = s->top;
		s->top = t;
	}
	s->size++;
	pthread_mutex_unlock(&g_rknn_list_mutex);
}

void rknn_list_pop(rknn_list *s, long long *timeval, RockIvaBaResult *ba_result) {
	pthread_mutex_lock(&g_rknn_list_mutex);
	Node *t = NULL;
	if (s == NULL || s->top == NULL) {
		pthread_mutex_unlock(&g_rknn_list_mutex);
		return;
	}
	t = s->top;
	*timeval = t->timeval;
	*ba_result = t->ba_result;
	s->top = t->next;
	free(t);
	s->size--;
	pthread_mutex_unlock(&g_rknn_list_mutex);
}

void rknn_list_drop(rknn_list *s) {
	pthread_mutex_lock(&g_rknn_list_mutex);
	Node *t = NULL;
	if (s == NULL || s->top == NULL) {
		pthread_mutex_unlock(&g_rknn_list_mutex);
		return;
	}
	t = s->top;
	s->top = t->next;
	free(t);
	s->size--;
	pthread_mutex_unlock(&g_rknn_list_mutex);
}

int rknn_list_size(rknn_list *s) {
	if (s == NULL)
		return -1;
	return s->size;
}

int rkipc_rknn_object_get(RockIvaBaResult *ba_result) {
	int ret = 0;
	long long time_before;
	if (rknn_list_size(rknn_list_)) {
		rknn_list_pop(rknn_list_, &time_before, ba_result);
		LOG_DEBUG("ba_result->objNum is %d\n", ba_result->objNum);
		ret = 0;
	} else {
		ret = -1; // no update
	}

	return ret;
}
static unsigned char flicker_draw = 0;
static unsigned char flicker_draw_flag = 0;
static unsigned char opendoorflag = 0;
static uint32_t s_bak_objNum;										  /* 目标个数 */
static RockIvaBaObjectInfo s_bak_triggerObjects[ROCKIVA_MAX_OBJ_NUM];  /* 触发周界规则的目标 */
void rkba_callback(const RockIvaBaResult *presult, const RockIvaExecuteStatus status,
                   void *userData) {
#if 1
//	printf("rkba_callback start frame[%u]\n", presult->frameId);

	s_bak_objNum = 0;

	for (int i = 0; i < presult->objNum; i++) {
//		printf("topLeft:[%d,%d], bottomRight:[%d,%d],"
//				"objId is %d, frameId is %d, score is %d, type is %d\n",
//				presult->triggerObjects[i].objInfo.rect.topLeft.x,
//				presult->triggerObjects[i].objInfo.rect.topLeft.y,
//				presult->triggerObjects[i].objInfo.rect.bottomRight.x,
//				presult->triggerObjects[i].objInfo.rect.bottomRight.y,
//				presult->triggerObjects[i].objInfo.objId,
//				presult->triggerObjects[i].objInfo.frameId,
//				presult->triggerObjects[i].objInfo.score, presult->triggerObjects[i].objInfo.type);
//
//		printf("triggerRules is %d, ruleID is %d, triggerType is %d\n",
//				presult->triggerObjects[i].triggerRules,
//				presult->triggerObjects[i].firstTrigger.ruleID,
//				presult->triggerObjects[i].firstTrigger.triggerType);
		// score 低于 60 的 丢弃
		if (presult->triggerObjects[i].objInfo.score < 60)
			continue;
		memcpy(&s_bak_triggerObjects[s_bak_objNum++], &presult->triggerObjects[i], sizeof(RockIvaBaObjectInfo));
	}

//	s_bak_objNum = presult->objNum;
//	memcpy(s_bak_triggerObjects, presult->triggerObjects, sizeof(RockIvaBaObjectInfo)*ROCKIVA_MAX_OBJ_NUM);

	rknn_list_push(rknn_list_, rkipc_get_curren_time_ms(), *presult);
	int size = rknn_list_size(rknn_list_);
	if (size >= MAX_RKNN_LIST_NUM)
		rknn_list_drop(rknn_list_);
	
//	printf("rkba_callback end frame[%u]\n", presult->frameId);

#else
	int triggerd = 0;
	int triggerd_none = 0;
	RockIvaBaResult *result;
    result = (RockIvaBaResult *)presult;

	//人形检测功能开启 ///注释有误，这里应该是人形检测功能未开启 add on 2025.02.11
	if (!rockiva_start_flag) {
		result->objNum = 0;

		rknn_list_push(rknn_list_, rkipc_get_curren_time_ms(), *result);
		int size = rknn_list_size(rknn_list_);
		if (size >= MAX_RKNN_LIST_NUM)
			rknn_list_drop(rknn_list_);

		RockIvaRectangle rect;
		rect.topLeft.x = 0;
		rect.topLeft.y = 0;
		rect.bottomRight.x = 0;
		rect.bottomRight.y = 0;

		if (human_detect_cb)
			human_detect_cb(0,rect);

		return ;
	}

	if (result->objNum == 0) {
		RockIvaRectangle rect;
		rect.topLeft.x = 0;
		rect.topLeft.y = 0;
		rect.bottomRight.x = 0;
		rect.bottomRight.y = 0;

		if (human_detect_cb)
			human_detect_cb(0,rect);
	}

	// LOG_INFO("status is %d, frame %d, result->objNum is %d\n", status, result->frameId,result->objNum);

	// 人形检测区域结果跟设置区域比较结果保存到new_result
	RockIvaBaResult new_result;
	RockIvaBaResult *pnew_result = NULL;
	memset(&new_result,0,sizeof(new_result));
	pnew_result = (RockIvaBaResult *)&new_result;
	int k = 0;
	for (int i = 0; i < result->objNum; i++) {//检测到的目标个数（框的个数）add on 2025.02.11
		// LOG_INFO("topLeft:[%d,%d], bottomRight:[%d,%d],"
		// 		"objId is %d, frameId is %d, score is %d, type is %d\n",
		// 		result->triggerObjects[i].objInfo.rect.topLeft.x,
		// 		result->triggerObjects[i].objInfo.rect.topLeft.y,
		// 		result->triggerObjects[i].objInfo.rect.bottomRight.x,
		// 		result->triggerObjects[i].objInfo.rect.bottomRight.y,
		// 		result->triggerObjects[i].objInfo.objId,
		// 		result->triggerObjects[i].objInfo.frameId,
		// 		result->triggerObjects[i].objInfo.score, result->triggerObjects[i].objInfo.type);

		// LOG_INFO("triggerRules is %d, ruleID is %d, triggerType is %d\n",
		//          result->triggerObjects[i].triggerRules,
		//          result->triggerObjects[i].firstTrigger.ruleID,
		//          result->triggerObjects[i].firstTrigger.triggerType);
		
		for (int j = 0; j< rkba_info.areas.areaNum; j++) {//遍历所有设置的区域 add on 2025.02.11
			RockIvaRectangle rect;
			rect.topLeft.x = rkba_info.areas.areas[j].points[0].x;
			rect.topLeft.y = rkba_info.areas.areas[j].points[0].y;
			rect.bottomRight.x = rkba_info.areas.areas[j].points[2].x;
			rect.bottomRight.y = rkba_info.areas.areas[j].points[2].y;

			if (doRectanglesIntersect(result->triggerObjects[i].objInfo.rect,rect)) {//有交集，表明触发 add on 2025.02.11
				if (pnew_result->triggerObjects[k].objInfo.type == ROCKIVA_OBJECT_TYPE_NONE) {//当前的数组没有被使用过 add on 2025.02.11
					pnew_result->triggerObjects[k].objInfo.rect.topLeft.x = result->triggerObjects[i].objInfo.rect.topLeft.x;
					pnew_result->triggerObjects[k].objInfo.rect.topLeft.y = result->triggerObjects[i].objInfo.rect.topLeft.y;
					pnew_result->triggerObjects[k].objInfo.rect.bottomRight.x = result->triggerObjects[i].objInfo.rect.bottomRight.x;
					pnew_result->triggerObjects[k].objInfo.rect.bottomRight.y = result->triggerObjects[i].objInfo.rect.bottomRight.y;
					pnew_result->triggerObjects[k].objInfo.type = result->triggerObjects[i].objInfo.type;
					triggerd = 1;
				}
			} else {
				if (pnew_result->triggerObjects[k].objInfo.type != ROCKIVA_OBJECT_TYPE_NONE) {//该数组被使用过，不能将其覆盖 add on 2025.02.11
					//do nonething
				} else {																	//该数组没有被使用过，但是当前的矩形不在设置的区域中 add on 2025.02.11		
					pnew_result->triggerObjects[k].objInfo.rect.topLeft.x = result->triggerObjects[i].objInfo.rect.topLeft.x;
					pnew_result->triggerObjects[k].objInfo.rect.topLeft.y = result->triggerObjects[i].objInfo.rect.topLeft.y;
					pnew_result->triggerObjects[k].objInfo.rect.bottomRight.x = result->triggerObjects[i].objInfo.rect.bottomRight.x;
					pnew_result->triggerObjects[k].objInfo.rect.bottomRight.y = result->triggerObjects[i].objInfo.rect.bottomRight.y;

					pnew_result->triggerObjects[k].objInfo.type = ROCKIVA_OBJECT_TYPE_NONE;
				}
			}
		}
		k++;
	}
	pnew_result->objNum = k;

	// LOG_INFO("new status is %d, frame %d, result->objNum is %d\n", status, pnew_result->frameId,pnew_result->objNum);

	if (pnew_result->objNum == 0) {	//没有触发人形检测 add on 2025.02.11
		RockIvaRectangle rect;
		rect.topLeft.x = 0;
		rect.topLeft.y = 0;
		rect.bottomRight.x = 0;
		rect.bottomRight.y = 0;

		if (human_detect_cb)
			human_detect_cb(0,rect);
	} else {						//有触发人形检测 add on 2025.02.11
		if (triggerd) {				//人形检测框在设置的区域中 add on 2025.02.11
			for (int i = 0; i < pnew_result->objNum; i++) {
				switch (pnew_result->triggerObjects[i].objInfo.type)
				{
					case ROCKIVA_OBJECT_TYPE_PERSON:
					{
						if (human_detect_cb)
							human_detect_cb(1,pnew_result->triggerObjects[i].objInfo.rect);
					}
					break;

					// case ROCKIVA_OBJECT_TYPE_VEHICLE:
					// {
					// 	if (vehicle_detect_cb)
					// 		vehicle_detect_cb(1,pnew_result->triggerObjects[i].objInfo.rect);
					// }
					// break;

					// case ROCKIVA_OBJECT_TYPE_NON_VEHICLE:
					// {
					// 	if (non_vehicle_detect_cb)
					// 		non_vehicle_detect_cb(1,pnew_result->triggerObjects[i].objInfo.rect);
					// }
					// break;
				
					default:
					break;
				}
			}
		} else {					//人形检测框不在设置的区域中（无效） add on 2025.02.11
			RockIvaRectangle rect;
			rect.topLeft.x = 0;
			rect.topLeft.y = 0;
			rect.bottomRight.x = 0;
			rect.bottomRight.y = 0;

			if (human_detect_cb)
				human_detect_cb(0,rect);
		}
	}

	rknn_list_push(rknn_list_, rkipc_get_curren_time_ms(), *pnew_result);
	int size = rknn_list_size(rknn_list_);
	if (size >= MAX_RKNN_LIST_NUM)
		rknn_list_drop(rknn_list_);

	// LOG_INFO("size is %d\n", size);
#endif
}

void rockiva_frame_release_callback(const RockIvaReleaseFrames *releaseFrames, void *userdata) {
	// LOG_INFO("%s: releaseFrames channelId is %d, count is %d\n", get_time_string());
//	printf("iva release channelId: %u, count: %u, ", releaseFrames->channelId, releaseFrames->count);
//	for (unsigned int i = 0; i < releaseFrames->count; i++)	
//		printf("frame[%u].frameId: %u ", i, releaseFrames->frames[i].frameId);
//	printf("\n");
	rk_signal_give(rockiva_signal);//释放信号量 add on 2025.02.11 注释
}

int rkipc_rockiva_init() {
	LOG_INFO("begin\n");
	RockIvaRetCode ret;
	const char *model_type;
	int rotation = rk_param_get_int("video.source:rotation", 0);
	// char *license_path = NULL;
	// char *license_key;
	// int license_size;

	memset(&initParams, 0, sizeof(initParams));
	memset(&globalParams, 0, sizeof(globalParams));

	// if (license_path != NULL) {
	//     license_size = read_data_file(license_path, &license_key);
	//     if (license_key != NULL && license_size > 0) {
	//         globalParams.license.memAddr = license_key;
	//         globalParams.license.memSize = license_size;
	//     }
	// }

#if 0
	snprintf(globalParams.modelPath, ROCKIVA_PATH_LENGTH, "/oem/usr/lib/");
	globalParams.coreMask = 0x04;
	globalParams.logLevel = ROCKIVA_LOG_ERROR;
	model_type = rk_param_get_string("event.regional_invasion:rockiva_model_type", "small");
	if (!strcmp(model_type, "small") || !strcmp(model_type, "medium")) {
		globalParams.detModel |= ROCKIVA_DET_MODEL_PFP;
	} else if (!strcmp(model_type, "big")) {
		globalParams.detModel |= ROCKIVA_DET_MODEL_CLS7;
	}
	globalParams.imageInfo.width = rk_param_get_int("video.2:width", 640);
	globalParams.imageInfo.height = rk_param_get_int("video.2:height", 360);
	globalParams.imageInfo.format = ROCKIVA_IMAGE_FORMAT_YUV420SP_NV12;
	// temporary solution
	// which will be changed to reinitialize when the resolution is dynamically switched
	if (rk_param_get_int("video.source:rotate_in_venc", 0))
		rotation = 0;
	if (rotation == 0 || rotation == 180) {
		globalParams.imageInfo.transformMode = ROCKIVA_IMAGE_TRANSFORM_ROTATE_180;
	} else if (rotation == 90) {
		globalParams.imageInfo.transformMode = ROCKIVA_IMAGE_TRANSFORM_ROTATE_90;
	} else if (rotation == 270) {
		globalParams.imageInfo.transformMode = ROCKIVA_IMAGE_TRANSFORM_ROTATE_270;
	}

	// globalParams.imageInfo.transformMode = rkba_info.rotation;
#else
//	snprintf(globalParams.modelPath, ROCKIVA_PATH_LENGTH, "/mnt/sdcard/det_data/");
	snprintf(globalParams.modelPath, ROCKIVA_PATH_LENGTH, "/oem/usr/lib/");
	globalParams.coreMask = 0x04;
	globalParams.logLevel = ROCKIVA_LOG_ERROR;
	globalParams.detModel = ROCKIVA_DET_MODEL_CLS8;
	globalParams.imageInfo.width = rk_param_get_int("video.2:width", 640);
	globalParams.imageInfo.height = rk_param_get_int("video.2:height", 360);
	globalParams.imageInfo.format = ROCKIVA_IMAGE_FORMAT_YUV420SP_NV12;
	globalParams.imageInfo.transformMode = ROCKIVA_IMAGE_TRANSFORM_NONE;
#endif

	ROCKIVA_Init(&rkba_handle, ROCKIVA_MODE_VIDEO, &globalParams, NULL);
	LOG_INFO("ROCKIVA_Init over\n");

	// 构建一个区域入侵规则
	int web_width = rk_param_get_int("osd.common:normalized_screen_width", 640);
	int web_height = rk_param_get_int("osd.common:normalized_screen_height", 360);
	int ri_x = rk_param_get_int("event.regional_invasion:position_x", 0);
	int ri_y = rk_param_get_int("event.regional_invasion:position_y", 0);
	int ri_w = rk_param_get_int("event.regional_invasion:width", 640);
	int ri_h = rk_param_get_int("event.regional_invasion:height", 360);

	initParams.baRules.areaInBreakRule[0].ruleEnable = rk_param_get_int("event.regional_invasion:enabled", 0);
	//算法用固定灵敏度，APP上的灵敏度设置通过判断算法结果置信度实现
	initParams.baRules.areaInBreakRule[0].sense = 50;//rk_param_get_int("event.regional_invasion:sensitivity_level", 50); // [1, 100]
	initParams.baRules.areaInBreakRule[0].alertTime = rk_param_get_int("event.regional_invasion:time_threshold", 1) * 1000; // ms
	initParams.baRules.areaInBreakRule[0].minObjSize[2].height = web_height / 100 * rk_param_get_int("event.regional_invasion:proportion", 5);
	initParams.baRules.areaInBreakRule[0].minObjSize[2].width = web_width / 100 * rk_param_get_int("event.regional_invasion:proportion", 5);
	initParams.baRules.areaInBreakRule[0].event = ROCKIVA_BA_TRIP_EVENT_STAY;
	initParams.baRules.areaInBreakRule[0].ruleID = 0;
	initParams.baRules.areaInBreakRule[0].objType = ROCKIVA_OBJECT_TYPE_BITMASK(ROCKIVA_OBJECT_TYPE_PERSON);
	// initParams.baRules.areaInBreakRule[0].objType |= ROCKIVA_OBJECT_TYPE_BITMASK(ROCKIVA_OBJECT_TYPE_FACE);
	// initParams.baRules.areaInBreakRule[0].objType |= ROCKIVA_OBJECT_TYPE_BITMASK(ROCKIVA_OBJECT_TYPE_PET);
	initParams.baRules.areaInBreakRule[0].objType |= ROCKIVA_OBJECT_TYPE_BITMASK(ROCKIVA_OBJECT_TYPE_VEHICLE);
	initParams.baRules.areaInBreakRule[0].objType |= ROCKIVA_OBJECT_TYPE_BITMASK(ROCKIVA_OBJECT_TYPE_NON_VEHICLE);
	initParams.baRules.areaInBreakRule[0].objType |= ROCKIVA_OBJECT_TYPE_BITMASK(ROCKIVA_OBJECT_TYPE_FACE);
//	initParams.baRules.areaInBreakRule[0].objType |= ROCKIVA_OBJECT_TYPE_BITMASK(ROCKIVA_OBJECT_TYPE_HEAD);
//	initParams.baRules.areaInBreakRule[0].objType |= ROCKIVA_OBJECT_TYPE_BITMASK(ROCKIVA_OBJECT_TYPE_MOTORCYCLE);
//	initParams.baRules.areaInBreakRule[0].objType |= ROCKIVA_OBJECT_TYPE_BITMASK(ROCKIVA_OBJECT_TYPE_BICYCLE);
//	initParams.baRules.areaInBreakRule[0].objType |= ROCKIVA_OBJECT_TYPE_BITMASK(ROCKIVA_OBJECT_TYPE_PLATE);
	

	initParams.baRules.areaInBreakRule[0].area.pointNum = 4;
	#if 0
	initParams.baRules.areaInBreakRule[0].area.points[0].x = ROCKIVA_PIXEL_RATION_CONVERT(web_width, ri_x);
	initParams.baRules.areaInBreakRule[0].area.points[0].y = ROCKIVA_PIXEL_RATION_CONVERT(web_height, ri_y);
	initParams.baRules.areaInBreakRule[0].area.points[1].x = ROCKIVA_PIXEL_RATION_CONVERT(web_width, ri_x + ri_w);
	initParams.baRules.areaInBreakRule[0].area.points[1].y = ROCKIVA_PIXEL_RATION_CONVERT(web_height, ri_y);
	initParams.baRules.areaInBreakRule[0].area.points[2].x = ROCKIVA_PIXEL_RATION_CONVERT(web_width, ri_x + ri_w);
	initParams.baRules.areaInBreakRule[0].area.points[2].y = ROCKIVA_PIXEL_RATION_CONVERT(web_height, ri_y + ri_h);
	initParams.baRules.areaInBreakRule[0].area.points[3].x = ROCKIVA_PIXEL_RATION_CONVERT(web_width, ri_x);
	initParams.baRules.areaInBreakRule[0].area.points[3].y = ROCKIVA_PIXEL_RATION_CONVERT(web_height, ri_y + ri_h);
	#else
	//设置成全画面
	initParams.baRules.areaInBreakRule[0].area.points[0].x = 0;
	initParams.baRules.areaInBreakRule[0].area.points[0].y = 0;
	initParams.baRules.areaInBreakRule[0].area.points[1].x = 9999;
	initParams.baRules.areaInBreakRule[0].area.points[1].y = 0;
	initParams.baRules.areaInBreakRule[0].area.points[2].x = 9999;
	initParams.baRules.areaInBreakRule[0].area.points[2].y = 9999;
	initParams.baRules.areaInBreakRule[0].area.points[3].x = 0;
	initParams.baRules.areaInBreakRule[0].area.points[3].y = 9999;
	#endif
	//	LOG_INFO("(%d,%d), (%d,%d), (%d,%d), (%d,%d)\n",
		printf("(%d,%d), (%d,%d), (%d,%d), (%d,%d)\n",
		initParams.baRules.areaInBreakRule[0].area.points[0].x,
		initParams.baRules.areaInBreakRule[0].area.points[0].y,
		initParams.baRules.areaInBreakRule[0].area.points[1].x,
		initParams.baRules.areaInBreakRule[0].area.points[1].y,
		initParams.baRules.areaInBreakRule[0].area.points[2].x,
		initParams.baRules.areaInBreakRule[0].area.points[2].y,
		initParams.baRules.areaInBreakRule[0].area.points[3].x,
		initParams.baRules.areaInBreakRule[0].area.points[3].y);

#if 1
	initParams.aiConfig.detectResultMode = 0;
#else
	initParams.aiConfig.detectResultMode = 1; // 上报没有触发规则的检测目标, 临时调试用
#endif
	ret = ROCKIVA_BA_Init(rkba_handle, &initParams, rkba_callback);
	if (ret != ROCKIVA_RET_SUCCESS) {
		printf("ROCKIVA_BA_Init error %d\n", ret);
		return -1;
	}
	LOG_INFO("ROCKIVA_BA_Init success\n");

	if (rockiva_signal)							//如果前面已经创建了信号量，则先释放掉 add on 2025.02.11 注释
		rk_signal_destroy(rockiva_signal);
	rockiva_signal = rk_signal_create(0, 1);	//创建信号量 add on 2025.02.11 注释
	if (!rockiva_signal) {						//创建信号量失败 add on 2025.02.11 注释
		LOG_ERROR("create signal fail\n");
		return -1;
	}
	ROCKIVA_SetFrameReleaseCallback(rkba_handle, rockiva_frame_release_callback);

	create_rknn_list(&rknn_list_);
	rockit_run_flag = 1;
	LOG_INFO("end\n");

	return ret;

}

int rkipc_rockiva_deinit() {
	LOG_INFO("begin\n");
	rockit_run_flag = 0;
	ROCKIVA_BA_Release(rkba_handle);
	LOG_INFO("ROCKIVA_BA_Release over\n");
	ROCKIVA_Release(rkba_handle);
	destory_rknn_list(&rknn_list_);
	if (rockiva_signal) {						//如果有信号量 add on 2025.02.11 注释
		rk_signal_give(rockiva_signal);			//释放信号量 add on 2025.02.11 注释
		rk_signal_destroy(rockiva_signal);		//销毁信号量 add on 2025.02.11 注释
		rockiva_signal = NULL;
	}
	LOG_INFO("end\n");

	return 0;
}

int rkipc_rockiva_write_rgb888_frame(uint16_t width, uint16_t height, uint32_t frame_id,
                                     unsigned char *buffer) {
	int ret;
	RockIvaImage *image = (RockIvaImage *)malloc(sizeof(RockIvaImage));
	if (!rockit_run_flag)
		return 0;
	memset(image, 0, sizeof(RockIvaImage));
	image->info.width = width;
	image->info.height = height;
	image->info.format = ROCKIVA_IMAGE_FORMAT_BGR888;
	image->dataAddr = buffer;
	image->frameId = frame_id;
	ret = ROCKIVA_PushFrame(rkba_handle, image, NULL);
	if (ret == 0)
		rk_signal_wait(rockiva_signal, 10000);
	else
		LOG_ERROR("ROCKIVA_PushFrame fail, ret is %d\n", ret);
	free(image);

	return ret;
}

int rkipc_rockiva_write_rgb888_frame_by_fd(uint16_t width, uint16_t height, uint32_t frame_id,
                                           int32_t fd) {
	int ret;
	RockIvaImage *image = (RockIvaImage *)malloc(sizeof(RockIvaImage));
	if (!rockit_run_flag)
		return 0;
	memset(image, 0, sizeof(RockIvaImage));
	image->info.width = width;
	image->info.height = height;
	image->info.format = ROCKIVA_IMAGE_FORMAT_BGR888;
	image->frameId = frame_id;
	image->dataAddr = NULL;
	image->dataPhyAddr = NULL;
	image->dataFd = fd;
	ret = ROCKIVA_PushFrame(rkba_handle, image, NULL);
	if (ret == 0)
		rk_signal_wait(rockiva_signal, 10000);
	else
		LOG_ERROR("ROCKIVA_PushFrame fail, ret is %d\n", ret);
	free(image);

	return ret;
}

int rkipc_rockiva_write_nv12_frame_by_fd(uint16_t width, uint16_t height, uint32_t frame_id,
                                         int32_t fd) {
	if (!rockit_run_flag)
		return -1;
	int ret;
	RockIvaImage *image = (RockIvaImage *)malloc(sizeof(RockIvaImage));
	int rotation = rk_param_get_int("video.source:rotation", 0);
	if (rk_param_get_int("video.source:rotate_in_venc", 0))
		rotation = 0;
	memset(image, 0, sizeof(RockIvaImage));
	#if 0
	if (rotation == 0) {
		image->info.transformMode = ROCKIVA_IMAGE_TRANSFORM_NONE;
	} else if (rotation == 90) {
		image->info.transformMode = ROCKIVA_IMAGE_TRANSFORM_ROTATE_90;
	} else if (rotation == 180) {
		image->info.transformMode = ROCKIVA_IMAGE_TRANSFORM_ROTATE_180;
	} else if (rotation == 270) {
		image->info.transformMode = ROCKIVA_IMAGE_TRANSFORM_ROTATE_270;
	}
	#else
	if (g_rotation == 0) {
		image->info.transformMode = ROCKIVA_IMAGE_TRANSFORM_NONE;
	} else if (g_rotation == 90) {
		image->info.transformMode = ROCKIVA_IMAGE_TRANSFORM_ROTATE_90;
	} else if (g_rotation == 180) {
		image->info.transformMode = ROCKIVA_IMAGE_TRANSFORM_ROTATE_180;
	} else if (g_rotation == 270) {
		image->info.transformMode = ROCKIVA_IMAGE_TRANSFORM_ROTATE_270;
	}
	image->info.transformMode = ROCKIVA_IMAGE_TRANSFORM_NONE;
	#endif
	image->info.width = width;
	image->info.height = height;
	image->info.format = ROCKIVA_IMAGE_FORMAT_YUV420SP_NV12;
	image->frameId = frame_id;
	image->dataAddr = NULL;
	image->dataPhyAddr = NULL;
	image->dataFd = fd;
	ret = ROCKIVA_PushFrame(rkba_handle, image, NULL);
	if (ret == 0)
		rk_signal_wait(rockiva_signal, 10000);
	else
		LOG_ERROR("ROCKIVA_PushFrame fail, ret is %d\n", ret);
	free(image);

	return ret;
}

//out_objNum , out_triggerObjects 不需要释放
int rkipc_rockiva_write_nv12_frame_by_fd_for_gb(uint16_t width, uint16_t height, uint32_t frame_id,
                                         int32_t fd, uint32_t **out_objNum, RockIvaBaObjectInfo **out_triggerObjects) {
	if (!rockit_run_flag || !out_objNum || !out_triggerObjects)
		return -1;
	int ret;
	RockIvaImage image;

	memset(&image, 0, sizeof(RockIvaImage));
	//在VI做的翻转，即视频源数据已经翻转，这里就不用设置翻转模式了
	image.info.transformMode = ROCKIVA_IMAGE_TRANSFORM_NONE;
	image.info.width = width;
	image.info.height = height;
	image.info.format = ROCKIVA_IMAGE_FORMAT_YUV420SP_NV12;
	image.frameId = frame_id;
	image.dataAddr = NULL;
	image.dataPhyAddr = NULL;
	image.dataFd = fd;
//	printf("send to iva frame[%u]\n", frame_id);
	ret = ROCKIVA_PushFrame(rkba_handle, &image, NULL);
	if (ret)
	{
		LOG_ERROR("ROCKIVA_PushFrame fail, ret is %d\n", ret);
		return -1;
	}
	rk_signal_wait(rockiva_signal, 10000);
//	printf("iva proc frame[%u] completely.\n", frame_id);

	*out_objNum = &s_bak_objNum;
	*out_triggerObjects = s_bak_triggerObjects;

	return 0;
}


int rkipc_rockiva_write_nv12_frame_by_phy_addr(uint16_t width, uint16_t height, uint32_t frame_id,
                                               uint8_t *phy_addr) {
	if (!rockit_run_flag)
		return 0;
	int ret;
	RockIvaImage *image = (RockIvaImage *)malloc(sizeof(RockIvaImage));
	int rotation = rk_param_get_int("video.source:rotation", 0);
	if (rk_param_get_int("video.source:rotate_in_venc", 0))
		rotation = 0;
	memset(image, 0, sizeof(RockIvaImage));
#if 0
	if (rotation == 0) {
		image->info.transformMode = ROCKIVA_IMAGE_TRANSFORM_NONE;
	} else if (rotation == 90) {
		image->info.transformMode = ROCKIVA_IMAGE_TRANSFORM_ROTATE_90;
	} else if (rotation == 180) {
		image->info.transformMode = ROCKIVA_IMAGE_TRANSFORM_ROTATE_180;
	} else if (rotation == 270) {
		image->info.transformMode = ROCKIVA_IMAGE_TRANSFORM_ROTATE_270;
	}
#else
	if (g_rotation == 0) {
		image->info.transformMode = ROCKIVA_IMAGE_TRANSFORM_NONE;
	} else if (g_rotation == 90) {
		image->info.transformMode = ROCKIVA_IMAGE_TRANSFORM_ROTATE_90;
	} else if (g_rotation == 180) {
		image->info.transformMode = ROCKIVA_IMAGE_TRANSFORM_ROTATE_180;
	} else if (g_rotation == 270) {
		image->info.transformMode = ROCKIVA_IMAGE_TRANSFORM_ROTATE_270;
	}
#endif
	image->info.width = width;
	image->info.height = height;
	image->info.format = ROCKIVA_IMAGE_FORMAT_YUV420SP_NV12;
	image->frameId = frame_id;
	image->dataPhyAddr = phy_addr;
	ret = ROCKIVA_PushFrame(rkba_handle, image, NULL);
	if (ret == 0)
		rk_signal_wait(rockiva_signal, 10000);
	else
		LOG_ERROR("ROCKIVA_PushFrame fail, ret is %d\n", ret);
	free(image);

	return ret;
}

static int doRectanglesIntersect(RockIvaRectangle rect1, RockIvaRectangle rect2) 
{
    // 判断两个矩形是否有交集
    if (rect1.topLeft.x >= rect2.bottomRight.x || rect1.bottomRight.x <= rect2.topLeft.x) {
        return 0; // 矩形没有交集
    }
    if (rect1.topLeft.y >= rect2.bottomRight.y || rect1.bottomRight.y <= rect2.topLeft.y) {
        return 0; // 矩形没有交集
    }
    // 两个矩形有交集
    return 1;
}

int rkipc_rockiva_start()
{
	rockiva_start_flag = 1;
	return 0;
}
int rkipc_rockiva_stop()
{
	rockiva_start_flag = 0;
	return 0;
}

int rkipc_rockiva_set(RockIvaInfo *pinfo)
{
	if (!pinfo)
		return -1;

	memcpy(&rkba_info,pinfo,sizeof(RockIvaInfo));
	if (pinfo->sense != initParams.baRules.areaInBreakRule[0].sense) {
		initParams.baRules.areaInBreakRule[0].sense = pinfo->sense;
		RockIvaRetCode ret;
		ret = ROCKIVA_BA_Reset(rkba_handle, &initParams);
		if (ret != ROCKIVA_RET_SUCCESS) {
			LOG_ERROR("ROCKIVA_BA_Reset error %d\n", ret);
			return -1;
		}
		LOG_INFO("ROCKIVA_BA_Reset sense %d\n", initParams.baRules.areaInBreakRule[0].sense);
	}
	
#if 0
	RockIvaRetCode ret;
	memset(&initParams, 0, sizeof(initParams));
	// 构建区域入侵规则
	int web_width = rk_param_get_int("osd.common:normalized_screen_width", 640);
	int web_height = rk_param_get_int("osd.common:normalized_screen_height", 360);
	for (int i = 0; i < rkba_info.areas.areaNum; i++)
	{
		initParams.baRules.areaInBreakRule[i].ruleEnable = rk_param_get_int("event.regional_invasion:enabled", 0);
		initParams.baRules.areaInBreakRule[i].sense = rk_param_get_int("event.regional_invasion:sensitivity_level", 50); // [1, 100]
		initParams.baRules.areaInBreakRule[i].alertTime = rk_param_get_int("event.regional_invasion:time_threshold", 1) * 1000; // ms
		initParams.baRules.areaInBreakRule[i].minObjSize[2].height = web_height / 100 * rk_param_get_int("event.regional_invasion:proportion", 5);
		initParams.baRules.areaInBreakRule[i].minObjSize[2].width = web_width / 100 * rk_param_get_int("event.regional_invasion:proportion", 5);
		initParams.baRules.areaInBreakRule[i].event = ROCKIVA_BA_TRIP_EVENT_STAY;
		initParams.baRules.areaInBreakRule[i].ruleID = 0;
		initParams.baRules.areaInBreakRule[i].objType = ROCKIVA_OBJECT_TYPE_BITMASK(ROCKIVA_OBJECT_TYPE_PERSON);
		initParams.baRules.areaInBreakRule[i].objType |= ROCKIVA_OBJECT_TYPE_BITMASK(ROCKIVA_OBJECT_TYPE_FACE);
		initParams.baRules.areaInBreakRule[i].objType |= ROCKIVA_OBJECT_TYPE_BITMASK(ROCKIVA_OBJECT_TYPE_PET);

		initParams.baRules.areaInBreakRule[i].area.pointNum = rkba_info.areas.areas[i].pointNum;
		initParams.baRules.areaInBreakRule[i].area.points[0].x = rkba_info.areas.areas[i].points[0].x;
		initParams.baRules.areaInBreakRule[i].area.points[0].y = rkba_info.areas.areas[i].points[0].y;
		initParams.baRules.areaInBreakRule[i].area.points[1].x = rkba_info.areas.areas[i].points[1].x;
		initParams.baRules.areaInBreakRule[i].area.points[1].y = rkba_info.areas.areas[i].points[1].y;
		initParams.baRules.areaInBreakRule[i].area.points[2].x = rkba_info.areas.areas[i].points[2].x;
		initParams.baRules.areaInBreakRule[i].area.points[2].y = rkba_info.areas.areas[i].points[2].y;
		initParams.baRules.areaInBreakRule[i].area.points[3].x = rkba_info.areas.areas[i].points[3].x;
		initParams.baRules.areaInBreakRule[i].area.points[3].y = rkba_info.areas.areas[i].points[3].y;

		LOG_INFO("(%d,%d), (%d,%d), (%d,%d), (%d,%d)\n",
			initParams.baRules.areaInBreakRule[i].area.points[0].x,
			initParams.baRules.areaInBreakRule[i].area.points[0].y,
			initParams.baRules.areaInBreakRule[i].area.points[1].x,
			initParams.baRules.areaInBreakRule[i].area.points[1].y,
			initParams.baRules.areaInBreakRule[i].area.points[2].x,
			initParams.baRules.areaInBreakRule[i].area.points[2].y,
			initParams.baRules.areaInBreakRule[i].area.points[3].x,
			initParams.baRules.areaInBreakRule[i].area.points[3].y);
	}
	initParams.aiConfig.detectResultMode = 0;
	ret = ROCKIVA_BA_Reset(rkba_handle, &initParams);
	if (ret != ROCKIVA_RET_SUCCESS) {
		LOG_ERROR("ROCKIVA_BA_Init error %d\n", ret);
		return -1;
	}
#endif
	return 0;
}

int rkipc_rockiva_get(RockIvaInfo *pinfo)
{
	memcpy(pinfo,&rkba_info,sizeof(RockIvaInfo));
	return 0;
}

int rkipc_rockiva_clean_hvn_detect_callback(int type)
{
	int ret = -1;

	switch (type)
	{
		case ROCKIVA_OBJECT_TYPE_PERSON:
		{
			human_detect_cb = NULL;
			ret = 0;
		}
		break;

		case ROCKIVA_OBJECT_TYPE_VEHICLE:
		{
			vehicle_detect_cb = NULL;
			ret = 0;
		}
		break;

		case ROCKIVA_OBJECT_TYPE_NON_VEHICLE:
		{
			non_vehicle_detect_cb = NULL;
			ret = 0;
		}
		break;
	
		default:
			break;
	}
	
	return ret;
}
int rkipc_rockiva_set_hvn_detect_callback(int type,hvn_detect_callback cb)
{
	int ret = -1;
	if (!cb)
	{
		return -1;
	}
	switch (type)
	{
		case ROCKIVA_OBJECT_TYPE_PERSON:
		{
			human_detect_cb = cb;
			ret = 0;
		}
		break;

		case ROCKIVA_OBJECT_TYPE_VEHICLE:
		{
			vehicle_detect_cb = cb;
			ret = 0;
		}
		break;

		case ROCKIVA_OBJECT_TYPE_NON_VEHICLE:
		{
			non_vehicle_detect_cb = cb;
			ret = 0;
		}
		break;
	
		default:
			break;
	}
	
	return ret;
}

int rkipc_rockiva_set_hvn_motion_tracker_callback(hvn_motion_tracker_callback cb)
{
	if (!cb)
	{
		return -1;
	}
	motion_tracker_cb = cb;
	return 0;
}

int rkipc_rockiva_clean_hvn_motion_tracker_callback()
{
	motion_tracker_cb = NULL;
	return 0;
}

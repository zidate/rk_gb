/**
 * @file dev_log_test.h
 * @brief 设备日志轮询测试线程 - 每隔2s轮询写入设备日志，方便测试
 *
 * 用法:
 *   - 在 dev_log_init() 之后调用 dev_log_test_start() 启动测试线程
 *   - 在 dev_log_deinit() 之前调用 dev_log_test_stop() 停止测试线程
 */
#ifndef __DEV_LOG_TEST_H__
#define __DEV_LOG_TEST_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 启动设备日志轮询测试线程
 * @return 0-成功, -1-失败
 */
int dev_log_test_start(void);

/**
 * 停止设备日志轮询测试线程
 */
void dev_log_test_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* __DEV_LOG_TEST_H__ */

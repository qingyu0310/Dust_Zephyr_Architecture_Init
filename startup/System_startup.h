/**
 * @file System_startup.h
 * @author qingyu
 * @brief 系统启动接口
 * @version 0.3
 * @date 2026-10-20
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 按阶段启动已注册组件
 *
 * 顺序：PreInit/PreThread → EarlyInit/EarlyThread → MidInit/MidThread
 *       → LateInit/LateThread → AppInit/AppThread。
 * 由 main() 调用一次，保证返回。
 */
void System_Startup(void);

/**
 * @brief 系统启动是否被中止
 *
 * 线程项以 HaltOnFail 策略启动失败时置位，后续阶段不再执行。
 * 由 main() 在 System_Startup() 返回后查询，决定进入正常运行还是停机。
 *
 * @return 1=已中止，0=正常
 */
int System_StartupAborted(void);

#ifdef __cplusplus
}
#endif

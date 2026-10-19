/**
 * @file System_startup.h
 * @author qingyu
 * @brief 系统启动接口
 * @version 0.2
 * @date 2026-10-19
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
 * 由 main() 调用一次。
 */
void System_Startup(void);

/**
 * @brief 系统启动是否被中止（线程启动失败即置位，后续阶段不再执行）
 * @return 1=已中止，0=正常
 */
int System_StartupAborted(void);

#ifdef __cplusplus
}
#endif

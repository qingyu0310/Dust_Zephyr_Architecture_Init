/**
 * @file main.cpp
 * @author qingyu
 * @brief 系统入口：启动架构层，失败时蜂鸣停机，正常则空转
 * @version 0.2
 * @date 2026-10-20
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <zephyr/kernel.h>
#include "System_startup.h"
#include "buzzer.hpp"
#include "log.hpp"

/**
 * @brief 系统入口
 *
 * System_Startup() 返回后检查中止标志：线程启动失败（HaltOnFail）时
 * 记录日志、长鸣并停机；否则进入空转，由各线程承担业务。
 *
 * @return 不返回（空转或停机循环）
 */
int main(void)
{
    System_Startup();

    if (System_StartupAborted())
    {
        DUST_LOG_ERR("[boot] startup aborted, halting");
        EXEC_BUZZER_LONG();

        for (;;) {
            k_msleep(1000);      // 休眠停机：让 shell 线程把日志发完
        }
    }

    for (;;) {
        k_msleep(1000);
    }
}

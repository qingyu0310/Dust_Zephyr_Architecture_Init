/**
 * @file thread_fault.cpp
 * @author qingyu
 * @brief 线程故障默认处置函数的弱实现
 * @version 0.5
 * @date 2026-10-24
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "thread_fault.hpp"
#include "thread.hpp"
#include "thread_registry.hpp"
#include "log.hpp"
#include "buzzer.hpp"
#include <zephyr/kernel.h>

#pragma message "Compiling Init/Thread/Thread_fault"

/**
 * @brief 停机：长鸣后终止所有线程，带保留标记的线程（shell）留作观察窗口
 */
[[noreturn]] static void Halt()
{
    EXEC_BUZZER_LONG();

    const k_tid_t self = k_current_get();

    // 先处决其他线程；Exit() 会把目标移出注册表，count 变小时同一位置由尾部补上。
    // 带保留标记的线程（shell 日志消费者）跳过不杀，留作停机后的观察窗口。
    for (uint8_t i = 0; i < thread_registry::Count(); )
    {
        ThreadBase* thread = thread_registry::At(i);
        if (thread == nullptr || thread->Tid() == self || thread->KeepAlive()) {
            ++i;
            continue;
        }
        thread->Exit();
    }

    // 最后处决自己（不返回）
    for (uint8_t i = 0; i < thread_registry::Count(); ++i)
    {
        ThreadBase* thread = thread_registry::At(i);
        if (thread != nullptr && thread->Tid() == self) {
            thread->Exit();
        }
    }

    // 调用者不在注册表（如中断上下文）时的兜底
    for (;;) {
        k_msleep(1000);
    }
}

/**
 * @brief 默认处置：按线程声明的策略兜底
 *
 * 只在 RetryHook 处理不了故障时进入，即"任务自己已经救不回来了"。
 * 因此任何策略都不会让故障线程原样继续 —— 那等于下一拍必然重犯。
 *
 * @param thread 出故障的线程
 */
void DefaultHandle(ThreadBase* thread)
{
    if (thread == nullptr) {
        DUST_LOG_ERR("thread fault with null thread, halting");
        Halt();
    }

    switch (thread->FaultPolicy())
    {
        case ThreadFaultPolicy::Isolate:
		{
            // 影响小：收尾后自杀，不拖累系统
            DUST_LOG_WRN("thread %s fault, isolating", thread->Name());
            thread->Exit();
		}

        case ThreadFaultPolicy::Recover:
        default:
		{
            // 影响大且无法恢复：不能只死自己，全部线程终止并保留现场
            DUST_LOG_ERR("thread %s fatal, halting", thread->Name());
            Halt();
		}
    }
}

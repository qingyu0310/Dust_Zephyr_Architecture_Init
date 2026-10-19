/**
 * @file Init_entry.cpp
 * @author qingyu
 * @brief 统一启动实现：注册表遍历、失败回滚与启动中止记录
 * @version 0.9
 * @date 2026-10-20
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "Init_entry.hpp"
#include "System_startup.h"
#include "boot_timing.hpp"
#include "thread_registry.hpp"
#include "buzzer.hpp"
#include "log.hpp"
#include <zephyr/kernel.h>

static bool startup_aborted_ = false;  		 			// 启动中止标志（线程启动失败时置位）

namespace {

constexpr uint8_t kMaxInitedEntries = 32;   			// 已成功初始化项上限

const InitEntry*  inited_entries[kMaxInitedEntries];    // 已成功初始化项（按执行顺序）
uint8_t           inited_count = 0;

} // namespace

/**
 * @brief 线程登记入口：Thread::Start 创建线程后调用
 * @param thread 已创建的线程对象
 */
void RegisterThreadInstance(ThreadBase* thread)
{
    (void)thread_registry::Add(thread);
}

/**
 * @brief 停机前逆序回滚已成功初始化项
 *
 * cleanup 须支持「成功初始化后调用」场景（关闭输出、设备进低功耗等），
 * 而不只是部分初始化失败的回滚。
 */
static void RollbackInitedEntries()
{
    for (int i = static_cast<int>(inited_count) - 1; i >= 0; --i)
    {
        if (inited_entries[i]->cleanup != nullptr)
        {
            inited_entries[i]->cleanup();
            DUST_LOG_WRN("[boot] %s rolled back", inited_entries[i]->name);
        }
        else
        {
            DUST_LOG_WRN("[boot] %s has no cleanup", inited_entries[i]->name);
        }
    }
}

/**
 * @brief 初始化项失败处理：记录日志，HaltOnFail 时回滚并停机
 *
 * 停机前逆序回滚所有已成功项，把硬件放回安全态（PWM 停输出、Flash 掉电等）。
 * 用长鸣而非无限鸣叫，避免高优先级主线程饿死低优先级 shell 线程导致日志发不出。
 *
 * @param entry 失败的注册项
 */
static void HandleInitFail(const InitEntry& entry)
{
    bool halt = (entry.fail == InitFailPolicy::HaltOnFail);

    // 失败横幅：HaltOnFail 红色 ERR、ContinueOnFail 橘色 WRN
    if (halt) {
        DUST_LOG_ERR("[boot] %s fail", entry.name);
    } else {
        DUST_LOG_WRN("[boot] %s fail", entry.name);
    }

    if (halt) 
	{
        RollbackInitedEntries();                 	// 停机前把硬件放回安全态
        EXEC_BUZZER_LONG();                         // 长鸣两声即返回，不阻塞日志发送

        for (;;) {
            k_msleep(1000);                     // 休眠停机：让 shell 线程把日志发完
        }
    }
}

/**
 * @brief 阶段内执行顺序：同一阶段先跑 High、再 Mid、最后 Low
 */
static constexpr InitLevel kLevelOrder[] = {
    InitLevel::High,
    InitLevel::Mid,
    InitLevel::Low,
};

/**
 * @brief 初始化阶段名（启动横幅用）
 */
static const char* StageName(InitStage stage)
{
    switch (stage)
    {
        case InitStage::PreInit:   return "PreInit";
        case InitStage::EarlyInit: return "EarlyInit";
        case InitStage::MidInit:   return "MidInit";
        case InitStage::LateInit:  return "LateInit";
        case InitStage::AppInit:   return "AppInit";
        default:                   return "?";
    }
}

/**
 * @brief 线程阶段名（启动横幅用）
 */
static const char* StageName(ThreadStage stage)
{
    switch (stage)
    {
        case ThreadStage::PreThread:   return "PreThread";
        case ThreadStage::EarlyThread: return "EarlyThread";
        case ThreadStage::MidThread:   return "MidThread";
        case ThreadStage::LateThread:  return "LateThread";
        case ThreadStage::AppThread:   return "AppThread";
        default:                       return "?";
    }
}

/**
 * @brief 执行一个初始化阶段：同阶段内按 High → Mid → Low 三轮遍历
 *
 * 每项执行前后计时并写入耗时槽。失败项先调 cleanup 回滚，再交给
 * HandleInitFail 按策略处理；成功项登记到已完成表，供停机时逆序回滚。
 *
 * @param stage 当前初始化阶段
 */
static void RunStage(InitStage stage)
{
    if (startup_aborted_) return;                        // 启动已中止：不再执行后续阶段

    extern const InitEntry* const __user_init_start[];
    extern const InitEntry* const __user_init_end[];

    for (const InitLevel level : kLevelOrder)
    {
        for (const InitEntry* const* item = __user_init_start; item < __user_init_end; ++item)
        {
            const InitEntry* e = *item;

            if (e->stage != static_cast<InitStage>(stage)) {
                continue;
            }
            if (e->level != level) {
                continue;
            }
            const uint64_t begin_cycle = k_cycle_get_64();
            const bool     ok          = e->function();
            BootTiming::Record(e->timing,
                               static_cast<uint32_t>(k_cyc_to_ns_floor64(k_cycle_get_64() - begin_cycle)),
                               ok);

            if (!ok) {
                if (e->cleanup != nullptr) e->cleanup();    	// 部分初始化失败也回滚本项资源
                HandleInitFail(*e);
                continue;                      				 	// 失败：不打印初始化提示
            }

            if (inited_count < kMaxInitedEntries) {
                inited_entries[inited_count++] = e;         	// 登记已成功项，供停机时回滚
            } else {
                DUST_LOG_ERR("[boot] init table full, %s not tracked", e->name);
            }

            DUST_LOG_OK("[boot] %s init", e->name);   		// 谁在初始化
        }
    }

    // 阶段横幅：阶段内条目全部执行完再打印（此时 DUST_LOG 已初始化，PreInit 完成也能出），
    // 表示哪个阶段初始化完成
    DUST_LOG_OK("=====     [boot] %s ok     =====\n", StageName(stage));
}

/**
 * @brief 执行一个线程启动阶段
 *
 * 线程项没有等级，一遍遍历。启动函数返回 false 时按 fail 策略处理：
 * HaltOnFail 置中止标志并立即返回（后续阶段全部跳过），
 * ContinueOnFail 记警告后继续启动其它线程。
 *
 * @param stage 当前线程阶段
 */
static void RunStage(ThreadStage stage)
{
    if (startup_aborted_) return;                        // 启动已中止：不再启动后续线程

    extern const ThreadEntry* const __user_thread_start[];
    extern const ThreadEntry* const __user_thread_end[];

    for (const ThreadEntry* const* item = __user_thread_start; item < __user_thread_end; ++item)
    {
        const ThreadEntry* e = *item;

        if (e->stage != stage) {
            continue;
        }

        const uint64_t begin_cycle = k_cycle_get_64();
        const bool     ok          = e->start();
        BootTiming::Record(e->timing,
                           static_cast<uint32_t>(k_cyc_to_ns_floor64(k_cycle_get_64() - begin_cycle)),
                           ok);

        if (!ok)                       							// 线程启动失败：按策略处理
        {
            if (e->fail == InitFailPolicy::HaltOnFail)
            {
                DUST_LOG_ERR("[boot] %s start fail, halting", e->name);
                startup_aborted_ = true;
                return;                       					// 中止后续所有启动
            }

            DUST_LOG_WRN("[boot] %s start fail, continue", e->name);
            continue;                         					// 非关键线程失败：继续
        }

        DUST_LOG_OK("[boot] %s start", e->name);   			// 谁的线程启动
    }

    // 阶段横幅：线程阶段全部启动完再打印
    DUST_LOG_OK("=====     [boot] %s ok     =====\n", StageName(stage));
}

/**
 * @brief 系统启动入口
 *
 * 顺序：PreInit/PreThread → EarlyInit/EarlyThread → MidInit/MidThread
 *       → LateInit/LateThread → AppInit/AppThread。
 * 每对先执行 Init（初始化对象），再启动对应的 Thread（线程循环）。
 * 阶段间保留 500 ms 给硬件上电稳定，完成后短鸣提示。
 *
 * 本函数保证返回：线程启动失败只记录到 System_StartupAborted()，
 * 停机策略由 main() 决定。
 */
void System_Startup(void)
{
    constexpr uint16_t kNormalStageDelayMs = 500;

    RunStage(InitStage::PreInit);
    k_msleep(kNormalStageDelayMs);
    RunStage(ThreadStage::PreThread);
    k_msleep(kNormalStageDelayMs);
    EXEC_BUZZER_SHORT();

    RunStage(InitStage::EarlyInit);
    k_msleep(kNormalStageDelayMs);
    RunStage(ThreadStage::EarlyThread);
    k_msleep(kNormalStageDelayMs);
    EXEC_BUZZER_SHORT();

    RunStage(InitStage::MidInit);
    k_msleep(kNormalStageDelayMs);
    RunStage(ThreadStage::MidThread);
    k_msleep(kNormalStageDelayMs);
    EXEC_BUZZER_SHORT();

    RunStage(InitStage::LateInit);
    k_msleep(kNormalStageDelayMs);
    RunStage(ThreadStage::LateThread);
    k_msleep(kNormalStageDelayMs);
    EXEC_BUZZER_SHORT();

    RunStage(InitStage::AppInit);
    k_msleep(kNormalStageDelayMs);
    RunStage(ThreadStage::AppThread);
}

/**
 * @brief 系统启动是否被中止
 *
 * 线程项以 HaltOnFail 策略启动失败时置位。由 main 在 System_Startup()
 * 返回后查询，决定是进入正常运行还是提示停机。
 *
 * @return 1=已中止，0=正常
 */
int System_StartupAborted(void)
{
    return startup_aborted_ ? 1 : 0;
}

/**
 * @file Init_entry.cpp
 * @author qingyu
 * @brief 统一启动实现：注册表遍历与失败回滚
 * @version 0.3
 * @date 2026-10-19
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "Init_entry.hpp"
#include "System_startup.h"
#include "thread_registry.hpp"
#include "buzzer.hpp"
#include "log.hpp"
#include <zephyr/kernel.h>

static bool startup_aborted = false;   // 启动中止标志（线程启动失败时置位）

/**
 * @brief 线程登记入口：Thread::Start 创建线程后调用
 * @param thread 已创建的线程对象
 */
void RegisterThreadInstance(ThreadBase* thread)
{
    (void)thread_registry::Add(thread);
}

static void HandleInitFail(const InitEntry& entry)
{
    bool halt = (entry.fail == InitFailPolicy::HaltOnFail);

    // 失败横幅：HaltOnFail 红色 ERR、ContinueOnFail 橘色 WRN
    if (halt) {
        DUST_LOG_ERR("[boot] %s fail", entry.name);
    } else {
        DUST_LOG_WRN("[boot] %s fail", entry.name);
    }

    if (halt) {
        EXEC_BUZZER_ERR(nullptr);
        while (1) {}
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

static void RunStage(InitStage stage)
{
    if (startup_aborted) return;                        // 启动已中止：不再执行后续阶段

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
            if (!e->function()) {
                if (e->cleanup != nullptr) e->cleanup();    	// 部分初始化失败也回滚本项资源
                HandleInitFail(*e);
                continue;                      				 	// 失败：不打印初始化提示
            }

            DUST_LOG_OK("[boot] %s init", e->name);   		// 谁在初始化
        }
    }

    // 阶段横幅：阶段内条目全部执行完再打印（此时 DUST_LOG 已初始化，PreInit 完成也能出），
    // 表示哪个阶段初始化完成
    DUST_LOG_OK("=====     [boot] %s ok     =====\n", StageName(stage));
}

static void RunStage(ThreadStage stage)
{
    if (startup_aborted) return;                        // 启动已中止：不再启动后续线程

    extern const ThreadEntry* const __user_thread_start[];
    extern const ThreadEntry* const __user_thread_end[];

    for (const ThreadEntry* const* item = __user_thread_start; item < __user_thread_end; ++item)
    {
        const ThreadEntry* e = *item;

        if (e->stage != stage) {
            continue;
        }

        if (!e->start()) {                       			// 线程启动失败：记录并中止后续启动
            DUST_LOG_ERR("[boot] %s start fail", e->name);
            startup_aborted = true;
            continue;
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
 * 由 main() 调用。
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
 * @brief 系统启动是否被中止（线程启动失败）
 * @return true=已中止
 */
int System_StartupAborted(void)
{
    return startup_aborted ? 1 : 0;
}

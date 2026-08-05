/**
 * @file Init_entry.cpp
 * @author qingyu
 * @brief
 * @version 0.1
 * @date 2026-07-20
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "Init_entry.hpp"
#include "System_startup.h"
#include "buzzer.hpp"
#include "log.hpp"
#include <zephyr/kernel.h>

static constexpr struct FailAction {
    InitLevel  level;
    bool       halt;
}
kFailActions[] {
    {InitLevel::High, true},
    {InitLevel::Mid,  false},
    {InitLevel::Low,  false},
};

static void HandleInitFail(const InitEntry& entry)
{
    bool halt = false;

    for (const auto& a : kFailActions)
    {
        if (a.level == entry.level) {
            halt = a.halt;
            break;
        }
    }

    // 失败横幅：高级红色 ERR、中级/低级橘色 WRN（打印在 halt 判断前，High 失败也能看到）
    if (entry.level == InitLevel::High) {
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
    extern const InitEntry __user_init_start[], __user_init_end[];

    for (const InitEntry* e = __user_init_start; e < __user_init_end; ++e)
    {
        if (e->stage != static_cast<InitStage>(stage)) {
            continue;
        }
        if (!e->func()) {
            HandleInitFail(*e);
            continue;                      				// 失败：不打印初始化提示
        }
        DUST_LOG_INF("[boot] %s init", e->name);   // 谁在初始化
    }

    // 阶段横幅：阶段内条目全部执行完再打印（此时 DUST_LOG 已初始化，PreInit 完成也能出），
    // 表示哪个阶段初始化完成
    DUST_LOG_OK("=====     [boot] %s ok     =====\n", StageName(stage));
}

static void RunStage(ThreadStage stage)
{
    extern const InitEntry __user_thread_start[], __user_thread_end[];

    for (const InitEntry* e = __user_thread_start; e < __user_thread_end; ++e)
    {
        if (static_cast<ThreadStage>(e->stage) != stage) {
            continue;
        }
        if (!e->func()) {
            HandleInitFail(*e);
            continue;                      					// 失败：不打印线程启动提示
        }
        DUST_LOG_INF("[boot] %s start", e->name);   	// 谁的线程启动
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

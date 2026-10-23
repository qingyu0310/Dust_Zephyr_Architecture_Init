/**
 * @file Init_entry.hpp
 * @author qingyu
 * @brief 初始化与线程注册项：阶段、优先级、失败策略和清理回调
 * @version 0.5
 * @date 2026-10-19
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstdint>
#include "boot_timing.hpp"

/**
 * @brief 初始化阶段
 *
 */
enum class InitStage : uint8_t
{
    PreInit   = 0,
    EarlyInit = 1,
    MidInit   = 2,
    LateInit  = 3,
    AppInit   = 4,
};

/**
 * @brief 线程阶段
 *
 */
enum class ThreadStage : uint8_t
{
    PreThread   = 0,
    EarlyThread = 1,
    MidThread   = 2,
    LateThread  = 3,
    AppThread   = 4,
};

/**
 * @brief 初始化等级（优先级：同一阶段内 High→Mid→Low）
 */
enum class InitLevel : uint8_t
{
    High = 0,
    Mid  = 1,
    Low  = 2,
};

/**
 * @brief 启动失败策略（初始化项与线程项共用，与优先级 InitLevel 独立，每条目自行声明）
 */
enum class StartFailPolicy : uint8_t
{
    HaltOnFail     = 0,   // 失败停机
    ContinueOnFail = 1,   // 失败警告后继续
};

/**
 * @brief 初始化函数原型
 * @return true=成功, false=失败
 */
using InitFunc = bool (*)();

/**
 * @brief 线程启动函数原型（创建线程后立即返回）
 * @return true=成功, false=失败
 */
using StartFunc = bool (*)();

/**
 * @brief 初始化回滚回调（仅启动期，勿与线程退出钩子混用）
 */
using RollbackFunc = void (*)();

/**
 * @brief 初始化项描述
 *
 * 每个组件声明一个 InitEntry，链接后形成初始化表，启动器遍历执行。
 */
struct InitEntry
{
    InitFunc          init;      // 初始化函数
    RollbackFunc      rollback;  // 初始化失败/停机前的逆序回滚；可为 nullptr
    BootTimingSlot*   timing;    // 本项耗时槽（.boot_timing 段）
    InitStage         stage;     // 所属阶段
    InitLevel         level;     // 优先级：高级/中级/低级（仅 Init 用）
    StartFailPolicy   fail;      // 失败策略：停机/继续
    const char*       name;      // 组件名，调试用
};

/**
 * @brief 线程注册项
 */
struct ThreadEntry
{
    StartFunc        start;    // 创建线程后立即返回
    BootTimingSlot*  timing;   // 本项耗时槽（.boot_timing 段）
    ThreadStage      stage;    // 所属线程启动阶段
    StartFailPolicy  fail;     // 启动失败策略：停机 / 继续
    const char*      name;     // 日志中的组件标识
};

/**
 * @brief 注册初始化项
 *
 * 每个组件用此宏声明一个 InitEntry，
 * 通过链接段收集形成初始化表，启动器遍历执行。
 * 同时生成一个耗时槽放进 .boot_timing 段，数量由实际注册项决定。
 *
 * @param fn       初始化函数（符合 InitFunc 签名）
 * @param stage_   InitStage 枚举值
 * @param level_   InitLevel 枚举值
 * @param fail_    失败策略
 * @param rollback_ 回滚函数，须支持部分初始化和未初始化状态
 * @param name_    组件标识，打印用
 */
#define REGISTER_INIT(fn, stage_, level_, fail_, rollback_, name_)                     \
    static BootTimingSlot kTiming_##fn                                                 \
        __attribute__((used, __section__(".boot_timing"), aligned(4))) =               \
            { name_, 0, false };                                                       \
    static const InitEntry kInitEntry_##fn = {                                         \
        fn, rollback_, &kTiming_##fn, InitStage::stage_, InitLevel::level_,            \
        StartFailPolicy::fail_, name_                                                  \
    };                                                                                 \
    static const InitEntry* const kInitEntryPtr_##fn                                   \
        __attribute__((used, __section__(".user_init"),                                \
                       aligned(sizeof(void*)))) = &kInitEntry_##fn

/**
 * @brief 注册线程项
 *
 * 每个线程用此宏声明一个 ThreadEntry（存放于 .user_thread 段），
 * 启动器遍历执行。同时生成一个耗时槽放进 .boot_timing 段。
 *
 * @param fn       线程启动函数（创建线程后立即返回）
 * @param stage_   ThreadStage 枚举值
 * @param fail_    失败策略：HaltOnFail 中止后续启动，ContinueOnFail 继续。
 *                 注意：只覆盖 thread_start() 返回 false 这一瞬间。线程启动
 *                 之后在运行期的故障处置由 ThreadFaultPolicy 决定，本策略不生效。
 * @param name_    组件标识，打印用
 */
#define REGISTER_THREAD(fn, stage_, fail_, name_)                                      \
    static BootTimingSlot kTiming_##fn                                                 \
        __attribute__((used, __section__(".boot_timing"), aligned(4))) =               \
            { name_, 0, false };                                                       \
    static const ThreadEntry kThreadEntry_##fn = {                                     \
        fn, &kTiming_##fn, ThreadStage::stage_, StartFailPolicy::fail_, name_          \
    };                                                                                 \
    static const ThreadEntry* const kThreadEntryPtr_##fn                               \
        __attribute__((used, __section__(".user_thread"),                              \
                       aligned(sizeof(void*)))) = &kThreadEntry_##fn

/**
 * @file Init_entry.hpp
 * @author qingyu
 * @brief 初始化与线程注册项：阶段、优先级、失败策略和清理回调
 * @version 0.3
 * @date 2026-10-19
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstdint>

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
 * @brief 初始化失败策略（与优先级 InitLevel 独立，每条目自行声明）
 */
enum class InitFailPolicy : uint8_t
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
 * @brief 资源释放、停止通知或清理回调
 */
using CleanupFunc = void (*)();

/**
 * @brief 初始化项描述
 *
 * 每个组件声明一个 InitEntry，链接后形成初始化表，启动器遍历执行。
 */
struct InitEntry
{
    InitFunc          function;  // 初始化函数
    CleanupFunc       cleanup;   // 失败回滚与退出清理；可为 nullptr
    InitStage         stage;     // 所属阶段
    InitLevel         level;     // 优先级：高级/中级/低级（仅 Init 用）
    InitFailPolicy    fail;      // 失败策略：停机/继续（仅 Init 用）
    const char*       name;      // 组件名，调试用
};

/**
 * @brief 线程注册项
 */
struct ThreadEntry
{
    InitFunc     start;   // 创建线程后立即返回
    ThreadStage  stage;   // 所属线程启动阶段
    const char*  name;    // 日志中的组件标识
};

/**
 * @brief 注册初始化项
 *
 * 每个组件用此宏声明一个 InitEntry，
 * 通过链接段收集形成初始化表，启动器遍历执行。
 *
 * @param fn       初始化函数（符合 InitFunc 签名）
 * @param stage_   InitStage 枚举值
 * @param level_   InitLevel 枚举值
 * @param fail_    失败策略
 * @param cleanup_ 清理函数，须支持部分初始化和未初始化状态
 * @param name_    组件标识，打印用
 */
#define REGISTER_INIT(fn, stage_, level_, fail_, cleanup_, name_)              \
    static const InitEntry kInitEntry_##fn = {                                 \
        fn, cleanup_, InitStage::stage_, InitLevel::level_,                    \
        InitFailPolicy::fail_, name_                                           \
    };                                                                         \
    static const InitEntry* const kInitEntryPtr_##fn                           \
        __attribute__((used, __section__(".user_init"),                        \
                       aligned(sizeof(void*)))) = &kInitEntry_##fn

/**
 * @brief 注册线程项
 *
 * 每个线程用此宏声明一个 ThreadEntry（存放于 .user_thread 段），
 * 启动器遍历执行。
 *
 * @param fn       线程启动函数（创建线程后立即返回）
 * @param stage_   ThreadStage 枚举值
 * @param name_    组件标识，打印用
 */
#define REGISTER_THREAD(fn, stage_, name_)                                     \
    static const ThreadEntry kThreadEntry_##fn = {                             \
        fn, ThreadStage::stage_, name_                                         \
    };                                                                         \
    static const ThreadEntry* const kThreadEntryPtr_##fn                       \
        __attribute__((used, __section__(".user_thread"),                      \
                       aligned(sizeof(void*)))) = &kThreadEntry_##fn

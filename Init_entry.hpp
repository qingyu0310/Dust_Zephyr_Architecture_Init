/**
 * @file Init_entry.hpp
 * @author qingyu
 * @brief 初始化项描述：等级、优先级、函数指针
 * @version 0.1
 * @date 2026-07-20
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
    PreInit     = 0,     // 预初始化
    PreThread   = 1,     // 预初始化线程
    EarlyInit   = 2,     // 早期初始化
    EarlyThread = 3,     // 早期初始化线程
    MidInit     = 4,     // 中期初始化
    MidThread   = 5,     // 中期初始化线程
    LateInit    = 6,     // 后期初始化
    LateThread  = 7,     // 后期初始化线程
    AppInit     = 8,     // 应用初始化
    AppThread   = 9,     // 应用初始化线程
};

/**
 * @brief 初始化等级
 */
enum class InitLevel : uint8_t
{
    High = 0,
    Mid  = 1,
    Low  = 2,
};

/**
 * @brief 初始化函数原型
 * @return true=成功, false=失败
 */
using InitFunc = bool (*)();

/**
 * @brief 初始化项描述
 *
 * 每个组件声明一个 InitEntry，链接后形成初始化表，启动器遍历执行。
 */
struct InitEntry
{
    InitFunc     func;      // 初始化函数
    InitStage    stage;     // 所属阶段
    InitLevel    level;     // 高级/中级/低级
    const char*  name;      // 组件名，调试用
};

/**
 * @brief 注册初始化项
 *
 * 每个组件用此宏声明一个 InitEntry，
 * 通过链接段收集形成初始化表，启动器遍历执行。
 *
 * @param fn       初始化函数（符合 InitFunc 签名）
 * @param stage_   InitStage 枚举值（不带 InitStage::）
 * @param level_   InitLevel 枚举值（不带 InitLevel::）
 * @param name_    组件标识，打印用
 */
#define REGISTER_INIT(fn, stage_, level_, name_)                                \
    static const InitEntry kInitEntry_##fn                                      \
    __attribute__((used, __section__(".user_init"))) = {                        \
        fn, InitStage::stage_, InitLevel::level_, name_                         \
}

/**
 * @file thread_registry.hpp
 * @author qingyu
 * @brief 线程注册表 — 已启动线程登记，供 shell 命令遍历
 * @version 0.3
 * @date 2026-10-23
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstdint>

class ThreadBase;

/**
 * @brief 线程注册表
 *
 * Thread::Start 时登记，Thread::Exit 时移除，shell 的 thread 命令按名字查找/遍历。
 * 静态数组，无动态内存；表满只影响命令可见性，不影响线程启动。
 */
class thread_registry
{
public:
    static constexpr uint8_t kMaxThreads = 16;   // 可登记线程数上限

    static bool Add(ThreadBase* thread);          // 登记（重复登记视为成功，表满返回 false）
    static bool Remove(ThreadBase* thread);       // 移除（尾部元素填补空位）
    static ThreadBase* At(uint8_t index);         // 按序号取，越界返回 nullptr
    static uint8_t Count();                       // 已登记数量
    static ThreadBase* Find(const char* name);    // 按名字查找，未找到返回 nullptr
    static void Reset();                          // 清空（仅调试/重启流程）

private:
    static ThreadBase* threads_[kMaxThreads];   // 已登记线程
    static uint8_t     count_;                  // 已登记数量
};

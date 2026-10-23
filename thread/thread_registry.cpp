/**
 * @file thread_registry.cpp
 * @author qingyu
 * @brief 线程注册表的登记/移除/查找实现
 * @version 0.3
 * @date 2026-10-23
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "thread_registry.hpp"
#include "thread.hpp"
#include <cstring>

#pragma message "Compiling Init/Thread/Thread_registry"

ThreadBase* thread_registry::threads_[kMaxThreads] {};
uint8_t     thread_registry::count_ = 0;

/**
 * @brief 登记一个已启动的线程
 * @param thread 线程对象
 * @return true=登记成功；false=表已满
 */
bool thread_registry::Add(ThreadBase* thread)
{
    if (thread == nullptr) return false;

    for (uint8_t i = 0; i < count_; ++i)
    {
        if (threads_[i] == thread) {
            return true;            // 同一对象重复登记：视为成功
        }
    }

    if (count_ >= kMaxThreads) return false;

    threads_[count_++] = thread;
    return true;
}

/**
 * @brief 从注册表移除一个已退出的线程
 *
 * 用尾部元素填补空位，保持数组紧凑。
 *
 * @param thread 线程对象
 * @return true=找到并已移除
 */
bool thread_registry::Remove(ThreadBase* thread)
{
    if (thread == nullptr) return false;

    for (uint8_t i = 0; i < count_; ++i)
    {
        if (threads_[i] == thread)
        {
            threads_[i] = threads_[count_ - 1];
            threads_[--count_] = nullptr;
            return true;
        }
    }
    return false;
}

/**
 * @brief 按序号取线程
 * @param index 序号（0 起）
 * @return 线程对象；越界返回 nullptr
 */
ThreadBase* thread_registry::At(uint8_t index)
{
    return (index < count_) ? threads_[index] : nullptr;
}

/**
 * @brief 已登记线程数
 */
uint8_t thread_registry::Count()
{
    return count_;
}

/**
 * @brief 按名字查找线程
 * @param name 线程名
 * @return 线程对象；未找到返回 nullptr
 */
ThreadBase* thread_registry::Find(const char* name)
{
    if (name == nullptr) return nullptr;

    for (uint8_t i = 0; i < count_; ++i)
    {
        if (std::strcmp(threads_[i]->Name(), name) == 0) {
            return threads_[i];
        }
    }
    return nullptr;
}

/**
 * @brief 清空注册表（仅调试/重启流程使用）
 */
void thread_registry::Reset()
{
    count_ = 0;
}

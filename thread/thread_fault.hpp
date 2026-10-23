/**
 * @file thread_fault.hpp
 * @author qingyu
 * @brief 线程故障类型与默认处置函数
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
 * @brief 线程故障策略：声明本线程出故障时的影响面
 *
 * 同一个故障对不同线程的影响面不同：心跳挂了系统照跑，姿态挂了必须全停。
 * 线程在 Start 之前用 SetFaultPolicy() 声明，DefaultHandle() 据此决定处置方式。
 */
enum class ThreadFaultPolicy : uint8_t
{
    Isolate = 0,    // 影响小：收尾后退出，不拖累系统（心跳、测试等）
    Recover = 1,    // 影响大：救不回来时全杀，保留现场（姿态、遥控等）
};

/**
 * @brief Retry 钩子：Task 返回 Retry 时由线程自己就地处理
 *
 * 与退出钩子无关：本钩子回答「扛住了吗」，返回 true 表示继续下一拍。
 * 返回 false 时交给 DefaultHandle，按线程策略处置。
 */
using RetryHook = bool (*)(void* context);

/**
 * @brief 退出钩子（亡语）：线程临死前最后一次发言
 *
 * 只负责收尾与交代后事，不负责杀线程。由 Thread::Exit() 唯一触发：
 * 自杀与被其他线程处决都会执行，执行完由 Exit() 注销注册表并终止线程。
 *
 * 与启动期的 RollbackFunc 无关：那个服务初始化回滚，这个服务线程退出。
 */
using ExitHook = void (*)(void* context);

/**
 * @brief 默认故障处置函数：按线程声明的策略兜底
 *
 * 弱符号，项目可在自己编译单元里重定义同名函数整体替换。
 * Isolate=让线程自杀；Recover=全杀（内部停机，不返回）。
 *
 * @param thread 出故障的线程
 */
__attribute__((weak)) void DefaultHandle(ThreadBase* thread);

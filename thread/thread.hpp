/**
 * @file thread.hpp
 * @author qingyu
 * @brief Zephyr 线程模板，封装线程创建、暂停/恢复、受管任务外壳与运行统计
 * @version 0.7
 * @date 2026-10-20
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <zephyr/kernel.h>
#include <cstdint>
#include <cstring>
#include "log.hpp"

/**
 * @brief 线程优先级（Zephyr 数值越小优先级越高）
 */
enum class ThreadPrio : uint8_t
{
    Highest     = 1,
    VeryHigh    = 2,
    High        = 3,
    Normal      = 4,
    Low         = 5,
    VeryLow     = 6,
    Lowest      = 7,
};

/**
 * @brief 线程任务结果（代替异常）
 */
enum class TaskResult : uint8_t
{
    Continue = 0,   // 本拍正常结束：外壳等下一拍再调一次
    Retry    = 1,   // 可恢复错误：交给恢复钩子
    Fatal    = 2,   // 不可恢复错误：记录后退出
};

using TaskFunc     = TaskResult (*)(void* context);
using RecoveryHook = bool (*)(void* context);

/**
 * @brief 线程运行统计快照
 */
struct ThreadStats
{
    const char* name;               // 线程名
    uint32_t    run_count;          // 任务体进入次数
    uint32_t    retry_count;        // 可恢复错误次数
    uint32_t    fatal_count;        // 不可恢复错误次数
    uint32_t    drop_count;         // 业务丢帧/丢包计数
    uint64_t    run_ns;             // 任务体累计执行时间
    uint64_t    max_run_ns;         // 单次最长执行时间
    uint64_t    last_run_ns;        // 最近一次执行时间
    uint32_t    stack_unused;       // 栈剩余字节（未开启栈统计时为 0）
    bool        running;            // 线程是否仍在运行
    bool        paused;             // 是否被暂停
};

/**
 * @brief 线程非模板基类：供注册表与 shell 命令统一访问
 */
class ThreadBase
{
public:
    virtual ~ThreadBase() = default;

    virtual const char* Name() const = 0;
    virtual k_tid_t     Tid() const = 0;
    virtual bool        GetStats(ThreadStats& out) const = 0;
    virtual void        ResetStats() = 0;

    /**
     * @brief 设置「保留标记」：致命错误冻结线程时不暂停本线程
     * @param on true=保留（如 shell 调试通道）
     */
    virtual void SetKeepAlive(bool on) = 0;

    /**
     * @brief 是否带保留标记
     */
    virtual bool KeepAlive() const = 0;

    /**
     * @brief 暂停线程：内核不再调度，保留全部现场
     */
    virtual void Pause() = 0;

    /**
     * @brief 恢复线程：从被暂停处继续执行
     */
    virtual void Resume() = 0;

    /**
     * @brief 是否处于暂停状态
     */
    virtual bool Paused() const = 0;
};

/**
 * @brief 线程登记入口（定义在 Init_entry.cpp，供 Thread::Start 调用）
 * @param thread 已创建的线程对象
 */
void RegisterThreadInstance(ThreadBase* thread);

/**
 * @brief 线程模板，封装 k_thread 创建、暂停/恢复和受管任务外壳
 *
 * @tparam StackSize 线程栈大小
 */
template<uint32_t StackSize = 1024>
class Thread final : public ThreadBase
{
public:
    Thread() = default;

    void Start(k_thread_entry_t entry, ThreadPrio prio = ThreadPrio::Normal, void* p1 = nullptr, const char* name = nullptr)
    {
        atomic_clear(&paused_);

        name_ = name;
        k_thread_create(&thread_, stack_, K_THREAD_STACK_SIZEOF(stack_), entry, p1, nullptr, nullptr, static_cast<uint8_t>(prio), 0, K_NO_WAIT);
		
        atomic_set(&started_, 1);

        if (name != nullptr) {
            k_thread_name_set(&thread_, name);
        }

        RegisterThreadInstance(this);   // 登记到注册表，供 shell 的 thread 命令遍历
    }

    /**
     * @brief 启动周期任务：外壳负责循环、周期等待与统计
     * @param entry   任务入口（每拍调用一次）
     * @param period  两拍之间的等待时长
     * @param prio    线程优先级
     * @param context 任务上下文（每拍原样传回）
     * @param name    线程名
     */
    void Start(TaskFunc entry, k_timeout_t period, ThreadPrio prio, void* context, const char* name)
    {
        task_    = entry;
        period_  = period;
        context_ = context;

        Start(RunEntry, prio, this, name);   // 复用裸任务路径
    }

    /**
     * @brief 注册恢复钩子：任务返回 Retry 时调用
     * @param hook true=重进任务入口；false=线程退出
     */
    void SetRecoveryHook(RecoveryHook hook) { hook_ = hook; }

    /**
     * @brief 暂停线程：内核不再调度，栈与局部变量全部保留
     *
     * 被暂停的线程若持有互斥量，等待该互斥量的线程会一直阻塞；
     * 当前工程只使用短临界区，暂停是安全的。
     */
    void Pause() override
    {
        if (atomic_get(&paused_) || !atomic_get(&started_)) return;

        k_thread_suspend(&thread_);
        atomic_set(&paused_, 1);
    }

    /**
     * @brief 恢复被暂停的线程，从挂起处继续执行
     */
    void Resume() override
    {
        if (!atomic_get(&paused_)) return;

        k_thread_resume(&thread_);
        atomic_clear(&paused_);
    }

    bool Paused() const override 
	{ 
		return atomic_get(&paused_) != 0; 
	}

    const char* Name() const override 
	{ 
		return (name_ != nullptr) ? name_ : "?"; 
	}

    k_tid_t Tid() const override 
	{ 
		return const_cast<k_thread*>(&thread_); 
	}

    void SetKeepAlive(bool on) override 
	{ 
		atomic_set(&keep_alive_, on ? 1 : 0); 
	}

    bool KeepAlive() const override
	{ 
		return atomic_get(&keep_alive_) != 0;
	}

    /**
     * @brief 业务侧计数一次丢帧/丢包
     */
    void CountDrop() 
	{ 
		atomic_inc(&drop_count_); 
	}

    /**
     * @brief 清零运行统计（不清线程生命周期与暂停状态）
     */
    void ResetStats() override
    {
        atomic_clear(&run_count_);
        atomic_clear(&retry_count_);
        atomic_clear(&fatal_count_);
        atomic_clear(&drop_count_);

        run_cycles_      = 0;
        max_run_cycles_  = 0;
        last_run_cycles_ = 0;
    }

    /**
     * @brief 取运行统计快照
     * @param out 输出
     * @return true=已启动过，数据有效
     */
    bool GetStats(ThreadStats& out) const override
    {
        out.name        = Name();
        out.run_count   = static_cast<uint32_t>(atomic_get(&run_count_));
        out.retry_count = static_cast<uint32_t>(atomic_get(&retry_count_));
        out.fatal_count = static_cast<uint32_t>(atomic_get(&fatal_count_));
        out.drop_count  = static_cast<uint32_t>(atomic_get(&drop_count_));
        out.run_ns      = k_cyc_to_ns_floor64(run_cycles_);
        out.max_run_ns  = k_cyc_to_ns_floor64(max_run_cycles_);
        out.last_run_ns = k_cyc_to_ns_floor64(last_run_cycles_);
        out.running     = IsAlive();
        out.paused      = Paused();
        out.stack_unused = StackUnused();

        return atomic_get(&started_) != 0;
    }

private:
    k_thread thread_ {};
    K_KERNEL_STACK_MEMBER(stack_, StackSize);

    atomic_t     paused_          = ATOMIC_INIT(0);   // 暂停标志
    atomic_t     started_         = ATOMIC_INIT(0);   // 是否已创建
    atomic_t     keep_alive_      = ATOMIC_INIT(0);   // fatal 冻结时保留（默认冻结）

    TaskFunc     task_    		  = nullptr;   		  // 受管任务入口（裸任务为空）
    k_timeout_t  period_          = K_NO_WAIT;        // 两拍间隔
    void*        context_ 		  = nullptr;   		  // 任务上下文
    const char*  name_    		  = nullptr;   		  // 线程名（日志与命令用）
    RecoveryHook hook_    		  = nullptr;   		  // 恢复钩子（Retry 时调用）

    atomic_t     run_count_       = ATOMIC_INIT(0);   // 任务体进入次数
    atomic_t     retry_count_     = ATOMIC_INIT(0);   // 可恢复错误次数
    atomic_t     fatal_count_     = ATOMIC_INIT(0);   // 不可恢复错误次数
    atomic_t     drop_count_      = ATOMIC_INIT(0);   // 业务计数
    uint64_t     run_cycles_      = 0;                // 累计执行周期（仅工作线程写）
    uint64_t     max_run_cycles_  = 0;                // 单次最长执行周期（仅工作线程写）
    uint64_t     last_run_cycles_ = 0;                // 最近一次执行周期（仅工作线程写）

    static void RunEntry(void* p1, void* p2, void* p3)
    {
        ARG_UNUSED(p2);
        ARG_UNUSED(p3);
        static_cast<Thread*>(p1)->Run();
    }

    /**
     * @brief 受管任务外壳：循环调用任务体，采集统计并处理结果
     *
     * 任务体只写「一拍」；循环、周期等待、统计全部在这里。
     */
    void Run()
    {
        for (;;)
        {
            atomic_inc(&run_count_);
            const uint64_t begin_cycle = k_cycle_get_64();

            const TaskResult result = task_(context_);

            const uint64_t elapsed = k_cycle_get_64() - begin_cycle;
            run_cycles_ += elapsed;
            last_run_cycles_ = elapsed;

            if (elapsed > max_run_cycles_) {
                max_run_cycles_ = elapsed;
            }

            switch (result)
            {
                case TaskResult::Continue:
                    break;

                case TaskResult::Fatal:
                    atomic_inc(&fatal_count_);
                    DUST_LOG_ERR("thread %s fatal", Name());
                    return;

                case TaskResult::Retry:
                    atomic_inc(&retry_count_);
                    if (hook_ == nullptr || !hook_(context_)) {
                        return;
                    }
                    break;
            }

            if (!K_TIMEOUT_EQ(period_, K_NO_WAIT)) {
                k_sleep(period_);               // 周期等待
            }
        }
    }

    /**
     * @brief 线程是否仍在运行
     * @return true=未退出
     */
    bool IsAlive() const
    {
        char buf[32];
        const char* state = k_thread_state_str(const_cast<k_thread*>(&thread_), buf, sizeof(buf));
        return (state != nullptr) && (std::strstr(state, "dead") == nullptr) && (std::strstr(state, "exited") == nullptr);
    }

    /**
     * @brief 栈剩余字节
     * @return 剩余字节；未开启栈统计配置时返回 0
     */
    uint32_t StackUnused() const
    {
		#if defined(CONFIG_INIT_STACKS) && defined(CONFIG_THREAD_STACK_INFO)
		{
			size_t unused = 0;
			if (k_thread_stack_space_get(&thread_, &unused) == 0) {
				return static_cast<uint32_t>(unused);
			}
		}
		#else
        	(void)this;
		#endif
       	 	return 0;
    }
};

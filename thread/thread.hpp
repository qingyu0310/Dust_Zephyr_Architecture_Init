/**
 * @file thread.hpp
 * @author qingyu
 * @brief Zephyr 线程模板，封装线程创建、暂停/恢复、受管任务外壳、运行统计与终止
 * @version 0.11
 * @date 2026-10-24
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <zephyr/kernel.h>
#include <cstdint>
#include <cstring>
#include "thread_fault.hpp"     // ThreadFaultConfig / ThreadFaultPolicy / DefaultHandle
#include "thread_registry.hpp"  // 线程登记表
#include "log.hpp"              // Exit 的退出日志 / 注册表满的警告

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
    Continue = 0,   				// 本拍成功：外壳等下一拍再调一次
    Retry    = 1,   				// 需要重试：先交给 RetryHook，处理不了再交给 DefaultHandle
    Exit     = 2,   				// 需要退出：跳过 RetryHook，直接交给 DefaultHandle 按策略处置
};

using TaskFunc = TaskResult (*)(void* context);

/**
 * @brief 线程运行统计快照
 */
struct ThreadStats
{
    const char* name;               // 线程名
    uint32_t    run_count;          // 任务体进入次数
    uint32_t    retry_count;        // 可恢复错误次数
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

    /**
     * @brief 终止线程：挂起 → 执行 ExitHook → 注销注册表 → 打日志 → k_thread_abort
     *
     * 任意上下文可调：自己调即自杀（不返回），其他线程调即处决目标线程。
     */
    virtual void Exit() = 0;

    /**
     * @brief 本线程的故障策略
     */
    virtual ThreadFaultPolicy FaultPolicy() const = 0;
};

/**
 * @brief 线程模板，封装 k_thread 创建、暂停/恢复、受管任务外壳与终止
 *
 * @tparam StackSize 线程栈大小
 */
template<uint32_t StackSize = 1024>
class Thread final : public ThreadBase
{
public:
    Thread() = default;

    /**
     * @brief 启动受管任务：外壳负责循环、周期等待、统计与结果处理
     * @param entry   任务入口（每拍调用一次）
     * @param period  两拍之间的等待时长（K_NO_WAIT=不等待，节拍由任务体自己决定）
     * @param prio    线程优先级
     * @param context 任务上下文（每拍原样传回）
     * @param name    线程名
     * @param fault   故障配置（钩子 / 策略 / 保留标记），不传即全默认
     */
    void Start(TaskFunc entry, k_timeout_t period, ThreadPrio prio, void* context, const char* name, const ThreadFaultConfig& fault = {})
    {
        task_.entry   = entry;
        task_.period  = period;
        task_.context = context;

        fault_ = fault;

        atomic_clear(&run_.paused);

        task_.name = name;
        k_thread_create(&thread_, stack_, K_THREAD_STACK_SIZEOF(stack_), RunEntry, this, nullptr, nullptr, static_cast<uint8_t>(prio), 0, K_NO_WAIT);

        atomic_set(&run_.started, 1);

        if (name != nullptr) {
            k_thread_name_set(&thread_, name);
        }

        // 登记到注册表，供 shell 的 thread 命令遍历；表满只在日志里留痕，不影响线程启动
        if (!thread_registry::Add(this)) {
            DUST_LOG_WRN("[boot] thread registry full, %s not visible to shell", Name());
        }
    }

    /**
     * @brief 本线程的故障策略
     */
    ThreadFaultPolicy FaultPolicy() const override { return fault_.policy;   }

    /**
     * @brief 终止线程：挂起 → 执行 ExitHook → 注销注册表 → 打日志 → k_thread_abort
     *
     * 自己调用即自杀（内核切走，不返回）；其他线程调用即处决目标线程。
     * 他杀时先挂起目标，亡语期间它不可能再被调度，避免收尾与运行交错。
     *
     * 带保留标记（shell）的线程拒绝退出：自杀被他杀都不允许，直接返回。
     */
    void Exit() override
    {
        if (KeepAlive())                    		// 保留线程（shell）不许自杀/他杀
        {
            DUST_LOG_WRN("thread %s is reserved, exit refused", Name());
            return;
        }

        if (k_current_get() != &thread_) {
            k_thread_suspend(&thread_);     // 他杀：先冻住，亡语期间不会运行
        }

        if (fault_.exit != nullptr) {
            fault_.exit(task_.context);
        }

        (void)thread_registry::Remove(this);

        DUST_LOG_WRN("thread %s exited", Name());

        k_thread_abort(&thread_);
    }

    /**
     * @brief 暂停线程：内核不再调度，栈与局部变量全部保留
     *
     * 被暂停的线程若持有互斥量，等待该互斥量的线程会一直阻塞；
     * 当前工程只使用短临界区，暂停是安全的。
     */
    void Pause() override
    {
        if (atomic_get(&run_.paused) || !atomic_get(&run_.started)) return;

        k_thread_suspend(&thread_);
        atomic_set(&run_.paused, 1);
    }

    /**
     * @brief 恢复被暂停的线程，从挂起处继续执行
     */
    void Resume() override
    {
        if (!atomic_get(&run_.paused)) return;

        k_thread_resume(&thread_);
        atomic_clear(&run_.paused);
    }

    /**
     * @brief 是否处于暂停状态
     */
    bool Paused() const override
	{
		return atomic_get(&run_.paused) != 0;
	}

    /**
     * @brief 线程名
     */
    const char* Name() const override
	{
		return (task_.name != nullptr) ? task_.name : "?";
	}

    /**
     * @brief 线程 ID（内核线程对象指针）
     */
    k_tid_t Tid() const override
	{
		return const_cast<k_thread*>(&thread_);
	}

    /**
     * @brief 是否带保留标记
     */
    bool KeepAlive() const override { return fault_.keep_alive; }

    /**
     * @brief 业务侧计数一次丢帧/丢包
     */
    void CountDrop()
	{
		atomic_inc(&stats_.drop_count);
	}

    /**
     * @brief 清零运行统计（不清线程生命周期与暂停状态）
     */
    void ResetStats() override
    {
        atomic_clear(&stats_.run_count);
        atomic_clear(&stats_.retry_count);
        atomic_clear(&stats_.drop_count);

        stats_.run_cycles      = 0;
        stats_.max_run_cycles  = 0;
        stats_.last_run_cycles = 0;
    }

    /**
     * @brief 取运行统计快照
     * @param out 输出
     * @return true=已启动过，数据有效
     */
    bool GetStats(ThreadStats& out) const override
    {
        out.name         = Name();
        out.run_count    = static_cast<uint32_t>(atomic_get(&stats_.run_count));
        out.retry_count  = static_cast<uint32_t>(atomic_get(&stats_.retry_count));
        out.drop_count   = static_cast<uint32_t>(atomic_get(&stats_.drop_count));
        out.run_ns       = k_cyc_to_ns_floor64(stats_.run_cycles);
        out.max_run_ns   = k_cyc_to_ns_floor64(stats_.max_run_cycles);
        out.last_run_ns  = k_cyc_to_ns_floor64(stats_.last_run_cycles);
        out.running      = IsAlive();
        out.paused       = Paused();
        out.stack_unused = StackUnused();

        return atomic_get(&run_.started) != 0;
    }

private:
    k_thread thread_ {};
    K_KERNEL_STACK_MEMBER(stack_, StackSize);

    struct
    {
        atomic_t paused      = ATOMIC_INIT(0);   					// 暂停标志
        atomic_t started     = ATOMIC_INIT(0);   					// 是否已创建
    } run_;                                      					// 生命周期状态

    struct
    {
        TaskFunc    entry   = nullptr;           					// 受管任务入口
        k_timeout_t period  = K_NO_WAIT;         					// 两拍间隔
        void       *context = nullptr;           					// 任务上下文
        const char *name    = nullptr;           					// 线程名（日志与命令用）
    } task_;                                     					// 任务配置

    struct
    {
        atomic_t run_count       = ATOMIC_INIT(0);   				// 任务体进入次数
        atomic_t retry_count     = ATOMIC_INIT(0);   				// 可恢复错误次数
        atomic_t drop_count      = ATOMIC_INIT(0);   				// 业务计数
        uint64_t run_cycles      = 0;                				// 累计执行周期（仅工作线程写）
        uint64_t max_run_cycles  = 0;                				// 单次最长执行周期（仅工作线程写）
        uint64_t last_run_cycles = 0;                				// 最近一次执行周期（仅工作线程写）
    } stats_;                                    	 				// 运行统计

	ThreadFaultConfig fault_ {};                             		// 故障处置（Start 时由参数定死）

    /**
     * @brief 内核线程入口：把 p1 还原成本对象，转入受管外壳
     * @param p1 线程对象（Start 时传入 this）
     */
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
            atomic_inc(&stats_.run_count);
            const uint64_t begin_cycle = k_cycle_get_64();

            const TaskResult result = task_.entry(task_.context);

            const uint64_t elapsed = k_cycle_get_64() - begin_cycle;
            stats_.run_cycles += elapsed;
            stats_.last_run_cycles = elapsed;

            if (elapsed > stats_.max_run_cycles) {
                stats_.max_run_cycles = elapsed;
            }

            if (result == TaskResult::Retry)
            {
                atomic_inc(&stats_.retry_count);

                // 一级：Retry 钩子就地处理（钩子自己决定要不要节流打日志）
                if (fault_.retry == nullptr || !fault_.retry(task_.context))
                {
                    // 二级：钩子处理不了 → 默认处置函数直接决定生死（不返回）
                    DefaultHandle(this);
                }
            }
            else if (result == TaskResult::Exit)
            {
                DefaultHandle(this);        // 业务要求退出：跳过 RetryHook，直接按策略处置（不返回）
            }

            if (!K_TIMEOUT_EQ(task_.period, K_NO_WAIT))
			{
                k_sleep(task_.period);               // 周期等待
            }
        }
    }

    /**
     * @brief 线程是否仍在运行
     *
     * 未启动过的对象 thread_state 为 0，内核不会给出任何状态串，直接判死。
     *
     * @return true=已启动且未被 abort
     */
    bool IsAlive() const
    {
        if (atomic_get(&run_.started) == 0) return false;

        char buf[32];
        const char* state = k_thread_state_str(const_cast<k_thread*>(&thread_), buf, sizeof(buf));
        return (state != nullptr) && (std::strstr(state, "dead") == nullptr);
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

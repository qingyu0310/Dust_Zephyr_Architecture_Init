/**
 * @file boot_timing.hpp
 * @author qingyu
 * @brief 启动期耗时槽 — 每个注册项一个槽，链接段收集，无固定数组
 * @version 0.2
 * @date 2026-10-20
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstdint>

/**
 * @brief 启动期耗时槽
 *
 * 由 REGISTER_INIT / REGISTER_THREAD 各生成一个，放进 .boot_timing 段。
 * 槽数量由链接器按实际注册项决定，无需固定容量。
 */
struct BootTimingSlot
{
    const char* name;   // 项目名（字符串字面量，RAM 中只存指针）
    uint32_t    ns;     // 耗时（纳秒），启动期由启动器写入
    bool        ok;     // 是否成功，启动期由启动器写入
};

/**
 * @brief 启动期耗时记录（操作链接段内的槽）
 *
 * 槽的边界由链接脚本导出：__boot_timing_start / __boot_timing_end。
 */
class BootTiming
{
public:
    /**
     * @brief 记录一个槽的耗时
     * @param slot 槽指针（注册宏生成）
     * @param ns   耗时（纳秒）
     * @param ok   是否成功
     */
    static void Record(BootTimingSlot* slot, uint32_t ns, bool ok)
    {
        if (slot == nullptr) return;

        slot->ns = ns;
        slot->ok = ok;
    }

    /**
     * @brief 槽总数（链接段长度 / 槽大小）
     */
    static uint8_t Count();

    /**
     * @brief 按序号取槽
     * @param index 序号（0 起）
     * @return 槽指针；越界返回 nullptr
     */
    static const BootTimingSlot* At(uint8_t index);

    /**
     * @brief 全部槽的总耗时（纳秒）
     */
    static uint64_t TotalNs();
};

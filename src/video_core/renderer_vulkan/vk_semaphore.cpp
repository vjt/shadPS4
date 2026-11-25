// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <limits>
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_semaphore.h"

#include "common/assert.h"

namespace Vulkan {

constexpr u64 WAIT_TIMEOUT = std::numeric_limits<u64>::max();

Semaphore::Semaphore(const Instance& instance_) : instance{instance_} {
    const vk::StructureChain semaphore_chain = {
        vk::SemaphoreCreateInfo{},
        vk::SemaphoreTypeCreateInfo{
            .semaphoreType = vk::SemaphoreType::eTimeline,
            .initialValue = 0,
        },
    };
    auto [semaphore_result, sem] =
        instance.GetDevice().createSemaphoreUnique(semaphore_chain.get());
    ASSERT_MSG(semaphore_result == vk::Result::eSuccess, "Failed to create master semaphore: {}",
               vk::to_string(semaphore_result));
    semaphore = std::move(sem);
}

Semaphore::~Semaphore() = default;

void Semaphore::Refresh() {
    u64 this_tick{};
    u64 counter{};
    do {
        this_tick = gpu_tick.load(std::memory_order_acquire);
        auto [counter_result, cntr] = instance.GetDevice().getSemaphoreCounterValue(*semaphore);
        ASSERT_MSG(counter_result == vk::Result::eSuccess,
                   "Failed to get master semaphore value: {}", vk::to_string(counter_result));
        counter = cntr;
        if (counter < this_tick) {
            return;
        }
    } while (!gpu_tick.compare_exchange_weak(this_tick, counter, std::memory_order_release,
                                             std::memory_order_relaxed));
}

void Semaphore::Wait(u64 tick, const char* who) {
    // No need to wait if the GPU is ahead of the tick
    if (IsFree(tick)) {
        return;
    }
    // Update the GPU tick and try again
    Refresh();
    if (IsFree(tick)) {
        return;
    }

    // If none of the above is hit, fallback to a regular wait
    const vk::SemaphoreWaitInfo wait_info = {
        .semaphoreCount = 1,
        .pSemaphores = &semaphore.get(),
        .pValues = &tick,
    };

    const auto t0 = std::chrono::steady_clock::now();
    while (instance.GetDevice().waitSemaphores(&wait_info, WAIT_TIMEOUT) != vk::Result::eSuccess) {
    }
    const auto t1 = std::chrono::steady_clock::now();
    struct Slot {
        const char* who;
        u64 waits, ns;
    };
    static std::mutex m;
    static Slot slots[32]{};
    static s64 last_print{};
    {
        std::scoped_lock lk{m};
        for (auto& sl : slots) {
            if (!sl.who || !strcmp(sl.who, who)) {
                sl.who = who;
                ++sl.waits;
                sl.ns += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
                break;
            }
        }
        const s64 now_s =
            std::chrono::duration_cast<std::chrono::seconds>(t1.time_since_epoch()).count();
        if (now_s >= last_print + 5) {
            last_print = now_s;
            for (const auto& sl : slots) {
                if (sl.who) {
                    fprintf(stderr, "WAITSTAT who=%s waits=%llu wait_ms=%llu\n", sl.who,
                            (unsigned long long)sl.waits, (unsigned long long)(sl.ns / 1000000));
                }
            }
        }
    }
    Refresh();
}

} // namespace Vulkan

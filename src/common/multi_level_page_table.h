// SPDX-FileCopyrightText: 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <cstdio>
#include <vector>
#include "common/object_pool.h"
#include "common/spin_lock.h"

namespace Common {

template <class Traits>
class MultiLevelPageTable final {
    using Entry = typename Traits::Entry;

    static constexpr size_t L1_BITS = Traits::L1_BITS;
    static constexpr size_t L2_BITS = Traits::ADDRESS_SPACE_BITS - L1_BITS - Traits::PAGE_BITS;
    static constexpr size_t NUM_L1_ENTRIES = 1ULL << L2_BITS;
    static constexpr bool NULL_CHECK = Traits::NULL_CHECK;

    using L1Page = std::array<Entry, NUM_L1_ENTRIES>;

public:
    explicit MultiLevelPageTable(size_t chunk_size = 64)
        : top_level{1ULL << L1_BITS, nullptr}, page_alloc{chunk_size} {}

    ~MultiLevelPageTable() noexcept = default;

    [[nodiscard]] Entry* find(size_t page) {
        const size_t l1_page = page >> L2_BITS;
        const size_t l2_page = page & (NUM_L1_ENTRIES - 1);
        if (!top_level[l1_page]) [[unlikely]] {
            return nullptr;
        }
        return &(*top_level[l1_page])[l2_page];
    }

    [[nodiscard]] const Entry* find(size_t page) const {
        const size_t l1_page = page >> L2_BITS;
        const size_t l2_page = page & (NUM_L1_ENTRIES - 1);
        if (!top_level[l1_page]) [[unlikely]] {
            return nullptr;
        }
        return &(*top_level[l1_page])[l2_page];
    }

    void reserve(size_t start_page, size_t end_page) {
        const size_t start_l1_page = start_page >> L2_BITS;
        const size_t end_l1_page = end_page >> L2_BITS;
        for (size_t l1_page = start_l1_page; l1_page <= end_l1_page; ++l1_page) {
            if (!top_level[l1_page]) {
                top_level[l1_page] = DbgCreate(l1_page, "reserve");
            }
        }
    }

    [[nodiscard]] const Entry& operator[](size_t page) const {
        const size_t l1_page = page >> L2_BITS;
        const size_t l2_page = page & (NUM_L1_ENTRIES - 1);
        if (NULL_CHECK && !top_level[l1_page]) [[unlikely]] {
            top_lock.lock();
            if (!top_level[l1_page]) {
                top_level[l1_page] = page_alloc.Create();
            }
            top_lock.unlock();
        }
        return (*top_level[l1_page])[l2_page];
    }

    [[nodiscard]] Entry& operator[](size_t page) {
        const size_t l1_page = page >> L2_BITS;
        const size_t l2_page = page & (NUM_L1_ENTRIES - 1);
        if (NULL_CHECK && !top_level[l1_page]) [[unlikely]] {
            top_lock.lock();
            if (!top_level[l1_page]) {
                top_level[l1_page] = DbgCreate(l1_page, "operator[]");
            }
            top_lock.unlock();
        }
        return (*top_level[l1_page])[l2_page];
    }

private:
    L1Page* DbgCreate(size_t l1_page, const char* who) {
        const int others = dbg_creating.fetch_add(1);
        L1Page* page = page_alloc.Create();
        const bool filled = top_level[l1_page] != nullptr;
        dbg_creating.fetch_sub(1);
        const int n = dbg_creates.fetch_add(1) + 1;
        if (others != 0 || filled) {
            std::fprintf(stderr, "RESERVERACE who=%s l1=%zu concurrent_creates=%d slot_filled=%d\n",
                         who, l1_page, others, int(filled));
        } else if ((n & (n - 1)) == 0) {
            std::fprintf(stderr, "RESERVESTAT creates=%d last=%s\n", n, who);
        }
        return page;
    }

    std::atomic<int> dbg_creating{0};
    std::atomic<int> dbg_creates{0};
    std::vector<L1Page*> top_level{};
    mutable Common::SpinLock top_lock{};
    Common::ObjectPool<L1Page> page_alloc;
};

} // namespace Common

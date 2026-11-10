// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <boost/container/static_vector.hpp>

#include <dynarmic/common/spin_lock.h>

namespace Dynarmic {

using VAddr = std::uint64_t;
using Vector = std::array<std::uint64_t, 2>;

class ExclusiveMonitor {
public:
    /// @param processor_count Maximum number of processors using this global
    ///                        exclusive monitor. Each processor must have a
    ///                        unique id.
    explicit ExclusiveMonitor(size_t processor_count);

    size_t GetProcessorCount() const;

    /// Marks a region containing [address, address+size) to be exclusive to
    /// processor processor_id.
    template<typename T, typename Function>
    T ReadAndMark(size_t processor_id, VAddr address, Function op) {
        static_assert(std::is_trivially_copyable_v<T>);
        const VAddr masked_address = address & RESERVATION_GRANULE_MASK;

        const bool serialized = Serialized();
        if (serialized) {
            lock.Lock();
        }
        SpinLock& stripe = StripeLock(masked_address);
        stripe.Lock();
        std::atomic_ref<VAddr>{exclusive_addresses[processor_id]}.store(masked_address, std::memory_order_relaxed);
        const T value = op();
        std::memcpy(exclusive_values[processor_id].data(), &value, sizeof(T));
        stripe.Unlock();
        if (serialized) {
            lock.Unlock();
        }
        return value;
    }

    /// Checks to see if processor processor_id has exclusive access to the
    /// specified region. If it does, executes the operation then clears
    /// the exclusive state for processors if their exclusive region(s)
    /// contain [address, address+size).
    template<typename T, typename Function>
        requires std::is_trivially_copyable_v<T>
    bool DoExclusiveOperation(size_t processor_id, VAddr address, Function op) {
        const bool serialized = Serialized();
        if (serialized) {
            lock.Lock();
        }
        if (!CheckAndClear(processor_id, address)) {
            if (serialized) {
                lock.Unlock();
            }
            return false;
        }

        T saved_value;
        std::memcpy(&saved_value, exclusive_values[processor_id].data(), sizeof(T));
        const bool result = op(saved_value);

        StripeLock(address & RESERVATION_GRANULE_MASK).Unlock();
        if (serialized) {
            lock.Unlock();
        }
        return result;
    }

    /// All exclusive operations also take one lock, as before the per-address locks (the
    /// embedder sets it, e.g. while a game loads: OoT corrupted its memory loading an area with
    /// its cores' atomics running concurrently). Taken on top of the address's lock, so switching
    /// while operations run is safe.
    static void SetSerialized(bool value) {
        serialized_flag().store(value, std::memory_order_relaxed);
    }
    static bool Serialized() {
        return serialized_flag().load(std::memory_order_relaxed);
    }

    /// ReadAndMark without the lock or the other processors: with
    /// OptimizationFlag::Unsafe_IgnoreGlobalMonitor, a processor's reservation is only its own
    /// address and value, and the exclusive store is a compare-and-swap against that value.
    template<typename T, typename Function>
    T ReadAndMarkLocal(size_t processor_id, VAddr address, Function op) {
        static_assert(std::is_trivially_copyable_v<T>);
        const T value = op();
        std::memcpy(exclusive_values[processor_id].data(), &value, sizeof(T));
        exclusive_addresses[processor_id] = address & RESERVATION_GRANULE_MASK;
        return value;
    }

    /// DoExclusiveOperation's counterpart of ReadAndMarkLocal.
    template<typename T, typename Function>
        requires std::is_trivially_copyable_v<T>
    bool DoExclusiveOperationLocal(size_t processor_id, VAddr address, Function op) {
        if (exclusive_addresses[processor_id] != (address & RESERVATION_GRANULE_MASK)) {
            return false;
        }
        exclusive_addresses[processor_id] = INVALID_EXCLUSIVE_ADDRESS;
        T saved_value;
        std::memcpy(&saved_value, exclusive_values[processor_id].data(), sizeof(T));
        return op(saved_value);
    }

    /// Unmark everything.
    void Clear();
    /// Unmark processor id
    void ClearProcessor(size_t processor_id);

private:
    bool CheckAndClear(size_t processor_id, VAddr address);

    void Lock();
    void Unlock();

    friend volatile int* GetExclusiveMonitorLockPointer(ExclusiveMonitor*);
    friend size_t GetExclusiveMonitorProcessorCount(ExclusiveMonitor*);
    friend VAddr* GetExclusiveMonitorAddressPointer(ExclusiveMonitor*, size_t index);
    friend Vector* GetExclusiveMonitorValuePointer(ExclusiveMonitor*, size_t index);

    // Operations on one address serialize on one of these (chosen by address), not on one lock
    // for everything: with six cores the single lock was spun on ~39% of the time. Same semantics:
    // an exclusive store clears the other processors' reservations of that address under its lock.
    static std::atomic<bool>& serialized_flag() {
        static std::atomic<bool> flag{false};
        return flag;
    }
    static constexpr size_t STRIPE_COUNT = 64;
    SpinLock& StripeLock(VAddr masked_address) {
        return stripes[((masked_address >> 3) * 0x9E3779B97F4A7C15ull) >> 58];
    }
    std::array<SpinLock, STRIPE_COUNT> stripes{};

    static constexpr VAddr RESERVATION_GRANULE_MASK = 0xFFFF'FFFF'FFFF'FFFFull;
    static constexpr VAddr INVALID_EXCLUSIVE_ADDRESS = 0xDEAD'DEAD'DEAD'DEADull;
    static constexpr size_t MAX_NUM_CPU_CORES = 6; // Sync with src/core/hardware_properties
    boost::container::static_vector<VAddr, MAX_NUM_CPU_CORES> exclusive_addresses;
    boost::container::static_vector<Vector, MAX_NUM_CPU_CORES> exclusive_values;
    SpinLock lock;
};

}  // namespace Dynarmic

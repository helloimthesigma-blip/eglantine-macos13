// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include "dynarmic/interface/exclusive_monitor.h"

#include <algorithm>

#include "common/assert.h"

namespace Dynarmic {

ExclusiveMonitor::ExclusiveMonitor(std::size_t processor_count)
        : exclusive_addresses(processor_count, INVALID_EXCLUSIVE_ADDRESS), exclusive_values(processor_count) {}

size_t ExclusiveMonitor::GetProcessorCount() const {
    return exclusive_addresses.size();
}

void ExclusiveMonitor::Lock() {
    lock.Lock();
}

void ExclusiveMonitor::Unlock() {
    lock.Unlock();
}

bool ExclusiveMonitor::CheckAndClear(std::size_t processor_id, VAddr address) {
    const VAddr masked_address = address & RESERVATION_GRANULE_MASK;

    SpinLock& stripe = StripeLock(masked_address);
    stripe.Lock();
    if (std::atomic_ref<VAddr>{exclusive_addresses[processor_id]}.load(std::memory_order_relaxed) != masked_address) {
        stripe.Unlock();
        return false;
    }

    // Other processors' reservations of this address are only set under this stripe's lock, so
    // clearing them here can't lose a newer one; a processor that moved on to another address
    // meanwhile keeps it (compare and swap).
    for (VAddr& other_address : exclusive_addresses) {
        VAddr expected = masked_address;
        std::atomic_ref<VAddr>{other_address}.compare_exchange_strong(expected, INVALID_EXCLUSIVE_ADDRESS, std::memory_order_relaxed);
    }
    return true;
}

void ExclusiveMonitor::Clear() {
    for (VAddr& address : exclusive_addresses) {
        std::atomic_ref<VAddr>{address}.store(INVALID_EXCLUSIVE_ADDRESS, std::memory_order_relaxed);
    }
}

void ExclusiveMonitor::ClearProcessor(size_t processor_id) {
    std::atomic_ref<VAddr>{exclusive_addresses[processor_id]}.store(INVALID_EXCLUSIVE_ADDRESS, std::memory_order_relaxed);
}

}  // namespace Dynarmic

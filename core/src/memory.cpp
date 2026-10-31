#include "fk/memory.hpp"

#include <cstring>
#include <new>

#if defined(_WIN32)
#include <windows.h>

// psapi.h needs the declarations of windows.h (keep it in its own include block)
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#else
#include <cstdio>
#include <fstream>
#include <string>
#endif

namespace fk {

const char* memCategoryName(MemCategory c) {
    switch (c) {
    case MemCategory::proxy:
        return "proxy";
    case MemCategory::decode_cache:
        return "decode_cache";
    case MemCategory::band:
        return "band";
    case MemCategory::region:
        return "region";
    case MemCategory::image:
        return "image";
    case MemCategory::display:
        return "display";
    case MemCategory::other:
        return "other";
    case MemCategory::count_:
        break;
    }
    return "unknown";
}

BufferAccounting& BufferAccounting::instance() {
    static BufferAccounting inst;
    return inst;
}

bool BufferAccounting::tryReserve(MemCategory c, uint64_t bytes) {
    const uint64_t lim = limit_.load(std::memory_order_relaxed);
    uint64_t cur = total_.load(std::memory_order_relaxed);
    for (;;) {
        const uint64_t next = cur + bytes;
        if (lim != 0 && next > lim) return false;
        if (total_.compare_exchange_weak(cur, next, std::memory_order_relaxed)) {
            byCat_[static_cast<size_t>(c)].fetch_add(bytes, std::memory_order_relaxed);
            uint64_t pk = peak_.load(std::memory_order_relaxed);
            while (next > pk && !peak_.compare_exchange_weak(pk, next, std::memory_order_relaxed)) {
            }
            return true;
        }
    }
}

void BufferAccounting::forceReserve(MemCategory c, uint64_t bytes) {
    const uint64_t next = total_.fetch_add(bytes, std::memory_order_relaxed) + bytes;
    byCat_[static_cast<size_t>(c)].fetch_add(bytes, std::memory_order_relaxed);
    uint64_t pk = peak_.load(std::memory_order_relaxed);
    while (next > pk && !peak_.compare_exchange_weak(pk, next, std::memory_order_relaxed)) {
    }
}

void BufferAccounting::release(MemCategory c, uint64_t bytes) {
    total_.fetch_sub(bytes, std::memory_order_relaxed);
    byCat_[static_cast<size_t>(c)].fetch_sub(bytes, std::memory_order_relaxed);
}

uint64_t BufferAccounting::current() const {
    return total_.load(std::memory_order_relaxed);
}
uint64_t BufferAccounting::current(MemCategory c) const {
    return byCat_[static_cast<size_t>(c)].load(std::memory_order_relaxed);
}
uint64_t BufferAccounting::peak() const {
    return peak_.load(std::memory_order_relaxed);
}
void BufferAccounting::resetPeak() {
    peak_.store(total_.load(std::memory_order_relaxed), std::memory_order_relaxed);
}
void BufferAccounting::setLimit(uint64_t bytes) {
    limit_.store(bytes, std::memory_order_relaxed);
}
uint64_t BufferAccounting::limit() const {
    return limit_.load(std::memory_order_relaxed);
}

Result<AccountedBuffer> AccountedBuffer::allocate(MemCategory c, size_t bytes, bool zeroFill) {
    AccountedBuffer b;
    if (bytes == 0) return b;
    if (!BufferAccounting::instance().tryReserve(c, bytes)) {
        return makeError(Errc::memory_budget, "Not enough memory budget for this operation",
                         std::string("category=") + memCategoryName(c) + " bytes=" + std::to_string(bytes));
    }
    uint8_t* p = new (std::nothrow) uint8_t[bytes];
    if (!p) {
        BufferAccounting::instance().release(c, bytes);
        return makeError(Errc::memory_budget, "Out of memory", "bytes=" + std::to_string(bytes));
    }
    if (zeroFill) std::memset(p, 0, bytes);
    b.data_.reset(p);
    b.size_ = bytes;
    b.cat_ = c;
    return b;
}

AccountedBuffer::~AccountedBuffer() {
    reset();
}

AccountedBuffer::AccountedBuffer(AccountedBuffer&& o) noexcept
    : data_(std::move(o.data_)), size_(o.size_), cat_(o.cat_) {
    o.size_ = 0;
}

AccountedBuffer& AccountedBuffer::operator=(AccountedBuffer&& o) noexcept {
    if (this != &o) {
        reset();
        data_ = std::move(o.data_);
        size_ = o.size_;
        cat_ = o.cat_;
        o.size_ = 0;
    }
    return *this;
}

void AccountedBuffer::reset() {
    if (size_) BufferAccounting::instance().release(cat_, size_);
    data_.reset();
    size_ = 0;
}

std::shared_ptr<void> accountBytes(MemCategory c, uint64_t bytes) {
    BufferAccounting::instance().forceReserve(c, bytes);
    return std::shared_ptr<void>(nullptr,
                                 [c, bytes](void*) { BufferAccounting::instance().release(c, bytes); });
}

MemoryStats currentMemoryStats() {
    MemoryStats s;
    s.accountedBytes = BufferAccounting::instance().current();
    s.accountedPeakBytes = BufferAccounting::instance().peak();
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                             sizeof(pmc))) {
        s.residentBytes = pmc.WorkingSetSize;
        s.peakResidentBytes = pmc.PeakWorkingSetSize;
        s.privateBytes = pmc.PrivateUsage;
    }
#elif defined(__APPLE__)
    task_vm_info_data_t vm{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&vm), &count) ==
        KERN_SUCCESS) {
        s.residentBytes = vm.resident_size;
        s.privateBytes = vm.phys_footprint;
    }
    rusage ru{};
    if (getrusage(RUSAGE_SELF, &ru) == 0)
        s.peakResidentBytes = static_cast<uint64_t>(ru.ru_maxrss); // bytes on macOS
#else
    std::ifstream f("/proc/self/status");
    std::string key;
    while (f >> key) {
        uint64_t kb = 0;
        if (key == "VmRSS:") {
            f >> kb;
            s.residentBytes = kb * 1024;
        } else if (key == "VmHWM:") {
            f >> kb;
            s.peakResidentBytes = kb * 1024;
        } else if (key == "RssAnon:") {
            f >> kb;
            s.privateBytes = kb * 1024;
        }
        f.ignore(256, '\n');
    }
#endif
    return s;
}

SystemMemory systemMemory() {
    SystemMemory m;
#if defined(_WIN32)
    MEMORYSTATUSEX st{};
    st.dwLength = sizeof(st);
    if (GlobalMemoryStatusEx(&st)) {
        m.totalBytes = st.ullTotalPhys;
        m.availableBytes = st.ullAvailPhys;
    }
#elif defined(__APPLE__)
    uint64_t total = 0;
    size_t len = sizeof(total);
    if (sysctlbyname("hw.memsize", &total, &len, nullptr, 0) == 0) m.totalBytes = total;
    vm_statistics64_data_t vm{};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vm), &count) ==
        KERN_SUCCESS)
        m.availableBytes = (static_cast<uint64_t>(vm.free_count) + vm.inactive_count + vm.purgeable_count) *
                           static_cast<uint64_t>(vm_page_size);
#else
    std::ifstream f("/proc/meminfo");
    std::string key;
    while (f >> key) {
        uint64_t kb = 0;
        if (key == "MemTotal:") {
            f >> kb;
            m.totalBytes = kb * 1024;
        } else if (key == "MemAvailable:") {
            f >> kb;
            m.availableBytes = kb * 1024;
        }
        f.ignore(256, '\n');
    }
#endif
    return m;
}

} // namespace fk

#include "MemoryPressure.hpp"

#include "fk/memory.hpp"

#include <QDateTime>

#include <algorithm>

#if defined(Q_OS_MACOS)
#include <dispatch/dispatch.h>
#elif defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fkapp {

namespace {
[[maybe_unused]] constexpr int kPollMs = 2000;
constexpr qint64 kRepeatMs = 30000;
} // namespace

MemoryPressure::MemoryPressure(QObject* parent) : QObject(parent) {
#if defined(Q_OS_MACOS)
    // the main queue is serviced by the Cocoa event loop (the UI thread): the handler runs there
    dispatch_source_t src = dispatch_source_create(
        DISPATCH_SOURCE_TYPE_MEMORYPRESSURE, 0,
        DISPATCH_MEMORYPRESSURE_WARN | DISPATCH_MEMORYPRESSURE_CRITICAL, dispatch_get_main_queue());
    if (src) {
        native_ = src;
        dispatch_set_context(src, this);
        dispatch_source_set_event_handler_f(src, [](void* ctx) {
            auto* self = static_cast<MemoryPressure*>(ctx);
            const auto level = dispatch_source_get_data(static_cast<dispatch_source_t>(self->native_));
            self->onLow((level & (DISPATCH_MEMORYPRESSURE_WARN | DISPATCH_MEMORYPRESSURE_CRITICAL)) != 0);
        });
        dispatch_resume(src);
    }
#else
#if defined(Q_OS_WIN)
    native_ = CreateMemoryResourceNotification(LowMemoryResourceNotification);
#endif
    connect(&timer_, &QTimer::timeout, this, &MemoryPressure::poll);
    timer_.start(kPollMs);
#endif
}

MemoryPressure::~MemoryPressure() {
#if defined(Q_OS_MACOS)
    if (native_) {
        auto src = static_cast<dispatch_source_t>(native_);
        dispatch_source_cancel(src);
        dispatch_release(src);
    }
#elif defined(Q_OS_WIN)
    if (native_) CloseHandle(static_cast<HANDLE>(native_));
#endif
}

void MemoryPressure::poll() {
#if defined(Q_OS_WIN)
    BOOL low = FALSE;
    if (native_ && QueryMemoryResourceNotification(static_cast<HANDLE>(native_), &low)) onLow(low != FALSE);
#elif !defined(Q_OS_MACOS)
    const fk::SystemMemory m = fk::systemMemory();
    if (m.totalBytes == 0) return;
    const uint64_t floor = std::max<uint64_t>(m.totalBytes / 20, 256ull << 20);
    onLow(m.availableBytes < floor);
#endif
}

void MemoryPressure::onLow(bool low) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (low && (!low_ || now - lastEmit_ >= kRepeatMs)) {
        lastEmit_ = now;
        Q_EMIT pressure();
    }
    low_ = low;
}

} // namespace fkapp

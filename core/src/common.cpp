#include "fk/common.hpp"

namespace fk {
const char* errcName(Errc code) {
    switch (code) {
    case Errc::ok:
        return "ok";
    case Errc::invalid_argument:
        return "invalid_argument";
    case Errc::io_open:
        return "io_open";
    case Errc::io_read:
        return "io_read";
    case Errc::io_write:
        return "io_write";
    case Errc::disk_full:
        return "disk_full";
    case Errc::exists:
        return "exists";
    case Errc::not_found:
        return "not_found";
    case Errc::unsupported_format:
        return "unsupported_format";
    case Errc::damaged_file:
        return "damaged_file";
    case Errc::color_profile:
        return "color_profile";
    case Errc::cancelled:
        return "cancelled";
    case Errc::memory_budget:
        return "memory_budget";
    case Errc::capability:
        return "capability";
    case Errc::needs_review:
        return "needs_review";
    case Errc::needs_confirmation:
        return "needs_confirmation";
    case Errc::conflict:
        return "conflict";
    case Errc::version_too_new:
        return "version_too_new";
    case Errc::parse_error:
        return "parse_error";
    case Errc::internal:
        return "internal";
    }
    return "unknown";
}
} // namespace fk

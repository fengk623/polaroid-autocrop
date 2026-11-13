#include "Display.hpp"

#include <QCoreApplication>
#include <QScreen>
#include <QtGlobal>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <vector>

#ifdef _WIN32
#include <QtGui/qscreen_platform.h>

#include <windows.h>

#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#endif

namespace fkapp {

namespace {
#ifdef _WIN32
/// The ICC file Windows associates with the monitor of `screen` (empty: none, or the screen is unknown).
std::wstring monitorProfilePath(QScreen* screen) {
    HDC dc = nullptr;
    bool created = false;
    if (screen)
        if (auto* ws = screen->nativeInterface<QNativeInterface::QWindowsScreen>()) {
            MONITORINFOEXW mi{};
            mi.cbSize = sizeof(mi);
            if (GetMonitorInfoW(ws->handle(), &mi)) {
                dc = CreateDCW(L"DISPLAY", mi.szDevice, nullptr, nullptr);
                created = dc != nullptr;
            }
        }
    if (!dc) dc = GetDC(nullptr); // the primary monitor
    if (!dc) return {};
    std::wstring path;
    DWORD n = 0;
    GetICMProfileW(dc, &n, nullptr);
    if (n > 0 && n < 32768) {
        path.assign(n, L'\0');
        if (GetICMProfileW(dc, &n, path.data()))
            path.resize(std::wcslen(path.c_str()));
        else
            path.clear();
    }
    if (created)
        DeleteDC(dc);
    else
        ReleaseDC(nullptr, dc);
    return path;
}
#endif

[[maybe_unused]] QString tr(const char* text) {
    return QCoreApplication::translate("Display", text);
}

std::mutex& profileMutex() {
    static std::mutex m;
    return m;
}
DisplayProfileInfo& currentProfile() {
    static DisplayProfileInfo p;
    return p;
}
} // namespace

DisplayProfileInfo readDisplayProfile([[maybe_unused]] QScreen* screen) {
    DisplayProfileInfo srgb;
#ifdef _WIN32
    if (qEnvironmentVariableIntValue("FK_DISPLAY_SRGB") == 1) {
        srgb.source = tr("sRGB (FK_DISPLAY_SRGB=1)");
        return srgb;
    }
    const std::wstring path = monitorProfilePath(screen);
    if (path.empty()) {
        srgb.source = tr("sRGB (no monitor profile)");
        return srgb;
    }
    // parse the file only when it changed (the window may move between monitors often)
    static std::mutex cacheMutex;
    static std::map<std::wstring,
                    std::pair<std::pair<uintmax_t, std::filesystem::file_time_type>, DisplayProfileInfo>>
        cache;
    std::error_code ec;
    const std::filesystem::path fp(path);
    const auto stamp =
        std::make_pair(std::filesystem::file_size(fp, ec), std::filesystem::last_write_time(fp, ec));
    {
        std::lock_guard<std::mutex> lk(cacheMutex);
        if (auto f = cache.find(path); f != cache.end() && f->second.first == stamp) return f->second.second;
    }
    std::ifstream f(fp, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    DisplayProfileInfo out = srgb;
    out.source = tr("sRGB (monitor profile unusable: %1)").arg(QString::fromStdWString(path));
    if (bytes.size() >= 132 && bytes.size() <= (64u << 20)) {
        auto p = fk::Profile::fromIcc(bytes);
        // only RGB output profiles that lcms can actually target
        if (p && p->isRgb() &&
            fk::Transform::create(fk::Profile::sRGB(), {3, 8, false}, *p, {3, 8, false},
                                  fk::Intent::relative_colorimetric, true)) {
            out.profile = *p;
            out.id = QString::fromStdString(p->sha256());
            out.description = QString::fromStdString(p->description());
            out.source = tr("monitor profile %1").arg(QString::fromStdWString(path));
        }
    }
    std::lock_guard<std::mutex> lk(cacheMutex);
    cache[path] = {stamp, out};
    return out;
#elif defined(__APPLE__)
    srgb.source = tr("sRGB (the window is sRGB-tagged; macOS converts to the display)");
    return srgb;
#else
    srgb.source = QStringLiteral("sRGB");
    return srgb;
#endif
}

DisplayProfileInfo currentDisplayProfile() {
    std::lock_guard<std::mutex> lk(profileMutex());
    return currentProfile();
}

bool setCurrentDisplayProfile(const DisplayProfileInfo& p) {
    std::lock_guard<std::mutex> lk(profileMutex());
    const bool changed = currentProfile().id != p.id;
    currentProfile() = p;
    return changed;
}

void convertDisplayImage(QImage& img, const fk::Profile& from, const fk::Profile& to) {
    if (img.isNull() || from.sha256() == to.sha256()) return;
    auto t = fk::Transform::create(from, {3, 8, false}, to, {3, 8, false}, fk::Intent::relative_colorimetric,
                                   true);
    if (!t) return;
    if (img.format() != QImage::Format_RGB32) img = img.convertToFormat(QImage::Format_RGB32);
    std::vector<uint8_t> in(static_cast<size_t>(img.width()) * 3), out(in.size());
    for (int y = 0; y < img.height(); ++y) {
        auto* row = reinterpret_cast<uint32_t*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            in[static_cast<size_t>(x) * 3] = static_cast<uint8_t>((row[x] >> 16) & 0xFF);
            in[static_cast<size_t>(x) * 3 + 1] = static_cast<uint8_t>((row[x] >> 8) & 0xFF);
            in[static_cast<size_t>(x) * 3 + 2] = static_cast<uint8_t>(row[x] & 0xFF);
        }
        t->apply(in.data(), out.data(), static_cast<size_t>(img.width()));
        for (int x = 0; x < img.width(); ++x)
            row[x] = 0xFF000000u | (uint32_t(out[static_cast<size_t>(x) * 3]) << 16) |
                     (uint32_t(out[static_cast<size_t>(x) * 3 + 1]) << 8) |
                     out[static_cast<size_t>(x) * 3 + 2];
    }
}

bool DisplayConverter::init(const fk::ImageInfo& info, const DisplayProfileInfo* to) {
    info_ = info;
    const DisplayProfileInfo target = to ? *to : currentDisplayProfile();
    profileId_ = target.id;
    gray_ = info.channels <= 2;
    colorCh_ = gray_ ? 1 : 3;
    fk::Profile src = gray_ ? fk::Profile::gray22() : fk::Profile::sRGB();
    if (!info.icc.empty()) {
        auto p = fk::Profile::fromIcc(info.icc);
        if (p && ((gray_ && p->isGray()) || (!gray_ && p->isRgb()))) src = *p;
    }
    const bool alpha = info.channels == 2 || info.channels == 4;
    const fk::Profile& dst = target.profile;
    auto t1 = fk::Transform::create(src, {colorCh_, info.bitsPerSample, alpha}, dst, {3, 8, false},
                                    fk::Intent::relative_colorimetric, true);
    if (!t1) return false;
    rowTransform_ = *t1;
    auto t2 = fk::Transform::create(src, {colorCh_, 32, false}, dst, {3, 8, false},
                                    fk::Intent::relative_colorimetric, true);
    if (!t2) return false;
    proxyTransform_ = *t2;
    return true;
}

void DisplayConverter::convertRow(const uint8_t* src, int64_t w, uint32_t* dst) const {
    // rows come from ImageSource::readRows: MinIsWhite is already in normal polarity (FK-011)
    std::vector<uint8_t> rgb(static_cast<size_t>(w) * 3);
    rowTransform_.apply(src, rgb.data(), static_cast<size_t>(w));
    for (int64_t x = 0; x < w; ++x)
        dst[x] =
            0xFF000000u | (uint32_t(rgb[x * 3]) << 16) | (uint32_t(rgb[x * 3 + 1]) << 8) | rgb[x * 3 + 2];
}

QImage DisplayConverter::convertProxy(const fk::Proxy& p) const {
    QImage img(static_cast<int>(p.width), static_cast<int>(p.height), QImage::Format_RGB32);
    std::vector<float> in(static_cast<size_t>(p.width) * colorCh_);
    std::vector<uint8_t> rgb(static_cast<size_t>(p.width) * 3);
    for (int64_t y = 0; y < p.height; ++y) {
        const float* row = p.px(0, y);
        if (gray_) {
            for (int64_t x = 0; x < p.width; ++x)
                in[x] = row[x * 3];
        } else {
            std::memcpy(in.data(), row, in.size() * sizeof(float));
        }
        proxyTransform_.apply(in.data(), rgb.data(), static_cast<size_t>(p.width));
        auto* dst = reinterpret_cast<uint32_t*>(img.scanLine(static_cast<int>(y)));
        for (int64_t x = 0; x < p.width; ++x)
            dst[x] =
                0xFF000000u | (uint32_t(rgb[x * 3]) << 16) | (uint32_t(rgb[x * 3 + 1]) << 8) | rgb[x * 3 + 2];
    }
    return img;
}

QImage boxDownscale(const QImage& src, int longSide) {
    if (src.isNull()) return {};
    const int L = std::max(src.width(), src.height());
    if (L <= longSide) return src;
    return src.scaled(src.width() * longSide / L, src.height() * longSide / L, Qt::KeepAspectRatio,
                      Qt::SmoothTransformation);
}

} // namespace fkapp

#include "ShiftTap.hpp"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QQuickItem>
#include <QQuickWindow>

namespace fkapp {

ShiftTap::ShiftTap(QObject* parent) : QObject(parent) {}

QObject* ShiftTap::window() const {
    return window_.data();
}

void ShiftTap::setWindow(QObject* w) {
    auto* win = qobject_cast<QWindow*>(w);
    if (win == window_) return;
    window_ = win;
    armed_ = false;
    // application-wide: a shortcut (Shift+→ …) consumes its key press before the window sees it, but the
    // shortcut-override query and the shortcut event still pass through here
    if (window_)
        QCoreApplication::instance()->installEventFilter(this);
    else
        QCoreApplication::instance()->removeEventFilter(this);
    Q_EMIT windowChanged();
}

void ShiftTap::setEnabled(bool v) {
    if (v == enabled_) return;
    enabled_ = v;
    armed_ = false;
    Q_EMIT enabledChanged();
}

void ShiftTap::setMaxHoldMs(int ms) {
    if (ms == maxHoldMs_) return;
    maxHoldMs_ = ms;
    Q_EMIT enabledChanged();
}

bool ShiftTap::eventFilter(QObject* watched, QEvent* e) {
    if (!window_) return false;
    switch (e->type()) {
    case QEvent::Shortcut: // any shortcut fired while Shift is down
    case QEvent::ApplicationStateChange:
        armed_ = false;
        return false;
    case QEvent::ShortcutOverride: // sent to the focus object before every key press, shortcut or not
        if (static_cast<QKeyEvent*>(e)->key() != Qt::Key_Shift) armed_ = false;
        return false;
    default:
        break;
    }
    // the rest only from this window (delivered to the window, then again to its items: harmless repeats)
    const auto* item = qobject_cast<QQuickItem*>(watched);
    if (watched != window_ && !(item && item->window() == window_)) return false;
    switch (e->type()) {
    case QEvent::KeyPress: {
        auto* k = static_cast<QKeyEvent*>(e);
        if (k->key() == Qt::Key_Shift) {
            if (k->isAutoRepeat() || armed_) break; // repeats; the same press delivered to an item
            // only a lone Shift: no other modifier, no mouse button held (a drag that started without Shift)
            const Qt::KeyboardModifiers others =
                k->modifiers() & ~(Qt::ShiftModifier | Qt::KeypadModifier | Qt::GroupSwitchModifier);
            armed_ = enabled_ && others == Qt::NoModifier && QGuiApplication::mouseButtons() == Qt::NoButton;
            held_.start();
        } else {
            armed_ = false; // Shift+key, also any shortcut
        }
        break;
    }
    case QEvent::KeyRelease: {
        auto* k = static_cast<QKeyEvent*>(e);
        if (k->key() != Qt::Key_Shift || k->isAutoRepeat()) break;
        const bool tap = armed_ && held_.isValid() && held_.elapsed() <= maxHoldMs_ && window_->isActive();
        armed_ = false;
        if (tap) Q_EMIT tapped();
        break;
    }
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
    case QEvent::Wheel:
    case QEvent::TouchBegin:
    case QEvent::TabletPress:
    case QEvent::NativeGesture:
    case QEvent::FocusOut:
    case QEvent::WindowDeactivate:
    case QEvent::Hide:
        armed_ = false;
        break;
    default:
        break;
    }
    return false; // never consumes: the event continues to the items and shortcuts
}

} // namespace fkapp

// SPDX-FileCopyrightText: Copyright 2026 Lichii contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lichii/util/keyboard_overlay.h"

#include <algorithm>
#include <QEvent>
#include <QGuiApplication>
#include <QInputMethod>
#include <QLayout>
#include <QMainWindow>
#include <QScopedValueRollback>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStyle>
#include <QTimer>

namespace Lichii {

namespace {

constexpr int EditorGap = 8;
constexpr int MaximumPanelWidth = 900;
constexpr int AnimationMs = 210;

QRect InWindow(const QWidget* child, const QWidget* window) {
    return QRect(child->mapTo(window, QPoint{}), child->size());
}

} // namespace

KeyboardOverlay::KeyboardOverlay(QWidget* window_, QWidget* panel_)
    : QObject(window_), window(window_), panel(panel_) {
    panel->setParent(window);
    panel->hide();
    panel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    window->installEventFilter(this);
    panel->installEventFilter(this);
    connect(QGuiApplication::inputMethod(), &QInputMethod::cursorRectangleChanged, this,
            &KeyboardOverlay::ScheduleUpdate);
    animation.setEasingCurve(QEasingCurve::InOutCubic);
    connect(&animation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        reveal = value.toReal();
        UpdateFrame();
    });
    connect(&animation, &QVariantAnimation::finished, this, [this] {
        if (!open) {
            FinishClose();
        }
    });
}

KeyboardOverlay::~KeyboardOverlay() {
    if (window && window->isVisible()) {
        FinishClose();
    }
    delete panel.data();
}

void KeyboardOverlay::Open(QWidget* target) {
    if (!window || !panel || !target || !window->isAncestorOf(target) ||
        panel->isAncestorOf(target)) {
        return;
    }
    if (target != editor) {
        QScopedValueRollback guard(updating, true);
        SetScroll(0);
        SetPan(0);
        if (editor) {
            editor->removeEventFilter(this);
        }
        disconnect(editor_destroyed);
        disconnect(scroll_changed);
        scroll_offset = 0;
        pan = 0;
        editor = target;
        editor->installEventFilter(this);
        editor_destroyed =
            connect(editor, &QObject::destroyed, this, &KeyboardOverlay::ScheduleUpdate);
        content = window;
        if (const auto* main = qobject_cast<const QMainWindow*>(window.data());
            main && main->centralWidget() && main->centralWidget()->isAncestorOf(target)) {
            content = main->centralWidget();
        }
        content_layout = content->layout();
        direct_child = nullptr;
        if (content_layout) {
            content_layout->activate();
            base_geometry = content_layout->geometry();
        } else {
            auto* child = target;
            while (child->parentWidget() != content && child != content) {
                child = child->parentWidget();
            }
            direct_child = child == content ? nullptr : child;
            base_geometry = direct_child ? direct_child->geometry() : QRect{};
        }

        scroll_area = nullptr;
        for (auto* ancestor = target->parentWidget(); ancestor && ancestor != content;
             ancestor = ancestor->parentWidget()) {
            if (auto* area = qobject_cast<QScrollArea*>(ancestor);
                area && area->viewport()->isAncestorOf(target)) {
                scroll_area = area;
                break;
            }
        }
        scroll_start = scroll_area ? scroll_area->verticalScrollBar()->value() : 0;
        if (scroll_area) {
            scroll_changed = connect(scroll_area->verticalScrollBar(), &QScrollBar::valueChanged,
                                     this, &KeyboardOverlay::ScheduleUpdate);
        }
        base_editor_rect = EditorRect();
        EnsureRoom();
    }

    const bool was_open = open;
    open = true;
    panel->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    UpdateFrame();
    panel->show();
    panel->raise();
    if (!was_open) {
        AnimateTo(1);
    }
}

void KeyboardOverlay::Close(bool animate) {
    if (!open && reveal == 0) {
        return;
    }
    open = false;
    if (panel) {
        panel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }
    AnimateTo(0, animate);
}

bool KeyboardOverlay::IsOpen() const { return open; }

QWidget* KeyboardOverlay::Panel() const { return panel; }

bool KeyboardOverlay::eventFilter(QObject* watched, QEvent* event) {
    if (watched == window && event->type() == QEvent::Hide) {
        Close(false);
    } else if (event->type() == QEvent::Resize || event->type() == QEvent::LayoutRequest ||
               (watched == editor &&
                (event->type() == QEvent::Move || event->type() == QEvent::Hide))) {
        ScheduleUpdate();
    }
    return QObject::eventFilter(watched, event);
}

void KeyboardOverlay::ScheduleUpdate() {
    if ((!open && reveal == 0) || updating || update_pending) {
        return;
    }
    update_pending = true;
    QTimer::singleShot(0, this, [this] {
        update_pending = false;
        if (open && (!editor || !editor->isVisible())) {
            Close();
        } else if (open || reveal > 0) {
            UpdateFrame();
        }
    });
}

void KeyboardOverlay::UpdateFrame() {
    if (updating || !window || !panel) {
        return;
    }
    QScopedValueRollback guard(updating, true);
    const int width = PanelWidth();
    const int height = PanelHeight(width);
    const int left = (window->width() - width) / 2;
    const int top = window->height() - qRound(reveal * height);
    const QRect panel_geometry(left, top, width, height);
    if (panel->geometry() != panel_geometry) {
        panel->setGeometry(panel_geometry);
    }

    const QRect actual_geometry = content_layout ? content_layout->geometry()
                                  : direct_child ? direct_child->geometry()
                                                 : QRect{};
    // Qt can replace the panned rectangle with a new layout baseline after a native resize.
    if (actual_geometry.isValid() && actual_geometry != base_geometry.translated(0, -pan)) {
        base_geometry = actual_geometry;
        pan = 0;
    }
    if (scroll_area) {
        const auto* bar = scroll_area->verticalScrollBar();
        const int value = bar->value();
        if (value != scroll_start + scroll_offset) {
            scroll_start = std::clamp(value - scroll_offset, bar->minimum(), bar->maximum());
            scroll_offset = value - scroll_start;
        }
    }
    if (editor && editor->isVisible()) {
        base_editor_rect = EditorRect().translated(0, pan + scroll_offset);
    }

    int overlap = 0;
    if (reveal > 0 && base_editor_rect.left() < left + width && base_editor_rect.right() >= left) {
        overlap = std::max(0, base_editor_rect.bottom() + 1 + EditorGap - top);
    }
    int scroll = 0;
    if (scroll_area) {
        const int viewport_top = InWindow(scroll_area->viewport(), window).top() + pan;
        const int room = scroll_area->verticalScrollBar()->maximum() - scroll_start;
        scroll = std::clamp(overlap, 0,
                            std::max(0, std::min(room, base_editor_rect.top() - viewport_top)));
    }
    SetScroll(scroll);
    SetPan(overlap - scroll_offset);
}

void KeyboardOverlay::AnimateTo(qreal target, bool animate) {
    animation.stop();
    if (!animate || reveal == target || !window ||
        window->style()->styleHint(QStyle::SH_Widget_Animation_Duration, nullptr, window) == 0) {
        reveal = target;
        UpdateFrame();
        if (!open) {
            FinishClose();
        }
        return;
    }
    {
        const QSignalBlocker block(animation);
        animation.setStartValue(reveal);
        animation.setEndValue(target);
        animation.setDuration(std::max(1, qRound(AnimationMs * qAbs(target - reveal))));
        animation.setCurrentTime(0);
    }
    animation.start();
}

void KeyboardOverlay::FinishClose() {
    QScopedValueRollback guard(updating, true);
    if (panel) {
        panel->hide();
    }
    SetScroll(0);
    SetPan(0);
    RestoreWindow();
    if (editor) {
        editor->removeEventFilter(this);
    }
    disconnect(editor_destroyed);
    disconnect(scroll_changed);
    editor = nullptr;
    content = nullptr;
    content_layout = nullptr;
    direct_child = nullptr;
    scroll_area = nullptr;
    pan = 0;
    scroll_offset = 0;
    base_editor_rect = {};
}

QRect KeyboardOverlay::EditorRect() const {
    if (!editor || !window) {
        return {};
    }
    if (editor->height() > 96) {
        const auto caret = editor->inputMethodQuery(Qt::ImCursorRectangle).toRect();
        if (caret.isValid()) {
            return caret.adjusted(-4, -4, 4, 4).translated(editor->mapTo(window, QPoint{}));
        }
    }
    return InWindow(editor, window);
}

int KeyboardOverlay::ContentTop() const {
    return content ? content->mapTo(window, QPoint{}).y() : 0;
}

int KeyboardOverlay::PanelWidth() const { return std::min(MaximumPanelWidth, window->width()); }

int KeyboardOverlay::PanelHeight(int width) const {
    const int editor_height = editor ? EditorRect().height() : base_editor_rect.height();
    const int available =
        window->height() - ContentTop() - std::max(0, editor_height) - 2 * EditorGap;
    const int preferred =
        panel->hasHeightForWidth() ? panel->heightForWidth(width) : panel->sizeHint().height();
    return std::min(std::max(0, available), std::max(preferred, panel->minimumSizeHint().height()));
}

void KeyboardOverlay::EnsureRoom() {
    if (window->isFullScreen() || window->isMaximized() || !window->screen()) {
        return;
    }
    const QRect available = window->screen()->availableGeometry();
    const QSize frame = window->frameGeometry().size() - window->size();
    const QSize limit = available.size() - frame;
    const QSize minimum = panel->minimumSizeHint().expandedTo(panel->minimumSize());
    const QSize needed{minimum.width(),
                       minimum.height() + ContentTop() + base_editor_rect.height() + 2 * EditorGap};
    const QSize size = window->size().expandedTo(needed.boundedTo(limit));
    if (size == window->size()) {
        return;
    }
    if (!original_window_geometry.isValid()) {
        original_window_geometry = window->geometry();
        original_maximum_size = window->maximumSize();
        if (auto* layout = window->layout();
            layout && (size.width() > original_maximum_size.width() ||
                       size.height() > original_maximum_size.height())) {
            constrained_layout = layout;
            original_size_constraint = layout->sizeConstraint();
            layout->setSizeConstraint(QLayout::SetNoConstraint);
        }
    }
    window->setMaximumSize(window->maximumSize().expandedTo(size));
    window->resize(size);
    const QRect new_frame = window->frameGeometry();
    const QPoint frame_origin(
        std::clamp(new_frame.x(), available.left(),
                   std::max(available.left(), available.right() + 1 - new_frame.width())),
        std::clamp(new_frame.y(), available.top(),
                   std::max(available.top(), available.bottom() + 1 - new_frame.height())));
    window->move(window->pos() + frame_origin - new_frame.topLeft());
    expanded_window_geometry = window->geometry();
    if (content_layout) {
        base_geometry = content_layout->geometry();
    }
}

void KeyboardOverlay::RestoreWindow() {
    if (!window || !original_window_geometry.isValid()) {
        return;
    }
    const bool restore = window->geometry() == expanded_window_geometry;
    window->setMaximumSize(original_maximum_size);
    if (restore && !window->isMaximized() && !window->isFullScreen()) {
        window->setGeometry(original_window_geometry);
    }
    if (constrained_layout) {
        constrained_layout->setSizeConstraint(original_size_constraint);
        constrained_layout->activate();
        constrained_layout = nullptr;
    }
    original_window_geometry = {};
}

void KeyboardOverlay::SetPan(int offset) {
    if (offset == pan) {
        return;
    }
    const QRect geometry = base_geometry.translated(0, -offset);
    if (content_layout) {
        content_layout->setGeometry(geometry);
    } else if (direct_child) {
        direct_child->move(geometry.topLeft());
    } else {
        return;
    }
    pan = offset;
}

void KeyboardOverlay::SetScroll(int offset) {
    if (scroll_area && offset != scroll_offset) {
        auto* bar = scroll_area->verticalScrollBar();
        bar->setValue(scroll_start + offset);
        scroll_offset = bar->value() - scroll_start;
    }
}

} // namespace Lichii

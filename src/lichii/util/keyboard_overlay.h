// SPDX-FileCopyrightText: Copyright 2026 Lichii contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QLayout>
#include <QPointer>
#include <QRect>
#include <QVariantAnimation>

class QScrollArea;
class QWidget;

namespace Lichii {

class KeyboardOverlay final : public QObject {
public:
    KeyboardOverlay(QWidget* window, QWidget* panel);
    ~KeyboardOverlay() override;

    void Open(QWidget* editor);
    void Close(bool animate = true);

    bool IsOpen() const;
    QWidget* Panel() const;

private:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void ScheduleUpdate();
    void UpdateFrame();
    void AnimateTo(qreal target, bool animate = true);
    void FinishClose();

    QRect EditorRect() const;
    int ContentTop() const;
    int PanelWidth() const;
    int PanelHeight(int width) const;
    void EnsureRoom();
    void RestoreWindow();
    void SetPan(int offset);
    void SetScroll(int offset);

    QPointer<QWidget> window;
    QPointer<QWidget> panel;
    QPointer<QWidget> editor;
    QPointer<QWidget> content;
    QPointer<QWidget> direct_child;
    QPointer<QLayout> content_layout;
    QPointer<QScrollArea> scroll_area;
    QVariantAnimation animation;
    QRect base_geometry;
    QRect base_editor_rect;
    QRect original_window_geometry;
    QRect expanded_window_geometry;
    QSize original_maximum_size;
    QPointer<QLayout> constrained_layout;
    QLayout::SizeConstraint original_size_constraint = QLayout::SetDefaultConstraint;
    QMetaObject::Connection editor_destroyed;
    QMetaObject::Connection scroll_changed;
    int scroll_start = 0;
    int scroll_offset = 0;
    int pan = 0;
    qreal reveal = 0;
    bool open = false;
    bool updating = false;
    bool update_pending = false;
};

} // namespace Lichii

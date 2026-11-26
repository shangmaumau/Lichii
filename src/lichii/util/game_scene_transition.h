// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>

#include <QPixmap>
#include <QVariantAnimation>
#include <QWidget>

class GameSceneTransition final : public QWidget {
public:
    explicit GameSceneTransition(QWidget* surface);

    bool Expand(const QRect& cover, QPixmap artwork, std::function<void()> complete,
                QPixmap frame);
    bool HoldFrame(const QPixmap& frame);
    void RefreshHeldFrame();
    void Shrink(const QRect& cover, QPixmap frame, QPixmap artwork = {});
    bool Reverse(std::function<void()> complete = {});
    bool IsExpanding() const;
    bool IsShrinking() const;
    void Release();
    void Clear();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    enum class Phase { Idle, Expanding, Holding, Shrinking, Releasing };
    void Start(Phase next, qreal start, qreal end, int duration, bool reversing = false);
    void Finish();
    void SetAnchor(const QRect& cover);

    QVariantAnimation motion;
    Phase phase = Phase::Idle;
    QRectF anchor;
    QPixmap artwork;
    QPixmap scene;
    QColor scene_base = Qt::black;
    std::function<void()> completion;
};

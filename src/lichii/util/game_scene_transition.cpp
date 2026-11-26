// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <utility>

#include <QEvent>
#include <QPainter>
#include <QPainterPath>

#include "lichii/util/game_scene_transition.h"

namespace {

constexpr int ZoomDuration = 450;
constexpr int ReleaseDuration = 90;
constexpr qreal OverlapStart = 0.35;
constexpr qreal IconEnd = 0.40;
constexpr qreal ReverseRamp = 0.18;

qreal SmoothStep(qreal t) {
    return t * t * (3.0 - 2.0 * t);
}

qreal Portion(qreal time, qreal start, qreal end) {
    return std::clamp((time - start) / (end - start), 0.0, 1.0);
}

qreal ContainerProgress(qreal time) {
    constexpr qreal damping = 5.5;
    const auto response = [](qreal t) {
        const qreal x = damping * t;
        return 1.0 - (1.0 + x) * std::exp(-x);
    };
    return response(time) / response(1.0);
}

qreal SceneOpacity(qreal time) {
    if (time < OverlapStart) {
        return 0.0;
    }
    if (time < IconEnd) {
        return std::lerp(0.10, 0.70, Portion(time, OverlapStart, IconEnd));
    }
    return std::lerp(0.70, 1.0, SmoothStep(Portion(time, IconEnd, 1.0)));
}

qreal ReversalProgress(qreal time) {
    const qreal distance = time < ReverseRamp ? time * time / (2.0 * ReverseRamp)
                                             : time - ReverseRamp / 2.0;
    return distance / (1.0 - ReverseRamp / 2.0);
}

QColor SceneBase(const QPixmap& scene) {
    const auto corner = scene.copy(0, 0, 1, 1).toImage();
    QColor color = corner.isNull() ? QColor(Qt::black) : corner.pixelColor(0, 0);
    color.setAlpha(255);
    return color;
}

void DrawCover(QPainter& painter, const QRectF& bounds, const QPixmap& image) {
    if (image.isNull()) {
        return;
    }
    QRectF source(QPointF{}, bounds.size().scaled(image.size(), Qt::KeepAspectRatio));
    source.moveCenter(QRectF(image.rect()).center());
    painter.drawPixmap(bounds, image, source);
}

} // namespace

GameSceneTransition::GameSceneTransition(QWidget* surface) : QWidget(surface) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    hide();
    surface->installEventFilter(this);

    connect(&motion, &QVariantAnimation::valueChanged, this, qOverload<>(&QWidget::update));
    connect(&motion, &QVariantAnimation::finished, this, &GameSceneTransition::Finish);
}

void GameSceneTransition::SetAnchor(const QRect& cover) {
    const auto canvas = parentWidget()->size();
    anchor = QRectF(qreal(cover.x()) / canvas.width(), qreal(cover.y()) / canvas.height(),
                    qreal(cover.width()) / canvas.width(), qreal(cover.height()) / canvas.height());
}

bool GameSceneTransition::Expand(const QRect& cover, QPixmap artwork,
                                 std::function<void()> complete, QPixmap frame) {
    if (artwork.isNull() || frame.isNull() || cover.isEmpty() || !parentWidget()->isVisible() ||
        parentWidget()->size().isEmpty()) {
        return false;
    }
    Clear();
    SetAnchor(cover);
    scene = std::move(frame);
    scene_base = SceneBase(scene);
    this->artwork = std::move(artwork);
    completion = std::move(complete);
    Start(Phase::Expanding, 0.0, 1.0, ZoomDuration);
    return true;
}

bool GameSceneTransition::HoldFrame(const QPixmap& frame) {
    if (frame.isNull() || !parentWidget()->isVisible() || parentWidget()->size().isEmpty()) {
        return false;
    }
    motion.stop();
    phase = Phase::Holding;
    artwork = {};
    completion = {};
    scene = frame;
    scene_base = SceneBase(scene);
    RefreshHeldFrame();
    return true;
}

void GameSceneTransition::RefreshHeldFrame() {
    if (phase == Phase::Holding && parentWidget()->isVisible()) {
        setGeometry(parentWidget()->rect());
        raise();
        show();
        repaint();
    }
}

void GameSceneTransition::Shrink(const QRect& cover, QPixmap frame, QPixmap artwork) {
    if (cover.isEmpty() || !parentWidget()->isVisible() || parentWidget()->size().isEmpty() ||
        frame.isNull()) {
        Clear();
        return;
    }
    completion = {};
    SetAnchor(cover);
    scene = std::move(frame);
    scene_base = SceneBase(scene);
    this->artwork = std::move(artwork);
    Start(Phase::Shrinking, 1.0, 0.0, ZoomDuration);
}

bool GameSceneTransition::Reverse(std::function<void()> complete) {
    if (phase != Phase::Expanding && phase != Phase::Holding &&
        (phase != Phase::Shrinking || (!completion && !complete))) {
        return false;
    }
    const bool opening = phase == Phase::Shrinking;
    if (opening && complete) {
        completion = std::move(complete);
    }
    const qreal from = phase == Phase::Holding ? 1.0 : motion.currentValue().toReal();
    const qreal to = opening ? 1.0 : 0.0;
    const int duration = std::max(1, qRound(ZoomDuration * std::abs(to - from) /
                                           (1.0 - ReverseRamp / 2.0)));
    Start(opening ? Phase::Expanding : Phase::Shrinking, from, to, duration, true);
    return true;
}

bool GameSceneTransition::IsExpanding() const {
    return phase == Phase::Expanding;
}

bool GameSceneTransition::IsShrinking() const {
    return phase == Phase::Shrinking;
}

void GameSceneTransition::Release() {
    if (phase == Phase::Holding) {
        Start(Phase::Releasing, 0.0, 1.0, ReleaseDuration);
    }
}

void GameSceneTransition::Start(Phase next, qreal start, qreal end, int duration, bool reversing) {
    motion.stop();
    phase = next;
    QEasingCurve timeline(QEasingCurve::Linear);
    if (reversing) {
        timeline.setCustomType(ReversalProgress);
    }
    motion.setEasingCurve(timeline);
    motion.setDuration(duration);
    motion.setStartValue(start);
    motion.setEndValue(end);
    motion.setCurrentTime(0);
    setGeometry(parentWidget()->rect());
    raise();
    show();
    update();
    motion.start();
}

void GameSceneTransition::Finish() {
    if (phase != Phase::Expanding) {
        Clear();
        return;
    }
    phase = Phase::Holding;
    repaint();
    auto complete = std::exchange(completion, {});
    if (complete) {
        complete();
    }
}

void GameSceneTransition::Clear() {
    motion.stop();
    hide();
    phase = Phase::Idle;
    artwork = {};
    scene = {};
    completion = {};
}

bool GameSceneTransition::eventFilter(QObject* watched, QEvent* event) {
    if (watched == parentWidget() && event->type() == QEvent::Resize && phase != Phase::Idle) {
        setGeometry(parentWidget()->rect());
    }
    return QWidget::eventFilter(watched, event);
}

void GameSceneTransition::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    const qreal progress = motion.currentValue().toReal();
    if (phase == Phase::Releasing) {
        painter.setOpacity(1.0 - SmoothStep(progress));
        DrawCover(painter, rect(), scene);
        return;
    }

    const qreal time = phase == Phase::Holding ? 1.0 : progress;
    const qreal expanded = ContainerProgress(time);
    const QRectF tile(anchor.x() * width(), anchor.y() * height(), anchor.width() * width(),
                      anchor.height() * height());
    const QRectF bounds(tile.topLeft() * (1.0 - expanded),
                        tile.size() * (1.0 - expanded) + QSizeF(size()) * expanded);
    const qreal radius = std::min(tile.width(), tile.height()) * 0.055 * (1.0 - expanded);

    if (expanded < 1.0) {
        painter.fillRect(rect(), QColor(0, 0, 0, qRound(80 * expanded)));
    }
    QPainterPath clip;
    clip.addRoundedRect(bounds, radius, radius);
    painter.setClipPath(clip);
    painter.fillRect(bounds, scene_base);
    if (time < IconEnd) {
        const qreal icon_fade = Portion(time, 0.0, IconEnd);
        painter.setOpacity(1.0 - icon_fade * icon_fade);
        DrawCover(painter, bounds, artwork);
    }
    if (time >= OverlapStart) {
        painter.setOpacity(SceneOpacity(time));
        DrawCover(painter, bounds, scene);
    }
}

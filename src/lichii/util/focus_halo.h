// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QConicalGradient>
#include <QPainter>
#include <QVariantAnimation>
#include <QWidget>

class FocusHalo final : public QWidget {
public:
    enum class Weight { Standard, Thin };

    explicit FocusHalo(QWidget* parent) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        rotation.setStartValue(0.0);
        rotation.setEndValue(360.0);
        rotation.setDuration(2600);
        rotation.setLoopCount(-1);
        connect(&rotation, &QVariantAnimation::valueChanged, this, qOverload<>(&QWidget::update));
        hide();
    }

    void ShowAround(const QRect& target, qreal radius, Weight weight = Weight::Standard) {
        if (target.isEmpty()) {
            hide();
            return;
        }
        corner_radius = radius;
        thin = weight == Weight::Thin;
        setGeometry(target.adjusted(-Margin, -Margin, Margin, Margin));
        if (isHidden()) {
            show();
        }
        raise();
        update();
    }

protected:
    void showEvent(QShowEvent*) override {
        rotation.start();
    }

    void hideEvent(QHideEvent*) override {
        rotation.stop();
    }

    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(Qt::NoBrush);
        const qreal gap = thin ? 1.0 : 2.0;
        const qreal inset = Margin - gap;
        const QRectF outline = QRectF(rect()).adjusted(inset, inset, -inset, -inset);
        static const QGradientStops stops{{0.00, QColor(85, 225, 255)},
                                          {0.17, QColor(123, 151, 255)},
                                          {0.34, QColor(195, 126, 255)},
                                          {0.51, QColor(255, 106, 173)},
                                          {0.68, QColor(255, 202, 121)},
                                          {0.84, QColor(123, 247, 209)},
                                          {1.00, QColor(85, 225, 255)}};
        QConicalGradient colors(QPointF{}, rotation.currentValue().toReal());
        colors.setStops(stops);
        QBrush gradient(colors);
        gradient.setTransform(QTransform::fromTranslate(outline.center().x(), outline.center().y())
                                  .scale(outline.width() / 2, outline.height() / 2));
        const qreal radius = corner_radius + gap;
        painter.setPen(QPen(gradient, thin ? 1.75 : 3.5));
        painter.drawRoundedRect(outline, radius, radius);
    }

private:
    static constexpr int Margin = 9;
    qreal corner_radius = 0;
    bool thin = false;
    QVariantAnimation rotation;
};

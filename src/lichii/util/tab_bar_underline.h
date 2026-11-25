// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QEvent>
#include <QPainter>
#include <QRegion>
#include <QTabBar>
#include <QToolButton>

#include "qt_common/config/uisettings.h"

class TabBarUnderline final : public QWidget {
public:
    static void Attach(QTabBar* tabs) {
        if (!tabs->findChild<QWidget*>(Name, Qt::FindDirectChildrenOnly)) {
            new TabBarUnderline(tabs);
        }
    }

    static void SetControllerActive(QWidget* window, bool active) {
        window->setProperty(ControllerInput, active);
        for (auto* const underline : window->findChildren<QWidget*>(Name)) {
            underline->update();
        }
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == tabs && event->type() == QEvent::Resize) {
            setGeometry(tabs->rect());
        }
        return QWidget::eventFilter(watched, event);
    }

    void paintEvent(QPaintEvent*) override {
        const auto& theme = UISettings::values.theme;
        if ((!theme.empty() && theme != "lichii") ||
            tabs->window()->property(ControllerInput).toBool()) {
            return;
        }

        const QRect tab = tabs->tabRect(tabs->currentIndex());
        constexpr int radius = 7;
        constexpr int margin = 2;
        constexpr int thickness = 2;
        const QRect line = tab.adjusted(radius, tab.height() - thickness, -radius - margin, 0);
        if (line.isEmpty()) {
            return;
        }

        QRegion clip(rect());
        for (const auto* const button : tabs->findChildren<QToolButton*>(Qt::FindDirectChildrenOnly)) {
            if (button->isVisible()) {
                clip -= QRect(button->x(), 0, button->width(), height());
            }
        }
        QPainter painter(this);
        painter.setClipRegion(clip);
        painter.fillRect(line, QColor(240, 56, 107));
    }

private:
    explicit TabBarUnderline(QTabBar* tabs_) : QWidget(tabs_), tabs(tabs_) {
        setObjectName(Name);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setGeometry(tabs->rect());
        lower();
        tabs->installEventFilter(this);
        connect(tabs, &QTabBar::currentChanged, this, [this] { update(); });
        show();
    }

    inline static const QString Name = QStringLiteral("lichiiTabBarUnderline");
    static constexpr auto ControllerInput = "lichiiControllerTabInput";
    QTabBar* const tabs;
};

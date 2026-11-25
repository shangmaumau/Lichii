// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <QApplication>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QScrollArea>
#include <QTabBar>
#include <QTabWidget>
#include <QTreeView>

namespace ConfigurationShared {

inline bool NavigateSettings(Qt::Key key, QTabWidget& pages, QDialogButtonBox& buttons,
                             QListWidget* categories = nullptr) {
    auto* focus = QApplication::focusWidget();
    auto* tabs = pages.tabBar();
    const bool confirm = key == Qt::Key_Enter || key == Qt::Key_Return;
    if (categories && key == Qt::Key_Escape && focus != categories) {
        categories->setFocus();
        return true;
    }

    const bool previous_tab = key == Qt::Key_Backtab || key == Qt::Key_PageUp;
    const bool next_tab = key == Qt::Key_Tab || key == Qt::Key_PageDown;
    if (previous_tab || next_tab ||
        (focus == tabs && (key == Qt::Key_Left || key == Qt::Key_Right))) {
        if (const int count = pages.count(); count > 0) {
            const int step = previous_tab || key == Qt::Key_Left ? -1 : 1;
            pages.setCurrentIndex((pages.currentIndex() + step + count) % count);
            tabs->setFocus();
        }
        return true;
    }

    if (categories && focus == categories) {
        if (key == Qt::Key_Right || confirm) {
            tabs->setFocus();
            return true;
        }
        return false;
    }
    if (focus == tabs && key == Qt::Key_Up) {
        if (categories) {
            categories->setFocus();
        }
        return categories != nullptr;
    }
    if (key != Qt::Key_Up && key != Qt::Key_Down && !(focus == tabs && confirm)) {
        return false;
    }

    const bool forward = key != Qt::Key_Up;
    if (auto* list = qobject_cast<QAbstractItemView*>(focus); list && list->model()) {
        const auto index = list->currentIndex();
        const auto* tree = qobject_cast<QTreeView*>(list);
        const auto next = tree ? (forward ? tree->indexBelow(index) : tree->indexAbove(index))
                               : index.siblingAtRow(index.row() + (forward ? 1 : -1));
        if (next.isValid() || (!index.isValid() && list->model()->rowCount(list->rootIndex()) > 0)) {
            return false;
        }
    }

    const auto* scroll = qobject_cast<QScrollArea*>(pages.currentWidget());
    const auto* page = scroll ? scroll->widget() : pages.currentWidget();
    QList<QWidget*> controls;
    QList<QWidget*> footer;
    for (auto* widget = tabs->nextInFocusChain(); widget != tabs;
         widget = widget->nextInFocusChain()) {
        if (!widget->isEnabled() || !widget->isVisible() || !(widget->focusPolicy() & Qt::TabFocus) ||
            widget->focusProxy()) {
            continue;
        }
        if (page && page->isAncestorOf(widget)) {
            controls.append(widget);
        } else if (buttons.isAncestorOf(widget)) {
            footer.append(widget);
        }
    }
    std::stable_sort(controls.begin(), controls.end(), [page](const QWidget* a, const QWidget* b) {
        const auto lhs = a->mapTo(page, QPoint(0, a->height() / 2));
        const auto rhs = b->mapTo(page, QPoint(0, b->height() / 2));
        return lhs.y() != rhs.y() ? lhs.y() < rhs.y() : lhs.x() < rhs.x();
    });
    controls.append(footer);
    auto position = controls.indexOf(focus);
    if (position < 0 && focus != tabs) {
        const auto owner = std::ranges::find_if(controls, [focus](const QWidget* widget) {
            return focus && widget->isAncestorOf(focus);
        });
        if (owner == controls.end()) {
            return false;
        }
        position = owner - controls.begin();
    }

    const auto next = position + (forward ? 1 : -1);
    if (next < 0) {
        tabs->setFocus();
    } else if (next < controls.size()) {
        auto* widget = controls.at(next);
        widget->setFocus(forward ? Qt::TabFocusReason : Qt::BacktabFocusReason);
        for (auto* parent = widget->parentWidget(); parent; parent = parent->parentWidget()) {
            if (auto* area = qobject_cast<QScrollArea*>(parent)) {
                area->ensureWidgetVisible(widget);
            }
        }
    }
    return true;
}

} // namespace ConfigurationShared

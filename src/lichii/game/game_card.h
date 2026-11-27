// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QStyledItemDelegate>

class QPixmap;

/**
 * A stylized "card"-like delegate for the game grid view.
 * Adapted from QML
 */
class GameCard : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit GameCard(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;

    QRect getCardRect(const QStyleOptionViewItem &option, const QModelIndex &index) const;
    QRect CoverRect(const QStyleOptionViewItem& option, const QModelIndex& index) const;

    bool hitTest(const QPoint& point, const QModelIndex& index,
                 const QWidget* widget, const QRect& cellRect) const;

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void setSize(const QSize& newSize, const QSize& contentSize, const int padding, const int columns);

private:
    struct CoverLayout {
        QRect rect;
        QSize physical_size;
        qreal device_pixel_ratio = 1.0;
    };

    static CoverLayout LayoutCover(const QRect& card, const QPixmap& picture, unsigned int icon_size);
    static constexpr int cardMargin = 8;

    QSize m_size;
    QSize m_contentSize;
    int m_padding = 0;
    int m_columns = 0;
};

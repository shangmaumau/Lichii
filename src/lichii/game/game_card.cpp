// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <QPainter>
#include <QStringList>
#include <QtMath>
#include "lichii/game/common.h"
#include "game_card.h"
#include "qt_common/config/uisettings.h"

GameCard::GameCard(QObject* parent) : QStyledItemDelegate{parent} {
    setObjectName("GameCard");
}

void GameCard::paint(QPainter* painter, const QStyleOptionViewItem& option,
                     const QModelIndex& index) const {
    if (!index.isValid())
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    constexpr int cardCornerRadius = 10;

    const QRect cardRect = getCardRect(option, index);

    QPalette palette = option.palette;
    QColor backgroundColor = palette.window().color();
    QColor borderColor = palette.dark().color();
    QColor textColor = palette.text().color();

    // highlight on select or hover
    if (option.state & QStyle::State_Selected) {
        backgroundColor = palette.highlight().color();
        borderColor = palette.highlight().color().lighter(150);
        textColor = palette.highlightedText().color();
    } else if (option.state & QStyle::State_MouseOver) {
        backgroundColor = backgroundColor.lighter(120);
    }

    painter->setBrush(backgroundColor);
    painter->setPen(QPen(borderColor, 1));
    painter->drawRoundedRect(cardRect, cardCornerRadius, cardCornerRadius);

    const u32 icon_size = UISettings::values.game_cover_size.GetValue();
    const auto icon_pixmap = index.data(Qt::DecorationRole).value<QPixmap>();
    const auto cover = LayoutCover(cardRect, icon_pixmap, icon_size);

    QRect iconRect;
    if (!cover.rect.isEmpty()) {
        QPixmap scaled =
            icon_pixmap.scaled(cover.physical_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        scaled.setDevicePixelRatio(cover.device_pixel_ratio);
        iconRect = cover.rect;
        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter->drawPixmap(iconRect, scaled);
    } else {
        iconRect = {cardRect.left() + cardMargin, cardRect.top() + cardMargin,
                    static_cast<int>(icon_size), static_cast<int>(icon_size)};
    }

    const bool suspended = index.data(GameListItemPath::SuspendedRole).toBool();
    if (suspended) {
        const QRect badge_bounds = iconRect.isEmpty() ? cardRect : iconRect;
        Lichii::DrawSuspendedGameBadge(*painter, badge_bounds, palette, option.font,
                                      index.data(Qt::AccessibleDescriptionRole).toString());
    }

    if (UISettings::values.show_game_name.GetValue()) {
        // Keep the title within the icon's width: at most two lines, cut with "..." beyond that.
        // Without icons (size 0) it takes the card's width instead.
        const int text_left = icon_size == 0 ? cardRect.left() + cardMargin : iconRect.left();
        const int text_width = icon_size == 0 ? cardRect.width() - 2 * cardMargin : iconRect.width();
        const int badge_clearance = icon_size == 0 && suspended ? 24 : 0;
        const int text_top = std::max(iconRect.bottom() + cardMargin, cardRect.top() + badge_clearance);
        const QRect textRect(text_left, text_top, text_width, cardRect.bottom() - text_top);

        QString title = index.data(Qt::DisplayRole).toString().split(QLatin1Char('\n')).first();

        painter->setPen(textColor);
        QFont font = option.font;
        font.setBold(true);
        font.setPixelSize(std::max(11.0, std::sqrt(static_cast<double>(icon_size))));
        painter->setFont(font);

        // Words wider than the box break anywhere; what doesn't fit on the second line is elided.
        const auto lines = Lichii::WrapGameTitle(title, font, textRect.width(), 2);
        painter->drawText(textRect, Qt::AlignHCenter | Qt::AlignTop,
                          lines.join(QLatin1Char('\n')));
    }

    painter->restore();
}

GameCard::CoverLayout GameCard::LayoutCover(const QRect& card, const QPixmap& picture,
                                           unsigned int icon_size) {
    if (picture.isNull() || icon_size == 0 || card.isEmpty()) {
        return {};
    }

    const qreal dpr = picture.devicePixelRatio() > 0.0 ? picture.devicePixelRatio() : 1.0;
    const int target = std::max(1, qRound(icon_size * dpr));
    QSize physical = picture.size();
    physical.scale(target, target, Qt::KeepAspectRatio);
    const QSize logical(qRound(physical.width() / dpr), qRound(physical.height() / dpr));
    return {{card.left() + (card.width() - logical.width()) / 2, card.top() + cardMargin - 1,
             logical.width(), logical.height()},
            physical, dpr};
}

QRect GameCard::CoverRect(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    if (!index.isValid() || option.rect.isEmpty()) {
        return {};
    }
    const auto picture = index.data(Qt::DecorationRole).value<QPixmap>();
    return LayoutCover(getCardRect(option, index), picture,
                       UISettings::values.game_cover_size.GetValue())
        .rect;
}

QRect GameCard::getCardRect(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    const int cell_width = option.rect.width();

    const int card_width = cell_width - m_padding;

    int card_left, card_top, card_height;

    if (m_columns >= 1) {
        // grid mode

        // center everything in-line, such that the leftmost and rightmost cards
        // have ~ equal padding to the edge of the viewport
        // spacing between each card is larger, but equal to each other
        const int column = index.row() % m_columns;

        const int row_width = m_columns * cell_width;
        const int total_gap = row_width - cardMargin * 2 - m_columns * card_width;
        const int gap = (m_columns > 1) ? (total_gap / (m_columns - 1)) : 0;
        card_left =
            option.rect.left() - column * cell_width + cardMargin + column * (card_width + gap) + 4;

        // fill cell vertically
        card_top = option.rect.top() + cardMargin;
        card_height = option.rect.height() - cardMargin - 1;
    } else {
        // carousel mode
        card_left = option.rect.left() + cardMargin + 4;

        // the delegate itself takes up the full height, but the card itself
        // gets centered
        const int content_height = m_contentSize.height() - cardMargin;
        const int cell_height = option.rect.height();

        card_height = std::min(content_height, cell_height - cardMargin * 2) - 1;
        card_top = option.rect.top() + (cell_height - card_height) / 2;
    }

    return QRect(card_left, card_top, card_width - cardMargin, card_height);
}

bool GameCard::hitTest(const QPoint& point, const QModelIndex& index, const QWidget* widget,
                       const QRect& cellRect) const {
    QStyleOptionViewItem option;
    option.initFrom(widget);
    option.rect = cellRect;
    return getCardRect(option, index).contains(point);
}

QSize GameCard::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    return m_size;
}

void GameCard::setSize(const QSize& newSize, const QSize& contentSize, const int padding,
                       const int columns) {
    m_size = newSize;
    m_contentSize = contentSize;
    m_padding = padding;
    m_columns = columns;
}

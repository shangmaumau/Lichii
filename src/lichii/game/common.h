// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <iterator>

#include <QCoreApplication>
#include <QFontMetrics>
#include <QPainter>
#include <QPalette>
#include <QStandardItem>
#include <QStringList>
#include <QTextLayout>

#include "qt_common/game_list/game_list_p.h"

namespace Lichii {

// Place the status on the cover, or use a small play symbol when the artwork is too small.
inline qreal DrawSuspendedGameBadge(QPainter& painter, const QRectF& bounds, const QPalette& palette,
                                   QFont font) {
    const auto area = bounds.adjusted(3, 3, -3, -3);
    if (area.width() < 16 || area.height() < 15) {
        return 0;
    }

    font.setPixelSize(12);
    font.setWeight(QFont::DemiBold);
    const QFontMetrics metrics(font);
    const auto label = QCoreApplication::translate("GameList", "Game in progress");
    const int label_width = metrics.horizontalAdvance(label) + 25;
    const bool show_text = area.width() >= label_width && area.height() >= metrics.height() + 6;
    const QSizeF size(show_text ? label_width : 16,
                      show_text ? metrics.height() + 6 : 15);
    const QRectF pill(QPointF(area.right() - size.width(), area.top()), size);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setFont(font);
    painter.setPen(Qt::NoPen);
    painter.setBrush(palette.color(QPalette::Highlight));
    painter.drawRoundedRect(pill, size.height() / 2, size.height() / 2);

    const QColor foreground = palette.color(QPalette::HighlightedText);
    painter.setBrush(foreground);
    const QPointF icon(show_text ? pill.left() + 8 : pill.left() + 6, pill.center().y());
    const QPointF triangle[]{icon + QPointF(0, -3.5), icon + QPointF(0, 3.5), icon + QPointF(5, 0)};
    painter.drawPolygon(triangle, std::size(triangle));
    if (show_text) {
        painter.setPen(foreground);
        painter.drawText(pill.adjusted(18, 0, -7, 0), Qt::AlignVCenter | Qt::AlignLeft, label);
    }

    painter.restore();

    return size.width() + 6;
}

inline QStringList WrapGameTitle(const QString& title, const QFont& font, int width, int max_lines) {
    QStringList lines;
    if (width <= 0) {
        return lines;
    }
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    QTextLayout layout(title, font);
    layout.setTextOption(option);
    layout.beginLayout();
    for (int i = 0; i < max_lines; ++i) {
        QTextLine line = layout.createLine();
        if (!line.isValid()) {
            break;
        }
        line.setLineWidth(width);
        lines.append(i + 1 == max_lines
                         ? QFontMetrics(font).elidedText(title.mid(line.textStart()).trimmed(),
                                                         Qt::ElideRight, width)
                         : title.mid(line.textStart(), line.textLength()).trimmed());
    }
    layout.endLayout();
    return lines;
}

inline bool ContainsAllWords(const QString& haystack, const QString& userinput) {
    const QStringList userinput_split = userinput.split(QLatin1Char{' '}, Qt::SkipEmptyParts);
    return std::all_of(userinput_split.begin(), userinput_split.end(), [&haystack](const QString& s) {
        return haystack.contains(s);
    });
}

inline bool FilterMatches(const QString& filter, const QStandardItem* item) {
    if (filter.isEmpty()) {
        return true;
    }

    auto const sluggify = [](const QString& s) {
        static const QRegularExpression separators(QStringLiteral("[^\\p{L}\\p{N}\\s]"));
        QString normalized = s.normalized(QString::NormalizationForm_D).toCaseFolded();
        return normalized.remove(separators);
    };

    const auto norm_filter = sluggify(filter);
    const auto program_id = item->data(GameListItemPath::ProgramIdRole).toULongLong();
    const QString file_path = item->data(GameListItemPath::FullPathRole).toString();
    const QString file_title = sluggify(item->data(GameListItemPath::TitleRole).toString());
    const QString file_program_id = QStringLiteral("%1").arg(program_id, 16, 16, QLatin1Char{'0'});
    const QString file_name =
        sluggify(file_path.mid(file_path.lastIndexOf(QLatin1Char{'/'}) + 1));
    return ContainsAllWords(file_name + QLatin1Char(' ') + file_title, norm_filter) ||
           (!norm_filter.isEmpty() && file_program_id.contains(norm_filter));
}

} // namespace Lichii

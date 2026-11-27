// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

#include <QActionGroup>
#include <QApplication>
#include <QCollator>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHash>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListView>
#include <QLocale>
#include <QMenu>
#include <QMouseEvent>
#include <QNetworkInformation>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollBar>
#include <QScroller>
#include <QSet>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QTime>
#include <QTimer>
#include <QToolButton>
#include <QtMath>
#include <QVariantAnimation>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "common/settings.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/hle/service/acc/profile_manager.h"
#include "lichii/configuration/system/new_user_dialog.h"
#include "lichii/game/common.h"
#include "lichii/game/game_home.h"
#include "lichii/util/focus_halo.h"
#include "lichii/util/util.h"
#include "qt_common/game_list/model.h"

namespace {

constexpr int SourceIndexRole = Qt::UserRole + 1;
constexpr int TileInset = 8;
constexpr int ShelfLimit = 12;
constexpr int AvatarSide = 42;
constexpr int LibraryButtonIconSize = 16;

QRect TileCoverRect(const QRect& cell, const QPixmap& cover) {
    const QRect tile = cell.adjusted(TileInset, TileInset, -TileInset, -TileInset);
    if (tile.isEmpty() || cover.isNull()) {
        return tile;
    }
    const QSize size = cover.size().scaled(tile.size(), Qt::KeepAspectRatio);
    return {tile.x() + (tile.width() - size.width()) / 2,
            tile.y() + (tile.height() - size.height()) / 2, size.width(), size.height()};
}

void DrawRunningGameStatus(QPainter& painter, const QRectF& cover, const QPixmap& avatar,
                           const QPalette& palette, QFont font, const QString& status) {
    const qreal short_side = std::min(cover.width(), cover.height());
    const int inset = std::clamp(qRound(short_side * 0.035), 3, 8);
    const QRectF area = cover.adjusted(inset, inset, -inset, -inset);
    const qreal preferred_height = std::clamp(short_side * 0.18, 18.0, 40.0);
    if (area.width() < preferred_height || area.height() < preferred_height) {
        return;
    }

    const int gap = std::clamp(qRound(short_side * 0.026), 3, 6);
    const int text_padding = std::clamp(qRound(short_side * 0.05), 5, 11);
    const auto badge_height = [preferred_height](const QFontMetrics& metrics) {
        return std::max(preferred_height, metrics.height() + 6.0);
    };
    font.setPixelSize(std::clamp(qRound(short_side * 0.095), 12, 22));
    font.setWeight(QFont::DemiBold);
    QFontMetrics metrics(font);
    while (font.pixelSize() > 12 &&
           (badge_height(metrics) > area.height() ||
            badge_height(metrics) + gap + 2 * text_padding + metrics.horizontalAdvance(status) >
                area.width())) {
        font.setPixelSize(font.pixelSize() - 1);
        metrics = QFontMetrics(font);
    }

    const qreal height = std::min({badge_height(metrics), area.width(), area.height()});
    const qreal pill_space = area.width() - height - gap;
    const int text_width = qFloor(pill_space) - 2 * text_padding;
    const QString label = pill_space >= height && text_width >= 32 && height >= metrics.height() + 6
                              ? metrics.elidedText(status, Qt::ElideRight, text_width)
                              : QString{};
    const qreal pill_width = label.isEmpty()
                                 ? 0
                                 : std::max(height, metrics.horizontalAdvance(label) + 2.0 * text_padding);
    const qreal group_width = height + (label.isEmpty() ? 0 : gap + pill_width);
    const QRectF image(area.center().x() - group_width / 2, area.bottom() - height, height, height);
    const qreal border_width = std::clamp(height * 0.048, 1.0, 2.0);
    const qreal border_inset = border_width / 2;

    painter.save();
    painter.setClipRect(cover, Qt::IntersectClip);
    QPainterPath image_mask;
    image_mask.addEllipse(image);
    painter.save();
    painter.setClipPath(image_mask, Qt::IntersectClip);
    painter.fillRect(image, palette.alternateBase());
    if (!avatar.isNull()) {
        const qreal side = std::min(avatar.width(), avatar.height());
        QRectF source(0, 0, side, side);
        source.moveCenter(QRectF(avatar.rect()).center());
        painter.drawPixmap(image, avatar, source);
    }
    painter.restore();

    painter.setPen(QPen(Qt::white, border_width));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(image.adjusted(border_inset, border_inset, -border_inset, -border_inset));

    if (!label.isEmpty()) {
        const QRectF pill(image.right() + gap, image.top(), pill_width, height);
        const QRectF outline = pill.adjusted(border_inset, border_inset, -border_inset, -border_inset);
        const qreal radius = outline.height() / 2;
        QColor background = palette.color(QPalette::Window);
        if (background.lightness() > 64) {
            background = QColor(23, 23, 27);
        }
        background.setAlpha(242);
        painter.setBrush(background);
        painter.drawRoundedRect(outline, radius, radius);
        painter.setFont(font);
        painter.drawText(pill, Qt::AlignCenter, label);
    }
    painter.restore();
}

class LibraryLoadingCovers final : public QWidget {
public:
    explicit LibraryLoadingCovers(QWidget* parent) : QWidget(parent) {
        sweep.setStartValue(0.0);
        sweep.setEndValue(1.0);
        sweep.setDuration(1600);
        sweep.setLoopCount(-1);
        connect(&sweep, &QVariantAnimation::valueChanged, this, qOverload<>(&QWidget::update));
    }

protected:
    void showEvent(QShowEvent* event) override {
        QWidget::showEvent(event);
        if (style()->styleHint(QStyle::SH_Widget_Animation_Duration, nullptr, this) > 0) {
            sweep.start();
        }
    }

    void hideEvent(QHideEvent* event) override {
        sweep.stop();
        QWidget::hideEvent(event);
    }

    void paintEvent(QPaintEvent*) override {
        const int cell = height();
        if (cell <= 2 * TileInset) {
            return;
        }
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        QColor base = palette().color(QPalette::Active, QPalette::WindowText);
        QColor highlight = base;
        base.setAlpha(15);
        highlight.setAlpha(30);
        const bool moving = sweep.state() == QAbstractAnimation::Running;
        const qreal progress = sweep.currentValue().toReal();
        const QEasingCurve easing(QEasingCurve::InOutSine);
        for (int x = 0; x < width(); x += cell) {
            const QRectF cover = TileCoverRect(QRect(x, 0, cell, cell), {});
            const qreal radius = cover.width() * GameIconCornerRatio;
            painter.setBrush(base);
            if (moving) {
                const qreal offset = progress - 0.055 * (x / cell);
                const qreal phase = easing.valueForProgress(offset - std::floor(offset));
                const qreal center = cover.x() + cover.width() * std::lerp(-0.5, 1.5, phase);
                const qreal span = cover.width() * 0.32;
                QLinearGradient glow(center - span, cover.center().y(),
                                     center + span, cover.top());
                glow.setColorAt(0, base);
                glow.setColorAt(0.5, highlight);
                glow.setColorAt(1, base);
                painter.setBrush(glow);
            }
            painter.drawRoundedRect(cover, radius, radius);
        }
    }

private:
    QVariantAnimation sweep;
};

class ShelfView final : public QListView {
  public:
    explicit ShelfView(QWidget* parent) : QListView(parent) {
        setObjectName(QStringLiteral("gameHomeShelf"));
        viewport()->setObjectName(QStringLiteral("gameHomeShelfViewport"));
        setStyleSheet(QStringLiteral(
            "QListView#gameHomeShelf { background: transparent; border: none; }"
            "QWidget#gameHomeShelfViewport { background: transparent; }"));
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll_timer.setInterval(16);
        scroll_timer.setTimerType(Qt::PreciseTimer);
        connect(&scroll_timer, &QTimer::timeout, this, &ShelfView::AdvanceScroll);
        for (auto* bar : {horizontalScrollBar(), verticalScrollBar()}) {
            connect(bar, &QScrollBar::sliderPressed, &scroll_timer, &QTimer::stop);
        }
        connect(QScroller::scroller(viewport()), &QScroller::stateChanged, this,
                [this](QScroller::State state) {
                    if (state != QScroller::Inactive) {
                        scroll_timer.stop();
                    }
                });
    }

    void RestoreScroll(const QPoint& position) {
        scroll_timer.stop();
        QScroller::scroller(viewport())->stop();
        horizontalScrollBar()->setValue(position.x());
        verticalScrollBar()->setValue(position.y());
    }

    void scrollTo(const QModelIndex& index, ScrollHint hint) override {
        const QRect tile = visualRect(index);
        if (hint != EnsureVisible || tile.isEmpty()) {
            scroll_timer.stop();
            QListView::scrollTo(index, hint);
            return;
        }
        const bool vertical = isWrapping();
        const int start = vertical ? tile.y() : tile.x();
        const int end = vertical ? tile.y() + tile.height() : tile.x() + tile.width();
        const int extent = vertical ? viewport()->height() : viewport()->width();
        const int delta = std::min(0, start) + std::max(0, end - extent);
        AnimateTo(ScrollBar()->value() + delta);
    }

  protected:
    void wheelEvent(QWheelEvent* event) override {
        auto* bar = ScrollBar();
        if (event->modifiers() & Qt::ControlModifier || bar->minimum() == bar->maximum()) {
            QListView::wheelEvent(event);
            return;
        }
        const auto along_axis = [this](const QPoint& point) {
            return isWrapping() || point.x() == 0 ? point.y() : point.x();
        };
        QScroller::scroller(viewport())->stop();
        if (!event->pixelDelta().isNull()) {
            scroll_timer.stop();
            bar->setValue(bar->value() - along_axis(event->pixelDelta()));
        } else if (const int delta = along_axis(event->angleDelta()); delta != 0) {
            const int step = isWrapping() ? 20 * QApplication::wheelScrollLines()
                                          : std::clamp(gridSize().width() / 2, 48, 104);
            const qreal base = scroll_timer.isActive() ? scroll_target : bar->value();
            AnimateTo(base - delta * step / 120.0);
        }
        event->accept();
    }

    void mousePressEvent(QMouseEvent* event) override {
        scroll_timer.stop();
        QListView::mousePressEvent(event);
    }

    void hideEvent(QHideEvent* event) override {
        scroll_timer.stop();
        QListView::hideEvent(event);
    }

  private:
    QScrollBar* ScrollBar() const {
        return isWrapping() ? verticalScrollBar() : horizontalScrollBar();
    }

    void AnimateTo(qreal target) {
        auto* bar = ScrollBar();
        scroll_target = std::clamp(target, qreal(bar->minimum()), qreal(bar->maximum()));
        if (!scroll_timer.isActive() && qRound(scroll_target) != bar->value()) {
            scroll_position = bar->value();
            frame_time.start();
            scroll_timer.start();
        }
    }

    void AdvanceScroll() {
        const qreal elapsed = frame_time.restart();
        auto* bar = ScrollBar();
        scroll_target = std::clamp(scroll_target, qreal(bar->minimum()), qreal(bar->maximum()));
        scroll_position += (scroll_target - scroll_position) * (1.0 - std::exp(-elapsed / 48.0));
        if (std::abs(scroll_target - scroll_position) <= 0.5) {
            scroll_position = scroll_target;
            scroll_timer.stop();
        }
        bar->setValue(qRound(scroll_position));
    }

    QTimer scroll_timer;
    QElapsedTimer frame_time;
    qreal scroll_position = 0;
    qreal scroll_target = 0;
};

class TileDelegate final : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void SetProfileAvatar(QPixmap avatar) {
        profile_avatar = std::move(avatar);
    }

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override {
        return static_cast<const QListView*>(parent())->gridSize();
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        const auto cover = index.data(Qt::DecorationRole).value<QPixmap>();
        const QRectF tile = TileCoverRect(option.rect, cover);
        const qreal radius = tile.width() * GameIconCornerRatio;
        const auto foreground = option.palette.color(QPalette::Text);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setRenderHint(QPainter::SmoothPixmapTransform);
        painter->setBrush(Qt::NoBrush);

        if (!cover.isNull()) {
            painter->drawPixmap(tile, cover, QRectF(cover.rect()));
        } else {
            painter->setPen(QPen(option.palette.color(QPalette::Mid), 1.0));
            painter->setBrush(option.palette.alternateBase());
            painter->drawRoundedRect(tile, radius, radius);
            painter->setPen(QPen(foreground, 1.7));
            painter->setBrush(Qt::NoBrush);
            const QPointF center = tile.center();
            const qreal side = tile.width() * 0.115;
            for (const int x : {-1, 0}) {
                for (const int y : {-1, 0}) {
                    painter->drawRoundedRect(QRectF(center.x() + x * (side + 3) + 1.5,
                                                    center.y() + y * (side + 3) + 1.5, side, side),
                                             2, 2);
                }
            }
        }
        const auto source = index.data(SourceIndexRole).value<QPersistentModelIndex>();
        if (source.data(GameListItemPath::SuspendedRole).toBool()) {
            const auto status = source.data(Qt::AccessibleDescriptionRole).toString();
            if (static_cast<const QListView*>(parent())->isWrapping()) {
                Lichii::DrawSuspendedGameBadge(*painter, tile, option.palette, option.font, status);
            } else {
                DrawRunningGameStatus(*painter, tile, profile_avatar, option.palette, option.font,
                                      status);
            }
        }

        painter->restore();
    }

  private:
    QPixmap profile_avatar;
};

QString RecentPath(const QString& path) {
    const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(path));
#ifdef _WIN32
    return clean.toCaseFolded();
#else
    return clean;
#endif
}

} // namespace

class LibraryPageTransition final : public QWidget {
public:
    explicit LibraryPageTransition(QWidget* parent) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        hide();
    }

    void SetFrame(QPixmap frame) {
        previous_page = std::move(frame);
        opacity = 1;
        setGeometry(parentWidget()->rect());
        raise();
        show();
    }

    void SetOpacity(qreal value) {
        opacity = value;
        update();
    }

    void Clear() {
        hide();
        previous_page = {};
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setOpacity(opacity);
        painter.drawPixmap(QPoint{}, previous_page);
    }

private:
    QPixmap previous_page;
    qreal opacity = 1;
};

class CoverTitleBubble final : public QWidget {
public:
    explicit CoverTitleBubble(QWidget* parent) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        hide();
    }

    void ShowTitle(const QString& title, const QRect& cover, bool prefer_below) {
        const QRect bounds = parentWidget()->rect().adjusted(8, 4, -8, -4);
        const int center = cover.x() + cover.width() / 2;
        const int width = 2 * std::min({cover.width(), center - bounds.left(),
                                        bounds.right() + 1 - center});
        const int text_width = width - 2 * PadX - 2;
        if (title.isEmpty() || text_width <= 0) {
            hide();
            return;
        }
        const auto metrics = fontMetrics();
        if (title != wrapped_title || text_width != wrapped_width || font() != wrapped_font) {
            wrapped_title = title;
            wrapped_width = text_width;
            wrapped_font = font();
            lines = Lichii::WrapGameTitle(title, wrapped_font, text_width, 3);
            widest_line = 0;
            for (const auto& line : lines) {
                widest_line = std::max(widest_line, metrics.horizontalAdvance(line));
            }
        }
        const int w = std::min(width, std::max(48, widest_line + 2 * PadX + 2));
        const int h = static_cast<int>(lines.size()) * metrics.lineSpacing() + 2 * PadY + Arrow;
        above = !prefer_below && (cover.top() - Gap - h >= bounds.top() ||
                                  cover.bottom() + Gap + h > bounds.bottom());
        const int y = std::clamp(above ? cover.top() - Gap - h : cover.bottom() + Gap,
                                 bounds.top(), std::max(bounds.top(), bounds.bottom() - h + 1));
        setGeometry(center - w / 2, y, w, h);
        if (isHidden()) {
            show();
        }
        raise();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF box = QRectF(rect()).adjusted(1, above ? 1 : Arrow, -1, above ? -Arrow : -1);
        const qreal edge = above ? box.bottom() : box.top();
        QPainterPath shape;
        shape.addRoundedRect(box, 7, 7);
        QPainterPath pointer;
        const int direction = above ? 1 : -1;
        const int tip = width() / 2;
        pointer.moveTo(tip - 7, edge - direction);
        pointer.lineTo(tip, edge + direction * (Arrow - 1));
        pointer.lineTo(tip + 7, edge - direction);
        pointer.closeSubpath();
        painter.setPen(QPen(QColor(162, 185, 218), 1));
        painter.setBrush(QColor(219, 233, 252));
        painter.drawPath(shape.united(pointer));
        painter.setPen(QColor(24, 39, 59));
        const int line_height = fontMetrics().lineSpacing();
        qreal top = box.top() + PadY - 1;
        for (const auto& line : lines) {
            painter.drawText(QRectF(box.left() + PadX, top, box.width() - 2 * PadX, line_height),
                             Qt::AlignCenter, line);
            top += line_height;
        }
    }

private:
    static constexpr int PadX = 13;
    static constexpr int PadY = 7;
    static constexpr int Arrow = 8;
    static constexpr int Gap = 8;
    QString wrapped_title;
    int wrapped_width = 0;
    QFont wrapped_font;
    QStringList lines;
    int widest_line = 0;
    bool above = true;
};

GameHome::GameHome(GameListModel* library_, Core::System& system_, QWidget* parent)
    : QWidget(parent), library(library_), system(system_), library_body(new QWidget(this)),
      view(new ShelfView(library_body)), tiles(new QStandardItemModel(this)),
      library_row(new QHBoxLayout(library_body)), status_bar(new QWidget(this)),
      collection_bar(new QWidget(this)), collection_header(new QHBoxLayout(collection_bar)),
      sort_rail(new QWidget(library_body)), heading(new QLabel(this)),
      profile_picture(new QToolButton(this)), profile_name(new QLabel(this)), clock(new QLabel(this)),
      network_icon(new QLabel(this)), sort_label(new QLabel(collection_bar)),
      empty_message(new QLabel(library_body)), loading_panel(new QWidget(library_body)),
      loading_covers(new LibraryLoadingCovers(loading_panel)),
      loading_message(new QLabel(loading_panel)), tile_halo(new FocusHalo(view->viewport())),
      title_bubble(new CoverTitleBubble(this)), transition_overlay(new LibraryPageTransition(this)),
      sort_button(new QPushButton(sort_rail)),
      back_button(new QPushButton(collection_bar)), sort_menu(new QMenu(sort_button)),
      sort_actions(new QActionGroup(sort_menu)), clock_timer(new QTimer(this)),
      refresh_timer(new QTimer(this)), page_transition(new QVariantAnimation(this)) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 16, 24, 12);
    layout->setSpacing(10);

    auto* status = new QHBoxLayout(status_bar);
    status->setContentsMargins(0, 0, 0, 0);
    status->addWidget(profile_picture, 0, Qt::AlignVCenter);
    status->addWidget(profile_name, 0, Qt::AlignVCenter);
    status->addStretch();
    status->addWidget(clock, 0, Qt::AlignVCenter);
    status->addWidget(network_icon, 0, Qt::AlignVCenter);
    layout->addWidget(status_bar);
    profile_picture->setFixedSize(AvatarSide, AvatarSide);
    profile_picture->setIconSize(QSize(AvatarSide, AvatarSide));
    profile_picture->setFocusPolicy(Qt::StrongFocus);
    profile_picture->setStyleSheet(QStringLiteral("QToolButton { border: 0; padding: 0; background: transparent; }"));
    profile_picture->setProperty("focusHaloRadius", AvatarSide / 2.0);
    profile_picture->setProperty("controllerMenuAbove", true);
    profile_picture->installEventFilter(this);
    connect(profile_picture, &QToolButton::clicked, this, &GameHome::ProfileRequested);
    network_icon->setFixedSize(24, 22);
    network_icon->setAlignment(Qt::AlignCenter);

    QFont heading_font = font();
    heading_font.setBold(true);
    if (heading_font.pixelSize() > 0) {
        heading_font.setPixelSize(qRound(heading_font.pixelSize() * 1.22));
    } else {
        heading_font.setPointSizeF(heading_font.pointSizeF() * 1.22);
    }
    heading->setFont(heading_font);
    clock->setFont(heading_font);
    collection_header->setContentsMargins(0, 0, 0, 0);
    collection_header->addWidget(back_button);
    collection_header->addWidget(heading);
    collection_header->addStretch();
    collection_header->addWidget(sort_label);
    layout->addWidget(collection_bar);

    library_row->setContentsMargins(0, 0, 0, 0);
    auto* rail = new QVBoxLayout(sort_rail);
    rail->setContentsMargins(0, TileInset, 0, 0);
    rail->addWidget(sort_button);
    rail->addStretch();
    library_row->addWidget(sort_rail);
    layout->addWidget(library_body, 1);
    library_body->installEventFilter(this);
    empty_message->setAlignment(Qt::AlignCenter);
    empty_message->setAttribute(Qt::WA_TransparentForMouseEvents);
    empty_message->hide();
    loading_message->setObjectName(QStringLiteral("gameLibraryLoading"));
    loading_message->setAlignment(Qt::AlignCenter);
    loading_message->setWordWrap(true);
    loading_panel->hide();

    view->setModel(tiles);
    page_transition->setStartValue(1.0);
    page_transition->setEndValue(0.0);
    page_transition->setDuration(230);
    page_transition->setEasingCurve(QEasingCurve::InOutSine);
    connect(page_transition, &QVariantAnimation::valueChanged, transition_overlay,
            [this](const QVariant& value) { transition_overlay->SetOpacity(value.toReal()); });
    connect(page_transition, &QVariantAnimation::finished, this, [this] {
        transition_overlay->Clear();
        UpdateTileFocus();
    });
    view->setItemDelegate(new TileDelegate(view));
    view->setViewMode(QListView::IconMode);
    view->setMovement(QListView::Static);
    view->setResizeMode(QListView::Adjust);
    view->setUniformItemSizes(true);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    view->setMouseTracking(true);
    view->setFrameShape(QFrame::NoFrame);
    view->setContextMenuPolicy(Qt::CustomContextMenu);
    view->installEventFilter(this);
    view->viewport()->installEventFilter(this);
    sort_button->installEventFilter(this);
    back_button->installEventFilter(this);
    setFocusProxy(view);

    for (const Sort order :
         {Sort::Recent, Sort::Longest, Sort::Shortest, Sort::Name, Sort::Publisher}) {
        auto* action = sort_menu->addAction(SortName(order));
        sort_actions->addAction(action);
        action->setCheckable(true);
        action->setChecked(order == sort);
        action->setData(static_cast<int>(order));
    }
    connect(sort_actions, &QActionGroup::triggered, this, [this](QAction* action) {
        const auto order = static_cast<Sort>(action->data().toInt());
        if (order == sort) {
            return;
        }
        sort = order;
        collection_selection = {};
        Refresh(true);
        Retranslate();
        QTimer::singleShot(0, this, [this] {
            if (collection && !loading && isVisible()) {
                FocusLibrary();
            }
        });
    });
    connect(sort_button, &QPushButton::clicked, this, [this] {
        sort_button->setFocus();
        sort_menu->popup(sort_button->mapToGlobal(QPoint(0, sort_button->height())));
        sort_menu->setActiveAction(sort_actions->checkedAction());
    });
    connect(back_button, &QPushButton::clicked, this, [this] { ShowCollection(false); });
    back_button->setProperty("controllerMenuAbove", true);
    back_button->setProperty("controllerBackButton", true);
    connect(view, &QListView::clicked, this, [this](const QModelIndex& index) {
        const auto source = SourceIndex(index);
        if (source.data(GameListItemPath::SuspendedRole).toBool() || (!collection && !source.isValid())) {
            Activate(index);
        }
    });
    connect(view, &QListView::doubleClicked, this, [this](const QModelIndex& index) {
        if (const auto source = SourceIndex(index); source.isValid()) {
            emit GameActivated(source);
        }
    });
    connect(view->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current) {
                if (current.isValid()) {
                    GameKey key{current.data(GameListItemPath::FullPathRole).toString(),
                                current.data(GameListItemPath::ProgramIdRole).toULongLong()};
                    if (collection) {
                        collection_selection = key;
                    } else {
                        shelf_selection = key;
                        shelf_all_games = key.first.isEmpty();
                    }
                }
                UpdateTileFocus();
            });
    connect(view->horizontalScrollBar(), &QScrollBar::valueChanged, this, &GameHome::UpdateTileFocus);
    connect(view->verticalScrollBar(), &QScrollBar::valueChanged, this, &GameHome::UpdateTileFocus);

    clock_timer->setInterval(5000);
    connect(clock_timer, &QTimer::timeout, this, &GameHome::UpdateStatus);
    refresh_timer->setSingleShot(true);
    connect(refresh_timer, &QTimer::timeout, this, [this] { Refresh(); });
    connect(library, &QAbstractItemModel::dataChanged, this, [this] { QueueRefresh(); });
    ShowCollection(false);
}

QListView* GameHome::View() const {
    return view;
}

QWidget* GameHome::NavigationTarget() const {
    return collection ? static_cast<QWidget*>(back_button) : profile_picture;
}

bool GameHome::ReturnToShelf(bool animate) {
    if (!collection) {
        return false;
    }
    mouse_navigation = false;
    ShowCollection(false, animate);
    return true;
}

bool GameHome::OpenSelectedGameMenu() {
    if (loading || !view->hasFocus()) {
        return false;
    }
    const auto tile = view->currentIndex();
    const auto source = SourceIndex(tile);
    if (!source.isValid()) {
        return false;
    }

    mouse_navigation = false;
    hover_position.reset();
    auto* viewport = view->viewport();
    const QRect area = viewport->rect();
    const QRect visible = area.intersected(view->visualRect(tile));
    const QPoint anchor = visible.isEmpty() ? area.center() : visible.center();
    emit GameMenuRequested(source, viewport->mapToGlobal(anchor));
    UpdateTileFocus();
    return true;
}

QModelIndex GameHome::SourceIndex(const QModelIndex& index) const {
    if (index.model() != tiles) {
        return {};
    }
    return index.data(SourceIndexRole).value<QPersistentModelIndex>();
}

QModelIndex GameHome::GetFilterResult() {
    if (refresh_timer->isActive()) {
        Refresh();
    }
    // The All Games tile has no source row and does not count as a search result.
    if (SourceIndex(tiles->index(1, 0)).isValid()) {
        return {};
    }
    return SourceIndex(tiles->index(0, 0));
}

QPair<QRect, QPixmap> GameHome::CoverForGame(const QString& filename, qulonglong program_id,
                                          const QWidget& surface) const {
    if (!isVisible() || !view->isVisible() || filename.isEmpty()) {
        return {};
    }

    const QFileInfo file(filename);
    const auto game_path = RecentPath(file.absoluteFilePath());
    const auto loose_path = RecentPath(file.fileName()) == QStringLiteral("main")
                                ? RecentPath(file.absolutePath())
                                : QString{};
    const auto cover_for = [&](const QModelIndex& index) -> QPair<QRect, QPixmap> {
        const auto source = SourceIndex(index);
        if (!source.isValid() ||
            (program_id != 0 &&
             source.data(GameListItemPath::ProgramIdRole).toULongLong() != program_id)) {
            return {};
        }
        const auto path = source.data(GameListItemPath::FullPathRole).toString();
        if (path.isEmpty()) {
            return {};
        }
        const auto entry = RecentPath(QFileInfo(path).absoluteFilePath());
        if (entry != game_path && entry != loose_path) {
            return {};
        }

        auto cover = index.data(Qt::DecorationRole).value<QPixmap>();
        const auto bounds = TileCoverRect(view->visualRect(index), cover);
        const auto visible = bounds.intersected(view->viewport()->rect());
        if (cover.isNull() || visible.isEmpty()) {
            return {};
        }
        if (visible != bounds) {
            const qreal x_scale = qreal(cover.width()) / bounds.width();
            const qreal y_scale = qreal(cover.height()) / bounds.height();
            const QRectF crop((visible.x() - bounds.x()) * x_scale,
                              (visible.y() - bounds.y()) * y_scale, visible.width() * x_scale,
                              visible.height() * y_scale);
            cover = cover.copy(crop.toAlignedRect().intersected(cover.rect()));
        }
        return {QRect(view->viewport()->mapTo(&surface, visible.topLeft()), visible.size()),
                std::move(cover)};
    };

    if (auto cover = cover_for(view->currentIndex()); !cover.first.isEmpty()) {
        return cover;
    }
    for (int row = 0; row < tiles->rowCount(); ++row) {
        if (auto cover = cover_for(tiles->index(row, 0)); !cover.first.isEmpty()) {
            return cover;
        }
    }
    return {};
}

void GameHome::FocusLibrary() {
    view->setFocus();
}

void GameHome::FocusSuspendedGame() {
    const auto matches = tiles->match(tiles->index(0, 0), GameListItemPath::SuspendedRole, true,
                                      1, Qt::MatchExactly);
    if (!matches.isEmpty()) {
        view->setCurrentIndex(matches.front());
        view->doItemsLayout();
        view->scrollTo(matches.front());
    }

    FocusLibrary();
}

void GameHome::QueueRefresh() {
    if (isVisible() && isEnabled() && !loading) {
        refresh_timer->start();
    }
}

void GameHome::Refresh(bool reset_position) {
    refresh_timer->stop();
    if (!isEnabled() || loading) {
        return;
    }

    const auto selection = collection ? collection_selection : shelf_selection;
    const bool restore_all_games = shelf_all_games;
    const QPoint scroll_position(view->horizontalScrollBar()->value(),
                                 view->verticalScrollBar()->value());

    struct Entry {
        QPersistentModelIndex source;
        GameKey key;
        QString title;
        QString publisher;
        qulonglong time;
        qsizetype recent;
    };
    QHash<QString, qsizetype> recent_ranks;
    const auto& recent_files = UISettings::values.recent_files;
    for (qsizetype i = recent_files.size(); i > 0; --i) {
        recent_ranks.insert(RecentPath(recent_files.at(i - 1)), i - 1);
    }

    std::vector<Entry> entries;
    QSet<GameKey> seen;
    constexpr auto unplayed = std::numeric_limits<qsizetype>::max();
    int total = 0;
    for (int row = 0; row < library->rowCount(); ++row) {
        const auto* item = library->item(row);
        if (item->data(GameListItem::TypeRole).value<GameListItemType>() !=
            GameListItemType::Game) {
            continue;
        }
        const QString path = item->data(GameListItemPath::FullPathRole).toString();
        const QString key = RecentPath(path);
        const auto program_id = item->data(GameListItemPath::ProgramIdRole).toULongLong();
        if (seen.contains({key, program_id})) {
            continue;
        }
        seen.insert({key, program_id});
        ++total;
        if (!Lichii::FilterMatches(filter, item)) {
            continue;
        }
        const auto time = item->index().siblingAtColumn(GameListModel::COLUMN_PLAY_TIME);
        QString title = item->data(GameListItemPath::TitleRole).toString().simplified();
        if (title.isEmpty()) {
            title = QFileInfo(path).completeBaseName();
        }
        const auto recent =
            recent_ranks.value(key, recent_ranks.value(key + QStringLiteral("/main"), unplayed));
        entries.push_back({QPersistentModelIndex(item->index()),
                           {path, program_id},
                           title,
                           item->data(GameListItemPath::PublisherRole).toString(),
                           time.data(GameListItemPlayTime::PlayTimeRole).toULongLong(),
                           recent});
    }

    const Sort order = collection ? sort : Sort::Recent;
    const auto& language = UISettings::values.language.GetValue();
    QCollator collator(language.empty() ? QLocale{} : QLocale(QString::fromStdString(language)));
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);
    std::ranges::sort(entries, [&](const Entry& lhs, const Entry& rhs) {
        if (order == Sort::Recent && lhs.recent != rhs.recent) {
            return lhs.recent < rhs.recent;
        }
        if (lhs.time != rhs.time) {
            if (order == Sort::Longest) {
                return lhs.time > rhs.time;
            }
            if (order == Sort::Shortest) {
                return lhs.time < rhs.time;
            }
        }
        if (order == Sort::Publisher) {
            if (lhs.publisher.isEmpty() != rhs.publisher.isEmpty()) {
                return !lhs.publisher.isEmpty();
            }
            if (const int compare = collator.compare(lhs.publisher, rhs.publisher); compare != 0) {
                return compare < 0;
            }
        }
        if (const int compare = collator.compare(lhs.title, rhs.title); compare != 0) {
            return compare < 0;
        }
        return lhs.key < rhs.key;
    });

    int restore = 0;
    tiles->clear();
    const int count = static_cast<int>(entries.size());
    const int shown = collection ? count : std::min(count, ShelfLimit);
    // Keep a game in progress on the shelf even when it was launched outside the shelf.
    if (!collection) {
        const auto suspended = std::ranges::find_if(entries, [](const Entry& entry) {
            return entry.source.data(GameListItemPath::SuspendedRole).toBool();
        });
        if (suspended != entries.end() && std::distance(entries.begin(), suspended) >= shown) {
            std::rotate(entries.begin(), suspended, std::next(suspended));
        }
    }

    QPixmap placeholder;
    for (int i = 0; i < shown; ++i) {
        const auto& entry = entries[i];
        auto* tile = new QStandardItem(entry.title);
        QPixmap art = entry.source.data(Qt::DecorationRole).value<QPixmap>();
        if (art.isNull() || !entry.source.data(GameListItemPath::HasIconRole).toBool()) {
            if (placeholder.isNull()) {
                placeholder = GetDefaultIcon(HomeCoverSize, true);
            }
            art = placeholder;
        }
        tile->setData(art, Qt::DecorationRole);
        tile->setData(QVariant::fromValue(entry.source), SourceIndexRole);
        tile->setData(entry.key.first, GameListItemPath::FullPathRole);
        tile->setData(entry.key.second, GameListItemPath::ProgramIdRole);
        if (entry.source.data(GameListItemPath::SuspendedRole).toBool()) {
            tile->setData(true, GameListItemPath::SuspendedRole);
            tile->setAccessibleDescription(entry.source.data(Qt::AccessibleDescriptionRole).toString());
        }
        if (entry.key == selection) {
            restore = i;
        }
        tiles->appendRow(tile);
    }
    if (!collection && total > 0) {
        auto* all_games = new QStandardItem(tr("All Games"));
        tiles->appendRow(all_games);
        if (restore_all_games) {
            restore = shown;
        }
    }
    UpdateTileSizes();
    view->doItemsLayout();
    const auto next = tiles->index(restore, 0);
    view->setCurrentIndex(next);
    static_cast<ShelfView*>(view)->RestoreScroll(reset_position ? QPoint{} : scroll_position);
    empty_message->setVisible(!next.isValid());
    empty_message->raise();
    UpdateTileFocus();
    emit FilterResultReady(count, total);
}

void GameHome::SetFilter(const QString& text) {
    if (filter != text) {
        filter = text;
        QueueRefresh();
    }
}

void GameHome::SetLoading(bool show) {
    if (loading == show) {
        return;
    }
    loading = show;
    if (loading) {
        StopPageTransition();
        empty_message->hide();
        title_bubble->hide();
        tile_halo->hide();
        UpdateTileSizes();
        loading_panel->raise();
    }
    view->setVisible(!loading);
    sort_rail->setVisible(collection && !loading);
    loading_panel->setVisible(loading);
}

QString GameHome::SortName(Sort order) const {
    switch (order) {
    case Sort::Recent:
        return tr("Recently Played");
    case Sort::Longest:
        return tr("Play Time (Longest First)");
    case Sort::Shortest:
        return tr("Play Time (Shortest First)");
    case Sort::Name:
        return tr("Software Name");
    case Sort::Publisher:
        return tr("Publisher");
    }
    return {};
}

void GameHome::Retranslate() {
    heading->setText(collection ? tr("All Games") : tr("Game Library"));
    view->setAccessibleName(heading->text());
    profile_picture->setAccessibleName(tr("User profile"));
    empty_message->setText(tr("No matching games"));
    loading_message->setText(tr("Scanning game library…"));
    back_button->setText(tr("Back"));
    sort_button->setText(tr("Sort"));
    UpdateCollectionButtons();
    sort_label->setText(SortName(sort));
    for (auto* action : sort_actions->actions()) {
        action->setText(SortName(static_cast<Sort>(action->data().toInt())));
    }
}

void GameHome::ShowCollection(bool show, bool animate) {
    QPixmap outgoing = animate && collection != show && isVisible() && !loading ? grab() : QPixmap{};
    StopPageTransition();
    collection = show;
    if (show) {
        collection_selection = {};
    } else {
        shelf_selection = {};
        shelf_all_games = false;
    }
    hover_position.reset();
    view->setCurrentIndex({});
    status_bar->setVisible(!show);
    collection_bar->setVisible(show);
    sort_rail->setVisible(show && !loading);
    if (show) {
        clock_timer->stop();
    } else if (isVisible()) {
        UpdateStatus();
        clock_timer->start();
    }
    view->setFlow(QListView::LeftToRight);
    view->setWrapping(show);
    view->setVerticalScrollBarPolicy(show ? Qt::ScrollBarAlwaysOn : Qt::ScrollBarAlwaysOff);
    if (show) {
        library_row->addWidget(view, 1);
    } else {
        library_row->removeWidget(view);
    }
    Retranslate();
    UpdateTileSizes();
    if (isVisible() && !loading) {
        Refresh(true);
        FocusLibrary();
        if (!outgoing.isNull()) {
            transition_overlay->SetFrame(std::move(outgoing));
            page_transition->start();
            UpdateTileFocus();
        }
    }
}

void GameHome::Activate(const QModelIndex& index) {
    if (const auto source = SourceIndex(index); source.isValid()) {
        emit GameActivated(source);
    } else if (!collection && index.isValid()) {
        ShowCollection(true);
    }
}

void GameHome::FocusFromSearch() {
    if (loading) {
        return;
    }
    auto target = view->currentIndex();
    if (collection || !target.isValid()) {
        target = tiles->index(collection ? tiles->rowCount() - 1 : 0, 0);
    }
    if (target.isValid()) {
        FocusLibrary();
        view->setCurrentIndex(target);
        view->scrollTo(target);
    }
}

bool GameHome::HandleKey(Qt::Key key, bool repeated) {
    if (mouse_navigation) {
        mouse_navigation = false;
        hover_position.reset();
        UpdateTileFocus();
    }
    if (key == Qt::Key_Escape && ReturnToShelf()) {
        return true;
    }
    if (collection && key == Qt::Key_F) {
        sort_button->click();
        return true;
    }
    if (profile_picture->hasFocus()) {
        if (key == Qt::Key_Down) {
            const auto first = tiles->index(0, 0);
            view->setCurrentIndex(first);
            if (first.isValid()) {
                view->scrollTo(first);
            }
            FocusLibrary();
            return true;
        }
        if (key == Qt::Key_Enter || key == Qt::Key_Return) {
            profile_picture->click();
            return true;
        }
        return false;
    }
    if (sort_button->hasFocus() || back_button->hasFocus()) {
        if (sort_button->hasFocus() && (key == Qt::Key_Up || key == Qt::Key_Left)) {
            back_button->setFocus();
            return true;
        }
        if (back_button->hasFocus() && key == Qt::Key_Down) {
            sort_button->setFocus();
            return true;
        }
        if (key == Qt::Key_Right) {
            FocusLibrary();
            return true;
        }
        if (key == Qt::Key_Enter || key == Qt::Key_Return) {
            (sort_button->hasFocus() ? sort_button : back_button)->click();
            return true;
        }
        return false;
    }

    if (!collection && (key == Qt::Key_Up || key == Qt::Key_Escape)) {
        profile_picture->setFocus();
        return true;
    }
    if (!collection && key == Qt::Key_Down) {
        emit SearchFocusRequested();
        return true;
    }

    const int row = view->currentIndex().row();
    if (key == Qt::Key_Menu && view->hasFocus()) {
        OpenSelectedGameMenu();
        return true;
    }
    if (key == Qt::Key_Enter || key == Qt::Key_Return) {
        const auto tile = view->currentIndex();
        if (tile.isValid()) {
            Activate(tile);
            return true;
        }
        return false;
    }
    if (collection && key == Qt::Key_Left && (row < 0 || row % columns == 0)) {
        if (!repeated) {
            sort_button->setFocus();
        }
        return true;
    }
    const int last = tiles->rowCount() - 1;
    int next = std::max(0, row);
    if (key == Qt::Key_Left) {
        next = std::max(0, row - 1);
    } else if (key == Qt::Key_Right) {
        next = collection && row >= 0 && (row + 1) % columns == 0 ? row : row + 1;
    } else if (collection && key == Qt::Key_Up) {
        if (row < columns) {
            if (!repeated) {
                back_button->setFocus();
            }
            return true;
        }
        next = row - columns;
    } else if (collection && key == Qt::Key_Down) {
        if (row >= 0 && row / columns == last / columns) {
            if (!repeated) {
                emit SearchFocusRequested();
            }
            return true;
        }
        next = row < 0 ? 0 : row + columns;
    } else {
        return false;
    }
    if (last >= 0) {
        view->setCurrentIndex(tiles->index(std::clamp(next, 0, last), 0));
    }
    return true;
}

bool GameHome::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->modifiers() == Qt::NoModifier &&
            HandleKey(static_cast<Qt::Key>(key->key()), key->isAutoRepeat())) {
            return true;
        }
    }
    if (watched == view) {
        if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut) {
            UpdateTileFocus();
        }
    } else if (watched == view->viewport()) {
        switch (event->type()) {
        case QEvent::Resize:
            if (collection) {
                UpdateTileSizes();
            }
            QTimer::singleShot(0, this, &GameHome::UpdateTileFocus);
            break;
        case QEvent::MouseMove:
        case QEvent::MouseButtonPress:
            mouse_navigation = true;
            hover_position = static_cast<QMouseEvent*>(event)->position().toPoint();
            UpdateTileFocus();
            break;
        case QEvent::Wheel:
            mouse_navigation = true;
            hover_position = static_cast<QWheelEvent*>(event)->position().toPoint();
            UpdateTileFocus();
            break;
        case QEvent::Leave:
            hover_position.reset();
            UpdateTileFocus();
            break;
        default:
            break;
        }
    } else if ((watched == back_button || watched == sort_button || watched == profile_picture) &&
               event->type() == QEvent::MouseButtonPress) {
        mouse_navigation = true;
        hover_position.reset();
        UpdateTileFocus();
    } else if (watched == library_body &&
               (event->type() == QEvent::Resize || event->type() == QEvent::Move)) {
        UpdateTileSizes();
        QTimer::singleShot(0, this, &GameHome::UpdateTileFocus);
    }
    return QWidget::eventFilter(watched, event);
}

void GameHome::UpdateTileSizes() {
    empty_message->setGeometry(library_body->rect());
    loading_panel->setGeometry(library_body->rect());
    const int available_height = library_body->height();
    const int available_width = library_body->width();
    const int shelf_cell = std::min({std::clamp(available_width / 5, 82, 230), available_width,
                                     available_height});
    const int center = height() / 2 - library_body->y();
    if (loading) {
        const int label_height =
            std::min(available_height, std::max(loading_message->sizeHint().height(),
                                                loading_message->heightForWidth(available_width)));
        const int height_for_covers = std::max(0, available_height - label_height - TileInset);
        const int cell = std::clamp(shelf_cell, 0, height_for_covers);
        const int width = std::min(available_width, (ShelfLimit + 1) * cell);
        const int gap = cell > 2 * TileInset ? TileInset : 0;
        const int top = std::clamp(center - cell / 2, 0, available_height - label_height - gap - cell);
        loading_covers->setGeometry((available_width - width) / 2, top, width, cell);
        loading_message->setGeometry(0, top + cell + gap, available_width, label_height);
    }
    if (collection) {
        const int available = view->viewport()->width();
        columns = std::clamp(available / 128, 1, 8);
        const int cell = std::max(TileInset * 2 + 1, available / columns);
        if (view->gridSize() != QSize(cell, cell)) {
            view->setGridSize(QSize(cell, cell));
        }
        const int grid_left = view->viewport()->mapTo(library_body, QPoint{}).x();
        const int grid_right = grid_left + columns * cell - TileInset;
        const int right_margin = std::max(0, library_body->width() - grid_right);
        if (collection_header->contentsMargins().right() != right_margin) {
            collection_header->setContentsMargins(0, 0, right_margin, 0);
        }
    } else {
        const int cell = shelf_cell;
        if (cell <= 2 * TileInset) {
            view->setGeometry(library_body->rect());
            return;
        }
        view->setGridSize(QSize(cell, cell));
        const int shelf_width = std::min(available_width, std::max(1, tiles->rowCount()) * cell);
        const int y = std::clamp(center - cell / 2, 0, available_height - cell);
        view->setGeometry((available_width - shelf_width) / 2, y, shelf_width, cell);
    }
}

void GameHome::UpdateTileFocus() {
    if (loading || !view->isVisible() || QApplication::activePopupWidget() ||
        page_transition->state() == QAbstractAnimation::Running) {
        tile_halo->hide();
        title_bubble->hide();
        return;
    }
    auto* viewport = view->viewport();
    const auto index = mouse_navigation
                           ? (hover_position ? view->indexAt(*hover_position) : QModelIndex{})
                           : (view->hasFocus() ? view->currentIndex() : QModelIndex{});
    const auto tile = index.isValid() ? view->visualRect(index) : QRect{};
    const auto cover = TileCoverRect(tile, index.data(Qt::DecorationRole).value<QPixmap>());
    const auto visible = cover.intersected(viewport->rect());
    if (visible.isEmpty() ||
        (mouse_navigation && (!hover_position || !visible.contains(*hover_position)))) {
        tile_halo->hide();
        title_bubble->hide();
        return;
    }

    const qreal radius = std::min(cover.width(), cover.height()) * GameIconCornerRatio;
    tile_halo->ShowAround(cover, radius, mouse_navigation ? FocusHalo::Weight::Thin
                                                         : FocusHalo::Weight::Standard);
    const auto origin = viewport->mapTo(this, QPoint{});
    const bool first_row = collection && tile.top() <= TileInset;
    title_bubble->ShowTitle(index.data().toString(), visible.translated(origin), first_row);
}

void GameHome::StopPageTransition() {
    page_transition->stop();
    transition_overlay->Clear();
}

void GameHome::UpdateCollectionButtons() {
    const QSize icon_size(LibraryButtonIconSize, LibraryButtonIconSize);
    const auto arrow = ThemePixmap("chevron_left", icon_size, devicePixelRatioF());
    back_button->setIcon(arrow.isNull() ? style()->standardIcon(QStyle::SP_ArrowBack) : QIcon(arrow));
    sort_button->setIcon(WidgetIcon("sliders"));
    for (auto* button : {back_button, sort_button}) {
        button->setIconSize(icon_size);
    }
    const QSize size = back_button->sizeHint().expandedTo(sort_button->sizeHint());
    back_button->setFixedSize(size);
    sort_button->setFixedSize(size);
}

void GameHome::UpdateProfile() {
    auto& profiles = system.GetProfileManager();
    const auto id = profiles.GetUser(Settings::values.current_user.GetValue());
    Service::Account::ProfileBase profile{};
    QString name;
    if (id && profiles.GetProfileBase(*id, profile)) {
        name = QString::fromStdString(Common::StringFromFixedZeroTerminatedBuffer(
            reinterpret_cast<const char*>(profile.username.data()), profile.username.size()));
    }
    const QPixmap avatar = id ? NewUserDialog::GetIcon(*id) : NewUserDialog::DefaultAvatar();
    QPixmap circle(AvatarSide * 2, AvatarSide * 2);
    circle.setDevicePixelRatio(2.0);
    circle.fill(Qt::transparent);
    QPainter painter(&circle);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    QPainterPath mask;
    mask.addEllipse(QRectF(0, 0, AvatarSide, AvatarSide));
    painter.setClipPath(mask);
    painter.fillRect(QRect(0, 0, AvatarSide, AvatarSide), palette().alternateBase());
    painter.drawPixmap(QRect(0, 0, AvatarSide, AvatarSide), avatar);
    painter.end();
    profile_picture->setIcon(QIcon(circle));
    static_cast<TileDelegate*>(view->itemDelegate())->SetProfileAvatar(avatar);
    view->viewport()->update();
    profile_name->setTextFormat(Qt::PlainText);
    profile_name->setText(name);
}

void GameHome::UpdateStatus() {
    clock->setText(QLocale().toString(QTime::currentTime(), QLocale::ShortFormat));
    const auto* network = QNetworkInformation::instance();
    const auto medium =
        network ? network->transportMedium() : QNetworkInformation::TransportMedium::Unknown;
    const auto reachability =
        network ? network->reachability() : QNetworkInformation::Reachability::Unknown;
    const bool disconnected = reachability == QNetworkInformation::Reachability::Disconnected;
    const bool wifi = !disconnected && medium == QNetworkInformation::TransportMedium::WiFi;
    auto color = palette().color(QPalette::WindowText);
    if (!wifi) {
        color.setAlpha(115);
    }
    network_icon->setToolTip(wifi           ? tr("Wi-Fi connected")
                             : disconnected ? tr("Network disconnected")
                             : medium == QNetworkInformation::TransportMedium::Ethernet
                                 ? tr("Connected via Ethernet")
                                 : tr("Wi-Fi status unavailable"));

    QPixmap icon(48, 44);
    icon.setDevicePixelRatio(2.0);
    icon.fill(Qt::transparent);
    QPainter painter(&icon);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap));
    constexpr int center_y = 17;
    for (const int radius : {5, 9, 13}) {
        painter.drawArc(QRectF(12 - radius, center_y - radius, 2 * radius, 2 * radius),
                        52 * 16, 76 * 16);
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawEllipse(QPointF(12, center_y), 1.25, 1.25);
    painter.end();
    network_icon->setPixmap(icon);
}

void GameHome::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        Retranslate();
        UpdateStatus();
        QueueRefresh();
    } else if (event->type() == QEvent::StyleChange) {
        UpdateCollectionButtons();
        UpdateStatus();
    } else if (event->type() == QEvent::FontChange) {
        UpdateCollectionButtons();
    }
    if (loading && (event->type() == QEvent::LanguageChange ||
                    event->type() == QEvent::StyleChange || event->type() == QEvent::FontChange)) {
        UpdateTileSizes();
    }
    QWidget::changeEvent(event);
}

void GameHome::hideEvent(QHideEvent* event) {
    clock_timer->stop();
    StopPageTransition();
    hover_position.reset();
    title_bubble->hide();
    tile_halo->hide();
    QWidget::hideEvent(event);
}

void GameHome::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    StopPageTransition();
    UpdateTileSizes();
}

void GameHome::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (!QNetworkInformation::instance()) {
        QNetworkInformation::loadDefaultBackend();
    }
    UpdateProfile();
    if (!collection) {
        UpdateStatus();
        clock_timer->start();
    }
    Refresh();
}

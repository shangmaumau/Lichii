// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>

#include <QBuffer>
#include <QByteArray>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QIODevice>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRadialGradient>
#include <QStyle>
#include <QStyleOption>
#include <QTime>
#include "core/loader/loader.h"
#include "ui_loading_screen.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/shader_warm/shader_warm_builder.h"
#include "lichii/loading_screen.h"

// Mingw seems to not have QMovie at all. If QMovie is missing then use a single frame instead of an
// showing the full animation
#if !LICHII_QT_MOVIE_MISSING
#include <QMovie>
#endif

namespace {

QSize TransitionRasterSize(const QSize& size, qreal scale) {
    constexpr QSize max_pixels(1600, 1000);
    QSize pixels = size * scale;
    if (pixels.width() > max_pixels.width() || pixels.height() > max_pixels.height()) {
        pixels.scale(max_pixels, Qt::KeepAspectRatio);
    }
    return pixels;
}

void PaintTransitionBackdrop(QPainter& painter, const QRectF& bounds) {
    painter.save();
    painter.translate(bounds.topLeft());
    painter.scale(bounds.width(), bounds.height());
    QRadialGradient glow(QPointF(0.53, 0.37), 0.65);
    glow.setColorAt(0.0, QColor(0x27, 0x49, 0x61));
    glow.setColorAt(1.0, QColor(0x0e, 0x18, 0x28));
    painter.fillRect(QRectF(0, 0, 1, 1), glow);
    painter.restore();
}

void PaintLoadingText(QPainter& painter, const QRectF& bounds, const QFont& font,
                      const QString& stage) {
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(bounds, Qt::AlignCenter, stage);
}

} // namespace

constexpr char PROGRESSBAR_STYLE_PREPARE[] = R"(
QProgressBar {}
QProgressBar::chunk {})";

constexpr char PROGRESSBAR_STYLE_BUILD[] = R"(
QProgressBar {
  background-color: black;
  border: 2px solid white;
  border-radius: 4px;
  padding: 2px;
}
QProgressBar::chunk {
  background-color: #ff3c28;
  width: 1px;
})";

constexpr char SKIP_BUTTON_STYLE[] = R"(
QPushButton {
  background-color: black;
  color: white;
  border: 2px solid white;
  border-radius: 6px;
  padding: 6px 24px;
  font: 12pt "Arial";
}
QPushButton:disabled {
  color: gray;
  border-color: gray;
})";

constexpr char PROGRESSBAR_STYLE_COMPLETE[] = R"(
QProgressBar {
  background-color: #0ab9e6;
  border: 2px solid white;
  border-radius: 4px;
  padding: 2px;
}
QProgressBar::chunk {
  background-color: #ff3c28;
})";

LoadingScreen::LoadingScreen(QWidget* parent)
    : QWidget(parent), ui(std::make_unique<Ui::LoadingScreen>()),
      previous_stage(VideoCore::LoadCallbackStage::Complete) {
    ui->setupUi(this);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    ui->fade_parent->installEventFilter(this);

    // Create a fade out effect to hide this loading screen widget.
    // When fading opacity, it will fade to the parent widgets background color, which is why we
    // create an internal widget named fade_widget that we use the effect on, while keeping the
    // loading screen widget's background color black. This way we can create a fade to black effect
    opacity_effect = new QGraphicsOpacityEffect(this);
    opacity_effect->setOpacity(1);
    ui->fade_parent->setGraphicsEffect(opacity_effect);
    fadeout_animation = std::make_unique<QPropertyAnimation>(opacity_effect, "opacity");
    fadeout_animation->setStartValue(1);
    fadeout_animation->setEndValue(0);
    fadeout_animation->setEasingCurve(QEasingCurve::InOutCubic);

    // After the fade completes, hide the widget and reset the opacity
    connect(fadeout_animation.get(), &QPropertyAnimation::finished, [this] {
        hide();
        opacity_effect->setOpacity(1);
        emit Hidden();
    });
    connect(this, &LoadingScreen::LoadProgress, this, &LoadingScreen::OnLoadProgress,
            Qt::QueuedConnection);

    // Shown while shaders are precached: starts the game without waiting for the rest.
    skip_button = new QPushButton(tr("Skip"), this);
    skip_button->setStyleSheet(QString::fromUtf8(SKIP_BUTTON_STYLE));
    skip_button->setToolTip(tr("Start the game now. The shaders not precached yet are precached "
                               "on a later launch."));
    skip_button->hide();
    ui->verticalLayout->insertWidget(2, skip_button, 0, Qt::AlignHCenter | Qt::AlignTop);
    connect(skip_button, &QPushButton::clicked, this, [this] {
        VideoCommon::ShaderWarm::RequestSkipWarm();
        skip_button->setEnabled(false);
    });
    qRegisterMetaType<VideoCore::LoadCallbackStage>();

    stage_translations = {
        {VideoCore::LoadCallbackStage::Prepare, tr("Loading...")},
        {VideoCore::LoadCallbackStage::Build, tr("Loading Shaders %1 / %2")},
        {VideoCore::LoadCallbackStage::Complete, tr("Launching...")},
        {VideoCore::LoadCallbackStage::Warm, tr("Precaching shaders %1 / %2")},
    };
    progressbar_style = {
        {VideoCore::LoadCallbackStage::Prepare, PROGRESSBAR_STYLE_PREPARE},
        {VideoCore::LoadCallbackStage::Build, PROGRESSBAR_STYLE_BUILD},
        {VideoCore::LoadCallbackStage::Complete, PROGRESSBAR_STYLE_COMPLETE},
        {VideoCore::LoadCallbackStage::Warm, PROGRESSBAR_STYLE_BUILD},
    };
}

LoadingScreen::~LoadingScreen() = default;

QPixmap LoadingScreen::TransitionFrame(const QSize& size) const {
    if (size.isEmpty()) {
        return {};
    }
    const auto pixels = TransitionRasterSize(size, devicePixelRatioF());
    ui->stage->ensurePolished();
    QPixmap frame(pixels);
    if (frame.isNull()) {
        return {};
    }
    QPainter painter(&frame);
    PaintTransitionBackdrop(painter, frame.rect());
    painter.scale(qreal(pixels.width()) / size.width(), qreal(pixels.height()) / size.height());
    PaintLoadingText(painter, QRectF(QPointF{}, size), ui->stage->font(), tr("Loading..."));
    return frame;
}

void LoadingScreen::Prepare(Loader::AppLoader& loader, bool use_transition_backdrop) {
    fadeout_animation->stop();
    opacity_effect->setOpacity(1);
    Clear();
    show_transition_backdrop = use_transition_backdrop;
    std::vector<u8> buffer;
    if (loader.ReadBanner(buffer) == Loader::ResultStatus::Success) {
#ifdef LICHII_QT_MOVIE_MISSING
        QPixmap map;
        map.loadFromData(buffer.data(), buffer.size());
        ui->banner->setPixmap(map);
#else
        backing_mem = std::make_unique<QByteArray>(reinterpret_cast<char*>(buffer.data()),
                                                   static_cast<int>(buffer.size()));
        backing_buf = std::make_unique<QBuffer>(backing_mem.get());
        backing_buf->open(QIODevice::ReadOnly);
        animation = std::make_unique<QMovie>(backing_buf.get(), QByteArray());
        animation->start();
        ui->banner->setMovie(animation.get());
#endif
        buffer.clear();
    }
    if (loader.ReadLogo(buffer) == Loader::ResultStatus::Success) {
        QPixmap map;
        map.loadFromData(buffer.data(), static_cast<uint>(buffer.size()));
        ui->logo->setPixmap(map);
    }

    slow_shader_compile_start = false;
    OnLoadProgress(VideoCore::LoadCallbackStage::Prepare, 0, 0);
}

void LoadingScreen::OnLoadComplete() {
    const int preferred_duration =
        style()->styleHint(QStyle::SH_Widget_Animation_Duration, nullptr, this);
    fadeout_animation->setDuration(std::clamp(preferred_duration, 0, 240));
    fadeout_animation->start(QPropertyAnimation::KeepWhenStopped);
}

void LoadingScreen::OnLoadProgress(VideoCore::LoadCallbackStage stage, std::size_t value,
                                   std::size_t total) {
    using namespace std::chrono;
    const auto now = steady_clock::now();
    // reset the timer if the stage changes
    if (stage != previous_stage) {
        ui->progress_bar->setStyleSheet(QString::fromUtf8(progressbar_style[stage]));
        // Hide the progress bar during the prepare stage
        if (stage == VideoCore::LoadCallbackStage::Prepare) {
            ui->progress_bar->hide();
        } else {
            ui->progress_bar->show();
        }
        skip_button->setVisible(stage == VideoCore::LoadCallbackStage::Warm);
        skip_button->setEnabled(true);
        previous_stage = stage;
        ui->fade_parent->update();
        // reset back to fast shader compiling since the stage changed
        slow_shader_compile_start = false;
    }
    // update the max of the progress bar if the number of shaders change
    if (total != previous_total) {
        ui->progress_bar->setMaximum(static_cast<int>(total));
        previous_total = total;
    }
    // Reset the progress bar ranges if compilation is done
    if (stage == VideoCore::LoadCallbackStage::Complete) {
        ui->progress_bar->setRange(0, 0);
    }

    QString estimate;
    // If there's a drastic slowdown in the rate, then display an estimate
    if (now - previous_time > milliseconds{50} || slow_shader_compile_start) {
        if (!slow_shader_compile_start) {
            slow_shader_start = steady_clock::now();
            slow_shader_compile_start = true;
            slow_shader_first_value = value;
        }
        // only calculate an estimate time after a second has passed since stage change
        const auto diff = duration_cast<milliseconds>(now - slow_shader_start);
        if (diff > seconds{1} && value > slow_shader_first_value) {
            const auto eta_mseconds =
                static_cast<long>(static_cast<double>(total - slow_shader_first_value) /
                                  (value - slow_shader_first_value) * diff.count());
            estimate =
                tr("Estimated Time %1")
                    .arg(QTime(0, 0, 0, 0)
                             .addMSecs(std::max<long>(eta_mseconds - diff.count() + 1000, 1000))
                             .toString(QStringLiteral("mm:ss")));
        }
    }

    // update labels and progress bar
    if (stage == VideoCore::LoadCallbackStage::Build ||
        stage == VideoCore::LoadCallbackStage::Warm) {
        ui->stage->setText(stage_translations[stage].arg(value).arg(total));
    } else {
        ui->stage->setText(stage_translations[stage]);
    }
    ui->value->setText(estimate);
    const bool transition_prepare =
        show_transition_backdrop && stage == VideoCore::LoadCallbackStage::Prepare;
    ui->stage->setVisible(!transition_prepare);
    ui->logo->setVisible(!transition_prepare);
    ui->banner->setVisible(!transition_prepare);
    ui->progress_bar->setValue(static_cast<int>(value));
    previous_time = now;
}

bool LoadingScreen::eventFilter(QObject* watched, QEvent* event) {
    if (watched == ui->fade_parent && event->type() == QEvent::Paint &&
        show_transition_backdrop) {
        const auto bounds = ui->fade_parent->rect();
        const auto pixels = TransitionRasterSize(bounds.size(), ui->fade_parent->devicePixelRatioF());
        if (transition_backdrop.size() != pixels) {
            transition_backdrop = QPixmap(pixels);
            QPainter painter(&transition_backdrop);
            PaintTransitionBackdrop(painter, transition_backdrop.rect());
        }
        QPainter painter(ui->fade_parent);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawPixmap(bounds, transition_backdrop);
        if (previous_stage == VideoCore::LoadCallbackStage::Prepare) {
            PaintLoadingText(painter, bounds, ui->stage->font(), ui->stage->text());
        }
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void LoadingScreen::paintEvent(QPaintEvent* event) {
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
    QWidget::paintEvent(event);
}

void LoadingScreen::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    ui->progress_bar->setMinimumWidth(std::min(width(), ui->progress_bar->maximumWidth()));
}

void LoadingScreen::Clear() {
    ui->banner->clear();
    ui->logo->clear();
    show_transition_backdrop = false;
    transition_backdrop = {};
#ifndef LICHII_QT_MOVIE_MISSING
    animation.reset();
    backing_buf.reset();
    backing_mem.reset();
#endif
}

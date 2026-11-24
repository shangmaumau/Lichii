// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2016 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <memory>
#include <tuple>
#include <QKeyEvent>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QStyle>
#include <QTabBar>
#include "common/logging.h"
#include "common/settings.h"
#include "common/settings_enums.h"
#include "core/core.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/util/vk.h"
#include "ui_configure.h"
#include "lichii/configuration/configuration_shared.h"
#include "lichii/configuration/configure_applets.h"
#include "lichii/configuration/configure_audio.h"
#include "lichii/configuration/configure_cpu.h"
#include "lichii/configuration/configure_debug_tab.h"
#include "lichii/configuration/configure_dialog.h"
#include "lichii/configuration/configure_filesystem.h"
#include "lichii/configuration/configure_general.h"
#include "lichii/configuration/configure_graphics.h"
#include "lichii/configuration/configure_graphics_advanced.h"
#include "lichii/configuration/configure_graphics_extensions.h"
#include "lichii/configuration/configure_hotkeys.h"
#include "lichii/configuration/configure_input.h"
#include "lichii/configuration/configure_input_player.h"
#include "lichii/configuration/configure_network.h"
#include "lichii/configuration/configure_navigation.h"
#include "lichii/configuration/configure_profile_manager.h"
#include "lichii/configuration/configure_system.h"
#include "lichii/configuration/configure_ui.h"
#include "lichii/configuration/configure_web.h"
#include "lichii/hotkeys.h"

Q_DECLARE_METATYPE(QList<QWidget*>);

static QString SettingsTabStyle(bool controls) {
    return controls ? QStringLiteral("QTabBar::tab { padding-left: 5px; padding-right: 5px; }")
                    : QString{};
}

class SettingsTabUnderline final : public QWidget {
public:
    explicit SettingsTabUnderline(QTabBar* tabs_)
        : QWidget(tabs_), tabs(tabs_), opacity(new QGraphicsOpacityEffect(this)),
          fade(opacity, "opacity", this) {
        tabs->setProperty("controllerTabNavigation", true);
        tabs->setProperty("controllerTabInputActive", false);
        setObjectName(QStringLiteral("settingsTabUnderline"));
        setAttribute(Qt::WA_StyledBackground);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        opacity->setOpacity(1.0);
        setGraphicsEffect(opacity);
        fade.setDuration(160);
        fade.setEndValue(1.0);
        fade.setEasingCurve(QEasingCurve::OutCubic);
        lower();
        tabs->installEventFilter(this);
        connect(tabs, &QTabBar::currentChanged, this, [this] { UpdateGeometry(); });
        UpdateGeometry();
    }

    void SetControllerActive(bool active) {
        if (controller_active == active) {
            return;
        }
        controller_active = active;
        tabs->setProperty("controllerTabInputActive", active);
        tabs->style()->unpolish(tabs);
        tabs->style()->polish(tabs);
        tabs->update();
        fade.stop();
        opacity->setOpacity(0.0);
        UpdateGeometry();
        if (!active) {
            fade.setStartValue(0.0);
            fade.start();
        }
    }

private:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == tabs && (event->type() == QEvent::Paint || event->type() == QEvent::Resize ||
                               event->type() == QEvent::Show ||
                               event->type() == QEvent::LayoutRequest ||
                               event->type() == QEvent::StyleChange)) {
            UpdateGeometry();
        }
        return QWidget::eventFilter(watched, event);
    }

    void UpdateGeometry() {
        const auto& theme = UISettings::values.theme;
        const QRect tab = tabs->tabRect(tabs->currentIndex());
        const bool show_line = !controller_active && !tab.isEmpty() &&
                               (theme.empty() || theme == "lichii");
        if (show_line) {
            setGeometry(tab.x(), tab.bottom() - 1, std::max(1, tab.width() - 2), 2);
        }
        setVisible(show_line);
    }

    QTabBar* tabs;
    QGraphicsOpacityEffect* opacity;
    QPropertyAnimation fade;
    bool controller_active{};
};

ConfigureDialog::ConfigureDialog(QWidget* parent, HotkeyRegistry& registry_,
                                 InputCommon::InputSubsystem* input_subsystem,
                                 std::vector<VkDeviceInfo::Record>& vk_device_records,
                                 Core::System& system_, bool enable_web_config)
    : QDialog(parent), ui{std::make_unique<Ui::ConfigureDialog>()}, registry(registry_),
      system{system_},
      builder{std::make_unique<ConfigurationShared::Builder>(this, !system_.IsPoweredOn())},
      applets_tab{std::make_unique<ConfigureApplets>(system_, nullptr, *builder, this)},
      audio_tab{std::make_unique<ConfigureAudio>(system_, nullptr, *builder, this)},
      cpu_tab{std::make_unique<ConfigureCpu>(system_, nullptr, *builder, this)},
      debug_tab_tab{std::make_unique<ConfigureDebugTab>(system_, this)},
      filesystem_tab{std::make_unique<ConfigureFilesystem>(this)},
      general_tab{std::make_unique<ConfigureGeneral>(system_, nullptr, *builder, this)},
      graphics_advanced_tab{
          std::make_unique<ConfigureGraphicsAdvanced>(system_, nullptr, *builder, this)},
      graphics_extensions_tab{
          std::make_unique<ConfigureGraphicsExtensions>(system_, nullptr, *builder, this)},
      ui_tab{std::make_unique<ConfigureUi>(system_, this)},
      graphics_tab{std::make_unique<ConfigureGraphics>(
          system_, vk_device_records, [&]() { graphics_advanced_tab->ExposeComputeOption(); },
          [this](Settings::AspectRatio ratio, Settings::ResolutionSetup setup) {
              ui_tab->UpdateScreenshotInfo(ratio, setup);
          },
          nullptr, *builder, this)},
      hotkeys_tab{std::make_unique<ConfigureHotkeys>(system_.HIDCore(), this)},
      input_tab{std::make_unique<ConfigureInput>(system_, this)},
      network_tab{std::make_unique<ConfigureNetwork>(system_, this)},
      profile_tab{std::make_unique<ConfigureProfileManager>(system_, this)},
      system_tab{std::make_unique<ConfigureSystem>(system_, nullptr, *builder, this)},
      web_tab{std::make_unique<ConfigureWeb>(this)} {
    Settings::SetConfiguringGlobal(true);

    ui->setupUi(this);
    ui->tabWidget->setUsesScrollButtons(true);
    tab_underline = new SettingsTabUnderline(ui->tabWidget->tabBar());

    ui->tabWidget->addTab(applets_tab.get(), tr("Applets"));
    ui->tabWidget->addTab(audio_tab.get(), tr("Audio"));
    ui->tabWidget->addTab(cpu_tab.get(), tr("CPU"));
    ui->tabWidget->addTab(debug_tab_tab.get(), tr("Debug"));
    ui->tabWidget->addTab(filesystem_tab.get(), tr("Filesystem"));
    ui->tabWidget->addTab(general_tab.get(), tr("General"));
    ui->tabWidget->addTab(graphics_tab.get(), tr("Graphics"));
    ui->tabWidget->addTab(graphics_advanced_tab.get(), tr("GraphicsAdvanced"));
    ui->tabWidget->addTab(graphics_extensions_tab.get(), tr("GraphicsExtra"));
    ui->tabWidget->addTab(hotkeys_tab.get(), tr("Hotkeys"));
    ui->tabWidget->addTab(input_tab.get(), tr("Controls"));
    ui->tabWidget->addTab(profile_tab.get(), tr("Profiles"));
    ui->tabWidget->addTab(network_tab.get(), tr("Network"));
    ui->tabWidget->addTab(system_tab.get(), tr("System"));
    ui->tabWidget->addTab(ui_tab.get(), tr("Game List"));
    ui->tabWidget->addTab(web_tab.get(), tr("Web"));

    web_tab->SetWebServiceConfigEnabled(enable_web_config);
    hotkeys_tab->Populate(registry);

    input_tab->Initialize(input_subsystem);

    general_tab->SetResetCallback([&] { this->close(); });

    SetConfiguration();

    const std::array<std::pair<QWidget*, const char*>, 15> tab_icons{{
        {general_tab.get(), "sliders"},
        {hotkeys_tab.get(), "keyboard"},
        {ui_tab.get(), "list"},
        {web_tab.get(), "cloud"},
        {debug_tab_tab.get(), "bug"},
        {system_tab.get(), "system"},
        {profile_tab.get(), "user"},
        {network_tab.get(), "globe"},
        {filesystem_tab.get(), "drive"},
        {applets_tab.get(), "apps"},
        {cpu_tab.get(), "cpu"},
        {graphics_tab.get(), "monitor"},
        {graphics_advanced_tab.get(), "layers"},
        {graphics_extensions_tab.get(), "wand"},
        {audio_tab.get(), "audio"},
    }};
    for (const auto& [tab, icon] : tab_icons) {
        tab->setProperty("tabIcon", QString::fromLatin1(icon));
        ConfigurationShared::UseToggles(tab);
    }
    PopulateSelectionList();

    connect(ui->tabWidget, &QTabWidget::currentChanged, this, [this](int index) {
        if (index != -1) {
            debug_tab_tab->SetCurrentIndex(0);
        }
    });
    connect(ui_tab.get(), &ConfigureUi::LanguageChanged, this, &ConfigureDialog::OnLanguageChanged);
    connect(general_tab.get(), &ConfigureGeneral::ExternalContentDirsChanged, this,
            &ConfigureDialog::ExternalContentDirsChanged);
    connect(ui->selectorList, &QListWidget::itemSelectionChanged, this,
            &ConfigureDialog::UpdateVisibleTabs);

    if (system.IsPoweredOn()) {
        QPushButton* apply_button = ui->buttonBox->addButton(QDialogButtonBox::Apply);
        connect(apply_button, &QAbstractButton::clicked, this,
                &ConfigureDialog::HandleApplyButtonClicked);
    }

    ui->selectorList->setFixedWidth(std::max(184, ui->selectorList->sizeHintForColumn(0) +
                                                      2 * ui->selectorList->frameWidth() + 32));
    FitToPages();
    ui->selectorList->setCurrentRow(0);
    ui->selectorList->setProperty("controllerHighlightItem", true);
    ui->selectorList->installEventFilter(this);
    ui->tabWidget->tabBar()->installEventFilter(this);
    ui->selectorList->setFocus();
}

ConfigureDialog::~ConfigureDialog() = default;

bool ConfigureDialog::HandleControllerNavigation(Qt::Key key) {
    return ConfigurationShared::NavigateSettings(key, *ui->selectorList, *ui->tabWidget,
                                                 *ui->buttonBox);
}

void ConfigureDialog::SetControllerInputActive(bool active) {
    tab_underline->SetControllerActive(active);
}

bool ConfigureDialog::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::KeyPress &&
        (watched == ui->selectorList || watched == ui->tabWidget->tabBar())) {
        const auto* key = static_cast<QKeyEvent*>(event);
        switch (key->key()) {
        case Qt::Key_Right:
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_Enter:
        case Qt::Key_Return:
            if (key->modifiers() == Qt::NoModifier &&
                HandleControllerNavigation(static_cast<Qt::Key>(key->key()))) {
                return true;
            }
            break;
        default:
            break;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void ConfigureDialog::SetConfiguration() {}

void ConfigureDialog::ApplyConfiguration() {
    general_tab->ApplyConfiguration();
    ui_tab->ApplyConfiguration();
    system_tab->ApplyConfiguration();
    profile_tab->ApplyConfiguration();
    filesystem_tab->ApplyConfiguration();
    input_tab->ApplyConfiguration();
    hotkeys_tab->ApplyConfiguration(registry);
    cpu_tab->ApplyConfiguration();
    graphics_tab->ApplyConfiguration();
    graphics_advanced_tab->ApplyConfiguration();
    graphics_extensions_tab->ApplyConfiguration();
    audio_tab->ApplyConfiguration();
    debug_tab_tab->ApplyConfiguration();
    web_tab->ApplyConfiguration();
    network_tab->ApplyConfiguration();
    applets_tab->ApplyConfiguration();
    system.ApplySettings();
    Settings::LogSettings();
}

void ConfigureDialog::RestoreInputConfiguration() {
    input_tab->RestoreConfiguration();
}

void ConfigureDialog::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }

    QDialog::changeEvent(event);
}

void ConfigureDialog::FitToPages() {
    const auto margins =
        ui->verticalLayout->contentsMargins() + ui->horizontalLayout->contentsMargins();
    const int frame_width = ui->tabWidget->style()->pixelMetric(QStyle::PM_DefaultFrameWidth, nullptr,
                                                               ui->tabWidget);
    const int chrome_width = margins.left() + margins.right() + ui->selectorList->width() +
                             ui->horizontalLayout->spacing() + 2 * frame_width;
    const QScrollBar scrollbar(Qt::Vertical, this);
    const int scrollbar_width = scrollbar.sizeHint().width();

    QTabBar tab_bar(ui->tabWidget);
    tab_bar.setIconSize(ui->tabWidget->iconSize());
    tab_bar.setShape(ui->tabWidget->tabBar()->shape());
    tab_bar.setProperty("controllerTabNavigation", true);
    const auto controls = input_tab->GetSubTabs();
    std::vector<std::pair<QWidget*, int>> page_widths;
    int pages_width = input_tab->MinimumPlayerFooterWidth();
    int tabs_width = 0;
    for (int row = 0; row < ui->selectorList->count(); ++row) {
        const auto tabs = ui->selectorList->item(row)->data(Qt::UserRole).value<QList<QWidget*>>();
        tab_bar.setStyleSheet(SettingsTabStyle(tabs == controls));
        for (QWidget* const page : tabs) {
            page->ensurePolished();
            const int page_width = page->minimumSizeHint().expandedTo(page->minimumSize()).width();
            page_widths.emplace_back(page, page_width);
            pages_width = std::max(pages_width, page_width);
            tab_bar.addTab(ConfigurationShared::PageIcon(page->property("tabIcon").toString()),
                           page->accessibleName());
        }
        tabs_width = std::max(tabs_width, tab_bar.sizeHint().width());
        while (tab_bar.count() > 0) {
            tab_bar.removeTab(tab_bar.count() - 1);
        }
    }

    const int content_width = std::max(tabs_width, pages_width + scrollbar_width);
    const int preferred_width = std::max(1040, chrome_width + content_width);
    ConfigurationShared::FitToScreen(this, QSize(preferred_width, 760));

    const int viewport_width = width() - chrome_width - scrollbar_width;
    for (const auto& [page, page_width] : page_widths) {
        if (page_width <= viewport_width) {
            continue;
        }
        page->setProperty("compactPage", true);
        for (auto* const button : page->findChildren<QPushButton*>()) {
            button->style()->unpolish(button);
            button->style()->polish(button);
        }
        page->updateGeometry();
    }
}

void ConfigureDialog::RetranslateUI() {
    const int old_row = ui->selectorList->currentRow();
    const int old_index = ui->tabWidget->currentIndex();

    ui->retranslateUi(this);

    PopulateSelectionList();
    ui->selectorList->setCurrentRow(old_row);

    UpdateVisibleTabs();
    ui->tabWidget->setCurrentIndex(old_index);
}

void ConfigureDialog::HandleApplyButtonClicked() {
    UISettings::values.configuration_applied = true;
    ApplyConfiguration();
}

void ConfigureDialog::SelectProfiles() {
    for (int row = 0; row < ui->selectorList->count(); ++row) {
        const auto tabs = ui->selectorList->item(row)->data(Qt::UserRole).value<QList<QWidget*>>();
        if (const auto profile_index = tabs.indexOf(profile_tab.get()); profile_index >= 0) {
            ui->selectorList->setCurrentRow(row);
            ui->tabWidget->setCurrentIndex(static_cast<int>(profile_index));
            ui->tabWidget->tabBar()->setFocus();
            return;
        }
    }
}

void ConfigureDialog::PopulateSelectionList() {
    const std::array<std::tuple<QString, const char*, QList<QWidget*>>, 6> items{
        {{tr("General"),
          "sliders",
          {general_tab.get(), hotkeys_tab.get(), ui_tab.get(), web_tab.get(), debug_tab_tab.get()}},
         {tr("System"),
          "system",
          {system_tab.get(), profile_tab.get(), network_tab.get(), filesystem_tab.get(),
           applets_tab.get()}},
         {tr("CPU"), "cpu", {cpu_tab.get()}},
         {tr("Graphics"),
          "monitor",
          {graphics_tab.get(), graphics_advanced_tab.get(), graphics_extensions_tab.get()}},
         {tr("Audio"), "audio", {audio_tab.get()}},
         {tr("Controls"), "gamepad", input_tab->GetSubTabs()}},
    };

    [[maybe_unused]] const QSignalBlocker blocker(ui->selectorList);

    ui->selectorList->clear();
    for (const auto& [name, icon, tabs] : items) {
        auto* const item =
            new QListWidgetItem(ConfigurationShared::PageIcon(QString::fromLatin1(icon)), name);
        item->setData(Qt::UserRole, QVariant::fromValue(tabs));

        ui->selectorList->addItem(item);
    }
}

void ConfigureDialog::OnLanguageChanged(const QString& locale) {
    emit LanguageChanged(locale);
    //  Reloading the game list is needed to force retranslation.
    UISettings::values.is_game_list_reload_pending = true;
    // first apply the configuration, and then restore the display
    ApplyConfiguration();
    RetranslateUI();
    SetConfiguration();
}

void ConfigureDialog::UpdateVisibleTabs() {
    const auto items = ui->selectorList->selectedItems();
    if (items.isEmpty()) {
        return;
    }

    [[maybe_unused]] const QSignalBlocker blocker(ui->tabWidget);

    ui->tabWidget->clear();

    const auto tabs = qvariant_cast<QList<QWidget*>>(items[0]->data(Qt::UserRole));
    ui->tabWidget->tabBar()->setStyleSheet(SettingsTabStyle(tabs == input_tab->GetSubTabs()));

    for (auto* const tab : tabs) {
        LOG_DEBUG(Frontend, "{}", tab->accessibleName().toStdString());
        auto& area = scroll_areas[tab];
        if (area == nullptr) {
            area = ConfigurationShared::ScrollTab(tab);
        }
        ui->tabWidget->addTab(area,
                              ConfigurationShared::PageIcon(tab->property("tabIcon").toString()),
                              tab->accessibleName());
    }
}

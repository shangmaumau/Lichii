// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>

#include <QModelIndex>
#include <QPair>
#include <QWidget>

class QActionGroup;
class CoverTitleBubble;
class FocusHalo;
class LibraryPageTransition;
class QHBoxLayout;
class QLabel;
class QListView;
class QMenu;
class QPushButton;
class QStandardItemModel;
class QTimer;
class QToolButton;
class QVariantAnimation;
class GameListModel;

namespace Core {
class System;
}

class GameHome final : public QWidget {
    Q_OBJECT

  public:
    GameHome(GameListModel* library, Core::System& system, QWidget* parent);

    QListView* View() const;
    QWidget* NavigationTarget() const;
    bool ReturnToShelf();
    QModelIndex SourceIndex(const QModelIndex& index) const;
    void FocusLibrary();
    void Refresh(bool reset_position = false);
    void UpdateProfile();
    void SetFilter(const QString& text);
    void SetLoading(bool loading);

  signals:
    void GameActivated(const QModelIndex& source_index);
    void FilterResultReady(int visible, int total);
    void ProfileRequested();

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void changeEvent(QEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

  private:
    using GameKey = QPair<QString, qulonglong>;
    enum class Sort { Recent, Longest, Shortest, Name, Publisher };

    void QueueRefresh();
    void UpdateTileSizes();
    void UpdateTileFocus();
    void UpdateStatus();
    void UpdateCollectionButtons();
    void StopPageTransition();
    void Retranslate();
    QString SortName(Sort order) const;
    void ShowCollection(bool show);
    void Activate(const QModelIndex& index);
    bool HandleKey(Qt::Key key);

    GameListModel* library;
    Core::System& system;
    QWidget* library_body;
    QListView* view;
    QStandardItemModel* tiles;
    QHBoxLayout* library_row;
    QWidget* status_bar;
    QWidget* collection_bar;
    QHBoxLayout* collection_header;
    QWidget* sort_rail;
    QLabel* heading;
    QToolButton* profile_picture;
    QLabel* profile_name;
    QLabel* clock;
    QLabel* network_icon;
    QLabel* sort_label;
    QLabel* empty_message;
    QWidget* loading_panel;
    QLabel* loading_message;
    FocusHalo* tile_halo;
    CoverTitleBubble* title_bubble;
    LibraryPageTransition* transition_overlay;
    QPushButton* sort_button;
    QPushButton* back_button;
    QMenu* sort_menu;
    QActionGroup* sort_actions;
    QTimer* clock_timer;
    QTimer* refresh_timer;
    QVariantAnimation* page_transition;

    Sort sort = Sort::Recent;
    QString filter;
    GameKey shelf_selection;
    GameKey collection_selection;
    std::optional<QPoint> hover_position;
    int columns = 1;
    bool collection = false;
    bool loading = false;
    bool mouse_navigation = false;
    bool shelf_all_games = false;
};

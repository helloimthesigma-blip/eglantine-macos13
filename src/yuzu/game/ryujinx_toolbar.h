// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QStyledItemDelegate>
#include <QWidget>

#include "common/common_types.h"
#include "common/settings_enums.h"

namespace PlayTime {
class PlayTimeManager;
}

class QComboBox;
class QLineEdit;
class QSlider;
class QToolButton;

/// The bar above the game list in the Ryujinx-style interface: list and grid buttons and an
/// icon size slider on the left, sorting and search on the right.
class RyujinxToolbar : public QWidget {
    Q_OBJECT

public:
    explicit RyujinxToolbar(QWidget* parent = nullptr);

    /// Shows the current view mode and icon size.
    void SyncFromSettings();

    void FocusSearch();
    void ClearSearch();

signals:
    void ViewModeRequested(Settings::GameListMode mode);
    void IconSizeRequested(u32 size);
    void SortRequested(int column, Qt::SortOrder order);
    void SearchChanged(const QString& text);

private:
    QToolButton* list_button{};
    QToolButton* grid_button{};
    QSlider* size_slider{};
    QComboBox* sort_box{};
    QLineEdit* search{};
};

/// Draws each game of the list view as a Ryujinx-style card: the icon, then the title, file name
/// and version, then the title ID and file type, then the play time and size on the right.
class RyujinxCardDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit RyujinxCardDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    /// For the last played and time played columns.
    static void SetPlayTimeManager(const PlayTime::PlayTimeManager* manager);

private:
    static const PlayTime::PlayTimeManager* play_time_manager;
};

/// "1 hour", "30 minutes": the largest unit, as Ryujinx's FormatTimeSpan.
[[nodiscard]] QString FormatRyujinxPlayTime(u64 seconds);

/// Whether the Ryujinx-style interface is selected (UISettings theme "ryujinx").
[[nodiscard]] bool IsRyujinxInterface();

// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>

#include <QComboBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QSlider>
#include <QToolButton>

#include "qt_common/config/uisettings.h"
#include "qt_common/game_list/game_list_p.h"
#include "qt_common/game_list/model.h"
#include "yuzu/game/ryujinx_toolbar.h"

namespace {
constexpr std::array<u32, 4> ICON_SIZES{32, 64, 128, 256};

struct SortChoice {
    const char* label;
    int column;
    Qt::SortOrder order;
};
constexpr std::array SORT_CHOICES{
    SortChoice{QT_TRANSLATE_NOOP("RyujinxToolbar", "Name"), GameListModel::COLUMN_NAME,
               Qt::AscendingOrder},
    SortChoice{QT_TRANSLATE_NOOP("RyujinxToolbar", "Name (Z-A)"), GameListModel::COLUMN_NAME,
               Qt::DescendingOrder},
    SortChoice{QT_TRANSLATE_NOOP("RyujinxToolbar", "Play Time"), GameListModel::COLUMN_PLAY_TIME,
               Qt::DescendingOrder},
    SortChoice{QT_TRANSLATE_NOOP("RyujinxToolbar", "File Size"), GameListModel::COLUMN_SIZE,
               Qt::DescendingOrder},
    SortChoice{QT_TRANSLATE_NOOP("RyujinxToolbar", "File Type"), GameListModel::COLUMN_FILE_TYPE,
               Qt::AscendingOrder},
};

QToolButton* MakeViewButton(QWidget* parent, const QString& glyph, const QString& tooltip) {
    auto* button = new QToolButton(parent);
    button->setText(glyph);
    button->setToolTip(tooltip);
    button->setCheckable(true);
    button->setAutoRaise(false);
    button->setFixedSize(36, 32);
    QFont font = button->font();
    font.setPointSize(16);
    button->setFont(font);
    return button;
}
} // Anonymous namespace

bool IsRyujinxInterface() {
    return UISettings::values.theme == std::string("ryujinx");
}

RyujinxToolbar::RyujinxToolbar(QWidget* parent) : QWidget{parent} {
    setObjectName(QStringLiteral("RyujinxToolbar"));
    setAttribute(Qt::WA_StyledBackground, true);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    list_button = MakeViewButton(this, QStringLiteral("☰"), tr("List view"));
    grid_button = MakeViewButton(this, QStringLiteral("⊞"), tr("Grid view"));
    connect(list_button, &QToolButton::clicked, this,
            [this] { emit ViewModeRequested(Settings::GameListMode::TreeView); });
    connect(grid_button, &QToolButton::clicked, this,
            [this] { emit ViewModeRequested(Settings::GameListMode::GridView); });

    size_slider = new QSlider(Qt::Horizontal, this);
    size_slider->setRange(0, static_cast<int>(ICON_SIZES.size()) - 1);
    size_slider->setPageStep(1);
    size_slider->setFixedWidth(150);
    size_slider->setToolTip(tr("Icon size"));
    connect(size_slider, &QSlider::valueChanged, this, [this](int value) {
        emit IconSizeRequested(ICON_SIZES[static_cast<size_t>(value)]);
    });

    sort_box = new QComboBox(this);
    for (const SortChoice& choice : SORT_CHOICES) {
        sort_box->addItem(tr(choice.label));
    }
    sort_box->setMinimumWidth(160);
    sort_box->setToolTip(tr("Sort by"));
    connect(sort_box, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0 && index < static_cast<int>(SORT_CHOICES.size())) {
            emit SortRequested(SORT_CHOICES[index].column, SORT_CHOICES[index].order);
        }
    });

    search = new QLineEdit(this);
    search->setPlaceholderText(tr("Search..."));
    search->setClearButtonEnabled(true);
    search->setMinimumWidth(260);
    connect(search, &QLineEdit::textChanged, this, &RyujinxToolbar::SearchChanged);

    layout->addWidget(list_button);
    layout->addWidget(grid_button);
    layout->addSpacing(8);
    layout->addWidget(size_slider);
    layout->addStretch(1);
    layout->addWidget(sort_box);
    layout->addWidget(search);

    SyncFromSettings();
}

void RyujinxToolbar::SyncFromSettings() {
    const auto mode = UISettings::values.game_list_mode.GetValue();
    list_button->setChecked(mode == Settings::GameListMode::TreeView);
    grid_button->setChecked(mode != Settings::GameListMode::TreeView);

    const u32 size = UISettings::values.game_icon_size.GetValue();
    int index = 1;
    for (size_t i = 0; i < ICON_SIZES.size(); ++i) {
        if (ICON_SIZES[i] == size) {
            index = static_cast<int>(i);
        }
    }
    const QSignalBlocker blocker{size_slider};
    size_slider->setValue(index);
}

void RyujinxToolbar::FocusSearch() {
    search->setFocus();
}

void RyujinxToolbar::ClearSearch() {
    search->clear();
}

RyujinxCardDelegate::RyujinxCardDelegate(QObject* parent) : QStyledItemDelegate{parent} {}

namespace {
bool IsGame(const QModelIndex& index) {
    return index.data(GameListItem::TypeRole).value<GameListItemType>() == GameListItemType::Game;
}

QString VersionOf(const QModelIndex& index) {
    // The tooltip carries "Play Time: ...\nVersion: x" (GameListItemPath).
    static const QRegularExpression version{QStringLiteral("Version: ([^\\n]+)")};
    const auto match = version.match(index.data(Qt::ToolTipRole).toString());
    return match.hasMatch() ? match.captured(1) : QString{};
}
} // Anonymous namespace

QSize RyujinxCardDelegate::sizeHint(const QStyleOptionViewItem& option,
                                    const QModelIndex& index) const {
    if (index.column() != 0) {
        return QStyledItemDelegate::sizeHint(option, index);
    }
    if (!IsGame(index)) {
        return {option.rect.width(), 34};
    }
    const int icon = (std::max)(static_cast<int>(UISettings::values.game_icon_size.GetValue()), 48);
    return {option.rect.width(), (std::min)(icon, 128) + 28};
}

void RyujinxCardDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                const QModelIndex& index) const {
    if (index.column() != 0) {
        return;
    }
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    const QColor text{0xff, 0xff, 0xff};
    const QColor secondary{0xc8, 0xc8, 0xc8};
    if (!IsGame(index)) {
        // Folder rows: a section heading.
        QFont font = option.font;
        font.setBold(true);
        painter->setFont(font);
        painter->setPen(secondary);
        const QRect area = option.rect.adjusted(8, 0, -8, 0);
        const QPixmap icon = index.data(Qt::DecorationRole).value<QPixmap>();
        int x = area.left();
        if (!icon.isNull()) {
            const QPixmap small = icon.scaled(20, 20, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            painter->drawPixmap(x, area.center().y() - small.height() / 2, small);
            x += 28;
        }
        painter->drawText(QRect(x, area.top(), area.right() - x, area.height()),
                          Qt::AlignVCenter | Qt::AlignLeft, index.data(Qt::DisplayRole).toString());
        painter->restore();
        return;
    }

    const bool selected = (option.state & QStyle::State_Selected) != 0;
    const bool hovered = (option.state & QStyle::State_MouseOver) != 0;
    // The tree indents the row by its depth; the card spans the whole viewport.
    QRect card = option.rect;
    if (const auto* view = qobject_cast<const QWidget*>(option.widget)) {
        card.setLeft(0);
        card.setRight(view->width() - 1);
    }
    card = card.adjusted(10, 5, -14, -5);
    QPainterPath path;
    path.addRoundedRect(QRectF(card), 8, 8);
    painter->fillPath(path, selected  ? QColor(0x45, 0x45, 0x45)
                            : hovered ? QColor(0x3f, 0x3f, 0x3f)
                                      : QColor(0x39, 0x39, 0x39));
    if (selected) {
        painter->setPen(QPen(QColor(0x4c, 0xa2, 0xff), 1.5));
        painter->drawPath(path);
    }

    const QRect inner = card.adjusted(12, 10, -16, -10);
    int x = inner.left();
    {
        const int side = inner.height();
        const QRect icon_rect(x, inner.top(), side, side);
        QPainterPath clip;
        clip.addRoundedRect(QRectF(icon_rect), 6, 6);
        const QPixmap icon = index.data(Qt::DecorationRole).value<QPixmap>();
        // Games without an icon (NUP dumps often carry none) get a tile with their initials.
        const bool blank = icon.isNull() ||
                           icon.toImage().pixelColor(icon.width() / 2, icon.height() / 2).alpha() == 0;
        painter->save();
        painter->setClipPath(clip);
        if (blank) {
            QLinearGradient gradient(icon_rect.topLeft(), icon_rect.bottomRight());
            gradient.setColorAt(0.0, QColor(0x2f, 0x5d, 0x8a));
            gradient.setColorAt(1.0, QColor(0x1e, 0x3a, 0x5c));
            painter->fillRect(icon_rect, gradient);
            QString initials;
            static const QRegularExpression words{QStringLiteral("[A-Za-z0-9]+")};
            static const QStringList skip{QStringLiteral("the"), QStringLiteral("of"),
                                          QStringLiteral("a"), QStringLiteral("and")};
            // "Series: Subtitle" games go by the subtitle.
            const QString name = index.data(GameListItemPath::TitleRole).toString();
            auto it = words.globalMatch(name.contains(QLatin1Char(':'))
                                            ? name.section(QLatin1Char(':'), 1)
                                            : name);
            while (it.hasNext() && initials.size() < 3) {
                const QString word = it.next().captured(0);
                if (!skip.contains(word.toLower())) {
                    initials += word.front().toUpper();
                }
            }
            QFont font = option.font;
            font.setBold(true);
            font.setPixelSize((std::max)(side / 3, 12));
            painter->setFont(font);
            painter->setPen(Qt::white);
            painter->drawText(icon_rect, Qt::AlignCenter, initials);
        } else {
            painter->drawPixmap(icon_rect, icon.scaled(side, side, Qt::KeepAspectRatio,
                                                       Qt::SmoothTransformation));
        }
        painter->restore();
        x += side + 16;
    }

    const auto sibling = [&](int column) {
        return index.siblingAtColumn(column).data(Qt::DisplayRole).toString();
    };
    QString title = index.data(GameListItemPath::TitleRole).toString();
    const QString file = QFileInfo(index.data(GameListItemPath::FullPathRole).toString()).fileName();
    if (title.isEmpty()) {
        title = file;
    }
    const QString version = VersionOf(index);
    const QString program_id = QStringLiteral("%1").arg(
        index.data(GameListItemPath::ProgramIdRole).toULongLong(), 16, 16, QLatin1Char('0'));
    const QString file_type = index.data(GameListItemPath::FileTypeRole).toString();
    const QString play_time = sibling(GameListModel::COLUMN_PLAY_TIME);
    const QString size = sibling(GameListModel::COLUMN_SIZE);

    const QFontMetrics metrics{option.font};
    const int line = metrics.height() + 4;
    const int right_width = (std::min)(170, inner.width() / 6);
    const int middle_width = (std::min)(190, inner.width() / 5);
    const QRect left_text(x, inner.top(), inner.right() - x - right_width - middle_width - 24,
                          inner.height());
    const QRect middle_text(inner.right() - right_width - middle_width, inner.top(), middle_width,
                            inner.height());
    const QRect right_text(inner.right() - right_width, inner.top(), right_width, inner.height());

    QFont bold = option.font;
    bold.setBold(true);
    painter->setFont(bold);
    painter->setPen(text);
    painter->drawText(QRect(left_text.left(), left_text.top(), left_text.width(), line),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      metrics.elidedText(title, Qt::ElideRight, left_text.width()));
    painter->setFont(option.font);
    painter->setPen(secondary);
    painter->drawText(QRect(left_text.left(), left_text.top() + line, left_text.width(), line),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      metrics.elidedText(file, Qt::ElideMiddle, left_text.width()));
    if (!version.isEmpty()) {
        painter->drawText(
            QRect(left_text.left(), left_text.top() + line * 2, left_text.width(), line),
            Qt::AlignLeft | Qt::AlignVCenter, version);
    }

    // Separator, then the title ID and file type.
    painter->setPen(QColor(0x50, 0x50, 0x50));
    painter->drawLine(middle_text.left() - 12, inner.top(), middle_text.left() - 12,
                      inner.bottom());
    painter->setPen(text);
    painter->drawText(QRect(middle_text.left(), middle_text.top(), middle_text.width(), line),
                      Qt::AlignLeft | Qt::AlignVCenter, program_id);
    painter->setPen(secondary);
    painter->drawText(
        QRect(middle_text.left(), middle_text.top() + line, middle_text.width(), line),
        Qt::AlignLeft | Qt::AlignVCenter, file_type.toUpper());

    painter->setPen(text);
    painter->drawText(QRect(right_text.left(), right_text.top(), right_text.width(), line),
                      Qt::AlignRight | Qt::AlignVCenter,
                      play_time.isEmpty() ? tr("Never played") : play_time);
    painter->setPen(secondary);
    painter->drawText(QRect(right_text.left(), right_text.top() + line, right_text.width(), line),
                      Qt::AlignRight | Qt::AlignVCenter, size);
    painter->restore();
}

// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>

#include <QComboBox>
#include <QDateTime>
#include <QFileInfo>
#include <QLocale>
#include <QHBoxLayout>
#include <QIcon>
#include <QLineEdit>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QSlider>
#include <QToolButton>

#include "frontend_common/play_time_manager.h"
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

/// Fluent's List and Grid symbols, drawn: three lines with bullets, or four rounded squares.
QIcon MakeViewIcon(bool grid) {
    QPixmap pixmap(40, 40);
    pixmap.setDevicePixelRatio(2.0);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(0xd0, 0xd0, 0xd0), 1.2));
    if (grid) {
        for (int row = 0; row < 2; ++row) {
            for (int column = 0; column < 2; ++column) {
                painter.drawRoundedRect(QRectF(3.5 + column * 7.0, 3.5 + row * 7.0, 5.5, 5.5), 1.2,
                                        1.2);
            }
        }
    } else {
        for (int row = 0; row < 3; ++row) {
            const double y = 5.5 + row * 4.5;
            painter.drawPoint(QPointF(4.5, y));
            painter.drawLine(QPointF(7.5, y), QPointF(16.0, y));
        }
    }
    return QIcon(pixmap);
}

QToolButton* MakeViewButton(QWidget* parent, bool grid, const QString& tooltip) {
    auto* button = new QToolButton(parent);
    button->setIcon(MakeViewIcon(grid));
    button->setIconSize(QSize(20, 20));
    button->setToolTip(tooltip);
    button->setCheckable(true);
    button->setAutoRaise(false);
    button->setFixedSize(40, 31); // MainViewControls.axaml: 40 wide in a 35 high row
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

    list_button = MakeViewButton(this, false, tr("List view"));
    grid_button = MakeViewButton(this, true, tr("Grid view"));
    connect(list_button, &QToolButton::clicked, this,
            [this] { emit ViewModeRequested(Settings::GameListMode::TreeView); });
    connect(grid_button, &QToolButton::clicked, this,
            [this] { emit ViewModeRequested(Settings::GameListMode::GridView); });

    size_slider = new QSlider(Qt::Horizontal, this);
    size_slider->setRange(0, static_cast<int>(ICON_SIZES.size()) - 1);
    size_slider->setPageStep(1);
    size_slider->setFixedWidth(150); // 150 wide, as Ryujinx's
    size_slider->setToolTip(tr("Icon size"));
    connect(size_slider, &QSlider::valueChanged, this, [this](int value) {
        emit IconSizeRequested(ICON_SIZES[static_cast<size_t>(value)]);
    });

    sort_box = new QComboBox(this);
    for (const SortChoice& choice : SORT_CHOICES) {
        sort_box->addItem(tr(choice.label));
    }
    sort_box->setFixedWidth(150);
    sort_box->setToolTip(tr("Sort by"));
    connect(sort_box, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0 && index < static_cast<int>(SORT_CHOICES.size())) {
            emit SortRequested(SORT_CHOICES[index].column, SORT_CHOICES[index].order);
        }
    });

    search = new QLineEdit(this);
    search->setPlaceholderText(tr("Search..."));
    search->setClearButtonEnabled(true);
    search->setMinimumWidth(200);
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

const PlayTime::PlayTimeManager* RyujinxCardDelegate::play_time_manager = nullptr;

void RyujinxCardDelegate::SetPlayTimeManager(const PlayTime::PlayTimeManager* manager) {
    play_time_manager = manager;
}

namespace {
// Ryujinx's dark theme (Assets/Styles/Themes.xaml) and list layout (ApplicationListView.axaml):
// cards with 10 px padding and 5 px corners, 2 px apart, columns Auto | 10 | * | 150 | 100.
const QColor CARD{0x3a, 0x3a, 0x3a};       // AppListBackgroundColor #0FFFFFFF on #2D2D2D
const QColor CARD_HOVER{0x46, 0x46, 0x46}; // AppListHoverBackgroundColor #1EFFFFFF
const QColor BORDER{0x50, 0x50, 0x50};     // ThemeControlBorderColor
const QColor SECONDARY{255, 255, 255, 0xA0};
const QColor FAVORITE{0xfc, 0xd1, 0x2a};
const QColor ACCENT{0x4c, 0xa2, 0xff};
constexpr int PADDING = 10;
constexpr int SPACING = 5;
constexpr int ID_COLUMN = 150;
constexpr int TIME_COLUMN = 100;

bool IsGame(const QModelIndex& index) {
    return index.data(GameListItem::TypeRole).value<GameListItemType>() == GameListItemType::Game;
}

QString VersionOf(const QModelIndex& index) {
    // The tooltip carries "Play Time: ...\nVersion: x" (GameListItemPath).
    static const QRegularExpression version{QStringLiteral("Version: ([^\\n]+)")};
    const auto match = version.match(index.data(Qt::ToolTipRole).toString());
    return match.hasMatch() ? match.captured(1) : QString{};
}

/// Ryujinx's icon widths for its four sizes, from the slider's icon sizes 32/64/128/256.
int IconWidth() {
    const u32 size = UISettings::values.game_icon_size.GetValue();
    return size <= 32 ? 50 : size <= 64 ? 80 : size <= 128 ? 100 : 120;
}

/// ValueFormatUtils.FormatTimeSpan: the largest unit, "1 hour", "45 minutes".
QString Plural(u64 count, const char* one, const char* many) {
    return QStringLiteral("%1 %2").arg(count).arg(
        count == 1 ? QObject::tr(one) : QObject::tr(many));
}
} // Anonymous namespace

QString FormatRyujinxPlayTime(u64 seconds) {
    if (seconds >= 3600) {
        return Plural(seconds / 3600, "hour", "hours");
    }
    if (seconds >= 60) {
        return Plural(seconds / 60, "minute", "minutes");
    }
    return Plural(seconds, "second", "seconds");
}

namespace {

QString FormatSize(u64 bytes) {
    static constexpr std::array units{"B", "KiB", "MiB", "GiB", "TiB"};
    double value = static_cast<double>(bytes);
    size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < units.size()) {
        value /= 1024.0;
        ++unit;
    }
    return unit == 0 ? QStringLiteral("%1 B").arg(bytes)
                     : QStringLiteral("%1 %2").arg(value, 0, 'f', 1).arg(QLatin1String(units[unit]));
}

int TextHeight(const QFont& font) {
    QFont bold = font;
    bold.setBold(true);
    return QFontMetrics{bold}.height() + 3 * (QFontMetrics{font}.height() + SPACING);
}
} // Anonymous namespace

QSize RyujinxCardDelegate::sizeHint(const QStyleOptionViewItem& option,
                                    const QModelIndex& index) const {
    if (index.column() != 0 || !IsGame(index)) {
        // Folder, favourites and "add directory" rows: Ryujinx lists games only.
        return {0, 0};
    }
    return {option.rect.width(),
            (std::max)(IconWidth(), TextHeight(option.font)) + PADDING * 2 + 2};
}

void RyujinxCardDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                const QModelIndex& index) const {
    if (index.column() != 0 || !IsGame(index)) {
        return;
    }
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    const bool selected = (option.state & QStyle::State_Selected) != 0;
    const bool hovered = (option.state & QStyle::State_MouseOver) != 0;

    // The card spans the viewport (the tree would indent it), with the list's 8 px padding.
    QRect card = option.rect;
    if (const auto* view = qobject_cast<const QWidget*>(option.widget)) {
        card.setLeft(0);
        card.setRight(view->width() - 1);
    }
    card = card.adjusted(8, 1, -8, -1);
    QPainterPath path;
    path.addRoundedRect(QRectF(card), 5, 5);
    painter->fillPath(path, selected || hovered ? CARD_HOVER : CARD);
    if (selected) {
        // Fluent's selection indicator: a short accent bar on the left edge.
        const int bar = (std::min)(card.height() - 16, 40);
        painter->fillRect(QRect(card.left(), card.center().y() - bar / 2, 3, bar), ACCENT);
    }

    const QRect inner = card.adjusted(PADDING, PADDING, -PADDING, -PADDING);
    const int icon_width = IconWidth();
    const QRect icon_rect(inner.left(), inner.top(), icon_width, icon_width);
    {
        QPainterPath clip;
        clip.addRoundedRect(QRectF(icon_rect), 3, 3);
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
            font.setPixelSize((std::max)(icon_width / 3, 12));
            painter->setFont(font);
            painter->setPen(Qt::white);
            painter->drawText(icon_rect, Qt::AlignCenter, initials);
        } else {
            painter->drawPixmap(icon_rect, icon.scaled(icon_width, icon_width, Qt::KeepAspectRatio,
                                                       Qt::SmoothTransformation));
        }
        painter->restore();
    }
    const u64 program_id = index.data(GameListItemPath::ProgramIdRole).toULongLong();
    if (UISettings::values.favorited_ids.contains(program_id)) {
        QFont star = option.font;
        star.setPixelSize(18);
        painter->setFont(star);
        painter->setPen(FAVORITE);
        painter->drawText(QRect(icon_rect.left() - 8, icon_rect.top() - 9, 20, 20),
                          Qt::AlignCenter, QStringLiteral("★"));
    }

    // Columns: name, publisher (the file here) and version | title ID and type | last played,
    // time played and size.
    const int name_left = icon_rect.right() + 1 + 10;
    const QRect time_column(inner.right() + 1 - TIME_COLUMN, inner.top(), TIME_COLUMN,
                            inner.height());
    const QRect id_column(time_column.left() - ID_COLUMN + 10, inner.top(), ID_COLUMN - 10,
                          inner.height());
    const int name_border = id_column.left() - 10;
    const QRect name_column(name_left, inner.top(), name_border - 5 - name_left, inner.height());
    painter->setPen(BORDER);
    painter->drawLine(name_border, inner.top(), name_border, inner.bottom());

    QFont bold = option.font;
    bold.setBold(true);
    const QFontMetrics metrics{option.font};
    const QFontMetrics bold_metrics{bold};
    const auto line = [&](const QRect& column, int& y, const QString& text, const QColor& color,
                          Qt::Alignment align, bool is_bold = false) {
        const QFontMetrics& m = is_bold ? bold_metrics : metrics;
        painter->setFont(is_bold ? bold : option.font);
        painter->setPen(color);
        painter->drawText(QRect(column.left(), y, column.width(), m.height()),
                          align | Qt::AlignVCenter, m.elidedText(text, Qt::ElideRight, column.width()));
        y += m.height() + SPACING;
    };

    QString title = index.data(GameListItemPath::TitleRole).toString();
    const QString file = QFileInfo(index.data(GameListItemPath::FullPathRole).toString()).fileName();
    if (title.isEmpty()) {
        title = file;
    }
    int y = name_column.top();
    line(name_column, y, title, Qt::white, Qt::AlignLeft, true);
    line(name_column, y, file, SECONDARY, Qt::AlignLeft);
    line(name_column, y, VersionOf(index), Qt::white, Qt::AlignLeft);

    // Title ID on a pill (a button in Ryujinx), then the file type.
    y = id_column.top();
    const QString id_text = QStringLiteral("%1").arg(program_id, 16, 16, QLatin1Char('0'));
    QRect pill(id_column.left() - 1, y, metrics.horizontalAdvance(id_text) + 6, metrics.height() + 3);
    QPainterPath pill_path;
    pill_path.addRoundedRect(QRectF(pill), 4, 4);
    painter->fillPath(pill_path, QColor(255, 255, 255, 0x0F));
    painter->setFont(option.font);
    painter->setPen(Qt::white);
    painter->drawText(pill.adjusted(3, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, id_text);
    y += pill.height() + SPACING;
    line(id_column, y, index.data(GameListItemPath::FileTypeRole).toString().toUpper(), Qt::white,
         Qt::AlignLeft);

    // Last played (date over time), time played, size.
    y = time_column.top();
    const s64 last = play_time_manager ? play_time_manager->GetLastPlayed(program_id) : 0;
    const u64 played = play_time_manager ? play_time_manager->GetPlayTime(program_id) : 0;
    if (last > 0) {
        const QDateTime when = QDateTime::fromSecsSinceEpoch(last);
        const QLocale locale;
        // The short date with the full year (10/27/2026), as Ryujinx shows it.
        QString date_format = locale.dateFormat(QLocale::ShortFormat);
        if (!date_format.contains(QStringLiteral("yyyy"))) {
            date_format.replace(QStringLiteral("yy"), QStringLiteral("yyyy"));
        }
        line(time_column, y, locale.toString(when.date(), date_format), Qt::white,
             Qt::AlignRight);
        line(time_column, y, locale.toString(when.time(), QLocale::LongFormat).section(QLatin1Char(' '), 0, 1),
             Qt::white, Qt::AlignRight);
    } else {
        line(time_column, y, tr("Never"), Qt::white, Qt::AlignRight);
    }
    if (played > 1) {
        line(time_column, y, FormatRyujinxPlayTime(played), Qt::white, Qt::AlignRight);
    }
    const u64 size = index.siblingAtColumn(GameListModel::COLUMN_SIZE)
                         .data(GameListItemSize::SizeRole)
                         .toULongLong();
    line(time_column, y, FormatSize(size), Qt::white, Qt::AlignRight);
    painter->restore();
}

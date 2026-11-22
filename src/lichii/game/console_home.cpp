// SPDX-FileCopyrightText: Copyright 2026 imenput-emu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>

#include <QCloseEvent>
#include <QCoreApplication>
#include <QEasingCurve>
#include <QFont>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRadialGradient>
#include <QSet>
#include <QStandardItem>
#include <QTime>
#include <QWheelEvent>

#include "core/core.h"
#include "lichii/game/console_home.h"
#include "lichii/util/controller_navigation.h"
#include "qt_common/game_list/game_list_p.h"
#include "qt_common/game_list/model.h"

namespace {
constexpr int kJump = 5;
}

ConsoleHome::ConsoleHome(Core::System& system, GameListModel* model_, QWidget* parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint), model(model_) {
    setAttribute(Qt::WA_DeleteOnClose);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setWindowTitle(tr("Console Mode"));

    scroll.setDuration(260);
    scroll.setEasingCurve(QEasingCurve::OutCubic);
    connect(&scroll, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        position = value.toDouble();
        update();
    });

    clock_timer.setInterval(1000);
    connect(&clock_timer, &QTimer::timeout, this, qOverload<>(&QWidget::update));
    clock_timer.start();

    // Artwork and new games arrive over time; coalesce the model's signals.
    refresh_timer.setSingleShot(true);
    refresh_timer.setInterval(250);
    connect(&refresh_timer, &QTimer::timeout, this, &ConsoleHome::Refresh);
    connect(model, &QAbstractItemModel::dataChanged, &refresh_timer, qOverload<>(&QTimer::start));
    connect(model, &QAbstractItemModel::rowsInserted, &refresh_timer, qOverload<>(&QTimer::start));
    connect(model, &QAbstractItemModel::rowsRemoved, &refresh_timer, qOverload<>(&QTimer::start));
    connect(model, &QAbstractItemModel::modelReset, &refresh_timer, qOverload<>(&QTimer::start));

    // The gamepad drives the screen through the same key events as a keyboard.
    navigation = new ControllerNavigation(system.HIDCore(), this);
    connect(navigation, &ControllerNavigation::TriggerKeyboardEvent, this, [this](Qt::Key key) {
        QCoreApplication::postEvent(this, new QKeyEvent(QEvent::KeyPress, key, Qt::NoModifier));
    });

    Refresh();
}

ConsoleHome::~ConsoleHome() {
    if (navigation != nullptr) {
        navigation->UnloadController();
    }
}

void ConsoleHome::Refresh() {
    QString keep_path;
    if (selected >= 0 && selected < static_cast<int>(entries.size())) {
        keep_path = entries[selected].path;
    }
    entries.clear();
    Collect(QModelIndex{});
    std::ranges::sort(entries, [](const Entry& a, const Entry& b) {
        return a.title.localeAwareCompare(b.title) < 0;
    });
    selected = 0;
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        if (entries[i].path == keep_path) {
            selected = i;
            break;
        }
    }
    scroll.stop();
    position = selected;
    update();
}

void ConsoleHome::Collect(const QModelIndex& parent) {
    // Games sit under their folders (and again under Favorites): list each file once.
    QSet<QString> seen;
    for (const Entry& e : entries) {
        seen.insert(e.path);
    }
    const int rows = model->rowCount(parent);
    for (int row = 0; row < rows; ++row) {
        const QModelIndex index = model->index(row, 0, parent);
        const QStandardItem* item = model->itemFromIndex(index);
        if (item == nullptr) {
            continue;
        }
        const auto type = item->data(GameListItem::TypeRole).value<GameListItemType>();
        if (type == GameListItemType::Game) {
            const QString path = item->data(GameListItemPath::FullPathRole).toString();
            if (path.isEmpty() || seen.contains(path)) {
                continue;
            }
            seen.insert(path);
            Entry entry;
            entry.index = index;
            entry.path = path;
            entry.title = item->data(GameListItemPath::TitleRole).toString();
            if (entry.title.isEmpty()) {
                entry.title = item->data(Qt::DisplayRole).toString().split(QLatin1Char('\n')).first();
            }
            entry.art = item->data(Qt::DecorationRole).value<QPixmap>();
            entries.push_back(std::move(entry));
        } else if (item->hasChildren()) {
            Collect(index);
        }
    }
}

void ConsoleHome::SelectRow(int row) {
    if (entries.empty()) {
        return;
    }
    row = std::clamp(row, 0, static_cast<int>(entries.size()) - 1);
    if (row == selected) {
        return;
    }
    selected = row;
    scroll.stop();
    scroll.setStartValue(position);
    scroll.setEndValue(static_cast<double>(selected));
    scroll.start();
}

void ConsoleHome::MoveBy(int delta) {
    SelectRow(selected + delta);
}

void ConsoleHome::Activate() {
    if (selected < 0 || selected >= static_cast<int>(entries.size())) {
        return;
    }
    const QModelIndex index = entries[selected].index;
    if (!index.isValid()) {
        return;
    }
    close();
    emit GameActivated(index);
}

void ConsoleHome::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
    case Qt::Key_Left:
        MoveBy(-1);
        break;
    case Qt::Key_Right:
        MoveBy(1);
        break;
    case Qt::Key_PageUp:
        MoveBy(-kJump);
        break;
    case Qt::Key_PageDown:
        MoveBy(kJump);
        break;
    case Qt::Key_Home:
        SelectRow(0);
        break;
    case Qt::Key_End:
        SelectRow(static_cast<int>(entries.size()) - 1);
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        Activate();
        break;
    case Qt::Key_Escape:
    case Qt::Key_Backspace:
        close();
        break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }
    event->accept();
}

void ConsoleHome::wheelEvent(QWheelEvent* event) {
    const int dy = event->angleDelta().y();
    const int dx = event->angleDelta().x();
    const int d = dx != 0 ? dx : dy;
    if (d != 0) {
        MoveBy(d < 0 ? 1 : -1);
    }
    event->accept();
}

void ConsoleHome::closeEvent(QCloseEvent* event) {
    if (navigation != nullptr) {
        navigation->UnloadController();
    }
    event->accept();
    emit Closed();
}

void ConsoleHome::TileGeometry(double distance, double tile, double& center_x,
                               double& scale) const {
    const double a = std::abs(distance);
    const double sign = distance < 0 ? -1.0 : 1.0;
    const double step = tile * 0.82;
    const double bump = tile * 0.22;
    const double left = width() * 0.17 + tile / 2.0;
    center_x = left + distance * step + sign * std::min(a, 1.0) * bump;
    scale = 1.0 - 0.2 * std::min(a, 1.0);
}

void ConsoleHome::mousePressEvent(QMouseEvent* event) {
    const double tile = std::min(width() * 0.2, height() * 0.34);
    const double cy = height() * 0.46;
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        double cx = 0;
        double s = 1;
        TileGeometry(i - position, tile, cx, s);
        const QRectF rect{cx - tile * s / 2.0, cy - tile * s / 2.0, tile * s, tile * s};
        if (rect.contains(event->position())) {
            if (i == selected) {
                Activate();
            } else {
                SelectRow(i);
            }
            break;
        }
    }
    event->accept();
}

void ConsoleHome::DrawTile(QPainter& painter, const Entry& entry, const QRectF& rect,
                           double focus) const {
    const double radius = rect.width() * 0.07;
    QPainterPath shape;
    shape.addRoundedRect(rect, radius, radius);

    if (focus > 0.01) {
        // Soft glow behind the focused tile.
        for (int i = 6; i >= 1; --i) {
            const double grow = i * rect.width() * 0.018;
            QColor glow(120, 170, 255, static_cast<int>(10 * focus));
            painter.setPen(Qt::NoPen);
            painter.setBrush(glow);
            painter.drawRoundedRect(rect.adjusted(-grow, -grow, grow, grow), radius + grow,
                                    radius + grow);
        }
    }

    painter.save();
    painter.setClipPath(shape);
    if (entry.art.isNull()) {
        QLinearGradient fill(rect.topLeft(), rect.bottomRight());
        fill.setColorAt(0.0, QColor(54, 78, 150));
        fill.setColorAt(1.0, QColor(22, 30, 70));
        painter.fillRect(rect, fill);
        QFont font = painter.font();
        font.setPixelSize(static_cast<int>(rect.height() * 0.4));
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(QColor(255, 255, 255, 170));
        painter.drawText(rect, Qt::AlignCenter, entry.title.left(1).toUpper());
    } else {
        // Centre-crop to a square so the artwork is never stretched.
        const double side = std::min(entry.art.width(), entry.art.height());
        const QRectF source{(entry.art.width() - side) / 2.0, (entry.art.height() - side) / 2.0,
                            side, side};
        painter.drawPixmap(rect, entry.art, source);
    }
    if (focus < 0.99) {
        painter.fillRect(rect, QColor(8, 12, 30, static_cast<int>(120 * (1.0 - focus))));
    }
    painter.restore();

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(255, 255, 255, static_cast<int>(40 + 200 * focus)),
                        1.0 + 2.0 * focus));
    painter.drawPath(shape);
}

void ConsoleHome::DrawHint(QPainter& painter, const QPointF& at, const QString& key,
                           const QString& label) const {
    const double d = height() * 0.035;
    painter.setPen(QPen(QColor(255, 255, 255, 200), 2));
    painter.setBrush(QColor(255, 255, 255, 25));
    painter.drawEllipse(QRectF{at.x(), at.y() - d / 2, d, d});
    QFont font = painter.font();
    font.setPixelSize(static_cast<int>(d * 0.6));
    font.setBold(true);
    painter.setFont(font);
    painter.drawText(QRectF{at.x(), at.y() - d / 2, d, d}, Qt::AlignCenter, key);
    font.setBold(false);
    font.setPixelSize(static_cast<int>(d * 0.62));
    painter.setFont(font);
    painter.setPen(QColor(255, 255, 255, 190));
    painter.drawText(QPointF{at.x() + d * 1.4, at.y() + d * 0.22}, label);
}

void ConsoleHome::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform |
                           QPainter::TextAntialiasing);
    const double w = width();
    const double h = height();

    // Backdrop: deep blue vertical gradient with a light bloom behind the selected tile.
    QLinearGradient sky(0, 0, 0, h);
    sky.setColorAt(0.0, QColor(6, 10, 32));
    sky.setColorAt(0.55, QColor(14, 30, 88));
    sky.setColorAt(1.0, QColor(4, 8, 24));
    painter.fillRect(rect(), sky);
    QRadialGradient bloom(QPointF(w * 0.3, h * 0.45), w * 0.55);
    bloom.setColorAt(0.0, QColor(70, 120, 255, 90));
    bloom.setColorAt(1.0, QColor(70, 120, 255, 0));
    painter.fillRect(rect(), bloom);

    // Top bar.
    QFont title_font = font();
    title_font.setPixelSize(static_cast<int>(h * 0.03));
    title_font.setBold(true);
    painter.setFont(title_font);
    painter.setPen(QColor(255, 255, 255, 220));
    painter.drawText(QPointF{w * 0.04, h * 0.07}, QStringLiteral("imenput-emu"));
    painter.drawText(QRectF{w * 0.7, h * 0.03, w * 0.26, h * 0.06},
                     Qt::AlignRight | Qt::AlignVCenter,
                     QTime::currentTime().toString(QStringLiteral("HH:mm")));

    if (entries.empty()) {
        QFont f = font();
        f.setPixelSize(static_cast<int>(h * 0.035));
        painter.setFont(f);
        painter.setPen(QColor(255, 255, 255, 200));
        painter.drawText(rect(), Qt::AlignCenter,
                         tr("No games found.\nAdd a game folder in the main window first."));
        return;
    }

    const double tile = std::min(w * 0.2, h * 0.34);
    const double cy = h * 0.46;
    const int count = static_cast<int>(entries.size());
    const int first = std::max(0, static_cast<int>(std::floor(position)) - 6);
    const int last = std::min(count - 1, static_cast<int>(std::ceil(position)) + 8);

    // Far tiles first so the focused one is on top.
    std::vector<int> order;
    for (int i = first; i <= last; ++i) {
        order.push_back(i);
    }
    std::ranges::sort(order, [this](int a, int b) {
        return std::abs(a - position) > std::abs(b - position);
    });
    for (const int i : order) {
        double cx = 0;
        double s = 1;
        TileGeometry(i - position, tile, cx, s);
        if (cx + tile < 0 || cx - tile > w) {
            continue;
        }
        const double focus = std::max(0.0, 1.0 - std::abs(i - position));
        const QRectF rect{cx - tile * s / 2.0, cy - tile * s / 2.0, tile * s, tile * s};
        DrawTile(painter, entries[i], rect, focus);
    }

    // Selected game: big title, then position in the list.
    const Entry& current = entries[std::clamp(selected, 0, count - 1)];
    QFont name_font = font();
    name_font.setPixelSize(static_cast<int>(h * 0.05));
    name_font.setBold(true);
    painter.setFont(name_font);
    painter.setPen(Qt::white);
    const QRectF name_rect{w * 0.17, cy + tile * 0.62, w * 0.7, h * 0.08};
    painter.drawText(name_rect, Qt::AlignLeft | Qt::AlignVCenter,
                     QFontMetricsF(name_font).elidedText(current.title, Qt::ElideRight,
                                                         name_rect.width()));
    QFont small = font();
    small.setPixelSize(static_cast<int>(h * 0.022));
    painter.setFont(small);
    painter.setPen(QColor(255, 255, 255, 150));
    painter.drawText(QRectF{w * 0.17, name_rect.bottom(), w * 0.7, h * 0.04},
                     Qt::AlignLeft | Qt::AlignVCenter,
                     tr("%1 of %2").arg(selected + 1).arg(count));

    // Controller hints along the bottom.
    const double base = h * 0.93;
    DrawHint(painter, QPointF{w * 0.04, base}, QStringLiteral("A"), tr("Start"));
    DrawHint(painter, QPointF{w * 0.04 + w * 0.12, base}, QStringLiteral("B"), tr("Back"));
    DrawHint(painter, QPointF{w * 0.04 + w * 0.24, base}, QStringLiteral("◄►"),
             tr("Browse"));
}

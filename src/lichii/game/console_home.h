// SPDX-FileCopyrightText: Copyright 2026 imenput-emu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>

#include <QPersistentModelIndex>
#include <QPixmap>
#include <QString>
#include <QTimer>
#include <QVariantAnimation>
#include <QWidget>

class ControllerNavigation;
class GameListModel;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QWheelEvent;

namespace Core {
class System;
}

/**
 * Full-screen "console mode" home screen: a dark, glowing backdrop with one horizontal row of
 * large game tiles, the selected one enlarged with its title underneath. Everything works with a
 * gamepad (D-pad / left stick to move, A to start, B to leave) as well as the keyboard and mouse.
 * The look is original: it uses no console-maker logos, icons or fonts.
 */
class ConsoleHome : public QWidget {
    Q_OBJECT

public:
    explicit ConsoleHome(Core::System& system, GameListModel* model, QWidget* parent = nullptr);
    ~ConsoleHome() override;

    /// Reads the games from the model again (artwork arrives after the list is populated).
    void Refresh();

signals:
    /// The user picked a game; the index belongs to the game list model.
    void GameActivated(const QModelIndex& index);
    void Closed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    struct Entry {
        QPersistentModelIndex index;
        QString title;
        QString path;
        QPixmap art;
    };

    void Collect(const QModelIndex& parent);
    void MoveBy(int delta);
    void SelectRow(int row);
    void Activate();
    /// Where tile `i` is drawn: centre x, size factor (1 for the selected tile).
    void TileGeometry(double distance, double tile, double& center_x, double& scale) const;
    void DrawTile(QPainter& painter, const Entry& entry, const QRectF& rect, double focus) const;
    void DrawHint(QPainter& painter, const QPointF& at, const QString& key,
                  const QString& label) const;

    GameListModel* model;
    ControllerNavigation* navigation = nullptr;
    std::vector<Entry> entries;
    int selected = 0;
    /// Fractional position of the row; follows `selected` with an animation.
    double position = 0.0;
    QVariantAnimation scroll;
    QTimer clock_timer;
    QTimer refresh_timer;
};

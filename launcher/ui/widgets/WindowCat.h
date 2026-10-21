// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QPointF>
#include <QTimer>
#include <QVector>
#include <QWidget>

#include <functional>
#include <optional>

class QPainter;
class QPainterPath;

// A small cat that lives on top of the launcher window. It treats the window's
// surfaces (instance tiles, sidebar buttons, the status bar) as ledges: it sits
// and naps on them, walks along them, steps off edges and falls, and leaps up to
// nearby ledges. Pick it up by the scruff and drop or throw it; it rights itself
// in the air and lands on its feet. The widget is only as big as the cat, so it
// never blocks clicks meant for the rest of the window.
class WindowCat : public QWidget {
    Q_OBJECT

   public:
    // A walkable surface: the top edge of something, in parent coordinates.
    struct Ledge {
        double x1 = 0;
        double x2 = 0;
        double y = 0;
    };
    using LedgeProvider = std::function<QVector<Ledge>()>;

    explicit WindowCat(QWidget* parent);

    void setLedgeProvider(LedgeProvider provider);
    void setActive(bool active);

   signals:
    void hideRequested();

   protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    // Groom: licks a raised paw. Stretch: a play bow (and a yawn), always after a nap.
    // Zoom: a sudden sprint. Crouch: stalking the cursor, rear wiggling, before a pounce.
    // Swat: a quick paw at a cursor waved in front of its face.
    enum class State { Sit, Walk, Sleep, Held, Air, Purr, Hang, Groom, Stretch, Zoom, Crouch, Swat };

    struct Coat {
        QString id;
        QString name;
        QColor fur;
        QColor furLight;
        QColor outline;
        QColor chest;
        QColor earInner;
        QColor eye;
        QColor nose;
    };

    struct Particle {
        enum Kind { Heart, Zzz, Star, Dust } kind = Heart;
        QPointF pos;  // design coordinates
        QPointF vel;
        double age = 0;
        double life = 1;
    };

    void tick();
    void step(double dt);
    void stepAir(double dt);
    void stepGrounded(double dt);
    void setState(State state);
    void decideIdle();
    bool tryLeap();
    bool tryPounce();
    bool canPounce() const;
    bool tryPlay();
    void stepHang(double dt);
    bool cursorCatchable() const;
    void setTransparent(bool transparent);
    void fall(double vx);
    void land(const Ledge& ledge, double impact);
    void refreshLedges(bool force = false);
    std::optional<Ledge> ledgeUnderFeet(double tolerance) const;
    void dropIn();
    void savePosition();
    void applyCoat(const QString& id);
    void wake();
    void spawn(Particle::Kind kind, QPointF at, int count);

    // Geometry. Drawing happens in a fixed design space, scaled to the widget.
    QPointF feet() const;  // paw contact point, parent coordinates
    void setFeet(QPointF feet);
    double maxX() const;
    double maxY() const;

    // pawRaise: 0 = both front paws down; else how far the right front paw is lifted (groom or swat).
    void drawSit(QPainter& p, bool happy, double pawRaise = 0, bool swat = false);
    void drawStretch(QPainter& p);
    void drawWalk(QPainter& p);
    void drawSleep(QPainter& p);
    void drawHanging(QPainter& p, bool falling, bool byPaws = false);
    void drawLeap(QPainter& p);
    void drawHead(QPainter& p, QPointF center, double r, bool happy, bool wide, bool earsBack);
    void drawTail(QPainter& p, QPointF from, QPointF c1, QPointF c2, QPointF to, double width);
    void drawFur(QPainter& p, const QPainterPath& shape, bool far = false);
    void drawSocks(QPainter& p, const QPainterPath& shape, const QVector<QPointF>& paws, double size);
    void drawContour(QPainter& p, const QPainterPath& clip, const QPainterPath& line);
    static QPainterPath limb(QPointF hip, QPointF knee, QPointF paw, double wTop, double wBottom);
    void drawParticles(QPainter& p);
    static void drawSparkle(QPainter& p, QPointF c, double r, QColor color);

    static QVector<Coat> coats();

    QTimer m_timer;
    QTimer m_saveTimer;  // landings save the spot a little later, not on every hop
    qint64 m_lastPatMs = -100000;
    qint64 m_zzzAt = 0;
    QElapsedTimer m_clock;
    qint64 m_lastMs = 0;

    LedgeProvider m_provider;
    QVector<Ledge> m_ledges;
    qint64 m_ledgesAt = -100000;

    State m_state = State::Air;
    double m_stateTime = 0;
    double m_stateLimit = 4;
    double m_idleTime = 0;
    double m_t = 0;

    QPointF m_pos;  // widget top-left, parent coordinates
    QPointF m_vel;  // px/s
    int m_facing = 1;
    bool m_leaping = false;
    int m_edgeChoice = 0;  // 0 undecided, 1 turn back, 2 step off
    double m_spin = 0;     // degrees while airborne
    double m_swing = 0;
    double m_swingVel = 0;
    double m_squash = 0;
    double m_dizzy = 0;
    double m_blink = 0;
    double m_nextBlink = 3;
    QPointF m_gaze;

    // The cursor, as a toy: position in parent coordinates, smoothed velocity, time at rest.
    QPointF m_cursor;
    QPointF m_cursorVel;
    double m_cursorStill = 0;
    double m_pounceCooldown = 8;
    double m_swatCooldown = 0;
    double m_playCooldown = 6;
    double m_gait = 0;       // walk cycle phase; runs faster when hunting or sprinting
    double m_speedMul = 1;   // walk speed: 1 strolling, more when trotting after the cursor
    double m_dustAt = 0;
    bool m_pouncing = false;

    bool m_pressed = false;
    bool m_dragging = false;
    QPoint m_pressGlobal;
    struct Sample {
        qint64 ms;
        QPointF pos;
    };
    QVector<Sample> m_samples;

    QVector<Particle> m_particles;
    Coat m_coat;
    bool m_active = false;
    bool m_placed = false;
};

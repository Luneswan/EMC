// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "WindowCat.h"

#include <algorithm>
#include <cmath>

#include <QAbstractButton>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QContextMenuEvent>
#include <QCursor>
#include <QLineF>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QPointer>
#include <QRandomGenerator>
#include <QtMath>

#include "Application.h"
#include "settings/SettingsObject.h"

namespace {
// The cat is drawn in a fixed 96x84 design space and scaled into the widget.
constexpr double kW = 96;
constexpr double kH = 84;
constexpr double kCx = kW / 2.0;
constexpr double kGround = 76;  // where the paws touch, design space
constexpr double kScale = 0.95;

constexpr double kGravity = 2200;   // px/s^2
constexpr double kWalkSpeed = 44;   // px/s
constexpr double kSleepAfter = 30;  // seconds without a touch
constexpr int kFrameMs = 16;
constexpr int kSleepFrameMs = 50;
constexpr int kLedgeRefreshMs = 200;
constexpr double kGripY = 3;  // where the front paws hold on when hanging from the cursor, design space

double rnd(double lo, double hi)
{
    return lo + QRandomGenerator::global()->generateDouble() * (hi - lo);
}
bool chance(int percent)
{
    return QRandomGenerator::global()->bounded(100) < percent;
}
}  // namespace

QVector<WindowCat::Coat> WindowCat::coats()
{
    return {
        { "tuxedo", QObject::tr("Tuxedo"), QColor(0x1C, 0x1C, 0x20), QColor(0x3A, 0x3A, 0x42), QColor(0x03, 0x03, 0x05),
          QColor(0xF4, 0xF4, 0xF6), QColor(0xD9, 0x9A, 0xAA), QColor(0xC8, 0xE0, 0x6A), QColor(0xE9, 0x9A, 0xAB) },
        { "silver", QObject::tr("Silver"), QColor(0x8E, 0x90, 0x98), QColor(0xC4, 0xC6, 0xCE), QColor(0x2A, 0x2B, 0x30),
          QColor(0xEE, 0xEF, 0xF2), QColor(0xE8, 0xAE, 0xBC), QColor(0x7F, 0xC8, 0xE8), QColor(0xE3, 0x9A, 0xA8) },
        { "midnight", QObject::tr("Midnight"), QColor(0x14, 0x14, 0x17), QColor(0x2C, 0x2C, 0x33), QColor(0x00, 0x00, 0x00),
          QColor(0x22, 0x22, 0x27), QColor(0x8C, 0x6A, 0x74), QColor(0xF0, 0xC0, 0x40), QColor(0x5A, 0x44, 0x4A) },
        { "snow", QObject::tr("Snow"), QColor(0xEC, 0xED, 0xF1), QColor(0xFF, 0xFF, 0xFF), QColor(0x55, 0x58, 0x64),
          QColor(0xFF, 0xFF, 0xFF), QColor(0xF4, 0xB4, 0xC6), QColor(0x6A, 0xA8, 0xF0), QColor(0xF0, 0x9C, 0xB2) },
        { "ginger", QObject::tr("Ginger"), QColor(0xD9, 0x80, 0x38), QColor(0xF2, 0xA8, 0x5E), QColor(0x4E, 0x24, 0x0C),
          QColor(0xFF, 0xF0, 0xDE), QColor(0xF2, 0xB2, 0xA0), QColor(0x7C, 0xC0, 0x4E), QColor(0xE5, 0x80, 0x90) },
    };
}

WindowCat::WindowCat(QWidget* parent) : QWidget(parent)
{
    setFixedSize(int(std::ceil(kW * kScale)), int(std::ceil(kH * kScale)));
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setAutoFillBackground(false);
    setMouseTracking(true);
    setCursor(Qt::OpenHandCursor);

    applyCoat(APPLICATION->settings()->get("CatCoat").toString());

    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &WindowCat::tick);
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(10000);
    connect(&m_saveTimer, &QTimer::timeout, this, &WindowCat::savePosition);
    parent->installEventFilter(this);
    parent->window()->installEventFilter(this);
    hide();
}

void WindowCat::setLedgeProvider(LedgeProvider provider)
{
    m_provider = std::move(provider);
    m_ledgesAt = -100000;
}

void WindowCat::setActive(bool active)
{
    m_active = active;
    if (!active) {
        savePosition();
        m_timer.stop();
        hide();
        return;
    }
    show();
    raise();
    m_clock.restart();
    m_lastMs = 0;
    m_placed = false;  // placed on the first tick, once the window has a real size
    m_timer.start(kFrameMs);
}

// ---------------------------------------------------------------- geometry

QPointF WindowCat::feet() const
{
    return m_pos + QPointF(kCx * kScale, kGround * kScale);
}

void WindowCat::setFeet(QPointF f)
{
    m_pos = f - QPointF(kCx * kScale, kGround * kScale);
}

double WindowCat::maxX() const
{
    return qMax(0, parentWidget()->width() - width());
}

double WindowCat::maxY() const
{
    return qMax(0, parentWidget()->height() - height());
}

void WindowCat::refreshLedges(bool force)
{
    const qint64 now = m_clock.elapsed();
    if (!force && now - m_ledgesAt < kLedgeRefreshMs) {
        return;
    }
    m_ledgesAt = now;
    m_ledges.clear();
    const double headroom = kGround * kScale;  // a ledge the cat cannot fit above is not a ledge
    if (m_provider) {
        for (const auto& l : m_provider()) {
            if (l.y >= headroom && l.x2 - l.x1 >= 18) {
                m_ledges.push_back(l);
            }
        }
    }
    // The bottom of the window is always solid.
    m_ledges.push_back({ 0, double(parentWidget()->width()), double(parentWidget()->height()) - 1 });
}

std::optional<WindowCat::Ledge> WindowCat::ledgeUnderFeet(double tolerance) const
{
    const QPointF f = feet();
    std::optional<Ledge> best;
    for (const auto& l : m_ledges) {
        if (f.x() < l.x1 - 2 || f.x() > l.x2 + 2) {
            continue;
        }
        const double d = std::abs(l.y - f.y());
        if (d <= tolerance && (!best || d < std::abs(best->y - f.y()))) {
            best = l;
        }
    }
    return best;
}

// ---------------------------------------------------------------- simulation

void WindowCat::tick()
{
    // Stop entirely while the window is not on screen; a cat nobody sees should not cost CPU.
    if (!window()->isVisible() || window()->isMinimized()) {
        m_timer.stop();
        return;
    }
    if (!m_placed && parentWidget()->width() > width() * 3) {
        dropIn();
    }
    const qint64 now = m_clock.elapsed();
    const double dt = qBound(0.0, (now - m_lastMs) / 1000.0, 0.05);  // a stall must not tunnel through a ledge
    m_lastMs = now;
    step(dt);
    update();
}

void WindowCat::dropIn()
{
    m_placed = true;
    refreshLedges(true);
    double fx = APPLICATION->settings()->get("CatX").toDouble();
    if (fx < 0 || fx > 1) {
        fx = 0.72;
    }
    m_pos = QPointF(fx * maxX(), 0);
    m_vel = QPointF(0, 0);
    m_spin = m_facing * 60;  // arrive tumbling, then right itself
    m_leaping = false;
    setState(State::Air);
    move(m_pos.toPoint());
}

void WindowCat::step(double dt)
{
    m_t += dt;
    m_stateTime += dt;
    if (m_state != State::Held) {
        m_idleTime += dt;
    }
    refreshLedges();

    if (m_blink > 0) {
        m_blink -= dt;
    } else if ((m_nextBlink -= dt) <= 0) {
        m_blink = 0.12;
        m_nextBlink = rnd(2.0, 6.0);
    }

    // Eyes follow the cursor when it is near.
    {
        const QPointF eye(kCx + 8 * m_facing, kGround - 36);
        const QPointF cursor = QPointF(mapFromGlobal(QCursor::pos())) / kScale;
        const QPointF d = cursor - eye;
        const double len = std::hypot(d.x(), d.y());
        QPointF target(0, 0);
        if (len > 1 && len < 520) {
            target = QPointF(d.x() / len * m_facing, d.y() / len);
        }
        m_gaze += (target - m_gaze) * qMin(1.0, dt * 8);
    }

    m_squash = qMax(0.0, m_squash - dt * 5.0);
    m_dizzy = qMax(0.0, m_dizzy - dt);
    m_pounceCooldown = qMax(0.0, m_pounceCooldown - dt);

    // Where the cursor is, how fast it moves and how long it has rested: the cat stalks a still cursor.
    {
        const QPointF c = parentWidget()->mapFromGlobal(QPointF(QCursor::pos()));
        if (dt > 0) {
            const QPointF v = (c - m_cursor) / dt;
            m_cursorVel += (v - m_cursorVel) * qMin(1.0, dt * 14);
        }
        m_cursorStill = (c - m_cursor).manhattanLength() < 2 ? m_cursorStill + dt : 0;
        m_cursor = c;
    }

    switch (m_state) {
        case State::Air:
            stepAir(dt);
            break;
        case State::Hang:
            stepHang(dt);
            break;
        case State::Held: {
            // Pendulum: the body trails behind the hand.
            const double target = qBound(-40.0, -m_vel.x() * 0.045, 40.0);
            const double acc = (target - m_swing) * 90 - m_swingVel * 9;
            m_swingVel += acc * dt;
            m_swing += m_swingVel * dt;
            m_vel *= qMax(0.0, 1 - dt * 6);
            break;
        }
        default:
            stepGrounded(dt);
            break;
    }

    if (m_dizzy > 0 && chance(10)) {
        spawn(Particle::Star, QPointF(kCx + 8 * m_facing + rnd(-14, 14), kGround - 58), 1);
    }
    for (auto& pt : m_particles) {
        pt.age += dt;
        pt.pos += pt.vel * dt;
        if (pt.kind == Particle::Dust) {
            pt.vel *= qMax(0.0, 1 - dt * 5);
        }
    }
    m_particles.erase(std::remove_if(m_particles.begin(), m_particles.end(), [](const Particle& pt) { return pt.age >= pt.life; }),
                      m_particles.end());

    move(m_pos.toPoint());
    const bool calm = m_state == State::Sleep || (m_state == State::Sit && !window()->isActiveWindow() && !m_pressed);
    const bool onlyZzz = std::all_of(m_particles.begin(), m_particles.end(), [](const Particle& pt) { return pt.kind == Particle::Zzz; });
    const int wanted = calm && onlyZzz ? kSleepFrameMs : kFrameMs;
    if (m_timer.interval() != wanted) {
        m_timer.setInterval(wanted);
    }
}

void WindowCat::stepGrounded(double dt)
{
    // Stay glued to whatever we stand on, so the cat rides along when the instance list scrolls.
    auto ledge = ledgeUnderFeet(m_state == State::Walk ? 6 : 18);
    if (!ledge) {
        fall(m_state == State::Walk ? m_facing * kWalkSpeed : 0);
        return;
    }
    setFeet(QPointF(feet().x(), ledge->y));

    // A cursor resting within reach above is irresistible, now and then.
    if ((m_state == State::Sit || m_state == State::Walk) && m_dizzy <= 0 && m_pounceCooldown <= 0 && m_cursorStill > 0.7 &&
        chance(2) && tryPounce()) {
        return;
    }

    switch (m_state) {
        case State::Walk: {
            QPointF f = feet();
            f.rx() += m_facing * kWalkSpeed * dt;
            const bool atWall = f.x() - kCx * kScale <= 0 || f.x() + kCx * kScale >= parentWidget()->width();
            const bool nearEdge = (m_facing > 0 && f.x() >= ledge->x2 - 3) || (m_facing < 0 && f.x() <= ledge->x1 + 3);
            if (atWall) {
                m_facing = -m_facing;
                m_edgeChoice = 0;
            } else if (nearEdge) {
                if (m_edgeChoice == 0) {
                    // Most of the time a cat peers over the edge and turns back; sometimes it steps off.
                    m_edgeChoice = ledge->y >= parentWidget()->height() - 2 || chance(60) ? 1 : 2;
                }
                if (m_edgeChoice == 1) {
                    m_facing = -m_facing;
                    m_edgeChoice = 0;
                    setState(State::Sit);
                    return;
                }
            }
            setFeet(f);
            if (m_stateTime > m_stateLimit) {
                decideIdle();
            }
            break;
        }
        case State::Purr:
            if (m_stateTime > 1.8) {
                setState(State::Sit);
            } else if (chance(6)) {
                spawn(Particle::Heart, QPointF(kCx + 8 * m_facing + rnd(-10, 10), kGround - 54), 1);
            }
            break;
        case State::Sleep:
            if (m_clock.elapsed() - m_zzzAt > 2600) {
                m_zzzAt = m_clock.elapsed();
                spawn(Particle::Zzz, QPointF(kCx + 16 * m_facing, kGround - 28), 1);
            }
            break;
        case State::Sit:
            if (m_idleTime > kSleepAfter) {
                setState(State::Sleep);
            } else if (m_stateTime > m_stateLimit) {
                decideIdle();
            }
            break;
        default:
            break;
    }
}

void WindowCat::stepAir(double dt)
{
    const QPointF before = feet();
    m_vel.ry() += kGravity * dt;
    m_pos += m_vel * dt;

    // Righting reflex: once it starts to fall, the cat turns feet-down.
    if (m_leaping) {
        m_spin = 0;
    } else if (m_vel.y() > 0) {
        m_spin += (0 - m_spin) * qMin(1.0, dt * 7);
    } else {
        m_spin += m_vel.x() * dt * 0.6;
    }

    if (m_pos.x() < 0) {
        m_pos.rx() = 0;
        m_vel.rx() = -m_vel.x() * 0.5;
    } else if (m_pos.x() > maxX()) {
        m_pos.rx() = maxX();
        m_vel.rx() = -m_vel.x() * 0.5;
    }
    if (m_pos.y() < 0) {
        m_pos.ry() = 0;
        m_vel.ry() = std::abs(m_vel.y()) * 0.3;
    }

    if (m_pouncing) {
        // Near the top of the jump: if the paws reach the cursor, grab on.
        const QPointF grip = m_pos + QPointF(kCx, kGripY) * kScale;
        if (m_vel.y() > -260 && QLineF(grip, m_cursor).length() < 30 && cursorCatchable()) {
            m_pouncing = false;
            m_swing = 0;
            m_swingVel = 0;
            setState(State::Hang);
            return;
        }
        if (m_vel.y() > 160) {
            m_pouncing = false;  // missed; fall and land like any other jump
            m_leaping = false;
            setTransparent(false);
        }
    }

    if (m_vel.y() <= 0) {
        return;
    }
    // Land on the highest ledge the paws crossed this frame.
    const QPointF after = feet();
    std::optional<Ledge> hit;
    for (const auto& l : m_ledges) {
        if (after.x() < l.x1 - 2 || after.x() > l.x2 + 2) {
            continue;
        }
        if (before.y() <= l.y + 0.5 && after.y() >= l.y && (!hit || l.y < hit->y)) {
            hit = l;
        }
    }
    if (hit) {
        land(*hit, m_vel.y());
    } else if (m_pos.y() > maxY()) {
        land({ 0, double(parentWidget()->width()), double(parentWidget()->height()) - 1 }, m_vel.y());
    }
}

bool WindowCat::cursorCatchable() const
{
    // Only over this window's own area, while it is the window being used.
    return window()->isActiveWindow() && parentWidget()->rect().adjusted(8, 8, -8, -8).contains(m_cursor.toPoint());
}

void WindowCat::setTransparent(bool transparent)
{
    // While it plays with the cursor the cat must never eat the clicks meant for the window.
    setAttribute(Qt::WA_TransparentForMouseEvents, transparent);
}

bool WindowCat::tryPounce()
{
    if (!cursorCatchable()) {
        return false;
    }
    const QPointF grip = feet() - QPointF(0, (kGround - kGripY) * kScale);
    const double rise = grip.y() - m_cursor.y();
    const double dx = m_cursor.x() - grip.x();
    if (rise < 24 || rise > 260 || std::abs(dx) > 240) {
        return false;
    }
    // Ballistic jump whose apex is the cursor.
    const double vy0 = -std::sqrt(2 * kGravity * rise);
    const double tUp = -vy0 / kGravity;
    m_vel = QPointF(dx / tUp, vy0);
    if (std::abs(dx) > 4) {
        m_facing = dx > 0 ? 1 : -1;
    }
    wake();
    m_leaping = true;
    m_pouncing = true;
    m_squash = 0.6;
    m_pounceCooldown = rnd(10.0, 25.0);
    setState(State::Air);
    setTransparent(true);
    return true;
}

void WindowCat::stepHang(double dt)
{
    // Dangling from the cursor by the front paws: the grip point follows it and the body swings.
    const double speed = std::hypot(m_cursorVel.x(), m_cursorVel.y());
    const QPointF next = m_cursor - QPointF(kCx, kGripY) * kScale;
    const bool outside = next.x() < 0 || next.x() > maxX() || next.y() < 0 || next.y() > maxY();
    if (m_stateTime > m_stateLimit || speed > 2400 || outside || !cursorCatchable()) {
        // Let go (or get shaken off), keeping some of the hand's motion.
        m_vel = QPointF(qBound(-1400.0, m_cursorVel.x() * 0.5, 1400.0), qBound(-900.0, m_cursorVel.y() * 0.5, 900.0));
        m_spin = m_swing * m_facing;
        m_leaping = false;
        setState(State::Air);
        if (speed > 2400) {
            m_dizzy = 1.0;
        }
        return;
    }
    const double target = qBound(-45.0, -m_cursorVel.x() * 0.05, 45.0);
    const double acc = (target - m_swing) * 70 - m_swingVel * 4;  // looser than the scruff hold: it swings
    m_swingVel += acc * dt;
    m_swing += m_swingVel * dt;
    if (std::abs(m_cursorVel.x()) > 60) {
        m_facing = m_cursorVel.x() > 0 ? -1 : 1;  // looks back the way it is dragged from
    }
    m_pos = next;
}

void WindowCat::fall(double vx)
{
    m_vel = QPointF(vx, 0);
    m_leaping = false;
    m_spin = 0;
    setState(State::Air);
}

void WindowCat::land(const Ledge& ledge, double impact)
{
    if (std::abs(m_vel.x()) > 20) {
        m_facing = m_vel.x() > 0 ? 1 : -1;
    }
    setFeet(QPointF(qBound(ledge.x1 + 2, feet().x(), ledge.x2 - 2), ledge.y));
    m_pos.rx() = qBound(0.0, m_pos.x(), maxX());
    m_vel = QPointF(0, 0);
    m_spin = 0;
    m_leaping = false;
    m_squash = qBound(0.35, impact / 900.0, 1.0);
    if (impact > 1500) {
        m_dizzy = 1.4;
    }
    spawn(Particle::Dust, QPointF(kCx, kGround), impact > 700 ? 6 : 3);
    setState(State::Sit);
    m_saveTimer.start();
}

bool WindowCat::tryLeap()
{
    // Look for a ledge a little above and within reach, and jump onto it.
    const QPointF f = feet();
    QVector<Ledge> options;
    for (const auto& l : m_ledges) {
        const double up = f.y() - l.y;
        if (up < 28 || up > 170) {
            continue;
        }
        const double tx = qBound(l.x1 + 12, f.x(), l.x2 - 12);
        if (std::abs(tx - f.x()) < 200 && l.x2 - l.x1 > 30) {
            options.push_back(l);
        }
    }
    if (options.isEmpty()) {
        return false;
    }
    const Ledge l = options.at(QRandomGenerator::global()->bounded(int(options.size())));
    const double tx = qBound(l.x1 + 12, f.x() + rnd(-30, 30), l.x2 - 12);
    const double apex = (f.y() - l.y) + 26;
    const double vy0 = -std::sqrt(2 * kGravity * apex);
    const double tUp = -vy0 / kGravity;
    const double tDown = std::sqrt(2 * 26 / kGravity);
    m_vel = QPointF((tx - f.x()) / (tUp + tDown), vy0);
    if (std::abs(m_vel.x()) > 5) {
        m_facing = m_vel.x() > 0 ? 1 : -1;
    }
    m_leaping = true;
    m_squash = 0.5;  // crouch before the jump reads as wind-up
    setState(State::Air);
    return true;
}

void WindowCat::setState(State state)
{
    m_state = state;
    m_stateTime = 0;
    m_edgeChoice = 0;
    switch (state) {
        case State::Sit:
            m_stateLimit = rnd(3.0, 8.0);
            break;
        case State::Walk:
            m_stateLimit = rnd(2.5, 7.0);
            break;
        case State::Hang:
            m_stateLimit = rnd(2.5, 5.5);
            break;
        default:
            m_stateLimit = 0;
            break;
    }
    setCursor(state == State::Held ? Qt::ClosedHandCursor : Qt::OpenHandCursor);
    setTransparent(state == State::Hang || (state == State::Air && m_pouncing));
}

void WindowCat::decideIdle()
{
    const int roll = QRandomGenerator::global()->bounded(100);
    if (roll < 22 && tryLeap()) {
        return;
    }
    if (roll < 70) {
        if (chance(40)) {
            m_facing = -m_facing;
        }
        setState(State::Walk);
    } else {
        setState(State::Sit);
    }
}

void WindowCat::wake()
{
    m_idleTime = 0;
    if (m_state == State::Sleep) {
        setState(State::Sit);
    }
}

void WindowCat::spawn(Particle::Kind kind, QPointF at, int count)
{
    for (int i = 0; i < count; ++i) {
        Particle pt;
        pt.kind = kind;
        pt.pos = at;
        switch (kind) {
            case Particle::Heart:
                pt.vel = QPointF(rnd(-12, 12), rnd(-38, -26));
                pt.life = 1.1;
                break;
            case Particle::Zzz:
                pt.vel = QPointF(m_facing * rnd(6, 12), rnd(-16, -10));
                pt.life = 2.2;
                break;
            case Particle::Star:
                pt.vel = QPointF(rnd(-20, 20), rnd(-14, -4));
                pt.life = 0.7;
                break;
            case Particle::Dust: {
                const double side = i % 2 == 0 ? 1 : -1;
                pt.pos += QPointF(side * rnd(4, 14), rnd(-2, 0));
                pt.vel = QPointF(side * rnd(40, 90), rnd(-18, -6));
                pt.life = rnd(0.35, 0.55);
                break;
            }
        }
        m_particles.push_back(pt);
    }
    if (m_particles.size() > 28) {
        m_particles.remove(0, m_particles.size() - 28);
    }
}

void WindowCat::savePosition()
{
    if (!m_placed || maxX() <= 0) {
        return;
    }
    APPLICATION->settings()->set("CatX", m_pos.x() / maxX());
}

void WindowCat::applyCoat(const QString& id)
{
    const auto all = coats();
    m_coat = all.first();
    for (const auto& c : all) {
        if (c.id == id) {
            m_coat = c;
        }
    }
    update();
}

bool WindowCat::eventFilter(QObject* watched, QEvent* event)
{
    if (!m_active) {
        return QWidget::eventFilter(watched, event);
    }
    if (watched == window() && !m_timer.isActive() && (event->type() == QEvent::WindowStateChange || event->type() == QEvent::Show)) {
        QTimer::singleShot(0, this, [this] {
            if (m_active && window()->isVisible() && !window()->isMinimized()) {
                m_lastMs = m_clock.elapsed();
                m_timer.start();
            }
        });
    }
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        m_pos.rx() = qBound(0.0, m_pos.x(), maxX());
        m_pos.ry() = qBound(0.0, m_pos.y(), maxY());
        m_ledgesAt = -100000;  // surfaces moved; the next frame re-reads them and the cat falls if its ledge is gone
        raise();
    }
    return QWidget::eventFilter(watched, event);
}

// ---------------------------------------------------------------- input

void WindowCat::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    m_pressed = true;
    m_dragging = false;
    m_pressGlobal = event->globalPosition().toPoint();
    m_samples.clear();
    m_squash = qMax(m_squash, 0.25);  // feedback on press, before we know if it is a pat or a grab
    event->accept();
}

void WindowCat::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_pressed) {
        return;
    }
    const QPoint global = event->globalPosition().toPoint();
    if (!m_dragging && (global - m_pressGlobal).manhattanLength() > 5) {
        m_dragging = true;
        m_vel = QPointF(0, 0);
        m_swing = 0;
        m_swingVel = 0;
        m_leaping = false;
        m_particles.clear();
        wake();
        setState(State::Held);
    }
    if (!m_dragging) {
        return;
    }
    // Held by the scruff: the scruff point stays under the cursor.
    const QPointF inParent = parentWidget()->mapFromGlobal(QPointF(global));
    QPointF next = inParent - QPointF(kCx, 14) * kScale;
    next.rx() = qBound(0.0, next.x(), maxX());
    next.ry() = qBound(0.0, next.y(), maxY());

    const qint64 now = m_clock.elapsed();
    m_samples.push_back({ now, next });
    while (m_samples.size() > 2 && now - m_samples.first().ms > 90) {
        m_samples.removeFirst();
    }
    if (m_samples.size() >= 2) {
        const auto& a = m_samples.first();
        const auto& b = m_samples.last();
        m_vel = (b.pos - a.pos) / (qMax<qint64>(1, b.ms - a.ms) / 1000.0);
        if (std::abs(m_vel.x()) > 30) {
            m_facing = m_vel.x() > 0 ? 1 : -1;
        }
    }
    m_pos = next;
    move(m_pos.toPoint());
    update();
}

void WindowCat::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !m_pressed) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    m_pressed = false;
    if (!m_dragging) {
        // The cat sits on buttons and tabs: a click on it still reaches what is underneath.
        const QPoint global = event->globalPosition().toPoint();
        const bool wasTransparent = testAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        QPointer<QWidget> under = QApplication::widgetAt(global);
        setAttribute(Qt::WA_TransparentForMouseEvents, wasTransparent);
        const bool listViewport = under && qobject_cast<QAbstractScrollArea*>(under->parentWidget()) &&
                                  static_cast<QAbstractScrollArea*>(under->parentWidget())->viewport() == under;
        if (under && under != this &&
            (qobject_cast<QAbstractButton*>(under) || (under->focusPolicy() & Qt::ClickFocus) || listViewport)) {
            const QPointF local = under->mapFromGlobal(QPointF(global));
            // A second pat in quick succession is a double-click on what is underneath (opens an instance).
            const qint64 now = m_clock.elapsed();
            const bool doubleClick = now - m_lastPatMs < QApplication::doubleClickInterval();
            m_lastPatMs = doubleClick ? -100000 : now;
            QMouseEvent press(doubleClick ? QEvent::MouseButtonDblClick : QEvent::MouseButtonPress, local, QPointF(global),
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(under, &press);
            if (under) {
                QMouseEvent release(QEvent::MouseButtonRelease, local, QPointF(global), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(under, &release);
            }
        }
        // A click is a pat.
        const bool wasAsleep = m_state == State::Sleep;
        wake();
        if (m_state != State::Air) {
            setState(State::Purr);
            spawn(Particle::Heart, QPointF(kCx + 8 * m_facing, kGround - 56), wasAsleep ? 1 : 3);
        }
        return;
    }
    m_dragging = false;
    // A hand that stopped before letting go drops the cat straight down; a moving one throws it.
    if (m_samples.isEmpty() || m_clock.elapsed() - m_samples.last().ms > 60) {
        m_vel = QPointF(0, 0);
    }
    m_vel.rx() = qBound(-2600.0, m_vel.x(), 2600.0);
    m_vel.ry() = qBound(-2600.0, m_vel.y(), 2600.0);
    m_spin = m_swing * m_facing;  // keep the swing angle it was dangling at, then right itself
    m_leaping = false;
    setState(State::Air);
}

void WindowCat::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || m_state == State::Held || m_state == State::Air) {
        return;
    }
    wake();
    if (!tryLeap()) {
        m_vel = QPointF(m_facing * 80, -640);
        m_leaping = true;
        setState(State::Air);
    }
    event->accept();
}

void WindowCat::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu menu(this);
    const bool grounded = m_state != State::Air && m_state != State::Held && m_state != State::Hang;
    auto* pet = menu.addAction(tr("Pet"), this, [this] {
        wake();
        setState(State::Purr);
        spawn(Particle::Heart, QPointF(kCx + 8 * m_facing, kGround - 56), 3);
    });
    pet->setEnabled(grounded);
    auto* nap = menu.addAction(m_state == State::Sleep ? tr("Wake Up") : tr("Nap"), this, [this] {
        if (m_state == State::Sleep) {
            wake();
        } else {
            setState(State::Sleep);
        }
    });
    nap->setEnabled(grounded);
    auto* floor = menu.addAction(tr("Jump Down"), this, [this] {
        wake();
        // Step off whatever it is on; the floor catches it.
        m_pos.ry() += 2;
        fall(m_facing * 60);
        m_ledgesAt = -100000;
    });
    floor->setEnabled(grounded && feet().y() < parentWidget()->height() - 4);
    auto* coatMenu = menu.addMenu(tr("Coat"));
    const QString current = m_coat.id;
    for (const auto& c : coats()) {
        auto* act = coatMenu->addAction(c.name, this, [this, id = c.id] {
            applyCoat(id);
            APPLICATION->settings()->set("CatCoat", id);
        });
        act->setCheckable(true);
        act->setChecked(c.id == current);
    }
    menu.addSeparator();
    menu.addAction(tr("Hide Cat"), this, [this] { emit hideRequested(); });
    menu.exec(event->globalPos());
}

// ---------------------------------------------------------------- drawing

void WindowCat::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(kScale, kScale);

    const bool grounded = m_state != State::Held && m_state != State::Air && m_state != State::Hang;
    if (grounded) {
        // Contact shadow.
        QRadialGradient g(QPointF(kCx, kGround), 28);
        g.setColorAt(0, QColor(0, 0, 0, 120));
        g.setColorAt(1, QColor(0, 0, 0, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawEllipse(QPointF(kCx, kGround + 1), 26, 4);
    }

    p.save();
    if (m_squash > 0) {
        const double s = m_squash * 0.2;
        p.translate(kCx, kGround);
        p.scale(1 + s * 0.8, 1 - s);
        p.translate(-kCx, -kGround);
    }
    if (m_facing < 0) {
        p.translate(kW, 0);
        p.scale(-1, 1);
    }

    switch (m_state) {
        case State::Sit:
            drawSit(p, false);
            break;
        case State::Purr:
            drawSit(p, true);
            break;
        case State::Walk:
            drawWalk(p);
            break;
        case State::Sleep:
            drawSleep(p);
            break;
        case State::Held:
            p.translate(kCx, 14);
            p.rotate(m_swing * m_facing);
            p.translate(-kCx, -14);
            drawHanging(p, false);
            break;
        case State::Hang:
            p.translate(kCx, kGripY);
            p.rotate(m_swing * m_facing);
            p.translate(-kCx, -kGripY);
            drawHanging(p, false, true);
            break;
        case State::Air:
            if (m_leaping) {
                drawLeap(p);
            } else {
                p.translate(kCx, kH / 2.0);
                p.rotate(m_spin * m_facing);
                p.translate(-kCx, -kH / 2.0);
                drawHanging(p, true);
            }
            break;
    }
    p.restore();
    drawParticles(p);
}

QPainterPath WindowCat::limb(QPointF hip, QPointF knee, QPointF paw, double wTop, double wBottom)
{
    // A leg as two tapered segments with round joints, ending in a slightly wider paw.
    QPainterPathStroker stroker;
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    QPainterPath upper(hip);
    upper.lineTo(knee);
    QPainterPath lower(knee);
    lower.lineTo(paw);
    stroker.setWidth(wTop);
    QPainterPath shape = stroker.createStroke(upper);
    stroker.setWidth(wBottom);
    shape.addPath(stroker.createStroke(lower));
    shape.addEllipse(paw + QPointF(0.8, -0.2), wBottom * 0.56, wBottom * 0.4);
    shape.setFillRule(Qt::WindingFill);
    return shape.simplified();
}

void WindowCat::drawFur(QPainter& p, const QPainterPath& shape, bool far)
{
    // One silhouette, one outline: body and legs read as a single animal.
    const QRectF b = shape.boundingRect();
    const QColor base = far ? m_coat.fur.darker(125) : m_coat.fur;
    QLinearGradient g(b.topLeft(), b.bottomLeft());
    g.setColorAt(0, far ? base : m_coat.furLight);
    g.setColorAt(0.45, base);
    g.setColorAt(1, base.darker(140));
    p.setPen(QPen(m_coat.outline, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(g);
    p.drawPath(shape);

    // Soft sheen along the back gives it volume.
    if (!far) {
        p.save();
        p.setClipPath(shape);
        QRadialGradient sheen(b.center() + QPointF(-b.width() * 0.15, -b.height() * 0.35), b.width() * 0.45);
        QColor hi = m_coat.furLight;
        hi.setAlpha(m_coat.fur.lightness() < 90 ? 70 : 110);
        QColor clear = hi;
        clear.setAlpha(0);
        sheen.setColorAt(0, hi);
        sheen.setColorAt(1, clear);
        p.setPen(Qt::NoPen);
        p.setBrush(sheen);
        p.drawRect(b);
        p.restore();
    }
}

void WindowCat::drawSocks(QPainter& p, const QPainterPath& shape, const QVector<QPointF>& paws, double size)
{
    p.save();
    p.setClipPath(shape);
    p.setPen(Qt::NoPen);
    p.setBrush(m_coat.chest);
    for (const QPointF& paw : paws) {
        // A sock: the lower part of the leg, cut by the silhouette so it follows the leg's shape.
        p.drawEllipse(paw + QPointF(0.4, -size * 0.35), size * 1.25, size * 1.05);
    }
    p.restore();
}

void WindowCat::drawContour(QPainter& p, const QPainterPath& clip, const QPainterPath& line)
{
    // Inner fur lines (thigh, leg separation) stay inside the silhouette.
    p.save();
    p.setClipPath(clip);
    QColor c = m_coat.fur.lightness() < 90 ? QColor(0, 0, 0, 150) : m_coat.outline;
    if (m_coat.fur.lightness() >= 90) {
        c.setAlpha(120);
    }
    p.setPen(QPen(c, 1.2, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    p.drawPath(line);
    p.restore();
}

void WindowCat::drawTail(QPainter& p, QPointF from, QPointF c1, QPointF c2, QPointF to, double width)
{
    // A tapered tail: sample the curve and build a filled outline that narrows to the tip.
    constexpr int n = 18;
    QVector<QPointF> left, right;
    auto at = [&](double t) {
        const double u = 1 - t;
        return u * u * u * from + 3 * u * u * t * c1 + 3 * u * t * t * c2 + t * t * t * to;
    };
    for (int i = 0; i <= n; ++i) {
        const double t = double(i) / n;
        const QPointF a = at(qMax(0.0, t - 0.02)), b = at(qMin(1.0, t + 0.02));
        QPointF d = b - a;
        const double len = std::hypot(d.x(), d.y());
        const QPointF nrm = len > 0 ? QPointF(-d.y() / len, d.x() / len) : QPointF(0, 1);
        const double w = width * (1 - 0.45 * t) / 2;
        left.push_back(at(t) + nrm * w);
        right.push_front(at(t) - nrm * w);
    }
    QPainterPath path;
    path.moveTo(left.first());
    for (const auto& pt : left) {
        path.lineTo(pt);
    }
    path.quadTo(to + (to - at(0.94)) * 1.2, right.first());
    for (const auto& pt : right) {
        path.lineTo(pt);
    }
    path.closeSubpath();
    p.setPen(QPen(m_coat.outline, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(m_coat.fur);
    p.drawPath(path);
}

void WindowCat::drawSparkle(QPainter& p, QPointF c, double r, QColor color)
{
    QPainterPath star;
    for (int i = 0; i < 8; ++i) {
        const double a = i * M_PI / 4 - M_PI / 2;
        const double rr = i % 2 == 0 ? r : r * 0.36;
        const QPointF pt = c + QPointF(std::cos(a) * rr, std::sin(a) * rr);
        if (i == 0) {
            star.moveTo(pt);
        } else {
            star.lineTo(pt);
        }
    }
    star.closeSubpath();
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawPath(star);
}

void WindowCat::drawHead(QPainter& p, QPointF c, double r, bool happy, bool wide, bool earsBack)
{
    // Head: one continuous outline with the ears grown out of the skull and soft cheek tufts,
    // drawn like an illustration rather than assembled from circles.
    auto P = [&](double x, double y) { return c + QPointF(x * r, y * r); };
    const double k = earsBack ? 1.0 : 0.0;  // ears flatten sideways when startled
    const QPointF tipL = P(-0.8 - 0.62 * k, -1.55 + 0.6 * k), tipR = P(0.8 + 0.62 * k, -1.55 + 0.6 * k);
    QPainterPath head;
    head.moveTo(P(-1.16, 0.42));
    head.cubicTo(P(-1.12, 0.12), P(-1.08, -0.18), P(-0.98, -0.4));
    head.cubicTo(P(-1.02 - 0.2 * k, -0.85 + 0.25 * k), tipL + QPointF(-0.14 * r, 0.22 * r), tipL);
    head.cubicTo(tipL + QPointF(0.2 * r, 0.16 * r), P(-0.44, -1.04), P(-0.3, -0.92));
    head.cubicTo(P(-0.1, -0.98), P(0.1, -0.98), P(0.3, -0.92));
    head.cubicTo(P(0.44, -1.04), tipR + QPointF(-0.2 * r, 0.16 * r), tipR);
    head.cubicTo(tipR + QPointF(0.14 * r, 0.22 * r), P(1.02 + 0.2 * k, -0.85 + 0.25 * k), P(0.98, -0.4));
    head.cubicTo(P(1.08, -0.18), P(1.12, 0.12), P(1.16, 0.42));
    head.lineTo(P(1.0, 0.5));
    head.lineTo(P(1.06, 0.6));
    head.cubicTo(P(0.82, 0.84), P(0.36, 0.92), P(0.0, 0.92));
    head.cubicTo(P(-0.36, 0.92), P(-0.82, 0.84), P(-1.06, 0.6));
    head.lineTo(P(-1.0, 0.5));
    head.closeSubpath();
    QRadialGradient hg(P(-0.35, -0.55), r * 1.8);
    hg.setColorAt(0, m_coat.furLight);
    hg.setColorAt(0.55, m_coat.fur);
    hg.setColorAt(1, m_coat.fur.darker(125));
    p.setPen(QPen(m_coat.outline, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(hg);
    p.drawPath(head);

    // Inner ears.
    QColor inner = m_coat.earInner;
    inner.setAlpha(190);
    p.setPen(Qt::NoPen);
    p.setBrush(inner);
    for (int side : { -1, 1 }) {
        const QPointF tip = side < 0 ? tipL : tipR;
        const QPointF outer = P(side * 0.86, -0.5), innerBase = P(side * 0.4, -0.9);
        QPainterPath ear;
        ear.moveTo(outer);
        ear.quadTo((outer + tip) / 2 + QPointF(-side * 0.04 * r, 0), tip + (outer - tip) * 0.18 + (innerBase - tip) * 0.05);
        ear.quadTo((innerBase + tip) / 2, innerBase);
        ear.quadTo(P(side * 0.62, -0.62), outer);
        p.drawPath(ear);
    }

    // Muzzle and blaze (white on the tuxedo), clipped to the head.
    p.save();
    p.setClipPath(head);
    p.setBrush(m_coat.chest);
    QPainterPath muzzle;
    muzzle.moveTo(P(0.06, 0.16));
    muzzle.cubicTo(P(0.42, 0.1), P(0.68, 0.36), P(0.62, 0.62));
    muzzle.cubicTo(P(0.54, 0.9), P(-0.42, 0.9), P(-0.5, 0.62));
    muzzle.cubicTo(P(-0.56, 0.36), P(-0.3, 0.1), P(0.06, 0.16));
    p.drawPath(muzzle);
    if (m_coat.chest.lightness() > m_coat.fur.lightness() + 60) {
        QPainterPath blaze;
        blaze.moveTo(P(0.06, -0.62));
        blaze.cubicTo(P(0.0, -0.22), P(-0.14, 0.04), P(-0.24, 0.26));
        blaze.lineTo(P(0.36, 0.26));
        blaze.cubicTo(P(0.26, 0.04), P(0.12, -0.22), P(0.06, -0.62));
        p.drawPath(blaze);
    }
    p.restore();

    // Eyes: almond-shaped, slit pupils that open up when startled or curious.
    const QPointF le = c + QPointF(-r * 0.45, -r * 0.02);
    const QPointF re = c + QPointF(r * 0.55, -r * 0.02);
    const double ew = r * 0.27, eh = r * (wide ? 0.27 : 0.21);
    const bool shut = m_blink > 0 && !wide;
    for (const QPointF& e : { le, re }) {
        if (happy || shut) {
            QPainterPath lid;
            lid.moveTo(e + QPointF(-ew, 0));
            if (happy) {
                lid.quadTo(e + QPointF(0, -eh * 1.6), e + QPointF(ew, 0));
            } else {
                lid.quadTo(e + QPointF(0, eh * 0.8), e + QPointF(ew, 0));
            }
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(m_coat.outline.lighter(m_coat.outline.lightness() < 40 ? 400 : 100), 1.7, Qt::SolidLine, Qt::RoundCap));
            if (m_coat.fur.lightness() < 80) {
                p.setPen(QPen(QColor(0xD8, 0xD8, 0xDE), 1.7, Qt::SolidLine, Qt::RoundCap));
            }
            p.drawPath(lid);
            continue;
        }
        QPainterPath almond;
        almond.moveTo(e + QPointF(-ew, 0));
        almond.cubicTo(e + QPointF(-ew * 0.5, -eh * 1.35), e + QPointF(ew * 0.5, -eh * 1.35), e + QPointF(ew, 0));
        almond.cubicTo(e + QPointF(ew * 0.5, eh * 1.35), e + QPointF(-ew * 0.5, eh * 1.35), e + QPointF(-ew, 0));
        QRadialGradient ig(e + QPointF(0, eh * 0.3), ew * 1.2);
        ig.setColorAt(0, m_coat.eye.lighter(130));
        ig.setColorAt(1, m_coat.eye.darker(140));
        p.setPen(QPen(QColor(0, 0, 0, 200), 1.0));
        p.setBrush(ig);
        p.drawPath(almond);

        const QPointF look(m_gaze.x() * ew * 0.35, m_gaze.y() * eh * 0.3);
        const double pupilW = wide ? ew * 0.62 : ew * 0.24 + std::max(0.0, 0.25 - std::abs(m_gaze.x()) * 0.2) * ew;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x05, 0x05, 0x07));
        p.save();
        p.setClipPath(almond);
        p.drawEllipse(e + look, pupilW, eh * 1.05);
        p.restore();
        p.setBrush(QColor(255, 255, 255, 230));
        p.drawEllipse(e + look + QPointF(-ew * 0.28, -eh * 0.42), ew * 0.17, ew * 0.17);
    }

    // Nose, mouth, whiskers.
    const QPointF n = c + QPointF(r * 0.06, r * 0.3);
    QPainterPath nose;
    nose.moveTo(n + QPointF(-r * 0.13, -r * 0.06));
    nose.quadTo(n + QPointF(0, -r * 0.12), n + QPointF(r * 0.13, -r * 0.06));
    nose.quadTo(n + QPointF(r * 0.04, r * 0.08), n + QPointF(0, r * 0.09));
    nose.quadTo(n + QPointF(-r * 0.04, r * 0.08), n + QPointF(-r * 0.13, -r * 0.06));
    p.setPen(Qt::NoPen);
    p.setBrush(m_coat.nose);
    p.drawPath(nose);

    const QColor lineColor = m_coat.chest.lightness() > 200 ? QColor(0x55, 0x55, 0x5C) : m_coat.outline;
    p.setPen(QPen(lineColor, 1.0, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    QPainterPath mouth;
    mouth.moveTo(n + QPointF(0, r * 0.09));
    mouth.lineTo(n + QPointF(0, r * 0.17));
    mouth.moveTo(n + QPointF(-r * 0.2, r * 0.22));
    mouth.quadTo(n + QPointF(-r * 0.1, r * 0.3), n + QPointF(0, r * 0.17));
    mouth.quadTo(n + QPointF(r * 0.1, r * 0.3), n + QPointF(r * 0.2, r * 0.22));
    p.drawPath(mouth);

    QColor w = m_coat.fur.lightness() < 120 ? QColor(255, 255, 255, 170) : QColor(0, 0, 0, 110);
    p.setPen(QPen(w, 0.8, Qt::SolidLine, Qt::RoundCap));
    for (int i = -1; i <= 1; ++i) {
        p.drawLine(n + QPointF(r * 0.38, r * 0.1 + i * r * 0.07), n + QPointF(r * 1.3, r * (0.02 + i * 0.16)));
        p.drawLine(n + QPointF(-r * 0.38, r * 0.1 + i * r * 0.07), n + QPointF(-r * 1.3, r * (0.02 + i * 0.16)));
    }
}

void WindowCat::drawSit(QPainter& p, bool happy)
{
    // Seated and facing the viewer, like the head: a teardrop body from the neck out to the haunches,
    // front legs straight down the middle, white chest and paws, the tail curled round the front.
    const double G = kGround;
    const double cx = kCx;
    auto P = [&](double x, double y) { return QPointF(cx + x, G - 76 + y); };

    QPainterPath body;
    body.moveTo(P(-8, 40));
    body.cubicTo(P(-13, 47), P(-19, 58), P(-18.5, 68));
    body.cubicTo(P(-18, 74), P(-15, 76), P(-11, 76));
    body.lineTo(P(11, 76));
    body.cubicTo(P(15, 76), P(18, 74), P(18.5, 68));
    body.cubicTo(P(19, 58), P(13, 47), P(8, 40));
    body.closeSubpath();
    drawFur(p, body);

    p.save();
    p.setClipPath(body);
    p.setPen(Qt::NoPen);
    p.setBrush(m_coat.chest);
    QPainterPath bib;
    bib.moveTo(P(0, 38));
    bib.cubicTo(P(6, 42), P(8, 50), P(6, 57));
    bib.cubicTo(P(4.5, 62), P(1.5, 66), P(0, 68));
    bib.cubicTo(P(-1.5, 66), P(-4.5, 62), P(-6, 57));
    bib.cubicTo(P(-8, 50), P(-6, 42), P(0, 38));
    p.drawPath(bib);
    // Front paws, and the hind feet peeking out from under the haunches.
    p.drawEllipse(P(-4.4, 74.6), 4.3, 3.2);
    p.drawEllipse(P(4.4, 74.6), 4.3, 3.2);
    p.drawEllipse(P(-14.5, 75.2), 3.8, 2.4);
    p.drawEllipse(P(14.5, 75.2), 3.8, 2.4);
    p.restore();

    // Haunches and the parting between the front legs.
    QPainterPath lines;
    lines.moveTo(P(-9.5, 56));
    lines.cubicTo(P(-14.5, 60), P(-15, 70), P(-10.5, 75.8));
    lines.moveTo(P(9.5, 56));
    lines.cubicTo(P(14.5, 60), P(15, 70), P(10.5, 75.8));
    lines.moveTo(P(0, 66));
    lines.lineTo(P(0, 75.8));
    drawContour(p, body, lines);

    // Tail from behind the right haunch, round the front paws; the tip flicks.
    const double flick = std::sin(m_t * (happy ? 7.0 : 1.7)) * (happy ? 4.0 : 2.5);
    drawTail(p, P(16, 70), P(24, 81), P(4, 82), P(-12 + flick * 0.4, 78.5 - std::abs(flick) * 0.5), 6);

    const double bob = happy ? std::sin(m_t * 9) * 0.8 : 0;
    drawHead(p, QPointF(cx, G - 45 + bob), 13.5, happy, false, false);
}

void WindowCat::drawWalk(QPainter& p)
{
    const double G = kGround;
    const double phase = m_t * 8.5;
    const double sway = std::sin(m_t * 3) * 6;
    const double bob = std::abs(std::sin(phase)) * 1.2;
    auto P = [&](double x, double y) { return QPointF(kCx - 48 + x, G - 76 + y - bob); };

    // Diagonal gait: each foot swings forward lifted, then pushes back on the ground.
    auto leg = [&](QPointF hip, double offset, bool hind, QPointF* pawOut) {
        const double s = std::sin(phase + offset);
        const double lift = qMax(0.0, std::cos(phase + offset)) * 4.5;
        const QPointF paw(hip.x() + s * 8, G - 2.6 - lift);
        const QPointF knee = (hip + paw) / 2 + QPointF(hind ? -4 : 1.5, 0);
        *pawOut = paw;
        return limb(hip, knee, paw, hind ? 10.5 : 8, 6);
    };
    const QPointF frontHip = P(61, 56), hindHip = P(32, 55);

    drawTail(p, P(26, 49), P(15, 46), P(9, 37), P(13 + sway, 29), 8.5);

    QPointF farFrontPaw, farHindPaw;
    QPainterPath farLegs = leg(frontHip + QPointF(-3, 0), M_PI, false, &farFrontPaw);
    farLegs.addPath(leg(hindHip + QPointF(-3, 0), 0, true, &farHindPaw));
    farLegs.setFillRule(Qt::WindingFill);
    farLegs = farLegs.simplified();
    drawFur(p, farLegs, true);
    drawSocks(p, farLegs, { farFrontPaw, farHindPaw }, 3.4);

    // Torso with a gentle shoulder, a soft back line and a rounded rump; near legs grow out of it.
    QPointF nearFrontPaw, nearHindPaw;
    QPainterPath body;
    body.setFillRule(Qt::WindingFill);
    body.moveTo(P(70, 50));
    body.cubicTo(P(70.5, 57), P(66, 61.5), P(59, 61.5));
    body.cubicTo(P(50, 61.5), P(41, 62), P(33, 61.5));
    body.cubicTo(P(26, 61), P(23, 56.5), P(23.5, 51));
    body.cubicTo(P(24, 45.5), P(29, 43), P(35, 43.5));
    body.cubicTo(P(45, 44.5), P(55, 43.5), P(61, 41.5));
    body.cubicTo(P(67, 40), P(70, 44), P(70, 50));
    body.closeSubpath();
    body.addPath(leg(frontHip, 0, false, &nearFrontPaw));
    body.addPath(leg(hindHip, M_PI, true, &nearHindPaw));
    body = body.simplified();
    drawFur(p, body);

    p.save();
    p.setClipPath(body);
    p.setPen(Qt::NoPen);
    p.setBrush(m_coat.chest);
    p.drawEllipse(P(67, 52), 5.2, 5.6);
    p.restore();
    drawSocks(p, body, { nearFrontPaw, nearHindPaw }, 3.6);
    QPainterPath lines;
    lines.moveTo(P(38, 46));
    lines.cubicTo(P(42, 52), P(41, 58), P(36, 61.5));
    drawContour(p, body, lines);

    drawHead(p, P(69, 39), 11.8, false, false, false);
}

void WindowCat::drawLeap(QPainter& p)
{
    // Stretched out in flight, pitched along the direction of travel.
    const double pitch = qBound(-35.0, qRadiansToDegrees(std::atan2(m_vel.y(), std::abs(m_vel.x()) + 1)) * 0.6, 35.0);
    p.translate(kCx, kGround - 22);
    p.rotate(pitch);
    p.translate(-kCx, -(kGround - 22));
    const double G = kGround;
    auto P = [&](double x, double y) { return QPointF(kCx - 48 + x, G - 76 + y); };
    drawTail(p, P(22, 50), P(12, 50), P(6, 48), P(0, 44), 7);

    const QPointF farFront = P(79, 63), farHind = P(13, 64);
    QPainterPath far = limb(P(59, 54), P(69, 60), farFront, 7, 5.4);
    far.addPath(limb(P(34, 54), P(24, 56), farHind, 9, 5.4));
    far.setFillRule(Qt::WindingFill);
    far = far.simplified();
    drawFur(p, far, true);
    drawSocks(p, far, { farFront, farHind }, 3.4);

    const QPointF nearFront = P(82, 59), nearHind = P(10, 60);
    QPainterPath body;
    body.setFillRule(Qt::WindingFill);
    body.moveTo(P(74, 48.5));
    body.cubicTo(P(74, 53), P(68, 56), P(60, 56));
    body.cubicTo(P(46, 56.5), P(32, 56.5), P(24, 54.5));
    body.cubicTo(P(17, 53), P(16.5, 46.5), P(23, 45.5));
    body.cubicTo(P(34, 44), P(54, 43.5), P(64, 44));
    body.cubicTo(P(70, 44.5), P(74, 45.5), P(74, 48.5));
    body.closeSubpath();
    body.addPath(limb(P(62, 53), P(72, 56), nearFront, 7, 5.4));
    body.addPath(limb(P(32, 53), P(21, 53), nearHind, 9, 5.4));
    body = body.simplified();
    drawFur(p, body);
    p.save();
    p.setClipPath(body);
    p.setPen(Qt::NoPen);
    p.setBrush(m_coat.chest);
    p.drawEllipse(P(60, 56), 10, 3.2);
    p.restore();
    drawSocks(p, body, { nearFront, nearHind }, 3.6);
    drawHead(p, P(73, 43), 12.5, false, true, true);
}

void WindowCat::drawSleep(QPainter& p)
{
    const double G = kGround;
    const double b = 1 + 0.045 * std::sin(m_t * 2.0);
    auto P = [&](double x, double y) { return QPointF(kCx - 48 + x, G - (76 - y) * b); };
    // A loaf: a soft bean of a body with the front paws tucked under, tail wrapped round the front.
    QPainterPath body;
    body.moveTo(P(24, 76));
    body.cubicTo(P(17, 76), P(15, 64), P(23, 58));
    body.cubicTo(P(31, 52), P(52, 51.5), P(63, 56));
    body.cubicTo(P(71, 60), P(75, 70), P(71, 76));
    body.closeSubpath();
    drawFur(p, body);
    QPainterPath lines;
    lines.moveTo(P(30, 60));
    lines.cubicTo(P(37, 62), P(40, 69), P(37, 75));
    drawContour(p, body, lines);
    drawTail(p, P(21, 72), P(20, 80), P(46, 80), P(70, 74), 7);
    const double saved = m_blink;
    m_blink = 1;
    drawHead(p, QPointF(kCx + 13, G - 15), 11, false, false, false);
    m_blink = saved;
}

void WindowCat::drawHanging(QPainter& p, bool falling, bool byPaws)
{
    // Held by the scruff (or falling): head up top, a pear-shaped body below, legs hanging or splayed.
    const double kick = std::sin(m_t * 14) * (falling ? 2.5 : 1.5);
    const double spread = falling ? 1.0 : 0.0;
    const double tailSway = std::sin(m_t * 5) * 10;
    if (falling) {
        drawTail(p, QPointF(kCx, 62), QPointF(kCx + 4, 72), QPointF(kCx + 16, 66), QPointF(kCx + 22 + tailSway * 0.4, 54), 7);
    } else {
        drawTail(p, QPointF(kCx - 2, 62), QPointF(kCx - 6, 72), QPointF(kCx - 16 + tailSway, 76), QPointF(kCx - 22 + tailSway, 82), 7);
    }

    QPointF fl(kCx - 10 - 9 * spread + kick, 52 - 10 * spread), fr(kCx + 10 + 9 * spread - kick, 52 - 10 * spread);
    if (byPaws) {
        fl = QPointF(kCx - 6, kGripY + 2);
        fr = QPointF(kCx + 6, kGripY + 2);
    }
    const QPointF hl(kCx - 8 - 10 * spread - kick, 77 - 8 * spread), hr(kCx + 8 + 10 * spread + kick, 77 - 8 * spread);
    QPainterPath body;
    body.setFillRule(Qt::WindingFill);
    body.moveTo(kCx, 32);
    body.cubicTo(kCx + 9, 32, kCx + 10.5, 46, kCx + 10.5, 55);
    body.cubicTo(kCx + 10.5, 63, kCx + 7, 67, kCx, 67);
    body.cubicTo(kCx - 7, 67, kCx - 10.5, 63, kCx - 10.5, 55);
    body.cubicTo(kCx - 10.5, 46, kCx - 9, 32, kCx, 32);
    body.closeSubpath();
    if (byPaws) {
        // Arms raised past the cheeks to the paws gripping the cursor.
        body.addPath(limb(QPointF(kCx - 6, 38), QPointF(kCx - 16.5, 21), fl, 6.4, 5.2));
        body.addPath(limb(QPointF(kCx + 6, 38), QPointF(kCx + 16.5, 21), fr, 6.4, 5.2));
    } else {
        body.addPath(limb(QPointF(kCx - 6, 39), QPointF(kCx - 9 - 4 * spread, 46), fl, 6.4, 5.4));
        body.addPath(limb(QPointF(kCx + 6, 39), QPointF(kCx + 9 + 4 * spread, 46), fr, 6.4, 5.4));
    }
    body.addPath(limb(QPointF(kCx - 6, 60), QPointF(kCx - 8 - 6 * spread, 68), hl, 8.5, 5.4));
    body.addPath(limb(QPointF(kCx + 6, 60), QPointF(kCx + 8 + 6 * spread, 68), hr, 8.5, 5.4));
    body = body.simplified();
    drawFur(p, body);

    p.save();
    p.setClipPath(body);
    p.setPen(Qt::NoPen);
    p.setBrush(m_coat.chest);
    QPainterPath belly;
    belly.moveTo(kCx, 34);
    belly.cubicTo(kCx + 6.5, 40, kCx + 6.5, 58, kCx, 63);
    belly.cubicTo(kCx - 6.5, 58, kCx - 6.5, 40, kCx, 34);
    p.drawPath(belly);
    p.restore();
    drawSocks(p, body, { fl, fr, hl, hr }, 3.3);
    QPainterPath lines;
    lines.moveTo(kCx - 6, 56);
    lines.quadTo(kCx - 8.5, 63, kCx - 7, 70);
    lines.moveTo(kCx + 6, 56);
    lines.quadTo(kCx + 8.5, 63, kCx + 7, 70);
    drawContour(p, body, lines);
    drawHead(p, QPointF(kCx, byPaws ? 27 : 24), 13, false, true, falling || byPaws);
    if (byPaws) {
        // The paws stay in front of the head: they are what holds on.
        QPainterPath paws;
        paws.setFillRule(Qt::WindingFill);
        paws.addEllipse(fl, 3.6, 3.1);
        paws.addEllipse(fr, 3.6, 3.1);
        paws = paws.simplified();
        drawFur(p, paws);
        drawSocks(p, paws, { fl, fr }, 3.3);
    }
}

void WindowCat::drawParticles(QPainter& p)
{
    for (const auto& pt : m_particles) {
        const double fade = 1 - pt.age / pt.life;
        switch (pt.kind) {
            case Particle::Heart: {
                QColor c(0xFF, 0x6B, 0x8B);
                c.setAlphaF(fade);
                const double s = 4 + pt.age * 2;
                const QPointF o = pt.pos;
                QPainterPath h;
                h.moveTo(o + QPointF(0, s * 0.9));
                h.cubicTo(o + QPointF(-s * 1.4, -s * 0.1), o + QPointF(-s * 0.6, -s * 1.1), o + QPointF(0, -s * 0.35));
                h.cubicTo(o + QPointF(s * 0.6, -s * 1.1), o + QPointF(s * 1.4, -s * 0.1), o + QPointF(0, s * 0.9));
                p.setPen(Qt::NoPen);
                p.setBrush(c);
                p.drawPath(h);
                break;
            }
            case Particle::Zzz: {
                QColor c(255, 255, 255);
                c.setAlphaF(fade * 0.8);
                QFont f = p.font();
                f.setBold(true);
                f.setPixelSize(int(8 + pt.age * 4));
                p.setFont(f);
                p.setPen(c);
                p.drawText(pt.pos, "z");
                break;
            }
            case Particle::Star: {
                QColor c(0xF5, 0xF5, 0xF7);
                c.setAlphaF(fade);
                drawSparkle(p, pt.pos, 3.5, c);
                break;
            }
            case Particle::Dust: {
                QColor c(200, 200, 206);
                c.setAlphaF(fade * 0.45);
                p.setPen(Qt::NoPen);
                p.setBrush(c);
                p.drawEllipse(pt.pos, 2.5 + pt.age * 9, 1.8 + pt.age * 5);
                break;
            }
        }
    }
}

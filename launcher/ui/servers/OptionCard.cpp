// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "OptionCard.h"

#include <QPainter>
#include <QPainterPath>

namespace {
constexpr int kPadX = 16;
constexpr int kPadY = 12;
constexpr int kGap = 4;

QFont titleFont(const QFont& base)
{
    QFont f = base;
    f.setPixelSize(14);
    f.setWeight(QFont::DemiBold);
    return f;
}

QFont bodyFont(const QFont& base)
{
    QFont f = base;
    f.setPixelSize(12);
    return f;
}
}  // namespace

OptionCard::OptionCard(QString title, QString description, QWidget* parent)
    : QAbstractButton(parent), m_title(std::move(title)), m_description(std::move(description))
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setAccessibleName(m_title);
    setAccessibleDescription(m_description);
    setToolTip(m_title);  // the title may be shortened on a narrow card
}

void OptionCard::setBadge(const QString& badge)
{
    m_badge = badge;
    update();
}

int OptionCard::heightForWidth(int width) const
{
    const QFontMetrics title(titleFont(font()));
    const QFontMetrics body(bodyFont(font()));
    const int textWidth = std::max(80, width - 2 * kPadX);
    const QRect bodyRect = body.boundingRect(QRect(0, 0, textWidth, 1000), Qt::TextWordWrap, m_description);
    return kPadY * 2 + title.height() + (m_description.isEmpty() ? 0 : kGap + bodyRect.height());
}

QSize OptionCard::sizeHint() const
{
    return { 260, heightForWidth(260) };
}

void OptionCard::enterEvent(QEnterEvent* event)
{
    m_hover = true;
    update();
    QAbstractButton::enterEvent(event);
}

void OptionCard::leaveEvent(QEvent* event)
{
    m_hover = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void OptionCard::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath shape;
    shape.addRoundedRect(r, 12, 12);

    const bool checked = isChecked();
    QColor fill(255, 255, 255, checked ? 22 : (m_hover ? 14 : 8));
    if (isDown()) {
        fill.setAlpha(28);
    }
    p.fillPath(shape, fill);
    QPen border(checked ? QColor(255, 255, 255, 220) : QColor(255, 255, 255, m_hover ? 46 : 26), checked ? 1.6 : 1.0);
    p.setPen(border);
    p.drawPath(shape);
    if (hasFocus()) {
        p.setPen(QPen(QColor(10, 132, 255, 160), 2));
        p.drawRoundedRect(r.adjusted(2, 2, -2, -2), 10, 10);
    }

    QColor text = isEnabled() ? QColor(245, 245, 247) : QColor(110, 110, 115);
    QColor sub = isEnabled() ? QColor(161, 161, 166) : QColor(90, 90, 95);

    const QFont tf = titleFont(font());
    const QFontMetrics tm(tf);
    int x = kPadX;
    int y = kPadY;
    QFont bf = bodyFont(font());
    bf.setPixelSize(10);
    bf.setWeight(QFont::DemiBold);
    const QFontMetrics bm(bf);
    const int bw = m_badge.isEmpty() ? 0 : bm.horizontalAdvance(m_badge) + 12;
    // A long (translated) title is shortened so the badge still fits on the card; the tooltip has it all.
    const int titleRoom = std::max(0, width() - 2 * kPadX - (bw ? bw + 8 : 0));
    const QString title = tm.elidedText(m_title, Qt::ElideRight, titleRoom);
    p.setFont(tf);
    p.setPen(text);
    p.drawText(QRect(x, y, titleRoom, tm.height()), Qt::AlignLeft | Qt::AlignVCenter, title);

    if (!m_badge.isEmpty()) {
        const QRectF badge(x + tm.horizontalAdvance(title) + 8, y + (tm.height() - 16) / 2.0, bw, 16);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, checked ? 230 : 40));
        p.drawRoundedRect(badge, 8, 8);
        p.setFont(bf);
        p.setPen(checked ? QColor(14, 14, 16) : text);
        p.drawText(badge, Qt::AlignCenter, m_badge);
    }

    if (!m_description.isEmpty()) {
        p.setFont(bodyFont(font()));
        p.setPen(sub);
        p.drawText(QRect(x, y + tm.height() + kGap, width() - 2 * kPadX, height() - y - tm.height() - kGap), Qt::TextWordWrap,
                   m_description);
    }
}

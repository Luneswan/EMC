// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QAbstractButton>

// A large, checkable choice with a title, a one-line explanation and an optional badge — used for
// the big decisions in the server wizard (edition, type), where a combo box would hide the options.
class OptionCard : public QAbstractButton {
    Q_OBJECT
   public:
    OptionCard(QString title, QString description, QWidget* parent = nullptr);

    void setBadge(const QString& badge);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return { 160, sizeHint().height() }; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;

   protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

   private:
    QString m_title;
    QString m_description;
    QString m_badge;
    bool m_hover = false;
};

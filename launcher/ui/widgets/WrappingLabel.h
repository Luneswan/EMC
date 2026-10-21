// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QEvent>
#include <QLabel>

// A word-wrapped label that always keeps room for all of its lines. Layouts size a wrapped label
// from a guessed width, and a height measured at construction is taken before the theme's font is
// applied, so the last line gets cut off. This one re-measures whenever its font, style, text or
// width changes.
class WrappingLabel : public QLabel {
   public:
    explicit WrappingLabel(const QString& text = {}, QWidget* parent = nullptr) : QLabel(text, parent)
    {
        setWordWrap(true);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    }

    void setText(const QString& text)
    {
        QLabel::setText(text);
        fit();
    }

   protected:
    void changeEvent(QEvent* event) override
    {
        QLabel::changeEvent(event);
        if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) {
            fit();
        }
    }
    void resizeEvent(QResizeEvent* event) override
    {
        QLabel::resizeEvent(event);
        fit();
    }
    void showEvent(QShowEvent* event) override
    {
        QLabel::showEvent(event);
        fit();
    }

   private:
    void fit()
    {
        if (width() <= 0) {
            return;
        }
        const int needed = heightForWidth(width());
        if (needed > 0 && minimumHeight() != needed) {
            setMinimumHeight(needed);
        }
    }
};

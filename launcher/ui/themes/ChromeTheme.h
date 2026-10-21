// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include "FusionTheme.h"

// EMC's default look: black surfaces, white type, and brushed-silver controls.
// Colour is left to content (instance icons, skins); the chrome itself stays monochrome.
class ChromeTheme : public FusionTheme {
   public:
    virtual ~ChromeTheme() {}

    QString id() override;
    QString name() override;
    QString tooltip() override;
    bool hasStyleSheet() override;
    QString appStyleSheet() override;
    QPalette colorScheme() override;
    double fadeAmount() override;
    QColor fadeColor() override;
};

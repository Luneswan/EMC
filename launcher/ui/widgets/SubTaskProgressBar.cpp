// SPDX-License-Identifier: GPL-3.0-only
/*
 *  PrismLaucher - Minecraft Launcher
 *  Copyright (C) 2022 Rachel Powers <508861+Ryex@users.noreply.github.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include "SubTaskProgressBar.h"
#include "ui_SubTaskProgressBar.h"

#include <QRegularExpression>
#include <QResizeEvent>
#include <QUrl>

unique_qobject_ptr<SubTaskProgressBar> SubTaskProgressBar::create(QWidget* parent)
{
    auto progress_bar = new SubTaskProgressBar(parent);
    return unique_qobject_ptr<SubTaskProgressBar>(progress_bar);
}

SubTaskProgressBar::SubTaskProgressBar(QWidget* parent) : QWidget(parent), ui(new Ui::SubTaskProgressBar)
{
    ui->setupUi(this);
    // EMC: one line per step. Long statuses (download URLs) are shortened in the middle instead of
    // wrapping into a fixed-height row, and the percentage is plain text beside a thin bar.
    ui->statusLabel->setWordWrap(false);
    ui->statusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    ui->statusLabel->setFont(QFont());
    ui->statusDetailsLabel->setFont(QFont());
    ui->statusDetailsLabel->setProperty("role", "body");
    ui->progressBar->setTextVisible(false);
    ui->verticalLayout->setSpacing(6);
    ui->verticalLayout->setContentsMargins(0, 4, 0, 6);
}
SubTaskProgressBar::~SubTaskProgressBar()
{
    delete ui;
}

void SubTaskProgressBar::setRange(int min, int max)
{
    ui->progressBar->setRange(min, max);
}

void SubTaskProgressBar::setValue(int value)
{
    ui->progressBar->setValue(value);
    refreshLabels();
}

void SubTaskProgressBar::setStatus(QString status)
{
    m_status = status;
    refreshLabels();
}

void SubTaskProgressBar::setDetails(QString details)
{
    m_details = details;
    refreshLabels();
}

void SubTaskProgressBar::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    refreshLabels();
}

void SubTaskProgressBar::refreshLabels()
{
    const auto* bar = ui->progressBar;
    QString details = m_details;
    if (details.isEmpty() && bar->maximum() > bar->minimum()) {
        details = QString("%1%").arg(qRound(100.0 * (bar->value() - bar->minimum()) / (bar->maximum() - bar->minimum())));
    }
    ui->statusDetailsLabel->setText(details);

    // "Requesting https://host/a/b/file" reads as noise; name the file being fetched instead.
    QString shown = m_status;
    static const QRegularExpression request(QStringLiteral("^Requesting (https?://\\S+)$"));
    if (const auto match = request.match(m_status); match.hasMatch()) {
        const QUrl url(match.captured(1));
        const QString file = url.fileName();
        shown = tr("Downloading %1").arg(file.isEmpty() ? url.host() : file);
    }
    const int width = ui->statusLabel->width() > 40 ? ui->statusLabel->width() : 360;
    ui->statusLabel->setText(ui->statusLabel->fontMetrics().elidedText(shown, Qt::ElideMiddle, width));
    ui->statusLabel->setToolTip(m_status);
}

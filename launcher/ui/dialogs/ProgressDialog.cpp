/// SPDX-License-Identifier: GPL-3.0-only
/*
 *  PrismLaucher - Minecraft Launcher
 *  Copyright (C) 2023 Rachel Powers <508861+Ryex@users.noreply.github.com>
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
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2013-2021 MultiMC Contributors
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#include "ProgressDialog.h"
#include <QPoint>
#include "ui_ProgressDialog.h"

#include <QDebug>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QRegularExpression>
#include <limits>

#include "tasks/Task.h"

// map a value in a numeric range of an arbitrary type to between 0 and INT_MAX
// for getting the best precision out of the qt progress bar
template <typename T>
std::tuple<int, int> map_int_zero_max(T current, T range_max, T range_min)
    requires(std::is_arithmetic_v<T>)
{
    int int_max = std::numeric_limits<int>::max();

    auto type_range = range_max - range_min;
    if (type_range == 0) {
        return { 0, int_max };
    }
    double percentage = static_cast<double>(current - range_min) / static_cast<double>(type_range);
    int mapped_current = percentage * int_max;

    return { mapped_current, int_max };
}

namespace {

// Long labels are cut with an ellipsis rather than wrapped, so the window never jumps in height.
void setElided(QLabel* label, const QString& text)
{
    const int width = label->width() > 40 ? label->width() : 320;
    label->setText(label->fontMetrics().elidedText(text, Qt::ElideRight, width));
    label->setToolTip(text.size() > 40 ? text : QString());
}

// "Downloading 6be14445d7574aed..." says nothing to a player.
QString readableStep(QString status)
{
    static const QRegularExpression hash(QStringLiteral("\\b[0-9a-fA-F]{20,}\\b"));
    if (status.contains(hash))
        return QObject::tr("Downloading game files");
    return status.trimmed();
}

}  // namespace

ProgressDialog::ProgressDialog(QWidget* parent) : QDialog(parent), ui(new Ui::ProgressDialog)
{
    ui->setupUi(this);
    // EMC: thin bar, percentage as text next to the status instead of inside the bar.
    ui->globalProgressBar->setTextVisible(false);
    ui->globalStatusLabel->setProperty("role", "cardTitle");
    ui->globalStatusLabel->setWordWrap(false);
    ui->globalStatusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    ui->globalStatusLabel->setMinimumSize(0, 0);
    ui->globalStatusDetailsLabel->setProperty("role", "note");
    ui->verticalLayout->setSpacing(10);
    ui->verticalLayout->setContentsMargins(22, 20, 22, 18);
    // EMC never shows the per-file list; see refreshStepLine().
    ui->taskProgressScrollArea->setHidden(true);

    m_stepLabel = new QLabel(this);
    m_stepPercent = new QLabel(this);
    for (auto* label : { m_stepLabel, m_stepPercent })
        label->setProperty("role", "note");
    m_stepLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_stepPercent->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_stepLabel->setText(QStringLiteral(" "));
    auto* stepRow = new QHBoxLayout();
    stepRow->setSpacing(12);
    stepRow->addWidget(m_stepLabel, 1);
    stepRow->addWidget(m_stepPercent);
    ui->verticalLayout->insertLayout(2, stepRow);
    ui->verticalLayout->setAlignment(ui->skipButton, Qt::AlignRight);
    ui->skipButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    ui->skipButton->setMinimumWidth(96);
    setSizeGripEnabled(false);
    m_stepTick.start();
    this->setWindowFlags(this->windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setAttribute(Qt::WidgetAttribute::WA_QuitOnClose, true);
    changeProgress(0, 100);
    updateSize(true);
    hideSkipButton();
}

void ProgressDialog::hideSkipButton()
{
    ui->skipButton->setEnabled(false);
    ui->skipButton->setVisible(false);
    updateSize();
}

void ProgressDialog::showSkipButton(const QString& label)
{
    ui->skipButton->setAutoDefault(false);
    ui->skipButton->setDefault(false);
    ui->skipButton->setFocusPolicy(Qt::ClickFocus);
    ui->skipButton->setEnabled(true);
    ui->skipButton->setVisible(true);
    ui->skipButton->setText(label);
    updateSize();
}

void ProgressDialog::on_skipButton_clicked(bool checked)
{
    Q_UNUSED(checked);
    if (ui->skipButton->isEnabled())  // prevent other triggers from aborting
        m_task->abort();
}

ProgressDialog::~ProgressDialog()
{
    for (auto conn : this->m_taskConnections) {
        disconnect(conn);
    }
    delete ui;
}

void ProgressDialog::updateSize(bool recenterParent)
{
    QSize lastSize = this->size();
    QPoint lastPos = this->pos();
    int minHeight = ui->globalStatusDetailsLabel->minimumSize().height() + (ui->verticalLayout->spacing() * 2);
    minHeight += ui->globalProgressBar->minimumSize().height() + ui->verticalLayout->spacing();
    if (!ui->taskProgressScrollArea->isHidden())
        minHeight += ui->taskProgressScrollArea->minimumSizeHint().height() + ui->verticalLayout->spacing();
    if (ui->skipButton->isVisible())
        minHeight += ui->skipButton->height() + ui->verticalLayout->spacing();
    minHeight = std::max(minHeight, 60);
    QSize minSize = QSize(480, minHeight);

    setMinimumSize(minSize);
    adjustSize();

    QSize newSize = this->size();
    // if the current window is a different size
    auto parent = this->parentWidget();
    if (recenterParent && parent) {
        auto newX = std::max(0, parent->x() + ((parent->width() - newSize.width()) / 2));
        auto newY = std::max(0, parent->y() + ((parent->height() - newSize.height()) / 2));
        this->move(newX, newY);
    } else if (lastSize != newSize) {
        // center on old position after resize
        QSize sizeDiff = lastSize - newSize;  // last size was smaller, the results should be negative
        auto newX = std::max(0, lastPos.x() + (sizeDiff.width() / 2));
        auto newY = std::max(0, lastPos.y() + (sizeDiff.height() / 2));
        this->move(newX, newY);
    }
}

int ProgressDialog::execWithTask(Task* task)
{
    this->m_task = task;

    if (!task) {
        qDebug() << "Programmer error: Progress dialog created with null task.";
        return QDialog::DialogCode::Accepted;
    }

    QDialog::DialogCode result{};
    if (handleImmediateResult(result)) {
        return result;
    }

    // Connect signals.
    this->m_taskConnections.push_back(connect(task, &Task::started, this, &ProgressDialog::onTaskStarted));
    this->m_taskConnections.push_back(connect(task, &Task::failed, this, &ProgressDialog::onTaskFailed));
    this->m_taskConnections.push_back(connect(task, &Task::succeeded, this, &ProgressDialog::onTaskSucceeded));
    this->m_taskConnections.push_back(connect(task, &Task::status, this, &ProgressDialog::changeStatus));
    this->m_taskConnections.push_back(connect(task, &Task::details, this, &ProgressDialog::changeStatus));
    this->m_taskConnections.push_back(connect(task, &Task::stepProgress, this, &ProgressDialog::changeStepProgress));
    this->m_taskConnections.push_back(connect(task, &Task::progress, this, &ProgressDialog::changeProgress));
    this->m_taskConnections.push_back(connect(task, &Task::aborted, this, &ProgressDialog::hide));
    this->m_taskConnections.push_back(connect(task, &Task::abortStatusChanged, ui->skipButton, &QPushButton::setEnabled));
    this->m_taskConnections.push_back(connect(task, &Task::abortButtonTextChanged, ui->skipButton, &QPushButton::setText));

    m_is_multi_step = task->isMultiStep();
    updateSize();

    // It's a good idea to start the task after we entered the dialog's event loop :^)
    if (!task->isRunning()) {
        QMetaObject::invokeMethod(task, &Task::start, Qt::QueuedConnection);
    } else {
        changeStatus(task->getStatus());
        changeProgress(task->getProgress(), task->getTotalProgress());
    }

    return QDialog::exec();
}

// TODO: only provide the unique_ptr overloads
int ProgressDialog::execWithTask(std::unique_ptr<Task>&& task)
{
    connect(this, &ProgressDialog::destroyed, task.get(), &Task::deleteLater);
    return execWithTask(task.release());
}
int ProgressDialog::execWithTask(std::unique_ptr<Task>& task)
{
    connect(this, &ProgressDialog::destroyed, task.get(), &Task::deleteLater);
    return execWithTask(task.release());
}

bool ProgressDialog::handleImmediateResult(QDialog::DialogCode& result)
{
    if (m_task->isFinished()) {
        if (m_task->wasSuccessful()) {
            result = QDialog::Accepted;
        } else {
            result = QDialog::Rejected;
        }
        return true;
    }
    return false;
}

Task* ProgressDialog::getTask()
{
    return m_task;
}

void ProgressDialog::onTaskStarted() {}

void ProgressDialog::onTaskFailed([[maybe_unused]] QString failure)
{
    reject();
    hide();
}

void ProgressDialog::onTaskSucceeded()
{
    accept();
    hide();
}

void ProgressDialog::changeStatus([[maybe_unused]] const QString& status)
{
    // The headline is what is happening ("Downloading game files"); the step counter sits quietly on the right.
    const QString taskStatus = m_task->getStatus().trimmed();
    const QString taskDetails = m_task->getDetails().trimmed();
    setElided(ui->globalStatusLabel, taskDetails.isEmpty() ? taskStatus : taskDetails);
    m_sideText = taskDetails.isEmpty() ? QString() : taskStatus;
    refreshSideText();
}

void ProgressDialog::refreshSideText()
{
    if (!m_sideText.isEmpty())
        ui->globalStatusDetailsLabel->setText(m_sideText);
    else
        ui->globalStatusDetailsLabel->setText(m_percent >= 0 ? QString("%1%").arg(m_percent) : QString());
}

void ProgressDialog::refreshStepLine(bool force)
{
    // Text that changes forty times a second is unreadable; refresh it a few times a second at most.
    if (!force && m_stepTick.elapsed() < 150)
        return;
    m_stepTick.restart();

    // Show the longest-running step that is still going: that is the stage, the newer ones are its files.
    const Step* shown = nullptr;
    QUuid shownId;
    for (auto it = m_steps.cbegin(); it != m_steps.cend(); ++it) {
        if (it->status.isEmpty())
            continue;
        if (!shown || it->order < shown->order) {
            shown = &it.value();
            shownId = it.key();
        }
    }
    if (!shown)
        return;  // keep the last line rather than blinking empty between stages
    m_shownStep = shownId;
    setElided(m_stepLabel, shown->status);
    if (shown->total > 0)
        m_stepPercent->setText(QString("%1%").arg(qBound<qint64>(qint64(0), 100 * shown->current / shown->total, qint64(100))));
    else
        m_stepPercent->clear();
}

void ProgressDialog::changeStepProgress(const TaskStepProgress& task_progress)
{
    m_is_multi_step = true;
    if (task_progress.isDone()) {
        m_steps.remove(task_progress.uid);
        refreshStepLine(task_progress.uid == m_shownStep);
        return;
    }
    auto it = m_steps.find(task_progress.uid);
    if (it == m_steps.end())
        it = m_steps.insert(task_progress.uid, Step{ m_nextStepOrder++, {}, 0, -1 });
    it->status = readableStep(task_progress.status);
    it->current = task_progress.current;
    it->total = task_progress.total;
    refreshStepLine(m_shownStep.isNull());
}

void ProgressDialog::changeProgress(qint64 current, qint64 total)
{
    const auto [mapped_current, mapped_total] = map_int_zero_max<qint64>(current, total, 0);
    if (total <= 0) {
        ui->globalProgressBar->setRange(0, 0);
    } else {
        ui->globalProgressBar->setRange(0, mapped_total);
        ui->globalProgressBar->setValue(mapped_current);
    }
    m_percent = total > 0 ? qRound(100.0 * current / total) : -1;
    refreshSideText();
}

void ProgressDialog::keyPressEvent(QKeyEvent* e)
{
    if (ui->skipButton->isVisible()) {
        if (e->key() == Qt::Key_Escape) {
            on_skipButton_clicked(true);
            return;
        } else if (e->key() == Qt::Key_Tab) {
            ui->skipButton->setFocusPolicy(Qt::StrongFocus);
            ui->skipButton->setFocus();
            ui->skipButton->setAutoDefault(true);
            ui->skipButton->setDefault(true);
            return;
        }
    }
    QDialog::keyPressEvent(e);
}

void ProgressDialog::closeEvent(QCloseEvent* e)
{
    if (m_task && m_task->isRunning()) {
        e->ignore();
    } else {
        QDialog::closeEvent(e);
    }
}

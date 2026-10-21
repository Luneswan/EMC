// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "ImportServerDialog.h"

#include <QCheckBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPushButton>
#include <QRadioButton>
#include <QStandardPaths>
#include <QVBoxLayout>

#include "server/ServerInstance.h"
#include "server/ServerSoftware.h"
#include "ui/dialogs/CustomMessageBox.h"
#include "ui/dialogs/ProgressDialog.h"

ImportServerDialog::ImportServerDialog(ServerList* list, QWidget* parent) : QDialog(parent), m_list(list)
{
    setWindowTitle(tr("Add Existing Server"));
    setModal(true);
    setAcceptDrops(true);
    resize(600, 480);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(28, 22, 28, 20);
    root->setSpacing(12);

    auto* title = new QLabel(tr("Add a server you already have"), this);
    title->setProperty("role", "title");
    auto* intro = new QLabel(tr("Pick the folder with server.properties and the server .jar (or bedrock_server.exe). EMC works out "
                                "the type, version, plugins and mods. You can also drop the folder here."),
                             this);
    intro->setProperty("role", "body");
    intro->setWordWrap(true);
    root->addWidget(title);
    root->addWidget(intro);

    auto* pickRow = new QHBoxLayout;
    m_path = new QLabel(tr("No folder chosen"), this);
    m_path->setProperty("role", "note");
    m_path->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* choose = new QPushButton(tr("Choose Folder…"), this);
    pickRow->addWidget(m_path, 1);
    pickRow->addWidget(choose);
    root->addSpacing(6);
    root->addLayout(pickRow);

    m_problem = new QLabel(this);
    m_problem->setWordWrap(true);
    m_problem->setStyleSheet("color: #FF9F0A;");
    m_problem->hide();
    root->addWidget(m_problem);

    m_details = new QWidget(this);
    auto* details = new QVBoxLayout(m_details);
    details->setContentsMargins(0, 6, 0, 0);
    details->setSpacing(10);
    m_summary = new QLabel(m_details);
    m_summary->setProperty("role", "body");
    m_summary->setWordWrap(true);
    m_summary->setTextFormat(Qt::RichText);
    details->addWidget(m_summary);
    auto* form = new QFormLayout;
    m_name = new QLineEdit(m_details);
    m_name->setMaxLength(48);
    form->addRow(tr("Name in EMC"), m_name);
    details->addLayout(form);
    m_inPlace = new QRadioButton(tr("Run it where it is"), m_details);
    m_inPlace->setChecked(true);
    m_copy = new QRadioButton(tr("Make a copy for EMC (the original stays untouched)"), m_details);
    details->addWidget(m_inPlace);
    details->addWidget(m_copy);
    m_eula = new QCheckBox(tr("I agree to the Minecraft End User License Agreement"), m_details);
    details->addWidget(m_eula);
    root->addWidget(m_details);
    m_details->hide();
    root->addStretch(1);

    auto* buttons = new QHBoxLayout;
    auto* cancel = new QPushButton(tr("Cancel"), this);
    m_add = new QPushButton(tr("Add Server"), this);
    m_add->setProperty("accent", true);
    m_add->setDefault(true);
    m_add->setMinimumWidth(120);
    buttons->addWidget(cancel);
    buttons->addStretch(1);
    buttons->addWidget(m_add);
    root->addLayout(buttons);

    connect(choose, &QPushButton::clicked, this, &ImportServerDialog::chooseFolder);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_add, &QPushButton::clicked, this, &ImportServerDialog::import);
    connect(m_name, &QLineEdit::textChanged, this, &ImportServerDialog::updateButtons);
    connect(m_eula, &QCheckBox::toggled, this, &ImportServerDialog::updateButtons);
    updateButtons();
}

void ImportServerDialog::chooseFolder()
{
    const QString start = m_scan.dir.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DesktopLocation) : m_scan.dir;
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Server folder"), start);
    if (!dir.isEmpty()) {
        scanFolder(dir);
    }
}

void ImportServerDialog::scanFolder(const QString& dir)
{
    m_scan = ServerImporter::scan(dir);
    m_path->setText(QDir::toNativeSeparators(m_scan.dir));
    m_problem->setVisible(!m_scan.valid || m_scan.running);
    m_details->setVisible(m_scan.valid);
    if (!m_scan.valid) {
        m_problem->setText(m_scan.problem);
        updateButtons();
        return;
    }
    if (m_scan.running) {
        m_problem->setText(tr("This server seems to be running right now. Stop it first, then add it."));
    }

    const auto* info = ServerSoftware::find(m_scan.software);
    QStringList lines;
    QString type = info ? info->name : m_scan.software;
    if (!m_scan.softwareNote.isEmpty()) {
        type = m_scan.softwareNote;
    }
    lines << tr("<b>%1</b> %2").arg(type.toHtmlEscaped(),
                                    m_scan.mcVersion.isEmpty() ? tr("(version unknown)") : m_scan.mcVersion.toHtmlEscaped());
    QStringList bits;
    if (m_scan.plugins > 0) {
        bits << (m_scan.plugins == 1 ? tr("1 plugin") : tr("%1 plugins").arg(m_scan.plugins));
    }
    if (m_scan.mods > 0) {
        bits << (m_scan.mods == 1 ? tr("1 mod") : tr("%1 mods").arg(m_scan.mods));
    }
    if (!m_scan.worlds.isEmpty()) {
        bits << (m_scan.worlds.size() == 1 ? tr("world \"%1\"").arg(m_scan.worlds.first().toHtmlEscaped())
                                           : tr("%1 worlds").arg(m_scan.worlds.size()));
    }
    bits << tr("port %1").arg(m_scan.port);
    if (m_scan.crossplay) {
        bits << tr("Bedrock crossplay (Geyser)");
    }
    lines << bits.join("  ·  ");
    m_summary->setText(lines.join("<br>"));
    m_name->setText(m_scan.name);
    m_eula->setVisible(!m_scan.eulaAccepted);
    updateButtons();
}

void ImportServerDialog::updateButtons()
{
    m_add->setEnabled(m_scan.valid && !m_scan.running && !m_name->text().trimmed().isEmpty() &&
                      (m_scan.eulaAccepted || m_eula->isChecked()));
}

void ImportServerDialog::import()
{
    auto task = std::make_unique<ServerImportTask>(m_list, m_scan, m_name->text().trimmed(), m_copy->isChecked(), m_eula->isChecked());
    ProgressDialog progress(this);
    progress.execWithTask(task.get());
    if (task->wasSuccessful() && task->server()) {
        m_imported = task->server();
        accept();
        return;
    }
    if (task->getState() != Task::State::AbortedByUser && !task->failReason().isEmpty()) {
        CustomMessageBox::selectable(this, tr("Could not add the server"), task->failReason(), QMessageBox::Warning)->exec();
    }
}

void ImportServerDialog::dragEnterEvent(QDragEnterEvent* event)
{
    const auto urls = event->mimeData()->urls();
    if (urls.size() == 1 && urls.first().isLocalFile() && QFileInfo(urls.first().toLocalFile()).isDir()) {
        event->acceptProposedAction();
    }
}

void ImportServerDialog::dropEvent(QDropEvent* event)
{
    scanFolder(event->mimeData()->urls().first().toLocalFile());
}

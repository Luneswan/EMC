// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QDialog>
#include <QPointer>

#include "server/ServerImporter.h"
#include "server/ServerInstance.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QWidget;
class ServerInstance;
class ServerList;

// "Add Existing Server": point EMC at a server folder; it works out the rest.
class ImportServerDialog : public QDialog {
    Q_OBJECT
   public:
    ImportServerDialog(ServerList* list, QWidget* parent = nullptr);
    ServerInstance* importedServer() const { return m_imported; }

    void scanFolder(const QString& dir);

   protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

   private:
    void chooseFolder();
    void updateButtons();
    void import();

    ServerList* m_list;
    QPointer<ServerInstance> m_imported;
    ServerScan m_scan;

    QLabel* m_path;
    QWidget* m_details;
    QLabel* m_summary;
    QLabel* m_problem;
    QLineEdit* m_name;
    QRadioButton* m_inPlace;
    QRadioButton* m_copy;
    QCheckBox* m_eula;
    QPushButton* m_add;
};

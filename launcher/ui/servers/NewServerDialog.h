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
#include <QHash>
#include <QList>
#include <QPointer>

#include "server/ServerInstance.h"
#include "server/ServerSoftware.h"

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QVBoxLayout;
class OptionCard;
class ServerInstance;
class ServerList;

// "New Server": a short guided flow — edition, type, version, world, performance, done.
class NewServerDialog : public QDialog {
    Q_OBJECT
   public:
    NewServerDialog(ServerList* list, QWidget* parent = nullptr);
    ServerInstance* createdServer() const { return m_created; }

   private:
    enum Step { EditionStep, TypeStep, VersionStep, WorldStep, ExtrasStep, ReviewStep, StepCount };
    enum class Edition { Java, Bedrock, Both };

    QWidget* buildEditionStep();
    QWidget* buildTypeStep();
    QWidget* buildVersionStep();
    QWidget* buildWorldStep();
    QWidget* buildExtrasStep();
    QWidget* buildReviewStep();
    QWidget* page(const QString& title, const QString& subtitle, QVBoxLayout** body);

    void goTo(int step);
    void next();
    void back();
    void refreshTypeOptions();
    void refreshSoftwareOptions();
    void loadVersions();
    void refreshExtras();
    void refreshReview();
    void updateButtons();
    void create();

    QString software() const;
    Edition edition() const;
    bool stepSkipped(int step) const;

    ServerList* m_list;
    QPointer<ServerInstance> m_created;
    int m_step = EditionStep;

    QStackedWidget* m_stack;
    QLabel* m_stepLabel;
    QPushButton* m_back;
    QPushButton* m_next;

    QButtonGroup* m_editionGroup;
    QButtonGroup* m_categoryGroup;
    QList<OptionCard*> m_categoryCards;
    QButtonGroup* m_softwareGroup;
    QWidget* m_softwareBox;
    QVBoxLayout* m_softwareLayout;

    QComboBox* m_version;
    QLabel* m_versionNote;
    QLineEdit* m_name;
    QHash<QString, QList<ServerVersionEntry>> m_versionCache;
    QString m_loadingFor;

    QComboBox* m_gamemode;
    QComboBox* m_difficulty;
    QLineEdit* m_seed;
    QLineEdit* m_motd;
    QCheckBox* m_hardcore;
    QCheckBox* m_pvp;
    QCheckBox* m_offlinePlayers;
    QCheckBox* m_whitelist;

    QCheckBox* m_autoOptimize;
    QLabel* m_optimizeSummary;
    QWidget* m_manualBox;
    QSlider* m_memory;
    QLabel* m_memoryLabel;
    QSpinBox* m_maxPlayers;
    QWidget* m_starterBox;
    QVBoxLayout* m_starterLayout;
    QList<QCheckBox*> m_starterChecks;

    QLabel* m_review;
    QCheckBox* m_eula;
};

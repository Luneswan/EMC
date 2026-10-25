// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QDialog>
#include <QImage>
#include <QPointer>

#include "minecraft/auth/ElySkinLookup.h"
#include "minecraft/auth/MinecraftAccount.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QNetworkReply;
class QPushButton;
class QTimer;
class SkinFrontView;

// Chooses the Ely.by skin an offline account wears in game. Any Ely.by player's name works; the account keeps its
// own name and UUID, so nothing about its worlds or servers changes.
class OfflineSkinDialog : public QDialog {
    Q_OBJECT
   public:
    OfflineSkinDialog(QWidget* parent, MinecraftAccountPtr account);
    ~OfflineSkinDialog() override;

   private:
    void lookUp();
    void onLookup(const ElySkinLookup::Result& result);
    void setStatus(const QString& text, bool error = false);
    void updateEnabled();
    void save();

    MinecraftAccountPtr m_account;
    ElySkinLookup* m_lookup;
    QPointer<QNetworkReply> m_download;
    QPointer<QNetworkReply> m_capeDownload;
    QTimer* m_debounce;

    SkinFrontView* m_preview;
    QLabel* m_side;
    QCheckBox* m_enabled;
    QLineEdit* m_name;
    QLabel* m_status;
    QPushButton* m_save;

    QByteArray m_skinPng;  // the skin found for the current name, kept for the account's face icon
    QString m_foundFor;    // the name m_skinPng belongs to
    bool m_foundSlim = false;
};

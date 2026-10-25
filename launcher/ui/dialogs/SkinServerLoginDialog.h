// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QDialog>
#include <QPointer>

#include "minecraft/auth/MinecraftAccount.h"

class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QNetworkReply;
class QPushButton;
class QWidget;

// Sign in to Ely.by (or another authlib-injector skin server) with an email or username and a password. The password
// goes to that server only and is never saved; EMC keeps the session token it gets back.
class SkinServerLoginDialog : public QDialog {
    Q_OBJECT
   public:
    // `server` and `loginName` prefill the dialog (signing in again); `reason` explains why it opened.
    static MinecraftAccountPtr newAccount(QWidget* parent, const QString& server = {}, const QString& loginName = {},
                                          const QString& reason = {});

    static const QString kElyServer;

   protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

   private:
    explicit SkinServerLoginDialog(QWidget* parent);

    void signIn();
    void startLogin(const QString& server, const QString& serverName);
    void onLoginFailed(const QString& reason);
    void setBusy(bool busy, const QString& status = {});
    void showError(const QString& text);
    void updateButtons();
    void setServerChoice(const QString& server);
    QString chosenServer() const;
    bool isEly() const;
    void retitle();

    QFormLayout* m_form;
    QLabel* m_title;
    QLabel* m_intro;
    QLineEdit* m_login;
    QLineEdit* m_password;
    QWidget* m_codeRow;
    QLineEdit* m_code;
    QLabel* m_status;
    QLabel* m_links;
    QWidget* m_serverRow;
    QComboBox* m_server;
    QLabel* m_serverToggle;
    QLabel* m_privacy;
    QPushButton* m_cancel;
    QPushButton* m_signIn;

    QPointer<QNetworkReply> m_probe;
    MinecraftAccountPtr m_account;
    shared_qobject_ptr<AuthFlow> m_task;
};

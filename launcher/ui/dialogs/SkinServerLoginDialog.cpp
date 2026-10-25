// SPDX-License-Identifier: GPL-3.0-only
#include "SkinServerLoginDialog.h"

#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>

#include "Application.h"
#include "minecraft/auth/steps/YggdrasilStep.h"

const QString SkinServerLoginDialog::kElyServer = QStringLiteral("https://authserver.ely.by/api/authlib-injector");

namespace {
struct Preset {
    const char* name;
    const char* root;
};
// Well-known authlib-injector servers. Ely.by is first and is what "Sign in with Ely.by" means everywhere in EMC.
const Preset kPresets[] = {
    { "Ely.by", "https://authserver.ely.by/api/authlib-injector" },
    { "LittleSkin", "https://littleskin.cn/api/yggdrasil" },
    { "Drasl (unmojang)", "https://drasl.unmojang.org/authlib-injector" },
};
const char* kDropPrefix = "authlib-injector:yggdrasil-server:";

QString normalizeServer(QString url)
{
    url = url.trimmed();
    if (url.isEmpty())
        return url;
    if (!url.contains("://"))
        url.prepend("https://");
    while (url.endsWith('/'))
        url.chop(1);
    return url;
}

bool isLocalHost(const QUrl& url)
{
    return url.host() == "localhost" || url.host() == "127.0.0.1" || url.host() == "::1";
}
}  // namespace

SkinServerLoginDialog::SkinServerLoginDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Sign in with Ely.by"));
    setModal(true);
    setAcceptDrops(true);
    setMinimumWidth(460);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(28, 24, 28, 20);
    root->setSpacing(10);

    m_title = new QLabel(this);
    m_title->setProperty("role", "title");
    m_intro = new QLabel(this);
    m_intro->setProperty("role", "body");
    m_intro->setWordWrap(true);
    root->addWidget(m_title);
    root->addWidget(m_intro);
    root->addSpacing(8);

    auto* form = m_form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(10);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    m_login = new QLineEdit(this);
    m_login->setPlaceholderText(tr("you@example.com"));
    m_login->setClearButtonEnabled(true);
    form->addRow(tr("Email or username"), m_login);

    m_password = new QLineEdit(this);
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setPlaceholderText(tr("Your Ely.by password"));
    form->addRow(tr("Password"), m_password);

    // Shown only after the server says the account uses two-factor sign-in.
    m_codeRow = new QWidget(this);
    auto* codeLayout = new QHBoxLayout(m_codeRow);
    codeLayout->setContentsMargins(0, 0, 0, 0);
    m_code = new QLineEdit(m_codeRow);
    m_code->setPlaceholderText(tr("6-digit code"));
    m_code->setMaxLength(8);
    m_code->setValidator(new QRegularExpressionValidator(QRegularExpression("^[0-9]{0,8}$"), m_code));
    m_code->setMaximumWidth(140);
    codeLayout->addWidget(m_code);
    codeLayout->addStretch(1);
    form->addRow(tr("Authenticator code"), m_codeRow);
    m_codeRow->setVisible(false);
    form->labelForField(m_codeRow)->setVisible(false);

    m_serverRow = new QWidget(this);
    auto* serverLayout = new QHBoxLayout(m_serverRow);
    serverLayout->setContentsMargins(0, 0, 0, 0);
    m_server = new QComboBox(m_serverRow);
    m_server->setEditable(true);
    m_server->setInsertPolicy(QComboBox::NoInsert);
    for (const auto& preset : kPresets)
        m_server->addItem(QString::fromLatin1(preset.name), QString::fromLatin1(preset.root));
    m_server->lineEdit()->setPlaceholderText(tr("https://your-server/api/yggdrasil"));
    serverLayout->addWidget(m_server, 1);
    form->addRow(tr("Skin server"), m_serverRow);
    m_serverRow->setVisible(false);
    form->labelForField(m_serverRow)->setVisible(false);

    root->addLayout(form);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setProperty("role", "body");
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_status->hide();
    root->addWidget(m_status);

    m_links = new QLabel(this);
    m_links->setProperty("role", "note");
    m_links->setTextFormat(Qt::RichText);
    m_links->setOpenExternalLinks(true);
    root->addWidget(m_links);

    m_privacy = new QLabel(this);
    m_privacy->setProperty("role", "note");
    m_privacy->setWordWrap(true);
    root->addWidget(m_privacy);

    root->addSpacing(6);
    auto* buttons = new QHBoxLayout;
    m_serverToggle = new QLabel(this);
    m_serverToggle->setProperty("role", "note");
    m_serverToggle->setTextFormat(Qt::RichText);
    m_serverToggle->setText(QString("<a href=\"#\" style=\"color:#A1A1A6;\">%1</a>").arg(tr("Use another skin server")));
    m_cancel = new QPushButton(tr("Cancel"), this);
    m_cancel->setAutoDefault(false);
    m_signIn = new QPushButton(tr("Sign In"), this);
    m_signIn->setProperty("accent", true);
    m_signIn->setDefault(true);
    m_signIn->setMinimumWidth(120);
    buttons->addWidget(m_serverToggle);
    buttons->addStretch(1);
    buttons->addWidget(m_cancel);
    buttons->addWidget(m_signIn);
    root->addLayout(buttons);

    connect(m_serverToggle, &QLabel::linkActivated, this, [this, form] {
        m_serverRow->setVisible(true);
        form->labelForField(m_serverRow)->setVisible(true);
        m_serverToggle->hide();
        m_server->setFocus();
        adjustSize();
    });
    connect(m_server, &QComboBox::currentTextChanged, this, [this] {
        // an error from the previous server no longer applies
        if (!m_task && !m_probe)
            m_status->hide();
        retitle();
        updateButtons();
    });
    connect(m_login, &QLineEdit::textChanged, this, &SkinServerLoginDialog::updateButtons);
    connect(m_password, &QLineEdit::textChanged, this, &SkinServerLoginDialog::updateButtons);
    connect(m_code, &QLineEdit::textChanged, this, &SkinServerLoginDialog::updateButtons);
    connect(m_cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_signIn, &QPushButton::clicked, this, &SkinServerLoginDialog::signIn);

    retitle();
    updateButtons();
}

bool SkinServerLoginDialog::isEly() const
{
    return QUrl(chosenServer()).host().endsWith("ely.by");
}

QString SkinServerLoginDialog::chosenServer() const
{
    // A preset shows its name; anything typed is a URL.
    const int index = m_server->findText(m_server->currentText());
    if (index >= 0)
        return m_server->itemData(index).toString();
    return normalizeServer(m_server->currentText());
}

void SkinServerLoginDialog::setServerChoice(const QString& server)
{
    for (int i = 0; i < m_server->count(); i++) {
        if (m_server->itemData(i).toString() == server) {
            m_server->setCurrentIndex(i);
            return;
        }
    }
    m_server->setEditText(server);
    m_serverRow->setVisible(true);
    m_form->labelForField(m_serverRow)->setVisible(true);
    m_serverToggle->hide();
}

void SkinServerLoginDialog::retitle()
{
    if (isEly()) {
        setWindowTitle(tr("Sign in with Ely.by"));
        m_title->setText(tr("Sign in with Ely.by"));
        m_intro->setText(tr("Ely.by is a free account with your own skin and cape. EMC shows it in game, and other players see it "
                            "on servers that use Ely.by."));
        m_password->setPlaceholderText(tr("Your Ely.by password"));
        m_links->setText(QString("<a href=\"https://account.ely.by/register\" style=\"color:#F5F5F7;\">%1</a>"
                                 "&nbsp;&nbsp;·&nbsp;&nbsp;"
                                 "<a href=\"https://account.ely.by/forgot-password\" style=\"color:#A1A1A6;\">%2</a>"
                                 "&nbsp;&nbsp;·&nbsp;&nbsp;"
                                 "<a href=\"https://ely.by/skins\" style=\"color:#A1A1A6;\">%3</a>")
                             .arg(tr("Create a free account"), tr("Forgot password?"), tr("Browse skins")));
        m_privacy->setText(tr("Your password goes only to Ely.by and is never saved. Never type your Microsoft password here."));
    } else {
        const QString host = QUrl(chosenServer()).host();
        const QString name = m_server->findText(m_server->currentText()) >= 0 ? m_server->currentText() : host;
        setWindowTitle(tr("Sign in to a skin server"));
        m_title->setText(name.isEmpty() ? tr("Sign in to a skin server") : tr("Sign in with %1").arg(name));
        m_intro->setText(tr("Any server that supports authlib-injector works. Your skin and cape come from it in game."));
        m_password->setPlaceholderText(tr("Your password on that server"));
        m_links->setText({});
        m_privacy->setText(host.isEmpty() ? QString()
                                          : tr("Your password goes only to %1 and is never saved. Never type your Microsoft "
                                               "password here.")
                                                .arg(host));
    }
    m_links->setVisible(!m_links->text().isEmpty());
}

void SkinServerLoginDialog::updateButtons()
{
    const bool busy = m_task || m_probe;
    const bool codeOk = !m_codeRow->isVisible() || m_code->text().size() >= 6;
    m_signIn->setEnabled(!busy && !m_login->text().trimmed().isEmpty() && !m_password->text().isEmpty() && codeOk &&
                         !chosenServer().isEmpty());
}

void SkinServerLoginDialog::setBusy(bool busy, const QString& status)
{
    for (QWidget* w : { static_cast<QWidget*>(m_login), static_cast<QWidget*>(m_password), static_cast<QWidget*>(m_code),
                        static_cast<QWidget*>(m_server) })
        w->setEnabled(!busy);
    m_signIn->setText(busy ? tr("Signing In…") : tr("Sign In"));
    if (busy) {
        m_status->setStyleSheet({});
        m_status->setText(status);
        m_status->show();
    }
    updateButtons();
}

void SkinServerLoginDialog::showError(const QString& text)
{
    m_status->setStyleSheet("color: #FF6961;");
    m_status->setText(text);
    m_status->show();
}

void SkinServerLoginDialog::signIn()
{
    if (!m_signIn->isEnabled())
        return;
    const QString server = chosenServer();
    const QUrl url(server);
    if (!url.isValid() || url.host().isEmpty()) {
        showError(tr("That skin server address doesn't look right."));
        return;
    }
    if (url.scheme() != "https" && !isLocalHost(url)) {
        showError(tr("The skin server must use https, or your password would travel unencrypted."));
        return;
    }

    // Presets are known API roots. A typed address may be the server's home page, which names its API root in the
    // X-Authlib-Injector-API-Location header.
    if (m_server->findText(m_server->currentText()) >= 0) {
        startLogin(server, m_server->currentText());
        return;
    }
    setBusy(true, tr("Contacting %1…").arg(url.host()));
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("Accept", "application/json");
    m_probe = APPLICATION->network()->get(request);
    connect(m_probe, &QNetworkReply::finished, this, [this, url] {
        auto* reply = m_probe.data();
        m_probe = nullptr;
        reply->deleteLater();
        QUrl apiRoot = reply->url();
        const QByteArray location = reply->rawHeader("X-Authlib-Injector-API-Location");
        if (!location.isEmpty())
            apiRoot = apiRoot.resolved(QUrl(QString::fromUtf8(location)));
        if (apiRoot.scheme() != "https" && !isLocalHost(apiRoot)) {
            setBusy(false);
            showError(tr("%1 sent EMC to an address without https. EMC won't send your password there.").arg(url.host()));
            return;
        }
        if (!location.isEmpty() && apiRoot != reply->url()) {
            // Read the metadata from the real API root.
            setBusy(true, tr("Contacting %1…").arg(apiRoot.host()));
            QNetworkRequest request(apiRoot);
            request.setRawHeader("Accept", "application/json");
            m_probe = APPLICATION->network()->get(request);
            connect(m_probe, &QNetworkReply::finished, this, [this, apiRoot] {
                auto* reply = m_probe.data();
                m_probe = nullptr;
                reply->deleteLater();
                const auto meta = QJsonDocument::fromJson(reply->readAll()).object().value("meta").toObject();
                if (reply->error() != QNetworkReply::NoError || meta.isEmpty()) {
                    setBusy(false);
                    showError(tr("%1 isn't an authlib-injector skin server.").arg(apiRoot.host()));
                    return;
                }
                startLogin(normalizeServer(apiRoot.toString()), meta.value("serverName").toString(apiRoot.host()));
            });
            return;
        }
        const auto meta = QJsonDocument::fromJson(reply->readAll()).object().value("meta").toObject();
        if (reply->error() != QNetworkReply::NoError || meta.isEmpty()) {
            setBusy(false);
            showError(reply->error() == QNetworkReply::NoError || reply->error() >= QNetworkReply::ContentAccessDenied
                          ? tr("%1 isn't an authlib-injector skin server.").arg(url.host())
                          : tr("Couldn't reach %1: %2").arg(url.host(), reply->errorString()));
            return;
        }
        startLogin(normalizeServer(apiRoot.toString()), meta.value("serverName").toString(apiRoot.host()));
    });
}

void SkinServerLoginDialog::startLogin(const QString& server, const QString& serverName)
{
    setBusy(true, tr("Signing in to %1…").arg(serverName));
    m_account = MinecraftAccount::createYggdrasil(server, m_login->text().trimmed());
    QString password = m_password->text();
    if (m_codeRow->isVisible())
        password += ":" + m_code->text();
    m_task = m_account->loginWithPassword(password);
    connect(m_task.get(), &Task::status, this, [this](const QString& status) { m_status->setText(status); });
    connect(m_task.get(), &Task::failed, this, &SkinServerLoginDialog::onLoginFailed);
    connect(m_task.get(), &Task::succeeded, this, [this] {
        m_task.reset();
        QDialog::accept();
    });
    m_task->start();
}

void SkinServerLoginDialog::onLoginFailed(const QString& reason)
{
    const bool needsCode = m_account && m_account->accountData()->needsOneTimeCode;
    m_task.reset();
    setBusy(false);
    if (needsCode && !m_codeRow->isVisible()) {
        // First refusal: the account has two-factor sign-in. Ask for the code without calling it an error.
        m_codeRow->setVisible(true);
        m_form->labelForField(m_codeRow)->setVisible(true);
        m_status->setStyleSheet({});
        m_status->setText(tr("This account uses two-factor sign-in. Enter the code from your authenticator app."));
        m_status->show();
        m_code->setFocus();
        adjustSize();
        return;
    }
    if (m_codeRow->isVisible() && reason == YggdrasilStep::tr("Wrong email, username or password.")) {
        showError(tr("Wrong password or authenticator code."));
        m_code->clear();
        m_code->setFocus();
        return;
    }
    showError(reason);
    m_password->setFocus();
    m_password->selectAll();
}

void SkinServerLoginDialog::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasText() && event->mimeData()->text().startsWith(kDropPrefix))
        event->acceptProposedAction();
}

void SkinServerLoginDialog::dropEvent(QDropEvent* event)
{
    // Skin server websites offer a draggable "authlib-injector:yggdrasil-server:<url>" link.
    const QString text = event->mimeData()->text();
    if (!text.startsWith(kDropPrefix))
        return;
    const QString server = normalizeServer(QUrl::fromPercentEncoding(text.mid(int(strlen(kDropPrefix))).toUtf8()));
    if (QUrl(server).isValid())
        setServerChoice(server);
    event->acceptProposedAction();
}

MinecraftAccountPtr SkinServerLoginDialog::newAccount(QWidget* parent, const QString& server, const QString& loginName,
                                                      const QString& reason)
{
    SkinServerLoginDialog dialog(parent);
    if (!server.isEmpty())
        dialog.setServerChoice(server);
    if (!loginName.isEmpty()) {
        dialog.m_login->setText(loginName);
        dialog.m_password->setFocus();
    } else {
        dialog.m_login->setFocus();
    }
    if (!reason.isEmpty()) {
        dialog.m_status->setStyleSheet({});
        dialog.m_status->setText(reason);
        dialog.m_status->show();
    }
    dialog.adjustSize();
    if (dialog.exec() == QDialog::Accepted)
        return dialog.m_account;
    return nullptr;
}

// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "ServerNetworkPage.h"

#include <QButtonGroup>
#include <QClipboard>
#include <QDesktopServices>
#include <QDnsLookup>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QHostInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QUrl>
#include <QVBoxLayout>

#include "OptionCard.h"
#include "server/PlayitAgent.h"
#include "server/ServerFirewall.h"
#include "server/ServerHosting.h"
#include "server/ServerInstance.h"
#include "server/ServerNetwork.h"

namespace {
QLabel* label(const QString& text, const char* role, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    l->setProperty("role", role);
    l->setWordWrap(true);
    return l;
}

QString line(bool ok, const QString& text)
{
    return QString("%1&nbsp;&nbsp;%2").arg(ok ? QString("&#10003;") : QString("&#10007;"), text.toHtmlEscaped());
}

// Status lines take no space while they are empty.
void setNote(QLabel* label, const QString& text)
{
    label->setText(text);
    label->setVisible(!text.isEmpty());
}

bool isIp(const QString& host)
{
    return QHostAddress(host).protocol() == QAbstractSocket::IPv4Protocol;
}

// The SRV record Java players' games look up; target is empty when there is none.
void lookupSrv(const QString& name, QObject* context, std::function<void(const QString& target, int port)> done)
{
    auto* dns = new QDnsLookup(QDnsLookup::SRV, "_minecraft._tcp." + name, context);
    QObject::connect(dns, &QDnsLookup::finished, context, [dns, done] {
        dns->deleteLater();
        const auto records = dns->serviceRecords();
        if (dns->error() != QDnsLookup::NoError || records.isEmpty()) {
            done({}, 0);
        } else {
            QString target = records.first().target();
            if (target.endsWith('.')) {
                target.chop(1);
            }
            done(target, records.first().port());
        }
    });
    dns->lookup();
}

void lookupIps(const QString& host, QObject* context, std::function<void(const QSet<QString>&)> done)
{
    if (isIp(host)) {
        done({ host });
        return;
    }
    QHostInfo::lookupHost(host, context, [done](const QHostInfo& info) {
        QSet<QString> ips;
        for (const auto& address : info.addresses()) {
            if (address.protocol() == QAbstractSocket::IPv4Protocol) {
                ips.insert(address.toString());
            }
        }
        done(ips);
    });
}

QString record(const QString& type, const QString& name, const QString& value)
{
    return QString("<tr><td><b>%1</b>&nbsp;&nbsp;</td><td>%2&nbsp;&nbsp;</td><td>%3</td></tr>").arg(type, name.toHtmlEscaped(), value.toHtmlEscaped());
}
}  // namespace

ServerNetworkPage::ServerNetworkPage(ServerInstance* server, QWidget* parent) : QWidget(parent), m_server(server)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll);
    auto* content = new QWidget(scroll);
    scroll->setWidget(content);
    auto* root = new QVBoxLayout(content);
    root->setContentsMargins(6, 4, 14, 4);
    root->setSpacing(8);

    root->addWidget(label(tr("Friends on the internet"), "cardTitle", content));
    root->addWidget(label(tr("Friends on your Wi-Fi can always join. Pick how friends anywhere else reach this server."), "note", content));

    m_modes = new QButtonGroup(this);
    m_off = new OptionCard(tr("Only my network"), tr("Nobody outside your home network can join."), content);
    m_upnp = new OptionCard(tr("Open a port on my router"),
                            tr("EMC asks your router to forward the port while the server runs. Fastest, no account. "
                               "Does not work on every internet connection."),
                            content);
    m_playit = new OptionCard(tr("Use a playit.gg tunnel"),
                              PlayitAgent::supported()
                                  ? tr("Works on any internet connection, including shared and mobile ones. Free; link "
                                       "EMC to playit.gg once.")
                                  : tr("playit.gg makes its tunnel program for Windows and Linux only, so it is not available here."),
                              content);
    m_playit->setEnabled(PlayitAgent::supported());
    int index = 0;
    for (auto* card : { m_off, m_upnp, m_playit }) {
        card->setCheckable(true);
        m_modes->addButton(card, index++);
        root->addWidget(card);
    }

    // playit linking, under its card
    m_playitBox = new QWidget(content);
    auto* playitLayout = new QHBoxLayout(m_playitBox);
    playitLayout->setContentsMargins(18, 0, 0, 4);
    m_playitState = label({}, "body", m_playitBox);
    m_link = new QPushButton(m_playitBox);
    playitLayout->addWidget(m_playitState, 1);
    playitLayout->addWidget(m_link);
    root->addWidget(m_playitBox);

    // connection test
    root->addSpacing(10);
    auto* checkRow = new QHBoxLayout;
    checkRow->addWidget(label(tr("Your connection"), "cardTitle", content), 1);
    m_check = new QPushButton(tr("Test Again"), content);
    checkRow->addWidget(m_check);
    root->addLayout(checkRow);
    m_checkResult = label({}, "body", content);
    m_checkResult->setTextFormat(Qt::RichText);
    root->addWidget(m_checkResult);

    // the address and an outside test
    root->addSpacing(10);
    root->addWidget(label(tr("Address for friends"), "cardTitle", content));
    auto* addressRow = new QHBoxLayout;
    m_address = new QLabel(content);
    m_address->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont mono = m_address->font();
    mono.setFamily("Consolas");
    mono.setPixelSize(14);
    m_address->setFont(mono);
    m_copy = new QPushButton(tr("Copy"), content);
    m_test = new QPushButton(tr("Test from the Internet"), content);
    addressRow->addWidget(m_address, 1);
    addressRow->addWidget(m_copy);
    addressRow->addWidget(m_test);
    root->addLayout(addressRow);
    m_testResult = label({}, "note", content);
    root->addWidget(m_testResult);
    m_testResult->hide();
    m_manual = label({}, "note", content);
    m_manual->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_manual);

    // the user's own domain
    root->addSpacing(10);
    root->addWidget(label(tr("Your own address"), "cardTitle", content));
    root->addWidget(label(tr("Give friends a name like play.example.com instead of numbers."), "note", content));
    auto* domainRow = new QHBoxLayout;
    m_domain = new QLineEdit(content);
    m_domain->setPlaceholderText(tr("play.example.com"));
    m_domainCheck = new QPushButton(tr("Check"), content);
    domainRow->addWidget(m_domain, 1);
    domainRow->addWidget(m_domainCheck);
    root->addLayout(domainRow);
    m_records = label({}, "body", content);
    m_records->setTextFormat(Qt::RichText);
    m_records->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_records);
    m_domainResult = label({}, "note", content);
    root->addWidget(m_domainResult);
    m_domainResult->hide();
    m_freeNote = label({}, "note", content);
    m_freeNote->setTextFormat(Qt::RichText);
    m_freeNote->setOpenExternalLinks(true);
    root->addWidget(m_freeNote);
    m_duckBox = new QWidget(content);
    auto* duckForm = new QFormLayout(m_duckBox);
    duckForm->setContentsMargins(0, 0, 0, 0);
    auto* duckNameRow = new QHBoxLayout;
    m_duckName = new QLineEdit(m_duckBox);
    m_duckName->setPlaceholderText(tr("myserver"));
    duckNameRow->addWidget(m_duckName, 1);
    duckNameRow->addWidget(label(".duckdns.org", "body", m_duckBox));
    m_duckToken = new QLineEdit(m_duckBox);
    m_duckToken->setEchoMode(QLineEdit::Password);
    m_duckToken->setPlaceholderText(tr("The token shown at the top of duckdns.org"));
    duckForm->addRow(tr("DuckDNS name"), duckNameRow);
    duckForm->addRow(tr("Token"), m_duckToken);
    root->addWidget(m_duckBox);

    // Windows Firewall
    m_firewallBox = new QWidget(content);
    auto* firewallLayout = new QVBoxLayout(m_firewallBox);
    firewallLayout->setContentsMargins(0, 10, 0, 0);
    firewallLayout->addWidget(label(tr("Windows Firewall"), "cardTitle", m_firewallBox));
    auto* firewallRow = new QHBoxLayout;
    m_firewallState = label({}, "body", m_firewallBox);
    m_firewallState->setTextFormat(Qt::RichText);
    m_firewallAllow = new QPushButton(tr("Allow"), m_firewallBox);
    firewallRow->addWidget(m_firewallState, 1);
    firewallRow->addWidget(m_firewallAllow);
    firewallLayout->addLayout(firewallRow);
    root->addWidget(m_firewallBox);
#ifndef Q_OS_WIN
    m_firewallBox->hide();
#endif
    root->addStretch(1);

    m_domain->setText(server->settings()->get("CustomDomain").toString());
    m_duckName->setText(server->settings()->get("DuckDnsName").toString());
    m_duckToken->setText(server->settings()->get("DuckDnsToken").toString());
    connect(m_domain, &QLineEdit::textEdited, this, [this] {
        setNote(m_domainResult, {});
        updateRecords();
    });
    connect(m_domain, &QLineEdit::returnPressed, this, &ServerNetworkPage::checkDomain);
    connect(m_domainCheck, &QPushButton::clicked, this, &ServerNetworkPage::checkDomain);
    for (auto* edit : { m_duckName, m_duckToken }) {
        connect(edit, &QLineEdit::editingFinished, this, [this] {
            if (!m_server) {
                return;
            }
            const QString name = m_duckName->text().trimmed().toLower().remove(".duckdns.org");
            const QString token = m_duckToken->text().trimmed();
            auto settings = m_server->settings();
            if (name == settings->get("DuckDnsName").toString() && token == settings->get("DuckDnsToken").toString()) {
                return;
            }
            settings->set("DuckDnsName", name);
            settings->set("DuckDnsToken", token);
            if (m_server->publicMode() == "upnp") {
                ServerHosting::modeChanged(m_server, "upnp");  // updates the name now if the server runs
            }
            emit m_server->changed();
        });
    }
    connect(m_firewallAllow, &QPushButton::clicked, this, [this] {
        if (!m_server) {
            return;
        }
        m_firewallAllow->setEnabled(false);
        QPointer<ServerNetworkPage> guard(this);
        ServerFirewall::allow(m_server->serverProgram(), [guard](bool) {
            if (guard) {
                guard->refreshFirewall();
            }
        });
    });

    connect(m_modes, &QButtonGroup::idClicked, this, [this](int id) {
        static const QStringList modes{ "off", "upnp", "playit" };
        setMode(modes.value(id));
    });
    connect(m_check, &QPushButton::clicked, this, &ServerNetworkPage::runRouterCheck);
    connect(m_copy, &QPushButton::clicked, this, [this] { QGuiApplication::clipboard()->setText(m_address->text().section("   ", 0, 0)); });
    connect(m_test, &QPushButton::clicked, this, &ServerNetworkPage::testFromInternet);
    connect(m_link, &QPushButton::clicked, this, [this] {
        auto* agent = PlayitAgent::instance();
        if (agent->isLinking()) {
            agent->cancelLink();
        } else if (agent->isLinked()) {
            agent->unlink();
        } else {
            agent->startLink();
        }
        refresh();
    });
    auto* agent = PlayitAgent::instance();
    connect(agent, &PlayitAgent::linkProgress, this, [this] { refresh(); });
    connect(agent, &PlayitAgent::linkedChanged, this, [this] {
        // Linked while this server runs in playit mode: set its tunnel up now.
        if (m_server && m_server->publicMode() == "playit") {
            ServerHosting::modeChanged(m_server, "playit");
        }
        refresh();
    });
    connect(agent, &PlayitAgent::agentStateChanged, this, &ServerNetworkPage::refresh);
    connect(server, &ServerInstance::statusChanged, this, &ServerNetworkPage::refresh);
    connect(server, &ServerInstance::changed, this, &ServerNetworkPage::refresh);
    refresh();
}

void ServerNetworkPage::openedImpl()
{
    if (!m_checked) {
        runRouterCheck();
    }
    refreshFirewall();
    refresh();
}

void ServerNetworkPage::refreshFirewall()
{
    if (!m_server) {
        return;
    }
    const QString program = m_server->serverProgram();
    m_firewallAllow->hide();
    if (program.isEmpty()) {
        m_firewallState->setText(tr("Start the server once; EMC then checks Windows Firewall for it."));
        return;
    }
    m_firewallState->setText(tr("Checking…"));
    QPointer<ServerNetworkPage> guard(this);
    ServerFirewall::check(program, [guard](ServerFirewall::State state) {
        if (!guard) {
            return;
        }
        using State = ServerFirewall::State;
        guard->m_firewallAllow->setEnabled(true);
        guard->m_firewallAllow->setVisible(state == State::Missing || state == State::Blocked);
        switch (state) {
            case State::Allowed:
                guard->m_firewallState->setText(line(true, tr("Other devices can reach this server.")));
                break;
            case State::Blocked:
                guard->m_firewallState->setText(line(false, tr("Windows Firewall blocks this server, so friends cannot join.")));
                break;
            case State::Missing:
                guard->m_firewallState->setText(tr("Windows Firewall has not been told about this server yet."));
                break;
            case State::Unknown:
                guard->m_firewallState->setText(tr("EMC could not read the Windows Firewall rules."));
                break;
        }
    });
}

void ServerNetworkPage::resolvePlayitTarget()
{
    const QString java = m_server ? m_server->settings()->get("PlayitJava").toString() : QString();
    if (java.isEmpty() || java == m_playitFor) {
        return;
    }
    m_playitFor = java;
    QPointer<ServerNetworkPage> guard(this);
    lookupSrv(java, this, [guard, java](const QString& target, int port) {
        if (!guard || guard->m_playitFor != java) {
            return;
        }
        guard->m_playitTarget = target.isEmpty() ? java : target;
        guard->m_playitPort = target.isEmpty() ? 25565 : port;
        guard->updateRecords();
    });
}

ServerNetworkPage::Endpoints ServerNetworkPage::endpoints()
{
    Endpoints e;
    if (!m_server) {
        return e;
    }
    const QString mode = m_server->publicMode();
    if (mode == "upnp") {
        QString host = m_server->duckDnsHost();
        if (host.isEmpty()) {
            host = m_server->settings()->get("PublicAddress").toString();
        }
        if (host.isEmpty()) {
            host = m_report.publicAddress;
        }
        e.javaHost = e.bedrockHost = host;
        e.javaPort = m_server->port();
        e.bedrockPort = m_server->isBedrock() ? m_server->port() : m_server->bedrockPort();
    } else if (mode == "playit") {
        resolvePlayitTarget();
        e.javaHost = m_playitTarget;
        e.javaPort = m_playitPort;
        const QString bedrock = m_server->settings()->get("PlayitBedrock").toString();
        e.bedrockHost = bedrock.section(':', 0, 0);
        e.bedrockPort = bedrock.section(':', -1).toInt();
    }
    return e;
}

void ServerNetworkPage::updateRecords()
{
    if (!m_server) {
        return;
    }
    const QString mode = m_server->publicMode();
    const bool on = mode != "off";
    m_domain->setVisible(on);
    m_domainCheck->setVisible(on);
    m_records->setVisible(on);
    m_duckBox->setVisible(mode == "upnp");
    if (!on) {
        m_freeNote->setText(tr("Choose how friends on the internet join first."));
        return;
    }
    m_freeNote->setText(mode == "upnp"
                            ? tr("No domain? Make a free one at <a href=\"https://www.duckdns.org\">duckdns.org</a>: sign in, add a name, "
                                 "then enter it and your token below. EMC keeps it pointing at your internet address.")
                            : tr("No domain? The playit.gg address above is free and stays the same."));

    const QString domain = m_domain->text().trimmed().toLower();
    if (domain.isEmpty()) {
        m_records->setText(tr("Bought a domain? Type it above and EMC shows the records to add in your domain's DNS "
                              "settings (Cloudflare, Namecheap, Porkbun and others all have them)."));
        return;
    }

    const Endpoints e = endpoints();
    const bool java = !m_server->isBedrock();
    const bool bedrock = m_server->isBedrock() || m_server->crossplay();
    if ((java && e.javaHost.isEmpty()) || (bedrock && e.bedrockHost.isEmpty())) {
        m_records->setText(tr("Start the server once so EMC knows its internet address, then the records appear here."));
        return;
    }

    QStringList rows;
    bool nameUsed = false;  // the domain itself holds one A or CNAME record
    if (java) {
        if (isIp(e.javaHost)) {
            rows << record("A", domain, e.javaHost);
            nameUsed = true;
            if (e.javaPort != 25565) {
                rows << record("SRV", "_minecraft._tcp." + domain, QString("0 5 %1 %2").arg(e.javaPort).arg(domain));
            }
        } else if (e.javaPort == 25565) {
            rows << record("CNAME", domain, e.javaHost);
            nameUsed = true;
        } else {
            rows << record("SRV", "_minecraft._tcp." + domain, QString("0 5 %1 %2").arg(e.javaPort).arg(e.javaHost));
        }
    }
    if (bedrock && !(nameUsed && e.bedrockHost == e.javaHost)) {
        rows << record(isIp(e.bedrockHost) ? "A" : "CNAME", domain, e.bedrockHost);
    }
    QString text = tr("Add these records in your domain's DNS settings:") +
                   "<table cellspacing=\"0\" cellpadding=\"3\" style=\"font-family: Consolas;\">" + rows.join("") + "</table>";
    if (bedrock) {
        text += tr("Bedrock players type the port %1 next to the name.").arg(e.bedrockPort);
    }
    if (mode == "upnp" && m_server->duckDnsHost().isEmpty()) {
        text += "<br>" + tr("Your internet address can change. Set up a free DuckDNS name below so the domain keeps working.");
    }
    m_records->setText(text);
}

void ServerNetworkPage::checkDomain()
{
    if (!m_server) {
        return;
    }
    const QString domain = m_domain->text().trimmed().toLower();
    m_server->settings()->set("CustomDomain", domain);
    m_server->settings()->set("CustomDomainOk", false);
    emit m_server->changed();
    if (domain.isEmpty()) {
        setNote(m_domainResult, {});
        return;
    }
    const Endpoints e = endpoints();
    const bool java = !m_server->isBedrock();
    if ((java && e.javaHost.isEmpty()) || (!java && e.bedrockHost.isEmpty())) {
        setNote(m_domainResult, tr("Start the server first so EMC knows where the domain should point."));
        return;
    }

    m_domainCheck->setEnabled(false);
    setNote(m_domainResult, tr("Looking up %1…").arg(domain));
    QPointer<ServerNetworkPage> guard(this);
    const auto finish = [guard, domain](bool ok, const QString& message) {
        if (!guard || !guard->m_server) {
            return;
        }
        guard->m_domainCheck->setEnabled(true);
        setNote(guard->m_domainResult, message);
        if (guard->m_domain->text().trimmed().toLower() == domain) {
            guard->m_server->settings()->set("CustomDomainOk", ok);
            emit guard->m_server->changed();
        }
    };
    // Both sides are compared by IP address, so a CNAME or a DuckDNS name in between is fine.
    const auto compare = [this, finish, domain](const QString& actualHost, const QString& expectedHost) {
        lookupIps(actualHost, this, [this, finish, domain, expectedHost](const QSet<QString>& actual) {
            lookupIps(expectedHost, this, [finish, domain, actual](const QSet<QString>& expected) {
                if (actual.isEmpty()) {
                    finish(false, tr("%1 does not exist yet. New records can take a few minutes, sometimes hours, to appear.").arg(domain));
                } else if (!actual.intersects(expected)) {
                    finish(false, tr("%1 points somewhere else. Check the records above.").arg(domain));
                } else {
                    finish(true, tr("Works: friends can join at %1.").arg(domain));
                }
            });
        });
    };
    if (!java) {
        compare(domain, e.bedrockHost);
        return;
    }
    lookupSrv(domain, this, [finish, compare, domain, e](const QString& target, int port) {
        if (target.isEmpty() && e.javaPort != 25565) {
            finish(false, tr("%1 has no SRV record yet, so Java players would reach the wrong port.").arg(domain));
            return;
        }
        if (!target.isEmpty() && port != e.javaPort) {
            finish(false, tr("The SRV record of %1 uses port %2; it should be %3.").arg(domain).arg(port).arg(e.javaPort));
            return;
        }
        compare(target.isEmpty() ? domain : target, e.javaHost);
    });
}

void ServerNetworkPage::setMode(const QString& mode)
{
    if (!m_server) {
        return;
    }
    const QString old = m_server->publicMode();
    if (old == mode) {
        return;
    }
    m_server->settings()->set("PublicMode", mode);
    setNote(m_testResult, {});
    ServerHosting::modeChanged(m_server, old);
    if (mode == "playit" && !PlayitAgent::instance()->isLinked() && !PlayitAgent::instance()->isLinking()) {
        PlayitAgent::instance()->startLink();  // the one thing left to do; start it for them
    }
    emit m_server->changed();
    refresh();
}

void ServerNetworkPage::runRouterCheck()
{
    if (m_checking) {
        return;
    }
    m_checking = true;
    m_check->setEnabled(false);
    m_checkResult->setText(tr("Testing your connection…"));
    QPointer<ServerNetworkPage> guard(this);
    RouterCheck::run([guard](const RouterCheck::Report& report) {
        if (!guard) {
            return;
        }
        guard->m_report = report;
        guard->m_checked = true;
        guard->m_checking = false;
        guard->m_check->setEnabled(true);
        guard->showReport();
        guard->refresh();
    });
}

void ServerNetworkPage::showReport()
{
    const auto& r = m_report;
    QStringList lines;
    lines << line(!r.lanAddress.isEmpty(), r.lanAddress.isEmpty() ? tr("Not connected to a network")
                                                                   : tr("Connected to your network as %1").arg(r.lanAddress));
    lines << line(r.upnp, r.upnp ? tr("Your router accepts automatic port forwarding%1")
                                       .arg(r.routerName.isEmpty() ? QString() : QString(" (%1)").arg(r.routerName))
                                 : tr("Your router does not accept automatic port forwarding (UPnP is off or missing)"));
    if (r.upnp && !r.routerAddress.isEmpty()) {
        lines << line(!r.sharedAddress, r.sharedAddress ? (r.carrierNat ? tr("Your internet provider shares your internet address with "
                                                                            "other homes (CGNAT), so ports cannot be opened")
                                                                        : tr("There is a second router in front of yours, so ports "
                                                                             "opened here do not reach the internet"))
                                                        : tr("Your internet address %1 can receive connections").arg(r.publicAddress));
    }

    QString advice;
    const bool upnpWorks = r.upnp && !r.sharedAddress && !r.routerAddress.isEmpty();
    if (upnpWorks) {
        advice = tr("Recommended: open a port on your router.");
    } else {
        advice = tr("Recommended: use a playit.gg tunnel.");
    }
    m_upnp->setBadge(upnpWorks ? tr("Recommended") : QString());
    m_playit->setBadge(upnpWorks ? QString() : tr("Recommended"));
    m_checkResult->setText(lines.join("<br>") + "<br><br><b>" + advice.toHtmlEscaped() + "</b>");
}

void ServerNetworkPage::testFromInternet()
{
    if (!m_server) {
        return;
    }
    const QStringList addresses = m_server->publicAddresses();
    if (addresses.isEmpty()) {
        return;
    }
    // Test the address Java players use (or the Bedrock one on Bedrock servers).
    QString host = addresses.first();
    int port = 0;
    if (const int colon = host.lastIndexOf(':'); colon > 0) {
        port = host.mid(colon + 1).toInt();
        host = host.left(colon);
    }
    m_testing = true;
    m_test->setEnabled(false);
    setNote(m_testResult, tr("Asking a server on the internet to connect…"));
    QPointer<ServerNetworkPage> guard(this);
    RouterCheck::testReachable(host, port, m_server->isBedrock(), [guard](RouterCheck::Reachable result, const QString& detail) {
        if (!guard) {
            return;
        }
        guard->m_testing = false;
        guard->m_test->setEnabled(true);
        switch (result) {
            case RouterCheck::Reachable::Yes:
                setNote(guard->m_testResult, tr("Works: friends can join from anywhere."));
                break;
            case RouterCheck::Reachable::No:
                setNote(guard->m_testResult, 
                    guard->m_server && guard->m_server->publicMode() == "upnp"
#ifdef Q_OS_WIN
                        ? tr("Not reachable yet. Windows Firewall may be blocking Java: allow it when Windows asks, or try playit.gg.")
#elif defined(Q_OS_MACOS)
                        ? tr("Not reachable yet. If macOS asked whether Java may accept connections, choose Allow.")
#else
                        ? tr("Not reachable yet. A firewall on this computer may be blocking the port, or try playit.gg.")
#endif
                        : tr("Not reachable yet. The tunnel can take a minute to connect; test again shortly."));
                break;
            case RouterCheck::Reachable::Unknown:
                setNote(guard->m_testResult, detail);
                break;
        }
    });
}

void ServerNetworkPage::refresh()
{
    if (!m_server) {
        return;
    }
    const QString mode = m_server->publicMode();
    {
        QSignalBlocker block(m_modes);
        (mode == "upnp" ? m_upnp : mode == "playit" ? m_playit : m_off)->setChecked(true);
    }

    auto* agent = PlayitAgent::instance();
    m_playitBox->setVisible(mode == "playit");
    if (agent->isLinking()) {
        m_playitState->setText(tr("Waiting for you to approve EMC on playit.gg. If the page did not open, go to %1").arg(agent->claimUrl()));
        m_link->setText(tr("Cancel"));
    } else if (agent->isLinked()) {
        const QString state = agent->agentStatus();
        m_playitState->setText(state.isEmpty() ? tr("Linked to playit.gg. The tunnel runs while the server runs.") : state);
        m_link->setText(tr("Unlink"));
    } else {
        m_playitState->setText(tr("Not linked yet. Linking takes one click on playit.gg; an account is optional."));
        m_link->setText(tr("Link playit.gg"));
    }

    const QStringList addresses = m_server->publicAddresses();
    const bool running = m_server->status() == ServerInstance::Status::Running;
    if (mode == "off") {
        m_address->setText(tr("Not public"));
    } else if (addresses.isEmpty()) {
        m_address->setText(m_server->isActive() ? tr("Getting the address…") : tr("Start the server to get the address"));
    } else {
        m_address->setText(addresses.join("   "));
    }
    m_copy->setEnabled(mode != "off" && !addresses.isEmpty());
    m_test->setEnabled(!m_testing && mode != "off" && !addresses.isEmpty() && running);
    m_test->setToolTip(running ? QString() : tr("Start the server first"));

    // When automatic forwarding is not possible, say exactly what to set up by hand.
    const QString lan = ServerNetwork::lanAddress();
    if (mode == "upnp" && m_checked && !m_report.upnp && !lan.isEmpty()) {
        const QString protocol = m_server->isBedrock() ? "UDP" : "TCP";
        m_manual->setText(tr("To forward the port by hand: open your router's settings page (often http://192.168.1.1 or "
                             "http://192.168.0.1), find Port Forwarding, and add %1 port %2 to %3. Or turn on UPnP there.")
                              .arg(protocol)
                              .arg(m_server->port())
                              .arg(lan));
        m_manual->show();
    } else {
        m_manual->hide();
    }
    updateRecords();
}

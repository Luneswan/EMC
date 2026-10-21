// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "ServerHosting.h"

#include <QCoreApplication>
#include <QNetworkReply>
#include <QPointer>
#include <QUrlQuery>

#include "Application.h"

#include "PlayitAgent.h"
#include "PortMapper.h"
#include "RouterCheck.h"

namespace {
struct Forward {
    int port;
    QString protocol;
};

QList<Forward> forwards(const ServerInstance* server)
{
    if (server->isBedrock()) {
        return { { server->port(), "UDP" } };
    }
    QList<Forward> list{ { server->port(), "TCP" } };
    if (server->crossplay()) {
        list.append({ server->bedrockPort(), "UDP" });
    }
    return list;
}

// Points the server's free name.duckdns.org at the router's internet address.
void updateDuckDns(ServerInstance* server, const QString& ip)
{
    const QString name = server->settings()->get("DuckDnsName").toString();
    const QString token = server->settings()->get("DuckDnsToken").toString();
    if (name.isEmpty() || token.isEmpty()) {
        return;
    }
    QUrlQuery query;
    query.addQueryItem("domains", name);
    query.addQueryItem("token", token);
    query.addQueryItem("ip", ip);
    QUrl url("https://www.duckdns.org/update");
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setTransferTimeout(10000);
    auto* reply = APPLICATION->network()->get(request);
    QPointer<ServerInstance> guard(server);
    QObject::connect(reply, &QNetworkReply::finished, qApp, [reply, guard, name] {
        reply->deleteLater();
        if (!guard) {
            return;
        }
        if (reply->error() == QNetworkReply::NoError && reply->readAll().trimmed() == "OK") {
            guard->logLine(QObject::tr("DuckDNS: %1.duckdns.org points to this server.").arg(name));
        } else {
            guard->logLine(QObject::tr("DuckDNS: could not update %1.duckdns.org. Check the name and token in the Network page.").arg(name),
                           MessageLevel::Warning);
        }
    });
}

void open(ServerInstance* server, const QString& mode)
{
    QPointer<ServerInstance> guard(server);
    if (mode == "upnp") {
        auto* mapper = PortMapper::instance();
        for (const auto& forward : forwards(server)) {
            mapper->addMapping(forward.port, forward.protocol, QString("EMC %1").arg(server->name()),
                               [guard, forward](const QString& error) {
                                   if (!guard) {
                                       return;
                                   }
                                   if (error.isEmpty()) {
                                       guard->logLine(QObject::tr("Router: port %1 (%2) is forwarded to this computer.")
                                                          .arg(forward.port)
                                                          .arg(forward.protocol));
                                   } else {
                                       guard->logLine(QObject::tr("Router: could not forward port %1: %2").arg(forward.port).arg(error),
                                                      MessageLevel::Warning);
                                   }
                               });
        }
        mapper->externalAddress([guard](const QString& ip, const QString&) {
            if (!guard || ip.isEmpty()) {
                return;
            }
            guard->settings()->set("PublicAddress", ip);
            if (RouterCheck::isPrivate(ip)) {
                guard->logLine(PlayitAgent::supported()
                                   ? QObject::tr("Your internet provider shares one public address between many homes, so port "
                                                 "forwarding cannot work here. Use playit.gg in the Network page instead.")
                                   : QObject::tr("Your internet provider shares one public address between many homes, so port "
                                                 "forwarding cannot work here. Friends on your Wi-Fi can still join."),
                               MessageLevel::Warning);
            } else {
                updateDuckDns(guard, ip);
                guard->logLine(QObject::tr("Friends can join at %1").arg(guard->publicAddresses().join(QObject::tr(" or "))));
            }
            emit guard->changed();
        });
    } else if (mode == "playit") {
        // A server set up on another computer may ask for playit where it has no agent (macOS).
        if (!PlayitAgent::supported()) {
            server->logLine(QObject::tr("playit.gg has no version for this computer, so only friends on your Wi-Fi can join."),
                            MessageLevel::Warning);
            return;
        }
        auto* agent = PlayitAgent::instance();
        agent->setServerActive(server, true);
        agent->prepare(server, [guard](const QString& error) {
            if (!guard) {
                return;
            }
            if (error.isEmpty()) {
                guard->logLine(QObject::tr("playit.gg: friends can join at %1").arg(guard->publicAddresses().join(QObject::tr(" or "))));
                emit guard->changed();
            } else {
                guard->logLine(QObject::tr("playit.gg: %1").arg(error), MessageLevel::Warning);
            }
        });
    }
}

void close(ServerInstance* server, const QString& mode)
{
    if (mode == "upnp") {
        for (const auto& forward : forwards(server)) {
            PortMapper::instance()->removeMapping(forward.port, forward.protocol, [](const QString&) {});
        }
    } else if (mode == "playit" && PlayitAgent::supported()) {
        PlayitAgent::instance()->setServerActive(server, false);
    }
}
}  // namespace

void ServerHosting::statusChanged(ServerInstance* server, ServerInstance::Status previous, ServerInstance::Status now)
{
    using Status = ServerInstance::Status;
    const bool wasUp = previous == Status::Starting || previous == Status::Running || previous == Status::Stopping;
    if (now == Status::Starting && !wasUp) {
        open(server, server->publicMode());
    } else if ((now == Status::Stopped || now == Status::Crashed) && wasUp) {
        close(server, server->publicMode());
    }
}

void ServerHosting::modeChanged(ServerInstance* server, const QString& oldMode)
{
    if (!server->isActive()) {
        return;
    }
    close(server, oldMode);
    open(server, server->publicMode());
}

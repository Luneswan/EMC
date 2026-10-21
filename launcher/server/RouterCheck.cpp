// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "RouterCheck.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>

#include <memory>

#include "Application.h"
#include "PortMapper.h"
#include "ServerNetwork.h"

namespace {
QNetworkReply* get(const QUrl& url)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, APPLICATION->getUserAgent());
    request.setTransferTimeout(10000);
    return APPLICATION->network()->get(request);
}
}  // namespace

bool RouterCheck::isPrivate(const QString& address)
{
    const QHostAddress ip(address);
    if (ip.isNull()) {
        return false;
    }
    return ip.isPrivateUse() || ip.isLinkLocal() || ip.isLoopback() || ip.isInSubnet(QHostAddress("100.64.0.0"), 10);
}

void RouterCheck::run(std::function<void(const Report&)> done)
{
    auto report = std::make_shared<Report>();
    report->lanAddress = ServerNetwork::lanAddress();
    // Two independent questions; answer when both are back.
    auto pending = std::make_shared<int>(2);
    auto finishOne = [report, pending, done] {
        if (--*pending > 0) {
            return;
        }
        if (!report->routerAddress.isEmpty()) {
            report->carrierNat = QHostAddress(report->routerAddress).isInSubnet(QHostAddress("100.64.0.0"), 10);
            report->sharedAddress = isPrivate(report->routerAddress) ||
                                    (!report->publicAddress.isEmpty() && report->publicAddress != report->routerAddress);
        }
        done(*report);
    };

    auto* reply = get(QUrl("https://api.ipify.org?format=json"));
    QObject::connect(reply, &QNetworkReply::finished, qApp, [reply, report, finishOne] {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            report->publicAddress = QJsonDocument::fromJson(reply->readAll()).object().value("ip").toString();
        }
        finishOne();
    });

    auto* mapper = PortMapper::instance();
    mapper->discover([mapper, report, finishOne](bool found) {
        report->upnp = found;
        report->routerName = mapper->routerName();
        if (!found) {
            finishOne();
            return;
        }
        mapper->externalAddress([report, finishOne](const QString& ip, const QString&) {
            report->routerAddress = ip;
            finishOne();
        });
    });
}

void RouterCheck::testReachable(const QString& host, int port, bool bedrock, std::function<void(Reachable, QString)> done)
{
    // mcsrvstat.us connects from the internet like a player would and reports what it saw.
    const QString address = port > 0 ? QString("%1:%2").arg(host).arg(port) : host;
    const QUrl url(QString("https://api.mcsrvstat.us/%1%2").arg(bedrock ? "bedrock/3/" : "3/", address));
    auto* reply = get(url);
    QObject::connect(reply, &QNetworkReply::finished, qApp, [reply, done] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            done(Reachable::Unknown, QObject::tr("Could not run the test: %1").arg(reply->errorString()));
            return;
        }
        const auto object = QJsonDocument::fromJson(reply->readAll()).object();
        if (object.value("online").toBool()) {
            const QString motd = object.value("motd").toObject().value("clean").toVariant().toStringList().join(' ');
            done(Reachable::Yes, motd);
        } else {
            done(Reachable::No, {});
        }
    });
}

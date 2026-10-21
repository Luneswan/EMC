// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "PortMapper.h"

#include <QCoreApplication>
#include <QNetworkDatagram>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QTimer>
#include <QUdpSocket>
#include <QXmlStreamReader>

#include "ServerNetwork.h"

namespace {
const QStringList kSearchTargets{ "urn:schemas-upnp-org:device:InternetGatewayDevice:1",
                                  "urn:schemas-upnp-org:device:InternetGatewayDevice:2",
                                  "urn:schemas-upnp-org:service:WANIPConnection:1",
                                  "urn:schemas-upnp-org:service:WANPPPConnection:1" };

// Readable text for the UPnP error codes routers actually send.
QString upnpError(const QString& code, const QString& description)
{
    if (code == "718") {
        return QObject::tr("Another device on your network already uses this port.");
    }
    if (code == "714") {
        return QObject::tr("The router has no such port forward.");
    }
    if (code == "606" || code == "401") {
        return QObject::tr("The router refused the request (UPnP changes are not allowed).");
    }
    return description.isEmpty() ? QObject::tr("The router answered with error %1.").arg(code) : description;
}
}  // namespace

PortMapper* PortMapper::instance()
{
    static auto* mapper = new PortMapper(qApp);
    return mapper;
}

PortMapper::PortMapper(QObject* parent) : QObject(parent)
{
    // The router is on the home network: a proxy set for the internet must not be used.
    m_network.setProxy(QNetworkProxy::NoProxy);
    m_network.setTransferTimeout(5000);
}

void PortMapper::discover(std::function<void(bool)> done)
{
    if (hasRouter()) {
        done(true);
        return;
    }
    m_waiting << std::move(done);
    if (m_waiting.size() > 1) {
        return;  // a search is already running; it answers everyone
    }
    auto finish = [this](bool found) {
        const auto waiting = std::exchange(m_waiting, {});
        for (const auto& callback : waiting) {
            callback(found);
        }
    };

    // SSDP: ask every UPnP device on the network to announce itself. Bind to the home-network
    // address so the question leaves through Wi-Fi or Ethernet, not a VPN adapter.
    auto* socket = new QUdpSocket(this);
    const QString lan = ServerNetwork::lanAddress();
    if (!socket->bind(lan.isEmpty() ? QHostAddress(QHostAddress::AnyIPv4) : QHostAddress(lan), 0)) {
        socket->deleteLater();
        finish(false);
        return;
    }
    for (const auto& target : kSearchTargets) {
        const QByteArray request = QString("M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\n"
                                           "MX: 2\r\nST: %1\r\n\r\n")
                                       .arg(target)
                                       .toUtf8();
        socket->writeDatagram(request, QHostAddress("239.255.255.250"), 1900);
    }
    auto* timeout = new QTimer(socket);
    timeout->setSingleShot(true);
    auto handled = std::make_shared<bool>(false);
    connect(timeout, &QTimer::timeout, this, [socket, finish, handled] {
        if (!*handled) {
            *handled = true;
            finish(false);
        }
        socket->deleteLater();
    });
    connect(socket, &QUdpSocket::readyRead, this, [this, socket, finish, handled] {
        while (socket->hasPendingDatagrams()) {
            const QString reply = QString::fromUtf8(socket->receiveDatagram().data());
            if (*handled) {
                continue;
            }
            for (const auto& line : reply.split("\r\n")) {
                if (line.startsWith("location:", Qt::CaseInsensitive)) {
                    *handled = true;
                    fetchDescription(QUrl(line.mid(9).trimmed()), finish);
                    break;
                }
            }
        }
    });
    timeout->start(3000);
}

void PortMapper::fetchDescription(const QUrl& location, std::function<void(bool)> done)
{
    auto* reply = m_network.get(QNetworkRequest(location));
    connect(reply, &QNetworkReply::finished, this, [this, reply, location, done] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            done(false);
            return;
        }
        // Walk the device description for the WAN connection service and its control URL.
        QXmlStreamReader xml(reply->readAll());
        QUrl base = location;
        QString serviceType, controlUrl, friendlyName;
        QString foundService, foundControl;
        while (!xml.atEnd()) {
            xml.readNext();
            if (!xml.isStartElement()) {
                if (xml.isEndElement() && xml.name() == QLatin1String("service")) {
                    if (foundService.isEmpty() &&
                        (serviceType.contains("WANIPConnection") || serviceType.contains("WANPPPConnection"))) {
                        foundService = serviceType;
                        foundControl = controlUrl;
                    }
                    serviceType.clear();
                    controlUrl.clear();
                }
                continue;
            }
            const auto name = xml.name();
            if (name == QLatin1String("URLBase")) {
                base = QUrl(xml.readElementText().trimmed());
            } else if (name == QLatin1String("serviceType")) {
                serviceType = xml.readElementText().trimmed();
            } else if (name == QLatin1String("controlURL")) {
                controlUrl = xml.readElementText().trimmed();
            } else if (name == QLatin1String("friendlyName") && friendlyName.isEmpty()) {
                friendlyName = xml.readElementText().trimmed();
            }
        }
        if (foundService.isEmpty()) {
            done(false);
            return;
        }
        m_service = foundService;
        m_control = base.resolved(QUrl(foundControl));
        m_routerName = friendlyName;
        done(true);
    });
}

void PortMapper::soap(const QString& action,
                      const QList<QPair<QString, QString>>& args,
                      std::function<void(QMap<QString, QString>, QString)> done)
{
    discover([this, action, args, done](bool found) {
        if (!found) {
            done({}, tr("No router with UPnP was found on this network."));
            return;
        }
        QString body;
        for (const auto& [key, value] : args) {
            body += QString("<%1>%2</%1>").arg(key, value.toHtmlEscaped());
        }
        const QByteArray envelope = QString("<?xml version=\"1.0\"?>"
                                            "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                                            "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>"
                                            "<u:%1 xmlns:u=\"%2\">%3</u:%1></s:Body></s:Envelope>")
                                        .arg(action, m_service, body)
                                        .toUtf8();
        QNetworkRequest request(m_control);
        request.setHeader(QNetworkRequest::ContentTypeHeader, "text/xml; charset=\"utf-8\"");
        request.setRawHeader("SOAPAction", QString("\"%1#%2\"").arg(m_service, action).toUtf8());
        auto* reply = m_network.post(request, envelope);
        connect(reply, &QNetworkReply::finished, this, [reply, done] {
            reply->deleteLater();
            // A SOAP fault arrives as HTTP 500 with a body, so read the body either way.
            QMap<QString, QString> values;
            QXmlStreamReader xml(reply->readAll());
            QString current;
            while (!xml.atEnd()) {
                const auto token = xml.readNext();
                if (token == QXmlStreamReader::StartElement) {
                    current = xml.name().toString();
                } else if (token == QXmlStreamReader::Characters && !xml.isWhitespace()) {
                    values[current] += xml.text().toString().trimmed();
                }
            }
            if (values.contains("errorCode")) {
                done(values, upnpError(values.value("errorCode"), values.value("errorDescription")));
            } else if (reply->error() != QNetworkReply::NoError) {
                done(values, tr("The router did not answer: %1").arg(reply->errorString()));
            } else {
                done(values, {});
            }
        });
    });
}

void PortMapper::externalAddress(std::function<void(QString, QString)> done)
{
    soap("GetExternalIPAddress", {}, [done](const QMap<QString, QString>& values, const QString& error) {
        done(values.value("NewExternalIPAddress"), error);
    });
}

void PortMapper::addMapping(int port, const QString& protocol, const QString& description, std::function<void(QString)> done)
{
    const QString lan = ServerNetwork::lanAddress();
    auto request = [this, port, protocol, description, lan](const QString& lease, std::function<void(QString)> next) {
        soap("AddPortMapping",
             { { "NewRemoteHost", "" },
               { "NewExternalPort", QString::number(port) },
               { "NewProtocol", protocol },
               { "NewInternalPort", QString::number(port) },
               { "NewInternalClient", lan },
               { "NewEnabled", "1" },
               { "NewPortMappingDescription", description },
               { "NewLeaseDuration", lease } },
             [next](const QMap<QString, QString>&, const QString& error) { next(error); });
    };
    // A permanent forward first; some routers only accept a limited lease (error 725), so retry
    // with one week. EMC renews it every time the server starts.
    request("0", [request, done](const QString& error) {
        if (error.isEmpty()) {
            done({});
            return;
        }
        request("604800", [done, error](const QString& second) { done(second.isEmpty() ? QString() : error); });
    });
}

void PortMapper::removeMapping(int port, const QString& protocol, std::function<void(QString)> done)
{
    soap("DeletePortMapping", { { "NewRemoteHost", "" }, { "NewExternalPort", QString::number(port) }, { "NewProtocol", protocol } },
         [done](const QMap<QString, QString>&, const QString& error) { done(error); });
}

void PortMapper::checkMapping(int port, const QString& protocol, std::function<void(bool, QString)> done)
{
    soap("GetSpecificPortMappingEntry",
         { { "NewRemoteHost", "" }, { "NewExternalPort", QString::number(port) }, { "NewProtocol", protocol } },
         [done](const QMap<QString, QString>& values, const QString& error) {
             if (!error.isEmpty()) {
                 done(false, error);
                 return;
             }
             done(values.value("NewInternalClient") == ServerNetwork::lanAddress(), {});
         });
}

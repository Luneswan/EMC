// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QMap>
#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>

#include <functional>

// A small UPnP Internet Gateway Device client: finds the home router on the network and asks it
// to forward a port to this computer (what "port forwarding" does, without opening the router's
// settings page). Every call is asynchronous; callbacks run on the main thread.
class PortMapper : public QObject {
    Q_OBJECT
   public:
    static PortMapper* instance();

    // Looks for the router (cached after the first success). `done(found)`.
    void discover(std::function<void(bool)> done);
    bool hasRouter() const { return m_control.isValid(); }
    QString routerName() const { return m_routerName; }

    // The router's own internet address. `done(ip, error)`.
    void externalAddress(std::function<void(QString, QString)> done);
    // Forwards `port` (TCP or UDP) on the router to the same port on this computer. `done(error)`.
    void addMapping(int port, const QString& protocol, const QString& description, std::function<void(QString)> done);
    void removeMapping(int port, const QString& protocol, std::function<void(QString)> done);
    // Whether the router forwards `port` to this computer. `done(mappedHere, error)`.
    void checkMapping(int port, const QString& protocol, std::function<void(bool, QString)> done);

   private:
    explicit PortMapper(QObject* parent = nullptr);
    void fetchDescription(const QUrl& location, std::function<void(bool)> done);
    void soap(const QString& action,
              const QList<QPair<QString, QString>>& args,
              std::function<void(QMap<QString, QString>, QString)> done);

    QNetworkAccessManager m_network;
    QUrl m_control;
    QString m_service;
    QString m_routerName;
    QList<std::function<void(bool)>> m_waiting;  // discover() calls made while one is running
};

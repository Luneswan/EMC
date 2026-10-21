// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QString>
#include <QStringList>

// A Java-style .properties file (server.properties) that keeps its comments, blank lines and key
// order when values are changed, so editing it in EMC never reshuffles a hand-edited file.
class ServerProperties {
   public:
    ServerProperties() = default;

    bool load(const QString& path);
    bool save(const QString& path) const;
    void parse(const QString& text);
    QString toText() const;

    bool contains(const QString& key) const;
    QString value(const QString& key, const QString& fallback = {}) const;
    int intValue(const QString& key, int fallback) const;
    bool boolValue(const QString& key, bool fallback) const;
    void setValue(const QString& key, const QString& value);
    void setValue(const QString& key, int value) { setValue(key, QString::number(value)); }
    void setValue(const QString& key, bool value) { setValue(key, QString(value ? "true" : "false")); }
    QStringList keys() const;

    // Java servers read the file as ISO-8859-1, so non-ASCII is written as \uXXXX; Bedrock reads UTF-8.
    void setUnicodeEscapes(bool on) { m_unicodeEscapes = on; }

   private:
    struct Line {
        QString raw;  // used for comments and blank lines
        QString key;  // empty for comments and blank lines
        QString value;
    };
    int indexOf(const QString& key) const;
    static QString unescape(const QString& s);
    QString escape(const QString& s) const;

    QList<Line> m_lines;
    bool m_unicodeEscapes = true;
};

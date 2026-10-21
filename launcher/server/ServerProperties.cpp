// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "ServerProperties.h"

#include <QFile>
#include <QSaveFile>

bool ServerProperties::load(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        m_lines.clear();
        return false;
    }
    parse(QString::fromUtf8(f.readAll()));
    return true;
}

bool ServerProperties::save(const QString& path) const
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        return false;
    }
    f.write(toText().toUtf8());
    return f.commit();
}

void ServerProperties::parse(const QString& text)
{
    m_lines.clear();
    QString normalized = text;
    normalized.replace("\r\n", "\n");
    const auto rows = normalized.split('\n');
    for (int i = 0; i < rows.size(); ++i) {
        const QString& row = rows[i];
        if (i == rows.size() - 1 && row.isEmpty()) {
            break;  // trailing newline
        }
        const QString trimmed = row.trimmed();
        Line line;
        line.raw = row;
        if (trimmed.isEmpty() || trimmed.startsWith('#') || trimmed.startsWith('!')) {
            m_lines.append(line);
            continue;
        }
        // The separator is the first unescaped '=' or ':'.
        int sep = -1;
        for (int c = 0; c < row.size(); ++c) {
            if (row[c] == '\\') {
                ++c;
                continue;
            }
            if (row[c] == '=' || row[c] == ':') {
                sep = c;
                break;
            }
        }
        if (sep < 0) {
            line.key = unescape(trimmed);
        } else {
            line.key = unescape(row.left(sep).trimmed());
            line.value = unescape(row.mid(sep + 1).trimmed());
        }
        m_lines.append(line);
    }
}

QString ServerProperties::toText() const
{
    QString out;
    for (const auto& line : m_lines) {
        if (line.key.isEmpty()) {
            out += line.raw;
        } else {
            out += escape(line.key) + '=' + escape(line.value);
        }
        out += '\n';
    }
    return out;
}

int ServerProperties::indexOf(const QString& key) const
{
    for (int i = 0; i < m_lines.size(); ++i) {
        if (m_lines[i].key == key) {
            return i;
        }
    }
    return -1;
}

bool ServerProperties::contains(const QString& key) const
{
    return indexOf(key) >= 0;
}

QString ServerProperties::value(const QString& key, const QString& fallback) const
{
    const int i = indexOf(key);
    return i < 0 ? fallback : m_lines[i].value;
}

int ServerProperties::intValue(const QString& key, int fallback) const
{
    bool ok = false;
    const int v = value(key).toInt(&ok);
    return ok ? v : fallback;
}

bool ServerProperties::boolValue(const QString& key, bool fallback) const
{
    const QString v = value(key).trimmed().toLower();
    if (v == "true") {
        return true;
    }
    if (v == "false") {
        return false;
    }
    return fallback;
}

void ServerProperties::setValue(const QString& key, const QString& value)
{
    const int i = indexOf(key);
    if (i >= 0) {
        m_lines[i].value = value;
        return;
    }
    Line line;
    line.key = key;
    line.value = value;
    m_lines.append(line);
}

QStringList ServerProperties::keys() const
{
    QStringList out;
    for (const auto& line : m_lines) {
        if (!line.key.isEmpty()) {
            out.append(line.key);
        }
    }
    return out;
}

QString ServerProperties::unescape(const QString& s)
{
    QString out;
    out.reserve(s.size());
    for (int i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 >= s.size()) {
            out += s[i];
            continue;
        }
        const QChar n = s[++i];
        if (n == 'n') {
            out += '\n';
        } else if (n == 't') {
            out += '\t';
        } else if (n == 'u' && i + 4 < s.size()) {
            bool ok = false;
            const ushort code = s.mid(i + 1, 4).toUShort(&ok, 16);
            if (ok) {
                out += QChar(code);
                i += 4;
            } else {
                out += n;
            }
        } else {
            out += n;
        }
    }
    return out;
}

QString ServerProperties::escape(const QString& s) const
{
    QString out;
    out.reserve(s.size());
    for (const QChar c : s) {
        if (c == '\\') {
            out += "\\\\";
        } else if (c == '\n') {
            out += "\\n";
        } else if (m_unicodeEscapes && c.unicode() > 0x7e) {
            out += QString("\\u%1").arg(uint(c.unicode()), 4, 16, QChar('0'));
        } else {
            out += c;
        }
    }
    return out;
}

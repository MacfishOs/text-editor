// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef RECENTFILES_H
#define RECENTFILES_H

#include <QDateTime>
#include <QObject>
#include <QStringList>

// The shared freedesktop recently-used list (~/.local/share/recently-used.xbel),
// so files opened here show up wherever else recent files are listed.
class RecentFiles : public QObject
{
    Q_OBJECT
    // Local paths this application opened or saved, newest first.
    Q_PROPERTY(QStringList files READ files NOTIFY filesChanged)

public:
    explicit RecentFiles(const QString &storePath = QString(), QObject *parent = nullptr);

    QStringList files() const { return m_files; }

    void add(const QString &path, const QString &mimeType);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void reload();

    static QString defaultStorePath();

signals:
    void filesChanged();

private:
    QString m_storePath;
    QStringList m_files;
};

#endif

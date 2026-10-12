// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef RECENTFILES_H
#define RECENTFILES_H

#include <QObject>
#include <QStringList>

// Files this application opened or saved, kept under [Recent] in its own settings file.
class RecentFiles : public QObject
{
    Q_OBJECT
    // Local paths, newest first.
    Q_PROPERTY(QStringList files READ files NOTIFY filesChanged)

public:
    explicit RecentFiles(const QString &settingsFile, QObject *parent = nullptr);

    QStringList files() const { return m_files; }

    void add(const QString &path);
    Q_INVOKABLE void clear();

signals:
    void filesChanged();

private:
    void write();

    QString m_settingsFile;
    QStringList m_files;
};

#endif

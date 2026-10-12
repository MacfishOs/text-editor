// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "recentfiles.h"

#include <QFileInfo>
#include <QSettings>

static const QString filesKey = QStringLiteral("Recent/Files");
static constexpr int maximumFiles = 20;

static QStringList existing(const QStringList &paths)
{
    QStringList result;
    for (const QString &path : paths) {
        if (QFileInfo::exists(path))
            result.append(path);
    }
    return result;
}

RecentFiles::RecentFiles(const QString &settingsFile, QObject *parent)
    : QObject(parent)
    , m_settingsFile(settingsFile)
{
    const QSettings settings(m_settingsFile, QSettings::IniFormat);
    m_files = existing(settings.value(filesKey).toStringList()).mid(0, maximumFiles);
}

void RecentFiles::add(const QString &path)
{
    if (!m_files.isEmpty() && m_files.first() == path)
        return;
    QStringList files = existing(m_files);
    files.removeAll(path);
    files.prepend(path);
    m_files = files.mid(0, maximumFiles);
    write();
    emit filesChanged();
}

void RecentFiles::clear()
{
    if (m_files.isEmpty())
        return;
    m_files.clear();
    write();
    emit filesChanged();
}

void RecentFiles::write()
{
    QSettings settings(m_settingsFile, QSettings::IniFormat);
    if (m_files.isEmpty())
        settings.remove(filesKey);
    else
        settings.setValue(filesKey, m_files);
}

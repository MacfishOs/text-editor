// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "session.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

QString Session::defaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QLatin1String("/cutefish-text-editor/session.json");
}

bool Session::save(const QList<WindowState> &windows, const QString &path)
{
    QJsonArray windowArray;
    for (const WindowState &window : windows) {
        QJsonArray tabs;
        for (const TabState &tab : window.tabs) {
            QJsonObject object{
                {QStringLiteral("draft"), tab.draftId},
                {QStringLiteral("line"), tab.line},
                {QStringLiteral("column"), tab.column},
                {QStringLiteral("scroll"), tab.scroll},
            };
            if (!tab.path.isEmpty())
                object.insert(QStringLiteral("path"), tab.path);
            if (tab.unsaved)
                object.insert(QStringLiteral("unsaved"), true);
            if (!tab.encoding.isEmpty())
                object.insert(QStringLiteral("encoding"), tab.encoding);
            if (!tab.language.isEmpty())
                object.insert(QStringLiteral("language"), tab.language);
            tabs.append(object);
        }
        if (tabs.isEmpty())
            continue;
        windowArray.append(QJsonObject{{QStringLiteral("tabs"), tabs}, {QStringLiteral("current"), window.current}});
    }

    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(QJsonObject{{QStringLiteral("windows"), windowArray}}).toJson(QJsonDocument::Compact));
    return file.commit();
}

QList<WindowState> Session::load(const QString &path, bool *ok)
{
    QList<WindowState> result;
    if (ok)
        *ok = true;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (ok)
            *ok = !file.exists();
        return result;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        if (ok)
            *ok = false;
        return result;
    }
    const QJsonArray windows = document.object().value(QStringLiteral("windows")).toArray();
    for (const QJsonValue &windowValue : windows) {
        const QJsonObject windowObject = windowValue.toObject();
        WindowState window;
        for (const QJsonValue &tabValue : windowObject.value(QStringLiteral("tabs")).toArray()) {
            const QJsonObject object = tabValue.toObject();
            TabState tab;
            tab.path = object.value(QStringLiteral("path")).toString();
            tab.draftId = object.value(QStringLiteral("draft")).toString();
            tab.unsaved = object.value(QStringLiteral("unsaved")).toBool();
            tab.line = object.value(QStringLiteral("line")).toInt();
            tab.column = object.value(QStringLiteral("column")).toInt();
            tab.scroll = object.value(QStringLiteral("scroll")).toDouble();
            tab.encoding = object.value(QStringLiteral("encoding")).toString();
            tab.language = object.value(QStringLiteral("language")).toString();
            // An untitled tab without a draft had nothing in it.
            if (tab.path.isEmpty() && !tab.unsaved)
                continue;
            window.tabs.append(tab);
        }
        window.current = qBound(0, windowObject.value(QStringLiteral("current")).toInt(), qMax(0, int(window.tabs.size()) - 1));
        if (!window.tabs.isEmpty())
            result.append(window);
    }
    return result;
}

void Session::remove(const QString &path)
{
    QFile::remove(path);
}

void Session::removeOrphanDrafts(const QString &directory, const QSet<QString> &keep, int minimumAgeDays)
{
    const QDir dir(directory);
    const QDateTime limit = QDateTime::currentDateTime().addDays(-minimumAgeDays);
    for (const QFileInfo &info : dir.entryInfoList({QStringLiteral("*.txt")}, QDir::Files)) {
        if (!keep.contains(info.completeBaseName()) && (minimumAgeDays == 0 || info.lastModified() < limit))
            QFile::remove(info.filePath());
    }
}

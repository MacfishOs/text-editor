// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "recentfiles.h"

#include <QCoreApplication>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

static const QString bookmarkNs = QStringLiteral("http://www.freedesktop.org/standards/desktop-bookmarks");
static const QString mimeNs = QStringLiteral("http://www.freedesktop.org/standards/shared-mime-info");
static constexpr int maximumFiles = 20;

static QString applicationName()
{
    return QCoreApplication::applicationName();
}

static QDomDocument readStore(const QString &path)
{
    QDomDocument document;
    QFile file(path);
    if (file.open(QIODevice::ReadOnly))
        document.setContent(&file, QDomDocument::ParseOption::UseNamespaceProcessing);
    if (document.documentElement().tagName() != QLatin1String("xbel")) {
        document = QDomDocument();
        document.appendChild(document.createProcessingInstruction(QStringLiteral("xml"),
                                                                  QStringLiteral("version=\"1.0\" encoding=\"UTF-8\"")));
        QDomElement root = document.createElement(QStringLiteral("xbel"));
        root.setAttribute(QStringLiteral("version"), QStringLiteral("1.0"));
        root.setAttribute(QStringLiteral("xmlns:bookmark"), bookmarkNs);
        root.setAttribute(QStringLiteral("xmlns:mime"), mimeNs);
        document.appendChild(root);
    }
    return document;
}

static bool writeStore(const QString &path, const QDomDocument &document)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(document.toByteArray(2));
    return file.commit();
}

// The bookmark:application elements of a bookmark.
static QList<QDomElement> applications(const QDomElement &bookmark)
{
    QList<QDomElement> result;
    const QDomNodeList nodes = bookmark.elementsByTagNameNS(bookmarkNs, QStringLiteral("application"));
    for (int i = 0; i < nodes.size(); ++i)
        result.append(nodes.at(i).toElement());
    return result;
}

RecentFiles::RecentFiles(const QString &storePath, QObject *parent)
    : QObject(parent)
    , m_storePath(storePath.isEmpty() ? defaultStorePath() : storePath)
{
    reload();
}

QString RecentFiles::defaultStorePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QLatin1String("/recently-used.xbel");
}

void RecentFiles::reload()
{
    const QDomDocument document = readStore(m_storePath);
    QList<QPair<QString, QString>> entries;
    QDomElement bookmark = document.documentElement().firstChildElement(QStringLiteral("bookmark"));
    for (; !bookmark.isNull(); bookmark = bookmark.nextSiblingElement(QStringLiteral("bookmark"))) {
        const QUrl url(bookmark.attribute(QStringLiteral("href")));
        if (!url.isLocalFile())
            continue;
        for (const QDomElement &app : applications(bookmark)) {
            if (app.attribute(QStringLiteral("name")) == applicationName()) {
                entries.append({app.attribute(QStringLiteral("modified")), url.toLocalFile()});
                break;
            }
        }
    }
    // ISO 8601 times sort as text.
    std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) { return a.first > b.first; });

    QStringList files;
    for (const auto &entry : std::as_const(entries)) {
        if (files.size() >= maximumFiles)
            break;
        if (QFileInfo::exists(entry.second))
            files.append(entry.second);
    }
    if (files != m_files) {
        m_files = files;
        emit filesChanged();
    }
}

void RecentFiles::add(const QString &path, const QString &mimeType)
{
    QDomDocument document = readStore(m_storePath);
    QDomElement root = document.documentElement();
    const QString href = QString::fromUtf8(QUrl::fromLocalFile(path).toEncoded());
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);

    QDomElement bookmark = root.firstChildElement(QStringLiteral("bookmark"));
    for (; !bookmark.isNull(); bookmark = bookmark.nextSiblingElement(QStringLiteral("bookmark"))) {
        if (bookmark.attribute(QStringLiteral("href")) == href)
            break;
    }
    if (bookmark.isNull()) {
        bookmark = document.createElement(QStringLiteral("bookmark"));
        bookmark.setAttribute(QStringLiteral("href"), href);
        bookmark.setAttribute(QStringLiteral("added"), now);
        QDomElement info = document.createElement(QStringLiteral("info"));
        QDomElement metadata = document.createElement(QStringLiteral("metadata"));
        metadata.setAttribute(QStringLiteral("owner"), QStringLiteral("http://freedesktop.org"));
        QDomElement mime = document.createElementNS(mimeNs, QStringLiteral("mime:mime-type"));
        mime.setAttribute(QStringLiteral("type"), mimeType);
        metadata.appendChild(mime);
        metadata.appendChild(document.createElementNS(bookmarkNs, QStringLiteral("bookmark:applications")));
        info.appendChild(metadata);
        bookmark.appendChild(info);
        root.appendChild(bookmark);
    }
    bookmark.setAttribute(QStringLiteral("modified"), now);
    bookmark.setAttribute(QStringLiteral("visited"), now);

    QDomElement application;
    for (const QDomElement &app : applications(bookmark)) {
        if (app.attribute(QStringLiteral("name")) == applicationName())
            application = app;
    }
    if (application.isNull()) {
        QDomNodeList lists = bookmark.elementsByTagNameNS(bookmarkNs, QStringLiteral("applications"));
        QDomElement list = lists.isEmpty() ? QDomElement() : lists.at(0).toElement();
        if (list.isNull()) {
            QDomElement metadata = bookmark.firstChildElement(QStringLiteral("info")).firstChildElement(QStringLiteral("metadata"));
            if (metadata.isNull())
                return;
            list = document.createElementNS(bookmarkNs, QStringLiteral("bookmark:applications"));
            metadata.appendChild(list);
        }
        application = document.createElementNS(bookmarkNs, QStringLiteral("bookmark:application"));
        application.setAttribute(QStringLiteral("name"), applicationName());
        application.setAttribute(QStringLiteral("exec"), QStringLiteral("'%1 %u'").arg(applicationName()));
        application.setAttribute(QStringLiteral("count"), 0);
        list.appendChild(application);
    }
    application.setAttribute(QStringLiteral("modified"), now);
    application.setAttribute(QStringLiteral("count"), application.attribute(QStringLiteral("count")).toInt() + 1);

    writeStore(m_storePath, document);
    reload();
}

void RecentFiles::clear()
{
    QDomDocument document = readStore(m_storePath);
    QDomElement root = document.documentElement();
    QList<QDomElement> emptied;
    QDomElement bookmark = root.firstChildElement(QStringLiteral("bookmark"));
    for (; !bookmark.isNull(); bookmark = bookmark.nextSiblingElement(QStringLiteral("bookmark"))) {
        const QList<QDomElement> apps = applications(bookmark);
        int removed = 0;
        for (const QDomElement &app : apps) {
            if (app.attribute(QStringLiteral("name")) == applicationName()) {
                app.parentNode().removeChild(app);
                ++removed;
            }
        }
        if (removed > 0 && removed == apps.size())
            emptied.append(bookmark);
    }
    // Entries other applications share stay; ours alone go.
    for (QDomElement &element : emptied)
        root.removeChild(element);
    writeStore(m_storePath, document);
    reload();
}

#ifndef KOBROWSE_FILE_POLICY_H
#define KOBROWSE_FILE_POLICY_H

#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSet>
#include <QString>
#include <algorithm>
#include <dirent.h>

// Qt on some Kobo firmware omits entries even with QDir::Hidden. Supplement
// the Qt listing from readdir only when hidden items are requested. Never use
// Nickel's book-index exclusion rules to filter filesystem browsing.
inline QFileInfoList browserQtEntries(const QDir &dir, bool showHidden) {
    QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System;
    if (showHidden) filters |= QDir::Hidden;
    return dir.entryInfoList(filters, QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
}

inline bool browserEntryLess(const QFileInfo &a, const QFileInfo &b) {
    if (a.isDir() != b.isDir()) return a.isDir();
    const int folded = QString::compare(a.fileName(), b.fileName(), Qt::CaseInsensitive);
    if (folded != 0) return folded < 0;
    return QString::compare(a.fileName(), b.fileName(), Qt::CaseSensitive) < 0;
}

inline QFileInfoList completeBrowserEntries(const QDir &dir, const QFileInfoList &qtEntries,
                                           bool showHidden) {
    if (!showHidden) return qtEntries;
    DIR *raw = ::opendir(QFile::encodeName(dir.absolutePath()).constData());
    if (!raw) return qtEntries;
    QFileInfoList entries = qtEntries;
    QSet<QString> names;
    for (const QFileInfo &item : entries) names.insert(item.fileName());
    bool added = false;
    while (struct dirent *item = ::readdir(raw)) {
        const QString name = QFile::decodeName(item->d_name);
        if (name == QLatin1String(".") || name == QLatin1String("..") || names.contains(name)) continue;
        entries.append(QFileInfo(dir.filePath(name)));
        names.insert(name);
        added = true;
    }
    ::closedir(raw);
    if (added) std::stable_sort(entries.begin(), entries.end(), browserEntryLess);
    return entries;
}

inline QFileInfoList browserEntries(const QDir &dir, bool showHidden) {
    return completeBrowserEntries(dir, browserQtEntries(dir, showHidden), showHidden);
}

inline bool pathWithin(const QString &path, const QString &base) {
    return path == base || path.startsWith(base + QLatin1Char('/'));
}

// Fail closed on symbolic links (including dangling links) in any path component.
// allowMissingLeaf is only for a new direct child of an existing directory.
inline bool safeLocalPath(const QString &root, const QString &path, bool allowMissingLeaf = false) {
    const QString base = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
    const QString target = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    if (!pathWithin(target, base)) return false;
    const QFileInfo rootInfo(base);
    if (!rootInfo.exists() || !rootInfo.isDir() || rootInfo.isSymLink()) return false;
    if (target == base) return true;
    const QStringList components = target.mid(base.size() + 1).split(QLatin1Char('/'));
    QString current = base;
    for (int i = 0; i < components.size(); ++i) {
        current += QLatin1Char('/') + components.at(i);
        const QFileInfo part(current);
        if (part.isSymLink()) return false;
        if (!part.exists()) return allowMissingLeaf && i == components.size() - 1;
        if (i < components.size() - 1 && !part.isDir()) return false;
    }
    return true;
}

// These directories remain visible, but operations must not modify their contents.
// Kobo's FAT volume resolves differently-cased names to the same entry, so
// compare the first component without case even on case-sensitive test hosts.
inline bool protectedLocalPath(const QString &root, const QString &path) {
    const QString base = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
    const QString target = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    if (target == base) return true;
    if (!pathWithin(target, base)) return false;
    const QString first = target.mid(base.size() + 1).section(QLatin1Char('/'), 0, 0);
    return first.compare(QLatin1String(".kobo"), Qt::CaseInsensitive) == 0
        || first.compare(QLatin1String(".adds"), Qt::CaseInsensitive) == 0
        || first.compare(QLatin1String(".kobrowse_trash"), Qt::CaseInsensitive) == 0;
}

// Preflight copies before creating anything; never follow links, including nested ones.
inline bool safeCopyTree(const QString &root, const QString &path) {
    if (!safeLocalPath(root, path)) return false;
    const QFileInfo info(path);
    if (info.isFile()) return true;
    if (!info.isDir() || !QDir(path).isReadable()) return false;
    const QFileInfoList children = browserEntries(QDir(path), true);
    for (const QFileInfo &child : children)
        if (!safeCopyTree(root, child.absoluteFilePath())) return false;
    return true;
}

#endif

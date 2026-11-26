#ifndef KOBROWSE_FILE_POLICY_H
#define KOBROWSE_FILE_POLICY_H

#include <QDir>
#include <QFileInfo>
#include <QString>

// This reads the filesystem, not Nickel's library index or its exclusions.
inline QFileInfoList browserEntries(const QDir &dir, bool showHidden) {
    QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System;
    if (showHidden) filters |= QDir::Hidden;
    return dir.entryInfoList(filters, QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
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
inline bool protectedLocalPath(const QString &root, const QString &path) {
    const QString base = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
    const QString target = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    return target == base || pathWithin(target, base + QLatin1String("/.kobo"))
        || pathWithin(target, base + QLatin1String("/.adds"))
        || pathWithin(target, base + QLatin1String("/.kobrowse_trash"));
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

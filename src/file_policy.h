#ifndef KOBROWSE_FILE_POLICY_H
#define KOBROWSE_FILE_POLICY_H

#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSet>
#include <QString>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#include <cerrno>

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

// Operations must not silently accept a partial listing. Browsing can fall
// back to Qt's view, but copying, deletion and ZIP creation must fail closed
// on opendir/readdir/closedir errors or names Qt cannot round-trip faithfully.
inline bool operationEntries(const QString &path, QFileInfoList &entries) {
    entries.clear();
    DIR *dir = ::opendir(QFile::encodeName(path).constData());
    if (!dir) return false;
    bool ok = true;
    for (;;) {
        errno = 0;
        struct dirent *entry = ::readdir(dir);
        if (!entry) { if (errno) ok = false; break; }
        const QByteArray raw(entry->d_name);
        if (raw == "." || raw == "..") continue;
        const QString name = QFile::decodeName(raw);
        if (QFile::encodeName(name) != raw) { ok = false; break; }
        entries.append(QFileInfo(QDir(path).filePath(name)));
    }
    if (::closedir(dir) != 0) ok = false;
    if (!ok) entries.clear();
    return ok;
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

// Compare the first component by spelling and by filesystem identity; FAT may
// expose a short alias. The protected root itself is never made editable.
inline bool rootComponentMatches(const QString &base, const QString &first, const char *name) {
    if (first.compare(QLatin1String(name), Qt::CaseInsensitive) == 0) return true;
    struct stat candidate, guarded;
    return ::lstat(QFile::encodeName(base + QLatin1Char('/') + first).constData(), &candidate) == 0
        && ::lstat(QFile::encodeName(base + QLatin1Char('/') + QLatin1String(name)).constData(), &guarded) == 0
        && candidate.st_dev == guarded.st_dev && candidate.st_ino == guarded.st_ino;
}
inline bool protectedLocalPath(const QString &root, const QString &path, bool allowAdds = false) {
    const QString base = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
    const QString target = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    if (target == base) return true;
    if (!pathWithin(target, base)) return false;
    const QString relative = target.mid(base.size() + 1);
    const QString first = relative.section(QLatin1Char('/'), 0, 0);
    if (rootComponentMatches(base, first, ".kobo") || rootComponentMatches(base, first, ".kobrowse_trash")) return true;
    if (rootComponentMatches(base, first, ".adds")) return !(allowAdds && relative.contains(QLatin1Char('/')));
    return false;
}
// The root .adds folder may be used as a destination when enabled, but may
// not itself be renamed, moved, deleted, or selected as an operation source.
inline bool writableLocalDirectory(const QString &root, const QString &dir, bool allowAdds) {
    const QString base = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
    const QString target = QDir::cleanPath(QFileInfo(dir).absoluteFilePath());
    if (target == base) return true;
    if (!pathWithin(target, base)) return false;
    const QString rel = target.mid(base.size() + 1);
    if (allowAdds && !rel.contains(QLatin1Char('/')) && rootComponentMatches(base, rel, ".adds")) return true;
    return !protectedLocalPath(base, target, allowAdds);
}

// Trash contents remain selectable for deletion; the trash directory itself
// and other protected entries can only be browsed, not selected for mutation.
inline bool readOnlyLocalEntry(const QString &root, const QString &trash, const QString &path, bool allowAdds = false) {
    const QString item = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    const QString bin = QDir::cleanPath(QFileInfo(trash).absoluteFilePath());
    const bool insideTrash = item != bin && pathWithin(item, bin);
    return protectedLocalPath(root, item, allowAdds) && !insideTrash;
}

// Preflight copies before creating anything; never follow links, including nested ones.
inline bool safeCopyTree(const QString &root, const QString &path) {
    if (!safeLocalPath(root, path)) return false;
    const QFileInfo info(path);
    if (info.isFile()) return true;
    if (!info.isDir() || !QDir(path).isReadable()) return false;
    QFileInfoList children;
    if (!operationEntries(path, children)) return false;
    for (const QFileInfo &child : children)
        if (!safeCopyTree(root, child.absoluteFilePath())) return false;
    return true;
}

#endif

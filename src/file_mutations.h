#ifndef KOBROWSE_FILE_MUTATIONS_H
#define KOBROWSE_FILE_MUTATIONS_H
#include "file_policy.h"

// Remove the entry itself, never the target of a symbolic link. Callers choose
// and validate the allowed subtree; this does not protect against concurrent
// path replacement by another process.
inline bool removeTreeNoFollow(const QString &root, const QString &path) {
    const QFileInfo info(path);
    if (!safeLocalPath(root, info.absolutePath())) return false;
    if (info.isSymLink()) return QFile::remove(path);
    if (!safeLocalPath(root, path)) return false;
    if (!info.isDir()) return info.isFile() && QFile::remove(path);
    if (!QDir(path).isReadable()) return false;
    const QFileInfoList items = browserEntries(QDir(path), true);
    bool ok = true;
    for (const QFileInfo &item : items)
        if (!removeTreeNoFollow(root, item.absoluteFilePath())) ok = false;
    return ok && QDir().rmdir(path);
}

// Deleting items from the trash is allowed, but never the trash root itself.
inline bool mayDeleteLocalEntry(const QString &root, const QString &trash, const QString &path) {
    if (path == trash) return false;
    if (pathWithin(path, trash))
        return safeLocalPath(root, trash) && safeLocalPath(root, QFileInfo(path).absolutePath());
    return !protectedLocalPath(root, path) && safeLocalPath(root, path);
}

inline bool cleanupAppInstall(const QString &root) {
    const QString app = root + QLatin1String("/.adds/kobrowse");
    const QString menu = root + QLatin1String("/.adds/nm/kobrowse");
    bool ok = true;
    const QFileInfo appInfo(app), menuInfo(menu);
    if ((appInfo.exists() || appInfo.isSymLink()) && !removeTreeNoFollow(root, app)) ok = false;
    if ((menuInfo.exists() || menuInfo.isSymLink()) && !removeTreeNoFollow(root, menu)) ok = false;
    return ok;
}

// Rename one local, non-protected entry; never replace an existing destination.
inline bool renameLocalEntry(const QString &root, const QString &source, const QString &name, QString &error) {
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String("..")
        || name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))
        || name.contains(QChar(0)) || name.contains(QLatin1Char('\n')) || name.contains(QLatin1Char('\r'))) {
        error = QString::fromUtf8("Недопустимое имя"); return false;
    }
    if (!safeLocalPath(root, source) || protectedLocalPath(root, source)) {
        error = QString::fromUtf8("Ссылка или защищённый путь недоступны"); return false;
    }
    const QFileInfo fi(source);
    const QString dest = fi.absolutePath() + QLatin1Char('/') + name;
    if (dest == source) return true;
    if (!safeLocalPath(root, dest, true) || QFileInfo(dest).exists() || QFileInfo(dest).isSymLink()) {
        error = QString::fromUtf8("Имя уже занято или недоступно"); return false;
    }
    if (!QDir().rename(source, dest)) { error = QString::fromUtf8("Не удалось переименовать"); return false; }
    return true;
}

inline QString archiveDestination(const QString &dir, const QString &firstName) {
    const QString base = dir + QLatin1Char('/') + firstName;
    QString candidate = base + QLatin1String(".zip");
    for (int n = 1; QFileInfo(candidate).exists() || QFileInfo(candidate).isSymLink(); ++n)
        candidate = base + QLatin1Char('_') + QString::number(n) + QLatin1String(".zip");
    return candidate;
}
#endif

#ifndef KOBROWSE_FILE_MUTATIONS_H
#define KOBROWSE_FILE_MUTATIONS_H
#include "file_policy.h"
#include <QTemporaryDir>
#include <QDateTime>
#include <cerrno>

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
    QFileInfoList items;
    if (!operationEntries(path, items)) return false;
    bool ok = true;
    for (const QFileInfo &item : items)
        if (!removeTreeNoFollow(root, item.absoluteFilePath())) ok = false;
    return ok && QDir().rmdir(path);
}

// Deleting items from the trash is allowed, but never the trash root itself.
inline bool mayDeleteLocalEntry(const QString &root, const QString &trash, const QString &path, bool allowAdds = false) {
    const QString cleanTrash = QDir::cleanPath(QFileInfo(trash).absoluteFilePath());
    const QString cleanPath = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    if (cleanPath == cleanTrash) return false;
    if (pathWithin(cleanPath, cleanTrash))
        return safeLocalPath(root, cleanTrash) && safeLocalPath(root, QFileInfo(cleanPath).absolutePath());
    return !protectedLocalPath(root, cleanPath, allowAdds) && safeLocalPath(root, cleanPath);
}

// Moves may target the normal user area, or the trash root for a trash action.
// Trash contents may be moved out, but never the trash root or other protected entries.
inline bool moveLocalEntry(const QString &root, const QString &source, const QString &dest,
                           QString &error, bool toTrash = false, bool allowAdds = false) {
    const QString base = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
    const QString src = QDir::cleanPath(QFileInfo(source).absoluteFilePath());
    const QString dst = QDir::cleanPath(QFileInfo(dest).absoluteFilePath());
    const QString parent = QFileInfo(dst).absolutePath();
    const QString trash = base + QLatin1String("/.kobrowse_trash");
    const bool trashTarget = toTrash && parent == trash;
    if (!safeLocalPath(base, src) || readOnlyLocalEntry(base, trash, src, allowAdds)
        || !safeLocalPath(base, parent) || !QFileInfo(parent).isDir()
        || (!writableLocalDirectory(base, parent, allowAdds) && !trashTarget)
        || (!trashTarget && protectedLocalPath(base, dst, allowAdds))
        || !safeLocalPath(base, dst, true) || QFileInfo(dst).exists() || QFileInfo(dst).isSymLink()
        || (QFileInfo(src).isDir() && pathWithin(parent, src))) {
        error = QString::fromUtf8("Источник или место назначения защищены или недоступны"); return false;
    }
    if (!QDir().rename(src, dst)) { error = QString::fromUtf8("Не удалось переместить файл"); return false; }
    return true;
}

// Missing marker only requests removal if the app directory is actually present.
// An unavailable onboard mount or a symlink is not a request to uninstall.
inline bool uninstallRequested(const QString &root) {
    const QString app = root + QLatin1String("/.adds/kobrowse");
    const QString marker = app + QLatin1String("/uninstall");
    if (!safeLocalPath(root, app) || !QFileInfo(app).isDir()) return false;
    struct stat info;
    errno = 0;
    return ::lstat(QFile::encodeName(marker).constData(), &info) != 0 && errno == ENOENT;
}

// Preserve the complete app data directory, not just known settings and logs.
// Caller waits until NickelHook's failsafe has restored the plugin filename.
// The plugin path is supplied by the caller so tests can use a fake file.
inline bool cleanupAppInstall(const QString &root, const QString &plugin, QString &error) {
    const QString app = root + QLatin1String("/.adds/kobrowse");
    const QString menu = root + QLatin1String("/.adds/nm/kobrowse");
    const QString trash = root + QLatin1String("/.kobrowse_trash");
    if (!uninstallRequested(root) || !safeLocalPath(root, trash, true)
        || (!QFileInfo(trash).exists() && !QDir().mkdir(trash))
        || !safeLocalPath(root, trash) || !QFileInfo(trash).isDir()) {
        error = QString::fromUtf8("Каталог приложения или корзина недоступны"); return false;
    }
    const QFileInfo pluginInfo(plugin);
    if (!pluginInfo.exists() || !pluginInfo.isFile() || pluginInfo.isSymLink()) {
        error = QString::fromUtf8("Библиотека приложения недоступна"); return false;
    }
    const QString prefix = trash + QLatin1String("/kobrowse-uninstall-")
        + QDateTime::currentDateTime().toString(QLatin1String("yyyyMMdd-hhmmss"));
    QString backup = prefix;
    for (int n = 1; QFileInfo(backup).exists() || QFileInfo(backup).isSymLink(); ++n)
        backup = prefix + QLatin1Char('-') + QString::number(n);
    if (!uninstallRequested(root) || !safeLocalPath(root, backup, true) || !QDir().rename(app, backup)) {
        error = QString::fromUtf8("Не удалось сохранить настройки и журнал в корзине"); return false;
    }
    if (!QFile::remove(plugin)) {
        error = QString::fromUtf8("Не удалось удалить библиотеку");
        if (!safeLocalPath(root, app, true) || !QDir().rename(backup, app))
            error += QString::fromUtf8("; данные остались в корзине: ") + backup;
        return false;
    }
    const QFileInfo menuInfo(menu);
    if ((menuInfo.exists() || menuInfo.isSymLink()) && !removeTreeNoFollow(root, menu)) {
        error = QString::fromUtf8("Библиотека удалена, пункт меню остался; данные в корзине: ") + backup;
        return false;
    }
    return true;
}

// Copy into an isolated sibling directory; publish the completed entry only.
// The optional file operation allows deterministic failure injection in tests.
typedef bool (*CopyFileOperation)(const QString &, const QString &);
inline bool copyStagedContents(const QString &root, const QString &src, const QString &dst,
                               QString &error, CopyFileOperation copyFile) {
    if (!safeLocalPath(root, src)) { error = QString::fromUtf8("Источник недоступен"); return false; }
    const QFileInfo fi(src);
    if (fi.isDir()) {
        if (!QDir().mkdir(dst)) { error = QString::fromUtf8("Не удалось создать временную папку"); return false; }
        if (!QDir(src).isReadable()) { error = QString::fromUtf8("Не удалось прочитать папку"); return false; }
        QFileInfoList children;
        if (!operationEntries(src, children)) { error = QString::fromUtf8("Не удалось прочитать содержимое папки"); return false; }
        for (const QFileInfo &child : children)
            if (!copyStagedContents(root, child.absoluteFilePath(), dst + QLatin1Char('/') + child.fileName(), error, copyFile))
                return false;
        return true;
    }
    if (!fi.isFile() || !(copyFile ? copyFile(src, dst) : QFile::copy(src, dst))) {
        error = QString::fromUtf8("Не удалось скопировать: ") + fi.fileName();
        return false;
    }
    return true;
}
inline bool copyStagedTree(const QString &root, const QString &src, const QString &dst,
                           QString &error, CopyFileOperation copyFile = nullptr, bool allowAdds = false) {
    const QFileInfo source(src), target(dst);
    const QString parent = target.absolutePath();
    const QString trash = QDir::cleanPath(QFileInfo(root).absoluteFilePath()) + QLatin1String("/.kobrowse_trash");
    if (readOnlyLocalEntry(root, trash, src, allowAdds) || !safeCopyTree(root, src)
        || !safeLocalPath(root, parent) || !QFileInfo(parent).isDir()
        || !writableLocalDirectory(root, parent, allowAdds)
        || protectedLocalPath(root, dst, allowAdds)
        || !safeLocalPath(root, dst, true) || target.exists() || target.isSymLink()
        || (source.isDir() && pathWithin(parent, source.absoluteFilePath()))) {
        error = QString::fromUtf8("Источник или место назначения недоступны"); return false;
    }
    QTemporaryDir temporary(parent + QLatin1String("/.kobrowse-copy-XXXXXX"));
    if (!temporary.isValid() || !safeLocalPath(root, temporary.path())) {
        error = QString::fromUtf8("Не удалось создать временную папку"); return false;
    }
    const QString staged = temporary.path() + QLatin1String("/entry");
    if (!copyStagedContents(root, src, staged, error, copyFile)) {
        if (!temporary.remove()) error += QString::fromUtf8("; временную папку не удалось удалить: ") + temporary.path();
        return false;
    }
    if (!safeLocalPath(root, dst, true) || QFileInfo(dst).exists() || QFileInfo(dst).isSymLink()
        || !QDir().rename(staged, dst)) {
        error = QString::fromUtf8("Не удалось сохранить копию");
        if (!temporary.remove()) error += QString::fromUtf8("; временную папку не удалось удалить: ") + temporary.path();
        return false;
    }
    if (!temporary.remove()) {
        error = QString::fromUtf8("Копия создана, но временную папку не удалось удалить: ") + temporary.path();
    }
    return true;
}

// Create exactly one child without overwriting any existing name or link.
inline bool createLocalDirectory(const QString &root, const QString &parent, const QString &name,
                                 QString &error, bool allowAdds = false) {
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String("..")
        || name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))
        || name.contains(QChar(0)) || name.contains(QLatin1Char('\n')) || name.contains(QLatin1Char('\r'))) {
        error = QString::fromUtf8("Недопустимое имя"); return false;
    }
    if (name.size() > 255 || name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' '))) {
        error = QString::fromUtf8("Недопустимое имя"); return false;
    }
    for (int i = 0; i < name.size(); ++i) {
        if (name.at(i).unicode() < 32 || name.at(i).unicode() == 127
            || QString::fromLatin1(":*?\"<>|").contains(name.at(i))) {
            error = QString::fromUtf8("Недопустимое имя"); return false;
        }
    }
    const QString stem = name.section(QLatin1Char('.'), 0, 0).toUpper();
    if (stem == QLatin1String("CON") || stem == QLatin1String("PRN") || stem == QLatin1String("AUX") || stem == QLatin1String("NUL")
        || (stem.size() == 4 && (stem.startsWith(QLatin1String("COM")) || stem.startsWith(QLatin1String("LPT")))
            && stem.at(3) >= QLatin1Char('1') && stem.at(3) <= QLatin1Char('9'))) {
        error = QString::fromUtf8("Недопустимое имя"); return false;
    }
    if (!safeLocalPath(root, parent) || !QFileInfo(parent).isDir() || !writableLocalDirectory(root, parent, allowAdds)) {
        error = QString::fromUtf8("Папка недоступна для изменений"); return false;
    }
    QFileInfoList existing;
    if (!operationEntries(parent, existing)) { error = QString::fromUtf8("Не удалось прочитать папку"); return false; }
    const QString folded = name.normalized(QString::NormalizationForm_C).toCaseFolded();
    for (const QFileInfo &item : existing) {
        if (item.fileName().normalized(QString::NormalizationForm_C).toCaseFolded() == folded) {
            error = QString::fromUtf8("Имя уже занято или недоступно"); return false;
        }
    }
    const QString dest = QDir(parent).filePath(name);
    if (protectedLocalPath(root, dest, allowAdds) || !safeLocalPath(root, dest, true)
        || QFileInfo(dest).exists() || QFileInfo(dest).isSymLink()) {
        error = QString::fromUtf8("Имя уже занято или недоступно"); return false;
    }
    // mkdir is atomic: unlike mkpath it never succeeds for a pre-existing entry.
    if (!QDir(parent).mkdir(name)) { error = QString::fromUtf8("Не удалось создать папку"); return false; }
    return true;
}

// Rename one local, non-protected entry; never replace an existing destination.
inline bool renameLocalEntry(const QString &root, const QString &source, const QString &name, QString &error, bool allowAdds = false) {
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String("..")
        || name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))
        || name.contains(QChar(0)) || name.contains(QLatin1Char('\n')) || name.contains(QLatin1Char('\r'))) {
        error = QString::fromUtf8("Недопустимое имя"); return false;
    }
    if (!safeLocalPath(root, source) || protectedLocalPath(root, source, allowAdds)) {
        error = QString::fromUtf8("Ссылка или защищённый путь недоступны"); return false;
    }
    const QFileInfo fi(source);
    const QString dest = fi.absolutePath() + QLatin1Char('/') + name;
    if (dest == source) return true;
    if (protectedLocalPath(root, dest, allowAdds) || !safeLocalPath(root, dest, true)
        || QFileInfo(dest).exists() || QFileInfo(dest).isSymLink()) {
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

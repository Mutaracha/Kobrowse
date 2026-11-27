#ifndef KOBROWSE_HOME_FOLDER_H
#define KOBROWSE_HOME_FOLDER_H
#include "file_policy.h"
#include <QSaveFile>

inline bool validHomeFolder(const QString &root, const QString &path) {
    return !path.isEmpty() && QDir::isAbsolutePath(path) && safeLocalPath(root, path)
        && QFileInfo(path).isDir() && QDir(path).isReadable();
}
inline QString readHomeFolder(const QString &root, const QString &file) {
    if (!safeLocalPath(root, file)) return root;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) return root;
    const QString path = QString::fromUtf8(f.readAll());
    return validHomeFolder(root, path) ? QDir::cleanPath(path) : root;
}
inline bool writeHomeFolder(const QString &root, const QString &file, const QString &path) {
    if (!validHomeFolder(root, path) || !safeLocalPath(root, file, true)) return false;
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly)) return false;
    const QByteArray value = QDir::cleanPath(path).toUtf8();
    return f.write(value) == value.size() && f.commit();
}
inline bool resetHomeFolder(const QString &root, const QString &file) {
    if (!safeLocalPath(root, file, true)) return false;
    return !QFileInfo(file).exists() || QFile::remove(file);
}
#endif

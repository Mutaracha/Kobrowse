#ifndef KOBROWSE_ZIP_WRITER_H
#define KOBROWSE_ZIP_WRITER_H
#include <QString>
#include <QStringList>

// Sources must be direct children of the destination directory. Creates a
// ZIP32 archive without overwriting an existing file; sources are unchanged.
bool createZipArchive(const QString &root, const QString &destination,
                      const QStringList &sources, QString &error, bool allowAdds = false);
#endif

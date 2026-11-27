#ifndef KOBROWSE_ZIP_VALIDATOR_H
#define KOBROWSE_ZIP_VALIDATOR_H
#include <QIODevice>
#include <QString>

// Isolated ZIP32 validation only. Does not extract or create any files.
// The caller must supply a single already-open, seekable source. It is not a
// substitute for safe staging, FAT alias checks or publication of extracted data.
bool validateZipArchive(QIODevice &source, QString &error);
#endif

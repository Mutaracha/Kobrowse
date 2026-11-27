#ifndef KOBROWSE_ZIP_VALIDATOR_H
#define KOBROWSE_ZIP_VALIDATOR_H
#include <QIODevice>
#include <QString>

// A sink receives only names validated by the same parser that reads and
// checks each payload. It must stage outputs and discard them on any failure.
class ZipSink {
public:
    virtual ~ZipSink() {}
    virtual bool beginArchive(quint64 totalBytes, int entries, QString &error) = 0;
    virtual bool begin(const QString &name, bool directory, QString &error) = 0;
    virtual bool write(const char *data, int size, QString &error) = 0;
    virtual bool end(QString &error) = 0;
};
// An already-open regular file is used for the entire operation. A null sink
// only validates; a non-null sink receives payloads while CRC is checked.
bool processZipArchive(QIODevice &source, QString &error, ZipSink *sink = nullptr);
inline bool validateZipArchive(QIODevice &source, QString &error) {
    return processZipArchive(source, error);
}
#endif

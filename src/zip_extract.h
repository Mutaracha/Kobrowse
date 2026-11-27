#ifndef KOBROWSE_ZIP_EXTRACT_H
#define KOBROWSE_ZIP_EXTRACT_H
#include <QString>

// Optional failure injection for isolated tests. A publish callback returns an
// errno value (0 on success). Production always passes nullptr.
struct ZipExtractHooks {
    bool (*write)(int fd, const char *bytes, int size) = nullptr;
    int (*publish)(const QString &source, const QString &destination) = nullptr;
    bool (*cleanup)(const QString &root, const QString &stage) = nullptr;
};

// ZIP stays in place. Fully checked contents are published in a new directory;
// existing destinations are never intentionally overwritten. On failure, error
// includes a retained staging path if cleanup fails.
bool extractZipArchive(const QString &root, const QString &archive, QString &published,
                       QString &error, bool allowAdds = false, const ZipExtractHooks *hooks = nullptr);
#endif

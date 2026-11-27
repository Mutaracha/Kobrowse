#ifndef KOBROWSE_ZIP_EXTRACT_H
#define KOBROWSE_ZIP_EXTRACT_H
#include <QString>

// Optional failure injection for isolated tests. A publish callback returns an
// errno value (0 on success). Production always passes nullptr.
struct ZipExtractHooks {
    bool (*write)(int fd, const char *bytes, int size) = nullptr;
    int (*publish)(const QString &source, const QString &destination) = nullptr;
    bool (*cleanup)(const QString &root, const QString &stage) = nullptr;
    bool (*copyWrite)(int fd, const char *bytes, int size) = nullptr;
};

// ZIP stays in place. Fully checked top-level entries are published beside it;
// collisions receive suffixes. On partial publication, error reports outputs
// and the retained staging directory. Existing files are not overwritten.
bool extractZipArchive(const QString &root, const QString &archive, QString &published,
                       QString &error, bool allowAdds = false, const ZipExtractHooks *hooks = nullptr);
#endif

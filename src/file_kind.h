#ifndef KOBROWSE_FILE_KIND_H
#define KOBROWSE_FILE_KIND_H
#include <QString>

enum FileKind { FileGeneric, FileBook, FileImage, FileArchive, FileFb2, FileEpub, FilePdf };

// Classification is visual only; it does not promise that Nickel or Kobrowse
// can open a particular file. Check compound archive names before book suffixes.
inline FileKind fileKind(const QString &name) {
    const QString lower = name.toLower();
    const char *archives[] = { ".zip", ".rar", ".7z", ".tar", ".gz", ".cbz", ".cbr" };
    const char *images[] = { ".jpg", ".jpeg", ".gif", ".png", ".bmp", ".tif", ".tiff" };
    const char *books[] = { ".epub", ".pdf", ".mobi", ".txt", ".html", ".htm", ".rtf", ".fb2" };
    for (const char *ext : archives) if (lower.endsWith(QLatin1String(ext))) return FileArchive;
    for (const char *ext : images) if (lower.endsWith(QLatin1String(ext))) return FileImage;
    if (lower.endsWith(QLatin1String(".fb2"))) return FileFb2;
    if (lower.endsWith(QLatin1String(".epub"))) return FileEpub;
    if (lower.endsWith(QLatin1String(".pdf"))) return FilePdf;
    for (const char *ext : books) if (lower.endsWith(QLatin1String(ext))) return FileBook;
    return FileGeneric;
}
#endif

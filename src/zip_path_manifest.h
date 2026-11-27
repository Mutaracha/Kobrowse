#ifndef KOBROWSE_ZIP_PATH_MANIFEST_H
#define KOBROWSE_ZIP_PATH_MANIFEST_H

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

// Name policy only. The ZIP reader and staged extractor separately check
// headers, types, CRC, available space, FAT collisions and publication.
class ZipPathManifest {
public:
    bool add(const QByteArray &raw, bool utf8Flag, bool directory, QString &error) {
        error.clear();
        if (++entries > 10000 || raw.isEmpty() || raw.size() > 4096) return fail(error);
        if (!utf8Flag) {
            for (int i = 0; i < raw.size(); ++i)
                if (static_cast<unsigned char>(raw.at(i)) >= 128) return fail(error);
        }
        QString name = QString::fromUtf8(raw.constData(), raw.size());
        if (name.toUtf8() != raw || name.startsWith(QLatin1Char('/')) || name.contains(QLatin1Char('\\')))
            return fail(error);
        const bool trailing = name.endsWith(QLatin1Char('/'));
        if (trailing != directory) return fail(error);
        if (trailing) name.chop(1);
        const QStringList parts = name.split(QLatin1Char('/'), QString::KeepEmptyParts);
        if (parts.isEmpty() || parts.size() > 32) return fail(error);
        QString foldedPath, originalPath;
        // Validate without changing state so rejection cannot poison later entries.
        for (int i = 0; i < parts.size(); ++i) {
            const QString &part = parts.at(i);
            if (!validPart(part)) return fail(error);
            const QString folded = part.normalized(QString::NormalizationForm_C).toCaseFolded();
            if (i == 0 && (folded == QLatin1String(".kobo") || folded == QLatin1String(".adds")
                           || folded == QLatin1String(".kobrowse_trash"))) return fail(error);
            if (i) { foldedPath += QLatin1Char('/'); originalPath += QLatin1Char('/'); }
            foldedPath += folded; originalPath += part;
            const bool last = i == parts.size() - 1;
            if (nodes.contains(foldedPath)) {
                const Node node = nodes.value(foldedPath);
                if (node.original != originalPath || node.file || (last && (!directory || node.explicitEntry)))
                    return fail(error);
            }
        }
        foldedPath.clear(); originalPath.clear();
        for (int i = 0; i < parts.size(); ++i) {
            if (i) { foldedPath += QLatin1Char('/'); originalPath += QLatin1Char('/'); }
            foldedPath += parts.at(i).normalized(QString::NormalizationForm_C).toCaseFolded();
            originalPath += parts.at(i);
            const bool last = i == parts.size() - 1;
            if (!nodes.contains(foldedPath)) {
                Node node = { originalPath, last && !directory, last };
                nodes.insert(foldedPath, node);
            } else if (last) {
                Node node = nodes.value(foldedPath); node.explicitEntry = true; nodes.insert(foldedPath, node);
            }
        }
        return true;
    }
private:
    struct Node { QString original; bool file; bool explicitEntry; };
    QHash<QString, Node> nodes;
    int entries = 0;
    static bool fail(QString &error) { error = QString::fromUtf8("Недопустимое или конфликтующее имя в ZIP"); return false; }
    static bool validPart(const QString &part) {
        if (part.isEmpty() || part == QLatin1String(".") || part == QLatin1String("..")
            || part.size() > 255 || part.endsWith(QLatin1Char('.')) || part.endsWith(QLatin1Char(' '))) return false;
        for (int i = 0; i < part.size(); ++i) {
            const ushort ch = part.at(i).unicode();
            if (ch < 32 || ch == 127 || QString::fromLatin1(":*?\"<>|").contains(part.at(i))) return false;
        }
        const QString base = part.section(QLatin1Char('.'), 0, 0).toUpper();
        if (base == QLatin1String("CON") || base == QLatin1String("PRN") || base == QLatin1String("AUX")
            || base == QLatin1String("NUL")) return false;
        if (base.size() == 4 && (base.startsWith(QLatin1String("COM")) || base.startsWith(QLatin1String("LPT")))
            && base.at(3) >= QLatin1Char('1') && base.at(3) <= QLatin1Char('9')) return false;
        return true;
    }
};
#endif

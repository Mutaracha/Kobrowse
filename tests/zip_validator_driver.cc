#include "../src/zip_validator.h"
#include <QCoreApplication>
#include <QFile>
#include <QTextStream>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2) return 2;
    QFile input(QString::fromLocal8Bit(argv[1]));
    if (!input.open(QIODevice::ReadOnly)) return 2;
    QString error;
    if (!validateZipArchive(input, error)) { QTextStream(stderr) << error << '\n'; return 1; }
    return 0;
}

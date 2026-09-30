#include "MediaRename.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QDebug>

namespace MediaRename {

QString newPathFor(const QString& typedName, const QString& currentPath, QString* error) {
    auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return QString();
    };
    const QFileInfo current(currentPath);
    const QString suffix = current.suffix();

    QString name = typedName.trimmed();
    // The extension is kept automatically; typing it as well must not double it
    if (!suffix.isEmpty() && name.endsWith("." + suffix, Qt::CaseInsensitive))
        name.chop(suffix.size() + 1);
    while (name.endsWith('.') || name.endsWith(' '))
        name.chop(1);   // Windows file systems drop trailing dots and spaces

    if (name.isEmpty())
        return fail("Enter a name.");
    if (name.size() > 100)
        return fail("The name is too long (100 characters at most).");
    if (name.startsWith('.'))
        return fail("The name cannot start with a dot.");
    // FAT32, exFAT and NTFS reject these; the file is meant to be copied to a USB stick
    static const QString forbidden = QStringLiteral("\\/:*?\"<>|");
    for (const QChar c : name) {
        if (forbidden.contains(c) || c.unicode() < 0x20)
            return fail("The name cannot contain any of these characters:  \\ / : * ? \" < > |");
    }

    const QString fileName = suffix.isEmpty() ? name : name + "." + suffix;
    if (fileName == current.fileName())
        return currentPath;   // unchanged

    // Taken already? Compared without case: on a USB stick "Knee.mp4" and "knee.mp4" are one file
    const QDir dir = current.dir();
    for (const QString& existing : dir.entryList(QDir::Files | QDir::Hidden)) {
        if (existing.compare(fileName, Qt::CaseInsensitive) == 0 &&
            existing.compare(current.fileName(), Qt::CaseSensitive) != 0)
            return fail(QString("A file named \"%1\" already exists here.").arg(existing));
    }
    return QDir::cleanPath(dir.filePath(fileName));
}

bool rename(const QString& table, int id, const QString& currentPath, const QString& newPath, QString* error) {
    auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };
    if (table != "recordings" && table != "snapshots")
        return fail("Unknown media table.");
    if (!QFileInfo::exists(currentPath))
        return fail("The file no longer exists on this system.");
    if (QFileInfo::exists(newPath))
        return fail("A file with that name already exists.");

    if (!QFile::rename(currentPath, newPath))
        return fail("The file could not be renamed.");

    QSqlQuery query;
    query.prepare(QString("UPDATE %1 SET file_path = ? WHERE id = ?").arg(table));
    query.addBindValue(newPath);
    query.addBindValue(id);
    if (!query.exec() || query.numRowsAffected() != 1) {
        qWarning() << "Rename: database update failed, restoring the file name:" << query.lastError().text();
        QFile::rename(newPath, currentPath);
        return fail("The name could not be saved. The file keeps its old name.");
    }
    return true;
}

} // namespace MediaRename

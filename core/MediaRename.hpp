#pragma once

#include <QString>

// Renaming a recording or snapshot: the file on disk and its row in the database together, so
// the new name is also the file's name when it is copied to a USB stick.
namespace MediaRename {

// Turns what the user typed into the new full path next to `currentPath`, keeping the file's
// extension. Returns an empty string and sets `error` (a message for the user) if the name is
// empty, has characters a USB stick's file system rejects, or is already taken in that folder.
// Returns `currentPath` itself if the name is unchanged.
QString newPathFor(const QString& typedName, const QString& currentPath, QString* error);

// Renames the file and updates `table` (recordings or snapshots) row `id`. If the database
// update fails the file gets its old name back. Returns false and sets `error` on failure.
bool rename(const QString& table, int id, const QString& currentPath, const QString& newPath, QString* error);

} // namespace MediaRename

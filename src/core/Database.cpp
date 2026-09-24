        return false;
    }

    return QFile::setPermissions(destinationFilePath, perms);
}

/**
 * Restores the database file from the backup file with
 * name <filename>.old.<extension> to filePath. This will
 * overwrite the existing file!
 *
 * @param filePath Path to the file to restore
 * @return true on success
 */
bool Database::restoreDatabase(const QString& filePath, const QString& fromBackupFilePath)
{
    // Only try to restore if the backup file actually exists.
    if (!QFile::exists(fromBackupFilePath)) {
        return false;
    }

    QFile backupFile(fromBackupFilePath);
    if (!backupFile.open(QIODevice::ReadOnly)) {
        return false;
    }

    // Keep the existing file intact until the backup has been copied
    // successfully. If the destination was already removed by the caller,
    // inherit permissions from the backup instead of applying permissions
    // from a nonexistent file.
    const auto perms =
        QFile::exists(filePath) ? QFile::permissions(filePath) : QFile::permissions(fromBackupFilePath);
    QSaveFile restoredFile(filePath);
    if (!restoredFile.open(QIODevice::WriteOnly)) {
        return false;
    }

    constexpr qint64 chunkSize = 1024 * 1024;
    QByteArray buffer;
    buffer.resize(chunkSize);
    while (!backupFile.atEnd()) {
        const auto bytesRead = backupFile.read(buffer.data(), buffer.size());
        if (bytesRead <= 0) {
            restoredFile.cancelWriting();
            return false;
        }

        qint64 totalWritten = 0;
        while (totalWritten < bytesRead) {
            const auto bytesWritten = restoredFile.write(buffer.constData() + totalWritten, bytesRead - totalWritten);
            if (bytesWritten <= 0) {
                restoredFile.cancelWriting();
                return false;
            }
            totalWritten += bytesWritten;
        }
    }

    if (!restoredFile.commit()) {
        return false;
    }

    return QFile::setPermissions(filePath, perms);
}
/**
 * Returns true if the database key exists, has subkeys, and the
 * root group exists
 *
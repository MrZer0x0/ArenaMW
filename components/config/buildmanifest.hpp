#ifndef OPENMW_COMPONENTS_CONFIG_BUILDMANIFEST_HPP
#define OPENMW_COMPONENTS_CONFIG_BUILDMANIFEST_HPP

#include <QString>
#include <QStringList>

namespace Config
{
    /// Portable standalone build description stored next to a Data Files folder.
    /// Only standalone build, content and archive fields are represented.
    class BuildManifest
    {
    public:
        BuildManifest();

        void clear();

        bool read(const QString& filePath, QString* errorMessage = nullptr);
        bool write(const QString& filePath, QString* errorMessage = nullptr) const;

        QString resolvedDataPath(const QString& manifestPath) const;

        static QString canonicalPathForDataDir(const QString& dataDir);
        static QString findForDataDir(const QString& dataDir);
        static QString portableDataPath(const QString& manifestPath, const QString& dataDir);
        static QString canonicalLanguage(const QString& language);

        int formatVersion;
        QString buildName;
        QString dataPath;
        QString language;
        bool languageSpecified;
        bool complete;
        QStringList contentFiles;
        QStringList groundcoverFiles;
        QStringList archives;
    };
}

#endif

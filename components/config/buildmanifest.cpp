#include "buildmanifest.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>

namespace
{
    QString decodeValue(QString value)
    {
        value = value.trimmed();
        if (value.size() < 2 || value.front() != QLatin1Char('"')
            || value.back() != QLatin1Char('"'))
            return value;

        value = value.mid(1, value.size() - 2);
        QString decoded;
        decoded.reserve(value.size());
        bool escaped = false;
        for (const QChar ch : value)
        {
            if (escaped)
            {
                if (ch == QLatin1Char('n'))
                    decoded += QLatin1Char('\n');
                else if (ch == QLatin1Char('r'))
                    decoded += QLatin1Char('\r');
                else if (ch == QLatin1Char('t'))
                    decoded += QLatin1Char('\t');
                else
                    decoded += ch;
                escaped = false;
            }
            else if (ch == QLatin1Char('\\'))
                escaped = true;
            else
                decoded += ch;
        }
        if (escaped)
            decoded += QLatin1Char('\\');
        return decoded;
    }

    QString encodeValue(const QString& value)
    {
        QString encoded;
        encoded.reserve(value.size() + 2);
        encoded += QLatin1Char('"');
        for (const QChar ch : value)
        {
            if (ch == QLatin1Char('\\') || ch == QLatin1Char('"'))
                encoded += QLatin1Char('\\');
            if (ch == QLatin1Char('\n'))
                encoded += QStringLiteral("\\n");
            else if (ch == QLatin1Char('\r'))
                encoded += QStringLiteral("\\r");
            else if (ch == QLatin1Char('\t'))
                encoded += QStringLiteral("\\t");
            else
                encoded += ch;
        }
        encoded += QLatin1Char('"');
        return encoded;
    }

    bool isContentExtensionKey(const QString& key)
    {
        return key == QLatin1String("content") || key == QLatin1String("plugin")
            || key == QLatin1String("esm") || key == QLatin1String("esp")
            || key == QLatin1String("omwgame") || key == QLatin1String("omwaddon");
    }

    bool parseBool(const QString& value)
    {
        return value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0
            || value == QLatin1String("1")
            || value.compare(QStringLiteral("yes"), Qt::CaseInsensitive) == 0;
    }

    bool isBuildSection(const QString& section)
    {
        return section.isEmpty() || section == QLatin1String("build")
            || section == QLatin1String("general") || section == QLatin1String("manifest");
    }
}

Config::BuildManifest::BuildManifest()
{
    clear();
}

void Config::BuildManifest::clear()
{
    formatVersion = 1;
    buildName = QStringLiteral("ArenaMW");
    dataPath.clear();
    language = QStringLiteral("English");
    languageSpecified = false;
    complete = false;
    contentFiles.clear();
    groundcoverFiles.clear();
    archives.clear();
}

bool Config::BuildManifest::read(const QString& filePath, QString* errorMessage)
{
    clear();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (errorMessage)
            *errorMessage = file.errorString();
        return false;
    }

    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    QString section;

    while (!stream.atEnd())
    {
        const QString line = stream.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))
            || line.startsWith(QLatin1Char(';')))
            continue;

        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']')))
        {
            section = line.mid(1, line.size() - 2).trimmed().toLower();
            continue;
        }

        const int equals = line.indexOf(QLatin1Char('='));
        if (equals <= 0)
            continue;

        const QString key = line.left(equals).trimmed().toLower();
        const QString value = decodeValue(line.mid(equals + 1));

        if (isBuildSection(section)
            && (key == QLatin1String("format") || key == QLatin1String("version")))
        {
            bool ok = false;
            const int parsed = value.toInt(&ok);
            if (ok && parsed > 0)
                formatVersion = parsed;
        }
        else if (isBuildSection(section)
            && (key == QLatin1String("name") || key == QLatin1String("build-name")))
            buildName = value;
        else if (isBuildSection(section)
            && (key == QLatin1String("data") || key == QLatin1String("data-path")
                || key == QLatin1String("datafiles")))
            dataPath = value;
        else if (key == QLatin1String("language") || key == QLatin1String("locale")
            || ((section == QLatin1String("language") || section == QLatin1String("locale"))
                && (key == QLatin1String("value") || key == QLatin1String("name")
                    || key == QLatin1String("selected") || key == QLatin1String("current"))))
        {
            language = canonicalLanguage(value);
            languageSpecified = !value.trimmed().isEmpty();
        }
        else if (isBuildSection(section)
            && (key == QLatin1String("complete") || key == QLatin1String("locked")
                || key == QLatin1String("read-only")))
            complete = parseBool(value);
        else if (isContentExtensionKey(key))
            contentFiles.append(value);
        else if (key == QLatin1String("groundcover") || key == QLatin1String("grass"))
            groundcoverFiles.append(value);
        else if (key == QLatin1String("archive") || key == QLatin1String("bsa")
            || key == QLatin1String("fallback-archive"))
            archives.append(value);

        // Unknown and network-only keys are intentionally ignored.
    }

    if (buildName.trimmed().isEmpty())
        buildName = QStringLiteral("ArenaMW");
    language = canonicalLanguage(language);
    return true;
}

bool Config::BuildManifest::write(const QString& filePath, QString* errorMessage) const
{
    const QFileInfo info(filePath);
    QDir dir = info.absoluteDir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("Could not create %1").arg(dir.absolutePath());
        return false;
    }

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        if (errorMessage)
            *errorMessage = file.errorString();
        return false;
    }

    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    stream << "# ArenaMW standalone portable build manifest\n";
    stream << "# Ordered entries are applied exactly as written.\n\n";
    stream << "[Build]\n";
    stream << "format=" << (formatVersion > 0 ? formatVersion : 1) << "\n";
    stream << "name=" << encodeValue(buildName.trimmed().isEmpty()
        ? QStringLiteral("ArenaMW") : buildName.trimmed()) << "\n";
    stream << "data-path=" << encodeValue(dataPath) << "\n";
    stream << "language=" << encodeValue(canonicalLanguage(language)) << "\n";
    stream << "complete=" << (complete ? "true" : "false") << "\n\n";

    stream << "[Content]\n";
    for (const QString& fileName : contentFiles)
        stream << "content=" << encodeValue(fileName) << "\n";
    for (const QString& fileName : groundcoverFiles)
        stream << "groundcover=" << encodeValue(fileName) << "\n";

    stream << "\n[Archives]\n";
    for (const QString& archive : archives)
        stream << "archive=" << encodeValue(archive) << "\n";

    stream.flush();
    if (stream.status() != QTextStream::Ok || !file.commit())
    {
        if (errorMessage)
            *errorMessage = file.errorString();
        return false;
    }
    return true;
}

QString Config::BuildManifest::resolvedDataPath(const QString& manifestPath) const
{
    const QFileInfo manifestInfo(manifestPath);
    const QDir manifestDir = manifestInfo.absoluteDir();

    if (!dataPath.trimmed().isEmpty())
    {
        const QFileInfo pathInfo(dataPath);
        if (pathInfo.isAbsolute())
            return QDir::cleanPath(pathInfo.absoluteFilePath());
        return QDir::cleanPath(manifestDir.absoluteFilePath(dataPath));
    }

    if (manifestDir.dirName().compare(QStringLiteral("Data Files"), Qt::CaseInsensitive) == 0)
        return QDir::cleanPath(manifestDir.absolutePath());

    const QString siblingDataFiles
        = manifestDir.absoluteFilePath(QStringLiteral("Data Files"));
    if (QFileInfo(siblingDataFiles).isDir())
        return QDir::cleanPath(siblingDataFiles);

    return QDir::cleanPath(manifestDir.absolutePath());
}

QString Config::BuildManifest::canonicalLanguage(const QString& language)
{
    const QString value = language.trimmed();
    if (value.isEmpty())
        return QStringLiteral("English");

    struct LanguageAlias
    {
        const char* canonical;
        const char* aliases[4];
    };

    // Accept canonical identifiers and translated values written by older
    // ArenaMP/ArenaMW Launcher and Wizard revisions.
    static const LanguageAlias supportedLanguages[] = {
        { "English", { "English", "Английский", "Anglais", nullptr } },
        { "French",  { "French", "Французский", "Français", nullptr } },
        { "German",  { "German", "Немецкий", "Deutsch", nullptr } },
        { "Italian", { "Italian", "Итальянский", "Italiano", nullptr } },
        { "Polish",  { "Polish", "Польский", "Polski", nullptr } },
        { "Russian", { "Russian", "Русский", "Русский язык", nullptr } },
        { "Spanish", { "Spanish", "Испанский", "Español", nullptr } }
    };

    for (const LanguageAlias& languageAlias : supportedLanguages)
    {
        for (const char* alias : languageAlias.aliases)
        {
            if (alias == nullptr)
                break;
            if (value.compare(QString::fromUtf8(alias), Qt::CaseInsensitive) == 0)
                return QString::fromLatin1(languageAlias.canonical);
        }
    }

    return value;
}

QString Config::BuildManifest::canonicalPathForDataDir(const QString& dataDir)
{
    // build.ini describes one concrete Data Files installation. Keep it beside
    // the standard Data Files directory, or inside a non-standard data folder.
    QDir dataDirectory(QDir::cleanPath(dataDir));
    if (dataDirectory.dirName().compare(QStringLiteral("Data Files"), Qt::CaseInsensitive) == 0)
    {
        QDir parentDirectory(dataDirectory);
        if (parentDirectory.cdUp())
            return parentDirectory.absoluteFilePath(QStringLiteral("build.ini"));
    }

    if (!dataDirectory.absolutePath().isEmpty())
        return dataDirectory.absoluteFilePath(QStringLiteral("build.ini"));

    return QDir(QCoreApplication::applicationDirPath())
        .absoluteFilePath(QStringLiteral("build.ini"));
}

QString Config::BuildManifest::findForDataDir(const QString& dataDir)
{
    QStringList candidates;

    const QString canonicalManifest = canonicalPathForDataDir(dataDir);
    if (!canonicalManifest.isEmpty())
        candidates.append(canonicalManifest);

    QDir dataDirectory(QDir::cleanPath(dataDir));
    const QString legacyInsideDataFiles
        = dataDirectory.absoluteFilePath(QStringLiteral("build.ini"));
    if (!candidates.contains(legacyInsideDataFiles, Qt::CaseInsensitive))
        candidates.append(legacyInsideDataFiles);

    // Compatibility with portable builds made by earlier patches.
    const QString applicationDir = QCoreApplication::applicationDirPath();
    if (!applicationDir.isEmpty())
    {
        const QString besideExecutable
            = QDir(applicationDir).absoluteFilePath(QStringLiteral("build.ini"));
        if (!candidates.contains(besideExecutable, Qt::CaseInsensitive))
            candidates.append(besideExecutable);
    }

    const QString workingDirectoryManifest
        = QDir::current().absoluteFilePath(QStringLiteral("build.ini"));
    if (!candidates.contains(workingDirectoryManifest, Qt::CaseInsensitive))
        candidates.append(workingDirectoryManifest);

    for (const QString& candidate : candidates)
    {
        const QFileInfo info(candidate);
        if (info.exists() && info.isFile() && info.isReadable())
            return QDir::cleanPath(info.absoluteFilePath());
    }

    return QString();
}

QString Config::BuildManifest::portableDataPath(
    const QString& manifestPath, const QString& dataDir)
{
    const QDir manifestDir = QFileInfo(manifestPath).absoluteDir();
    QString relative = manifestDir.relativeFilePath(QDir::cleanPath(dataDir));
    if (relative.isEmpty())
        relative = QStringLiteral(".");
    return QDir::fromNativeSeparators(relative);
}

#include "maindialog.hpp"

#include <components/version/version.hpp>
#include <components/misc/helpviewer.hpp>
#include <components/config/buildmanifest.hpp>
#include <components/config/contentorder.hpp>

#include <QDate>
#include <QMessageBox>
#include <QFontDatabase>
#include <QInputDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QCloseEvent>
#include <QTextCodec>

#include "playpage.hpp"
#include "graphicspage.hpp"
#include "datafilespage.hpp"
#include "settingspage.hpp"
#include "advancedpage.hpp"

using namespace Process;

namespace
{
    constexpr int sLauncherWidth = 1024;
    constexpr int sLauncherHeight = 650;
}

void cfgError(const QString& title, const QString& msg) {
    QMessageBox msgBox;
    msgBox.setWindowTitle(title);
    msgBox.setIcon(QMessageBox::Critical);
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.setText(msg);
    msgBox.exec();
}

namespace
{
    bool containsGameContent(const QDir& dir)
    {
        if (!dir.exists())
            return false;

        const QStringList files = dir.entryList(
            QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
        for (const QString& fileName : files)
        {
            if (fileName.endsWith(QLatin1String(".esm"), Qt::CaseInsensitive)
                || fileName.endsWith(QLatin1String(".esp"), Qt::CaseInsensitive)
                || fileName.endsWith(QLatin1String(".omwgame"), Qt::CaseInsensitive)
                || fileName.endsWith(QLatin1String(".omwaddon"), Qt::CaseInsensitive))
                return true;
        }
        return false;
    }

    QString resolveDataFilesDirectory(const QString& selectedPath)
    {
        if (selectedPath.trimmed().isEmpty())
            return QString();

        const QString cleanPath = QDir::cleanPath(selectedPath);
        const QDir selectedDir(cleanPath);
        if (!selectedDir.exists())
            return QString();
        if (containsGameContent(selectedDir))
            return cleanPath;

        const QStringList childDirectories = selectedDir.entryList(
            QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
        for (const QString& childName : childDirectories)
        {
            if (childName.compare(QLatin1String("Data Files"), Qt::CaseInsensitive) != 0)
                continue;

            const QString childPath = QDir::cleanPath(selectedDir.filePath(childName));
            if (containsGameContent(QDir(childPath)))
                return childPath;
        }
        return QString();
    }
}

Launcher::MainDialog::MainDialog(QWidget *parent)
    : QMainWindow(parent)
    , mBuildComplete(false)
    , mGameSettings (mCfgMgr)
{
    setupUi(this);
    setFixedSize(sLauncherWidth, sLauncherHeight);

    mGameInvoker = new ProcessInvoker();
    mWizardInvoker = new ProcessInvoker();

    connect(mWizardInvoker->getProcess(), SIGNAL(started()),
            this, SLOT(wizardStarted()));

    connect(mWizardInvoker->getProcess(), SIGNAL(finished(int,QProcess::ExitStatus)),
            this, SLOT(wizardFinished(int,QProcess::ExitStatus)));

    iconWidget->setViewMode(QListView::IconMode);
    iconWidget->setWrapping(false);
    iconWidget->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff); // Just to be sure
    iconWidget->setIconSize(QSize(48, 48));
    iconWidget->setMovement(QListView::Static);

    iconWidget->setSpacing(4);
    iconWidget->setCurrentRow(0);
    iconWidget->setFlow(QListView::LeftToRight);

    QPushButton *helpButton = new QPushButton(tr("Help"));
    QPushButton *playButton = new QPushButton(tr("Play"));
    buttonBox->button(QDialogButtonBox::Close)->setText(tr("Close"));
    buttonBox->addButton(helpButton, QDialogButtonBox::HelpRole);
    buttonBox->addButton(playButton, QDialogButtonBox::AcceptRole);

    connect(buttonBox, SIGNAL(rejected()), this, SLOT(close()));
    connect(buttonBox, SIGNAL(accepted()), this, SLOT(play()));
    connect(buttonBox, SIGNAL(helpRequested()), this, SLOT(help()));

    // Remove what's this? button
    setWindowFlags(this->windowFlags() & ~Qt::WindowContextHelpButtonHint);

    createIcons();
}

Launcher::MainDialog::~MainDialog()
{
    delete mGameInvoker;
    delete mWizardInvoker;
}

void Launcher::MainDialog::createIcons()
{
    if (!QIcon::hasThemeIcon("document-new"))
        QIcon::setThemeName("tango");

    QListWidgetItem *playButton = new QListWidgetItem(iconWidget);
    playButton->setIcon(QIcon(":/images/openmw.png"));
    playButton->setText(tr("Play"));
    playButton->setTextAlignment(Qt::AlignCenter);
    playButton->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    QListWidgetItem *dataFilesButton = new QListWidgetItem(iconWidget);
    dataFilesButton->setIcon(QIcon(":/images/openmw-plugin.png"));
    dataFilesButton->setText(tr("Data Files"));
    dataFilesButton->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    dataFilesButton->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    QListWidgetItem *graphicsButton = new QListWidgetItem(iconWidget);
    graphicsButton->setIcon(QIcon(":/images/preferences-video.png"));
    graphicsButton->setText(tr("Graphics"));
    graphicsButton->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom | Qt::AlignAbsolute);
    graphicsButton->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    QListWidgetItem *settingsButton = new QListWidgetItem(iconWidget);
    settingsButton->setIcon(QIcon(":/images/preferences.png"));
    settingsButton->setText(tr("Settings"));
    settingsButton->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    settingsButton->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    QListWidgetItem *advancedButton = new QListWidgetItem(iconWidget);
    advancedButton->setIcon(QIcon(":/images/preferences-advanced.png"));
    advancedButton->setText(tr("Advanced"));
    advancedButton->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    advancedButton->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    connect(iconWidget,
            SIGNAL(currentItemChanged(QListWidgetItem*,QListWidgetItem*)),
            this, SLOT(changePage(QListWidgetItem*,QListWidgetItem*)));

}

void Launcher::MainDialog::createPages()
{
    // Avoid creating the widgets twice
    if (pagesWidget->count() != 0)
        return;

    mPlayPage = new PlayPage(this);
    mDataFilesPage = new DataFilesPage(mCfgMgr, mGameSettings, mLauncherSettings, this);
    mGraphicsPage = new GraphicsPage(mLauncherSettings, this);
    mSettingsPage = new SettingsPage(mCfgMgr, mGameSettings, mLauncherSettings, this);
    mAdvancedPage = new AdvancedPage(mGameSettings, this);

    // Set the combobox of the play page to imitate the combobox on the datafilespage
    mPlayPage->setProfilesModel(mDataFilesPage->profilesModel());
    mPlayPage->setProfilesIndex(mDataFilesPage->profilesIndex());

    // Add the pages to the stacked widget
    pagesWidget->addWidget(mPlayPage);
    pagesWidget->addWidget(mDataFilesPage);
    pagesWidget->addWidget(mGraphicsPage);
    pagesWidget->addWidget(mSettingsPage);
    pagesWidget->addWidget(mAdvancedPage);

    // Select the first page
    iconWidget->setCurrentItem(iconWidget->item(0), QItemSelectionModel::Select);

    connect(mPlayPage, SIGNAL(playButtonClicked()), this, SLOT(play()));

    connect(mPlayPage, SIGNAL(signalProfileChanged(int)), mDataFilesPage, SLOT(slotProfileChanged(int)));
    connect(mDataFilesPage, SIGNAL(signalProfileChanged(int)), mPlayPage, SLOT(setProfilesIndex(int)));
    // Using Qt::QueuedConnection because signal is emitted in a subthread and slot is in the main thread
    connect(mDataFilesPage, SIGNAL(signalLoadedCellsChanged(QStringList)), mAdvancedPage, SLOT(slotLoadedCellsChanged(QStringList)), Qt::QueuedConnection);

}

Launcher::FirstRunDialogResult Launcher::MainDialog::showFirstRunDialog()
{
    if (!setupLauncherSettings())
        return FirstRunDialogResultFailure;

    if (mLauncherSettings.value(QString("General/firstrun"), QString("true")) == QLatin1String("true"))
    {
        QMessageBox msgBox;
        msgBox.setWindowTitle(tr("First run"));
        msgBox.setIcon(QMessageBox::Question);
        msgBox.setStandardButtons(QMessageBox::NoButton);
        msgBox.setText(tr("<html><head/><body><p><b>Welcome to OpenMW!</b></p> \
                          <p>It is recommended to run the Installation Wizard.</p> \
                          <p>The Wizard will let you select an existing Morrowind installation, \
                          or install Morrowind for OpenMW to use.</p></body></html>"));

        QAbstractButton *wizardButton =
                msgBox.addButton(tr("Run &Installation Wizard"), QMessageBox::AcceptRole); // ActionRole doesn't work?!
        QAbstractButton *skipButton =
                msgBox.addButton(tr("Skip"), QMessageBox::RejectRole);

        msgBox.exec();

        if (msgBox.clickedButton() == wizardButton)
        {
            if (mWizardInvoker->startProcess(QLatin1String("openmw-wizard"), false))
                return FirstRunDialogResultWizard;
        }
        else if (msgBox.clickedButton() == skipButton)
        {
            // Don't bother setting up absent game data.
            if (setup())
                return FirstRunDialogResultContinue;
        }
        return FirstRunDialogResultFailure;
    }

    if (!setup() || !setupGameData()) {
        return FirstRunDialogResultFailure;
    }
    return FirstRunDialogResultContinue;
}

void Launcher::MainDialog::setVersionLabel()
{
    // Add version information to bottom of the window
    Version::Version v = Version::getOpenmwVersion(mGameSettings.value("resources").toUtf8().constData());

    QString revision(QString::fromUtf8(v.mCommitHash.c_str()));
    QString tag(QString::fromUtf8(v.mTagHash.c_str()));

    versionLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    if (!v.mVersion.empty() && (revision.isEmpty() || revision == tag))
        versionLabel->setText(tr("OpenMW %1 release").arg(QString::fromUtf8(v.mVersion.c_str())));
    else
        versionLabel->setText(tr("OpenMW development (%1)").arg(revision.left(10)));

    // Add the compile date and time
    auto compileDate = QLocale(QLocale::C).toDate(QString(__DATE__).simplified(), QLatin1String("MMM d yyyy"));
    auto compileTime = QLocale(QLocale::C).toTime(QString(__TIME__).simplified(), QLatin1String("hh:mm:ss"));
    versionLabel->setToolTip(tr("Compiled on %1 %2").arg(QLocale::system().toString(compileDate, QLocale::LongFormat),
                                                         QLocale::system().toString(compileTime, QLocale::ShortFormat)));
}

bool Launcher::MainDialog::setup()
{
    if (!setupGameSettings())
        return false;

    const bool buildManifestLoaded = loadBuildManifest();
    setVersionLabel();

    // build.ini is authoritative. Do not overwrite its exact content order with
    // the merged openmw.cfg/launcher.cfg list during startup.
    if (!buildManifestLoaded)
        mLauncherSettings.setContentList(mGameSettings);

    if (!setupGraphicsSettings())
        return false;

    // Now create the pages as they need the settings
    createPages();

    // Call this so we can exit on SDL errors before mainwindow is shown
    if (!mGraphicsPage->loadSettings())
        return false;

    loadSettings();
    applyBuildManifestRestrictions();

    return true;
}

bool Launcher::MainDialog::reloadSettings()
{
    if (!setupLauncherSettings())
        return false;

    if (!setupGameSettings())
        return false;

    const bool buildManifestLoaded = loadBuildManifest();
    if (!buildManifestLoaded)
        mLauncherSettings.setContentList(mGameSettings);

    if (!setupGraphicsSettings())
        return false;

    if (!mSettingsPage->loadSettings())
        return false;

    if (!mDataFilesPage->loadSettings())
        return false;

    if (!mGraphicsPage->loadSettings())
        return false;

    if (!mAdvancedPage->loadSettings())
        return false;

    loadSettings();
    applyBuildManifestRestrictions();

    return true;
}

void Launcher::MainDialog::changePage(QListWidgetItem *current, QListWidgetItem *previous)
{
    if (!current)
        current = previous;

    int currentIndex = iconWidget->row(current);
    pagesWidget->setCurrentIndex(currentIndex);
    mSettingsPage->resetProgressBar();
}

bool Launcher::MainDialog::setupLauncherSettings()
{
    mLauncherSettings.clear();

    mLauncherSettings.setMultiValueEnabled(true);

    QString userPath = QString::fromUtf8(mCfgMgr.getUserConfigPath().string().c_str());

    QStringList paths;
    paths.append(QString(Config::LauncherSettings::sLauncherConfigFileName));
    paths.append(QDir(userPath).filePath(QString(Config::LauncherSettings::sLauncherConfigFileName)));

    for (const QString &path : paths)
    {
        qDebug() << "Loading config file:" << path.toUtf8().constData();
        QFile file(path);
        if (file.exists()) {
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                cfgError(tr("Error opening OpenMW configuration file"),
                         tr("<br><b>Could not open %0 for reading</b><br><br> \
                             Please make sure you have the right permissions \
                             and try again.<br>").arg(file.fileName()));
                return false;
            }
            QTextStream stream(&file);
            stream.setCodec(QTextCodec::codecForName("UTF-8"));

            mLauncherSettings.readFile(stream);
        }
        file.close();
    }

    return true;
}

bool Launcher::MainDialog::setupGameSettings()
{
    mGameSettings.clear();

    QString localPath = QString::fromUtf8(mCfgMgr.getLocalPath().string().c_str());
    QString userPath = QString::fromUtf8(mCfgMgr.getUserConfigPath().string().c_str());
    QString globalPath = QString::fromUtf8(mCfgMgr.getGlobalPath().string().c_str());

    // Load the user config file first, separately
    // So we can write it properly, uncontaminated
    QString path = QDir(userPath).filePath(QLatin1String("openmw.cfg"));
    QFile file(path);

    qDebug() << "Loading config file:" << path.toUtf8().constData();

    if (file.exists()) {
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            cfgError(tr("Error opening OpenMW configuration file"),
                     tr("<br><b>Could not open %0 for reading</b><br><br> \
                         Please make sure you have the right permissions \
                         and try again.<br>").arg(file.fileName()));
            return false;
        }
        QTextStream stream(&file);
        stream.setCodec(QTextCodec::codecForName("UTF-8"));

        mGameSettings.readUserFile(stream);
        file.close();
    }

    // Now the rest - priority: user > local > global
    QStringList paths;
    paths.append(QDir(globalPath).filePath(QStringLiteral("openmw.cfg")));
    paths.append(QDir(localPath).filePath(QStringLiteral("openmw.cfg")));
    paths.append(QDir(userPath).filePath(QStringLiteral("openmw.cfg")));

    for (const QString &path2 : paths)
    {
        qDebug() << "Loading config file:" << path2.toUtf8().constData();

        file.setFileName(path2);
        if (file.exists()) {
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                cfgError(tr("Error opening OpenMW configuration file"),
                         tr("<br><b>Could not open %0 for reading</b><br><br> \
                             Please make sure you have the right permissions \
                             and try again.<br>").arg(file.fileName()));
                return false;
            }
            QTextStream stream(&file);
            stream.setCodec(QTextCodec::codecForName("UTF-8"));

            mGameSettings.readFile(stream);
            file.close();
        }
    }

    // Normalize legacy data= entries before loadBuildManifest() runs. This
    // makes build.ini, groundcover and BSA ordering available during the same
    // Launcher start even when an older Wizard stored the Morrowind root.
    const QStringList configuredDataDirs = mGameSettings.getDataDirs();
    for (const QString& configuredPath : configuredDataDirs)
    {
        const QString resolvedPath = resolveDataFilesDirectory(configuredPath);
        if (!resolvedPath.isEmpty()
            && !mGameSettings.getDataDirs().contains(resolvedPath, Qt::CaseInsensitive))
        {
            mGameSettings.setMultiValue(QLatin1String("data"), resolvedPath);
            mGameSettings.addDataDir(resolvedPath);
        }
    }

    return true;
}

bool Launcher::MainDialog::setupGameData()
{
    QStringList candidates = mGameSettings.getDataDirs();
    if (!mBuildDataPath.isEmpty())
        candidates.prepend(mBuildDataPath);

    // Recover portable installations and configurations created by older
    // Wizard builds that stored the Morrowind root instead of Data Files.
    const QString localPath = QString::fromUtf8(mCfgMgr.getLocalPath().string().c_str());
    if (!localPath.isEmpty())
    {
        candidates.append(QDir(localPath).filePath(QLatin1String("Data Files")));
        candidates.append(localPath);
    }

    QStringList dataDirs;
    for (const QString& candidate : candidates)
    {
        const QString resolvedPath = resolveDataFilesDirectory(candidate);
        if (resolvedPath.isEmpty()
            || dataDirs.contains(resolvedPath, Qt::CaseInsensitive))
            continue;

        dataDirs.append(resolvedPath);
        if (!mGameSettings.getDataDirs().contains(resolvedPath, Qt::CaseInsensitive))
        {
            mGameSettings.setMultiValue(QLatin1String("data"), resolvedPath);
            mGameSettings.addDataDir(resolvedPath);
        }
    }

    if (dataDirs.isEmpty())
    {
        QMessageBox msgBox;
        msgBox.setWindowTitle(tr("Error detecting Morrowind installation"));
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.setStandardButtons(QMessageBox::NoButton);
        msgBox.setText(tr("<br><b>Could not find the Data Files location</b><br><br> \
                                   The directory containing the data files was not found."));

        QAbstractButton *wizardButton =
                msgBox.addButton(tr("Run &Installation Wizard..."), QMessageBox::ActionRole);
        QAbstractButton *skipButton =
                msgBox.addButton(tr("Skip"), QMessageBox::RejectRole);

        Q_UNUSED(skipButton); // Suppress compiler unused warning

        msgBox.exec();

        if (msgBox.clickedButton() == wizardButton)
        {
            if (!mWizardInvoker->startProcess(QLatin1String("openmw-wizard"), false))
                return false;
        }
    }

    return true;
}

QString Launcher::MainDialog::primaryDataDirectory() const
{
    const QString dataLocal = mGameSettings.getDataLocal();
    if (!dataLocal.isEmpty() && QFileInfo(dataLocal).isDir())
        return QDir::cleanPath(dataLocal);

    const QStringList dataDirs = mGameSettings.getDataDirs();
    for (auto it = dataDirs.crbegin(); it != dataDirs.crend(); ++it)
    {
        if (!Config::BuildManifest::findForDataDir(*it).isEmpty())
            return QDir::cleanPath(*it);
    }

    const QStringList filters = {
        QStringLiteral("*.esm"), QStringLiteral("*.esp"),
        QStringLiteral("*.omwgame"), QStringLiteral("*.omwaddon")
    };
    for (auto it = dataDirs.crbegin(); it != dataDirs.crend(); ++it)
    {
        QDir dir(*it);
        if (dir.exists()
            && !dir.entryList(filters, QDir::Files | QDir::Readable).isEmpty())
            return QDir::cleanPath(*it);
    }

    return dataDirs.isEmpty() ? QString() : QDir::cleanPath(dataDirs.last());
}

bool Launcher::MainDialog::loadBuildManifest()
{
    mBuildManifestPath.clear();
    mBuildName = mLauncherSettings.value(
        QStringLiteral("General/Build/name"), QStringLiteral("ArenaMW"));
    mBuildDataPath = primaryDataDirectory();
    mBuildComplete = false;

    if (mBuildDataPath.isEmpty())
        return false;

    const QString manifestPath = Config::BuildManifest::findForDataDir(mBuildDataPath);
    if (manifestPath.isEmpty())
    {
        // No saved build order exists yet. Apply the recommended order once,
        // then the first Launcher save creates build.ini and all later starts
        // preserve the user's exact order from that file.
        const QStringList orderedContent = Config::applyCanonicalContentOrder(
            mGameSettings.getContentList(), QDir(mBuildDataPath));
        if (!orderedContent.isEmpty())
        {
            mGameSettings.setContentList(orderedContent);
            QString profileName = mLauncherSettings.getCurrentContentListName();
            if (profileName.isEmpty())
                profileName = QStringLiteral("Default");
            QStringList profileFiles = orderedContent;
            const QStringList groundcover = mGameSettings.getGroundcoverList();
            profileFiles.append(groundcover);
            mLauncherSettings.setContentList(profileName, profileFiles, groundcover,
                !groundcover.isEmpty());
            mLauncherSettings.setCurrentContentListName(profileName);
        }
        return false;
    }

    Config::BuildManifest manifest;
    QString error;
    if (!manifest.read(manifestPath, &error))
    {
        qWarning() << "Could not read standalone build manifest"
                   << manifestPath << error;
        return false;
    }

    const QString resolvedDataPath = manifest.resolvedDataPath(manifestPath);
    if (QFileInfo(resolvedDataPath).isDir())
    {
        mBuildDataPath = QDir::cleanPath(resolvedDataPath);
        if (!mGameSettings.getDataDirs().contains(mBuildDataPath))
        {
            mGameSettings.setMultiValue(QStringLiteral("data"), mBuildDataPath);
            mGameSettings.addDataDir(mBuildDataPath);
        }
    }

    mBuildName = manifest.buildName.trimmed().isEmpty()
        ? QStringLiteral("ArenaMW") : manifest.buildName.trimmed();

    // A present build.ini owns the enabled plug-in list, including an
    // intentionally empty list. Never merge in installed but unlisted plug-ins.
    const QStringList orderedContent = manifest.contentFiles;
    mGameSettings.setContentList(orderedContent);
    mGameSettings.setGroundcoverList(manifest.groundcoverFiles);

    QStringList profileFiles = orderedContent;
    profileFiles.append(manifest.groundcoverFiles);
    mLauncherSettings.setContentList(mBuildName, profileFiles,
        manifest.groundcoverFiles, !manifest.groundcoverFiles.isEmpty());
    mLauncherSettings.setCurrentContentListName(mBuildName);

    if (!manifest.archives.isEmpty())
    {
        mGameSettings.remove(QStringLiteral("fallback-archive"));
        for (const QString& archive : manifest.archives)
            mGameSettings.setMultiValue(QStringLiteral("fallback-archive"), archive);
    }

    if (manifest.languageSpecified)
    {
        const QString language = Config::BuildManifest::canonicalLanguage(manifest.language);
        mLauncherSettings.remove(QStringLiteral("Settings/language"));
        mLauncherSettings.setValue(QStringLiteral("Settings/language"), language);
        if (language == QLatin1String("Polish"))
            mGameSettings.setValue(QStringLiteral("encoding"), QStringLiteral("win1250"));
        else if (language == QLatin1String("Russian"))
            mGameSettings.setValue(QStringLiteral("encoding"), QStringLiteral("win1251"));
        else
            mGameSettings.setValue(QStringLiteral("encoding"), QStringLiteral("win1252"));
    }

    mBuildManifestPath = manifestPath;
    mBuildComplete = manifest.complete;
    mLauncherSettings.setValue(QStringLiteral("General/Build/name"), mBuildName);

    qDebug() << "Loaded ArenaMW standalone build manifest:" << manifestPath;
    return true;
}

bool Launcher::MainDialog::writeBuildManifest()
{
    QString dataDir = mBuildDataPath;
    if (dataDir.isEmpty() || !QFileInfo(dataDir).isDir())
        dataDir = primaryDataDirectory();
    if (dataDir.isEmpty() || !QFileInfo(dataDir).isDir())
        return true;

    // Always save beside the selected Data Files. A legacy manifest found beside
    // the executable is read once and then migrated to the canonical location.
    const QString sourceManifestPath = mBuildManifestPath;
    const QString manifestPath = Config::BuildManifest::canonicalPathForDataDir(dataDir);

    Config::BuildManifest manifest;
    QString existingManifestPath = manifestPath;
    if (!sourceManifestPath.isEmpty() && QFileInfo::exists(sourceManifestPath))
        existingManifestPath = sourceManifestPath;

    if (QFileInfo::exists(existingManifestPath))
    {
        QString readError;
        if (!manifest.read(existingManifestPath, &readError))
        {
            cfgError(tr("Error reading ArenaMW build manifest"),
                tr("<br><b>Could not read %1</b><br><br>%2")
                    .arg(existingManifestPath, readError));
            return false;
        }
    }

    manifest.formatVersion = 1;
    manifest.buildName = mBuildName.trimmed().isEmpty()
        ? QStringLiteral("ArenaMW") : mBuildName.trimmed();
    manifest.dataPath = Config::BuildManifest::portableDataPath(manifestPath, dataDir);
    manifest.language = Config::BuildManifest::canonicalLanguage(
        mLauncherSettings.value(QStringLiteral("Settings/language"), QStringLiteral("English")));
    manifest.languageSpecified = true;
    manifest.complete = mBuildComplete;
    manifest.contentFiles = mGameSettings.getContentList();
    manifest.groundcoverFiles = mGameSettings.getGroundcoverList();
    manifest.archives = Config::LauncherSettings::reverse(
        mGameSettings.values(QStringLiteral("fallback-archive")));

    QString error;
    if (!manifest.write(manifestPath, &error))
    {
        cfgError(tr("Error writing ArenaMW build manifest"),
            tr("<br><b>Could not write %1</b><br><br>%2")
                .arg(manifestPath, error));
        return false;
    }

    mBuildManifestPath = manifestPath;
    mBuildName = manifest.buildName;
    mBuildDataPath = dataDir;
    mBuildComplete = manifest.complete;
    mLauncherSettings.setValue(QStringLiteral("General/Build/name"), mBuildName);
    return true;
}

void Launcher::MainDialog::applyBuildManifestRestrictions()
{
    // build.ini stores the current user order; keep Data Files available so
    // deliberate Launcher changes can be written back to the manifest.
    if (iconWidget != nullptr && iconWidget->count() > 1)
        iconWidget->item(1)->setHidden(false);

    if (mAdvancedPage != nullptr)
        mAdvancedPage->setGameMechanicsVisible(!mBuildComplete);
}

bool Launcher::MainDialog::setupGraphicsSettings()
{
    // This method is almost a copy of OMW::Engine::loadSettings().  They should definitely
    // remain consistent, and possibly be merged into a shared component.  At the very least
    // the filenames should be in the CfgMgr component.

    // Ensure to clear previous settings in case we had already loaded settings.
    mEngineSettings.clear();

    // Create the settings manager and load default settings file
    const std::string localDefault = (mCfgMgr.getLocalPath() / "defaults.bin").string();
    const std::string globalDefault = (mCfgMgr.getGlobalPath() / "defaults.bin").string();
    std::string defaultPath;

    // Prefer the defaults.bin in the current directory.
    if (boost::filesystem::exists(localDefault))
        defaultPath = localDefault;
    else if (boost::filesystem::exists(globalDefault))
        defaultPath = globalDefault;
    // Something's very wrong if we can't find the file at all.
    else {
        cfgError(tr("Error reading OpenMW configuration file"),
                 tr("<br><b>Could not find defaults.bin</b><br><br> \
                     The problem may be due to an incomplete installation of OpenMW.<br> \
                     Reinstalling OpenMW may resolve the problem."));
        return false;
    }

    // Load the default settings, report any parsing errors.
    try {
        mEngineSettings.loadDefault(defaultPath);
    }
    catch (std::exception& e) {
        std::string msg = std::string("<br><b>Error reading defaults.bin</b><br><br>") + e.what();
        cfgError(tr("Error reading OpenMW configuration file"), tr(msg.c_str()));
        return false;
    }

    // Load user settings if they exist
    const std::string userPath = (mCfgMgr.getUserConfigPath() / "settings.cfg").string();
    // User settings are not required to exist, so if they don't we're done.
    if (!boost::filesystem::exists(userPath)) return true;

    try {
        mEngineSettings.loadUser(userPath);
    }
    catch (std::exception& e) {
        std::string msg = std::string("<br><b>Error reading settings.cfg</b><br><br>") + e.what();
        cfgError(tr("Error reading OpenMW configuration file"), tr(msg.c_str()));
        return false;
    }

    return true;
}

void Launcher::MainDialog::loadSettings()
{
    int posX = mLauncherSettings.value(QString("General/MainWindow/posx")).toInt();
    int posY = mLauncherSettings.value(QString("General/MainWindow/posy")).toInt();

    // Match ArenaMP's launcher geometry and keep the page layouts stable.
    // Old launcher.cfg width/height values are deliberately ignored.
    setFixedSize(sLauncherWidth, sLauncherHeight);
    move(posX, posY);
}

void Launcher::MainDialog::saveSettings()
{
    QString width = QString::number(sLauncherWidth);
    QString height = QString::number(sLauncherHeight);

    mLauncherSettings.remove(QString("General/MainWindow/width"));
    mLauncherSettings.remove(QString("General/MainWindow/height"));
    mLauncherSettings.setValue(QString("General/MainWindow/width"), width);
    mLauncherSettings.setValue(QString("General/MainWindow/height"), height);

    QString posX = QString::number(this->pos().x());
    QString posY = QString::number(this->pos().y());

    mLauncherSettings.setValue(QString("General/MainWindow/posx"), posX);
    mLauncherSettings.setValue(QString("General/MainWindow/posy"), posY);

    mLauncherSettings.setValue(QString("General/firstrun"), QString("false"));

}

bool Launcher::MainDialog::writeSettings()
{
    // Now write all config files
    saveSettings();
    mDataFilesPage->saveSettings();
    mGraphicsPage->saveSettings();
    mSettingsPage->saveSettings();
    mAdvancedPage->saveSettings();

    QString userPath = QString::fromUtf8(mCfgMgr.getUserConfigPath().string().c_str());
    QDir dir(userPath);

    if (!dir.exists()) {
        if (!dir.mkpath(userPath)) {
            cfgError(tr("Error creating OpenMW configuration directory"),
                     tr("<br><b>Could not create %0</b><br><br> \
                         Please make sure you have the right permissions \
                         and try again.<br>").arg(userPath));
            return false;
        }
    }

    // Game settings
    QFile file(dir.filePath(QStringLiteral("openmw.cfg")));

    if (!file.open(QIODevice::ReadWrite | QIODevice::Text)) {
        // File cannot be opened or created
        cfgError(tr("Error writing OpenMW configuration file"),
                 tr("<br><b>Could not open or create %0 for writing</b><br><br> \
                     Please make sure you have the right permissions \
                     and try again.<br>").arg(file.fileName()));
        return false;
    }


    mGameSettings.writeFileWithComments(file);
    file.close();

    // Graphics settings
    const std::string settingsPath = (mCfgMgr.getUserConfigPath() / "settings.cfg").string();
    try {
        mEngineSettings.saveUser(settingsPath);
    }
    catch (std::exception& e) {
        std::string msg = "<br><b>Error writing settings.cfg</b><br><br>" +
            settingsPath + "<br><br>" + e.what();
        cfgError(tr("Error writing user settings file"), tr(msg.c_str()));
        return false;
    }

    // Launcher settings
    mLauncherSettings.setValue(QStringLiteral("General/Build/name"),
        mBuildName.trimmed().isEmpty() ? QStringLiteral("ArenaMW") : mBuildName.trimmed());
    file.setFileName(dir.filePath(QString(Config::LauncherSettings::sLauncherConfigFileName)));

    if (!file.open(QIODevice::ReadWrite | QIODevice::Text | QIODevice::Truncate)) {
        // File cannot be opened or created
        cfgError(tr("Error writing Launcher configuration file"),
                 tr("<br><b>Could not open or create %0 for writing</b><br><br> \
                     Please make sure you have the right permissions \
                     and try again.<br>").arg(file.fileName()));
        return false;
    }

    QTextStream stream(&file);
    stream.setDevice(&file);
    stream.setCodec(QTextCodec::codecForName("UTF-8"));

    mLauncherSettings.writeFile(stream);
    file.close();

    if (!writeBuildManifest())
        return false;

    return true;
}

void Launcher::MainDialog::closeEvent(QCloseEvent *event)
{
    writeSettings();
    event->accept();
}

void Launcher::MainDialog::wizardStarted()
{
    hide();
}

void Launcher::MainDialog::wizardFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (exitCode != 0 || exitStatus == QProcess::CrashExit)
        return qApp->quit();

    // The Wizard has just replaced openmw.cfg and launcher.cfg. Reload those
    // files before validating the selected Data Files directory.
    if (!setup() || !reloadSettings())
        return qApp->quit();

    if (setupGameData())
    {
        show();
        raise();
        activateWindow();
    }
}

void Launcher::MainDialog::play()
{
    if (!writeSettings())
        return qApp->quit();

    if (!mGameSettings.hasMaster()) {
        QMessageBox msgBox;
        msgBox.setWindowTitle(tr("No game file selected"));
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setText(tr("<br><b>You do not have a game file selected.</b><br><br> \
                          OpenMW will not start without a game file selected.<br>"));
                          msgBox.exec();
        return;
    }

    // Launch the game detached

    if (mGameInvoker->startProcess(QLatin1String("openmw"), true))
        return qApp->quit();
}

void Launcher::MainDialog::help()
{
    Misc::HelpViewer::openHelp("reference/index.html");
}

/* === This file is part of Calamares - <https://github.com/calamares> ===
 *
 *   Copyright 2014-2015, Teo Mrnjavac <teo@kde.org>
 *   Copyright 2018, Adriaan de Groot <groot@kde.org>
 *
 *   Calamares is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   Calamares is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Calamares. If not, see <http://www.gnu.org/licenses/>.
 */
#include <QDesktopWidget>
#include "CalamaresApplication.h"

#include "CalamaresConfig.h"
#include "CalamaresWindow.h"
#include "CalamaresVersion.h"
#include "progresstree/ProgressTreeView.h"
#include "progresstree/ProgressTreeModel.h"

#include "modulesystem/ModuleManager.h"
#include "utils/CalamaresUtilsGui.h"
#include "utils/CalamaresUtilsSystem.h"
#include "utils/Logger.h"
#include "JobQueue.h"
#include "Branding.h"
#include "Settings.h"
#include "viewpages/ViewStep.h"
#include "ViewManager.h"

#include <QDir>
#include <QFileInfo>


CalamaresApplication::CalamaresApplication( int& argc, char* argv[] )
    : QApplication( argc, argv )
    , m_mainwindow( nullptr )
    , m_moduleManager( nullptr )
    , m_debugMode( false )
{
    // Setting the organization name makes the default cache
    // directory -- where Calamares stores logs, for instance --
    // <org>/<app>/, so we end up with ~/.cache/Calamares/calamares/
    // which is excessively squidly.
    //
    // setOrganizationName( QStringLiteral( CALAMARES_ORGANIZATION_NAME ) );
    setOrganizationDomain( QStringLiteral( CALAMARES_ORGANIZATION_DOMAIN ) );
    setApplicationName( QStringLiteral( CALAMARES_APPLICATION_NAME ) );
    setApplicationVersion( QStringLiteral( CALAMARES_VERSION ) );

    CalamaresUtils::installTranslator( QLocale::system(), QString(), this );

    QFont f = font();
    CalamaresUtils::setDefaultFontSize( f.pointSize() );
}


void
CalamaresApplication::init()
{
    Logger::setupLogfile();
    cDebug() << "Calamares version:" << CALAMARES_VERSION;
    cDebug() << "        languages:" << QString( CALAMARES_TRANSLATION_LANGUAGES ).replace( ";", ", " );

    setQuitOnLastWindowClosed( false );

    initQmlPath();
    initSettings();
    initBranding();

    setWindowIcon( QIcon( Calamares::Branding::instance()->
                          imagePath( Calamares::Branding::ProductIcon ) ) );

    cDebug() << "STARTUP: initQmlPath, initSettings, initBranding done";

    initModuleManager(); //also shows main window

    cDebug() << "STARTUP: initModuleManager: module init started";
}


CalamaresApplication::~CalamaresApplication()
{
    cDebug( Logger::LOGVERBOSE ) << "Shutting down Calamares...";

//    if ( JobQueue::instance() )
//        JobQueue::instance()->stop();

//    delete m_mainwindow;

//    delete JobQueue::instance();

    cDebug( Logger::LOGVERBOSE ) << "Finished shutdown.";
}


CalamaresApplication*
CalamaresApplication::instance()
{
    return qobject_cast< CalamaresApplication* >( QApplication::instance() );
}


void
CalamaresApplication::setDebug( bool enabled )
{
    m_debugMode = enabled;
}


bool
CalamaresApplication::isDebug()
{
    return m_debugMode;
}


CalamaresWindow*
CalamaresApplication::mainWindow()
{
    return m_mainwindow;
}


static QStringList
qmlDirCandidates( bool assumeBuilddir )
{
    static const char QML[] = "qml";

    QStringList qmlDirs;
    if ( CalamaresUtils::isAppDataDirOverridden() )
        qmlDirs << CalamaresUtils::appDataDir().absoluteFilePath( QML );
    else
    {
        if ( assumeBuilddir )
            qmlDirs << QDir::current().absoluteFilePath( "src/qml" );  // In build-dir
        if ( CalamaresUtils::haveExtraDirs() )
            for ( auto s : CalamaresUtils::extraDataDirs() )
                qmlDirs << ( s + QML );
        qmlDirs << CalamaresUtils::appDataDir().absoluteFilePath( QML );
    }

    return qmlDirs;
}


static QStringList
settingsFileCandidates( bool assumeBuilddir )
{
    static const char settings[] = "settings.conf";

    QStringList settingsPaths;
    if ( CalamaresUtils::isAppDataDirOverridden() )
        settingsPaths << CalamaresUtils::appDataDir().absoluteFilePath( settings );
    else
    {
        if ( assumeBuilddir )
            settingsPaths << QDir::current().absoluteFilePath( settings );
        if ( CalamaresUtils::haveExtraDirs() )
            for ( auto s : CalamaresUtils::extraConfigDirs() )
                settingsPaths << ( s + settings );
        settingsPaths << CMAKE_INSTALL_FULL_SYSCONFDIR "/calamares/settings.conf";  // String concat
        settingsPaths << CalamaresUtils::appDataDir().absoluteFilePath( settings );
    }

    return settingsPaths;
}


static QStringList
brandingFileCandidates( bool assumeBuilddir, const QString& brandingFilename )
{
    QStringList brandingPaths;
    if ( CalamaresUtils::isAppDataDirOverridden() )
        brandingPaths << CalamaresUtils::appDataDir().absoluteFilePath( brandingFilename );
    else
    {
        if ( assumeBuilddir )
            brandingPaths << ( QDir::currentPath() + QStringLiteral( "/src/" ) + brandingFilename );
        if ( CalamaresUtils::haveExtraDirs() )
            for ( auto s : CalamaresUtils::extraDataDirs() )
                brandingPaths << ( s + brandingFilename );
        brandingPaths << QDir( CMAKE_INSTALL_FULL_SYSCONFDIR "/calamares/" ).absoluteFilePath( brandingFilename );
        brandingPaths << CalamaresUtils::appDataDir().absoluteFilePath( brandingFilename);
    }

    return brandingPaths;
}


void
CalamaresApplication::initQmlPath()
{
    QDir importPath;  // Right now, current-dir
    QStringList qmlDirCandidatesByPriority = qmlDirCandidates( isDebug() );
    bool found = false;

    foreach ( const QString& path, qmlDirCandidatesByPriority )
    {
        QDir dir( path );
        if ( dir.exists() && dir.isReadable() )
        {
            importPath = dir;
            found = true;
            break;
        }
    }

    if ( !found || !importPath.exists() || !importPath.isReadable() )
    {
        cError() << "Cowardly refusing to continue startup without a QML directory."
            << Logger::DebugList( qmlDirCandidatesByPriority );
        if ( CalamaresUtils::isAppDataDirOverridden() )
            cError() << "FATAL: explicitly configured application data directory is missing qml/";
        else
            cError() << "FATAL: none of the expected QML paths exist.";
        ::exit( EXIT_FAILURE );
    }

    cDebug() << "Using Calamares QML directory" << importPath.absolutePath();
    CalamaresUtils::setQmlModulesDir( importPath );
}


void
CalamaresApplication::initSettings()
{
    QStringList settingsFileCandidatesByPriority = settingsFileCandidates( isDebug() );

    QFileInfo settingsFile;
    bool found = false;

    foreach ( const QString& path, settingsFileCandidatesByPriority )
    {
        QFileInfo pathFi( path );
        if ( pathFi.exists() && pathFi.isReadable() )
        {
            settingsFile = pathFi;
            found = true;
            break;
        }
    }

    if ( !found || !settingsFile.exists() || !settingsFile.isReadable() )
    {
        cError() << "Cowardly refusing to continue startup without settings."
            << Logger::DebugList( settingsFileCandidatesByPriority );
        if ( CalamaresUtils::isAppDataDirOverridden() )
            cError() << "FATAL: explicitly configured application data directory is missing settings.conf";
        else
            cError() << "FATAL: none of the expected configuration file paths exist.";
        ::exit( EXIT_FAILURE );
    }

    auto* settings = new Calamares::Settings( settingsFile.absoluteFilePath(), isDebug(), this );  // Creates singleton
    if ( settings->modulesSequence().count() < 1 )
    {
        cError() << "FATAL: no sequence set.";
        ::exit( EXIT_FAILURE );
    }
}


void
CalamaresApplication::initBranding()
{
    QString brandingComponentName = Calamares::Settings::instance()->brandingComponentName();
    if ( brandingComponentName.simplified().isEmpty() )
    {
        cError() << "FATAL: branding component not set in settings.conf";
        ::exit( EXIT_FAILURE );
    }

    QString brandingDescriptorSubpath = QString( "branding/%1/branding.desc" ).arg( brandingComponentName );
    QStringList brandingFileCandidatesByPriority = brandingFileCandidates( isDebug(), brandingDescriptorSubpath);

    QFileInfo brandingFile;
    bool found = false;

    foreach ( const QString& path, brandingFileCandidatesByPriority )
    {
        QFileInfo pathFi( path );
        if ( pathFi.exists() && pathFi.isReadable() )
        {
            brandingFile = pathFi;
            found = true;
            break;
        }
    }

    if ( !found || !brandingFile.exists() || !brandingFile.isReadable() )
    {
        cError() << "Cowardly refusing to continue startup without branding."
            << Logger::DebugList( brandingFileCandidatesByPriority );
        if ( CalamaresUtils::isAppDataDirOverridden() )
            cError() << "FATAL: explicitly configured application data directory is missing" << brandingComponentName;
        else
            cError() << "FATAL: none of the expected branding descriptor file paths exist.";
        ::exit( EXIT_FAILURE );
    }

    new Calamares::Branding( brandingFile.absoluteFilePath(), this );
}


void
CalamaresApplication::initModuleManager()
{
    m_moduleManager = new Calamares::ModuleManager(
        Calamares::Settings::instance()->modulesSearchPaths(), this );
    connect( m_moduleManager, &Calamares::ModuleManager::initDone,
             this,            &CalamaresApplication::initView );
    m_moduleManager->init();
}


void
CalamaresApplication::initView()
{
    cDebug() << "STARTUP: initModuleManager: all modules init done";
    initJobQueue();
    cDebug() << "STARTUP: initJobQueue done";

    m_mainwindow = new CalamaresWindow(); //also creates ViewManager

    connect( m_moduleManager, &Calamares::ModuleManager::modulesLoaded,
             this, &CalamaresApplication::initViewSteps );
    connect( m_moduleManager, &Calamares::ModuleManager::modulesFailed,
             this, &CalamaresApplication::initFailed );

    m_moduleManager->loadModules();

    m_mainwindow->move(
        this->desktop()->availableGeometry().center() -
        m_mainwindow->rect().center() );

    cDebug() << "STARTUP: CalamaresWindow created; loadModules started";
}


void
CalamaresApplication::initViewSteps()
{
    cDebug() << "STARTUP: loadModules for all modules done";
    m_moduleManager->checkRequirements();
    if ( Calamares::Branding::instance()->windowMaximize() )
    {
        m_mainwindow->setWindowFlag( Qt::FramelessWindowHint );
        m_mainwindow->showMaximized();
    }
    else
        m_mainwindow->show();

    ProgressTreeModel* m = new ProgressTreeModel( nullptr );
    ProgressTreeView::instance()->setModel( m );
    cDebug() << "STARTUP: Window now visible and ProgressTreeView populated";
}

void
CalamaresApplication::initFailed(const QStringList& l)
{
    cError() << "STARTUP: failed modules are" << l;
    m_mainwindow->show();
}

void
CalamaresApplication::initJobQueue()
{
    Calamares::JobQueue* jobQueue = new Calamares::JobQueue( this );
    new CalamaresUtils::System( Calamares::Settings::instance()->doChroot(), this );
    Calamares::Branding::instance()->setGlobals( jobQueue->globalStorage() );
}

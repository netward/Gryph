#include <csignal>

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QTranslator>
#include <QMessageBox>
#include <QStandardPaths>
#include <QLocalSocket>
#include <QLocalServer>
#include <QThread>
#include <3rdparty/WinCommander.hpp>

#include "include/global/TaskExecutor.hpp"
#include "include/global/Configs.hpp"
#include "include/ui/MainWindowAPI.h"
#include "include/api/RPC.h"
#include "include/stats/traffic/TrafficLooper.hpp"
#include "include/stats/connections/ConnectionLister.hpp"

#ifdef Q_OS_WIN
#include "include/sys/windows/MiniDump.h"
#include "include/sys/windows/eventHandler.h"
#include "include/sys/windows/WinVersion.h"
#include <qfontdatabase.h>
#endif

#ifdef Q_OS_LINUX
#include <include/sys/linux/coreDump.h>
#include <qfontdatabase.h>
#endif

#ifdef Q_OS_MACOS
#include <QFileOpenEvent>

class MacDeeplinkFilter : public QObject 
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *obj, QEvent *event) override 
    {
        if (event->type() == QEvent::FileOpen) 
        {
            const QString url = static_cast<QFileOpenEvent *>(event)->url().toString();
            if (url.startsWith("Gryph://")) 
            {
                Deeplink_Submit(url);
                return true;
            }
        }
        return QObject::eventFilter(obj, event);
    }
};
#endif

void signal_handler(int signum)
{
    Q_UNUSED(signum);
    MainWindowApi::PrepareExit();
    qApp->quit();
}

QTranslator* trans = nullptr;
QTranslator* trans_qt = nullptr;

void loadTranslate(const QString& locale) {
    QT_TRANSLATE_NOOP("QPlatformTheme", "Cancel");
    QT_TRANSLATE_NOOP("QPlatformTheme", "Apply");
    QT_TRANSLATE_NOOP("QPlatformTheme", "Yes");
    QT_TRANSLATE_NOOP("QPlatformTheme", "No");
    QT_TRANSLATE_NOOP("QPlatformTheme", "OK");
    
    if (trans != nullptr) 
    {
        trans->deleteLater();
    }

    if (trans_qt != nullptr) 
    {
        trans_qt->deleteLater();
    }
    
    trans = new QTranslator;
    trans_qt = new QTranslator;
    QLocale::setDefault(QLocale(locale));
    
    if (trans->load(":/translations/" + locale + ".qm")) 
    {
        QCoreApplication::installTranslator(trans);
    }
}

#define LOCAL_SERVER_PREFIX "Gryph-"

int main(int argc, char* argv[]) 
{
#ifdef Q_OS_WIN
    Windows_SetCrashHandler();
#endif

#ifdef Q_OS_LINUX
    enable_core_dumps();
#endif

    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication::setQuitOnLastWindowClosed(false);
    QApplication a(argc, argv);

#ifdef Q_OS_MACOS
    a.installEventFilter(new MacDeeplinkFilter(&a));
#endif

#if !defined(Q_OS_MACOS) && (QT_VERSION >= QT_VERSION_CHECK(6,9,0))
#ifdef Q_OS_WIN
    int fontId = QFontDatabase::addApplicationFont(WinVersion::IsBuildNumGreaterOrEqual(BuildNumber::Windows_11_22H2) ? ":/font/notoEmoji" : ":/font/Twemoji");
#else
    int fontId = QFontDatabase::addApplicationFont(":/font/notoEmoji");
#endif
    if (fontId >= 0)
    {
        QStringList fontFamilies = QFontDatabase::applicationFontFamilies(fontId);
        QFontDatabase::setApplicationEmojiFontFamilies(fontFamilies);
    } 
    else
    {
        qDebug() << "could not load emoji font!";
    }
#endif

    QDir::setCurrent(QApplication::applicationDirPath());
    if (QFile::exists("updater.old")) 
    {
        QFile::remove("updater.old");
    }

    QStringList arguments = QApplication::arguments();
    const QString launchDeeplink = Deeplink_ExtractFromArgs(arguments);
    auto wd = QDir(QApplication::applicationDirPath());
    bool useAppdata = false;
    QString appdataDir;

    if (arguments.contains("-appdata")) 
    {
        useAppdata = true;
        int appdataIndex = arguments.indexOf("-appdata");

        if (arguments.size() > appdataIndex + 1 && !arguments.at(appdataIndex + 1).startsWith("-")) 
        {
            appdataDir = arguments.at(appdataIndex + 1);
        }
    }

#ifdef NKR_CPP_USE_APPDATA
    useAppdata = true;
#endif

    if(useAppdata) 
    {
        QApplication::setApplicationName("Gryph");
        if (!appdataDir.isEmpty()) 
        {
            wd.setPath(appdataDir);
        } 
        else 
        {
            wd.setPath(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
        }
    }

    if (!wd.exists())
    {
        wd.mkpath(wd.absolutePath());
    }

    if (!wd.exists("config")) 
    { 
        wd.mkdir("config"); 
    }

    QDir::setCurrent(wd.absoluteFilePath("config"));
    QDir("temp").removeRecursively();
    Configs::initDB(QString(QDir::currentPath() + QDir::separator() + "Gryph.db").toStdString());
    Configs::dataManager->settingsRepo->argv = arguments;

    if (Configs::dataManager->settingsRepo->argv.contains("-many"))
    {
        Configs::dataManager->settingsRepo->flag_many = true;
    }

    if (Configs::dataManager->settingsRepo->argv.contains("-tray"))
    {
        Configs::dataManager->settingsRepo->flag_tray = true;
    }

    if (Configs::dataManager->settingsRepo->argv.contains("-debug"))
    {
        Configs::dataManager->settingsRepo->flag_debug = true;
    }

    if (Configs::dataManager->settingsRepo->argv.contains("-flag_restart_tun_on"))
    {
        Configs::dataManager->settingsRepo->flag_restart_tun_on = true;
    }

    if (Configs::dataManager->settingsRepo->argv.contains("-flag_restart_dns_set")) 
    {
        Configs::dataManager->settingsRepo->flag_dns_set = true;
    }

    Configs::dataManager->settingsRepo->flag_use_appdata = useAppdata;

    if(useAppdata && !appdataDir.isEmpty()) 
    {
        Configs::dataManager->settingsRepo->appdataDir = appdataDir;
    }

#ifdef NKR_CPP_DEBUG
    Configs::dataManager->settingsRepo->flag_debug = true;
#endif

#ifdef Q_OS_LINUX
    QApplication::addLibraryPath(QApplication::applicationDirPath() + "/usr/plugins");
#endif

    DS_cores = new QThread;
    DS_cores->start();
    LogThread = new QThread;
    LogThread->start();
    QIcon::setFallbackSearchPaths(QStringList{
        ":/icon",
    });

    if (QIcon::themeName().isEmpty()) 
    {
        QIcon::setThemeName("breeze");
    }

#ifdef Q_OS_WIN

    if (Configs::dataManager->settingsRepo->windows_set_admin && !Configs::IsAdmin() && !Configs::dataManager->settingsRepo->disable_run_admin)
    {
        Configs::dataManager->settingsRepo->windows_set_admin = false;
        Configs::dataManager->settingsRepo->Save();
        WinCommander::runProcessElevated(QApplication::applicationFilePath(), {}, "", 1, false);
        QApplication::quit();
        return 0;
    }

#endif

    if (Configs::dataManager->settingsRepo->start_minimal)
    {
        Configs::dataManager->settingsRepo->flag_tray = true;
    }

    QString locale;
    switch (Configs::dataManager->settingsRepo->language) 
    {
        case 1: 
            break;
        case 2:
            locale = "zh_CN";
            break;
        case 3:
            locale = "fa_IR";
            break;
        case 4:
            locale = "ru_RU";
            break;
        default:
            locale = QLocale().name();
    }

    QGuiApplication::tr("QT_LAYOUT_DIRECTION");
    loadTranslate(locale);
    QByteArray hashBytes = QCryptographicHash::hash(wd.absolutePath().toUtf8(), QCryptographicHash::Md5).toBase64(QByteArray::OmitTrailingEquals);
    hashBytes.replace('+', '0').replace('/', '1');
    auto serverName = LOCAL_SERVER_PREFIX + QString::fromUtf8(hashBytes);
    qDebug() << "server name: " << serverName;
    QLocalSocket socket;
    socket.connectToServer(serverName);

    if (socket.waitForConnected(250))
    {
        qDebug() << "Another instance is running, let's wake it up and quit";
        
        if (!launchDeeplink.isEmpty()) 
        {
            socket.write(launchDeeplink.toUtf8());
            socket.flush();
            socket.waitForBytesWritten(250);
        }

        socket.disconnectFromServer();
        return 0;
    }

    QLocalServer server(qApp);
    server.setSocketOptions(QLocalServer::WorldAccessOption);
    
    if (!server.listen(serverName)) 
    {
        qWarning() << "Failed to start QLocalServer! Error:" << server.errorString();
        return 1;
    }

    QObject::connect(&server, &QLocalServer::newConnection, qApp, [&] {
        auto s = server.nextPendingConnection();
        qDebug() << "Another instance tried to wake us up on " << serverName << s;

        auto readPayload = [s] {

            if (s->bytesAvailable() <= 0)
            {
                return;
            }
            Deeplink_Submit(QString::fromUtf8(s->readAll()).trimmed());
        };

        QObject::connect(s, &QLocalSocket::readyRead, s, readPayload);
        QObject::connect(s, &QLocalSocket::disconnected, s, &QLocalSocket::deleteLater);
        readPayload();
        MW_dialog_message(MwMessage::Raise, {});
    });

    QObject::connect(qApp, &QApplication::aboutToQuit, [&]
    {
        server.close();
        QLocalServer::removeServer(serverName);
    });

#ifdef Q_OS_LINUX

    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);

#endif

#ifdef Q_OS_WIN

    auto eventFilter = new PowerOffTaskkillFilter(signal_handler);
    a.installNativeEventFilter(eventFilter);

#endif

#ifdef Q_OS_MACOS

    QObject::connect(qApp, &QGuiApplication::commitDataRequest, [&](QSessionManager &manager)
    {
        Q_UNUSED(manager);
        signal_handler(0);
    });

#endif

    API::defaultClient = new API::Client();
    MainWindowApi::Initialize();
    if (!launchDeeplink.isEmpty())
    {
        Deeplink_Submit(launchDeeplink);
    }
    
    Deeplink_FlushPending();
    const int exitCode = QApplication::exec();
    Stats::trafficLooper->RequestStop();
    Stats::connection_lister->stopLoop();
    Async::shutdown();
    // All RPC users must be stopped before this point.
    delete API::defaultClient;
    API::defaultClient = nullptr;
    qInfo() << "[RPC] Client destroyed";
    return exitCode;
}

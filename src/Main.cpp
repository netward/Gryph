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

class MacDeeplinkFilter : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *obj, QEvent *event) override {
        if (event->type() == QEvent::FileOpen) {
            const QString url = static_cast<QFileOpenEvent *>(event)->url().toString();
            if (url.startsWith("Gryph://")) {
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
    
    if (trans != nullptr) {
        trans->deleteLater();
    }
    if (trans_qt != nullptr) {
        trans_qt->deleteLater();
    }
    
    trans = new QTranslator;
    trans_qt = new QTranslator;

    QLocale::setDefault(QLocale(locale));
    
    if (trans->load(":/translations/" + locale + ".qm")) {
        QCoreApplication::installTranslator(trans);
    }
}

#define LOCAL_SERVER_PREFIX "Gryph-"

int main(int argc, char* argv[]) {
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
    } else
    {
        qDebug() << "could not load emoji font!";
    }
#endif

    // 4. Начальный рабочий каталог и очистка. 
    //    Сначала рабочим каталогом становится папка с исполняемым файлом, поэтому updater.old ищется рядом с Gryph.exe.
    QDir::setCurrent(QApplication::applicationDirPath());
    if (QFile::exists("updater.old")) {
        QFile::remove("updater.old");
    }

    // QApplication::arguments() возвращает путь к приложению и все аргументы.
    QStringList arguments = QApplication::arguments();
    
    //В Windows и Linux ссылка Gryph:// может прийти через argv.
    // Пока только извлекаем её.
    // Обработка произойдёт после создания главного окна, либо ссылка будет передана уже работающему экземпляру приложения.
    const QString launchDeeplink = Deeplink_ExtractFromArgs(arguments);

    // 5. Выбор каталога данных.
    //    По умолчанию данные хранятся рядом с приложением.
    // Варианты:
    //  Gryph.exe -appdata
    //  Gryph.exe -appdata "D:/GryphData"
    auto wd = QDir(QApplication::applicationDirPath());
    bool useAppdata = false;
    QString appdataDir;
    if (arguments.contains("-appdata")) {
        useAppdata = true;
        int appdataIndex = arguments.indexOf("-appdata");
        // Аргумент после -appdata считается путём, если он существует и не начинается с дефиса.
        if (arguments.size() > appdataIndex + 1 && !arguments.at(appdataIndex + 1).startsWith("-")) {
            appdataDir = arguments.at(appdataIndex + 1);
        }
    }
#ifdef NKR_CPP_USE_APPDATA
    // Для пакетных сборок режим AppData включается при компиляции.
    useAppdata = true;
#endif
    if(useAppdata) {
        // Имя участвует в формировании стандартного пути AppConfigLocation.
        QApplication::setApplicationName("Gryph");
        if (!appdataDir.isEmpty()) {
            // Используем путь, явно переданный пользователем.
            wd.setPath(appdataDir);
        } else {
            // Используем стандартный системный каталог конфигурации.
            wd.setPath(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
        }
    }
    // Создаём основной каталог данных.
    if (!wd.exists()) wd.mkpath(wd.absolutePath());
    // Создаём подпапку config.
    if (!wd.exists("config")) wd.mkdir("config");
    // После этой команды все относительные пути будут отсчитываться от каталога config.
    QDir::setCurrent(wd.absoluteFilePath("config"));
    // Удаляем временные файлы предыдущего запуска.
    QDir("temp").removeRecursively();

    // 6. Инициализация базы данных.
    //    Gryph.db создаётся или открывается в каталоге config.
    Configs::initDB(QString(QDir::currentPath() + QDir::separator() + "Gryph.db").toStdString());

    // 7. Аргументы и флаги запуска.
    //    Полный список argv сохраняется в SettingsRepo.
    Configs::dataManager->settingsRepo->argv = arguments;
    if (Configs::dataManager->settingsRepo->argv.contains("-many")) Configs::dataManager->settingsRepo->flag_many = true;
    if (Configs::dataManager->settingsRepo->argv.contains("-tray")) Configs::dataManager->settingsRepo->flag_tray = true;
    if (Configs::dataManager->settingsRepo->argv.contains("-debug")) Configs::dataManager->settingsRepo->flag_debug = true;
    if (Configs::dataManager->settingsRepo->argv.contains("-flag_restart_tun_on")) Configs::dataManager->settingsRepo->flag_restart_tun_on = true;
    if (Configs::dataManager->settingsRepo->argv.contains("-flag_restart_dns_set")) Configs::dataManager->settingsRepo->flag_dns_set = true;
    Configs::dataManager->settingsRepo->flag_use_appdata = useAppdata;
    if(useAppdata && !appdataDir.isEmpty()) Configs::dataManager->settingsRepo->appdataDir = appdataDir;

#ifdef NKR_CPP_DEBUG
    // Сборки Debug всегда включают внутренний режим отладки.
    Configs::dataManager->settingsRepo->flag_debug = true;
#endif

#ifdef Q_OS_LINUX
    // Добавляем каталог с поставляемыми вместе с Gryph Qt-плагинами.
    QApplication::addLibraryPath(QApplication::applicationDirPath() + "/usr/plugins");
#endif

    // 8. Рабочие потоки.
    //    DS_cores обслуживает операции с ядрами прокси. LogThread обслуживает работу с журналами.
    DS_cores = new QThread;
    DS_cores->start();

    LogThread = new QThread;
    LogThread->start();

    // 9. Настройка иконок.
    //    Qt будет искать резервные иконки во встроенном ресурсе :/icon.
    QIcon::setFallbackSearchPaths(QStringList{
        ":/icon",
    });

    // При отсутствии системной темы используем Breeze.
    if (QIcon::themeName().isEmpty()) {
        QIcon::setThemeName("breeze");
    }

#ifdef Q_OS_WIN
    // 10. Перезапуск с правами администратора.
    //     При необходимости запускаем повышенную копию через UAC и завершаем текущий процесс.
    if (Configs::dataManager->settingsRepo->windows_set_admin && !Configs::IsAdmin() && !Configs::dataManager->settingsRepo->disable_run_admin)
    {
        // Сбрасываем настройку заранее.
        // Если пользователь отклонит запрос UAC, следующий запуск не попадёт в бесконечный цикл повышения прав.
        Configs::dataManager->settingsRepo->windows_set_admin = false;
        Configs::dataManager->settingsRepo->Save();
        WinCommander::runProcessElevated(QApplication::applicationFilePath(), {}, "", 1, false);
        QApplication::quit();
        return 0;
    }
#endif

    // Минимальный режим подразумевает запуск в системном трее.
    if (Configs::dataManager->settingsRepo->start_minimal) Configs::dataManager->settingsRepo->flag_tray = true;

    // 11. Локализация.
    // Значение language:
    //  1 — английский;
    //  2 — китайский;
    //  3 — персидский;
    //  4 — русский;
    //  другое значение — системный язык.
    QString locale;
    switch (Configs::dataManager->settingsRepo->language) {
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

    // 12. Проверка второго экземпляра.
    //     Имя IPC-сервера зависит от абсолютного пути каталога данных.
    //     Поэтому:
    //      - экземпляры с разными -appdata каталогами могут работать параллельно;
    //      - экземпляры с одним каталогом данных не могут.
    //     MD5 здесь используется только для получения короткого стабильного имени, а не для обеспечения безопасности.
    QByteArray hashBytes = QCryptographicHash::hash(wd.absolutePath().toUtf8(), QCryptographicHash::Md5).toBase64(QByteArray::OmitTrailingEquals);
    // Символы + и / могут быть неудобны в имени локального сервера.
    hashBytes.replace('+', '0').replace('/', '1');
    auto serverName = LOCAL_SERVER_PREFIX + QString::fromUtf8(hashBytes);
    qDebug() << "server name: " << serverName;
    // Сначала пытаемся подключиться к серверу уже запущенного экземпляра.
    QLocalSocket socket;
    socket.connectToServer(serverName);
    if (socket.waitForConnected(250))
    {
        qDebug() << "Another instance is running, let's wake it up and quit";
        // Если приложение запущено через Gryph://, передаём ссылку основному экземпляру.
        if (!launchDeeplink.isEmpty()) {
            socket.write(launchDeeplink.toUtf8());
            socket.flush();
            socket.waitForBytesWritten(250);
        }
        socket.disconnectFromServer();
        return 0;
    }

    // Подключиться не удалось.
    // Этот процесс становится главным экземпляром и создаёт IPC-сервер.
    QLocalServer server(qApp);
    // Разрешает подключение другим локальным пользователям ОС.
    server.setSocketOptions(QLocalServer::WorldAccessOption);
    if (!server.listen(serverName)) {
        qWarning() << "Failed to start QLocalServer! Error:" << server.errorString();
        return 1;
    }
    // Срабатывает, когда другой экземпляр подключается к основному.
    QObject::connect(&server, &QLocalServer::newConnection, qApp, [&] {
        auto s = server.nextPendingConnection();
        qDebug() << "Another instance tried to wake us up on " << serverName << s;
        // Второй экземпляр может передать ссылку Gryph:// как содержимое локального сокета.
        auto readPayload = [s] {
            if (s->bytesAvailable() <= 0) return;
            Deeplink_Submit(QString::fromUtf8(s->readAll()).trimmed());
        };
        QObject::connect(s, &QLocalSocket::readyRead, s, readPayload);
        QObject::connect(s, &QLocalSocket::disconnected, s, &QLocalSocket::deleteLater);
        // Данные могли прийти до подключения readyRead.
        readPayload();
        // Показываем и поднимаем главное окно.
        MW_dialog_message(MwMessage::Raise, {});
    });
    // При штатном завершении закрываем сервер и удаляем его системное имя.
    QObject::connect(qApp, &QApplication::aboutToQuit, [&]
    {
        server.close();
        QLocalServer::removeServer(serverName);
    });

    // 13. Обработчики завершения от операционной системы.
#ifdef Q_OS_LINUX
    // SIGTERM обычно приходит от kill/systemd.
    signal(SIGTERM, signal_handler);
    // SIGINT обычно приходит после Ctrl+C.
    signal(SIGINT, signal_handler);
#endif

#ifdef Q_OS_WIN
    // Перехватываем выключение Windows и завершение пользовательской сессии.
    auto eventFilter = new PowerOffTaskkillFilter(signal_handler);
    a.installNativeEventFilter(eventFilter);
#endif

#ifdef Q_OS_MACOS
    // macOS просит приложение сохранить данные перед завершением сессии.
    QObject::connect(qApp, &QGuiApplication::commitDataRequest, [&](QSessionManager &manager)
    {
        Q_UNUSED(manager);
        signal_handler(0);
    });
#endif

    // Create the long-lived client before MainWindow can launch any asynchronous RPC users.
    API::defaultClient = new API::Client();
    
    MainWindowApi::Initialize();

    if (!launchDeeplink.isEmpty()) Deeplink_Submit(launchDeeplink);
    Deeplink_FlushPending();

    const int exitCode =
        QApplication::exec();


    // QApplication object `a` still exists here.
    //
    // Stop background pool BEFORE leaving main(),
    // so workers cannot outlive QApplication / Configs.
    Async::shutdown();


    return exitCode;
}

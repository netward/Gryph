#include "include/ui/mainwindow.h"
#include "ui_mainwindow.h"

#include <memory>
#include <atomic>
#include <ranges>

#include <QSemaphore>
#include <QMutexLocker>
#include <QAbstractItemView>
#include <QMenu>

#include "include/configs/sub/GroupUpdater.hpp"
#include "include/sys/Process.hpp"
#include "include/sys/AutoRun.hpp"
#include "include/sys/UrlScheme.hpp"

#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/setting/Icon.hpp"
#include "include/ui/profile/dialog_edit_profile.h"
#include "include/ui/setting/dialog_basic_settings.h"
#include "include/ui/group/dialog_manage_groups.h"
#include "include/ui/setting/dialog_manage_routes.h"
#include "include/ui/setting/dialog_vpn_settings.h"
#include "include/ui/setting/dialog_hotkey.h"

#include "3rdparty/qrcodegen.hpp"
#include "3rdparty/qv2ray/v2/ui/LogHighlighter.hpp"
#include "3rdparty/QrDecoder.h"
#include "include/configs/generate.h"
#include "include/database/GroupsRepo.h"
#include "include/database/ProfilesRepo.h"

#include "include/database/RoutesRepo.h"
#include "include/ui/utils/ProfilesTableFilterHeader.h"
#include "include/ui/group/dialog_edit_group.h"

#ifdef Q_OS_WIN
#include "3rdparty/WinCommander.hpp"
#include "include/sys/windows/WinVersion.h"
#include <Wbemidl.h>
#else
#ifdef Q_OS_LINUX
#include "include/sys/linux/LinuxCap.h"
#include <QDBusInterface>
#include <QDBusReply>
#include <sys/socket.h>
#endif
#ifdef Q_OS_MACOS
#include <sys/socket.h>
#include <sys/un.h>
#endif
#include <unistd.h>
#endif

#include <QUuid>
#include <QUrlQuery>

#include <QClipboard>
#include <QModelIndex>
#include <QLabel>
#include <QTextBlock>
#include <QScrollBar>
#include <QScreen>
#include <QDesktopServices>
#include <QInputDialog>
#include <QThread>
#include <QTimer>
#include <QMessageBox>
#include <QDir>
#include <QFileInfo>
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#include <QStyleHints>
#endif
#include <QFileDialog>
#include <QToolTip>
#include <QMimeData>
#include <random>
#include <3rdparty/QHotkey/qhotkey.h>
#include <3rdparty/qv2ray/v2/proxy/QvProxyConfigurator.hpp>
#include <include/global/HTTPRequestHelper.hpp>
#include "include/global/DeviceDetailsHelper.hpp"

#include "include/sys/macos/MacOS.h"

// Создание единственного экземпляра главного окна.
// Вызывается из main.cpp после подготовки приложения, БД и рабочих потоков.

// Проверяет, что подключившийся к IPC-серверу процесс действительно является запущенным GryphCore.
// PID извлекается платформенным способом и сравнивается с PID объекта core_process. 
// Вызывающий код обязан удерживать coreProcessMutex.
bool MainWindow::verify_core_pid(QLocalSocket* socket) {
    if (!core_process) return false;
    qint64 expectedPid = core_process->processId();
    if (expectedPid <= 0) return false;

#if defined(Q_OS_LINUX)
    struct ucred cred = {};
    socklen_t credLen = sizeof(cred);
    if (getsockopt(static_cast<int>(socket->socketDescriptor()), SOL_SOCKET, SO_PEERCRED, &cred, &credLen) == 0) {
        return static_cast<qint64>(cred.pid) == expectedPid;
    }
    return false;
#elif defined(Q_OS_MACOS)
    pid_t pid = 0;
    socklen_t pidLen = sizeof(pid);
    if (getsockopt(static_cast<int>(socket->socketDescriptor()), SOL_LOCAL, LOCAL_PEERPID, &pid, &pidLen) == 0) {
        return static_cast<qint64>(pid) == expectedPid;
    }
    return false;
#elif defined(Q_OS_WIN)
    ULONG pid = 0;
    HANDLE hPipe = reinterpret_cast<HANDLE>(static_cast<qintptr>(socket->socketDescriptor()));
    if (GetNamedPipeClientProcessId(hPipe, &pid)) {
        return static_cast<qint64>(pid) == expectedPid;
    }
    return false;
#else
    Q_UNUSED(socket)
        return true;
#endif
}

// Определяет, нужно ли использовать тёмную цветовую схему подсветки журнала.
// Для известных тем яркость задаётся явно, для системных тем берётся режим ОС.
static bool themeUsesDarkLog(const QString& theme) {
    const auto lower = theme.toLower();
    if (lower.contains("vista") || lower.contains("flatgray") || lower.contains("lightblue")) {
        return false; // light themes
    }
    if (lower.contains("qdarkstyle") || lower.contains("blacksoft")) {
        return true; // dark themes
    }
    return isDarkMode(); // bi-mode themes, follow system preference
}

// Конструктор полностью собирает рабочее состояние главного окна.
// Последовательность инициализации:
//   1. Регистрирует глобальные UI-callback и deeplink-обработчик.
//   2. Восстанавливает автозапуск, URL-схему, тему, геометрию и шрифты.
//   3. Настраивает журнал и фоновые потоки.
//   4. Создаёт локальный IPC-сервер для GryphCore и запускает ядро.
//   5. Подключает меню, таблицы, фильтры, статистику и график скорости.
//   6. Формирует системный трей и динамические меню.
//   7. Настраивает маршруты, тестирование, импорт, таймеры и обновления.
// Большинство connect() связывают действие пользователя или системное событие с методом MainWindow либо асинхронной задачей.
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), ui(new Ui::MainWindow) {
    // Регистрация главного окна и глобальных UI-маршрутизаторов.
    setAcceptDrops(true);
    MW_dialog_message = [=, this](MwMessage cmd, QStringList args) {
        runOnUiThread([=, this]
            {
                dialog_message_impl(cmd, args);
            });
        };
    // Общий обработчик deeplink-ссылок Gryph://.
    MW_handle_deeplink = [=, this](const QString& url) {
        runOnUiThread([=, this]
            {
                handle_deeplink_impl(url);
            });
        };

    // Проверка соответствия прав записи автозапуска текущим правам процесса и при необходимости перенос старого формата настройки автозапуска.
    AutoRun_FixPrivilegeIfNeeded();
    AutoRun_MigrateIfNeeded();

    // Регистрация пользовательской URL-схемы Gryph://.
    UrlScheme_RegisterIfNeeded();

    // Создание интерфейса и применение темы.
    bool isNum;
    Configs::dataManager->settingsRepo->theme.toInt(&isNum);
    if (isNum) {
        Configs::dataManager->settingsRepo->theme = "System";
    }
    // Тема применяется до setupUi(), чтобы создаваемые виджеты сразу получили правильную палитру и таблицы стилей.
    themeManager->ApplyTheme(Configs::dataManager->settingsRepo->theme);
    // Создание элементов mainwindow.ui, их назначение ui-полям.
    ui->setupUi(this);

    // Инициализация сочетаний клавиш.
    setActionsData();
    loadShortcuts();

    // Восстановление положения и размеров окна.
    if (!Configs::dataManager->settingsRepo->mainWindowGeometry.isEmpty()) {
        auto geo = DecodeB64IfValid(Configs::dataManager->settingsRepo->mainWindowGeometry);
        this->restoreGeometry(geo);
    }

    // Настройка просмотрщика журналов.
    ui->splitter->restoreState(DecodeB64IfValid(Configs::dataManager->settingsRepo->splitter_state));
    new SyntaxHighlighter(themeUsesDarkLog(Configs::dataManager->settingsRepo->theme), qvLogDocument);
    qvLogDocument->setUndoRedoEnabled(false);
    qvLogDocument->setMaximumBlockCount(Configs::dataManager->settingsRepo->max_log_line);
    ui->masterLogBrowser->setUndoRedoEnabled(false);
    ui->masterLogBrowser->setDocument(qvLogDocument);
    applyLogBrowserFont();
    updateLogFilterFields();
    runOnThread([=, this] {
        log_process_loop();
        }, LogThread);

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, this, [=, this](const Qt::ColorScheme& scheme) {
        new SyntaxHighlighter(scheme == Qt::ColorScheme::Dark, qvLogDocument);
        themeManager->ApplyTheme(Configs::dataManager->settingsRepo->theme, true);
        });
#endif
    // Реакция на смену темы из настроек.
    connect(themeManager, &ThemeManager::themeChanged, this, [=, this](const QString& theme) {
        new SyntaxHighlighter(themeUsesDarkLog(theme), qvLogDocument);
        scheduleProxyListRefresh();
        });
    MW_show_log = [=, this](const QString& log) {
        append_log(log);
        };

    // Выбор входящего порта.
    if (Configs::dataManager->settingsRepo->random_inbound_port)
    {
        Configs::dataManager->settingsRepo->inbound_socks_port = MkPort();
    }

    // Получение сведений об устройстве.
    runOnNewThread([=, this] {GetDeviceDetails(); });

    // Подготовка параметров GryphCore.
    auto core_path = QApplication::applicationDirPath() + "/";
    core_path += "GryphCore";
    // Режим отладки ядра в зависимости от выбранного уровня журналирования.
    bool coreDebugMode = (Configs::dataManager->settingsRepo->log_level == "debug");

    // Создание локального IPC-сервера со случаййным UUID для избежания конфликтов между экземплярами.
    Configs::dataManager->settingsRepo->core_socket_name =
        "gryphIPC-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    core_server = new QLocalServer(this);
    // Подключение к IPC-серверу только для текущего пользователя.
    core_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!core_server->listen(Configs::dataManager->settingsRepo->core_socket_name)) {
        qWarning() << "Failed to start IPC server:" << core_server->errorString();
        qApp->quit();
    }

    // Обработка подключения ядра к серверу.
    // PID клиента дополнительно сверяется с PID GryphCore.
    connect(core_server, &QLocalServer::newConnection, this, [=, this]() {
        auto socket = core_server->nextPendingConnection();
        int profileId = -1;
        {
            // Защита core_process от одновременного чтения и записи.
            // Поток DS_cores может в этот момент ещё создавать или запускать объект процесса.
            QMutexLocker lock(&coreProcessMutex);
            if (!verify_core_pid(socket)) {
                MW_show_log("[Warn] IPC connection from unexpected process rejected");
                socket->close();
                socket->deleteLater();
                return;
            }
            if (core_process) {
                // Забираем одноразовое задание запуска сохранённого профиля.
                profileId = core_process->start_profile_when_core_is_up;
                // Сбрасываем значение, чтобы профиль не был запущен повторно при следующем переподключении ядра.
                core_process->start_profile_when_core_is_up = -1;
            }
        }
        // Создание RPC-канала поверх сокета после проверки клиента.
        setup_rpc(socket);
        Configs::dataManager->settingsRepo->core_running = true;
        // Уведомление главного окна о готовности ядра.
        // Обработчик CoreStarted восстановит нужные режимы и при необходимости запустит profileId.
        MW_dialog_message(MwMessage::CoreStarted, { Int2String(profileId) });
        });

    // Запуск GryphCore
    auto socketFullName = core_server->fullServerName();
    runOnThread(
        [=, this] {
            QMutexLocker lock(&coreProcessMutex);
            core_process = new Configs_sys::CoreProcess(core_path, socketFullName, coreDebugMode);
            // Если включено восстановление последнего профиля, его ID сохраняется в объекте процесса до фактического запуска ядра.
            if (Configs::dataManager->settingsRepo->remember_enable &&
                Configs::dataManager->settingsRepo->remember_id >= 0) {
                core_process->start_profile_when_core_is_up =
                    Configs::dataManager->settingsRepo->remember_id;
            }
            core_process->Start();
        },
        DS_cores);

    // Применение пользовательского шрифта
    if (!Configs::dataManager->settingsRepo->font.isEmpty()) {
        auto font = qApp->font();
        font.setFamily(Configs::dataManager->settingsRepo->font);
        qApp->setFont(font);
    }
    if (Configs::dataManager->settingsRepo->font_size != 0) {
        auto font = qApp->font();
        font.setPointSize(Configs::dataManager->settingsRepo->font_size);
        qApp->setFont(font);
    }

    // Ограничение параллельных вызовов ядра
    parallelCoreCallPool->setMaxThreadCount(10);

    // Основные действия, вкладки и фильтры
    connect(ui->menu_start, &QAction::triggered, this, [=, this]() { profile_start(); });
    connect(ui->menu_stop, &QAction::triggered, this, [=, this]() { profile_stop(false, false, true); });
    // При перетаскивании вкладки сохраняем новый порядок групп.
    connect(ui->tabWidget->tabBar(), &QTabBar::tabMoved, this, [=, this](int from, int to) {
        // tabData каждой вкладки содержит ID соответствующей группы.
        QList<int> tabOrder;
        for (int i = 0; i < ui->tabWidget->tabBar()->count(); i++) {
            tabOrder += ui->tabWidget->tabBar()->tabData(i).toInt();
        }
        Configs::dataManager->groupsRepo->SetGroupsTabOrder(tabOrder);
        on_tabWidget_currentChanged(ui->tabWidget->tabBar()->currentIndex());
        });
    // Главное окно перехватывает клики и двойные клики виджетов через MainWindow::eventFilter().
    ui->label_running->installEventFilter(this);
    ui->label_inbound->installEventFilter(this);
    ui->splitter->installEventFilter(this);
    ui->tabWidget->installEventFilter(this);
    // Кнопка в углу вкладок показывает или скрывает поля фильтрации в пользовательском заголовке таблицы профилей.
    auto btnFilter = new QToolButton(this);
    btnFilter->setIcon(QIcon(":/icon/filter.png"));
    btnFilter->setToolTip(QString("%1\n%2").arg(tr("Enable Filter"), QKeySequence(QKeySequence::Find).toString(QKeySequence::NativeText)));
    btnFilter->setShortcut(QKeySequence::Find);
    btnFilter->setCheckable(true);
    connect(btnFilter, &QToolButton::toggled, static_cast<ProfilesTableFilterHeader*>(ui->profilesTableView->horizontalHeader()), &ProfilesTableFilterHeader::setFiltersVisible);
    ui->tabWidget->setCornerWidget(btnFilter, Qt::TopRightCorner);
    // Регистрация глобальных системных горячих клавиш.
    RegisterHotkey(false);
    // Дополнительное восстановление размера для старого формата настроек вида "ШxВ".
    auto last_size = Configs::dataManager->settingsRepo->mw_size.split("x");
    if (last_size.length() == 2) {
        auto w = last_size[0].toInt();
        auto h = last_size[1].toInt();
        if (w > 0 && h > 0) {
            resize(w, h);
        }
    }

    // Идентификация приложения и подготовка служебных каталогов
    software_name = "Gryph";
    software_core_name = "sing-box";
    //  Создание локальной страницы-заглушки при первом запуске.
    if (auto dashDir = QDir("dashboard"); !dashDir.exists() && QDir().mkdir("dashboard")) {
        if (auto dashFile = QFile(":/Gryph/dashboard-notice.html"); dashFile.exists() && dashFile.open(QIODevice::ReadOnly))
        {
            auto data = dashFile.readAll();
            if (auto dest = QFile("dashboard/index.html"); dest.open(QIODevice::Truncate | QIODevice::WriteOnly))
            {
                dest.write(data);
                dest.close();
            }
            dashFile.close();
        }
    }
    // Использование каталога icons для пользовательских иконок профилей.
    if (auto iconsDir = QDir("icons"); !iconsDir.exists()) {
        QDir().mkdir("icons") ? qDebug("created icons dir") : qDebug("Failed to create icons dir");
    }

    // Верхняя панель и меню.
    ui->toolButton_program->setMenu(ui->menu_program);
    ui->toolButton_preferences->setMenu(ui->menu_preferences);
    ui->toolButton_server->setMenu(ui->menu_server);
    ui->toolButton_routing->setMenu(ui->menuRouting_Menu);
    ui->menubar->setVisible(false);
    // Проверка обновлений выполняется в новом потоке, так как содержит сетевой запрос к GitHub.
    connect(ui->toolButton_update, &QToolButton::clicked, this, [=, this] { runOnNewThread([=, this] { CheckUpdate(); }); });
    if (!QFile::exists(QApplication::applicationDirPath() + "/updater") && !QFile::exists(QApplication::applicationDirPath() + "/updater.exe"))
    {
        ui->toolButton_update->hide();
    }

    // Таблица активных соединений и статистика.
    setupConnectionList();
    // Возврат пользователя на последнюю открытую вкладку статистики.
    ui->stats_widget->tabBar()->setCurrentIndex(Configs::dataManager->settingsRepo->stats_tab);
    connect(ui->stats_widget->tabBar(), &QTabBar::currentChanged, this, [=, this](int index)
        {
            Configs::dataManager->settingsRepo->stats_tab = ui->stats_widget->tabBar()->currentIndex();
        });
    // Изменение критерия сортировки активных соединений при нажатии на заголовок.
    // Сортировка профилей по нажатию на заголовок.
    // Повторное нажатие на тот же столбец
    // переключает направление.
    const auto sortGroupAsync =
        [this](
            const std::shared_ptr<
            Configs::Group
            >& group,
            const GroupSortAction& action)
        {
            if (!group)
            {
                return;
            }


            const int groupId =
                group->Id();


            runOnNewThread(
                [
                    this,
                    group,
                    groupId,
                    action
                ]()
                {
                    // -------------------------------------
                    // First attempt
                    // -------------------------------------

                    if (!group->SortProfiles(action))
                    {
                        // The group profile list was
                        // modified concurrently, e.g.
                        // subscription update/add/remove.
                        //
                        // SortProfiles is now serialized,
                        // so this is NOT another sort.
                        //
                        // Retry once using the new state.
                        if (!group->SortProfiles(action))
                        {
                            MW_show_log(
                                "Group changed while sorting; "
                                "sort cancelled."
                            );

                            return;
                        }
                    }


                    // Persist exactly the group we sorted.
                    Configs::dataManager
                        ->groupsRepo
                        ->Save(group);


                    // -------------------------------------
                    // UI refresh
                    // -------------------------------------

                    runOnUiThread(
                        [
                            this,
                            groupId
                        ]()
                        {
                            auto currentGroup =
                                Configs::dataManager
                                ->groupsRepo
                                ->CurrentGroup();


                            // User switched to another
                            // group while sorting.
                            if (!currentGroup ||
                                currentGroup->Id() != groupId)
                            {
                                return;
                            }


                            refresh_proxy_list(
                                {},
                                true
                            );
                        }
                    );
                }
            );
        };

    // =========================================================
// Profile table sort state
//
// 0 = default subscription order
// 1 = ascending
// 2 = descending
// =========================================================

    struct ProfileSortState
    {
        int column = -1;
        int mode = 0;
    };


    const auto profileSortState =
        std::make_shared<ProfileSortState>();


    const auto restoreDefaultOrderAsync =
        [this](
            const std::shared_ptr<
            Configs::Group
            >& group)
        {
            if (!group)
            {
                return;
            }


            const int groupId =
                group->Id();


            runOnNewThread(
                [
                    this,
                    group,
                    groupId
                ]()
                {
                    // Restore the canonical order received
                    // from the subscription.
                    if (!group
                        ->RestoreDefaultProfileOrder())
                    {
                        MW_show_log(
                            "Default profile order "
                            "is not available."
                        );

                        return;
                    }


                    Configs::dataManager
                        ->groupsRepo
                        ->Save(group);


                    runOnUiThread(
                        [
                            this,
                            groupId
                        ]()
                        {
                            auto currentGroup =
                                Configs::dataManager
                                ->groupsRepo
                                ->CurrentGroup();


                            if (!currentGroup ||
                                currentGroup->Id() !=
                                groupId)
                            {
                                return;
                            }


                            refresh_proxy_list(
                                {},
                                true
                            );
                        }
                    );
                }
            );
        };

    connect(
        ui->profilesTableView->horizontalHeader(),
        &QHeaderView::sectionClicked,
        this,

        [
            this,
            sortGroupAsync,
            restoreDefaultOrderAsync,
            profileSortState
        ](int logicalIndex)
        {
            // We only have five sortable columns.
            if (logicalIndex < 0 ||
                logicalIndex > 4)
            {
                return;
            }


            auto group =
                Configs::dataManager
                ->groupsRepo
                ->CurrentGroup();


            if (!group)
            {
                return;
            }


            // =================================================
            // Determine next state
            // =================================================

            if (profileSortState->column !=
                logicalIndex)
            {
                // First click on another column.
                profileSortState->column =
                    logicalIndex;

                profileSortState->mode =
                    1; // ascending
            }
            else
            {
                // Same column:
                //
                // ascending
                //     ↓
                // descending
                //     ↓
                // default
                //     ↓
                // ascending

                switch (profileSortState->mode)
                {
                case 0:

                    profileSortState->mode =
                        1;

                    break;


                case 1:

                    profileSortState->mode =
                        2;

                    break;


                case 2:

                    profileSortState->mode =
                        0;

                    break;


                default:

                    profileSortState->mode =
                        1;

                    break;
                }
            }


            auto* header =
                ui->profilesTableView
                ->horizontalHeader();


            // =================================================
            // Default subscription order
            // =================================================

            if (profileSortState->mode == 0)
            {
                // No active ascending/descending sort.
                header->setSortIndicatorShown(
                    false
                );


                restoreDefaultOrderAsync(
                    group
                );


                return;
            }


            // =================================================
            // Ascending / descending
            // =================================================

            GroupSortAction action;


            action.descending =
                profileSortState->mode == 2;


            // -------------------------------------------------
            // Column mapping
            //
            // 0 = Name
            // 1 = Type
            // 2 = Address
            // 3 = Test Result
            // 4 = Traffic
            // -------------------------------------------------

            switch (logicalIndex)
            {
            case 0:

                action.method =
                    GroupSortMethod::ByName;

                break;


            case 1:

                action.method =
                    GroupSortMethod::ByType;

                break;


            case 2:

                action.method =
                    GroupSortMethod::ByAddress;

                break;


            case 3:

                action.method =
                    GroupSortMethod::ByTestResult;

                break;


            case 4:

                action.method =
                    GroupSortMethod::ByTraffic;

                break;


            default:

                return;
            }


            // =================================================
            // Visual sort indicator
            // =================================================

            header->setSortIndicatorShown(
                true
            );


            header->setSortIndicator(
                logicalIndex,

                action.descending
                ? Qt::DescendingOrder
                : Qt::AscendingOrder
            );


            // =================================================
            // Execute
            // =================================================

            sortGroupAsync(
                group,
                action
            );
        }
    );

    connect(
        ui->tabWidget,
        &QTabWidget::currentChanged,
        this,

        [
            this,
            profileSortState
        ](int)
        {
            profileSortState->column =
                -1;

            profileSortState->mode =
                0;


            ui->profilesTableView
                ->horizontalHeader()
                ->setSortIndicatorShown(
                    false
                );
        }
    );


    // График скорости
    speedChartWidget = new SpeedWidget(this);
    ui->graph_tab->layout()->addWidget(speedChartWidget);

    // Модель и представление списка профилей
    profilesTableModel = new ProfilesTableModel(this);
    ui->profilesTableView->setModel(profilesTableModel);
    // Callback вызывается пользовательским представлением после перетаскивания строк.
    ui->profilesTableView
        ->rowsSwapped =
        [this](int row1, int row2)
        {
            if (!addressFilterString.isEmpty() ||
                !nameFilterString.isEmpty() ||
                !typeFilterString.isEmpty() ||
                !countryFilterString.isEmpty())
            {
                return;
            }


            if (row1 == row2)
            {
                return;
            }


            auto group =
                Configs::dataManager
                ->groupsRepo
                ->CurrentGroup();


            if (!group)
            {
                return;
            }


            if (!group->EmplaceProfile(
                row1,
                row2))
            {
                return;
            }


            profilesTableModel
                ->emplaceProfiles(
                    row1,
                    row2
                );


            Configs::dataManager
                ->groupsRepo
                ->Save(group);
        };
    // Пользовательская ширина каждого столбца сохраняется отдельно для текущей группы.
    connect(ui->profilesTableView->horizontalHeader(), &QHeaderView::sectionResized, this, [=, this](int, int, int) {
        auto group = Configs::dataManager->groupsRepo->CurrentGroup();
        if (Configs::dataManager->settingsRepo->refreshing_group || group == nullptr) return;
        QList<int> widths;


        auto* header =
            ui->profilesTableView
            ->horizontalHeader();


        widths.reserve(
            header->count()
        );


        for (int i = 0;
            i < header->count();
            ++i)
        {
            widths.append(
                header->sectionSize(i)
            );
        }


        group->SetColumnWidths(
            widths
        );


        Configs::dataManager
            ->groupsRepo
            ->Save(group);
        });
    // Контекстное меню заголовка:
    //  столбец 3 — состав и сортировка результатов тестирования;
    //  столбец 4 — способ сортировки трафика.
    ui->profilesTableView->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->profilesTableView
        ->horizontalHeader()
        ->setContextMenuPolicy(
            Qt::CustomContextMenu
        );

    connect(
        ui->profilesTableView->horizontalHeader(),
        &QWidget::customContextMenuRequested,
        this,

        [
            this,
            sortGroupAsync,
            profileSortState
        ]
        (const QPoint& pos)
        {
            auto* header =
                ui->profilesTableView
                ->horizontalHeader();

            const int columnIndex =
                header->logicalIndexAt(pos);


            auto group =
                Configs::dataManager
                ->groupsRepo
                ->CurrentGroup();


            if (!group)
            {
                return;
            }


            const auto groupSnapshot =
                group->Snapshot();


            // =================================================
            // Test Result
            // =================================================
            if (columnIndex == 3)
            {
                QMenu menu(this);

                auto* includeLabel =
                    menu.addAction(
                        tr("Include:")
                    );

                includeLabel->setEnabled(
                    false
                );


                auto* actionShowOutIP =
                    menu.addAction(
                        tr("Out IP")
                    );

                actionShowOutIP
                    ->setCheckable(true);

                actionShowOutIP
                    ->setChecked(
                        groupSnapshot.test_items_to_show ==
                        Configs::testShowItems::all
                        ||
                        groupSnapshot.test_items_to_show ==
                        Configs::testShowItems::ipOnly
                    );


                auto* actionShowSpeed =
                    menu.addAction(
                        tr("Speed")
                    );

                actionShowSpeed
                    ->setCheckable(true);

                actionShowSpeed
                    ->setChecked(
                        groupSnapshot.test_items_to_show ==
                        Configs::testShowItems::all
                        ||
                        groupSnapshot.test_items_to_show ==
                        Configs::testShowItems::speedOnly
                    );


                auto updateTestItemsToShow =
                    [
                        this,
                        group,
                        actionShowOutIP,
                        actionShowSpeed
                    ]()
                    {
                        const bool ip =
                            actionShowOutIP
                            ->isChecked();

                        const bool speed =
                            actionShowSpeed
                            ->isChecked();


                        Configs::testShowItems value;


                        if (ip && speed)
                        {
                            value =
                                Configs::testShowItems::all;
                        }
                        else if (ip)
                        {
                            value =
                                Configs::testShowItems::ipOnly;
                        }
                        else if (speed)
                        {
                            value =
                                Configs::testShowItems::speedOnly;
                        }
                        else
                        {
                            value =
                                Configs::testShowItems::none;
                        }


                        group->SetTestItemsToShow(
                            value
                        );

                        group->ResetCalculatedColumnWidth(
                            3
                        );

                        Configs::dataManager
                            ->groupsRepo
                            ->Save(group);

                        refresh_proxy_list();
                    };


                connect(
                    actionShowOutIP,
                    &QAction::triggered,
                    this,
                    updateTestItemsToShow
                );

                connect(
                    actionShowSpeed,
                    &QAction::triggered,
                    this,
                    updateTestItemsToShow
                );


                menu.addSeparator();


                auto* sortByLabel =
                    menu.addAction(
                        tr("Sort By:")
                    );

                sortByLabel->setEnabled(
                    false
                );


                struct SortOption
                {
                    int value;
                    QString label;
                };


                const QList<SortOption> options =
                {
                    {
                        static_cast<int>(
                            Configs::testBy::latency
                        ),
                        tr("Latency")
                    },

                    {
                        static_cast<int>(
                            Configs::testBy::dlSpeed
                        ),
                        tr("Download Speed")
                    },

                    {
                        static_cast<int>(
                            Configs::testBy::ulSpeed
                        ),
                        tr("Upload Speed")
                    },

                    {
                        static_cast<int>(
                            Configs::testBy::ipOut
                        ),
                        tr("IP Out")
                    }
                };


                for (const auto& opt : options)
                {
                    auto* act =
                        menu.addAction(
                            opt.label
                        );

                    act->setData(
                        opt.value
                    );

                    act->setCheckable(
                        true
                    );

                    act->setChecked(
                        static_cast<int>(
                            groupSnapshot.test_sort_by
                            ) == opt.value
                    );
                }


                auto* chosen =
                    menu.exec(
                        header->mapToGlobal(pos)
                    );


                if (!chosen ||
                    !chosen->data().isValid())
                {
                    return;
                }


                const int testSortBy =
                    chosen
                    ->data()
                    .toInt();


                group->SetTestSortBy(
                    static_cast<Configs::testBy>(
                        testSortBy
                        )
                );


                Configs::dataManager
                    ->groupsRepo
                    ->Save(group);


                GroupSortAction action;

                action.method =
                    GroupSortMethod::ByTestResult;

                action.descending =
                    true;

                profileSortState->column =
                    3;

                profileSortState->mode =
                    action.descending
                    ? 2
                    : 1;


                header->setSortIndicatorShown(
                    true
                );

                header->setSortIndicator(
                    3,
                    action.descending
                    ? Qt::DescendingOrder
                    : Qt::AscendingOrder
                );

                sortGroupAsync(
                    group,
                    action
                );


                return;
            }


            // =================================================
            // Traffic
            // =================================================

            if (columnIndex == 4)
            {
                QMenu menu(this);


                auto* sortByLabel =
                    menu.addAction(
                        tr("Sort By:")
                    );

                sortByLabel->setEnabled(
                    false
                );


                struct TrafficSortOption
                {
                    int value;
                    QString label;
                };


                const QList<TrafficSortOption> options =
                {
                    {
                        0,
                        tr("Total")
                    },
                    {
                        1,
                        tr("Downloaded")
                    },
                    {
                        2,
                        tr("Uploaded")
                    }
                };


                for (const auto& opt : options)
                {
                    auto* act =
                        menu.addAction(
                            opt.label
                        );

                    act->setData(
                        opt.value
                    );

                    act->setCheckable(
                        true
                    );

                    act->setChecked(
                        static_cast<int>(
                            groupSnapshot.traffic_sort_by
                            ) == opt.value
                    );
                }


                auto* chosen =
                    menu.exec(
                        header->mapToGlobal(pos)
                    );


                if (!chosen ||
                    !chosen->data().isValid())
                {
                    return;
                }


                const int trafficSortBy =
                    chosen
                    ->data()
                    .toInt();


                group->SetTrafficSortBy(
                    static_cast<Configs::trafficBy>(
                        trafficSortBy
                        )
                );


                Configs::dataManager
                    ->groupsRepo
                    ->Save(group);


                GroupSortAction action;

                action.method =
                    GroupSortMethod::ByTraffic;

                action.descending =
                    false;

                profileSortState->column =
                    4;

                profileSortState->mode =
                    action.descending
                    ? 2
                    : 1;


                header->setSortIndicatorShown(
                    true
                );

                header->setSortIndicator(
                    4,
                    action.descending
                    ? Qt::DescendingOrder
                    : Qt::AscendingOrder
                );

                sortGroupAsync(
                    group,
                    action
                );


                return;
            }
        }
    );
    // Фиксированная высота строк 24 px для уменьшения затрат на перерасчёт геометрии большой таблицы.
    ui->profilesTableView->verticalHeader()->setStretchLastSection(false);
    ui->profilesTableView->verticalHeader()->setDefaultSectionSize(24);
    ui->profilesTableView->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    ui->profilesTableView->setTabKeyNavigation(false);
    ui->profilesTableView->horizontalHeader()->setResizeContentsPrecision(0);
    // Повторное уточнение автоматической ширины столбцов после прокрутки с учетом появившихся в viewport строк.
    connect(ui->profilesTableView->verticalScrollBar(), &QScrollBar::valueChanged, ui->profilesTableView, [=, this] {
        refresh_proxy_list_column_size();
        });

    // Фильтры таблицы профилей.
    connect(static_cast<ProfilesTableFilterHeader*>(ui->profilesTableView->horizontalHeader()), &ProfilesTableFilterHeader::typeFilterChanged, this, [=, this](const QString& currentText)
        {
            typeFilterString = currentText;
            refresh_proxy_list({}, true);
        });
    connect(static_cast<ProfilesTableFilterHeader*>(ui->profilesTableView->horizontalHeader()), &ProfilesTableFilterHeader::addressFilterChanged, this, [=, this](const QString& currentText)
        {
            addressFilterString = currentText;
            refresh_proxy_list({}, true);
        });
    connect(static_cast<ProfilesTableFilterHeader*>(ui->profilesTableView->horizontalHeader()), &ProfilesTableFilterHeader::nameFilterChanged, this, [=, this](const QString& currentText)
        {
            nameFilterString = currentText;
            refresh_proxy_list({}, true);
        });
    connect(static_cast<ProfilesTableFilterHeader*>(ui->profilesTableView->horizontalHeader()), &ProfilesTableFilterHeader::testFilterChanged, this, [=, this](const QString& currentText)
        {
            countryFilterString = currentText;
            refresh_proxy_list({}, true);
        });

    // Загрузка групп, создание вкладок и отображение текущей группы.
    this->refresh_groups();

    // Системный трей.
    tray = new QSystemTrayIcon(nullptr);
    tray->setIcon(GetTrayIcon(Icon::NONE));
    QApplication::setWindowIcon(Icon::GetTrayIcon(Icon::NONE));
    auto* trayMenu = new QMenu();
    trayMenu->addAction(ui->actionShow_window);
    trayMenu->addSeparator();
    trayMenu->addAction(ui->actionStart_with_system);
    trayMenu->addAction(ui->actionRemember_last_proxy);
    trayMenu->addAction(ui->actionAllow_LAN);
    trayMenu->addSeparator();

    // Подменю выбора профиля создаётся заново при каждом открытии.
    // Пагинация ограничивает размер меню пятнадцатью профилями.
    constexpr int PAGE_CAPACITY = 15;
    trayServerMenu = new QMenu(tr("Select Server"));
    trayMenu->addMenu(trayServerMenu);
    connect(trayServerMenu, &QMenu::aboutToShow, this, [=, this]() {
        trayServerMenu->clear();
        // Остановка текущего профиля, если он запущен.
        if (running) {
            const auto runningConfig =
                running->ConfigSnapshot();

            auto* stopAction =
                trayServerMenu->addAction(
                    tr("Stop: %1").arg(
                        runningConfig.name
                    )
                );

            connect(
                stopAction,
                &QAction::triggered,
                this,
                [=, this]()
                {
                    profile_stop(
                        false,
                        false,
                        true
                    );
                }
            );

            trayServerMenu->addSeparator();
        }
        // Формирование плоского списка профилей. 
        // Первой становится группа активного профиля либо текущая выбранная группа.
        int startGroupId = Configs::dataManager->settingsRepo->current_group;
        if (running) startGroupId = running->GroupId();
        auto groupIds = Configs::dataManager->groupsRepo->GetGroupsTabOrder();
        // Циклическая перестановка групп, чтобы startGroupId находилась в начале списка.
        int startIdx = groupIds.indexOf(startGroupId);
        if (startIdx > 0) {
            QList<int> reordered = groupIds.mid(startIdx) + groupIds.mid(0, startIdx);
            groupIds = reordered;
        }
        QList<int> allProfileIDs;
        for (const int gid :
        groupIds)
        {
            auto group =
                Configs::dataManager
                ->groupsRepo
                ->GetGroup(
                    gid
                );
            if (!group)
            {
                MW_show_log(
                    QString(
                        "Tray menu: group %1 "
                        "does not exist."
                    )
                    .arg(gid)
                );

                continue;
            }
            allProfileIDs.append(
                group->Profiles()
            );
        }
        int totalProfiles = allProfileIDs.size();
        // Ограничение номера страницы допустимым диапазоном после добавления или удаления профилей.
        int maxPage = qMax(0, (totalProfiles - 1) / PAGE_CAPACITY);
        trayServerPage = qBound(0, trayServerPage, maxPage);
        int offset = trayServerPage * PAGE_CAPACITY;
        int end = qMin(offset + PAGE_CAPACITY, totalProfiles);
        // На страницах после первой добавляем переход назад.
        if (trayServerPage > 0) {
            auto* upAction = trayServerMenu->addAction(QStringLiteral("\u2191"));
            connect(upAction, &QAction::triggered, this, [=, this]() {
                trayServerPage--;
                trayServerMenu->popup(trayServerMenu->pos());
                });
        }
        // Загрузка из репозитория только ID и имен текущей страницы без создания полных объектов всех профилей.
        auto neededProfilesIDNames = Configs::dataManager->profilesRepo->GetProfileIDNameMappedBatch(allProfileIDs.sliced(offset, end - offset));
        for (const auto& [id, name] : neededProfilesIDNames) {
            auto* action = trayServerMenu->addAction(name);
            action->setCheckable(true);
            action->setChecked(running && running->Id() == id);
            connect(action, &QAction::triggered, this, [=, this]() { profile_start(id); });
        }
        // Если остались профили, добавляем переход на следующую страницу.
        if (trayServerPage < maxPage) {
            auto* downAction = trayServerMenu->addAction(QStringLiteral("\u2193"));
            connect(downAction, &QAction::triggered, this, [=, this]() {
                trayServerPage++;
                trayServerMenu->popup(trayServerMenu->pos());
                });
        }
        });
    trayMenu->addSeparator();

    // macOS некорректно переиспользует один QMenu с разными родителями, поэтому для трея создаётся отдельное меню режимов.
    if (getOS() == Darwin) {
        auto* traySpmodeMenu = new QMenu(ui->menu_spmode->title(), trayMenu);
        traySpmodeMenu->addAction(ui->menu_spmode_system_proxy);
        traySpmodeMenu->addAction(ui->menu_spmode_vpn);
        traySpmodeMenu->addAction(ui->menu_spmode_disabled);
        connect(traySpmodeMenu, &QMenu::aboutToShow, this, [=, this]() {
            ui->menu_spmode_disabled->setChecked(!(Configs::dataManager->settingsRepo->spmode_system_proxy || Configs::dataManager->settingsRepo->spmode_vpn));
            ui->menu_spmode_system_proxy->setChecked(Configs::dataManager->settingsRepo->spmode_system_proxy);
            ui->menu_spmode_vpn->setChecked(Configs::dataManager->settingsRepo->spmode_vpn);
            });
        trayMenu->addMenu(traySpmodeMenu);
    }
    else {
        trayMenu->addMenu(ui->menu_spmode);
    }
    trayMenu->addSeparator();
    trayMenu->addAction(ui->actionRestart_Proxy);
    trayMenu->addAction(ui->actionRestart_Program);
    trayMenu->addAction(ui->menu_exit);
    tray->setVisible(!Configs::dataManager->settingsRepo->disable_tray);
    tray->setContextMenu(trayMenu);
    connect(trayMenu, &QMenu::aboutToShow, this, [=, this]() {
        trayServerPage = 0;
        });
    connect(tray, &QSystemTrayIcon::activated, qApp, [=, this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger && getOS() != Darwin) {
            ActivateWindow(this);
            refresh_proxy_list_column_size();
        }
        });

    // Общие команды и режимы работы.
    ui->actionRemember_last_proxy->setChecked(Configs::dataManager->settingsRepo->remember_enable);
    ui->actionStart_with_system->setChecked(AutoRun_IsEnabled());
    ui->actionAllow_LAN->setChecked(QStringList{ "::", "0.0.0.0" }.contains(Configs::dataManager->settingsRepo->inbound_address));

    connect(ui->actionHide_window, &QAction::triggered, this, [=, this]() { HideWindow(this); });
    connect(ui->menu_open_config_folder, &QAction::triggered, this, [=, this] { QDesktopServices::openUrl(QUrl::fromLocalFile(QDir::currentPath())); });
    connect(ui->menu_add_from_clipboard2, &QAction::triggered, ui->menu_add_from_clipboard, &QAction::trigger);
    // Перезапуск прокси фактически останавливает профиль и завершает ядро;
    // дальнейшее восстановление выполняется общей логикой процесса.
    connect(ui->actionRestart_Proxy, &QAction::triggered, this, [=, this] {
        runOnThread([=, this] {
            profile_stop(true, true, true);
            core_process->Kill();
            }, DS_cores);
        });
    connect(ui->actionRestart_Program, &QAction::triggered, this, [=, this] { MW_dialog_message(MwMessage::RestartProgram, {}); });
    connect(ui->actionShow_window, &QAction::triggered, this, [=, this] { ActivateWindow(this); });
    connect(ui->actionRemember_last_proxy, &QAction::triggered, this, [=, this](bool checked) {
        Configs::dataManager->settingsRepo->remember_enable = checked;
        ui->actionRemember_last_proxy->setChecked(checked);
        Configs::dataManager->settingsRepo->Save();
        });
    connect(ui->actionStart_with_system, &QAction::triggered, this, [=, this](bool checked) {
        AutoRun_SetEnabled(checked);
        ui->actionStart_with_system->setChecked(checked);
        });
    // "::" открывает mixed-inbound на всех IPv6/IPv4-интерфейсах,
    // "127.0.0.1" ограничивает доступ локальной машиной.
    connect(ui->actionAllow_LAN, &QAction::triggered, this, [=, this](bool checked) {
        Configs::dataManager->settingsRepo->inbound_address = checked ? "::" : "127.0.0.1";
        ui->actionAllow_LAN->setChecked(checked);
        MW_dialog_message(MwMessage::UpdateSettings, {});
        });

    // Переключатели VPN/TUN, системного прокси и системного DNS.
    // Фактическая работа вынесена в set_spmode_*() и set_system_dns().
    connect(ui->checkBox_VPN, &QCheckBox::clicked, this, [=, this](bool checked) { set_spmode_vpn(checked); });
    connect(ui->checkBox_SystemProxy, &QCheckBox::clicked, this, [=, this](bool checked) { set_spmode_system_proxy(checked); });
    connect(ui->menu_spmode, &QMenu::aboutToShow, this, [=, this]() {
        ui->menu_spmode_disabled->setChecked(!(Configs::dataManager->settingsRepo->spmode_system_proxy || Configs::dataManager->settingsRepo->spmode_vpn));
        ui->menu_spmode_system_proxy->setChecked(Configs::dataManager->settingsRepo->spmode_system_proxy);
        ui->menu_spmode_vpn->setChecked(Configs::dataManager->settingsRepo->spmode_vpn);
        });
    connect(ui->menu_spmode_system_proxy, &QAction::triggered, this, [=, this](bool checked) { set_spmode_system_proxy(checked); });
    connect(ui->menu_spmode_vpn, &QAction::triggered, this, [=, this](bool checked) { set_spmode_vpn(checked); });
    connect(ui->menu_spmode_disabled, &QAction::triggered, this, [=, this]() {
        set_spmode_system_proxy(false);
        set_spmode_vpn(false);
        });
    connect(ui->menu_qr, &QAction::triggered, this, [=, this]() { display_qr_link(false); });
    connect(ui->system_dns, &QCheckBox::clicked, this, [=, this](bool checked) {
        if (const auto ok = set_system_dns(checked); !ok) {
            ui->system_dns->setChecked(!checked);
        }
        else {
            refresh_status();
        }
        });
    if (Configs::dataManager->settingsRepo->show_system_dns) ui->system_dns->show();
    else ui->system_dns->hide();

    // Включение действий, применимых к текущему состоянию и выделению перед показом меню сервера.
    connect(
        ui->menu_server,
        &QMenu::aboutToShow,
        this,
        [this]()
        {
            // -------------------------------------------------
            // Current profile speed test
            // -------------------------------------------------

            ui->actionSpeedtest_Current
                ->setEnabled(
                    static_cast<bool>(running)
                );


            // -------------------------------------------------
            // Selected profiles actions
            // -------------------------------------------------

            const auto selected =
                get_now_selected_list();

            const bool hasSelection =
                !selected.empty();


            ui->actionSpeedtest_Selected
                ->setEnabled(hasSelection);

            ui->actionUrl_Test_Selected
                ->setEnabled(hasSelection);

            ui->menu_resolve_selected
                ->setEnabled(hasSelection);

            ui->actionResolve_Selected_Out_IP
                ->setEnabled(hasSelection);


            // -------------------------------------------------
            // Stop testing action
            // -------------------------------------------------

            const bool isTesting =
                speedtestRunning.load(
                    std::memory_order_acquire
                );


            if (isTesting) {

                // Add only if it is not already in the menu.
                if (!ui->menu_server
                    ->actions()
                    .contains(
                        ui->menu_stop_testing
                    ))
                {
                    ui->menu_server
                        ->addAction(
                            ui->menu_stop_testing
                        );
                }

            }
            else {

                ui->menu_server
                    ->removeAction(
                        ui->menu_stop_testing
                    );
            }
        }
    );

    // Получение каталога удалённых профилей маршрутизации
    auto getRemoteRouteProfiles = [=, this]
        {
            auto resp = NetworkRequestHelper::HttpGet(
                "https://api.github.com/repos/netward/routeprofiles/contents/profiles?ref=main"
            );

            if (!resp.error.isEmpty()) {
                MW_show_log(
                    "Failed to get remote route profiles: "
                    + resp.error
                    + "\n"
                    + resp.data
                );
                return;
            }

            QStringList newRemoteRouteProfiles;
            const QJsonArray files = QString2QJsonArray(resp.data);

            for (const QJsonValue& value : files) {
                const QJsonObject fileObject = value.toObject();

                if (fileObject["type"].toString() != "file")
                    continue;

                QString profile = fileObject["name"].toString();

                if (!profile.endsWith(".json", Qt::CaseInsensitive))
                    continue;

                if (!profile.startsWith("bypass", Qt::CaseInsensitive) &&
                    !profile.startsWith("proxy", Qt::CaseInsensitive))
                    continue;

                profile.chop(5); // удалить .json
                newRemoteRouteProfiles.push_back(profile);
            }

            QMutexLocker locker(&mu_remoteRouteProfiles);
            remoteRouteProfiles = newRemoteRouteProfiles;
        };
    runOnNewThread(getRemoteRouteProfiles);

    // Сброс сохранённых ширин возвращает автоматический расчёт столбцов.
    connect(ui->actionRefresh_Column_Widths, &QAction::triggered, this, [=, this] {
        auto group =
            Configs::dataManager
            ->groupsRepo
            ->CurrentGroup();

        if (!group) {
            return;
        }

        group->ClearColumnWidths();

        Configs::dataManager
            ->groupsRepo
            ->Save(group);

        show_group(
            group->Id()
        );
        });

    // Динамическое меню маршрутизации.
    connect(ui->menuRouting_Menu, &QMenu::aboutToShow, this, [=, this]()
        {
            if (remoteRouteProfiles.isEmpty())
                runOnNewThread(getRemoteRouteProfiles);
            ui->menuRouting_Menu->clear();
            ui->menuRouting_Menu->addAction(ui->menu_routing_settings);

            // Глобальный переключатель добавления правил AdBlock в генерируемую конфигурацию.
            auto* actionAdblock = new QAction(ui->menuRouting_Menu);
            actionAdblock->setText("Enable AdBlock");
            actionAdblock->setCheckable(true);
            actionAdblock->setChecked(Configs::dataManager->settingsRepo->adblock_enable);
            connect(actionAdblock, &QAction::triggered, this, [=, this](bool checked) {
                Configs::dataManager->settingsRepo->adblock_enable = checked;
                actionAdblock->setChecked(checked);
                Configs::dataManager->settingsRepo->Save();
                if (Configs::dataManager->settingsRepo->started_id >= 0) profile_start(Configs::dataManager->settingsRepo->started_id);
                });
            ui->menuRouting_Menu->addAction(actionAdblock);

            // Глобальный переключатель маршрутизации через WARP.
            auto* actionWarp = new QAction(ui->menuRouting_Menu);
            actionWarp->setText("Enable Warp");
            actionWarp->setCheckable(true);
            actionWarp->setChecked(Configs::dataManager->settingsRepo->enable_warp);
            connect(actionWarp, &QAction::triggered, this, [=, this](bool checked) {
                Configs::dataManager->settingsRepo->enable_warp = checked;
                actionWarp->setChecked(checked);
                Configs::dataManager->settingsRepo->Save();
                if (Configs::dataManager->settingsRepo->started_id >= 0) profile_start(Configs::dataManager->settingsRepo->started_id);
                });
            ui->menuRouting_Menu->addAction(actionWarp);

            // Загрузка шаблонов удаленных профилей маршрутизации.
            mu_remoteRouteProfiles.lock();
            if (!remoteRouteProfiles.isEmpty()) {
                QMenu* profilesMenu = ui->menuRouting_Menu->addMenu(QObject::tr("Download Profiles"));
                for (const auto& profile : remoteRouteProfiles)
                {
                    auto* action = new QAction(profilesMenu);
                    action->setText(profile);
                    connect(action, &QAction::triggered, this, [=, this]()
                        {
                            auto resp = NetworkRequestHelper::HttpGet(Configs::get_jsdelivr_link("https://raw.githubusercontent.com/netward/routeprofiles/main/profiles/" + profile + ".json"));
                            if (!resp.error.isEmpty()) {
                                runOnUiThread([=] {
                                    MessageBoxWarning(QObject::tr("Download Profiles"), QObject::tr("Requesting profile error: %1").arg(resp.error + "\n" + resp.data));
                                    });
                                return;
                            }
                            auto err = new QString;
                            auto parsed = Configs::RouteProfile::parseJsonArray(QString2QJsonArray(resp.data), err);
                            if (!err->isEmpty()) {
                                runOnUiThread([=]
                                    {
                                        MessageBoxInfo(tr("Invalid JSON Array"), tr("The provided input cannot be parsed to a valid route rule array:\n") + *err);
                                    });
                                return;
                            }
                            // Создание локальной копии удалённого профиля, доступную для редактирования пользователем.
                            auto chain = Configs::dataManager->routesRepo->NewRouteProfile();
                            chain->name = QString(profile).replace('_', ' ');
                            chain->defaultOutboundID = profile.startsWith("bypass", Qt::CaseInsensitive) ? Configs::proxyID : Configs::directID;
                            chain->Rules.clear();
                            chain->Rules << parsed;
                            Configs::dataManager->routesRepo->AddRouteProfile(chain);
                        });
                    profilesMenu->addAction(action);
                }
            }
            mu_remoteRouteProfiles.unlock();

            // Добавление сохраненных локальных профилей маршрутизации.
            ui->menuRouting_Menu->addSeparator();
            for (const auto& route : Configs::dataManager->routesRepo->GetAllRouteProfiles())
            {
                auto* action = new QAction(ui->menuRouting_Menu);
                action->setText(route->name);
                action->setData(route->id);
                action->setCheckable(true);
                action->setChecked(Configs::dataManager->settingsRepo->current_route_id == route->id);
                connect(action, &QAction::triggered, this, [=, this]()
                    {
                        auto routeID = action->data().toInt();
                        if (Configs::dataManager->settingsRepo->current_route_id == routeID) return;
                        Configs::dataManager->settingsRepo->current_route_id = routeID;
                        Configs::dataManager->settingsRepo->Save();
                        if (Configs::dataManager->settingsRepo->started_id >= 0) profile_start(Configs::dataManager->settingsRepo->started_id);
                    });
                ui->menuRouting_Menu->addAction(action);
            }
        });
    // Тестирование, экспорт и импорт профилей.
    connect(ui->actionClear_Test_Result, &QAction::triggered, this, [=, this]() {
        auto entIDs = get_now_selected_list();
        auto ents = Configs::dataManager->profilesRepo->GetProfileBatch(entIDs);
        if (ents.empty()) return;
        for (const auto& ent : ents) {
            ent->ClearTestResults();
        }
        Configs::dataManager->profilesRepo->SaveBatch(ents);
        if (auto group =
            Configs::dataManager
            ->groupsRepo
            ->GetGroup(
                ents.first()->GroupId()
            ))
        {
            group->ResetCalculatedColumnWidth(
                3
            );
        }
        refresh_proxy_list();
        });
    connect(ui->actionUrl_Test_Selected, &QAction::triggered, this, [=, this]() {
        urltest_current_group(get_now_selected_list());
        });
    connect(
        ui->actionUrl_Test_Group,
        &QAction::triggered,
        this,
        [this]()
        {
            auto group =
                Configs::dataManager
                ->groupsRepo
                ->CurrentGroup();
            if (!group)
            {
                return;
            }
            const auto profileIDs =
                group->Profiles();
            if (profileIDs.isEmpty())
            {
                return;
            }
            urltest_current_group(
                profileIDs
            );
        }
    );
    connect(ui->actionSpeedtest_Current, &QAction::triggered, this, [=, this]()
        {
            if (running != nullptr)
            {
                speedtest_current_group({}, true);
            }
        });
    connect(ui->actionSpeedtest_Selected, &QAction::triggered, this, [=, this]()
        {
            speedtest_current_group(get_now_selected_list());
        });
    connect(
        ui->actionSpeedtest_Group,
        &QAction::triggered,
        this,
        [this]()
        {
            auto group =
                Configs::dataManager
                ->groupsRepo
                ->CurrentGroup();
            if (!group)
            {
                return;
            }
            const auto profileIDs =
                group->Profiles();
            if (profileIDs.isEmpty())
            {
                return;
            }
            speedtest_current_group(
                profileIDs
            );
        }
    );
    connect(ui->actionResolve_Selected_Out_IP, &QAction::triggered, this, [=, this]() {
        iptest_current_group(get_now_selected_list());
        });
    connect(
        ui->actionResolve_Out_IP,
        &QAction::triggered,
        this,
        [this]()
        {
            auto group =
                Configs::dataManager
                ->groupsRepo
                ->CurrentGroup();
            if (!group)
            {
                return;
            }
            const auto profileIDs =
                group->Profiles();
            if (profileIDs.isEmpty())
            {
                return;
            }
            iptest_current_group(
                profileIDs
            );
        }
    );
    connect(ui->menu_stop_testing, &QAction::triggered, this, [=, this]() { stopTests(); });
    // Свойство selected_or_group сообщает общим обработчикам, к чему относится команда меню:
    //  0 — ко всей группе;
    //  1 — только к выделенным профилям;
    //  2 — контекст не определён (меню скрыто).
    auto set_selected_or_group = [=, this](int mode) {
        ui->menu_server->setProperty("selected_or_group", mode);
        };
    connect(ui->menu_server, &QMenu::aboutToHide, this, [=, this] {
        setTimeout([=, this] { set_selected_or_group(2); }, this, 200);
        });
    set_selected_or_group(2);
    // Состав меню экспорта зависит от числа и типа выделенных профилей.
    connect(ui->menu_share_item, &QMenu::aboutToShow, this, [=, this] {
        QString name;
        auto selected = get_now_selected_list();

        ui->menu_export_config->setVisible(false);
        ui->actionExport_Xray_config->setVisible(false);
        if (selected.isEmpty()) return;

        auto profile = Configs::dataManager->profilesRepo->GetProfile(selected.first());
        if (!profile) return;

        if (selected.count() == 1 && profile->DisplayTestResult().trimmed().isEmpty()) {
            ui->actionCopy_Test_Result->setVisible(false);
        }
        else {
            ui->actionCopy_Test_Result->setVisible(true);
        }

        ui->menu_export_config->setVisible(true);

        const auto outbound =
            profile->OutboundSnapshot();

        if ((outbound &&
            outbound->IsXray()) ||
            profile->Type() == "chain")
        {
            ui->actionExport_Xray_config
                ->setVisible(true);
        }
        });
    // Генерация конфигурации ядра для единственного выбранного профиля, предложение скопировать основной/тестовый вариант.
    connect(ui->actionExport_Xray_config, &QAction::triggered, this, [=, this]() {
        auto ents = get_now_selected_list();
        if (ents.count() != 1) return;
        auto ent = Configs::dataManager->profilesRepo->GetProfile(ents.first());

        auto result = Configs::BuildSingBoxConfig(ent);
        if (!result->error.isEmpty()) {
            MessageBoxWarning("Build config error", result->error);
            return;
        }
        QString config_core = QJsonObject2QString(result->xrayConfig, true);
        QApplication::clipboard()->setText(config_core);

        QMessageBox msg(QMessageBox::Information, tr("Config copied"), config_core);
        QPushButton* button_1 = msg.addButton(tr("Copy core config"), QMessageBox::YesRole);
        QPushButton* button_2 = msg.addButton(tr("Copy test config"), QMessageBox::YesRole);
        msg.addButton(QMessageBox::Ok);
        msg.setEscapeButton(QMessageBox::Ok);
        msg.setDefaultButton(QMessageBox::Ok);
        msg.exec();
        if (msg.clickedButton() == button_1) {
            QApplication::clipboard()->setText(config_core);
        }
        else if (msg.clickedButton() == button_2) {
            auto res = Configs::BuildTestConfig({ ent });
            if (!res->error.isEmpty()) {
                MessageBoxWarning("Build Test config error", res->error);
                return;
            }
            config_core = QJsonObject2QString(res->xrayConfig, true);
            QApplication::clipboard()->setText(config_core);
        }
        });
    // Сборка непустых результатов тестирования в один текстовый блок.
    // Ограничение в 1000 профилей защищает интерфейс от слишком тяжёлой операции.
    connect(ui->actionCopy_Test_Result, &QAction::triggered, this, [=, this]() {
        auto ents = get_now_selected_list();
        if (ents.count() == 0 || ents.count() > 1000) return;
        auto entList = Configs::dataManager->profilesRepo->GetProfileBatch(ents);
        QString res;
        int counter = 0;
        for (auto ent : entList) {
            auto testRes = ent->DisplayTestResult();
            if (!testRes.trimmed().isEmpty()) {
                res += testRes.trimmed() + "\n";
                counter++;
            }
        }
        QApplication::clipboard()->setText(res);
        MW_show_log(QString::number(counter) + tr(" Test result(s) copied to clipboard!"));
        });
    // Импорт подписки/списка профилей из локального файла.
    connect(ui->actionAdd_profile_from_File, &QAction::triggered, this, [=, this]()
        {
            auto path = QFileDialog::getOpenFileName();
            if (path.isEmpty())
            {
                return;
            }
            auto file = QFile(path);
            if (!file.exists()) return;
            if (file.size() > 50 * 1024 * 1024) {
                MW_show_log("File too large, will not process it");
                return;
            }
            if (!file.open(QIODevice::ReadOnly)) return;
            auto contents = file.readAll();
            file.close();
            Subscription::groupUpdater->AsyncUpdate(contents);
        });

    // Сохранение состояния и периодические таймеры.
    connect(qApp, &QGuiApplication::commitDataRequest, this, &MainWindow::on_commitDataRequest);
    // Синхронизация индикаторов состояния с настройками каждые 2 секунды.
    auto t = new QTimer;
    connect(t, &QTimer::timeout, this, [=, this]() { refresh_status(); });
    t->start(2000);
    // Счётчик журналов сбрасывается каждую секунду и используется для ограничения или расчёта частоты сообщений.
    t = new QTimer;
    connect(t, &QTimer::timeout, this, [&] { Configs_sys::logCounter.fetchAndStoreRelaxed(0); });
    t->start(1000);

    // Отложенное обновление таблицы профилей.
    // Множественные события изменения шрифта, темы или размера окна могут приходить подряд. 
    // Одноразовый таймер объединяет их в одно обновление через 200 мс и предотвращает лишние перерасчёты таблицы.
    m_proxyListRefreshDebounce = new QTimer(this);
    m_proxyListRefreshDebounce->setSingleShot(true);
    connect(m_proxyListRefreshDebounce, &QTimer::timeout, this, [this] { refresh_proxy_list({}, false); });

    // Периодическое обновление подписок.
    TM_auto_update_subsctiption = new QTimer;
    TM_auto_update_subsctiption_Reset_Minute = [&](int m) {
        TM_auto_update_subsctiption->stop();
        if (m >= 30) TM_auto_update_subsctiption->start(m * 60 * 1000);
        };
    connect(TM_auto_update_subsctiption, &QTimer::timeout, this, [&] { UI_update_all_groups(true); });
    TM_auto_update_subsctiption_Reset_Minute(Configs::dataManager->settingsRepo->sub_auto_update);

    // Отображение главного окна при отсутствии флага -tray.
    // В противном случае окно остаётся скрытым, а управление доступно из трея.
    if (!Configs::dataManager->settingsRepo->flag_tray) show();
    // HTML-представление статистики встраивается без собственного фона и рамки, чтобы совпадать с оформлением родительской панели.
    ui->data_view->setStyleSheet("background: transparent; border: none;");
}

// Обработка попытки закрытия главного окна Gryph.
void MainWindow::closeEvent(QCloseEvent* event) {
    if (tray->isVisible()) {
        HideWindow(this);
        event->ignore();
    }
    else {
        on_menu_exit_triggered();
    }
}

// Настройка шрифта окна журнала.
void MainWindow::applyLogBrowserFont() {
    QFont logFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    int pt = qApp->font().pointSize();
    if (pt <= 0) pt = Configs::dataManager->settingsRepo->font_size;
    if (pt > 0) logFont.setPointSize(pt);
    ui->masterLogBrowser->setFont(logFont);
}

void MainWindow::changeEvent(QEvent* event) {
    if (event->type() == QEvent::FontChange) {
        // masterLogBrowser keeps its monospace family but follows the user's point size
        applyLogBrowserFont();

        // Widgets with per-widget stylesheets (set in the .ui files — tabWidgets, toolButtons,
        // etc.) get wrapped in QStyleSheetStyle, which caches font-dependent metrics like tab
        // size hints and button paddings. Those caches don't invalidate on FontChange, so the
        // visible size stays at the old font. Toggling the stylesheet through "" forces
        // QStyleSheetStyle::repolish, which clears the cache and re-evaluates rules.
        auto refreshStylesheetCache = [](QWidget* w) {
            QString ss = w->styleSheet();
            if (ss.isEmpty()) return;
            w->setStyleSheet("");
            w->setStyleSheet(ss);
            };
        const auto allChildren = findChildren<QWidget*>();
        for (QWidget* w : allChildren) {
            refreshStylesheetCache(w);
        }

        // profilesTableView has no per-widget stylesheet, so the stylesheet trick above
        // doesn't apply. Toggle its font through a different point size to force a real
        // FontChange (Qt skips setFont when the resolved font is unchanged), then return
        // to inheriting from qApp so future changes still propagate. Both updates coalesce.
        auto forceFontReapply = [](QWidget* w) {
            if (!w) return;
            QFont currentFont = QApplication::font();
            QFont diffFont = currentFont;
            diffFont.setPointSize(currentFont.pointSize() + 1);
            w->setFont(diffFont);
            w->setFont(QFont());
            w->updateGeometry();
            };
        forceFontReapply(ui->profilesTableView);
    }
    if (event->type() == QEvent::FontChange ||
        event->type() == QEvent::PaletteChange ||
        event->type() == QEvent::StyleChange) {
        scheduleProxyListRefresh();
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    scheduleProxyListRefresh();
}

void MainWindow::scheduleProxyListRefresh() {
    if (m_proxyListRefreshDebounce) m_proxyListRefreshDebounce->start(200);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls() || event->mimeData()->hasText()) {
        event->acceptProposedAction();
    }
    else {
        event->ignore();
    }
}

void MainWindow::dropEvent(QDropEvent* event)
{
    auto mimeData = event->mimeData();

    if (mimeData->hasUrls()) {
        QList<QUrl> urlList = mimeData->urls();
        for (const QUrl& url : urlList) {
            if (url.isLocalFile()) {
                if (auto qpx = QPixmap(url.toLocalFile()); !qpx.isNull())
                {
                    parseQrImage(&qpx);
                }
                else if (auto file = QFile(url.toLocalFile()); file.exists() && file.open(QFile::ReadOnly))
                {
                    if (file.size() > 50 * 1024 * 1024)
                    {
                        file.close();
                        MW_show_log("File size is larger than 50MB, will not parse it");
                        event->acceptProposedAction();
                        return;
                    }
                    auto contents = file.readAll();
                    file.close();
                    Subscription::groupUpdater->AsyncUpdate(contents);
                }
            }
        }
        event->acceptProposedAction();
        return;
    }

    if (mimeData->hasText()) {
        import_or_handle_deeplink(mimeData->text());
        event->acceptProposedAction();
        return;
    }

    event->ignore();
}

MainWindow::~MainWindow() {
    delete ui;
}

// Group tab manage

inline int tabIndex2GroupId(int index) {
    auto tabOrder = Configs::dataManager->groupsRepo->GetGroupsTabOrder();
    if (tabOrder.length() <= index) return -1;
    return tabOrder[index];
}

inline int groupId2TabIndex(int gid) {
    auto tabOrder = Configs::dataManager->groupsRepo->GetGroupsTabOrder();
    for (int key = 0; key < tabOrder.count(); key++) {
        if (tabOrder[key] == gid) return key;
    }
    return 0;
}

void MainWindow::on_tabWidget_currentChanged(int index) {
    if (Configs::dataManager->settingsRepo->refreshing_group_list) return;
    auto gid = tabIndex2GroupId(index);
    if (gid == Configs::dataManager->settingsRepo->current_group) return;
    show_group(gid);
}

void MainWindow::show_group(int gid) {
    if (Configs::dataManager->settingsRepo->refreshing_group) return;
    Configs::dataManager->settingsRepo->refreshing_group = true;

    auto group = Configs::dataManager->groupsRepo->GetGroup(gid);
    if (group == nullptr) {
        MessageBoxWarning(tr("Error"), QString("No such group: %1").arg(gid));
        Configs::dataManager->settingsRepo->refreshing_group = false;
        return;
    }

    if (Configs::dataManager->settingsRepo->current_group != gid) {
        saveProfileFocusState();
        if (auto lastGroup = Configs::dataManager->groupsRepo->CurrentGroup()) {
            lastGroup->SetScrollLastProfile(
                ui->profilesTableView
                ->firstVisibleRow()
            );
            Configs::dataManager->groupsRepo->Save(lastGroup);
        }
        Configs::dataManager->settingsRepo->current_group = gid;
        Configs::dataManager->settingsRepo->Save();
    }

    ui->tabWidget->widget(groupId2TabIndex(gid))->layout()->addWidget(ui->profilesTableView);

    // show proxies
    refresh_proxy_list({}, true);

    int rowCount = profilesTableModel->rowCount();
    const auto groupSnapshot =
        group->Snapshot();
    int targetRow =
        groupSnapshot.scroll_last_profile;
    if (targetRow >= rowCount && rowCount > 0) targetRow = rowCount - 1;
    QTimer::singleShot(0, ui->profilesTableView, [=, this]() {
        if (targetRow >= 0) {
            if (QModelIndex idx = profilesTableModel->index(targetRow, 0); idx.isValid()) {
                ui->profilesTableView->scrollTo(idx, QAbstractItemView::PositionAtTop);
            }
        }
        refresh_proxy_list_column_size();
        });

    Configs::dataManager->settingsRepo->refreshing_group = false;
}

// callback

void MainWindow::handle_deeplink_impl(const QString& url) {
    const QUrl u(url);
    // QUrl lowercases the host, so "Gryph://AddSub/" arrives with host "addsub".
    const QString cmd = u.host();
    const QUrlQuery q(u);

    if (cmd.compare("addsub", Qt::CaseInsensitive) == 0) {
        const QString subUrl = q.queryItemValue("url", QUrl::FullyDecoded);
        const QString name = q.queryItemValue("name", QUrl::FullyDecoded);
        const QString autoUpdateRaw = q.queryItemValue("autoupdate", QUrl::FullyDecoded).trimmed().toLower();
        // Default ON when the param is absent (matches normal subscription behavior).
        const bool autoUpdate = autoUpdateRaw.isEmpty() || autoUpdateRaw == "1"
            || autoUpdateRaw == "true" || autoUpdateRaw == "on" || autoUpdateRaw == "yes";
        handle_addsub(subUrl, name, autoUpdate);
        return;
    }

    if (cmd.compare("route", Qt::CaseInsensitive) == 0) {
        handle_import_route(url);
        return;
    }

    MW_show_log(tr("Ignored deeplink with unknown command: %1").arg(cmd));
}

void MainWindow::handle_import_route(const QString& url) {
    QString fatal, warnings;
    bool wasOldArray = false;
    auto profile = Configs::RouteProfile::FromShareInput(url, &fatal, &warnings, &wasOldArray);
    if (!profile) {
        MessageBoxWarning(tr("Import routing profile"), tr("The link could not be parsed:\n") + fatal);
        return;
    }
    if (profile->name.trimmed().isEmpty()) profile->name = tr("Imported profile");

    ActivateWindow(this);

    auto prompt = tr("Add this routing profile?\n\nName: %1").arg(profile->name);
    if (!warnings.isEmpty()) prompt += "\n\n" + tr("Note:") + "\n" + warnings.trimmed();
    if (QMessageBox::question(GetMessageBoxParent(), tr("Import routing profile"), prompt) != QMessageBox::StandardButton::Yes) {
        return;
    }

    Configs::dataManager->routesRepo->AddRouteProfile(profile);
}

void MainWindow::handle_addsub(
    const QString& url,
    const QString& name,
    bool autoUpdate)
{
    if (url.isEmpty())
    {
        MessageBoxWarning(
            tr("Add subscription"),
            tr(
                "The link did not contain "
                "a subscription URL."
            )
        );

        return;
    }

    ActivateWindow(this);

    const QString groupName =
        FIRST_OR_SECOND(
            name,
            QUrl(url).host()
        );

    const auto prompt =
        tr(
            "Add this subscription?\n\n"
            "Name: %1\n"
            "URL: %2\n"
            "Auto update: %3"
        )
        .arg(
            groupName,
            url,
            autoUpdate
            ? tr("On")
            : tr("Off")
        );

    if (QMessageBox::question(
        GetMessageBoxParent(),
        tr("Add subscription"),
        prompt
    )
        != QMessageBox::StandardButton::Yes)
    {
        return;
    }

    auto group =
        Configs::GroupsRepo::NewGroup();

    group->SetSubscriptionSource(
        groupName,
        url
    );

    group->SetSkipAutoUpdate(
        !autoUpdate
    );

    if (!Configs::dataManager
        ->groupsRepo
        ->AddGroup(group))
    {
        return;
    }

    const int groupId =
        group->Id();

    refresh_groups();

    Subscription::groupUpdater
        ->AsyncUpdate(
            url,
            groupId
        );
}

void MainWindow::import_or_handle_deeplink(const QString& text) {
    if (const QString trimmed = text.trimmed(); trimmed.startsWith("Gryph://")) {
        handle_deeplink_impl(trimmed);
        return;
    }
    Subscription::groupUpdater->AsyncUpdate(text);
}

void MainWindow::dialog_message_impl(MwMessage cmd, const QStringList& args) {
    const auto changed = [&](const QString& flag) { return args.contains(flag); };
    auto& settings = Configs::dataManager->settingsRepo;

    switch (cmd) {
    case MwMessage::UpdateSettings: {
        updateLogFilterFields();
        if (changed(MwArg::TrayIcon)) {
            icon_status = -1;
        }
        if (changed(MwArg::MaxLogLines)) {
            qvLogDocument->setMaximumBlockCount(settings->max_log_line);
        }
        if (changed(MwArg::DisableTray)) {
            tray->setVisible(!settings->disable_tray);
        }
        if (changed(MwArg::SystemDns)) {
            if (settings->show_system_dns) ui->system_dns->show();
            else ui->system_dns->hide();
        }
        if (changed(MwArg::ChoosePort)) {
            settings->inbound_socks_port = MkPort();
            if (settings->spmode_system_proxy) {
                set_spmode_system_proxy(false);
                set_spmode_system_proxy(true);
            }
        }
        if (changed(MwArg::DisableAdmin)) {
            AutoRun_FixPrivilegeIfNeeded();
        }
        auto suggestRestartProxy = settings->Save();
        if (changed(MwArg::Route)) {
            settings->Save();
            suggestRestartProxy = true;
        }
        if (changed(MwArg::NeedRestart)) {
            suggestRestartProxy = false;
        }
        if (changed(MwArg::Vpn) && settings->spmode_vpn) {
            MessageBoxWarning(tr("Tun Settings changed"), tr("Restart Tun to take effect."));
        }
        if ((changed(MwArg::ChoosePort) || suggestRestartProxy) && settings->started_id >= 0 &&
            QMessageBox::question(GetMessageBoxParent(), tr("Confirmation"), tr("Settings changed, restart proxy?")) == QMessageBox::StandardButton::Yes) {
            profile_start(settings->started_id);
        }
        refresh_status();
        if (changed(MwArg::NeedRestart) &&
            QMessageBox::warning(GetMessageBoxParent(), tr("Settings changed"), tr("Restart the program to take effect."), QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
            this->exit_reason = 2;
            on_menu_exit_triggered();
        }
        break;
    }
    case MwMessage::RestartProgram:
        this->exit_reason = 2;
        on_menu_exit_triggered();
        break;
    case MwMessage::Raise:
        ActivateWindow(this);
        break;
    case MwMessage::UpdateShortcuts:
        loadShortcuts();
        break;
    case MwMessage::ProfileChanged:
        refresh_proxy_list({}, true);
        if (changed(MwArg::RestartProxy) &&
            QMessageBox::question(GetMessageBoxParent(), tr("Confirmation"), tr("Settings changed, restart proxy?")) == QMessageBox::StandardButton::Yes) {
            profile_start(settings->started_id);
        }
        break;
    case MwMessage::GroupsChanged:
        refresh_groups();
        break;
    case MwMessage::SubscriptionFinished:
        refresh_proxy_list({}, true);
        if (!changed(MwArg::Quiet)) {
            MW_show_log(tr("Imported %1 profile(s)").arg(settings->imported_count));
        }
        break;
    case MwMessage::SubscriptionNewGroup:
        refresh_groups();
        break;
    case MwMessage::CoreCrashed:
        profile_stop();
        break;
    case MwMessage::CoreStarted:
        Configs::IsAdmin(true);
        if (settings->remember_system_proxy) {
            set_spmode_system_proxy(true, false);
        }
        if (settings->remember_tun || settings->flag_restart_tun_on) {
            set_spmode_vpn(true, false);
        }
        if (settings->flag_dns_set) {
            set_system_dns(true);
        }
        if (auto id = args.value(0).toInt(); id >= 0) {
            profile_start(id);
        }
        if (settings->system_dns_set) {
            set_system_dns(true);
            ui->system_dns->setChecked(true);
        }
        refresh_status();
        break;
    }
}

// top bar & tray menu

inline bool dialog_is_using = false;

#define USE_DIALOG(a)                               \
    if (dialog_is_using) return;                    \
    dialog_is_using = true;                         \
    auto dialog = new a(this);                      \
    connect(dialog, &QDialog::finished, this, [=,this] { \
        dialog->deleteLater();                      \
        dialog_is_using = false;                    \
    });                                             \
    dialog->show();

void MainWindow::on_menu_basic_settings_triggered() {
    USE_DIALOG(DialogBasicSettings)
}

void MainWindow::on_menu_manage_groups_triggered() {
    USE_DIALOG(DialogManageGroups)
}

void MainWindow::on_menu_routing_settings_triggered() {
    if (dialog_is_using) return;
    dialog_is_using = true;
    auto dialog = new DialogManageRoutes(this);
    connect(dialog, &QDialog::finished, this, [=, this] {
        dialog->deleteLater();
        dialog_is_using = false;
        });
    dialog->show();
}

void MainWindow::on_menu_vpn_settings_triggered() {
    USE_DIALOG(DialogVPNSettings)
}

void MainWindow::on_menu_hotkey_settings_triggered() {
    if (dialog_is_using) return;
    dialog_is_using = true;
    auto dialog = new DialogHotkey(this, getActionsForShortcut());
    connect(dialog, &QDialog::finished, this, [=, this]
        {
            dialog->deleteLater();
            dialog_is_using = false;
        });
    dialog->show();
}

void MainWindow::on_commitDataRequest() {
    qDebug() << "Start of data save";

    auto* settings = Configs::dataManager->settingsRepo.get();

    settings->mainWindowGeometry = this->saveGeometry().toBase64(QByteArray::Base64Encoding);
    if (!isMaximized()) {
        auto news = QString("%1x%2").arg(size().width()).arg(size().height());
        if (settings->mw_size != news) settings->mw_size = news;
    }
    settings->splitter_state = ui->splitter->saveState().toBase64();

    // Snapshot the live app state on exit so "remember last proxy" restores it
    // on the next launch. Capturing it here, rather than when each toggle
    // happens, makes the result independent of the order in which the user
    // toggled the proxy/tun modes vs. the remember option itself.
    if (settings->remember_enable) {
        if (settings->started_id >= 0) settings->remember_id = settings->started_id;
        settings->remember_system_proxy = settings->spmode_system_proxy;
        settings->remember_tun = settings->spmode_vpn;
    }
    else {
        settings->remember_system_proxy = false;
        settings->remember_tun = false;
    }

    settings->Save();
    qDebug() << "End of data save";
}

void MainWindow::prepare_exit()
{
    qDebug() << "prepare for exit...";
    mu_exit.lock();
    if (Configs::dataManager->settingsRepo->prepare_exit)
    {
        qDebug() << "prepare exit had already succeeded, ignoring...";
        mu_exit.unlock();
        return;
    }
    Configs::dataManager->settingsRepo->prepare_exit = true;
    //
    set_system_proxy(false);
    if (Configs::dataManager->settingsRepo->system_dns_set) set_system_dns(false, false);
    RegisterHiddenMenuShortcuts(true);
    RegisterHotkey(true);
    //
    on_commitDataRequest();
    //
    Configs::dataManager->settingsRepo->noSave = true; // don't change Configs::dataManager->settingsRepo after this line
    profile_stop(false, true);

    runOnThread([=, this]()
        {
            core_process->Kill();
        }, DS_cores, true);
    HideWindow(this);

    mu_exit.unlock();
    qDebug() << "prepare exit done!";
}

void MainWindow::on_menu_exit_triggered() {
    prepare_exit();
    //
    if (exit_reason == 1) {
        QDir::setCurrent(QApplication::applicationDirPath());
#ifdef Q_OS_WIN
        QFile::copy("./updater.exe", "./updater.old");
        QProcess::startDetached("./updater.old", QStringList{});
#else
        QProcess::startDetached("./updater", QStringList{});
#endif
    }
    else if (exit_reason == 2 || exit_reason == 3 || exit_reason == 4) {
        QDir::setCurrent(QApplication::applicationDirPath());

        auto arguments = Configs::dataManager->settingsRepo->argv;
        if (arguments.length() > 0) {
            arguments.removeFirst();
            arguments.removeAll("-tray");
            arguments.removeAll("-flag_restart_tun_on");
            arguments.removeAll("-flag_restart_dns_set");
        }
        auto program = QApplication::applicationFilePath();

        if (exit_reason == 3 || exit_reason == 4) {
            if (exit_reason == 3) arguments << "-flag_restart_tun_on";
            if (exit_reason == 4) arguments << "-flag_restart_dns_set";
#ifdef Q_OS_WIN
            WinCommander::runProcessElevated(program, arguments, "", 1, false);
#else
            QProcess::startDetached(program, arguments);
#endif
        }
        else {
            QProcess::startDetached(program, arguments);
        }
    }
    QCoreApplication::quit();
}

void MainWindow::toggle_system_proxy() {
    auto currentState = Configs::dataManager->settingsRepo->spmode_system_proxy;
    if (currentState) {
        set_spmode_system_proxy(false);
    }
    else {
        set_spmode_system_proxy(true);
    }
}

bool MainWindow::get_elevated_permissions(int reason) {
    if (Configs::dataManager->settingsRepo->disable_privilege_req)
    {
        MW_show_log(tr("User opted for no privilege req, some features may not work"));
        return true;
    }
    if (Configs::IsAdmin()) return true;
#ifdef Q_OS_LINUX
    if (!Linux_HavePkexec()) {
        MessageBoxWarning(software_name, "Please install \"pkexec\" first.");
        return false;
    }
    auto n = QMessageBox::warning(GetMessageBoxParent(), software_name, tr("Please give the core root privileges"), QMessageBox::Yes | QMessageBox::No);
    if (n == QMessageBox::Yes) {
        runOnNewThread([=, this]
            {
                auto chownArgs = QString("root:root " + Configs::FindCoreRealPath());
                auto ret = Linux_Run_Command("chown", chownArgs);
                if (ret != 0) {
                    MW_show_log(QString("Failed to run chown %1 code is %2").arg(chownArgs).arg(ret));
                }
                auto chmodArgs = QString("u+s " + Configs::FindCoreRealPath());
                ret = Linux_Run_Command("chmod", chmodArgs);
                if (ret == 0) {
                    StopVPNProcess();
                }
                else {
                    MW_show_log(QString("Failed to run chmod %1").arg(chmodArgs));
                }
            });
        return false;
    }
#endif
#ifdef Q_OS_WIN
    auto n = QMessageBox::warning(GetMessageBoxParent(), software_name, tr("Please run Gryph as admin"), QMessageBox::Yes | QMessageBox::No);
    if (n == QMessageBox::Yes) {
        this->exit_reason = reason;
        on_menu_exit_triggered();
    }
#endif

#ifdef Q_OS_MACOS
    if (Configs::isSetuidSet(Configs::FindCoreRealPath().toStdString()))
    {
        StopVPNProcess();
        return true;
    }
    auto n = QMessageBox::warning(GetMessageBoxParent(), software_name, tr("Please give the core root privileges"), QMessageBox::Yes | QMessageBox::No);
    if (n == QMessageBox::Yes)
    {
        auto Command = QString("sudo chown root:wheel '%1' && sudo chmod u+s '%1'").arg(Configs::FindCoreRealPath());
        auto ret = Mac_Run_Command(Command);
        if (ret == 0) {
            MessageBoxInfo(tr("Requesting permission"), tr("Please Enter your password in the opened terminal, then try again"));
            return false;
        }
        else {
            MW_show_log(QString("Failed to run %1 with %2").arg(Command).arg(ret));
            return false;
        }
    }
#endif
    return false;
}

void MainWindow::set_system_proxy(bool enable) {
    if (enable) {
        auto socks_port = Configs::dataManager->settingsRepo->inbound_socks_port;
        SetSystemProxy(socks_port, socks_port, Configs::dataManager->settingsRepo->proxy_scheme);
    }
    else {
        ClearSystemProxy();
    }
}

void MainWindow::set_spmode_system_proxy(bool enable, bool save) {
    if (enable && Configs::dataManager->settingsRepo->disable_mixed_inbound) {
        runOnUiThread([=] {
            MessageBoxWarning("Invalid Operation", "Cannot set system proxy when mixed inbound is disabled.");
            });
        ui->checkBox_SystemProxy->setChecked(false);
        return;
    }
    Configs::dataManager->settingsRepo->spmode_system_proxy = enable;
    if (running) {
        set_system_proxy(enable);
        if (!enable && Configs::dataManager->settingsRepo->reset_proxy_on_disable_sp) {
            profile_start(running->Id());
        }
    }

    if (save) {
        Configs::dataManager->settingsRepo->Save();
    }

    refresh_status();
}

void MainWindow::set_spmode_vpn(bool enable, bool save) {
    if (enable == Configs::dataManager->settingsRepo->spmode_vpn) return;

    if (enable) {
        bool requestPermission = !Configs::IsAdmin();
        if (requestPermission) {
            if (!get_elevated_permissions()) {
                refresh_status();
                return;
            }
        }
    }

    if (save) {
        Configs::dataManager->settingsRepo->Save();
    }

    Configs::dataManager->settingsRepo->spmode_vpn = enable;
    refresh_status();

    if (Configs::dataManager->settingsRepo->started_id >= 0) profile_start(Configs::dataManager->settingsRepo->started_id);
}

void MainWindow::UpdateDataView(
    bool force)
{
    runOnUiThread(
        [this, force]()
        {
            const auto now =
                QDateTime::
                currentDateTime();


            if (!force &&
                lastUpdated
                .msecsTo(now) < 100)
            {
                return;
            }


            const QString html =
                dataViewHtmlGenerator_
                .buildHtml();


            ui->data_view
                ->setHtml(
                    html
                );


            lastUpdated =
                now;
        },

        true
    );
}

void MainWindow::setDownloadReport(const DownloadProgressReport& report, bool show)
{
    dataViewHtmlGenerator_.setDownloadReport(report, show);
}


void MainWindow::setupConnectionList()
{
    ui->connections->horizontalHeader()->setHighlightSections(false);
    ui->connections->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->connections->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->connections->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    ui->connections->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    ui->connections->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    ui->connections->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    ui->connections->verticalHeader()->hide();
    connect(ui->connections, &QTableWidget::cellClicked, this, [=, this](int row, int column)
        {
            if (column > 3) return;
            auto selected = ui->connections->item(row, column);
            QApplication::clipboard()->setText(selected->text());
            QPoint pos = ui->connections->mapToGlobal(ui->connections->visualItemRect(selected).center());
            QToolTip::showText(pos, "Copied!", this);
            auto r = ++toolTipID;
            QTimer::singleShot(1500, [=, this] {
                if (r != toolTipID)
                {
                    return;
                }
                QToolTip::hideText();
                });
        });
}

void MainWindow::UpdateConnectionList(const QMap<QString, Stats::ConnectionMetadata>& toUpdate, const QMap<QString, Stats::ConnectionMetadata>& toAdd)
{
    connectionListMu.lock();
    ui->connections->setUpdatesEnabled(false);
    for (int row = 0; row < ui->connections->rowCount(); row++)
    {
        auto key = ui->connections->item(row, 0)->data(Stats::IDKEY).toString();
        if (!toUpdate.contains(key))
        {
            ui->connections->removeRow(row);
            row--;
            continue;
        }

        auto conn = toUpdate[key];
        // C0: Dest (Domain)
        ui->connections->item(row, 0)->setText(DisplayDest(conn.dest, conn.domain));

        // C1: Process
        ui->connections->item(row, 1)->setText(conn.process);

        // C2: Protocol
        auto prot = conn.network;
        if (!conn.protocol.isEmpty()) prot += " (" + conn.protocol + ")";
        ui->connections->item(row, 2)->setText(prot);

        // C3: Outbound
        ui->connections->item(row, 3)->setText(conn.outbound);

        // C4: Traffic
        ui->connections->item(row, 4)->setText(ReadableSize(conn.upload) + "↑" + " " + ReadableSize(conn.download) + "↓");
    }
    int row = ui->connections->rowCount();
    for (const auto& conn : toAdd)
    {
        ui->connections->insertRow(row);
        auto f0 = std::make_unique<QTableWidgetItem>();
        f0->setData(Stats::IDKEY, conn.id);

        // C0: Dest (Domain)
        auto f = f0->clone();
        f->setText(DisplayDest(conn.dest, conn.domain));
        ui->connections->setItem(row, 0, f);

        // C1: Process
        f = f0->clone();
        f->setText(conn.process);
        ui->connections->setItem(row, 1, f);

        // C2: Protocol
        f = f0->clone();
        auto prot = conn.network;
        if (!conn.protocol.isEmpty()) prot += " (" + conn.protocol + ")";
        f->setText(prot);
        ui->connections->setItem(row, 2, f);

        // C3: Outbound
        f = f0->clone();
        f->setText(conn.outbound);
        ui->connections->setItem(row, 3, f);

        // C4: Traffic
        f = f0->clone();
        f->setText(ReadableSize(conn.upload) + "↑" + " " + ReadableSize(conn.download) + "↓");
        ui->connections->setItem(row, 4, f);

        row++;
    }
    ui->connections->setUpdatesEnabled(true);
    connectionListMu.unlock();
}

void MainWindow::UpdateConnectionListWithRecreate(const QList<Stats::ConnectionMetadata>& connections)
{
    connectionListMu.lock();
    ui->connections->setUpdatesEnabled(false);
    ui->connections->setRowCount(0);
    int row = 0;
    for (const auto& conn : connections)
    {
        ui->connections->insertRow(row);
        auto f0 = std::make_unique<QTableWidgetItem>();
        f0->setData(Stats::IDKEY, conn.id);

        // C0: Dest (Domain)
        auto f = f0->clone();
        f->setText(DisplayDest(conn.dest, conn.domain));
        ui->connections->setItem(row, 0, f);

        // C1: Process
        f = f0->clone();
        f->setText(conn.process);
        ui->connections->setItem(row, 1, f);

        // C2: Protocol
        f = f0->clone();
        auto prot = conn.network;
        if (!conn.protocol.isEmpty()) prot += " (" + conn.protocol + ")";
        f->setText(prot);
        ui->connections->setItem(row, 2, f);

        // C3: Outbound
        f = f0->clone();
        f->setText(conn.outbound);
        ui->connections->setItem(row, 3, f);

        // C4: Traffic
        f = f0->clone();
        f->setText(ReadableSize(conn.upload) + "↑" + " " + ReadableSize(conn.download) + "↓");
        ui->connections->setItem(row, 4, f);

        row++;
    }
    ui->connections->setUpdatesEnabled(true);
    connectionListMu.unlock();
}

void MainWindow::updateLogFilterFields() {
    QMutexLocker locker(&logMutex);
    includeKeywords.clear();
    excludeKeywords.clear();
    for (const auto& inKeyword : Configs::dataManager->settingsRepo->log_include_keyword) includeKeywords.append(inKeyword);
    for (const auto& exKeyword : Configs::dataManager->settingsRepo->log_exclude_keyword) excludeKeywords.append(exKeyword);
    includeCombined.setPattern(Configs::dataManager->settingsRepo->log_include_regex.join("|"));
    excludeCombined.setPattern(Configs::dataManager->settingsRepo->log_exclude_regex.join("|"));
    includeCombined.optimize();
    excludeCombined.optimize();
}

QList<int>
MainWindow::filterProfilesList(
    const QList<int>& profileIDs)
{
    if (addressFilterString.isEmpty() &&
        nameFilterString.isEmpty() &&
        typeFilterString.isEmpty() &&
        countryFilterString.isEmpty())
    {
        return profileIDs;
    }


    QList<int> result;


    const auto profiles =
        Configs::dataManager
        ->profilesRepo
        ->GetProfileBatch(
            profileIDs
        );


    for (const auto& profile :
        profiles)
    {
        if (!profile)
        {
            continue;
        }


        const auto config =
            profile->ConfigSnapshot();

        const auto test =
            profile->TestSnapshot();

        const auto outbound =
            profile->OutboundSnapshot();


        if (!outbound)
        {
            continue;
        }


        const auto portMatches =
            [&]() -> bool
            {
                const QString value =
                    addressFilterString.mid(5);


                if (!value.contains(':'))
                {
                    return
                        !value.isEmpty() &&
                        outbound->server_port ==
                        value.toInt();
                }


                const QStringList parts =
                    value.split(':');


                const bool minOk =
                    parts[0].isEmpty() ||
                    outbound->server_port >=
                    parts[0].toInt();


                const bool maxOk =
                    parts.size() < 2 ||
                    parts[1].isEmpty() ||
                    outbound->server_port <=
                    parts[1].toInt();


                return minOk && maxOk;
            };


        const bool addressMatches =
            addressFilterString.isEmpty()
            ||
            (
                addressFilterString
                .startsWith("port=")
                ? portMatches()
                : outbound
                ->server
                .contains(
                    addressFilterString,
                    Qt::CaseInsensitive
                )
                );


        const bool nameMatches =
            nameFilterString.isEmpty()
            ||
            config.name.contains(
                nameFilterString,
                Qt::CaseInsensitive
            );


        const bool typeMatches =
            typeFilterString.isEmpty()
            ||
            config.type.contains(
                typeFilterString,
                Qt::CaseInsensitive
            );


        const bool countryMatches =
            countryFilterString.isEmpty()
            ||
            test.testCountry.contains(
                countryFilterString,
                Qt::CaseInsensitive
            );


        if (addressMatches &&
            nameMatches &&
            typeMatches &&
            countryMatches)
        {
            result.append(
                config.id
            );
        }
    }


    return result;
}

void MainWindow::refresh_status(const QString& traffic_update) {
    auto refresh_speed_label = [=, this] {
        if (Configs::dataManager->settingsRepo->disable_traffic_stats) {
            ui->label_speed->setText("");
        }
        else if (traffic_update_cache == "") {
            ui->label_speed->setText(QObject::tr("Proxy: %1\nDirect: %2").arg("", ""));
        }
        else {
            ui->label_speed->setText(traffic_update_cache);
        }
        };

    // From TrafficLooper
    if (!traffic_update.isEmpty() && !Configs::dataManager->settingsRepo->disable_traffic_stats) {
        traffic_update_cache = traffic_update;
        if (traffic_update == "STOP") {
            traffic_update_cache = "";
        }
        else {
            refresh_speed_label();
            return;
        }
    }

    refresh_speed_label();

    // From UI.
    //
    // Take immutable copies once so the status refresh never
    // reads Profile's private configuration directly.
    QString group_name;
    Configs::ProfileConfigSnapshot runningConfig;
    QString runningCountryInfo;
    const bool hasRunningProfile =
        static_cast<bool>(running);

    if (hasRunningProfile)
    {
        runningConfig =
            running->ConfigSnapshot();

        runningCountryInfo =
            running->RunningCountryInfo();

        auto group =
            Configs::dataManager
            ->groupsRepo
            ->GetGroup(
                runningConfig.gid
            );

        if (group)
        {
            group_name =
                group->Snapshot().name;
        }
    }

    if (QDateTime::currentSecsSinceEpoch() -
        last_test_time > 2)
    {
        QString runningLabelText;

        if (hasRunningProfile)
        {
            runningLabelText =
                QString("[%1] %2")
                .arg(
                    group_name,
                    runningConfig.displayName
                );

            if (!runningCountryInfo.isEmpty())
            {
                runningLabelText +=
                    "\n" + runningCountryInfo;
            }
        }
        else
        {
            runningLabelText =
                tr("Not Running");
        }

        ui->label_running
            ->setText(runningLabelText);
    }
    //
    auto display_socks = DisplayAddress(Configs::dataManager->settingsRepo->inbound_address, Configs::dataManager->settingsRepo->inbound_socks_port);
    auto inbound_disabled = Configs::dataManager->settingsRepo->disable_mixed_inbound;
    auto inbound_txt = QString("Mixed: %1").arg(inbound_disabled ? "Disabled" : display_socks);
    ui->label_inbound->setText(inbound_txt);
    //
    ui->checkBox_VPN->setChecked(Configs::dataManager->settingsRepo->spmode_vpn);
    ui->checkBox_SystemProxy->setChecked(Configs::dataManager->settingsRepo->spmode_system_proxy);
    if (select_mode) {
        ui->label_running->setText(tr("Select") + " *");
        ui->label_running->setToolTip(tr("Select mode, double-click or press Enter to select a profile, press ESC to exit."));
    }
    else {
        ui->label_running->setToolTip({});
    }

    auto make_title = [=, this](bool isTray) {
        QStringList tt;
        if (!isTray && Configs::IsAdmin()) tt << "[Admin]";
        if (select_mode) tt << "[" + tr("Select") + "]";
        if (!title_error.isEmpty()) tt << "[" + title_error + "]";
        if (Configs::dataManager->settingsRepo->spmode_vpn && !Configs::dataManager->settingsRepo->spmode_system_proxy) tt << "[Tun]";
        if (!Configs::dataManager->settingsRepo->spmode_vpn && Configs::dataManager->settingsRepo->spmode_system_proxy) tt << "[" + tr("System Proxy") + "]";
        if (Configs::dataManager->settingsRepo->spmode_vpn && Configs::dataManager->settingsRepo->spmode_system_proxy) tt << "[Tun+" + tr("System Proxy") + "]";
        tt << software_name;
        if (!isTray) tt << QString(NKR_VERSION);
        if (!Configs::dataManager->settingsRepo->active_routing.isEmpty() && Configs::dataManager->settingsRepo->active_routing != "Default") {
            tt << "[" + Configs::dataManager->settingsRepo->active_routing + "]";
        }
        if (hasRunningProfile)
        {
            const QString displayTypeAndName =
                QString("[%1] %2")
                .arg(
                    runningConfig.displayType,
                    runningConfig.displayName
                );

            tt <<
                displayTypeAndName
                + "@"
                + group_name;

            if (!runningCountryInfo.isEmpty())
            {
                tt << runningCountryInfo;
            }
        }
        return tt.join(isTray ? "\n" : " ");
        };

    auto icon_status_new = Icon::NONE;

    if (running != nullptr) {
        if (Configs::dataManager->settingsRepo->spmode_vpn) {
            icon_status_new = Icon::VPN;
        }
        else if (Configs::dataManager->settingsRepo->system_dns_set && Configs::dataManager->settingsRepo->spmode_system_proxy) {
            icon_status_new = Icon::SYSTEM_PROXY_DNS;
        }
        else if (Configs::dataManager->settingsRepo->system_dns_set) {
            icon_status_new = Icon::DNS;
        }
        else if (Configs::dataManager->settingsRepo->spmode_system_proxy) {
            icon_status_new = Icon::SYSTEM_PROXY;
        }
        else {
            icon_status_new = Icon::RUNNING;
        }
    }

    // refresh title & window icon
    setWindowTitle(software_name);
    if (icon_status_new != icon_status) QApplication::setWindowIcon(GetTrayIcon(icon_status_new));

    // refresh tray
    if (tray != nullptr) {
        tray->setToolTip(make_title(true));
        if (icon_status_new != icon_status) tray->setIcon(Icon::GetTrayIcon(icon_status_new));
    }

    icon_status = icon_status_new;
}

void MainWindow::update_traffic_graph(int proxyDl, int proxyUp, int directDl, int directUp)
{
    if (speedChartWidget) {
        QMap<SpeedWidget::GraphType, long> pointData;
        pointData[SpeedWidget::OUTBOUND_PROXY_UP] = proxyUp;
        pointData[SpeedWidget::OUTBOUND_PROXY_DOWN] = proxyDl;
        pointData[SpeedWidget::OUTBOUND_DIRECT_UP] = directUp;
        pointData[SpeedWidget::OUTBOUND_DIRECT_DOWN] = directDl;

        speedChartWidget->AddPointData(pointData);
    }
}

// table显示

// refresh_groups -> show_group -> refresh_proxy_list
void MainWindow::refresh_groups()
{
    Configs::dataManager
        ->settingsRepo
        ->refreshing_group_list = true;

    for (int i =
        ui->tabWidget->count() - 1;
        i > 0;
        --i)
    {
        ui->tabWidget->removeTab(i);
    }

    const auto groupOrder =
        Configs::dataManager
        ->groupsRepo
        ->GetGroupsTabOrder();

    int index = 0;

    for (const int gid : groupOrder)
    {
        auto group =
            Configs::dataManager
            ->groupsRepo
            ->GetGroup(gid);

        if (!group) {
            continue;
        }

        const auto snapshot =
            group->Snapshot();

        if (index == 0)
        {
            ui->tabWidget
                ->setTabText(
                    0,
                    snapshot.name
                );
        }
        else
        {
            auto* widget =
                new QWidget();

            auto* layout =
                new QVBoxLayout();

            layout->setContentsMargins(
                QMargins()
            );

            layout->setSpacing(0);

            widget->setLayout(
                layout
            );

            ui->tabWidget
                ->addTab(
                    widget,
                    snapshot.name
                );
        }

        ui->tabWidget
            ->tabBar()
            ->setTabData(
                index,
                gid
            );

        ++index;
    }

    if (Configs::dataManager
        ->groupsRepo
        ->CurrentGroup() == nullptr)
    {
        Configs::dataManager
            ->settingsRepo
            ->current_group = -1;

        const int targetGroup =
            !groupOrder.isEmpty()
            ? groupOrder.first()
            : 0;

        ui->tabWidget
            ->setCurrentIndex(
                groupId2TabIndex(
                    targetGroup
                )
            );

        show_group(
            targetGroup
        );
    }
    else
    {
        const int currentGroupId =
            Configs::dataManager
            ->settingsRepo
            ->current_group;

        ui->tabWidget
            ->setCurrentIndex(
                groupId2TabIndex(
                    currentGroupId
                )
            );

        show_group(
            currentGroupId
        );
    }

    Configs::dataManager
        ->settingsRepo
        ->refreshing_group_list = false;
}

void MainWindow::refresh_proxy_list_column_size()
{
    auto group =
        Configs::dataManager
        ->groupsRepo
        ->CurrentGroup();

    if (!group) {
        return;
    }

    auto* hHeader =
        dynamic_cast<
        ProfilesTableFilterHeader*
        >(
            ui->profilesTableView
            ->horizontalHeader()
            );

    if (!hHeader) {
        return;
    }

    QTimer::singleShot(
        0,
        ui->profilesTableView,

        [this, group, hHeader]()
        {
            const auto snapshot =
                group->Snapshot();

            const auto calculated =
                group
                ->CalculatedColumnWidths();

            hHeader->blockSignals(
                true
            );

            if (snapshot
                .column_width
                .isEmpty())
            {
                hHeader->setSectionResizeMode(
                    0,
                    QHeaderView::
                    ResizeToContents
                );

                hHeader->setSectionResizeMode(
                    1,
                    QHeaderView::Stretch
                );

                hHeader->setSectionResizeMode(
                    2,
                    QHeaderView::Stretch
                );

                hHeader->setSectionResizeMode(
                    3,
                    QHeaderView::
                    ResizeToContents
                );

                hHeader->setSectionResizeMode(
                    4,
                    QHeaderView::
                    ResizeToContents
                );

                if (calculated.size() > 0 &&
                    calculated[0] >
                    hHeader->sectionSize(0))
                {
                    hHeader
                        ->setSectionResizeMode(
                            0,
                            QHeaderView::Fixed
                        );

                    hHeader->resizeSection(
                        0,
                        calculated[0]
                    );
                }

                if (calculated.size() > 3 &&
                    calculated[3] >
                    hHeader->sectionSize(3))
                {
                    hHeader
                        ->setSectionResizeMode(
                            3,
                            QHeaderView::Fixed
                        );

                    hHeader->resizeSection(
                        3,
                        calculated[3]
                    );
                }

                if (calculated.size() > 4 &&
                    calculated[4] >
                    hHeader->sectionSize(4))
                {
                    hHeader
                        ->setSectionResizeMode(
                            4,
                            QHeaderView::Fixed
                        );

                    hHeader->resizeSection(
                        4,
                        calculated[4]
                    );
                }

                ui->profilesTableView
                    ->setHorizontalScrollBarPolicy(
                        Qt::ScrollBarAlwaysOff
                    );

                QList<int>
                    newCalculated;

                newCalculated.reserve(
                    5
                );

                for (int i = 0;
                    i <= 4;
                    ++i)
                {
                    const int size =
                        hHeader
                        ->sectionSize(i);

                    hHeader
                        ->setSectionResizeMode(
                            i,
                            QHeaderView::
                            Interactive
                        );

                    hHeader->resizeSection(
                        i,
                        size
                    );

                    newCalculated.append(
                        size
                    );
                }

                group
                    ->SetCalculatedColumnWidths(
                        newCalculated
                    );
            }
            else
            {
                group
                    ->clearCalculatedColumnWidth();

                const int count =
                    qMin(
                        5,
                        snapshot
                        .column_width
                        .size()
                    );

                for (int i = 0;
                    i < count;
                    ++i)
                {
                    hHeader
                        ->setSectionResizeMode(
                            i,
                            QHeaderView::
                            Interactive
                        );

                    hHeader->resizeSection(
                        i,
                        snapshot
                        .column_width
                        .at(i)
                    );
                }

                ui->profilesTableView
                    ->setHorizontalScrollBarPolicy(
                        Qt::ScrollBarAsNeeded
                    );
            }

            hHeader->adjustPositions();

            hHeader->blockSignals(
                false
            );
        }
    );
}

void MainWindow::refresh_proxy_list(const QList<int>& ids, bool mayNeedReset) {
    if (!Configs::dataManager->settingsRepo->refreshing_group) saveProfileFocusState();
    refresh_proxy_list_impl(ids, mayNeedReset);
    if (mayNeedReset) restoreProfileFocusState();
}

void MainWindow::refresh_proxy_list_impl(const QList<int>& ids, bool mayNeedReset) {
    auto currentGroup = Configs::dataManager->groupsRepo->CurrentGroup();
    if (currentGroup == nullptr)
    {
        MW_show_log("Could not find current group!");
        return;
    }
    // refresh data
    refresh_proxy_list_impl_refresh_data(ids, mayNeedReset);
    // now refresh column sizes
    refresh_proxy_list_column_size();
}

void MainWindow::refresh_proxy_list_impl_refresh_data(const QList<int>& ids, bool mayNeedReset) {
    auto currentGroup = Configs::dataManager->groupsRepo->CurrentGroup();
    if (currentGroup == nullptr) return;
    if (!ids.isEmpty()) {
        if (filterProfilesList(ids).isEmpty())
            return;
        for (auto id : ids) profilesTableModel->refreshProfileId(id);
    }
    else {
        const auto profileIDs =
            filterProfilesList(
                currentGroup->Profiles()
            );
        profilesTableModel->refreshTable(profileIDs, mayNeedReset);
    }
}

// table菜单相关

void MainWindow::on_profilesTableView_doubleClicked(const QModelIndex& index) {
    if (!index.isValid() || !profilesTableModel) return;
    int id = profilesTableModel->data(index, ProfilesTableModel::ProfileIdRole).toInt();
    if (select_mode) {
        emit profile_selected(id);
        select_mode = false;
        refresh_status();
        return;
    }
    auto dialog = new DialogEditProfile("", id, this);
    connect(dialog, &QDialog::finished, dialog, &QDialog::deleteLater);
}

void MainWindow::on_menu_add_from_input_triggered() {
    auto dialog = new DialogEditProfile("socks", Configs::dataManager->settingsRepo->current_group, this);
    connect(dialog, &QDialog::finished, dialog, &QDialog::deleteLater);
}

void MainWindow::on_menu_add_from_clipboard_triggered() {
    auto clipboard = QApplication::clipboard()->text();
    import_or_handle_deeplink(clipboard);
}

void MainWindow::on_menu_clone_triggered() {
    auto entIDs = get_now_selected_list();
    if (entIDs.isEmpty()) return;

    auto btn = QMessageBox::question(this, tr("Clone"), tr("Clone %1 item(s)").arg(entIDs.count()));
    if (btn != QMessageBox::Yes) return;

    QStringList sls;
    auto ents = Configs::dataManager->profilesRepo->GetProfileBatch(entIDs);
    for (const auto& ent : ents)
    {
        if (!ent)
        {
            continue;
        }

        const auto outbound =
            ent->OutboundSnapshot();

        if (!outbound)
        {
            continue;
        }

        sls <<
            outbound->ExportJsonLink();
    }

    Subscription::groupUpdater->AsyncUpdate(sls.join("\n"));
}

void MainWindow::on_menu_delete_repeat_triggered()
{
    // =====================================================
    // Current group
    // =====================================================
    auto group =
        Configs::dataManager
        ->groupsRepo
        ->CurrentGroup();
    if (!group)
    {
        return;
    }
    // Take one immutable profile ID snapshot.
    const auto profileIDs =
        group->Profiles();
    if (profileIDs.isEmpty())
    {
        return;
    }
    // Load profiles only once.
    const auto profiles =
        Configs::dataManager
        ->profilesRepo
        ->GetProfileBatch(
            profileIDs
        );
    if (profiles.isEmpty())
    {
        return;
    }
    QList<
        std::shared_ptr<
        Configs::Profile
        >
    > uniqueProfiles;

    QList<
        std::shared_ptr<
        Configs::Profile
        >
    > duplicateProfiles;
    // =====================================================
    // Find duplicates
    // =====================================================
    Configs::ProfileFilter::Uniq(
        profiles,
        uniqueProfiles,
        false
    );
    Configs::ProfileFilter::
        OnlyInSrc_ByPointer(
            profiles,
            uniqueProfiles,
            duplicateProfiles
        );
    if (duplicateProfiles.isEmpty())
    {
        return;
    }
    // =====================================================
    // Confirmation text
    // =====================================================
    QString removeDisplay;
    int removeDisplayCount = 0;
    for (const auto& profile :
        duplicateProfiles)
    {
        if (!profile)
        {
            continue;
        }

        const auto outbound =
            profile->OutboundSnapshot();

        if (!outbound)
        {
            continue;
        }

        removeDisplay +=
            outbound->DisplayTypeAndName()
            +
            "\n";
        ++removeDisplayCount;
        if (removeDisplayCount >= 20)
        {
            removeDisplay +=
                "...";

            break;
        }
    }
    // =====================================================
    // Confirmation
    // =====================================================
    if (!Configs::dataManager
        ->settingsRepo
        ->skip_delete_confirmation)
    {
        const auto answer =
            QMessageBox::question(
                this,
                tr("Confirmation"),
                tr("Remove %1 item(s) ?")
                .arg(duplicateProfiles.size()
                )
                +
                "\n"
                +
                removeDisplay
            );
        if (answer !=
            QMessageBox::
            StandardButton::Yes)
        {
            return;
        }
    }
    // =====================================================
    // IDs to delete
    // =====================================================
    QList<int> deleteIDs;
    deleteIDs.reserve(
        duplicateProfiles.size()
    );
    for (const auto& profile :
        duplicateProfiles)
    {
        if (!profile)
        {
            continue;
        }

        const int profileId =
            profile->Id();

        if (profileId < 0)
        {
            continue;
        }

        deleteIDs.append(
            profileId
        );
    }
    if (deleteIDs.isEmpty())
    {
        return;
    }
    // =====================================================
    // Delete
    // =====================================================
    Configs::dataManager
        ->profilesRepo
        ->BatchDeleteProfiles(
            deleteIDs,
            true
        );
    refresh_proxy_list(
        {},
        true
    );
}

void MainWindow::on_menu_delete_triggered() {
    auto entIDs = get_now_selected_list();
    if (entIDs.count() == 0) return;
    if (Configs::dataManager->settingsRepo->skip_delete_confirmation || QMessageBox::question(this, tr("Confirmation"), QString(tr("Remove %1 item(s) ?")).arg(entIDs.count())) == QMessageBox::StandardButton::Yes) {
        Configs::dataManager->profilesRepo->BatchDeleteProfiles(entIDs, true);
        refresh_proxy_list({}, true);
    }
}

void MainWindow::on_menu_reset_traffic_triggered()
{
    const auto entIDs =
        get_now_selected_list();

    if (entIDs.isEmpty()) {
        return;
    }

    const auto ents =
        Configs::dataManager
        ->profilesRepo
        ->GetProfileBatch(entIDs);

    if (ents.empty()) {
        return;
    }

    for (const auto& ent : ents)
    {
        if (!ent) {
            continue;
        }

        ent->ResetTraffic();

        Configs::dataManager
            ->profilesRepo
            ->SaveTraffic(ent);
    }

    if (auto group =
        Configs::dataManager
        ->groupsRepo
        ->GetGroup(
            ents.first()->GroupId()
        );
        group)
    {
        group->ResetCalculatedColumnWidth(
            4
        );
    }

    refresh_proxy_list(
        entIDs
    );
}

void MainWindow::on_menu_copy_links_triggered() {
    if (ui->masterLogBrowser->hasFocus()) {
        ui->masterLogBrowser->copy();
        return;
    }
    auto entIDs = get_now_selected_list();
    QStringList links;
    auto ents = Configs::dataManager->profilesRepo->GetProfileBatch(entIDs);
    for (const auto& ent : ents)
    {
        if (!ent)
        {
            continue;
        }

        const auto outbound =
            ent->OutboundSnapshot();

        if (!outbound)
        {
            continue;
        }

        QString link =
            outbound->ExportToLink();

        if (link.isEmpty())
        {
            link =
                outbound->ExportJsonLink();
        }

        if (!link.isEmpty())
        {
            links += link;
        }
    }
    if (links.length() == 0) return;
    QApplication::clipboard()->setText(links.join("\n"));
    MW_show_log(tr("Copied %1 item(s)").arg(links.length()));
}

void MainWindow::on_menu_copy_links_nkr_triggered() {
    auto entIDs = get_now_selected_list();
    QStringList links;
    auto ents = Configs::dataManager->profilesRepo->GetProfileBatch(entIDs);
    for (const auto& ent : ents)
    {
        if (!ent)
        {
            continue;
        }

        const auto outbound =
            ent->OutboundSnapshot();

        if (!outbound)
        {
            continue;
        }

        const QString link =
            outbound->ExportJsonLink();

        if (!link.isEmpty())
        {
            links += link;
        }
    }
    if (links.length() == 0) return;
    QApplication::clipboard()->setText(links.join("\n"));
    MW_show_log(tr("Copied %1 item(s)").arg(links.length()));
}

void MainWindow::on_menu_export_config_triggered() {
    auto ents = get_now_selected_list();
    if (ents.count() != 1) return;
    auto ent = Configs::dataManager->profilesRepo->GetProfile(ents.first());

    auto result = Configs::BuildSingBoxConfig(ent);
    QString config_core = QJsonObject2QString(result->coreConfig, true);
    QApplication::clipboard()->setText(config_core);

    QMessageBox msg(QMessageBox::Information, tr("Config copied"), config_core);
    QPushButton* button_1 = msg.addButton(tr("Copy core config"), QMessageBox::YesRole);
    QPushButton* button_2 = msg.addButton(tr("Copy test config"), QMessageBox::YesRole);
    msg.addButton(QMessageBox::Ok);
    msg.setEscapeButton(QMessageBox::Ok);
    msg.setDefaultButton(QMessageBox::Ok);
    msg.exec();
    if (msg.clickedButton() == button_1) {
        result = BuildSingBoxConfig(ent);
        if (!result->error.isEmpty()) {
            MessageBoxWarning("Build config error", result->error);
            return;
        }
        config_core = QJsonObject2QString(result->coreConfig, true);
        QApplication::clipboard()->setText(config_core);
    }
    else if (msg.clickedButton() == button_2) {
        auto res = Configs::BuildTestConfig({ ent });
        if (!res->error.isEmpty()) {
            MessageBoxWarning("Build Test config error", res->error);
            return;
        }
        config_core = QJsonObject2QString(res->coreConfig, true);
        QApplication::clipboard()->setText(config_core);
    }
}

void MainWindow::display_qr_link(bool nkrFormat) {
    auto ents = get_now_selected_list();
    if (ents.count() != 1) return;

    class W : public QDialog {
    public:
        QLabel* l = nullptr;
        QCheckBox* cb = nullptr;
        //
        QPlainTextEdit* l2 = nullptr;
        QImage im;
        //
        QString link;
        QString link_nk;

        void show_qr(const QSize& size) const {
            auto side = size.height() - 20 - l2->size().height() - cb->size().height();
            l->setPixmap(QPixmap::fromImage(im.scaled(side, side, Qt::KeepAspectRatio, Qt::FastTransformation),
                Qt::MonoOnly));
            l->resize(side, side);
        }

        void refresh(bool is_nk) {
            auto link_display = is_nk ? link_nk : link;
            l2->setPlainText(link_display);
            constexpr qint32 qr_padding = 2;
            //
            try {
                qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(link_display.toUtf8().data(), qrcodegen::QrCode::Ecc::MEDIUM);
                qint32 sz = qr.getSize();
                im = QImage(sz + qr_padding * 2, sz + qr_padding * 2, QImage::Format_RGB32);
                QRgb black = qRgb(0, 0, 0);
                QRgb white = qRgb(255, 255, 255);
                im.fill(white);
                for (int y = 0; y < sz; y++)
                    for (int x = 0; x < sz; x++)
                        if (qr.getModule(x, y))
                            im.setPixel(x + qr_padding, y + qr_padding, black);
                show_qr(size());
            }
            catch (const std::exception& ex) {
                QMessageBox::warning(nullptr, "error", ex.what());
            }
        }

        W(const QString& link_, const QString& link_nk_) {
            link = link_;
            link_nk = link_nk_;
            //
            setLayout(new QVBoxLayout);
            setMinimumSize(256, 256);
            QSizePolicy sizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
            sizePolicy.setHeightForWidth(true);
            setSizePolicy(sizePolicy);
            //
            l = new QLabel();
            l->setMinimumSize(256, 256);
            l->setMargin(6);
            l->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            l->setScaledContents(true);
            layout()->addWidget(l);
            cb = new QCheckBox;
            cb->setText("Neko Links");
            layout()->addWidget(cb);
            l2 = new QPlainTextEdit();
            l2->setReadOnly(true);
            layout()->addWidget(l2);
            //
            connect(cb, &QCheckBox::toggled, this, &W::refresh);
            refresh(false);
        }

        void resizeEvent(QResizeEvent* resizeEvent) override {
            show_qr(resizeEvent->size());
        }
    };

    auto ent =
        Configs::dataManager
        ->profilesRepo
        ->GetProfile(
            ents.first()
        );

    if (!ent)
    {
        return;
    }

    const auto outbound =
        ent->OutboundSnapshot();

    if (!outbound)
    {
        return;
    }

    const QString link =
        outbound->ExportToLink();

    const QString link_nk =
        outbound->ExportJsonLink();

    auto* w =
        new W(
            link,
            link_nk
        );

    w->setWindowTitle(
        outbound->DisplayTypeAndName()
    );

    w->exec();
    w->deleteLater();
}

#ifdef Q_OS_LINUX
OrgFreedesktopPortalRequestInterface::OrgFreedesktopPortalRequestInterface(
    const QString& service,
    const QString& path,
    const QDBusConnection& connection,
    QObject* parent)
    : QDBusAbstractInterface(service,
        path,
        "org.freedesktop.portal.Request",
        connection,
        parent)
{}

OrgFreedesktopPortalRequestInterface::~OrgFreedesktopPortalRequestInterface() {}
#endif

QPixmap grabScreen(QScreen* screen, bool& ok)
{
    QPixmap p;
    QRect geom = screen->geometry();
#ifdef Q_OS_LINUX
    if (qEnvironmentVariable("XDG_SESSION_TYPE") == "wayland" || qEnvironmentVariable("WAYLAND_DISPLAY").contains("wayland", Qt::CaseInsensitive)) {
        QDBusInterface screenshotInterface(
            QStringLiteral("org.freedesktop.portal.Desktop"),
            QStringLiteral("/org/freedesktop/portal/desktop"),
            QStringLiteral("org.freedesktop.portal.Screenshot"));

        // unique token
        QString token =
            QUuid::createUuid().toString().remove('-').remove('{').remove('}');

        // premake interface
        auto* request = new OrgFreedesktopPortalRequestInterface(
            QStringLiteral("org.freedesktop.portal.Desktop"),
            "/org/freedesktop/portal/desktop/request/" +
            QDBusConnection::sessionBus().baseService().remove(':').replace('.', '_') +
            "/" + token,
            QDBusConnection::sessionBus());

        QEventLoop loop;
        const auto gotSignal = [&p, &loop](uint status, const QVariantMap& map) {
            if (status == 0) {
                // Parse this as URI to handle unicode properly
                QUrl uri = map.value("uri").toString();
                QString uriString = uri.toLocalFile();
                p = QPixmap(uriString);
                p.setDevicePixelRatio(qApp->devicePixelRatio());
                QFile imgFile(uriString);
                imgFile.remove();
            }
            loop.quit();
            };

        // prevent racy situations and listen before calling screenshot
        QMetaObject::Connection conn = QObject::connect(
            request, &org::freedesktop::portal::Request::Response, gotSignal);

        screenshotInterface.call(
            QStringLiteral("Screenshot"),
            "",
            QMap<QString, QVariant>({ { "handle_token", QVariant(token) },
                                      { "interactive", QVariant(false) } }));

        loop.exec();
        QObject::disconnect(conn);
        request->Close().waitForFinished();
        request->deleteLater();

        if (p.isNull()) {
            ok = false;
        }
        return p;
    }
    else
#endif
        return screen->grabWindow(0, geom.x(), geom.y(), geom.width(), geom.height());
}

void MainWindow::parseQrImage(const QPixmap* image)
{
    const QVector<QString> texts = QrDecoder().decode(image->toImage().convertToFormat(QImage::Format_Grayscale8));
    if (texts.isEmpty()) {
        MessageBoxInfo(software_name, tr("QR Code not found"));
    }
    else {
        for (const QString& text : texts) {
            MW_show_log("QR Code Result:\n" + text);
            Subscription::groupUpdater->AsyncUpdate(text);
        }
    }
}

void MainWindow::on_menu_scan_qr_triggered() {
    hide();
    QThread::sleep(1);

    bool ok = true;
    QPixmap qpx(grabScreen(QGuiApplication::primaryScreen(), ok));

    show();
    if (ok) {
        parseQrImage(&qpx);
    }
    else {
        MessageBoxInfo(software_name, tr("Unable to capture screen"));
    }
}

void MainWindow::on_menu_clear_test_result_triggered()
{
    const auto entIDs =
        get_selected_or_group();

    const auto ents =
        Configs::dataManager
        ->profilesRepo
        ->GetProfileBatch(entIDs);

    if (ents.empty()) {
        return;
    }

    for (const auto& ent : ents)
    {
        if (ent) {
            ent->ClearTestResults();
        }
    }

    Configs::dataManager
        ->profilesRepo
        ->SaveBatch(ents);

    if (auto group =
        Configs::dataManager
        ->groupsRepo
        ->GetGroup(
            ents.first()->GroupId()
        );
        group)
    {
        group->ResetCalculatedColumnWidth(
            3
        );
    }

    refresh_proxy_list();
}

void MainWindow::on_menu_select_all_triggered() {
    if (ui->masterLogBrowser->hasFocus()) {
        ui->masterLogBrowser->selectAll();
        return;
    }
    ui->profilesTableView->selectAll();
}

namespace
{
    std::atomic_bool mw_sub_updating{
        false
    };
}

void MainWindow::on_menu_update_subscription_triggered()
{
    auto group =
        Configs::dataManager
        ->groupsRepo
        ->CurrentGroup();


    if (!group)
    {
        return;
    }


    const auto snapshot =
        group->Snapshot();


    if (snapshot.url.isEmpty())
    {
        return;
    }


    // Atomically:
    //
    // false -> true : this operation acquired the flag
    // true  -> true : another operation is already running
    if (mw_sub_updating.exchange(
        true,
        std::memory_order_acq_rel))
    {
        return;
    }


    Subscription::groupUpdater
        ->AsyncUpdate(
            snapshot.url,
            snapshot.id,

            []
            {
                mw_sub_updating.store(
                    false,
                    std::memory_order_release
                );
            }
        );
}

void MainWindow::on_menu_remove_unavailable_triggered() {
    clearUnavailableProfiles();
}

void MainWindow::on_menu_remove_invalid_triggered()
{
    runOnNewThread(
        [this]()
        {
            // -------------------------------------------------
            // Get current group
            // -------------------------------------------------

            auto currentGroup =
                Configs::dataManager
                ->groupsRepo
                ->CurrentGroup();


            if (!currentGroup)
            {
                return;
            }


            // -------------------------------------------------
            // Take ONE immutable profile ID snapshot.
            //
            // Do not call Profiles() again below.
            // Otherwise the group could change between calls.
            // -------------------------------------------------

            const QList<int> profileIDs =
                currentGroup->Profiles();


            if (profileIDs.isEmpty())
            {
                return;
            }


            // -------------------------------------------------
            // Result storage
            // -------------------------------------------------

            QList<
                std::shared_ptr<
                Configs::Profile
                >
            > invalidProfiles;


            QMutex invalidProfilesMutex;


            // -------------------------------------------------
            // Completion semaphore.
            //
            // Every started task releases exactly one permit.
            // The outer worker waits for all permits.
            // -------------------------------------------------

            auto completion =
                std::make_shared<QSemaphore>(
                    0
                );


            int taskCount = 0;


            // -------------------------------------------------
            // Start validation tasks
            // -------------------------------------------------

            for (const int profileID :
            profileIDs)
            {
                auto profile =
                    Configs::dataManager
                    ->profilesRepo
                    ->GetProfile(
                        profileID
                    );


                // Profile may have been deleted meanwhile.
                if (!profile)
                {
                    continue;
                }


                ++taskCount;
                parallelCoreCallPool->start(
                    [
                        profile,
                        completion,
                        &invalidProfiles,
                        &invalidProfilesMutex
                    ]()
                    {
                        // -------------------------------------
                        // Validate profile
                        // -------------------------------------
                        if (!IsValid(profile))
                        {
                            QMutexLocker locker(
                                &invalidProfilesMutex
                            );


                            invalidProfiles.append(
                                profile
                            );
                        }
                        // -------------------------------------
                        // Signal task completion.
                        //
                        // IMPORTANT:
                        // QSemaphore may be released by
                        // another thread. QMutex may not.
                        // -------------------------------------

                        completion->release();
                    }
                );
            }
            // -------------------------------------------------
            // Wait until every actually started task finishes.
            // -------------------------------------------------
            if (taskCount > 0)
            {
                completion->acquire(
                    taskCount
                );
            }
            // No pool task can touch invalidProfiles anymore
            // after acquire(taskCount) returns.
            if (invalidProfiles.isEmpty())
            {
                return;
            }
            // -------------------------------------------------
            // Prepare confirmation text outside UI thread
            // -------------------------------------------------
            QString removeDisplay;
            int removeDisplayCount = 0;
            for (const auto& profile :
                invalidProfiles)
            {
                if (!profile)

                {

                    continue;

                }


                const auto outbound =

                    profile->OutboundSnapshot();


                if (!outbound)

                {

                    continue;

                }


                removeDisplay +=

                    outbound->DisplayTypeAndName()

                    +

                    "\n";
                ++removeDisplayCount;
                if (removeDisplayCount == 20)
                {
                    removeDisplay +=
                        "...";

                    break;
                }
            }

            // -------------------------------------------------
            // UI interaction + deletion
            // -------------------------------------------------
            runOnUiThread(
                [
                    this,
                    invalidProfiles,
                    removeDisplay
                ]()
                {
                    if (invalidProfiles.isEmpty())
                    {
                        return;
                    }
                    const bool skipConfirmation =
                        Configs::dataManager
                        ->settingsRepo
                        ->skip_delete_confirmation;

                    bool confirmed =
                        skipConfirmation;

                    if (!confirmed)
                    {
                        confirmed =
                            QMessageBox::question(
                                this,
                                tr("Confirmation"),

                                tr(
                                    "Remove %1 Invalid "
                                    "item(s) ?"
                                )
                                .arg(
                                    invalidProfiles.size()
                                )
                                +
                                "\n"
                                +
                                removeDisplay
                            )
                            ==
                            QMessageBox::
                            StandardButton::Yes;
                    }

                    if (!confirmed)
                    {
                        return;
                    }

                    // -----------------------------------------
                    // Convert Profile objects to IDs
                    // -----------------------------------------
                    QList<int> deleteIDs;

                    deleteIDs.reserve(
                        invalidProfiles.size()
                    );

                    for (const auto& profile :
                        invalidProfiles)
                    {
                        if (!profile)
                        {
                            continue;
                        }
                        const int profileId =
                            profile->Id();

                        if (profileId < 0)
                        {
                            continue;
                        }

                        deleteIDs.append(
                            profileId
                        );
                    }

                    if (deleteIDs.isEmpty())
                    {
                        return;
                    }

                    // -----------------------------------------
                    // Delete
                    // -----------------------------------------
                    Configs::dataManager
                        ->profilesRepo
                        ->BatchDeleteProfiles(
                            deleteIDs,
                            true
                        );
                    refresh_proxy_list(
                        {},
                        true
                    );
                }
            );
        }
    );
}

void MainWindow::on_menu_resolve_selected_triggered()
{
    const auto profileIDs =
        get_now_selected_list();


    if (profileIDs.isEmpty())
    {
        return;
    }


    const auto loadedProfiles =
        Configs::dataManager
        ->profilesRepo
        ->GetProfileBatch(
            profileIDs
        );


    QList<
        std::shared_ptr<
        Configs::Profile
        >
    > profiles;


    profiles.reserve(
        loadedProfiles.size()
    );


    // Exclude objects that cannot actually start
    // an asynchronous resolve operation.
    for (const auto& profile :
        loadedProfiles)
    {
        if (!profile)
        {
            continue;
        }

        const auto outbound =
            profile->OutboundSnapshot();

        if (!outbound)
        {
            continue;
        }


        profiles.append(
            profile
        );
    }


    if (profiles.isEmpty())
    {
        return;
    }


    // Acquire global operation flag atomically.
    if (mw_sub_updating.exchange(
        true,
        std::memory_order_acq_rel))
    {
        return;
    }


    auto* settings =
        Configs::dataManager
        ->settingsRepo
        .get();


    settings
        ->resolve_count
        .store(
            profiles.size(),
            std::memory_order_release
        );


    for (const auto& profile :
        profiles)
    {
        profile
            ->ResolveDomainToIP(
                [this, profile, settings]()
                {
                    Configs::dataManager
                        ->profilesRepo
                        ->Save(profile);


                    refresh_proxy_list(
                        {
                            profile->Id()
                        }
                    );


                    // fetch_sub() returns the OLD value.
                    //
                    // old == 1 means:
                    // this callback completed the final
                    // outstanding resolve operation.
                    if (settings
                        ->resolve_count
                        .fetch_sub(
                            1,
                            std::memory_order_acq_rel
                        )
                        != 1)
                    {
                        return;
                    }


                    mw_sub_updating.store(
                        false,
                        std::memory_order_release
                    );
                }
            );
    }
}

void MainWindow::
on_menu_resolve_domain_triggered()
{
    auto group =
        Configs::dataManager
        ->groupsRepo
        ->CurrentGroup();


    if (!group)
    {
        return;
    }


    const auto profileIDs =
        group->Profiles();


    if (profileIDs.isEmpty())
    {
        return;
    }


    if (QMessageBox::question(
        this,
        tr("Confirmation"),
        tr(
            "Replace domain server "
            "addresses with their "
            "resolved IPs?"
        )
    )
        != QMessageBox::StandardButton::Yes)
    {
        return;
    }


    // Load profiles in one batch instead of calling
    // GetProfile() for every ID separately.
    const auto loadedProfiles =
        Configs::dataManager
        ->profilesRepo
        ->GetProfileBatch(
            profileIDs
        );


    QList<
        std::shared_ptr<
        Configs::Profile
        >
    > profiles;


    profiles.reserve(
        loadedProfiles.size()
    );


    for (const auto& profile :
        loadedProfiles)
    {
        if (!profile)
        {
            continue;
        }

        const auto outbound =
            profile->OutboundSnapshot();

        if (!outbound)
        {
            continue;
        }


        profiles.append(
            profile
        );
    }


    if (profiles.isEmpty())
    {
        return;
    }


    if (mw_sub_updating.exchange(
        true,
        std::memory_order_acq_rel))
    {
        return;
    }

    auto* settings =
        Configs::dataManager
        ->settingsRepo
        .get();


    settings
        ->resolve_count
        .store(
            profiles.size(),
            std::memory_order_release
        );


    for (const auto& profile :
        profiles)
    {
        profile
            ->ResolveDomainToIP(
                [this, profile, settings]()
                {
                    Configs::dataManager
                        ->profilesRepo
                        ->Save(profile);


                    refresh_proxy_list(
                        {
                            profile->Id()
                        }
                    );


                    if (settings
                        ->resolve_count
                        .fetch_sub(
                            1,
                            std::memory_order_acq_rel
                        )
                        != 1)
                    {
                        return;
                    }


                    // Last resolve completed.
                    mw_sub_updating.store(
                        false,
                        std::memory_order_release
                    );
                }
            );
    }
}

void MainWindow::on_profilesTableView_customContextMenuRequested(const QPoint& pos) {
    ui->menu_server->popup(ui->profilesTableView->viewport()->mapToGlobal(pos));
}

QList<int> MainWindow::get_now_selected_list() {
    QList<int> list;
    if (!profilesTableModel) return list;
    QModelIndexList indices = ui->profilesTableView->selectionModel()->selectedRows(0);
    for (const QModelIndex& idx : indices) {
        int id = profilesTableModel->data(idx, ProfilesTableModel::ProfileIdRole).toInt();
        list << id;
    }
    return list;
}

QList<int>
MainWindow::get_selected_or_group()
{
    const int selectedOrGroup =
        ui->menu_server
        ->property(
            "selected_or_group"
        )
        .toInt();
    // =====================================================
    // Explicit selection
    // =====================================================
    if (selectedOrGroup > 0)
    {
        auto profileIDs =
            get_now_selected_list();
        if (!profileIDs.isEmpty())
        {
            return profileIDs;
        }
        // Mode 1 means selected profiles only.
        //
        // There is no selection, therefore return
        // an empty list instead of falling back to
        // the whole group.
        if (selectedOrGroup != 2)
        {
            return {};
        }
    }
    // =====================================================
    // Whole current group
    // =====================================================
    auto group =
        Configs::dataManager
        ->groupsRepo
        ->CurrentGroup();
    if (!group)
    {
        return {};
    }
    return group->Profiles();
}

void MainWindow::saveProfileFocusState()
{
    auto group =
        Configs::dataManager
        ->groupsRepo
        ->CurrentGroup();

    if (!group ||
        !profilesTableModel ||
        !ui->profilesTableView
        ->selectionModel())
    {
        return;
    }

    const auto indices =
        ui->profilesTableView
        ->selectionModel()
        ->selectedRows(0);

    QList<std::pair<int, int>>
        selection;

    selection.reserve(
        indices.size()
    );

    for (const QModelIndex& idx :
        indices)
    {
        selection.append(
            {
                profilesTableModel
                    ->profileIdAt(
                        idx.row()
                    ),

                idx.row()
            }
        );
    }

    group
        ->SetSelectedProfilesIdIdxPairs(
            selection
        );
}


void MainWindow::restoreProfileFocusState()
{
    auto group =
        Configs::dataManager
        ->groupsRepo
        ->CurrentGroup();

    if (!group ||
        !profilesTableModel ||
        !ui->profilesTableView
        ->selectionModel())
    {
        return;
    }

    const auto selectionState =
        group
        ->SelectedProfilesIdIdxPairs();

    if (selectionState.isEmpty()) {
        return;
    }

    QList<int> newIndexes;

    for (const auto& item :
        selectionState)
    {
        const int profileId =
            item.first;

        const int newIdx =
            profilesTableModel
            ->indexOfProfile(
                profileId
            );

        if (newIdx >= 0)
        {
            newIndexes.append(
                newIdx
            );
        }
    }

    ui->profilesTableView
        ->setFocus();

    if (!newIndexes.isEmpty())
    {
        QItemSelection selection;

        for (const int row :
        newIndexes)
        {
            const QModelIndex left =
                profilesTableModel
                ->index(row, 0);

            const QModelIndex right =
                profilesTableModel
                ->index(
                    row,
                    profilesTableModel
                    ->columnCount() - 1
                );

            selection.select(
                left,
                right
            );
        }

        ui->profilesTableView
            ->selectionModel()
            ->select(
                selection,

                QItemSelectionModel::
                ClearAndSelect
                |
                QItemSelectionModel::
                Rows
            );

        ui->profilesTableView
            ->selectionModel()
            ->setCurrentIndex(
                profilesTableModel
                ->index(
                    newIndexes.first(),
                    0
                ),

                QItemSelectionModel::
                NoUpdate
            );

        return;
    }

    int desiredIndex =
        selectionState
        .first()
        .second;

    desiredIndex =
        std::min(
            desiredIndex,

            static_cast<int>(
                profilesTableModel
                ->profileIds()
                .size() - 1
                )
        );

    if (desiredIndex < 0) {
        return;
    }

    if (selectionState.size() == 1)
    {
        QItemSelection selection;

        const QModelIndex left =
            profilesTableModel
            ->index(
                desiredIndex,
                0
            );

        const QModelIndex right =
            profilesTableModel
            ->index(
                desiredIndex,

                profilesTableModel
                ->columnCount() - 1
            );

        selection.select(
            left,
            right
        );

        ui->profilesTableView
            ->selectionModel()
            ->select(
                selection,
                QItemSelectionModel::Select
            );
    }

    ui->profilesTableView
        ->selectionModel()
        ->setCurrentIndex(
            profilesTableModel
            ->index(
                desiredIndex,
                0
            ),

            QItemSelectionModel::
            NoUpdate
        );
}

void MainWindow::clearUnavailableProfiles(bool confirm, QList<int> profileIDs) {
    QList<int> del_ids;
    int remove_display_count = 0;
    QString remove_display;

    auto group = Configs::dataManager->groupsRepo->CurrentGroup();
    if (!group) return;

    if (profileIDs.isEmpty()) profileIDs = group->Profiles();

    auto profiles = Configs::dataManager->profilesRepo->GetProfileBatch(profileIDs);
    for (const auto& profile : profiles) {
        if (!profile) {
            continue;
        }
        const auto test =
            profile->TestSnapshot();
        if (test.latency < 0) {
            del_ids +=
                profile->Id();
            if (++remove_display_count == 20) {
                remove_display +=
                    "...";
            }
            else if (remove_display_count < 20)
            {
                const auto outbound =
                    profile->OutboundSnapshot();

                if (outbound)
                {
                    remove_display +=
                        outbound
                        ->DisplayTypeAndName()
                        + "\n";
                }
            }
        }
    }

    auto clearFunc = [&, this] {
        Configs::dataManager->profilesRepo->BatchDeleteProfiles(del_ids);
        refresh_proxy_list({}, true);
        };

    if (!del_ids.isEmpty()) {
        if (confirm && !Configs::dataManager->settingsRepo->skip_delete_confirmation) {
            if (QMessageBox::question(this, tr("Confirmation"), tr("Remove %1 Unavailable item(s) ?").arg(del_ids.length()) + "\n" + remove_display) == QMessageBox::StandardButton::Yes) {
                clearFunc();
            }
        }
        else {
            clearFunc();
        }
    }
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
    case Qt::Key_Escape:
        // take over by shortcut_esc
        break;
    case Qt::Key_Enter:
        profile_start();
        break;
    default:
        QMainWindow::keyPressEvent(event);
    }
}

// Log

inline void FastAppendTextDocument(const QString& message, QTextDocument* doc) {
    QTextCursor cursor(doc);
    cursor.movePosition(QTextCursor::End);
    cursor.beginEditBlock();
    cursor.insertBlock();
    cursor.insertText(message);
    cursor.endEditBlock();
}

void MainWindow::append_log(const QString& log) {
    if (log.size() > 20000) {
        append_log(QString("TRUNCATED LONG LOG: ") + log.first(1000) + "...");
        return;
    }
    QMutexLocker locker(&logMutex);
    if (logQueue.size() > 1000) {
        // log is overloaded, just discard it
        return;
    }
    logQueue.enqueue(log);
    if (logQueue.size() == 1) logWaiter.wakeOne();
}

void MainWindow::log_process_loop() {
    while (true) {
        logMutex.lock();
        while (logQueue.isEmpty()) {
            logWaiter.wait(&logMutex);
        }
        auto logLines = logQueue.dequeue().split("\n");

        QString batchToPrint;
        for (const auto& logLine : logLines) {
            if (should_print_log(logLine)) {
                batchToPrint += logLine + "\n";
            }
        }
        logMutex.unlock();

        if (!batchToPrint.isEmpty()) {
            QString trimmedBatch = batchToPrint.trimmed();
            runOnUiThread([trimmedBatch = std::move(trimmedBatch), this] {
                auto bar = ui->masterLogBrowser->verticalScrollBar();
                auto layout = qvLogDocument->documentLayout();
                // Anchor to the block at the top of the viewport; if trim shifts its
                // document-Y afterwards, we replay the original sub-block offset.
                QTextBlock anchorBlock = ui->masterLogBrowser->cursorForPosition(QPoint(0, 0)).block();
                int viewportOffset = bar->value() - static_cast<int>(layout->blockBoundingRect(anchorBlock).y());
                FastAppendTextDocument(trimmedBatch, qvLogDocument);
                if (Configs::dataManager->settingsRepo->log_auto_scroll) {
                    bar->setValue(bar->maximum());
                }
                else if (anchorBlock.isValid()) {
                    int newY = static_cast<int>(layout->blockBoundingRect(anchorBlock).y());
                    bar->setValue(newY + viewportOffset);
                }
                });
        }
    }
}

bool MainWindow::should_print_log(const QString& log) {
    if (log.trimmed().isEmpty()) return false;
    bool result = true;
    if (Configs::dataManager->settingsRepo->log_enable_include) {
        result = false;
        for (const auto& includeKeyword : includeKeywords) {
            if (log.contains(includeKeyword)) {
                result = true;
                break;
            }
        }
        if (!includeCombined.pattern().isEmpty() && includeCombined.match(log).hasMatch()) {
            result = true;
        }
    }
    if (result && Configs::dataManager->settingsRepo->log_enable_exclude) {
        for (const auto& excludeKeyword : excludeKeywords) {
            if (log.contains(excludeKeyword)) {
                result = false;
                break;
            }
        }
        if (!excludeCombined.pattern().isEmpty() && excludeCombined.match(log).hasMatch()) {
            result = false;
        }
    }
    return result;
}

void MainWindow::on_masterLogBrowser_customContextMenuRequested(const QPoint& pos) {
    QMenu* menu = ui->masterLogBrowser->createStandardContextMenu();

    auto sep = new QAction(this);
    sep->setSeparator(true);
    menu->addAction(sep);

    auto action_clear = new QAction(this);
    action_clear->setText(tr("Clear"));
    connect(action_clear, &QAction::triggered, this, [=, this] {
        qvLogDocument->clear();
        ui->masterLogBrowser->clear();
        });
    menu->addAction(action_clear);

    menu->exec(ui->masterLogBrowser->viewport()->mapToGlobal(pos)); // 弹出菜单
}

void MainWindow::on_tabWidget_customContextMenuRequested(
    const QPoint& p)
{
    auto* tabBar =
        ui->tabWidget->tabBar();

    const int clickedIndex =
        tabBar->tabAt(p);


    // =====================================================
    // Clicked on empty tab-bar area
    // =====================================================

    if (clickedIndex == -1)
    {
        QMenu menu(this);


        auto* addAction =
            menu.addAction(
                tr("Add new Group")
            );


        connect(
            addAction,
            &QAction::triggered,
            this,
            [this]()
            {
                auto group =
                    Configs::dataManager
                    ->groupsRepo
                    ->NewGroup();


                auto* dialog =
                    new DialogEditGroup(
                        group,
                        this
                    );


                const int result =
                    dialog->exec();


                dialog->deleteLater();


                if (result !=
                    QDialog::Accepted)
                {
                    return;
                }


                Configs::dataManager
                    ->groupsRepo
                    ->AddGroup(group);


                MW_dialog_message(
                    MwMessage::GroupsChanged,
                    {}
                );
            }
        );


        menu.exec(
            tabBar->mapToGlobal(p)
        );

        return;
    }


    // =====================================================
    // Resolve clicked group
    // =====================================================

    const auto groupsOrder =
        Configs::dataManager
        ->groupsRepo
        ->GetGroupsTabOrder();


    if (clickedIndex < 0 ||
        clickedIndex >= groupsOrder.size())
    {
        return;
    }


    const int clickedGroupId =
        groupsOrder[
            clickedIndex
        ];


    auto group =
        Configs::dataManager
        ->groupsRepo
        ->GetGroup(
            clickedGroupId
        );


    if (!group) {
        return;
    }


    ui->tabWidget
        ->setCurrentIndex(
            clickedIndex
        );


    QMenu menu(this);


    // =====================================================
    // Add group
    // =====================================================

    auto* addAction =
        menu.addAction(
            tr("Add new Group")
        );


    connect(
        addAction,
        &QAction::triggered,
        this,
        [this]()
        {
            auto newGroup =
                Configs::GroupsRepo::
                NewGroup();


            auto* dialog =
                new DialogEditGroup(
                    newGroup,
                    this
                );


            const int result =
                dialog->exec();


            dialog->deleteLater();


            if (result !=
                QDialog::Accepted)
            {
                return;
            }


            Configs::dataManager
                ->groupsRepo
                ->AddGroup(
                    newGroup
                );


            MW_dialog_message(
                MwMessage::GroupsChanged,
                {}
            );
        }
    );


    // =====================================================
    // Delete group
    // =====================================================

    auto* deleteAction =
        new QAction(
            tr("Delete selected Group"),
            &menu
        );


    connect(
        deleteAction,
        &QAction::triggered,
        this,
        [
            this,
            clickedGroupId
        ]()
        {
            auto selectedGroup =
                Configs::dataManager
                ->groupsRepo
                ->GetGroup(
                    clickedGroupId
                );


            if (!selectedGroup) {
                return;
            }


            const auto snapshot =
                selectedGroup
                ->Snapshot();


            const auto answer =
                QMessageBox::question(
                    this,
                    tr("Confirmation"),
                    tr("Remove %1?")
                    .arg(
                        snapshot.name
                    )
                );


            if (answer !=
                QMessageBox::
                StandardButton::Yes)
            {
                return;
            }


            if (running &&
                running->GroupId() ==
                clickedGroupId)
            {
                profile_stop(
                    false,
                    true,
                    false
                );
            }


            Configs::dataManager
                ->groupsRepo
                ->DeleteGroup(
                    clickedGroupId
                );


            MW_dialog_message(
                MwMessage::GroupsChanged,
                {}
            );
        }
    );


    // =====================================================
    // Edit group
    // =====================================================

    auto* editAction =
        new QAction(
            tr("Edit selected Group"),
            &menu
        );


    connect(
        editAction,
        &QAction::triggered,
        this,
        [
            this,
            clickedGroupId
        ]()
        {
            auto selectedGroup =
                Configs::dataManager
                ->groupsRepo
                ->GetGroup(
                    clickedGroupId
                );


            if (!selectedGroup) {
                return;
            }


            auto* dialog =
                new DialogEditGroup(
                    selectedGroup,
                    this
                );


            connect(
                dialog,
                &QDialog::finished,
                this,
                [
                    this,
                    dialog,
                    selectedGroup
                ](int result)
                {
                    if (result ==
                        QDialog::Accepted)
                    {
                        Configs::dataManager
                            ->groupsRepo
                            ->Save(
                                selectedGroup
                            );


                        MW_dialog_message(
                            MwMessage::
                            GroupsChanged,
                            {}
                        );
                    }


                    dialog
                        ->deleteLater();
                }
            );


            dialog->show();
        }
    );


    // =====================================================
    // Common actions
    // =====================================================

    menu.addAction(
        ui->actionRefresh_Column_Widths
    );

    // addAction is already present because it was created
    // through menu.addAction().

    menu.addAction(
        editAction
    );


    if (Configs::dataManager
        ->groupsRepo
        ->GetAllGroupIds()
        .size() > 1)
    {
        menu.addAction(
            deleteAction
        );
    }


    // =====================================================
    // Immutable Group snapshot
    // =====================================================

    const auto groupSnapshot =
        group->Snapshot();


    if (!groupSnapshot
        .profiles
        .isEmpty())
    {
        menu.addAction(
            ui->actionUrl_Test_Group
        );

        menu.addAction(
            ui->actionSpeedtest_Group
        );

        menu.addAction(
            ui->actionResolve_Out_IP
        );

        menu.addAction(
            ui->menu_resolve_domain
        );

        menu.addAction(
            ui->menu_clear_test_result
        );

        menu.addAction(
            ui->menu_delete_repeat
        );

        menu.addAction(
            ui->menu_remove_unavailable
        );

        menu.addAction(
            ui->menu_remove_invalid
        );
    }


    // =====================================================
    // Subscription action
    // =====================================================

    if (!groupSnapshot
        .url
        .isEmpty())
    {
        menu.addAction(
            ui->menu_update_subscription
        );
    }


    // =====================================================
    // Stop testing
    // =====================================================

    const bool isTesting =
        speedtestRunning.load(
            std::memory_order_acquire
        );


    if (isTesting)
    {
        menu.addAction(
            ui->menu_stop_testing
        );
    }


    // =====================================================
    // Show
    // =====================================================

    menu.exec(
        tabBar->mapToGlobal(p)
    );
}

// eventFilter

bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::MouseButtonPress) {
        auto mouseEvent = dynamic_cast<QMouseEvent*>(event);
        if (obj == ui->label_running && mouseEvent->button() == Qt::LeftButton && running != nullptr) {
            url_test_current();
            return true;
        }
        else if (obj == ui->label_inbound && mouseEvent->button() == Qt::LeftButton) {
            on_menu_basic_settings_triggered();
            return true;
        }
        else if (obj == ui->tabWidget && mouseEvent->button() == Qt::RightButton) {
            on_tabWidget_customContextMenuRequested(mouseEvent->position().toPoint());
            return true;
        }
    }
    else if (event->type() == QEvent::MouseButtonDblClick) {
        if (obj == ui->splitter) {
            auto size = ui->splitter->size();
            ui->splitter->setSizes({ size.height() / 2, size.height() / 2 });
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

// profile selector

void MainWindow::start_select_mode(QObject* context, const std::function<void(int)>& callback) {
    select_mode = true;
    connectOnce(this, &MainWindow::profile_selected, context, callback);
    refresh_status();
}

// 连接列表

inline QJsonArray last_arr; // format is nekoray_connections_json

// Hotkey

inline QList<std::shared_ptr<QHotkey>> RegisteredHotkey;

void MainWindow::RegisterHotkey(bool unregister) {
    while (!RegisteredHotkey.isEmpty()) {
        auto hk = RegisteredHotkey.takeFirst();
        hk->deleteLater();
    }
    if (unregister || Configs::dataManager->settingsRepo->prepare_exit) return;

    QStringList regstr{
        Configs::dataManager->settingsRepo->hotkey_mainwindow,
        Configs::dataManager->settingsRepo->hotkey_group,
        Configs::dataManager->settingsRepo->hotkey_route,
        Configs::dataManager->settingsRepo->hotkey_system_proxy_menu,
        Configs::dataManager->settingsRepo->hotkey_toggle_system_proxy,
    };

    for (const auto& key : regstr) {
        if (key.isEmpty()) continue;
        if (regstr.count(key) > 1) return; // Conflict hotkey
    }
    for (const auto& key : regstr) {
        QKeySequence k(key);
        if (k.isEmpty()) continue;
        auto hk = std::make_shared<QHotkey>(k, true);
        if (hk->isRegistered()) {
            RegisteredHotkey += hk;
            connect(hk.get(), &QHotkey::activated, this, [=, this] { HotkeyEvent(key); });
        }
        else {
            hk->deleteLater();
        }
    }
}

void MainWindow::RegisterHiddenMenuShortcuts(bool unregister) {
    for (const auto s : hiddenMenuShortcuts) s->deleteLater();
    hiddenMenuShortcuts.clear();

    if (unregister) return;

    for (const auto& action : ui->menuHidden_menu->actions()) {
        if (!action->shortcut().toString().isEmpty())
        {
            hiddenMenuShortcuts.append(new QShortcut(action->shortcut(), this, [=, this]() {
                action->trigger();
                }));
        }
    }
}

void MainWindow::setActionsData()
{
    // assign ids to menu actions so that we can save and restore them
    ui->menu_add_from_input->setData(QString("m2"));
    ui->menu_clear_test_result->setData(QString("m3"));
    ui->menu_clone->setData(QString("m4"));
    ui->menu_delete_repeat->setData(QString("m6"));
    ui->menu_export_config->setData(QString("m7"));
    ui->menu_qr->setData(QString("m8"));
    ui->menu_remove_invalid->setData(QString("m9"));
    ui->menu_remove_unavailable->setData(QString("m10"));
    ui->menu_reset_traffic->setData(QString("m11"));
    ui->menu_resolve_domain->setData(QString("m12"));
    ui->menu_resolve_selected->setData(QString("m13"));
    ui->menu_scan_qr->setData(QString("m14"));
    ui->menu_stop_testing->setData(QString("m15"));
    ui->menu_update_subscription->setData(QString("m16"));
    ui->actionSpeedtest_Current->setData(QString("m18"));
    ui->actionSpeedtest_Group->setData(QString("m19"));
    ui->actionSpeedtest_Selected->setData(QString("m20"));
    ui->actionUrl_Test_Group->setData(QString("m21"));
    ui->actionUrl_Test_Selected->setData(QString("m22"));
    ui->actionHide_window->setData(QString("m23"));
    ui->actionAdd_profile_from_File->setData(QString("m24"));
    ui->actionRefresh_Column_Widths->setData(QString("m25"));
    ui->actionResolve_Out_IP->setData(QString("m26"));
    ui->actionResolve_Selected_Out_IP->setData(QString("m27"));
    ui->actionCopy_Test_Result->setData(QString("m28"));
    ui->actionClear_Test_Result->setData(QString("m29"));
}

QList<QAction*> MainWindow::getActionsForShortcut()
{
    QList<QAction*> list;
    QList<QAction*> actions = findChildren<QAction*>();

    for (QAction* action : actions) {
        if (action->data().isNull() || action->data().toString().isEmpty()) continue;
        list.append(action);
    }
    return list;
}

void MainWindow::loadShortcuts()
{
    auto mp = Configs::dataManager->settingsRepo->shortcuts;
    for (QList<QAction*> actions = findChildren<QAction*>(); QAction * action : actions)
    {
        if (action->data().isNull() || action->data().toString().isEmpty()) continue;
        // Only apply saved shortcut if user has defined one; preserve default UI shortcuts otherwise
        if (mp.count(action->data().toString()) > 0) {
            action->setShortcut(mp[action->data().toString()]);
        }
    }

    RegisterHiddenMenuShortcuts();
}


void MainWindow::HotkeyEvent(const QString& key) {
    if (key.isEmpty()) return;
    runOnUiThread([=, this] {
        if (key == Configs::dataManager->settingsRepo->hotkey_mainwindow) {
            tray->activated(QSystemTrayIcon::ActivationReason::Trigger);
        }
        else if (key == Configs::dataManager->settingsRepo->hotkey_group) {
            on_menu_manage_groups_triggered();
        }
        else if (key == Configs::dataManager->settingsRepo->hotkey_route) {
            on_menu_routing_settings_triggered();
        }
        else if (key == Configs::dataManager->settingsRepo->hotkey_system_proxy_menu) {
            ui->menu_spmode->popup(QCursor::pos());
        }
        else if (key == Configs::dataManager->settingsRepo->hotkey_toggle_system_proxy) {
            toggle_system_proxy();
        }
        });
}

bool MainWindow::StopVPNProcess() {
    runOnThread([=, this]
        {
            core_process->Kill();
        }, DS_cores, true);

    return true;
}

bool isNewer(QString assetName) {
    if (QString(NKR_VERSION).isEmpty()) return false;
    assetName = assetName.mid(7); // take out Gryph-
    QString version;
    auto spl = assetName.split('-');
    version += spl[0]; // version: 1.2.3
    if (spl[1].contains("beta") || spl[1].contains("alpha") || spl[1].contains("rc")) version += "." + spl[1]; // .beta.13
    auto parts = version.split("."); // [1,2,3,beta,13]
    auto currentParts = QString(NKR_VERSION).replace("-", ".").split('.');
    if (parts.size() < 3 || currentParts.size() < 3)
    {
        MW_show_log("Version strings seem to be invalid" + QString(NKR_VERSION) + " and " + version);
        return false;
    }
    std::vector<int> verNums;
    std::vector<int> currNums;
    // add base version first
    verNums.push_back(parts[0].toInt());
    verNums.push_back(parts[1].toInt());
    verNums.push_back(parts[2].toInt());
    if (parts.size() > 3)
    {
        if (parts[3] == "alpha") verNums.push_back(1);
        if (parts[3] == "beta") verNums.push_back(2);
        if (parts[3] == "rc") verNums.push_back(3);
        if (parts.size() > 4) verNums.push_back(parts[4].toInt());
    }

    currNums.push_back(currentParts[0].toInt());
    currNums.push_back(currentParts[1].toInt());
    currNums.push_back(currentParts[2].toInt());
    if (currentParts.size() > 3)
    {
        if (currentParts[3] == "alpha") currNums.push_back(1);
        if (currentParts[3] == "beta") currNums.push_back(2);
        if (currentParts[3] == "rc") currNums.push_back(3);
        if (currentParts.size() > 4) currNums.push_back(currentParts[4].toInt());
    }

    if (verNums.size() < 3 || currNums.size() < 3)
    {
        MW_show_log("Version strings seem to be invalid" + QString(NKR_VERSION) + " and " + version);
        return false;
    }

    for (int i = 0; i < 3; i++)
    {
        if (verNums[i] > currNums[i]) return true;
        if (verNums[i] < currNums[i]) return false;
    }

    // equal base version, check beta-ness
    if (verNums.size() == 5 && currNums.size() == 3) return false;
    if (verNums.size() == 3 && currNums.size() == 5) return true;
    if (verNums.size() == 5 && currNums.size() == 5)
    {
        for (int i = 3; i < 5; i++)
        {
            if (verNums[i] > currNums[i]) return true;
            if (verNums[i] < currNums[i]) return false;
        }
    }
    else
    {
        MW_show_log("There are no updates. You have the latest version - " + QString(NKR_VERSION));
        return false;
    }
    return false;
}

void MainWindow::CheckUpdate() {
    QString search;
#ifdef Q_OS_WIN
#  ifdef Q_PROCESSOR_ARM_64
    search = "windows-arm64";
#  else
#    ifdef Q_OS_WIN64
    if (WinVersion::IsBuildNumGreaterOrEqual(BuildNumber::Windows_10_1809))
        search = "windows64";
    else
        search = "windowslegacy64";
#    else
    search = "windows32";
#    endif
#  endif
#endif
#ifdef Q_OS_LINUX
#  ifdef Q_PROCESSOR_X86_64
    search = "linux-amd64";
#  else
    search = "linux-arm64";
#  endif
#endif
#ifdef Q_OS_MACOS
#  ifdef Q_PROCESSOR_X86_64
    search = "macos-amd64";
#  else
    search = "macos-arm64";
#  endif
#endif
    if (search.isEmpty()) {
        runOnUiThread([=, this] {
            MessageBoxWarning(QObject::tr("Update"), QObject::tr("Not official support platform"));
            });
        return;
    }

    auto resp = NetworkRequestHelper::HttpGet("https://api.github.com/repos/throneproj/Gryph/releases");
    if (!resp.error.isEmpty()) {
        runOnUiThread([=, this] {
            MessageBoxWarning(QObject::tr("Update"), QObject::tr("Requesting update error: %1").arg(resp.error + "\n" + resp.data));
            });
        return;
    }

    QString assets_name, release_download_url, release_url, release_note, note_pre_release;
    bool exitFlag = false;
    QJsonArray array = QString2QJsonArray(resp.data);
    for (const QJsonValue value : array) {
        QJsonObject release = value.toObject();
        if (release["prerelease"].toBool() && !Configs::dataManager->settingsRepo->allow_beta_update) continue;
        for (const QJsonValue asset : release["assets"].toArray()) {
            if (asset["name"].toString().contains(search) && asset["name"].toString().section('.', -1) == QString("zip")) {
                note_pre_release = release["prerelease"].toBool() ? " (Pre-release)" : "";
                release_url = release["html_url"].toString();
                release_note = release["body"].toString();
                assets_name = asset["name"].toString();
                release_download_url = asset["browser_download_url"].toString();
                exitFlag = true;
                break;
            }
        }
        if (exitFlag) break;
    }

    if (release_download_url.isEmpty() || !isNewer(assets_name)) {
        runOnUiThread([=, this] {
            MessageBoxInfo(QObject::tr("Update"), QObject::tr("No update"));
            });
        return;
    }

    runOnUiThread([=, this] {
        auto allow_updater = !Configs::dataManager->settingsRepo->flag_use_appdata;
        QMessageBox box(QMessageBox::Question, QObject::tr("Update") + note_pre_release,
            QObject::tr("Update found: %1\nRelease note:\n%2").arg(assets_name, release_note));
        //
        QAbstractButton* btn1 = nullptr;
        if (allow_updater) {
            btn1 = box.addButton(QObject::tr("Update"), QMessageBox::AcceptRole);
        }
        QAbstractButton* btn2 = box.addButton(QObject::tr("Open in browser"), QMessageBox::AcceptRole);
        box.addButton(QObject::tr("Close"), QMessageBox::RejectRole);
        box.exec();
        //
        if (btn1 == box.clickedButton() && allow_updater) {
            // Download Update
            runOnNewThread([=, this] {
                if (!mu_download_update.tryLock()) {
                    runOnUiThread([=, this]() {
                        MessageBoxWarning(tr("Cannot start"), tr("Last download request has not finished yet"));
                        });
                    return;
                }
                QString errors;
                if (!release_download_url.isEmpty()) {
                    auto res = NetworkRequestHelper::DownloadAsset(release_download_url, "Gryph.zip");
                    if (!res.isEmpty()) {
                        errors += res;
                    }
                }
                mu_download_update.unlock();
                runOnUiThread([=, this] {
                    if (errors.isEmpty()) {
                        auto q = QMessageBox::question(nullptr, QObject::tr("Update"),
                            QObject::tr("Update is ready, restart to install?"));
                        if (q == QMessageBox::StandardButton::Yes) {
                            this->exit_reason = 1;
                            on_menu_exit_triggered();
                        }
                    }
                    else {
                        MessageBoxWarning(tr("Failed to download update assets"), errors);
                    }
                    });
                });
        }
        else if (btn2 == box.clickedButton()) {
            QDesktopServices::openUrl(QUrl(release_url));
        }
        });
}
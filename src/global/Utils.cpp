#include "include/global/Utils.hpp"
#include "include/ui/MainWindowAPI.h"
#include "3rdparty/QThreadCreateThread.hpp"

#include <random>
#include <utility>

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QEventLoop>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QUrlQuery>
#include <QTcpServer>
#include <QTimer>
#include <QMessageBox>
#include <QFile>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QDateTime>
#include <QLocale>

#ifdef Q_OS_WIN
#include "include/sys/windows/guihelper.h"
#endif
#ifdef Q_OS_MAC
#include <ApplicationServices/ApplicationServices.h>
#endif

namespace
{
    // =========================================================
    // Messages which arrive before MainWindowApi::Initialize()
    // finishes publishing MainWindow.
    // =========================================================

    struct PendingMainWindowMessage
    {
        MwMessage command;

        QStringList arguments;
    };


    QMutex
        g_pendingMainWindowEventsMutex;


    QList<PendingMainWindowMessage>
        g_pendingMainWindowMessages;


    QStringList
        g_pendingMainWindowLogs;


    // Avoid unlimited memory use if something goes badly wrong
    // during application initialization.
    constexpr int
        MAX_PENDING_MAINWINDOW_LOGS = 1000;


    constexpr int
        MAX_PENDING_MAINWINDOW_MESSAGES = 100;
}

// =============================================================
// Stable MainWindow bridges
//
// No MainWindow* is stored here.
// No callback captures `this`.
// =============================================================

void MW_show_log(
    const QString& log)
{
    if (log.isEmpty())
    {
        return;
    }


    // =========================================================
    // Normal path
    //
    // MainWindow already exists.
    // =========================================================

    if (MainWindowApi::DispatchLog(
        log))
    {
        return;
    }


    // =========================================================
    // MainWindow is not published yet.
    //
    // This happens during MainWindow construction because the
    // Core and several background services already start before
    // MainWindowApi::Initialize() can publish g_mainWindow.
    //
    // Do NOT lose this log line.
    // =========================================================

    {
        QMutexLocker locker(
            &g_pendingMainWindowEventsMutex
        );


        // Keep the startup buffer bounded.
        if (g_pendingMainWindowLogs.size()
            >=
            MAX_PENDING_MAINWINDOW_LOGS)
        {
            g_pendingMainWindowLogs
                .removeFirst();
        }


        g_pendingMainWindowLogs
            .append(
                log
            );
    }


    // Also keep the message visible in debugger/console.
    qInfo()
        .noquote()
        << log;
}

void MW_dialog_message(
    MwMessage cmd,
    QStringList args)
{
    // =========================================================
    // Normal delivery
    // =========================================================

    if (MainWindowApi::DispatchMessage(
        cmd,
        args))
    {
        return;
    }


    // =========================================================
    // MainWindow is still being constructed.
    //
    // Preserve the message and replay it immediately after
    // MainWindowApi publishes the window.
    // =========================================================

    {
        QMutexLocker locker(
            &g_pendingMainWindowEventsMutex
        );


        if (g_pendingMainWindowMessages.size()
            >=
            MAX_PENDING_MAINWINDOW_MESSAGES)
        {
            // Dropping the oldest event is preferable to
            // unbounded startup memory growth.
            g_pendingMainWindowMessages
                .removeFirst();
        }


        PendingMainWindowMessage
            pending;


        pending.command =
            cmd;


        pending.arguments =
            std::move(args);


        g_pendingMainWindowMessages
            .append(
                std::move(pending)
            );
    }
}

void MW_FlushPendingMainWindowEvents()
{
    // =========================================================
    // Move pending data into local containers.
    //
    // Do NOT hold the pending mutex while invoking MainWindow.
    // MainWindow callbacks may themselves log messages.
    // =========================================================

    QList<PendingMainWindowMessage>
        messages;


    QStringList
        logs;


    {
        QMutexLocker locker(
            &g_pendingMainWindowEventsMutex
        );


        messages.swap(
            g_pendingMainWindowMessages
        );


        logs.swap(
            g_pendingMainWindowLogs
        );
    }


    // =========================================================
    // Deliver control messages first.
    //
    // CoreStarted, for example, updates application state.
    // =========================================================

    QList<PendingMainWindowMessage>
        undeliveredMessages;


    for (auto& pending :
        messages)
    {
        const bool delivered =
            MainWindowApi::DispatchMessage(
                pending.command,
                pending.arguments
            );


        if (!delivered)
        {
            undeliveredMessages
                .append(
                    std::move(pending)
                );
        }
    }


    // =========================================================
    // Then replay startup log.
    // =========================================================

    QStringList
        undeliveredLogs;


    for (const auto& log :
        logs)
    {
        if (!MainWindowApi::DispatchLog(
            log))
        {
            undeliveredLogs
                .append(
                    log
                );
        }
    }


    // =========================================================
    // Extremely defensive:
    //
    // If the MainWindow disappeared again during the flush,
    // return the undelivered events to the queue.
    // =========================================================

    if (undeliveredMessages.isEmpty()
        &&
        undeliveredLogs.isEmpty())
    {
        return;
    }


    {
        QMutexLocker locker(
            &g_pendingMainWindowEventsMutex
        );


        // Older messages must remain before newer messages
        // which may have appeared while we were flushing.

        if (!undeliveredMessages.isEmpty())
        {
            QList<PendingMainWindowMessage>
                merged;


            merged.reserve(
                undeliveredMessages.size()
                +
                g_pendingMainWindowMessages.size()
            );


            merged.append(
                undeliveredMessages
            );


            merged.append(
                g_pendingMainWindowMessages
            );


            g_pendingMainWindowMessages =
                std::move(merged);
        }


        if (!undeliveredLogs.isEmpty())
        {
            QStringList
                merged;


            merged.reserve(
                undeliveredLogs.size()
                +
                g_pendingMainWindowLogs.size()
            );


            merged.append(
                undeliveredLogs
            );


            merged.append(
                g_pendingMainWindowLogs
            );


            g_pendingMainWindowLogs =
                std::move(merged);
        }
    }
}

QStringList SplitLines(const QString &_string) {
    return _string.split(QRegularExpression("[\r\n]"), Qt::SplitBehaviorFlags::SkipEmptyParts);
}

QStringList SplitLinesSkipSharp(const QString &_string, int maxLine) {
    auto lines = SplitLines(_string);
    QStringList newLines;
    int i = 0;
    for (const auto &line: lines) {
        if (line.trimmed().startsWith("#")) continue;
        newLines << line;
        if (maxLine > 0 && ++i >= maxLine) break;
    }
    return newLines;
}

QByteArray DecodeB64IfValid(const QString &input, QByteArray::Base64Options options) {
    QByteArray::Base64Options newOptions = options | QByteArray::Base64Option::AbortOnBase64DecodingErrors;
    auto result = QByteArray::fromBase64Encoding(input.toUtf8(), newOptions);
    if (result) {
        return result.decoded;
    }
    return {};
}

QStringList SplitAndTrim(const QString& raw, const QString& separator, bool keepEmpty) {
    QStringList result;
    auto spl = raw.split(separator);
    for (const auto& str : spl) {
        auto trimmed = str.trimmed();
        if (!keepEmpty && trimmed.isEmpty()) continue;
        result << trimmed;
    }
    return result;
}

QString QStringList2Command(const QStringList &list) {
    QStringList new_list;
    for (auto str: list) {
        auto q = "\"" + str.replace("\"", "\\\"") + "\"";
        new_list << q;
    }
    return new_list.join(" ");
}

QString GetQueryValue(const QUrlQuery &q, const QString &key, const QString &def) {
    auto a = q.queryItemValue(key);
    if (a.isEmpty()) {
        return def;
    }
    return a;
}

QString GetRandomString(int randomStringLength) {
    std::random_device rd;
    std::mt19937 mt(rd());

    const QString possibleCharacters("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");

    std::uniform_int_distribution<int> dist(0, possibleCharacters.length() - 1);

    QString randomString;
    for (int i = 0; i < randomStringLength; ++i) {
        QChar nextChar = possibleCharacters.at(dist(mt));
        randomString.append(nextChar);
    }
    return randomString;
}

quint64 GetRandomUint64() {
    std::random_device rd;
    std::mt19937 mt(rd());
    std::uniform_int_distribution<quint64> dist;
    return dist(mt);
}

QString GenRandomLoopback() {
#ifdef Q_OS_MACOS
    return "127.0.0.1";
#else
    std::random_device rd;
    std::mt19937 mt(rd());
    std::uniform_int_distribution<int> octet(1, 254);
    return QString("127.%1.%2.%3").arg(octet(mt)).arg(octet(mt)).arg(octet(mt));
#endif
}

// QString >> QJson
QJsonObject QString2QJsonObject(const QString &jsonString) {
    QJsonDocument jsonDocument = QJsonDocument::fromJson(jsonString.toUtf8());
    QJsonObject jsonObject = jsonDocument.object();
    return jsonObject;
}

// QJson >> QString
QString QJsonObject2QString(const QJsonObject &jsonObject, bool compact) {
    return QJsonDocument(jsonObject).toJson(compact ? QJsonDocument::Compact : QJsonDocument::Indented);
}

QJsonArray QListStr2QJsonArray(const QList<QString> &list) {
    QVariantList list2;
    bool isEmpty = true;
    for (auto &item: list) {
        if (item.trimmed().isEmpty()) continue;
        list2.append(item);
        isEmpty = false;
    }

    if (isEmpty) return {};
    else return QJsonArray::fromVariantList(list2);
}

QJsonArray QListInt2QJsonArray(const QList<int> &list) {
    QVariantList list2;
    for (auto &item: list)
        list2.append(item);
    return QJsonArray::fromVariantList(list2);
}

QList<int> QJsonArray2QListInt(const QJsonArray &arr) {
    QList<int> list2;
    for (auto item: arr)
        list2.append(item.toInt());
    return list2;
}

QList<QString> QJsonArray2QListString(const QJsonArray &arr) {
    QList<QString> list2;
    for (auto item: arr)
        list2.append(item.toString());
    return list2;
}

QJsonArray QString2QJsonArray(const QString& str) {
    auto doc = QJsonDocument::fromJson(str.toUtf8());
    if (doc.isArray()) {
        return doc.array();
    }
    return {};
}

QJsonObject QMapString2QJsonObject(const QMap<QString,QString> &mp) {
    QJsonObject res;
    for (const auto &key: mp.keys()) {
        res.insert(key, mp[key]);
    }

    return res;
}

QList<QString> QListInt2QListString(const QList<int> &list) {
    QList<QString> resp;
    for (int item : list) resp << Int2String(item);
    return resp;
}

QByteArray ReadFile(const QString &path) {
    QFile file(path);
    file.open(QFile::ReadOnly);
    return file.readAll();
}

QString ReadFileText(const QString &path) {
    QFile file(path);
    file.open(QFile::ReadOnly | QFile::Text);
    QTextStream stream(&file);
    return stream.readAll();
}

int MkPort() {
    QTcpServer s;
    s.listen();
    auto port = s.serverPort();
    s.close();
    return port;
}

QList<int> MkManyPorts(int num) {
    QList<int> res;
    QList<QTcpServer*> servers;
    for (int i=0;i<num;i++) {
        auto server = new QTcpServer();
        server->listen();
        servers.append(server);
        res.append(server->serverPort());
    }
    for (const auto s: servers) {
        s->close();
        delete s;
    }
    servers.clear();
    return res;
}

QString ReadableSize(const qint64 &size) {
    double sizeAsDouble = size;
    static QStringList measures;
    if (measures.isEmpty())
        measures << "B"
                 << "KiB"
                 << "MiB"
                 << "GiB"
                 << "TiB"
                 << "PiB"
                 << "EiB"
                 << "ZiB"
                 << "YiB";
    QStringListIterator it(measures);
    QString measure(it.next());
    while (sizeAsDouble >= 1024.0 && it.hasNext()) {
        measure = it.next();
        sizeAsDouble /= 1024.0;
    }
    return QString::fromLatin1("%1 %2").arg(sizeAsDouble, 0, 'f', 2).arg(measure);
}

bool IsIpAddress(const QString &str) {
    auto address = QHostAddress(str);
    if (address.protocol() == QAbstractSocket::IPv4Protocol || address.protocol() == QAbstractSocket::IPv6Protocol)
        return true;
    return false;
}

bool IsIpAddressV4(const QString &str) {
    auto address = QHostAddress(str);
    if (address.protocol() == QAbstractSocket::IPv4Protocol)
        return true;
    return false;
}

bool IsIpAddressV6(const QString &str) {
    auto address = QHostAddress(str);
    if (address.protocol() == QAbstractSocket::IPv6Protocol)
        return true;
    return false;
}

QString DisplayTime(long long time, int formatType) {
    QDateTime t;
    t.setMSecsSinceEpoch(time * 1000);
    return QLocale().toString(t, QLocale::FormatType(formatType));
}

QWidget* GetMessageBoxParent()
{
    QWidget* activeWindow = QApplication::activeWindow();

    if (activeWindow != nullptr) {
        return activeWindow;
    }

    QWidget* mainWindow = MainWindowApi::Widget();

    if (mainWindow != nullptr && mainWindow->isVisible()) {
        return mainWindow;
    }

    return nullptr;
}

int MessageBoxWarning(const QString &title, const QString &text) {
    return QMessageBox::warning(GetMessageBoxParent(), title, text);
}

int MessageBoxInfo(const QString &title, const QString &text) {
    return QMessageBox::information(GetMessageBoxParent(), title, text);
}

void ActivateWindow(QWidget *w) {
    w->setWindowState(w->windowState() & ~Qt::WindowMinimized);
    w->setVisible(true);
#ifdef Q_OS_WIN
    Windows_QWidget_SetForegroundWindow(w);
#elif defined(Q_OS_MAC)
    ProcessSerialNumber psn = { 0, kCurrentProcess };
    TransformProcessType(&psn, kProcessTransformToForegroundApplication);
#endif
    w->raise();
    w->activateWindow();
}

void HideWindow(QWidget *w) {
    w->hide();
#ifdef Q_OS_MAC
    ProcessSerialNumber psn = { 0, kCurrentProcess };
    TransformProcessType(&psn, kProcessTransformToUIElementApplication);
#endif
}

// =============================================================
// Context-bound UI dispatcher
// =============================================================

bool runOnUiThread(
    QObject* context,
    const std::function<void()>& callback,
    bool wait)
{
    // =========================================================
    // Validate callback
    // =========================================================

    if (!callback)
    {
        return false;
    }


    // =========================================================
    // QApplication/QCoreApplication is the canonical owner of
    // the UI thread.
    //
    // Do NOT use MainWindowApi::Widget() for discovering the UI
    // thread:
    //
    // MainWindow may:
    //   - not exist yet during startup;
    //   - already be detached during shutdown.
    //
    // QApplication still gives us the correct UI thread.
    // =========================================================

    QCoreApplication* app =
        QCoreApplication::instance();


    if (!app)
    {
        // -----------------------------------------------------
        // CRITICAL:
        //
        // Never execute callback() here.
        //
        // We do not know whether the caller is the UI thread.
        // Executing it directly would violate runOnUiThread()
        // semantics.
        // -----------------------------------------------------

        qWarning()
            << "runOnUiThread: "
            "QCoreApplication does not exist; "
            "callback discarded.";

        return false;
    }


    // =========================================================
    // During QCoreApplication destruction the event loop can no
    // longer be relied upon.
    //
    // Queuing work at this stage may either never execute or,
    // with a blocking request, cause a shutdown deadlock.
    // =========================================================

    if (QCoreApplication::closingDown())
    {
        qWarning()
            << "runOnUiThread: "
            "application is shutting down; "
            "callback discarded.";

        return false;
    }


    QThread* const uiThread =
        app->thread();


    if (!uiThread)
    {
        qWarning()
            << "runOnUiThread: "
            "application has no UI thread.";

        return false;
    }


    // =========================================================
    // Choose QObject context
    //
    // No explicit context:
    //     QApplication becomes the context.
    //
    // Explicit context:
    //     queued callback is tied to that QObject's lifetime.
    // =========================================================

    QObject* target =
        context
        ? context
        : static_cast<QObject*>(app);


    if (!target)
    {
        return false;
    }


    // =========================================================
    // Contract enforcement
    //
    // runOnUiThread() must NEVER dispatch to an object which
    // belongs to another thread.
    // =========================================================

    if (target->thread() !=
        uiThread)
    {
        qWarning()
            << "runOnUiThread: supplied context does not "
            "belong to the QApplication/UI thread.";

        return false;
    }


    // =========================================================
    // Already on UI thread
    //
    // Execute synchronously.
    // =========================================================

    if (QThread::currentThread() ==
        uiThread)
    {
        callback();

        return true;
    }


    // =========================================================
    // Worker -> UI, asynchronous
    //
    // QMetaObject::invokeMethod with QObject context has an
    // important property:
    //
    // If `target` dies before callback delivery, Qt removes the
    // pending invocation instead of executing it against the
    // destroyed QObject.
    // =========================================================

    if (!wait)
    {
        const bool queued =
            QMetaObject::invokeMethod(
                target,

                [
                    callback
                ]()
                {
                    callback();
                },

                        Qt::QueuedConnection
                        );


        if (!queued)
        {
            qWarning()
                << "runOnUiThread: "
                "failed to queue callback.";
        }


        return queued;
    }


    // =========================================================
    // Worker -> UI, synchronous
    //
    // The caller explicitly requested wait=true.
    //
    // BlockingQueuedConnection waits until callback has
    // completed in the UI thread.
    //
    // We already handled the same-thread case above, therefore
    // BlockingQueuedConnection cannot self-deadlock merely from
    // being called on the UI thread.
    // =========================================================

    const bool invoked =
        QMetaObject::invokeMethod(
            target,

            [
                callback
            ]()
            {
                callback();
            },

                    Qt::BlockingQueuedConnection
                    );


    if (!invoked)
    {
        qWarning()
            << "runOnUiThread: "
            "failed to execute blocking callback.";
    }


    return invoked;
}

// =============================================================
// Default UI dispatcher
//
// Application object itself is used as QObject context.
//
// This overload is source-compatible with all existing:
//
//     runOnUiThread(lambda);
//     runOnUiThread(lambda, true);
//
// calls.
// =============================================================
bool runOnUiThread(
    const std::function<void()>& callback,
    bool wait)
{
    QCoreApplication* app =
        QCoreApplication::instance();


    if (!app)
    {
        // Again: NEVER fall back to callback() in caller thread.
        qWarning()
            << "runOnUiThread: "
            "QCoreApplication does not exist; "
            "callback discarded.";

        return false;
    }


    return runOnUiThread(
        app,
        callback,
        wait
    );
}

namespace
{
    QMutex
        g_pendingDeeplinkMutex;


    QString
        g_pendingDeeplink;
}


void MW_handle_deeplink(
    const QString& url)
{
    Deeplink_Submit(
        url
    );
}


QString Deeplink_ExtractFromArgs(
    const QStringList& args)
{
    for (const auto& arg :
        args)
    {
        if (arg.startsWith(
            QStringLiteral(
                "Gryph://"
            )))
        {
            return arg;
        }
    }


    return {};
}


void Deeplink_Submit(
    const QString& url)
{
    if (url.isEmpty() ||
        !url.startsWith(
            QStringLiteral(
                "Gryph://"
            )))
    {
        return;
    }


    // =========================================================
    // Try immediate/context-bound delivery.
    // =========================================================

    if (MainWindowApi::
        DispatchDeeplink(
            url
        ))
    {
        return;
    }


    // =========================================================
    // MainWindow does not exist yet.
    //
    // Preserve URL for Deeplink_FlushPending().
    // =========================================================

    {
        QMutexLocker locker(
            &g_pendingDeeplinkMutex
        );


        g_pendingDeeplink =
            url;
    }
}


void Deeplink_FlushPending()
{
    QString url;


    // ---------------------------------------------------------
    // Take stable copy.
    // ---------------------------------------------------------

    {
        QMutexLocker locker(
            &g_pendingDeeplinkMutex
        );


        if (g_pendingDeeplink
            .isEmpty())
        {
            return;
        }


        url =
            g_pendingDeeplink;
    }


    // ---------------------------------------------------------
    // Window still unavailable.
    //
    // Keep the pending value.
    // ---------------------------------------------------------

    if (!MainWindowApi::
        DispatchDeeplink(
            url
        ))
    {
        return;
    }


    // ---------------------------------------------------------
    // Successfully dispatched.
    //
    // Clear only if nobody replaced pending URL meanwhile.
    // ---------------------------------------------------------

    {
        QMutexLocker locker(
            &g_pendingDeeplinkMutex
        );


        if (g_pendingDeeplink
            ==
            url)
        {
            g_pendingDeeplink
                .clear();
        }
    }
}

void runOnNewThread(const std::function<void()> &callback, bool wait) {
    auto *timer = new QTimer();
    auto thread = new QThread();
    timer->moveToThread(thread);
    timer->setSingleShot(true);

    thread->start();
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);

    QEventLoop loop;
    QObject::connect(timer, &QTimer::timeout, [=, &loop]() {
        callback();
        timer->deleteLater();
        QMetaObject::invokeMethod(thread, "quit", Qt::QueuedConnection);

        if (wait)
        {
            QMetaObject::invokeMethod(&loop, "quit", Qt::QueuedConnection);
        }
    });
    QMetaObject::invokeMethod(timer, "start", Qt::QueuedConnection, Q_ARG(int, 0));

    if (wait && QThread::currentThread() != thread) {
        loop.exec();
    }
}

void runOnThread(const std::function<void()> &callback, QObject *parent, bool wait) {
    auto *timer = new QTimer();
    auto thread = dynamic_cast<QThread *>(parent);
    if (thread == nullptr) {
        timer->moveToThread(parent->thread());
        thread = parent->thread();
    } else {
        timer->moveToThread(thread);
    }
    timer->setSingleShot(true);

    QEventLoop loop;
    QObject::connect(timer, &QTimer::timeout, [=, &loop]() {
        callback();
        timer->deleteLater();

        if (wait)
        {
            QMetaObject::invokeMethod(&loop, "quit", Qt::QueuedConnection);
        }
    });
    QMetaObject::invokeMethod(timer, "start", Qt::QueuedConnection, Q_ARG(int, 0));

    if (wait && QThread::currentThread() != thread) {
        loop.exec();
    }
}

void setTimeout(const std::function<void()> &callback, QObject *obj, int timeout) {
    auto t = new QTimer;
    QObject::connect(t, &QTimer::timeout, obj, [=] {
        callback();
        t->deleteLater();
    });
    t->setSingleShot(true);
    t->setInterval(timeout);
    t->start();
}

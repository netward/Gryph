#include "include/global/TaskExecutor.hpp"

#include <algorithm>
#include <atomic>
#include <exception>
#include <utility>

#include <QList>
#include <QMutex>
#include <QMutexLocker>
#include <QEventLoop>
#include <QSemaphore>
#include <QCoreApplication>
#include <QDebug>
#include <QThread>
#include <QThreadPool>


namespace
{
    thread_local bool g_insideBackgroundPool = false;

    // RAII marker for shared pool worker execution.
    class BackgroundPoolExecutionGuard final
    {
    public:
        BackgroundPoolExecutionGuard():previous_(g_insideBackgroundPool)
        {
            g_insideBackgroundPool = true;
        }

        ~BackgroundPoolExecutionGuard()
        {
            g_insideBackgroundPool = previous_;
        }
        
        BackgroundPoolExecutionGuard(const BackgroundPoolExecutionGuard&) = delete;
        BackgroundPoolExecutionGuard&operator=(const BackgroundPoolExecutionGuard&) = delete;

    private:
        bool previous_;
    };

    // Holder for shared Gryph background thread pool.
    class BackgroundPoolHolder final
    {
    public:
        BackgroundPoolHolder()
        {
            int ideal = QThread::idealThreadCount();

            if (ideal <= 0)
            {
                ideal = 4;
            }

            // Gryph has many I/O / RPC tasks which spend part of
            // their lifetime waiting rather than consuming CPU.
            //
            // Therefore using slightly more worker threads than
            // CPU cores is reasonable.
            //
            // Lower bound:
            //     4
            //
            // Upper bound:
            //     16
            //
            // Example:
            //
            // 2 cores  -> 4 workers
            // 4 cores  -> 8 workers
            // 8 cores  -> 16 workers
            // 16 cores -> 16 workers
            const int maxThreads = std::clamp(ideal * 2, 4, 16);
            pool.setMaxThreadCount(maxThreads);
            // Unused worker threads may be destroyed after 30 seconds. New tasks will recreate them when necessary.
            pool.setExpiryTimeout(30'000);
            pool.setObjectName(QStringLiteral("GryphBackgroundPool"));
        }

        QThreadPool pool;
        // Long-running workers, managed separately from QThreadPool.
        QMutex dedicatedMutex;
        QList<QThread*> dedicatedThreads;
        // Once shutdown starts no new tasks should enter pool.
        std::atomic_bool acceptingTasks{ true };
    };


    BackgroundPoolHolder&holder()
    {
        // C++ static-local initialization is thread-safe.
        static BackgroundPoolHolder instance;
        return instance;
    }

    // Execute task without allowing a C++ exception to escape from the worker entry point.
    void executeTask(std::function<void()>& task) noexcept
    {
        try
        {
            task();
        }

        catch (const std::exception& e)
        {
            qCritical() << "Unhandled exception in background task:" << e.what();
        }

        catch (...)
        {
            qCritical() << "Unknown exception in background task.";
        }
    }
}

// Shared thread pool
QThreadPool&Async::backgroundPool()
{
    return holder().pool;
}

// Finite background task
void Async::run(std::function<void()> task, bool wait)
{
    if (!task)
    {
        return;
    }

    auto& state = holder();

    if (!state.acceptingTasks.load(std::memory_order_acquire))
    {
        return;
    }

    if (QCoreApplication::closingDown())
    {
        return;
    }

    if (wait && g_insideBackgroundPool)
    {
        executeTask(task);
        return;
    }

    if (!wait)
    {
        state.pool.start(
            [task = std::move(task)]() mutable
            {
                BackgroundPoolExecutionGuard executionGuard;
                executeTask(task);
            }
        );
        return;
    }

    // Blocking execution
    QCoreApplication* const app = QCoreApplication::instance();

    if (!app)
    {
        return;
    }

    const bool callerIsUiThread = QThread::currentThread() == app->thread();

    if (callerIsUiThread)
    {
        QEventLoop waitLoop;

        state.pool.start(
            [task = std::move(task), &waitLoop]() mutable
            {
                BackgroundPoolExecutionGuard executionGuard;
                executeTask(task);
                QMetaObject::invokeMethod(&waitLoop, "quit", Qt::QueuedConnection);
            }
        );

        waitLoop.exec();
        return;
    }

    QSemaphore completion(0);

    state.pool.start(
        [task = std::move(task), &completion]() mutable
        {
            BackgroundPoolExecutionGuard executionGuard;
            executeTask(task);
            completion.release();
        }
    );
    completion.acquire();
}

// Dedicated long-running worker
void Async::runDedicated(std::function<void()> task, const char* threadName)
{
    if (!task)
    {
        return;
    }

    auto& state = holder();
    QMutexLocker locker(&state.dedicatedMutex);

    if (!state.acceptingTasks.load(std::memory_order_acquire))
    {
        return;
    }

    if (QCoreApplication::closingDown())
    {
        return;
    }

    QThread* thread = QThread::create(
        [task = std::move(task)]() mutable
        {
            executeTask(task);
        }
    );

    if (threadName && *threadName != '\0')
    {
        thread->setObjectName(QString::fromUtf8(threadName));
    }

    state.dedicatedThreads.append(thread);
    thread->start();
}

// Background pool shutdown

void Async::shutdown()
{
    auto& state = holder();
    QList<QThread*> threads;

    {
        QMutexLocker locker(&state.dedicatedMutex);
        const bool wasAccepting = state.acceptingTasks.exchange(false, std::memory_order_acq_rel);

        if (!wasAccepting)
        {
            return;
        }

        threads.swap(state.dedicatedThreads);
    }

    // Finish finite tasks from the shared pool.
    state.pool.waitForDone();
    // Wait until every dedicated worker has actually exited.
    for (QThread* thread : threads)
    {
        if (!thread)
        {
            continue;
        }

        qInfo() << "[Async] Waiting for" << thread->objectName();
        thread->wait();
        qInfo() << "[Async] Stopped" << thread->objectName();
        delete thread;
    }

    qInfo() << "[Async] All background workers stopped";
}

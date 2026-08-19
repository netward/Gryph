#include "include/global/TaskExecutor.hpp"

#include <algorithm>
#include <atomic>
#include <exception>
#include <utility>

#include <QEventLoop>
#include <QSemaphore>
#include <QCoreApplication>
#include <QDebug>
#include <QThread>
#include <QThreadPool>


namespace
{

    // =============================================================
// True while code is already running as a task of our shared
// background pool.
//
// This prevents a worker from enqueueing another blocking task
// into the same pool and then waiting for it while the pool is
// saturated.
// =============================================================

    thread_local bool
        g_insideBackgroundPool =
        false;


    // =============================================================
    // RAII marker for shared pool worker execution.
    // =============================================================

    class BackgroundPoolExecutionGuard final
    {
    public:
        BackgroundPoolExecutionGuard()
            :
            previous_(
                g_insideBackgroundPool
            )
        {
            g_insideBackgroundPool =
                true;
        }


        ~BackgroundPoolExecutionGuard()
        {
            g_insideBackgroundPool =
                previous_;
        }


        BackgroundPoolExecutionGuard(
            const BackgroundPoolExecutionGuard&
        ) = delete;


        BackgroundPoolExecutionGuard&
            operator=(
                const BackgroundPoolExecutionGuard&
                ) = delete;


    private:
        bool previous_;
    };

    // =========================================================
    // Holder for shared Gryph background thread pool.
    // =========================================================

    class BackgroundPoolHolder final
    {
    public:
        BackgroundPoolHolder()
        {
            int ideal =
                QThread::idealThreadCount();

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
            const int maxThreads =
                std::clamp(
                    ideal * 2,
                    4,
                    16
                );


            pool.setMaxThreadCount(
                maxThreads
            );


            // Unused worker threads may be destroyed after
            // 30 seconds. New tasks will recreate them when
            // necessary.
            pool.setExpiryTimeout(
                30'000
            );


            pool.setObjectName(
                QStringLiteral(
                    "GryphBackgroundPool"
                )
            );
        }


        QThreadPool pool;


        // Once shutdown starts no new tasks should enter pool.
        std::atomic_bool acceptingTasks{
            true
        };
    };


    BackgroundPoolHolder&
        holder()
    {
        // C++ static-local initialization is thread-safe.
        static BackgroundPoolHolder instance;

        return instance;
    }


    // =========================================================
    // Execute task without allowing a C++ exception to escape
    // from the worker entry point.
    // =========================================================

    void executeTask(
        std::function<void()>& task
    ) noexcept
    {
        try
        {
            task();
        }

        catch (const std::exception& e)
        {
            qCritical()
                << "Unhandled exception in background task:"
                << e.what();
        }

        catch (...)
        {
            qCritical()
                << "Unknown exception in background task.";
        }
    }
}


// =============================================================
// Shared thread pool
// =============================================================

QThreadPool&
Async::backgroundPool()
{
    return holder().pool;
}


// =============================================================
// Finite background task
// =============================================================

void Async::run(
    std::function<void()> task,
    bool wait
)
{
    // ---------------------------------------------------------
    // Nothing to execute.
    // ---------------------------------------------------------
    if (!task)
    {
        return;
    }


    auto& state =
        holder();


    // ---------------------------------------------------------
    // Do not accept new work once application shutdown begins.
    // ---------------------------------------------------------
    if (!state.acceptingTasks.load(
        std::memory_order_acquire))
    {
        return;
    }


    if (QCoreApplication::closingDown())
    {
        return;
    }


    // =========================================================
    // IMPORTANT:
    //
    // If a worker which already belongs to this pool asks us to
    // execute ANOTHER task synchronously, queueing and waiting
    // could cause pool starvation:
    //
    // worker 1 waits for task A
    // worker 2 waits for task B
    // ...
    // no worker remains to execute A/B
    //
    // Execute nested blocking task inline instead.
    // =========================================================

    if (wait &&
        g_insideBackgroundPool)
    {
        executeTask(
            task
        );

        return;
    }


    // =========================================================
    // Normal asynchronous execution
    // =========================================================

    if (!wait)
    {
        state.pool.start(
            [
                task = std::move(task)
            ]() mutable
            {
                BackgroundPoolExecutionGuard
                    executionGuard;


                executeTask(
                    task
                );
            }
                    );


        return;
    }


    // =========================================================
    // Blocking execution
    // =========================================================

    QCoreApplication* const app =
        QCoreApplication::instance();


    if (!app)
    {
        return;
    }


    const bool callerIsUiThread =
        QThread::currentThread()
        ==
        app->thread();


    // =========================================================
    // UI caller
    //
    // DO NOT use QSemaphore here.
    //
    // The background worker can itself call:
    //
    //     runOnUiThread(..., true)
    //
    // If the UI thread were blocked on a semaphore:
    //
    // UI waits for worker
    // worker waits for UI
    //         ↓
    //      DEADLOCK
    //
    // A nested QEventLoop preserves the original semantics of
    // runOnNewThread(..., true): the caller waits, but UI events
    // continue to be dispatched.
    // =========================================================

    if (callerIsUiThread)
    {
        QEventLoop waitLoop;


        state.pool.start(
            [
                task = std::move(task),
                &waitLoop
            ]() mutable
            {
                BackgroundPoolExecutionGuard
                    executionGuard;


                executeTask(
                    task
                );


                // Signal completion back to the thread which
                // owns waitLoop.
                QMetaObject::invokeMethod(
                    &waitLoop,
                    "quit",
                    Qt::QueuedConnection
                );
            }
        );


        waitLoop.exec();


        return;
    }


    // =========================================================
    // Non-UI caller
    //
    // No UI deadlock is possible merely from blocking this
    // worker thread. A semaphore is cheaper than creating a
    // nested event loop here.
    // =========================================================

    QSemaphore completion(
        0
    );


    state.pool.start(
        [
            task = std::move(task),
            &completion
        ]() mutable
        {
            BackgroundPoolExecutionGuard
                executionGuard;


            executeTask(
                task
            );


            completion.release();
        }
    );


    completion.acquire();
}

// =============================================================
// Dedicated long-running worker
// =============================================================

void Async::runDedicated(
    std::function<void()> task,
    const char* threadName
)
{
    if (!task)
    {
        return;
    }


    if (!holder().acceptingTasks.load(
        std::memory_order_acquire))
    {
        return;
    }


    QThread* thread =
        QThread::create(
            [
                task = std::move(task)
            ]() mutable
            {
                executeTask(
                    task
                );
            }
                    );


    if (threadName &&
        *threadName != '\0')
    {
        thread->setObjectName(
            QString::fromUtf8(
                threadName
            )
        );
    }


    // If worker exits normally, clean the QThread QObject.
    QObject::connect(
        thread,
        &QThread::finished,

        thread,
        &QObject::deleteLater
    );


    thread->start();
}


// =============================================================
// Background pool shutdown
// =============================================================

void Async::shutdown()
{
    auto& state =
        holder();


    const bool wasAccepting =
        state.acceptingTasks.exchange(
            false,
            std::memory_order_acq_rel
        );


    if (!wasAccepting)
    {
        return;
    }


    // Do NOT clear queued tasks here.
    //
    // A task may have been submitted by Async::run(..., true).
    // Removing it from the queue would prevent its completion
    // notification and could leave the waiting caller blocked.
    //
    // Finish all already accepted work deterministically.
    state.pool.waitForDone();
}
#pragma once

#include <functional>

class QThreadPool;

namespace Async
{
    // =========================================================
    // General background task pool.
    //
    // ONLY for finite tasks:
    //
    //   - HTTP requests
    //   - sorting
    //   - subscription updates
    //   - profile processing
    //   - device info
    //   - update checks
    //   - finite RPC operations
    //
    // DO NOT put infinite service loops here.
    // =========================================================

    QThreadPool& backgroundPool();


    // =============================================================
    // Execute a finite task in shared background pool.
    //
    // wait == false:
    //     enqueue task and return immediately.
    //
    // wait == true:
    //     execute task in background pool and wait until it finishes.
    //
    // When called from the UI thread with wait == true, a nested
    // QEventLoop is used instead of blocking the UI thread. This is
    // required because the background task itself may perform
    // runOnUiThread(..., true).
    // =============================================================
    void run(
        std::function<void()> task,
        bool wait = false
    );


    // =========================================================
    // Dedicated thread.
    //
    // ONLY for long-running / permanent workers.
    //
    // Example:
    //
    //   TrafficLooper::Loop()
    //   ConnectionLister::Loop()
    // =========================================================

    void runDedicated(
        std::function<void()> task,
        const char* threadName = nullptr
    );


    // =========================================================
    // Stop accepting new pool tasks and wait for running tasks.
    //
    // Must be called while QApplication still exists.
    // =========================================================

    void shutdown();
}
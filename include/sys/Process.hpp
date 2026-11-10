#pragma once

#include <QAtomicInt>
#include <QProcess>
#include <QString>
#include <QStringList>

namespace Configs_sys {

    class CoreProcess : public QProcess
    {
    public:
        explicit CoreProcess(
            const QString& corePath,
            const QString& socketName,
            bool debugMode,
            QObject* parent = nullptr
        );

        ~CoreProcess() override = default;

        // Low-level process operations only.
        void Start();
        void Kill();

        QString program;
        QStringList arguments;

    private:
        QString socketName_;
        bool debugMode_ = false;
    };

    inline QAtomicInt logCounter;

} // namespace Configs_sys

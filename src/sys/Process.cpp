#include "include/sys/Process.hpp"
#include "include/global/Configs.hpp"
#include "include/ui/MainWindowAPI.h"

#include <QProcessEnvironment>

namespace Configs_sys {

    CoreProcess::CoreProcess(
        const QString& corePath,
        const QString& socketName,
        bool debugMode,
        QObject* parent
    )
        : QProcess(parent),
        program(corePath),
        socketName_(socketName),
        debugMode_(debugMode)
    {
        connect(this, &QProcess::readyReadStandardOutput, this, 
            [this]()
            {
                const QByteArray output = readAllStandardOutput();

                if (output.contains("Extra process exited unexpectedly"))
                {
                    MW_show_log("Extra Core exited, stopping profile...");
                    MW_dialog_message(MwMessage::CoreCrashed, {});
                }

                const int lines = output.count('\n');

                if (logCounter.fetchAndAddRelaxed(lines) > Configs::dataManager->settingsRepo->max_log_line)
                {
                    return;
                }

                MW_show_log(output);
            }
        );

        connect(this, &QProcess::readyReadStandardError, this,
            [this]()
            {
                MW_show_log(readAllStandardError().trimmed());
            }
        );
    }

    void CoreProcess::Start()
    {
        if (QProcess::state() != QProcess::NotRunning)
        {
            return;
        }

        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("GRYPH_CORE_SOCKET", socketName_);

        if (debugMode_)
        {
            environment.insert("GRYPH_CORE_DEBUG", "1");
        }

        setProcessEnvironment(environment);
        start(program, arguments);
    }

    void CoreProcess::Kill()
    {
        if (QProcess::state() == QProcess::NotRunning)
        {
            return;
        }

        kill();
        waitForFinished();
    }

} // namespace Configs_sys

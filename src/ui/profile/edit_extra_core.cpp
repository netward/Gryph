#include "include/ui/profile/edit_extra_core.h"

#include <QFileDialog>
#include <QIntValidator>
#include <QDir>

#include "include/ui/profile/dialog_edit_profile.h"


EditExtraCore::EditExtraCore(
    QWidget* parent)
    :
    QWidget(parent),
    ui(new Ui::EditExtraCore)
{
    ui->setupUi(this);


    ui->socks_port->setValidator(
        new QIntValidator(
            1,
            65534,
            ui->socks_port
        )
    );


    // =====================================================
    // Extra-core executable selector
    // =====================================================

    connect(
        ui->path_button,
        &QPushButton::pressed,
        this,
        [this]()
        {
            QString filePath =
                QFileDialog::getOpenFileName(
                    this
                );


            if (filePath.isEmpty())
            {
                return;
            }


            // Store relative path when the executable
            // is located inside the application's tree.
            const QString relativePath =
                QDir::current()
                .relativeFilePath(
                    filePath
                );


            if (!relativePath.startsWith(
                "../../"))
            {
                filePath =
                    relativePath;
            }


            auto* settings =
                Configs::dataManager
                ->settingsRepo
                .get();


            if (settings &&
                settings->AddExtraCorePath(
                    filePath
                ))
            {
                ui->path_combo->addItem(
                    filePath
                );
            }


            ui->path_combo->setCurrentText(
                filePath
            );


            ui->path_combo->setSizeAdjustPolicy(
                QComboBox::AdjustToContents
            );


            adjustSize();
        }
    );
}


EditExtraCore::~EditExtraCore()
{
    delete ui;
}


void EditExtraCore::onStart(
    std::shared_ptr<Configs::Profile> _ent)
{
    ent =
        std::move(_ent);


    if (!ent)
    {
        return;
    }


    // =====================================================
    // Read ExtraCore configuration from a detached copy.
    // =====================================================

    const auto outbound =
        ent->OutboundCloneAs<
        Configs::extracore
        >();


    if (!outbound)
    {
        return;
    }


    // =====================================================
    // Load UI
    // =====================================================

    ui->socks_address->setText(
        outbound->socksAddress
    );


    ui->socks_port->setText(
        Int2String(
            outbound->socksPort
        )
    );


    ui->config->setPlainText(
        outbound->extraCoreConf
    );


    ui->args->setText(
        outbound->extraCoreArgs
    );


    // =====================================================
    // Extra-core paths
    // =====================================================

    ui->path_combo->clear();


    auto* settings =
        Configs::dataManager
        ->settingsRepo
        .get();


    if (settings)
    {
        ui->path_combo->addItems(
            settings->GetExtraCorePaths()
        );
    }


    ui->path_combo->setCurrentText(
        outbound->extraCorePath
    );
}


bool EditExtraCore::onEnd()
{
    if (!ent)
    {
        return false;
    }


    // =====================================================
    // Freeze UI values before modifying Profile.
    // =====================================================

    const QString socksAddress =
        ui->socks_address->text();


    const int socksPort =
        ui->socks_port
        ->text()
        .toInt();


    const QString extraCoreConf =
        ui->config
        ->toPlainText();


    const QString extraCorePath =
        ui->path_combo
        ->currentText();


    const QString extraCoreArgs =
        ui->args->text();


    // =====================================================
    // Atomically update ExtraCore configuration.
    //
    // Do NOT use OutboundCloneAs() for writing:
    // changes made to a clone would be lost.
    // =====================================================

    const bool updated =
        ent
        ->MutateOutbound<
        Configs::extracore
        >(
            [
                socksAddress,
                socksPort,
                extraCoreConf,
                extraCorePath,
                extraCoreArgs
            ](
                Configs::extracore& outbound
                ) -> bool
            {
                outbound.socksAddress =
                    socksAddress;


                outbound.socksPort =
                    socksPort;


                outbound.extraCoreConf =
                    extraCoreConf;


                outbound.extraCorePath =
                    extraCorePath;


                outbound.extraCoreArgs =
                    extraCoreArgs;


                return true;
            }
        );


    if (!updated)
    {
        MessageBoxWarning(
            software_name,
            tr(
                "Failed to update extra-core "
                "profile configuration."
            )
        );

        return false;
    }


    return true;
}
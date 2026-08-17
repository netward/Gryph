#include "include/ui/profile/edit_ssh.h"

#include <QFileDialog>
#include <QDir>


EditSSH::EditSSH(
    QWidget* parent)
    :
    QWidget(parent),
    ui(new Ui::EditSSH)
{
    ui->setupUi(this);


    // =====================================================
    // Private key file selector
    //
    // Connect once during widget construction.
    // =====================================================

    connect(
        ui->choose_pk,
        &QPushButton::clicked,
        this,
        [this]()
        {
            const QString fileName =
                QFileDialog::getOpenFileName(
                    this,
                    tr("Select"),
                    QDir::currentPath(),
                    QString(),
                    nullptr,
                    QFileDialog::Option::ReadOnly
                );


            if (fileName.isEmpty())
            {
                return;
            }


            ui->private_key_path->setText(
                fileName
            );
        }
    );
}


EditSSH::~EditSSH()
{
    delete ui;
}


void EditSSH::onStart(
    std::shared_ptr<Configs::Profile> _ent)
{
    ent = _ent;


    if (!ent)
    {
        return;
    }


    // =====================================================
    // Read SSH configuration from a detached copy.
    // =====================================================

    const auto outbound =
        ent->OutboundCloneAs<
        Configs::ssh
        >();


    if (!outbound)
    {
        return;
    }


    // =====================================================
    // Load UI
    // =====================================================

    ui->user->setText(
        outbound->user
    );


    ui->password->setText(
        outbound->password
    );


    ui->private_key->setText(
        outbound->private_key
    );


    ui->private_key_path->setText(
        outbound->private_key_path
    );


    ui->private_key_pass->setText(
        outbound->private_key_passphrase
    );


    ui->host_key->setText(
        outbound->host_key.join(",")
    );


    ui->host_key_algs->setText(
        outbound
        ->host_key_algorithms
        .join(",")
    );


    ui->client_version->setText(
        outbound->client_version
    );
}


bool EditSSH::onEnd()
{
    if (!ent)
    {
        return false;
    }


    // =====================================================
    // Freeze UI values before Profile mutation.
    // =====================================================

    const QString user =
        ui->user->text();


    const QString password =
        ui->password->text();


    const QString privateKey =
        ui->private_key
        ->toPlainText();


    const QString privateKeyPath =
        ui->private_key_path
        ->text();


    const QString privateKeyPassphrase =
        ui->private_key_pass
        ->text();


    QStringList hostKey;

    if (!ui->host_key
        ->text()
        .trimmed()
        .isEmpty())
    {
        hostKey =
            ui->host_key
            ->text()
            .split(
                ",",
                Qt::SkipEmptyParts
            );

        for (QString& item :
            hostKey)
        {
            item =
                item.trimmed();
        }
    }


    QStringList hostKeyAlgorithms;

    if (!ui->host_key_algs
        ->text()
        .trimmed()
        .isEmpty())
    {
        hostKeyAlgorithms =
            ui->host_key_algs
            ->text()
            .split(
                ",",
                Qt::SkipEmptyParts
            );

        for (QString& item :
            hostKeyAlgorithms)
        {
            item =
                item.trimmed();
        }
    }


    const QString clientVersion =
        ui->client_version
        ->text();


    // =====================================================
    // Atomically update SSH configuration.
    //
    // Do NOT use OutboundCloneAs() for writing.
    // =====================================================

    return ent
        ->MutateOutbound<
        Configs::ssh
        >(
            [
                user,
                password,
                privateKey,
                privateKeyPath,
                privateKeyPassphrase,
                hostKey,
                hostKeyAlgorithms,
                clientVersion
            ](
                Configs::ssh& outbound
                ) -> bool
            {
                outbound.user =
                    user;


                outbound.password =
                    password;


                outbound.private_key =
                    privateKey;


                outbound.private_key_path =
                    privateKeyPath;


                outbound.private_key_passphrase =
                    privateKeyPassphrase;


                outbound.host_key =
                    hostKey;


                outbound.host_key_algorithms =
                    hostKeyAlgorithms;


                outbound.client_version =
                    clientVersion;


                return true;
            }
        );
}
#include "include/ui/profile/edit_socks.h"


EditSocks::EditSocks(
    QWidget* parent)
    :
    QWidget(parent),
    ui(new Ui::EditSocks)
{
    ui->setupUi(this);
}


EditSocks::~EditSocks()
{
    delete ui;
}


void EditSocks::onStart(
    std::shared_ptr<Configs::Profile> _ent)
{
    ent = _ent;

    if (!ent)
    {
        return;
    }


    // Read SOCKS configuration from a detached copy.
    const auto outbound =
        ent->OutboundCloneAs<
        Configs::socks
        >();

    if (!outbound)
    {
        return;
    }


    ui->version->setCurrentText(
        Int2String(
            outbound->version
        )
    );

    ui->username->setText(
        outbound->username
    );

    ui->password->setText(
        outbound->password
    );
}


bool EditSocks::onEnd()
{
    if (!ent)
    {
        return false;
    }


    const int version =
        ui->version
        ->currentText()
        .toInt();

    const QString username =
        ui->username
        ->text();

    const QString password =
        ui->password
        ->text();


    return ent
        ->MutateOutbound<
        Configs::socks
        >(
            [
                version,
                username,
                password
            ](
                Configs::socks& outbound
                ) -> bool
            {
                outbound.version =
                    version;

                outbound.username =
                    username;

                outbound.password =
                    password;

                return true;
            }
        );
}
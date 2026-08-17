#include "include/ui/profile/edit_http.h"


EditHttp::EditHttp(
    QWidget* parent)
    :
    QWidget(parent),
    ui(new Ui::EditHttp)
{
    ui->setupUi(this);
}


EditHttp::~EditHttp()
{
    delete ui;
}


void EditHttp::onStart(
    std::shared_ptr<Configs::Profile> _ent)
{
    ent = _ent;

    if (!ent)
    {
        return;
    }


    // =====================================================
    // Read HTTP configuration from a detached copy.
    // =====================================================

    const auto outbound =
        ent->OutboundCloneAs<
        Configs::http
        >();

    if (!outbound)
    {
        return;
    }


    ui->username->setText(
        outbound->username
    );

    ui->password->setText(
        outbound->password
    );
}


bool EditHttp::onEnd()
{
    if (!ent)
    {
        return false;
    }


    // =====================================================
    // Freeze UI values before modifying Profile.
    // =====================================================

    const QString username =
        ui->username->text();

    const QString password =
        ui->password->text();


    // =====================================================
    // Atomically update HTTP configuration.
    // =====================================================

    return ent
        ->MutateOutbound<
        Configs::http
        >(
            [
                username,
                password
            ](
                Configs::http& outbound
                ) -> bool
            {
                outbound.username =
                    username;

                outbound.password =
                    password;

                return true;
            }
        );
}
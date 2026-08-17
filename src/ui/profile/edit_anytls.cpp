#include "include/ui/profile/edit_anytls.h"

#include <QUuid>
#include <QRegularExpressionValidator>

#include "include/global/GuiUtils.hpp"


EditAnyTLS::EditAnyTLS(
    QWidget* parent)
    :
    QWidget(parent),
    ui(new Ui::EditAnyTLS)
{
    ui->setupUi(this);

    ui->min->setValidator(
        QRegExpValidator_Number
    );
}


EditAnyTLS::~EditAnyTLS()
{
    delete ui;
}


void EditAnyTLS::onStart(
    std::shared_ptr<Configs::Profile> _ent)
{
    ent =
        std::move(_ent);


    if (!ent)
    {
        return;
    }


    // =====================================================
    // Read configuration from a detached AnyTLS copy.
    //
    // Never expose the live Profile::outbound_ object.
    // =====================================================

    const auto outbound =
        ent->OutboundCloneAs<
        Configs::anyTLS
        >();


    if (!outbound)
    {
        return;
    }


    ui->password->setText(
        outbound->password
    );


    ui->interval->setText(
        outbound
        ->idle_session_check_interval
    );


    ui->timeout->setText(
        outbound
        ->idle_session_timeout
    );


    ui->min->setText(
        Int2String(
            outbound->min_idle_session
        )
    );
}


bool EditAnyTLS::onEnd()
{
    if (!ent)
    {
        return false;
    }


    // =====================================================
    // Freeze UI values before mutating Profile.
    // =====================================================

    const QString password =
        ui->password->text();


    const QString interval =
        ui->interval->text();


    const QString timeout =
        ui->timeout->text();


    const int minIdleSession =
        ui->min
        ->text()
        .toInt();


    // =====================================================
    // Atomically update AnyTLS configuration.
    //
    // MutateOutbound performs:
    //
    // snapshot
    //   ↓
    // detached copy
    //   ↓
    // modify
    //   ↓
    // revision check
    //   ↓
    // atomic publish
    // =====================================================

    return ent
        ->MutateOutbound<
        Configs::anyTLS
        >(
            [
                password,
                interval,
                timeout,
                minIdleSession
            ](
                Configs::anyTLS& outbound
                ) -> bool
            {
                outbound.password =
                    password;


                outbound
                    .idle_session_check_interval =
                    interval;


                outbound
                    .idle_session_timeout =
                    timeout;


                outbound
                    .min_idle_session =
                    minIdleSession;


                return true;
            }
        );
}
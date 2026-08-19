#include "include/ui/group/GroupItem.h"

#include "include/ui/group/DialogEditGroup.h"
#include "include/global/GuiUtils.hpp"
#include "include/configs/sub/GroupUpdater.hpp"

#include <QMessageBox>

#include "include/database/GroupsRepo.h"
#include "include/ui/MainWindowAPI.h"


QString ParseSubInfo(const QString &info) {
    if (info.trimmed().isEmpty()) return "";

    QString result;

    long long used = 0;
    long long total = 0;
    long long expire = 0;

    auto re0m = QRegularExpression("total=([0-9]+)").match(info);
    if (re0m.lastCapturedIndex() >= 1) {
        total = re0m.captured(1).toLongLong();
    } else {
        return "";
    }
    auto re1m = QRegularExpression("upload=([0-9]+)").match(info);
    if (re1m.lastCapturedIndex() >= 1) {
        used += re1m.captured(1).toLongLong();
    }
    auto re2m = QRegularExpression("download=([0-9]+)").match(info);
    if (re2m.lastCapturedIndex() >= 1) {
        used += re2m.captured(1).toLongLong();
    }
    auto re3m = QRegularExpression("expire=([0-9]+)").match(info);
    if (re3m.lastCapturedIndex() >= 1) {
        expire = re3m.captured(1).toLongLong();
    }

    result = QObject::tr("Used: %1 Remain: %2 Expire: %3")
                         .arg(ReadableSize(used), (total == 0) ? QString::fromUtf8("\u221E") : ReadableSize(total - used), DisplayTime(expire, QLocale::ShortFormat));

    return result;
}

GroupItem::GroupItem(QWidget *parent, const std::shared_ptr<Configs::Group> &ent, QListWidgetItem *item) : QWidget(parent), ui(new Ui::GroupItem) {
    ui->setupUi(this);
    this->setLayoutDirection(Qt::LeftToRight);

    this->parentWindow = parent;
    this->ent = ent;
    this->item = item;
    if (ent == nullptr) return;

    connect(this, &GroupItem::edit_clicked, this, &GroupItem::on_edit_clicked);
    connect(
        Subscription::groupUpdater,
        &Subscription::GroupUpdater::asyncUpdateCallback,
        this,
        [this](int gid)
        {
            if (!this->ent)
            {
                return;
            }

            if (gid == this->ent->Id())
            {
                refresh_data();
            }
        }
    );

    refresh_data();
}

GroupItem::~GroupItem() {
    delete ui;
}

void GroupItem::refresh_data()
{
    if (!ent)
    {
        return;
    }


    const auto snapshot =
        ent->Snapshot();


    ui->name->setText(
        snapshot.name
    );


    QString type =
        snapshot.url.isEmpty()
        ? tr("Basic")
        : tr("Subscription");


    if (snapshot.archive)
    {
        type =
            tr("Archive")
            + " "
            + type;
    }


    type +=
        " ("
        + Int2String(
            snapshot.profiles.size()
        )
        + ")";


    ui->type->setText(
        type
    );


    if (snapshot.url.isEmpty())
    {
        ui->url->hide();
        ui->subinfo->hide();
        ui->update_sub->hide();
    }
    else
    {
        ui->url->show();

        ui->url->setText(
            snapshot.url
        );


        QStringList info;


        if (snapshot.sub_last_update != 0)
        {
            info <<
                tr("Last update: %1")
                .arg(
                    DisplayTime(
                        snapshot.sub_last_update,
                        QLocale::ShortFormat
                    )
                );
        }


        if (!snapshot.info.isEmpty())
        {
            const QString subinfo =
                ParseSubInfo(
                    snapshot.info
                );


            if (!subinfo.isEmpty())
            {
                info <<
                    subinfo;
            }
        }


        if (info.isEmpty())
        {
            ui->subinfo->hide();
        }
        else
        {
            ui->subinfo->show();

            ui->subinfo->setText(
                info.join(" | ")
            );
        }
    }


    runOnThread(
        [=, this]
        {
            adjustSize();

            item->setSizeHint(
                sizeHint()
            );

            dynamic_cast<QWidget*>(
                parent()
                )->adjustSize();
        },
        this
    );
}

void GroupItem::on_update_sub_clicked()
{
    if (!ent)
    {
        return;
    }


    const auto snapshot =
        ent->Snapshot();


    Subscription::groupUpdater
        ->AsyncUpdate(
            snapshot.url,
            snapshot.id
        );
}

void GroupItem::on_edit_clicked() {
    auto dialog = new DialogEditGroup(ent, parentWindow);
    connect(dialog, &QDialog::finished, this, [=,this] {
        if (dialog->result() == QDialog::Accepted) {
            Configs::dataManager->groupsRepo->Save(ent);
            refresh_data();
            MW_dialog_message(MwMessage::GroupsChanged, {});
        }
        dialog->deleteLater();
    });
    dialog->show();
}

void GroupItem::on_remove_clicked()
{
    if (!ent)
    {
        return;
    }


    if (Configs::dataManager
        ->groupsRepo
        ->GetAllGroupIds()
        .size() <= 1)
    {
        return;
    }


    const auto snapshot =
        ent->Snapshot();


    if (QMessageBox::question(
        this,
        tr("Confirmation"),
        tr("Remove %1?")
        .arg(
            snapshot.name
        )
    )
        != QMessageBox::StandardButton::Yes)
    {
        return;
    }


    MainWindowApi::StopProfile(
        false,
        true,
        false
    );


    Configs::dataManager
        ->groupsRepo
        ->DeleteGroup(
            snapshot.id
        );


    MW_dialog_message(
        MwMessage::GroupsChanged,
        {}
    );


    delete item;
}

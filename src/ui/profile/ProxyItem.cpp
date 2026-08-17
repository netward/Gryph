#include "include/ui/profile/ProxyItem.h"

#include <QMessageBox>


ProxyItem::ProxyItem(
    QWidget* parent,
    const std::shared_ptr<Configs::Profile>& ent,
    QListWidgetItem* item)
    :
    QWidget(parent),
    ui(new Ui::ProxyItem)
{
    ui->setupUi(this);

    setLayoutDirection(
        Qt::LeftToRight
    );

    this->item =
        item;

    this->ent =
        ent;


    if (!this->ent)
    {
        return;
    }


    refresh_data();
}


ProxyItem::~ProxyItem()
{
    delete ui;
}


void ProxyItem::refresh_data()
{
    if (!ent)
    {
        return;
    }


    // =====================================================
    // Take one coherent immutable configuration snapshot.
    //
    // No live outbound pointer is exposed.
    // =====================================================

    const auto config =
        ent->ConfigSnapshot();


    // =====================================================
    // Profile information
    // =====================================================

    ui->type->setText(
        config.displayType
    );


    ui->name->setText(
        config.displayName
    );


    ui->address->setText(
        config.displayAddress
    );


    // =====================================================
    // Runtime information
    // =====================================================

    ui->traffic->setText(
        ent->DisplayTraffic()
    );


    ui->test_result->setText(
        ent->DisplayTestResult()
    );


    // =====================================================
    // Recalculate item geometry
    // =====================================================

    runOnThread(
        [this]()
        {
            adjustSize();

            if (item)
            {
                item->setSizeHint(
                    sizeHint()
                );
            }

            QWidget* parent =
                this->parentWidget();

            if (parent)
            {
                parent->adjustSize();
            }
        },
        this
    );
}


void ProxyItem::on_remove_clicked()
{
    if (!ent)
    {
        return;
    }


    // =====================================================
    // Read display name safely
    // =====================================================

    const auto config =
        ent->ConfigSnapshot();


    // =====================================================
    // Confirmation
    // =====================================================

    const bool confirmed =
        !remove_confirm
        ||
        QMessageBox::question(
            this,

            tr("Confirmation"),

            tr("Remove %1?")
            .arg(
                config.displayName
            )
        )
        ==
        QMessageBox::StandardButton::Yes;


    if (!confirmed)
    {
        return;
    }


    // TODO:
    // Prefer deleting the Profile through ProfilesRepo
    // instead of deleting only QListWidgetItem if this
    // callback is expected to remove the actual Profile.
    delete item;

    item =
        nullptr;
}

QPushButton* ProxyItem::get_change_button()
{
    return ui->change;
}
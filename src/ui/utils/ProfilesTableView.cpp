#include "include/ui/utils/ProfilesTableView.h"
#include "include/ui/utils/ProfilesTableVerticalHeader.h"
#include "include/ui/utils/ProfilesTableFilterHeader.h"
#include "include/ui/utils/ProfilesTableModel.h"
#include <QDragMoveEvent>
#include <QHeaderView>
#include <QMimeData>
#include <QMouseEvent>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QStyleOptionViewItem>

class ProfilesTableItemDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter,
        const QStyleOptionViewItem& option,
        const QModelIndex& index) const override {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);

        const auto* table = qobject_cast<const ProfilesTableView*>(opt.widget);

        const bool isSelected = opt.state & QStyle::State_Selected;
        const bool isHoveredRow = table && index.row() == table->hoveredRow();

        QColor background;

        if (isSelected) {
            background = QColor("#cce8ff");
        }
        else if (isHoveredRow) {
            background = QColor("#cce8ff");
        }

        painter->save();

        if (background.isValid()) {
            painter->fillRect(opt.rect, background);
        }

        // Убираем стандартные фон, hover, выделение и рамку фокуса,
        // чтобы Qt не рисовал скругления и чёрную рамку поверх нашего фона.
        opt.state &= ~QStyle::State_Selected;
        opt.state &= ~QStyle::State_MouseOver;
        opt.state &= ~QStyle::State_HasFocus;
        opt.backgroundBrush = Qt::NoBrush;

        QStyledItemDelegate::paint(painter, opt, index);

        painter->restore();
    }
};

ProfilesTableView::ProfilesTableView(QWidget *parent)
    : QTableView(parent) {
    setDragDropMode(InternalMove);
    setDropIndicatorShown(true);
    setSelectionBehavior(SelectRows);
    setDragEnabled(true);
    setAcceptDrops(true);
    setDefaultDropAction(Qt::MoveAction);

    setMouseTracking(true);
    viewport()->setMouseTracking(true);
    setItemDelegate(new ProfilesTableItemDelegate(this));

    m_verticalHeader = new ProfilesTableVerticalHeader(this);
    setVerticalHeader(m_verticalHeader);
    m_filterHeader = new ProfilesTableFilterHeader(this);
    setHorizontalHeader(m_filterHeader);

    setObjectName("profilesTableView");

    setStyleSheet(R"(
        QTableView#profilesTableView {
            selection-background-color: #cce8ff;
            selection-color: #111111;
        }

        QTableView#profilesTableView::item:selected {
            background-color: #cce8ff;
            color: #111111;
        }

        QTableView#profilesTableView::item:selected:active {
            background-color: #cce8ff;
            color: #111111;
        }

        QTableView#profilesTableView::item:selected:!active {
            background-color: #cce8ff;
            color: #111111;
        }
        QTableView#profilesTableView {
            selection-background-color: #cce8ff;
            selection-color: #111111;
            outline: 0;
        }

        QTableView#profilesTableView::item:selected {
            background-color: #cce8ff;
            color: #111111;
            border: none;
        }

        QTableView#profilesTableView::item:focus {
            outline: none;
            border: none;
        }

        QTableView#profilesTableView::item:selected:focus {
            outline: none;
            border: none;
        }
    )");
}

void ProfilesTableView::setModel(QAbstractItemModel *model) {
    QTableView::setModel(model);
    if (auto *pm = qobject_cast<ProfilesTableModel*>(model)) {
        m_verticalHeader->setProfilesModel(pm);
    } else {
        m_verticalHeader->setProfilesModel(nullptr);
    }
}

int ProfilesTableView::firstVisibleRow() {
    QRect rect = this->viewport()->rect();

    QModelIndex topIndex = indexAt(rect.topLeft());

    if (!topIndex.isValid()) return 0;

    int startRow = topIndex.row();
    return startRow;
}


void ProfilesTableView::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasFormat("application/profile-row-number")) {

        event->accept();
        QTableView::dragEnterEvent(event);
    } else {
        event->ignore();
    }
}

void ProfilesTableView::dragMoveEvent(QDragMoveEvent *event) {
    if (event->mimeData()->hasFormat("application/profile-row-number")) {
        QPoint pos = event->position().toPoint();
        QModelIndex targetIndex = indexAt(pos);
        if (!targetIndex.isValid()) {
            QModelIndex lastRowIndex = model()->index(model()->rowCount() - 1, 0);
            QRect rect = visualRect(lastRowIndex);
            if (event->pos().y() > rect.bottom()) {
                QPoint fakePos(rect.center().x(), rect.bottom() - 5);

                QDragMoveEvent fakeEvent(
                    fakePos,
                    event->possibleActions(),
                    event->mimeData(),
                    event->mouseButtons(),
                    event->keyboardModifiers()
                );
                QTableView::dragMoveEvent(&fakeEvent);
                event->accept();
                return;
            }
        }
        event->accept();
        QTableView::dragMoveEvent(event);
    } else {
        event->ignore();
    }
}

void ProfilesTableView::dropEvent(QDropEvent *event) {
    if (event->source() == this && event->mimeData()->hasFormat("application/profile-row-number")) {
        QByteArray encodedData = event->mimeData()->data("application/profile-row-number");
        QDataStream stream(&encodedData, QIODevice::ReadOnly);
        int rowNum;
        stream >> rowNum;

        QPoint pos = event->position().toPoint();
        QModelIndex targetIndex = indexAt(pos);

        int newRow;
        if (!targetIndex.isValid()) {
            newRow = model()->rowCount() - 1;
        } else {
            DropIndicatorPosition indicatorPos = dropIndicatorPosition();
            newRow = targetIndex.row();
            if (indicatorPos == AboveItem) {
                newRow--;
            }
        }
        rowsSwapped(rowNum, newRow);
        event->accept();
    } else {
        QTableView::dropEvent(event);
    }
}

void ProfilesTableView::mouseMoveEvent(QMouseEvent* event) {
    const QModelIndex index = indexAt(event->position().toPoint());
    const int newHoveredRow = index.isValid() ? index.row() : -1;

    if (m_hoveredRow != newHoveredRow) {
        m_hoveredRow = newHoveredRow;
        viewport()->update();
    }

    QTableView::mouseMoveEvent(event);
}

void ProfilesTableView::leaveEvent(QEvent* event) {
    if (m_hoveredRow != -1) {
        m_hoveredRow = -1;
        viewport()->update();
    }

    QTableView::leaveEvent(event);
}
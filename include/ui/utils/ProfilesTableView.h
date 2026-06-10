#pragma once

#include <QTableView>
#include <functional>

class ProfilesTableView : public QTableView {
    Q_OBJECT
public:
    explicit ProfilesTableView(QWidget* parent = nullptr);

    std::function<void(int row1, int row2)> rowsSwapped;

    void setModel(QAbstractItemModel* model) override;

    int firstVisibleRow();

    int hoveredRow() const { return m_hoveredRow; }

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    class ProfilesTableVerticalHeader* m_verticalHeader = nullptr;
    class ProfilesTableFilterHeader* m_filterHeader = nullptr;

    int m_hoveredRow = -1;
};
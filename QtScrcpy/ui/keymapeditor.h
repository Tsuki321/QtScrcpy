#pragma once
#include "keymapdocument.h"
#include <QWidget>
class QLabel;

class KeyMapEditor : public QWidget
{
    Q_OBJECT
public:
    explicit KeyMapEditor(QWidget *video);
    void open();
    void setScript(const QString &script);
signals:
    void applied(const QString &script);
    void editingFinished();
protected:
    bool focusNextPrevChild(bool) override { return false; }
    bool eventFilter(QObject *object, QEvent *event) override;
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
private:
    void save();
    void load();
    void apply();
    void finish();
    void erase();
    void bind(int index);
    void movePoint(QPointF point);
    KeyMapDocument m_document;
    QLabel *m_hint;
    QWidget *m_toolbar;
    int m_selected = -1;
    bool m_dragging = false;
    bool m_dirty = false;
    QString m_path;
};

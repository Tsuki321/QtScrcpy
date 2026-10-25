#pragma once
#include "keymapdocument.h"
#include <QWidget>
#include <QVector>
class QLabel;
class QComboBox;
class QListWidget;

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
    struct Marker { int node; QString field; int step; QPointF point; QString label; };
    QVector<Marker> markers() const;
    void save(bool saveAs = false);
    void load();
    void apply();
    void finish();
    void erase();
    void edit(int index, const QString &newType = QString(), QPointF point = QPointF(.5, .5));
    void settings();
    void editJson();
    void remember();
    void refresh();
    void undo(bool redo = false);
    KeyMapDocument m_document;
    QVector<QJsonObject> m_undo, m_redo;
    QLabel *m_hint;
    QWidget *m_toolbar;
    QListWidget *m_list;
    QComboBox *m_type;
    int m_selected = -1;
    int m_dragMarker = -1;
    bool m_dirty = false;
    QString m_path;
    QString m_sourceScript;
};

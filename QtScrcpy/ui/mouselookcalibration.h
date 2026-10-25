#pragma once
#include <QJsonObject>
#include <QPointF>
#include <QTimer>
#include <QWidget>

class QLabel;
class QPushButton;

class MouseLookCalibration : public QWidget
{
    Q_OBJECT
public:
    explicit MouseLookCalibration(const QJsonObject &mouse, QWidget *parent);
    ~MouseLookCalibration() override;
    void cancel();
    void interrupt();
signals:
    // Empty script stops the preview and releases its touches/cursor.
    void previewRequested(const QString &script);
    void movementRequested(const QPointF &position);
    void accepted(const QJsonObject &mouse);
    void finished();
protected:
    bool event(QEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void hideEvent(QHideEvent *event) override;
private:
    enum Stage { PickStart, Ready, Recording, Settling, PickEnd, Result, Testing };
    void refresh(const QString &message = QString());
    void startMotion(bool testing);
    void stopMotion();
    void endSweep();
    void recordEndpoint(QPointF point);
    void restartAxis(const QString &message = QString());
    void slowDown();
    double axisValue(QPointF point) const;
    double axisSize() const;
    QPointF center() const;
    QJsonObject previewMouse() const;

    QJsonObject m_original;
    QPointF m_ratios;
    QPointF m_candidate;
    QPointF m_start;
    QPointF m_lastMouse;
    QPointF m_motion;
    double m_travel = 0;
    double m_firstGain = 0;
    double m_weightedGain = 0;
    double m_weight = 0;
    int m_axis = 0;
    int m_samples = 0;
    Stage m_stage = PickStart;
    bool m_previewing = false;
    bool m_finished = false;
    bool m_spaceDown = false;
    QTimer m_settleTimer;
    QTimer m_timeout;
    QWidget *m_panel;
    QLabel *m_title;
    QLabel *m_instructions;
    QPushButton *m_retry;
    QPushButton *m_slower;
    QPushButton *m_use;
};

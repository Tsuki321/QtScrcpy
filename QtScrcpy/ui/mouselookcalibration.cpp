#include "mouselookcalibration.h"
#include "mouselookcalibrationmath.h"
#include <QCursor>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QVBoxLayout>

namespace {
QPointF mousePosition(const QMouseEvent *event)
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    return event->localPos();
#else
    return event->position();
#endif
}
}

MouseLookCalibration::MouseLookCalibration(const QJsonObject &mouse, QWidget *parent)
    : QWidget(parent), m_original(mouse), m_ratios(MouseLookCalibrationMath::ratios(mouse)), m_candidate(m_ratios)
{
    setObjectName("mouseLookCalibration");
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_NoMousePropagation);
    setCursor(Qt::CrossCursor);
    m_panel = new QWidget(this);
    m_panel->setStyleSheet("QWidget { background: #20252b; color: white; } QPushButton { padding: 5px; }");
    auto *layout = new QVBoxLayout(m_panel);
    m_title = new QLabel;
    layout->addWidget(m_title);
    m_instructions = new QLabel;
    m_instructions->setObjectName("calibrationInstructions");
    m_instructions->setWordWrap(true);
    layout->addWidget(m_instructions);
    auto *buttons = new QHBoxLayout;
    layout->addLayout(buttons);
    const auto button = [this, buttons](const QString &text, const QString &name) {
        auto *value = new QPushButton(text);
        value->setObjectName(name);
        value->setFocusPolicy(Qt::NoFocus);
        buttons->addWidget(value);
        return value;
    };
    m_retry = button(tr("Restart axis"), "restartCalibration");
    m_slower = button(tr("Point left screen"), "slowerCalibration");
    m_use = button(tr("Use calibration"), "useCalibration");
    auto *cancelButton = button(tr("Cancel"), "cancelCalibration");
    connect(m_retry, &QPushButton::clicked, this, [this]() {
        if (m_stage == Result) { m_axis = 0; m_ratios = m_candidate; }
        restartAxis();
    });
    connect(m_slower, &QPushButton::clicked, this, &MouseLookCalibration::slowDown);
    connect(cancelButton, &QPushButton::clicked, this, &MouseLookCalibration::cancel);
    connect(m_use, &QPushButton::clicked, this, [this]() {
        if (m_stage != Result || m_finished) { return; }
        m_finished = true;
        emit accepted(MouseLookCalibrationMath::withRatios(m_original, m_candidate));
        emit finished();
    });
    m_settleTimer.setSingleShot(true);
    m_settleTimer.setInterval(500);
    connect(&m_settleTimer, &QTimer::timeout, this, [this]() {
        if (m_stage == Settling) { m_stage = PickEnd; refresh(); }
    });
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(8000);
    connect(&m_timeout, &QTimer::timeout, this, [this]() {
        if (m_stage == Testing) { stopMotion(); m_stage = Result; refresh(); }
        else { restartAxis(tr("The sweep timed out. Try a short, steady movement.")); }
    });
    parent->installEventFilter(this);
    window()->installEventFilter(this);
    setGeometry(parent->rect());
    refresh();
}

MouseLookCalibration::~MouseLookCalibration() { stopMotion(); }

double MouseLookCalibration::axisValue(QPointF point) const { return m_axis == 0 ? point.x() : point.y(); }
double MouseLookCalibration::axisSize() const { return m_axis == 0 ? width() : height(); }
QPointF MouseLookCalibration::center() const { return QPointF(width() / 2.0, height() / 2.0); }

QJsonObject MouseLookCalibration::previewMouse() const
{
    auto mouse = MouseLookCalibrationMath::withRatios(m_original, m_stage == Testing ? m_candidate : m_ratios);
    mouse.remove("smallEyes");
    return mouse;
}

void MouseLookCalibration::refresh(const QString &message)
{
    const bool result = m_stage == Result || m_stage == Testing;
    m_title->setText(result ? tr("Mouse look calibrated")
        : tr("%1 calibration - sweep %2 of 2").arg(m_axis == 0 ? tr("Horizontal") : tr("Vertical")).arg(m_samples + 1));
    QString instructions;
    switch (m_stage) {
    case PickStart:
        instructions = tr("Stand still in a practice area. Click a fixed detail near the center of the game view, such as a wall corner. Avoid the HUD, crosshair and your character.");
        break;
    case Ready:
        instructions = tr("Hold Space and move the mouse steadily %1 by about %2 pixels, then release Space. Keep the marked detail on screen; only this axis will move.")
            .arg(m_axis == 0 ? tr("left or right") : tr("up or down")).arg(qRound(qMin(100.0, axisSize() * .15)));
        break;
    case Recording:
        instructions = tr("Move steadily in one direction. Release Space to finish. Mouse movement: %1 px.").arg(qRound(qAbs(axisValue(m_motion))));
        break;
    case Settling:
        instructions = tr("Let the camera stop moving...");
        break;
    case PickEnd:
        instructions = tr("Click the SAME detail at its new position. If it moved out of view, choose Point left screen and the next sweep will be slower.");
        break;
    case Result:
        instructions = tr("Matched camera motion to desktop mouse travel near the center of the view.\nHold Space for a short test; release to stop. Use calibration updates the editor, then Apply to play and Save to keep the profile.\nRecalibrate after changing mouse DPI, game sensitivity, zoom or window size.");
        break;
    case Testing:
        instructions = tr("Try the feel. Release Space to stop testing; Escape cancels.");
        break;
    }
    m_instructions->setText(message.isEmpty() ? instructions : message + "\n\n" + instructions);
    m_retry->setVisible(m_stage != Recording && m_stage != Testing && m_stage != Settling);
    m_retry->setText(result ? tr("Calibrate again") : tr("Restart axis"));
    m_slower->setVisible(m_stage == PickEnd);
    m_use->setVisible(m_stage == Result);
    const int panelWidth = qMin(470, qMax(200, width() - 20));
    m_panel->setFixedWidth(panelWidth);
    m_panel->resize(panelWidth, m_panel->heightForWidth(panelWidth));
    m_panel->move(10, 10);
    update();
}

void MouseLookCalibration::startMotion(bool testing)
{
    m_stage = testing ? Testing : Recording;
    m_motion = QPointF();
    m_travel = 0;
    m_lastMouse = mapFromGlobal(QCursor::pos());
    m_previewing = true;
    // An isolated profile ensures Space and landmark clicks never activate game
    // bindings. The preview uses exactly the same mouse converter as gameplay.
    const QJsonObject profile{{"switchKey", "Key_F12"}, {"keyMapNodes", QJsonArray()}, {"mouseMoveMap", previewMouse()}};
    emit previewRequested(QString::fromUtf8(QJsonDocument(profile).toJson(QJsonDocument::Compact)));
    if (m_finished) { return; }
    emit movementRequested(center()); // Seed the converter; this sends no swipe.
    if (m_finished) { return; }
    grabMouse();
    m_timeout.start();
    refresh();
}

void MouseLookCalibration::stopMotion()
{
    m_timeout.stop();
    if (!m_previewing) { return; }
    m_previewing = false;
    releaseMouse();
    emit previewRequested(QString());
}

void MouseLookCalibration::restartAxis(const QString &message)
{
    stopMotion();
    m_settleTimer.stop();
    m_samples = 0;
    m_weight = m_weightedGain = m_firstGain = 0;
    m_stage = PickStart;
    refresh(message);
}

void MouseLookCalibration::slowDown()
{
    const double ratio = axisValue(m_ratios);
    if (ratio > 500000) { restartAxis(tr("Cannot slow this axis further. Check the touch origin and game settings.")); return; }
    if (m_axis == 0) { m_ratios.setX(ratio * 2); } else { m_ratios.setY(ratio * 2); }
    restartAxis(tr("The next sweep will be slower. Choose a new detail near the center."));
}

void MouseLookCalibration::endSweep()
{
    stopMotion();
    const double distance = qAbs(axisValue(m_motion));
    if (distance < qMax(20.0, axisSize() * .04) || distance < m_travel * .85) {
        m_stage = PickStart;
        refresh(tr("Move a little farther in one direction without doubling back, then try again."));
        return;
    }
    m_stage = Settling;
    refresh();
    m_settleTimer.start();
}

void MouseLookCalibration::recordEndpoint(QPointF point)
{
    const QPointF scene = point - m_start;
    const double observed = axisValue(scene);
    const double distance = axisValue(m_motion);
    const double crossAxis = m_axis == 0 ? scene.y() : scene.x();
    if (qAbs(observed) < qMax(6.0, axisSize() * .01)) {
        const double ratio = axisValue(m_ratios);
        if (ratio >= .002) {
            if (m_axis == 0) { m_ratios.setX(ratio / 2); } else { m_ratios.setY(ratio / 2); }
            restartAxis(tr("Too little camera movement to measure. The next sweep will be faster. Check that the touch origin is in the game's camera area."));
        } else { restartAxis(tr("No measurable camera movement. Check the touch origin and game sensitivity.")); }
        return;
    }
    if (qAbs(crossAxis) > qMax(12.0, qAbs(observed) * .35)) {
        m_stage = PickStart;
        refresh(tr("The detail moved too far sideways for this axis. Stand still and mark the same fixed detail again."));
        return;
    }
    const double ratio = MouseLookCalibrationMath::correctedRatio(axisValue(m_ratios), distance, observed);
    if (!ratio) { restartAxis(tr("This measurement is outside the supported sensitivity range. Try again.")); return; }
    const double gain = observed / distance;
    if (m_samples && !MouseLookCalibrationMath::consistent(m_firstGain, gain)) {
        restartAxis(tr("The two sweeps disagreed. Repeat at a similar speed with a detail near the center. Game acceleration or a moving object can affect the result."));
        return;
    }
    m_firstGain = gain;
    m_weightedGain += qAbs(observed * distance);
    m_weight += distance * distance;
    if (++m_samples < 2) { m_stage = PickStart; refresh(tr("First sweep recorded. Repeat to confirm the response.")); return; }
    const double calibrated = axisValue(m_ratios) * m_weightedGain / m_weight;
    if (m_axis == 0) {
        m_candidate.setX(calibrated);
        m_axis = 1;
        restartAxis(tr("Horizontal response measured. Now calibrate vertical movement."));
    } else {
        m_candidate.setY(calibrated);
        m_stage = Result;
        refresh();
    }
}

void MouseLookCalibration::mousePressEvent(QMouseEvent *event)
{
    event->accept();
    setFocus();
    if (event->button() != Qt::LeftButton) { return; }
    const QPointF point = mousePosition(event);
    if (m_stage == PickStart) { m_start = point; m_stage = Ready; refresh(); }
    else if (m_stage == PickEnd) { recordEndpoint(point); }
}

void MouseLookCalibration::mouseMoveEvent(QMouseEvent *event)
{
    event->accept();
    if (m_stage != Recording && m_stage != Testing) { return; }
    const QPointF point = mousePosition(event);
    QPointF delta = point - m_lastMouse;
    m_lastMouse = point;
    if (m_stage == Recording) {
        if (m_axis == 0) { delta.setY(0); } else { delta.setX(0); }
    }
    // Keep virtual coordinates away from the converter's cursor-warp zone.
    // A bounded sweep also helps keep the selected landmark visible.
    const QPointF proposed = m_motion + delta;
    const QPointF bounded(qBound(-width() * .24, proposed.x(), width() * .24),
                          qBound(-height() * .24, proposed.y(), height() * .24));
    m_travel += qAbs(axisValue(bounded - m_motion));
    m_motion = bounded;
    emit movementRequested(center() + m_motion);
    if (m_finished) { return; }
    if (bounded != proposed) {
        if (m_stage == Testing) { stopMotion(); m_stage = Result; refresh(); }
        else { endSweep(); }
    } else { update(); }
}

void MouseLookCalibration::keyPressEvent(QKeyEvent *event)
{
    event->accept();
    if (event->isAutoRepeat()) { return; }
    if (event->key() == Qt::Key_Escape) { cancel(); return; }
    if (event->key() == Qt::Key_Tab) { m_panel->setVisible(!m_panel->isVisible()); return; }
    if (event->key() == Qt::Key_Space && !m_spaceDown) {
        m_spaceDown = true;
        if (m_stage == Ready || m_stage == Result) { startMotion(m_stage == Result); }
    }
}

void MouseLookCalibration::keyReleaseEvent(QKeyEvent *event)
{
    event->accept();
    if (event->isAutoRepeat() || event->key() != Qt::Key_Space) { return; }
    m_spaceDown = false;
    if (m_stage == Recording) { endSweep(); }
    else if (m_stage == Testing) { stopMotion(); m_stage = Result; refresh(); }
}

void MouseLookCalibration::mouseReleaseEvent(QMouseEvent *event) { event->accept(); }
void MouseLookCalibration::mouseDoubleClickEvent(QMouseEvent *event) { event->accept(); }
void MouseLookCalibration::wheelEvent(QWheelEvent *event) { event->accept(); }

void MouseLookCalibration::interrupt()
{
    m_spaceDown = false;
    if (m_stage == Testing) { stopMotion(); m_stage = Result; refresh(); }
    else if (m_stage != Result) { restartAxis(tr("Calibration paused. Select a new detail to continue.")); }
}

void MouseLookCalibration::cancel()
{
    if (m_finished) { return; }
    m_finished = true;
    stopMotion();
    m_settleTimer.stop();
    emit finished();
}

bool MouseLookCalibration::event(QEvent *event)
{
    // Keep application/editor shortcuts from changing profiles mid-sweep.
    if (event->type() == QEvent::ShortcutOverride) { event->accept(); return true; }
    if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Tab) {
        keyPressEvent(static_cast<QKeyEvent *>(event));
        return true;
    }
    return QWidget::event(event);
}

bool MouseLookCalibration::eventFilter(QObject *object, QEvent *event)
{
    if (object == parentWidget() && event->type() == QEvent::Resize) { setGeometry(parentWidget()->rect()); }
    if (event->type() == QEvent::WindowDeactivate && isVisible()) { interrupt(); }
    return QWidget::eventFilter(object, event);
}

void MouseLookCalibration::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (!event->oldSize().isEmpty()) { m_axis = 0; interrupt(); restartAxis(tr("View size changed. Repeat calibration at the size you play.")); }
    else { refresh(); }
}

void MouseLookCalibration::hideEvent(QHideEvent *event)
{
    cancel();
    QWidget::hideEvent(event);
}

void MouseLookCalibration::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::white);
    painter.drawText(QRect(10, height() - 65, width() - 20, 24), Qt::AlignCenter,
        tr("Tab: show/hide instructions | Esc: cancel"));
    if (m_stage == Ready || m_stage == Recording || m_stage == Settling || m_stage == PickEnd) {
        painter.setPen(QPen(QColor("#ffc857"), 2));
        painter.drawEllipse(m_start, 8, 8);
        painter.drawLine(m_start + QPointF(-13, 0), m_start + QPointF(13, 0));
        painter.drawLine(m_start + QPointF(0, -13), m_start + QPointF(0, 13));
    }
    if (m_stage == Recording || m_stage == Testing) {
        painter.setPen(QPen(QColor("#58d6ed"), 2));
        painter.drawLine(center(), center() + m_motion);
        painter.drawEllipse(center() + m_motion, 5, 5);
        painter.drawText(QRect(10, height() - 40, width() - 20, 30), Qt::AlignCenter,
            tr("Mouse travel: %1 px - release Space to stop").arg(qRound(m_stage == Recording ? qAbs(axisValue(m_motion)) : qSqrt(QPointF::dotProduct(m_motion, m_motion)))));
    }
}

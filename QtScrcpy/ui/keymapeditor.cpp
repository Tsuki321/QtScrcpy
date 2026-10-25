#include "keymapeditor.h"
#include <QDialog>
#include <QLineF>
#include <functional>
#include <QDir>
#include <QFileDialog>
#include <QFile>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QMetaEnum>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace {
QString profilesPath()
{
    const auto path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/keymap";
    QDir().mkpath(path);
    return path;
}

class BindingCapture : public QDialog
{
public:
    QString binding;
    explicit BindingCapture(QWidget *parent) : QDialog(parent)
    {
        setWindowTitle(tr("Assign keyboard or mouse button"));
        auto *layout = new QVBoxLayout(this);
        auto *label = new QLabel(tr("Press a key or click a mouse button here.\nEscape cancels. Use single keys; modifiers can be mapped separately."));
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
        layout->addWidget(label);
        resize(400, 100);
    }
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Escape) {
            reject();
            return;
        }
        const char *name = QMetaEnum::fromType<Qt::Key>().valueToKey(event->key());
        if (name) {
            binding = QString::fromLatin1(name);
            accept();
        }
    }
    void mousePressEvent(QMouseEvent *event) override
    {
        const char *name = QMetaEnum::fromType<Qt::MouseButtons>().valueToKey(event->button());
        if (name) {
            binding = QString::fromLatin1(name);
            accept();
        }
    }
};
}

KeyMapEditor::KeyMapEditor(QWidget *video) : QWidget(video)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_NoMousePropagation);
    video->installEventFilter(this);
    auto *layout = new QVBoxLayout(this);
    auto *bar = new QWidget;
    bar->setStyleSheet("QWidget { background: #20252b; color: white; } QPushButton { padding: 5px; }");
    auto *buttons = new QHBoxLayout(bar);
    const auto button = [this, buttons](const QString &text, std::function<void()> action) {
        auto *b = new QPushButton(text);
        buttons->addWidget(b);
        connect(b, &QPushButton::clicked, this, action);
    };
    button(tr("Load"), [this]() { load(); });
    button(tr("Save"), [this]() { save(); });
    button(tr("Bind"), [this]() { bind(m_selected); });
    button(tr("Delete"), [this]() { erase(); });
    button(tr("Apply"), [this]() { apply(); });
    button(tr("Close"), [this]() { finish(); });
    layout->addWidget(bar);
    m_hint = new QLabel(tr("Click a screen location to assign a key or mouse button. Drag a marker to move it."));
    m_hint->setWordWrap(true);
    m_hint->setStyleSheet("background: #20252b; color: white; padding: 4px;");
    layout->addWidget(m_hint);
    layout->addStretch();
    hide();
}

void KeyMapEditor::open()
{
    setGeometry(parentWidget()->rect());
    show();
    raise();
    setFocus();
}

bool KeyMapEditor::eventFilter(QObject *object, QEvent *event)
{
    if (object == parentWidget() && event->type() == QEvent::Resize) {
        setGeometry(parentWidget()->rect());
    }
    return QWidget::eventFilter(object, event);
}

void KeyMapEditor::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto nodes = m_document.nodes();
    for (int i = 0; i < nodes.size(); ++i) {
        const auto node = nodes[i].toObject();
        if (node.value("type") != "KMT_CLICK") {
            continue;
        }
        const auto pos = node.value("pos").toObject();
        const QPointF point(pos.value("x").toDouble() * width(), pos.value("y").toDouble() * height());
        painter.setPen(QPen(i == m_selected ? QColor("#ffda6a") : QColor("#6affcd"), 2));
        painter.setBrush(QColor(20, 25, 30, 200));
        painter.drawEllipse(point, 22, 22);
        painter.drawText(QRectF(point.x() - 65, point.y() - 15, 130, 30), Qt::AlignCenter,
                         node.value("key").toString().remove("Key_"));
    }
}

void KeyMapEditor::mousePressEvent(QMouseEvent *event)
{
    event->accept();
    if (event->button() != Qt::LeftButton) {
        return;
    }
    const auto nodes = m_document.nodes();
    m_selected = -1;
    for (int i = nodes.size() - 1; i >= 0; --i) {
        const auto node = nodes[i].toObject();
        const auto pos = node.value("pos").toObject();
        const QPointF point(pos.value("x").toDouble() * width(), pos.value("y").toDouble() * height());
        if (node.value("type") == "KMT_CLICK" && QLineF(point, event->localPos()).length() <= 28) {
            m_selected = i;
            m_dragging = true;
            update();
            return;
        }
    }
    BindingCapture capture(this);
    if (capture.exec() != QDialog::Accepted) {
        return;
    }
    auto updated = nodes;
    const auto pos = KeyMapDocument::normalized(event->localPos(), size());
    updated.append(QJsonObject{{"type", "KMT_CLICK"}, {"key", capture.binding},
                               {"pos", QJsonObject{{"x", pos.x()}, {"y", pos.y()}}}, {"switchMap", false}});
    auto candidate = m_document;
    candidate.setNodes(updated);
    QString error;
    if (!m_document.load(candidate.json(), &error)) {
        m_hint->setText(error);
        return;
    }
    m_selected = updated.size() - 1;
    m_dirty = true;
    update();
}

void KeyMapEditor::movePoint(QPointF point)
{
    auto nodes = m_document.nodes();
    if (m_selected < 0 || m_selected >= nodes.size()) {
        return;
    }
    auto node = nodes[m_selected].toObject();
    const auto pos = KeyMapDocument::normalized(point, size());
    node["pos"] = QJsonObject{{"x", pos.x()}, {"y", pos.y()}};
    nodes[m_selected] = node;
    m_document.setNodes(nodes);
    m_dirty = true;
    update();
}

void KeyMapEditor::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging) {
        movePoint(event->localPos());
    }
    event->accept();
}

void KeyMapEditor::mouseReleaseEvent(QMouseEvent *event)
{
    m_dragging = false;
    event->accept();
}

void KeyMapEditor::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        finish();
    } else if (event->key() == Qt::Key_Delete) {
        erase();
    }
    event->accept();
}

void KeyMapEditor::bind(int index)
{
    auto nodes = m_document.nodes();
    if (index < 0 || index >= nodes.size()) {
        return;
    }
    BindingCapture capture(this);
    if (capture.exec() == QDialog::Accepted) {
        auto node = nodes[index].toObject();
        node["key"] = capture.binding;
        nodes[index] = node;
        auto candidate = m_document;
        candidate.setNodes(nodes);
        QString error;
        if (m_document.load(candidate.json(), &error)) {
            m_dirty = true;
            update();
        } else {
            m_hint->setText(error);
        }
    }
}

void KeyMapEditor::erase()
{
    auto nodes = m_document.nodes();
    if (m_selected >= 0 && m_selected < nodes.size()) {
        nodes.removeAt(m_selected);
        m_document.setNodes(nodes);
        m_selected = -1;
        m_dirty = true;
        update();
    }
}

void KeyMapEditor::save()
{
    const auto path = QFileDialog::getSaveFileName(this, tr("Save keymap"),
            m_path.isEmpty() ? profilesPath() + "/custom.json" : m_path, tr("Keymaps (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    QSaveFile file(path);
    const auto data = m_document.json();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        QMessageBox::warning(this, tr("Save failed"), file.errorString());
        return;
    }
    m_path = path;
    m_dirty = false;
    m_hint->setText(tr("Saved %1. Apply activates the profile immediately.").arg(path));
}

void KeyMapEditor::load()
{
    if (m_dirty && QMessageBox::question(this, tr("Unsaved edits"), tr("Discard unsaved edits and load a profile?")) != QMessageBox::Yes) {
        return;
    }
    const auto path = QFileDialog::getOpenFileName(this, tr("Load keymap"), profilesPath(), tr("Keymaps (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    QString error;
    if (!file.open(QIODevice::ReadOnly) || !m_document.load(file.readAll(), &error)) {
        QMessageBox::warning(this, tr("Invalid keymap"), error.isEmpty() ? file.errorString() : error);
        return;
    }
    m_path = path;
    m_dirty = false;
    m_selected = -1;
    m_hint->setText(tr("Loaded. Click mappings can be edited; advanced mappings are preserved."));
    update();
}

void KeyMapEditor::apply()
{
    emit applied(QString::fromUtf8(m_document.json()));
    hide();
    emit editingFinished();
}

void KeyMapEditor::finish()
{
    if (m_dirty && QMessageBox::question(this, tr("Unsaved edits"), tr("Close without saving? Edits remain available until this phone window closes.")) != QMessageBox::Yes) {
        return;
    }
    hide();
    emit editingFinished();
}

#include "keymapeditor.h"
#include "keymapproperties.h"
#include <QDir>
#include <QFileDialog>
#include <QGridLayout>
#include <QListWidget>
#include <QLineF>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QSaveFile>
#include <QShortcut>
#include <QStandardPaths>
#include <functional>

namespace {
QString profilesPath()
{
    const auto path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/keymap";
    QDir().mkpath(path);
    return path;
}
QPointF pointOf(const QJsonValue &value)
{
    const auto object = value.toObject();
    return {object.value("x").toDouble(), object.value("y").toDouble()};
}
QJsonObject position(QPointF p) { return {{"x", p.x()}, {"y", p.y()}}; }
}

KeyMapEditor::KeyMapEditor(QWidget *video) : QWidget(video)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_NoMousePropagation);
    video->installEventFilter(this);
    auto *layout = new QVBoxLayout(this);
    layout->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_toolbar = new QWidget;
    m_toolbar->setMaximumWidth(460);
    m_toolbar->setStyleSheet("QWidget { background: #20252b; color: white; } QPushButton { padding: 4px; }");
    auto *bar = new QVBoxLayout(m_toolbar);
    m_type = new QComboBox;
    m_type->setObjectName("mappingType");
    for (const auto &type : {"KMT_CLICK", "KMT_CLICK_TWICE", "KMT_CLICK_MULTI", "KMT_STEER_WHEEL", "KMT_DRAG", "KMT_ANDROID_KEY", "KMT_MOUSE_MOVE"}) {
        m_type->addItem(KeyMapUi::typeName(type), type);
    }
    bar->addWidget(m_type);
    auto *buttons = new QGridLayout;
    bar->addLayout(buttons);
    int n = 0;
    const auto button = [this, buttons, &n](const QString &text, const QString &name, std::function<void()> action) {
        auto *b = new QPushButton(text); b->setObjectName(name);
        buttons->addWidget(b, n / 4, n % 4); ++n;
        connect(b, &QPushButton::clicked, this, action);
    };
    button(tr("Add"), "addMapping", [this]() { edit(-1, m_type->currentData().toString()); });
    button(tr("Edit / Bind"), "editMapping", [this]() { edit(m_selected); });
    button(tr("Delete"), "deleteMapping", [this]() { erase(); });
    button(tr("Clear all"), "clearMappings", [this]() { remember(); m_document.clear(); m_selected = -1; refresh(); });
    button(tr("Load"), "loadProfile", [this]() { load(); });
    button(tr("Save"), "saveProfile", [this]() { save(); });
    button(tr("Save as"), "saveProfileAs", [this]() { save(true); });
    button(tr("New"), "newProfile", [this]() {
        remember(); m_document = KeyMapDocument(); m_path.clear(); m_selected = -1; refresh();
    });
    button(tr("Undo"), "undoMapping", [this]() { undo(); });
    button(tr("Redo"), "redoMapping", [this]() { undo(true); });
    button(tr("Toggle key"), "profileSettings", [this]() { settings(); });
    button(tr("JSON"), "editJson", [this]() { editJson(); });
    button(tr("Apply"), "applyProfile", [this]() { apply(); });
    button(tr("Close"), "closeEditor", [this]() { finish(); });
    m_list = new QListWidget;
    m_list->setObjectName("mappingList");
    m_list->setMaximumHeight(135);
    bar->addWidget(m_list);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0) { m_selected = m_list->item(row)->data(Qt::UserRole).toInt(); update(); }
    });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) { edit(m_selected); });
    m_hint = new QLabel;
    m_hint->setWordWrap(true); bar->addWidget(m_hint);
    layout->addWidget(m_toolbar);
    const auto shortcut = [this](const QKeySequence &key, std::function<void()> action) {
        auto *s = new QShortcut(key, this); s->setContext(Qt::WidgetWithChildrenShortcut);
        connect(s, &QShortcut::activated, this, action);
    };
    shortcut(QKeySequence(Qt::Key_Tab), [this]() { m_toolbar->setVisible(!m_toolbar->isVisible()); setFocus(); });
    shortcut(QKeySequence::Undo, [this]() { undo(); });
    shortcut(QKeySequence::Redo, [this]() { undo(true); });
    shortcut(QKeySequence::Save, [this]() { save(); });
    hide();
}

void KeyMapEditor::open()
{
    setGeometry(parentWidget()->rect()); refresh(); show(); raise(); setFocus();
}

bool KeyMapEditor::eventFilter(QObject *object, QEvent *event)
{
    if (object == parentWidget() && event->type() == QEvent::Resize) { setGeometry(parentWidget()->rect()); }
    return QWidget::eventFilter(object, event);
}

void KeyMapEditor::remember()
{
    m_undo.append(m_document.root);
    if (m_undo.size() > 100) { m_undo.removeFirst(); }
    m_redo.clear(); m_dirty = true;
}

void KeyMapEditor::undo(bool redo)
{
    auto &source = redo ? m_redo : m_undo;
    auto &target = redo ? m_undo : m_redo;
    if (source.isEmpty()) { return; }
    target.append(m_document.root); m_document.root = source.takeLast();
    m_selected = -1; m_dragMarker = -1; m_dirty = true; refresh();
}

void KeyMapEditor::refresh()
{
    m_list->blockSignals(true); m_list->clear();
    const auto append = [this](const QString &text, int index) {
        auto *item = new QListWidgetItem(text, m_list); item->setData(Qt::UserRole, index);
        if (index == m_selected) { m_list->setCurrentItem(item); }
    };
    if (m_document.root.contains("mouseMoveMap")) { append(tr("Mouse look / free look"), -2); }
    const auto nodes = m_document.nodes();
    for (int i = 0; i < nodes.size(); ++i) {
        const auto node = nodes[i].toObject();
        const auto binding = node.value("key").toString(node.value("upKey").toString());
        append(QString("%1  %2  %3").arg(binding, KeyMapUi::typeName(node.value("type").toString()), node.value("comment").toString()), i);
    }
    m_list->blockSignals(false);
    m_hint->setText(tr("%1 mappings%2 | Toggle: %3\nChoose a type, then click the video or Add. Drag markers; double-click to edit. Tab hides the panel. Clear all + Apply restores normal controls.")
        .arg(nodes.size()).arg(m_dirty ? tr(" (unsaved)") : QString()).arg(m_document.root.value("switchKey").toString()));
    update();
}

QVector<KeyMapEditor::Marker> KeyMapEditor::markers() const
{
    QVector<Marker> result;
    const auto add = [&result](int node, const QString &field, int step, const QJsonValue &value, const QString &label) {
        if (value.isObject()) { result.append(Marker{node, field, step, pointOf(value), label}); }
    };
    const auto mouse = m_document.root.value("mouseMoveMap").toObject();
    add(-2, "startPos", -1, mouse.value("startPos"), tr("Look"));
    add(-2, "smallEyes", -1, mouse.value("smallEyes").toObject().value("pos"), tr("Free look"));
    const auto nodes = m_document.nodes();
    for (int i = 0; i < nodes.size(); ++i) {
        const auto node = nodes[i].toObject();
        const auto label = node.value("key").toString(node.value("upKey").toString());
        for (const auto &field : {"pos", "centerPos", "startPos", "endPos"}) {
            add(i, field, -1, node.value(field), label + (QString(field) == "endPos" ? tr(" end") : QString()));
        }
        const auto steps = node.value("clickNodes").toArray();
        for (int j = 0; j < steps.size(); ++j) { add(i, "clickNodes", j, steps[j].toObject().value("pos"), label + QString(" #%1").arg(j + 1)); }
    }
    return result;
}

void KeyMapEditor::paintEvent(QPaintEvent *)
{
    QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(0, 0, 0, 65));
    const auto screen = [this](QPointF p) { return QPointF(p.x() * width(), p.y() * height()); };
    for (const auto &value : m_document.nodes()) {
        const auto node = value.toObject(); painter.setPen(QPen(QColor(100, 210, 255), 2));
        if (node.value("type") == "KMT_DRAG") { painter.drawLine(screen(pointOf(node.value("startPos"))), screen(pointOf(node.value("endPos")))); }
        if (node.value("type") == "KMT_STEER_WHEEL") {
            const auto center = pointOf(node.value("centerPos"));
            painter.drawLine(screen(center - QPointF(node.value("leftOffset").toDouble(), 0)), screen(center + QPointF(node.value("rightOffset").toDouble(), 0)));
            painter.drawLine(screen(center - QPointF(0, node.value("upOffset").toDouble())), screen(center + QPointF(0, node.value("downOffset").toDouble())));
        }
    }
    for (const auto &marker : markers()) {
        const auto point = screen(marker.point);
        painter.setPen(QPen(marker.node == m_selected ? QColor(255, 205, 70) : QColor(100, 210, 255), 2));
        painter.setBrush(QColor(20, 25, 30, 190)); painter.drawEllipse(point, 20, 20);
        painter.drawText(QRectF(point.x() - 75, point.y() + 22, 150, 38), Qt::AlignHCenter | Qt::TextWordWrap, marker.label);
    }
}

void KeyMapEditor::mousePressEvent(QMouseEvent *event)
{
    event->accept();
    if (event->button() != Qt::LeftButton) { return; }
    const auto points = markers();
    for (int i = points.size() - 1; i >= 0; --i) {
        const auto p = points[i].point;
        if (QLineF(event->pos(), QPointF(p.x() * width(), p.y() * height())).length() <= 24) {
            m_selected = points[i].node; m_dragMarker = i; remember(); refresh(); return;
        }
    }
    edit(-1, m_type->currentData().toString(), KeyMapDocument::normalized(event->pos(), size()));
}

void KeyMapEditor::mouseMoveEvent(QMouseEvent *event)
{
    event->accept();
    const auto points = markers();
    if (!(event->buttons() & Qt::LeftButton) || m_dragMarker < 0 || m_dragMarker >= points.size()) { return; }
    const auto marker = points[m_dragMarker];
    auto node = marker.node == -2 ? m_document.root.value("mouseMoveMap").toObject() : m_document.nodes()[marker.node].toObject();
    QPointF point = KeyMapDocument::normalized(event->pos(), size());
    if (marker.field == "centerPos") {
        point.setX(qBound(node.value("leftOffset").toDouble(), point.x(), 1 - node.value("rightOffset").toDouble()));
        point.setY(qBound(node.value("upOffset").toDouble(), point.y(), 1 - node.value("downOffset").toDouble()));
    }
    if (marker.field == "smallEyes") {
        auto eyes = node.value("smallEyes").toObject(); eyes["pos"] = position(point); node["smallEyes"] = eyes;
    } else if (marker.step >= 0) {
        auto steps = node.value("clickNodes").toArray(); auto step = steps[marker.step].toObject();
        step["pos"] = position(point); steps[marker.step] = step; node["clickNodes"] = steps;
    } else { node[marker.field] = position(point); }
    if (marker.node == -2) { m_document.root["mouseMoveMap"] = node; }
    else { auto nodes = m_document.nodes(); nodes[marker.node] = node; m_document.setNodes(nodes); }
    update();
}
void KeyMapEditor::mouseReleaseEvent(QMouseEvent *event) { m_dragMarker = -1; event->accept(); }
void KeyMapEditor::mouseDoubleClickEvent(QMouseEvent *event) { event->accept(); m_dragMarker = -1; edit(m_selected); }
void KeyMapEditor::keyReleaseEvent(QKeyEvent *event) { event->accept(); }
void KeyMapEditor::wheelEvent(QWheelEvent *event) { event->accept(); }
void KeyMapEditor::keyPressEvent(QKeyEvent *event)
{
    event->accept();
    if (event->key() == Qt::Key_Escape) { finish(); }
    else if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) { erase(); }
}

void KeyMapEditor::erase()
{
    if (m_selected == -2 && m_document.root.contains("mouseMoveMap")) {
        remember(); m_document.root.remove("mouseMoveMap");
    } else if (m_selected >= 0 && m_selected < m_document.nodes().size()) {
        remember(); auto nodes = m_document.nodes(); nodes.removeAt(m_selected); m_document.setNodes(nodes);
    } else { return; }
    m_selected = -1; refresh();
}

void KeyMapEditor::edit(int index, const QString &newType, QPointF point)
{
    auto nodes = m_document.nodes();
    QString type = newType;
    QJsonObject node;
    if (type == "KMT_MOUSE_MOVE" || index == -2) {
        index = -2; type = "KMT_MOUSE_MOVE"; node = m_document.root.value("mouseMoveMap").toObject();
        if (node.isEmpty()) { node = {{"startPos", position(point)}, {"speedRatioX",3}, {"speedRatioY",1.3333}}; }
    } else if (type.isEmpty()) {
        if (index < 0 || index >= nodes.size()) { return; }
        node = nodes[index].toObject(); type = node.value("type").toString();
    } else {
        node = {{"type",type}, {"key",""}};
        if (type == "KMT_CLICK" || type == "KMT_CLICK_TWICE") { node["pos"] = position(point); }
        if (type == "KMT_STEER_WHEEL") {
            node.remove("key"); node["centerPos"] = position(point);
            node["leftKey"] = "Key_A"; node["rightKey"] = "Key_D"; node["upKey"] = "Key_W"; node["downKey"] = "Key_S";
            node["leftOffset"] = qMin(.1, point.x()); node["rightOffset"] = qMin(.1, 1 - point.x());
            node["upOffset"] = qMin(.1, point.y()); node["downOffset"] = qMin(.1, 1 - point.y());
        }
        if (type == "KMT_DRAG") { node["startPos"] = position(point); node["endPos"] = position(QPointF(qMin(.95, point.x() + .2), point.y())); }
        if (type == "KMT_CLICK_MULTI") { node["clickNodes"] = QJsonArray{QJsonObject{{"pos",position(point)}, {"delay",0}}}; }
    }
    KeyMapUi::Properties dialog(node, type, this);
    while (dialog.exec() == QDialog::Accepted) {
        auto candidate = m_document.root;
        const auto edited = dialog.result();
        if (index == -2) { candidate["mouseMoveMap"] = edited; }
        else { auto editedNodes = nodes; if (index >= 0) { editedNodes[index] = edited; } else { editedNodes.append(edited); } candidate["keyMapNodes"] = editedNodes; }
        const auto error = KeyMapValidation::validate(candidate);
        if (!error.isEmpty()) { QMessageBox::warning(this, tr("Invalid binding"), error); continue; }
        remember(); m_document.root = candidate; m_selected = index == -1 ? nodes.size() : index; refresh(); break;
    }
}

void KeyMapEditor::settings()
{
    QDialog dialog(this); dialog.setWindowTitle(tr("Keymap toggle"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *binding = new KeyMapUi::Binding(m_document.root.value("switchKey").toString(), &dialog);
    layout->addWidget(new QLabel(tr("Toggle between custom and normal controls:"))); layout->addWidget(binding);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    while (dialog.exec() == QDialog::Accepted) {
        auto candidate = m_document.root; candidate["switchKey"] = binding->value();
        const auto error = KeyMapValidation::validate(candidate);
        if (!error.isEmpty()) { QMessageBox::warning(this, tr("Invalid toggle"), error); continue; }
        remember(); m_document.root = candidate; refresh(); break;
    }
}

void KeyMapEditor::editJson()
{
    QDialog dialog(this); dialog.setWindowTitle(tr("Edit keymap JSON")); dialog.resize(700, 600);
    auto *layout = new QVBoxLayout(&dialog); auto *text = new QPlainTextEdit(QString::fromUtf8(m_document.json())); layout->addWidget(text);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    while (dialog.exec() == QDialog::Accepted) {
        KeyMapDocument candidate; QString error;
        if (!candidate.load(text->toPlainText().toUtf8(), &error)) { QMessageBox::warning(this, tr("Invalid keymap"), error); continue; }
        remember(); m_document = candidate; m_selected = -1; refresh(); break;
    }
}

void KeyMapEditor::save(bool saveAs)
{
    const auto error = KeyMapValidation::validate(m_document.root);
    if (!error.isEmpty()) { QMessageBox::warning(this, tr("Invalid keymap"), error); return; }
    QString path = m_path;
    if (saveAs || path.isEmpty()) { path = QFileDialog::getSaveFileName(this, tr("Save keymap"), path.isEmpty() ? profilesPath() + "/profile.json" : path, tr("Keymaps (*.json)")); }
    if (path.isEmpty()) { return; }
    if (!path.endsWith(".json", Qt::CaseInsensitive)) { path += ".json"; }
    QSaveFile file(path); const auto json = m_document.json();
    if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size() || !file.commit()) { QMessageBox::warning(this, tr("Save failed"), file.errorString()); return; }
    m_path = path; m_dirty = false; refresh();
}

void KeyMapEditor::load()
{
    const auto path = QFileDialog::getOpenFileName(this, tr("Load keymap"), profilesPath(), tr("Keymaps (*.json)"));
    if (path.isEmpty()) { return; }
    QFile file(path); KeyMapDocument candidate; QString error;
    if (!file.open(QIODevice::ReadOnly) || !candidate.load(file.readAll(), &error)) { QMessageBox::warning(this, tr("Invalid keymap"), error.isEmpty() ? file.errorString() : error); return; }
    remember(); m_document = candidate; m_path = path; m_dirty = false; m_selected = -1; refresh();
}

void KeyMapEditor::apply()
{
    const auto error = KeyMapValidation::validate(m_document.root);
    if (!error.isEmpty()) { QMessageBox::warning(this, tr("Invalid keymap"), error); return; }
    const QString script = m_document.nodes().isEmpty() && !m_document.root.contains("mouseMoveMap") ? QString() : QString::fromUtf8(m_document.json());
    m_sourceScript = script; emit applied(script); hide(); emit editingFinished();
}
void KeyMapEditor::finish() { hide(); emit editingFinished(); }
void KeyMapEditor::setScript(const QString &script)
{
    if (script == m_sourceScript) { return; }
    KeyMapDocument candidate; QString error;
    if (!script.isEmpty() && !candidate.load(script.toUtf8(), &error)) { m_hint->setText(error); return; }
    m_document = candidate; m_sourceScript = script; m_undo.clear(); m_redo.clear();
    m_dirty = false; m_selected = -1; m_path.clear(); refresh();
}

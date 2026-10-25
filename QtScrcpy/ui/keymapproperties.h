#pragma once
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QTableWidget>
#include <QVBoxLayout>
#include "keymapdocument.h"

namespace KeyMapUi {
inline QString typeName(const QString &type)
{
    if (type == "KMT_CLICK") { return QObject::tr("Touch / hold"); }
    if (type == "KMT_CLICK_TWICE") { return QObject::tr("Tap on press and release"); }
    if (type == "KMT_CLICK_MULTI") { return QObject::tr("Timed tap sequence"); }
    if (type == "KMT_STEER_WHEEL") { return QObject::tr("Steering pad (WASD)"); }
    if (type == "KMT_DRAG") { return QObject::tr("Swipe"); }
    if (type == "KMT_ANDROID_KEY") { return QObject::tr("Android key"); }
    if (type == "KMT_MOUSE_MOVE") { return QObject::tr("Mouse look"); }
    return type;
}

class Capture : public QDialog
{
public:
    QString binding;
    Capture(QWidget *parent) : QDialog(parent)
    {
        setWindowTitle(tr("Capture binding"));
        auto *layout = new QVBoxLayout(this);
        auto *label = new QLabel(tr("Press one key (including Escape or Tab), or a mouse button here.\nUse Cancel below to leave without assigning."));
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
        layout->addWidget(label);
        auto *cancel = new QPushButton(tr("Cancel"));
        cancel->setAutoDefault(false);
        cancel->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(cancel);
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        setFocusPolicy(Qt::StrongFocus);
        setFocus();
    }
    bool focusNextPrevChild(bool) override { return false; }
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->isAutoRepeat()) { return; }
        const char *name = QMetaEnum::fromType<Qt::Key>().valueToKey(event->key());
        if (name && KeyMapDocument::validKey(QString::fromLatin1(name))) {
            binding = QString::fromLatin1(name); accept();
        }
    }
    void mousePressEvent(QMouseEvent *event) override
    {
        const char *name = QMetaEnum::fromType<Qt::MouseButtons>().valueToKey(int(event->button()));
        if (name) { binding = QString::fromLatin1(name); accept(); }
    }
};

class Binding : public QWidget
{
public:
    QComboBox *names;
    Binding(const QString &value, QWidget *parent, bool keyboardOnly = false) : QWidget(parent)
    {
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        names = new QComboBox;
        names->setEditable(true);
        names->setInsertPolicy(QComboBox::NoInsert);
        names->addItem(QString());
        for (const auto &meta : {QMetaEnum::fromType<Qt::Key>(), QMetaEnum::fromType<Qt::MouseButtons>()}) {
            for (int i = 0; i < meta.keyCount(); ++i) {
                const auto key = QString::fromLatin1(meta.key(i));
                if (KeyMapValidation::bindingId(key, keyboardOnly)) { names->addItem(key); }
            }
        }
        names->setCurrentText(value);
        layout->addWidget(names, 1);
        auto *capture = new QPushButton(tr("Capture"));
        capture->setAutoDefault(false);
        layout->addWidget(capture);
        connect(capture, &QPushButton::clicked, this, [this, keyboardOnly]() {
            Capture dialog(this);
            if (dialog.exec() == QDialog::Accepted && KeyMapValidation::bindingId(dialog.binding, keyboardOnly)) {
                names->setCurrentText(dialog.binding);
            }
        });
    }
    QString value() const { return names->currentText().trimmed(); }
};

// Keep the source object so comments and extension fields survive UI edits.
class Properties : public QDialog
{
public:
    QJsonObject original;
    QMap<QString, Binding *> bindings;
    QMap<QString, QDoubleSpinBox *> numbers;
    QMap<QString, QPair<QDoubleSpinBox *, QDoubleSpinBox *>> positions;
    QLineEdit *comment;
    QCheckBox *switchMap = nullptr;
    QCheckBox *freeLook = nullptr;
    QTableWidget *steps = nullptr;
    QFormLayout *form;
    QString type;

    Properties(QJsonObject node, const QString &nodeType, QWidget *parent) : QDialog(parent), original(node), type(nodeType)
    {
        setWindowTitle(typeName(type));
        resize(590, 600);
        auto *layout = new QVBoxLayout(this);
        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        auto *body = new QWidget;
        form = new QFormLayout(body);
        scroll->setWidget(body);
        layout->addWidget(scroll);
        comment = new QLineEdit(node.value("comment").toString());
        form->addRow(tr("Name / comment"), comment);
        if (type != "KMT_STEER_WHEEL" && type != "KMT_MOUSE_MOVE") { binding("key", tr("Binding")); }
        if (type == "KMT_CLICK" || type == "KMT_CLICK_TWICE") {
            position("pos", tr("Touch position"));
            number("androidKey", tr("Also send Android key (0 = none)"), 0, 65535, 0, 0);
        }
        if (type == "KMT_CLICK") {
            switchMap = new QCheckBox(tr("Release / recapture mouse look on release"));
            switchMap->setChecked(node.value("switchMap").toBool()); form->addRow(switchMap);
        }
        if (type == "KMT_STEER_WHEEL") {
            position("centerPos", tr("Pad center"));
            for (const auto &dir : {"left", "right", "up", "down"}) {
                const QString d = QString::fromLatin1(dir);
                binding(d + "Key", d + tr(" key"), true);
                number(d + "Offset", d + tr(" distance"), 0, 1, 4, .1);
            }
        }
        if (type == "KMT_DRAG") {
            position("startPos", tr("Swipe start")); position("endPos", tr("Swipe end"));
            number("startDelay", tr("Hold before swipe (ms)"), 0, 60000, 0, 0);
            number("dragSpeed", tr("Swipe speed (higher = faster)"), 0, 1, 3, 1);
        }
        if (type == "KMT_ANDROID_KEY") {
            number("androidKey", tr("Android key code"), 0, 65535, 0, 4);
            auto *presets = new QComboBox;
            presets->addItem(tr("Choose a common Android key"), -1);
            const QList<QPair<QString, int>> keys{{"Back",4},{"Home",3},{"App switch",187},{"Menu",82},
                {"Volume up",24},{"Volume down",25},{"Power",26},{"Enter",66},{"Delete",67},
                {"D-pad up",19},{"D-pad down",20},{"D-pad left",21},{"D-pad right",22},{"Media play/pause",85}};
            for (const auto &key : keys) { presets->addItem(key.first, key.second); }
            form->addRow(tr("Common keys"), presets);
            connect(presets, QOverload<int>::of(&QComboBox::activated), this, [this, presets](int) {
                if (presets->currentData().toInt() >= 0) { numbers["androidKey"]->setValue(presets->currentData().toInt()); }
            });
        }
        if (type == "KMT_MOUSE_MOVE") {
            position("startPos", tr("Mouse-look touch origin"));
            const double legacy = node.value("speedRatio").toDouble(1);
            number("speedRatioX", tr("X speed ratio (lower = faster)"), .001, 1000000, 4, legacy);
            number("speedRatioY", tr("Y speed ratio (lower = faster)"), .001, 1000000, 4, node.contains("speedRatio") ? legacy / 2.25 : 1);
            freeLook = new QCheckBox(tr("Enable free look while holding a binding (smallEyes)"));
            freeLook->setChecked(node.contains("smallEyes")); form->addRow(freeLook);
            const auto eyes = node.value("smallEyes").toObject();
            bindings["eyesKey"] = new Binding(eyes.value("key").toString(), this);
            form->addRow(tr("Free-look binding"), bindings["eyesKey"]);
            original["eyesPos"] = eyes.value("pos"); position("eyesPos", tr("Free-look touch origin"));
        }
        if (type == "KMT_CLICK_MULTI") {
            steps = new QTableWidget(0, 3);
            steps->setHorizontalHeaderLabels({tr("Delay (ms)"), tr("X"), tr("Y")});
            steps->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
            steps->setSelectionBehavior(QAbstractItemView::SelectRows); steps->setMinimumHeight(210);
            form->addRow(steps);
            for (const auto &step : node.value("clickNodes").toArray()) { addStep(step.toObject()); }
            if (!steps->rowCount()) { addStep(QJsonObject()); }
            auto *actions = new QWidget;
            auto *row = new QHBoxLayout(actions);
            const QStringList labels{tr("Add step"), tr("Remove step"), tr("Move up"), tr("Move down")};
            for (int action = 0; action < labels.size(); ++action) {
                auto *button = new QPushButton(labels[action]); button->setAutoDefault(false); row->addWidget(button);
                connect(button, &QPushButton::clicked, this, [this, action]() {
                    const int selected = steps->currentRow();
                    if (action == 0) { if (steps->rowCount() < 50) { addStep(QJsonObject()); } return; }
                    if (selected < 0) { return; }
                    if (action == 1) { steps->removeRow(selected); return; }
                    const int other = selected + (action == 2 ? -1 : 1);
                    if (other < 0 || other >= steps->rowCount()) { return; }
                    for (int col = 0; col < 3; ++col) {
                        auto *a = static_cast<QDoubleSpinBox *>(steps->cellWidget(selected, col));
                        auto *b = static_cast<QDoubleSpinBox *>(steps->cellWidget(other, col));
                        const double value = a->value(); a->setValue(b->value()); b->setValue(value);
                    }
                    steps->selectRow(other);
                });
            }
            form->addRow(actions);
            form->addRow(new QLabel(tr("Each delay follows the previous tap. Maximum 50 steps.")));
        }
        auto *help = new QLabel(tr("Positions and distances use 0-1 of the video size.\nUse single keys or mouse buttons; modifier keys can be assigned separately."));
        help->setWordWrap(true); form->addRow(help);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }
    void binding(const QString &field, const QString &label, bool keyboardOnly = false)
    {
        bindings[field] = new Binding(original.value(field).toString(), this, keyboardOnly);
        bindings[field]->names->setObjectName(field); form->addRow(label, bindings[field]);
    }
    QDoubleSpinBox *spin(double min, double max, int decimals, double value)
    {
        auto *box = new QDoubleSpinBox;
        box->setDecimals(decimals); box->setRange(min, max);
        box->setSingleStep(decimals ? .01 : 1); box->setValue(value); return box;
    }
    void number(const QString &field, const QString &label, double min, double max, int decimals, double fallback)
    {
        numbers[field] = spin(min, max, decimals, original.value(field).toDouble(fallback));
        numbers[field]->setObjectName(field); form->addRow(label, numbers[field]);
    }
    void position(const QString &field, const QString &label)
    {
        const auto pos = original.value(field).toObject();
        auto *x = spin(0, 1, 4, pos.value("x").toDouble(.5));
        auto *y = spin(0, 1, 4, pos.value("y").toDouble(.5));
        x->setObjectName(field + "X"); y->setObjectName(field + "Y");
        x->setPrefix("X "); y->setPrefix("Y "); positions[field] = qMakePair(x, y);
        auto *row = new QWidget;
        auto *layout = new QHBoxLayout(row); layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(x); layout->addWidget(y); form->addRow(label, row);
    }
    void addStep(const QJsonObject &step)
    {
        const int row = steps->rowCount(); steps->insertRow(row);
        auto *metadata = new QTableWidgetItem;
        metadata->setData(Qt::UserRole, step);
        steps->setItem(row, 0, metadata);
        const auto pos = step.value("pos").toObject();
        steps->setCellWidget(row, 0, spin(0, 60000, 0, step.value("delay").toDouble(100)));
        steps->setCellWidget(row, 1, spin(0, 1, 4, pos.value("x").toDouble(.5)));
        steps->setCellWidget(row, 2, spin(0, 1, 4, pos.value("y").toDouble(.5))); steps->selectRow(row);
    }
    QJsonObject result() const
    {
        auto node = original; node["comment"] = comment->text();
        if (type != "KMT_MOUSE_MOVE") { node["type"] = type; }
        for (auto it = bindings.cbegin(); it != bindings.cend(); ++it) { node[it.key()] = it.value()->value(); }
        for (auto it = numbers.cbegin(); it != numbers.cend(); ++it) { node[it.key()] = it.value()->value(); }
        for (auto it = positions.cbegin(); it != positions.cend(); ++it) {
            auto pos = original.value(it.key()).toObject();
            pos["x"] = it.value().first->value(); pos["y"] = it.value().second->value(); node[it.key()] = pos;
        }
        if (switchMap) { node["switchMap"] = switchMap->isChecked(); }
        if (freeLook) {
            if (freeLook->isChecked()) {
                auto eyes = original.value("smallEyes").toObject();
                eyes["type"] = "KMT_CLICK"; eyes["key"] = node.value("eyesKey");
                eyes["pos"] = node.value("eyesPos"); eyes["switchMap"] = false; node["smallEyes"] = eyes;
            } else { node.remove("smallEyes"); }
            node.remove("eyesKey"); node.remove("eyesPos");
        }
        if (steps) {
            QJsonArray sequence;
            for (int row = 0; row < steps->rowCount(); ++row) {
                const auto value = [this, row](int col) { return static_cast<QDoubleSpinBox *>(steps->cellWidget(row, col))->value(); };
                auto step = steps->item(row, 0)->data(Qt::UserRole).toJsonObject();
                auto pos = step.value("pos").toObject();
                pos["x"] = value(1); pos["y"] = value(2);
                step["delay"] = value(0); step["pos"] = pos; sequence.append(step);
            }
            node["clickNodes"] = sequence;
        }
        return node;
    }
};
}

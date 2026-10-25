#include <QtTest>
#include <QtEndian>
#include <QDir>
#include <QFile>
#include <QListWidget>
#include "controller.h"
#include "keymap.h"
#include "keymapeditor.h"
#include "keymapproperties.h"

namespace {
QJsonObject pos(double x = .5, double y = .5) { return {{"x", x}, {"y", y}}; }
QJsonObject touch(const QString &key, bool releaseMouse = false)
{
    return {{"type", "KMT_CLICK"}, {"key", key}, {"pos", pos()}, {"switchMap", releaseMouse}};
}
QJsonObject mouseLook()
{
    return {{"startPos", pos()}, {"speedRatioX", 1}, {"speedRatioY", 1},
        {"smallEyes", QJsonObject{{"type", "KMT_CLICK"}, {"key", "Key_Alt"}, {"pos", pos(.2, .3)}}}};
}
QJsonObject steering()
{
    return {{"type", "KMT_STEER_WHEEL"}, {"centerPos", pos()},
        {"leftKey", "Key_A"}, {"rightKey", "Key_D"}, {"upKey", "Key_W"}, {"downKey", "Key_S"},
        {"leftOffset", .2}, {"rightOffset", .2}, {"upOffset", .2}, {"downOffset", .2}};
}
QJsonObject sequence(const QString &key, int delay = 0)
{
    return {{"type", "KMT_CLICK_MULTI"}, {"key", key}, {"clickNodes", QJsonArray{
        QJsonObject{{"delay", delay}, {"pos", pos(.3, .4)}},
        QJsonObject{{"delay", delay}, {"pos", pos(.6, .7)}}}};
}
QJsonObject swipe(const QString &key)
{
    return {{"type", "KMT_DRAG"}, {"key", key}, {"startPos", pos(.4, .5)},
        {"endPos", pos(.6, .5)}, {"startDelay", 0}, {"dragSpeed", 1}};
}
QJsonObject profile(QJsonArray nodes = {}, QJsonObject mouse = {})
{
    QJsonObject root{{"switchKey", "Key_QuoteLeft"}, {"keyMapNodes", nodes}};
    if (!mouse.isEmpty()) { root["mouseMoveMap"] = mouse; }
    return root;
}
QString json(const QJsonObject &root) { return QString::fromUtf8(QJsonDocument(root).toJson()); }
int actionCount(const QVector<QByteArray> &packets, int type, int action)
{
    int count = 0;
    for (const auto &packet : packets) {
        if (packet.size() > 1 && quint8(packet[0]) == type && quint8(packet[1]) == action) { ++count; }
    }
    return count;
}
quint32 read32(const QByteArray &packet, int offset)
{
    return qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(packet.constData() + offset));
}
struct Input {
    QVector<QByteArray> packets;
    Controller controller;
    explicit Input(const QJsonObject &root) : controller([this](const QByteArray &p) -> qint64 {
        packets.append(p); return p.size();
    }, json(root)) {}
    void key(int code, bool down, bool repeat = false, QSize frame = QSize(1000, 1000))
    {
        QKeyEvent e(down ? QEvent::KeyPress : QEvent::KeyRelease, code, Qt::NoModifier, QString(), repeat);
        controller.keyEvent(&e, frame, QSize(1000, 1000));
    }
    void enable() { key(Qt::Key_QuoteLeft, true); key(Qt::Key_QuoteLeft, false); }
    void button(Qt::MouseButton button, bool down)
    {
        QMouseEvent e(down ? QEvent::MouseButtonPress : QEvent::MouseButtonRelease,
            QPointF(400, 400), button, down ? Qt::MouseButtons(button) : Qt::NoButton, Qt::NoModifier);
        controller.mouseEvent(&e, QSize(1000, 1000), QSize(1000, 1000));
    }
    void move(qreal x, qreal y = 500)
    {
        QMouseEvent e(QEvent::MouseMove, QPointF(x, y), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        controller.mouseEvent(&e, QSize(1000, 1000), QSize(1000, 1000));
    }
};
}

class KeymapTests : public QObject
{
    Q_OBJECT
private slots:
    void bundledProfiles()
    {
        QDir dir(QStringLiteral(KEYMAP_FIXTURES));
        const auto names = dir.entryList({"*.json"}, QDir::Files);
        QVERIFY(!names.isEmpty());
        for (const auto &name : names) {
            QFile file(dir.filePath(name)); QVERIFY(file.open(QIODevice::ReadOnly));
            KeyMapDocument doc; QString error;
            QVERIFY2(doc.load(file.readAll(), &error), qPrintable(name + ": " + error));
            KeyMap map; map.loadKeyMap(QString::fromUtf8(doc.json()));
            for (const auto &value : doc.nodes()) {
                const auto node = value.toObject();
                const QString key = node.value(node.value("type") == "KMT_STEER_WHEEL" ? "leftKey" : "key").toString();
                const int id = KeyMapValidation::bindingId(key);
                const auto &parsed = id < 0 ? map.getKeyMapNodeMouse(-id) : map.getKeyMapNodeKey(id);
                QVERIFY2(parsed.type != KeyMap::KMT_INVALID, qPrintable(name + ": " + key));
            }
            QCOMPARE(map.isValidMouseMoveMap(), doc.root.contains("mouseMoveMap"));
        }
    }
    void rejectInvalidProfilesAtomically()
    {
        const auto valid = profile({touch("Key_F")});
        KeyMapDocument doc; QString error;
        QVERIFY(doc.load(json(valid).toUtf8(), &error));
        const QVector<QJsonObject> invalid{
            profile({touch("Key_F"), touch("Key_F")}),
            profile({touch("Key_QuoteLeft")}),
            profile({touch("BackButton"), touch("XButton1")}),
            profile({touch("AllButtons")}),
            profile({touch("Key_unknown")}),
            profile({steering(), touch("Key_W")}),
            profile({steering(), steering()}),
            profile({QJsonObject{{"type", "KMT_CLICK"}, {"key", "Key_F"}, {"pos", pos(2, .5)}}}),
            profile({QJsonObject{{"type", "KMT_UNKNOWN"}, {"key", "Key_F"}}}),
            profile({}, QJsonObject{{"startPos", pos()}, {"speedRatioX", 1}, {"speedRatioY", 0}}),
            profile({QJsonObject{{"type", "KMT_CLICK_MULTI"}, {"key", "Key_F"}, {"clickNodes", QJsonArray()}}),
            profile({QJsonObject{{"type", "KMT_DRAG"}, {"key", "Key_F"}, {"startPos", pos()}, {"endPos", pos()}, {"startDelay", -1}}})
        };
        for (const auto &root : invalid) {
            QVERIFY(!doc.load(json(root).toUtf8(), &error)); QVERIFY(!error.isEmpty());
            QCOMPARE(doc.root, valid);
        }
        Input input(valid); input.enable(); input.key(Qt::Key_F, true);
        input.controller.updateScript(json(invalid.first()));
        QVERIFY(input.controller.isCurrentCustomKeymap());
        input.key(Qt::Key_F, false);
        QCOMPARE(actionCount(input.packets, 2, 0), 1);
        QCOMPARE(actionCount(input.packets, 2, 1), 1);
    }
    void legacySensitivityAndNamespaces()
    {
        auto root = profile({touch("Key_At"), touch("ExtraButton4")},
            QJsonObject{{"startPos", pos()}, {"speedRatioX", 3}});
        QVERIFY(KeyMapValidation::validate(root).isEmpty());
        KeyMap map; map.loadKeyMap(json(root));
        QCOMPARE(map.getMouseMoveMap().data.mouseMove.speedRatio, QPointF(3, 1));
        QCOMPARE(map.getKeyMapNodeKey(Qt::Key_At).type, KeyMap::KMT_CLICK);
        QCOMPARE(map.getKeyMapNodeMouse(Qt::ExtraButton4).type, KeyMap::KMT_CLICK);
        root["mouseMoveMap"] = QJsonObject{{"startPos", pos()}, {"speedRatio", 9}, {"speedRatioY", 2}};
        map.loadKeyMap(json(root));
        QCOMPARE(map.getMouseMoveMap().data.mouseMove.speedRatio, QPointF(9, 2));
        map.loadKeyMap(json(profile()));
        QCOMPARE(map.getKeyMapNodeKey(Qt::Key_At).type, KeyMap::KMT_INVALID);
        QVERIFY(!map.isValidMouseMoveMap());
    }
    void mouseBoundActions_data()
    {
        QTest::addColumn<QJsonObject>("node");
        QTest::addColumn<int>("touches");
        QTest::newRow("hold") << touch("LeftButton") << 1;
        QTest::newRow("press-release-taps") << QJsonObject{{"type", "KMT_CLICK_TWICE"}, {"key", "LeftButton"}, {"pos", pos()}} << 2;
        QTest::newRow("sequence") << sequence("LeftButton") << 2;
        QTest::newRow("swipe") << swipe("LeftButton") << 1;
        QTest::newRow("android-key") << QJsonObject{{"type", "KMT_ANDROID_KEY"}, {"key", "LeftButton"}, {"androidKey", 3}} << 0;
    }
    void mouseBoundActions()
    {
        QFETCH(QJsonObject, node); QFETCH(int, touches);
        Input input(profile({node})); input.enable(); input.button(Qt::LeftButton, true);
        QTest::qWait(150); input.button(Qt::LeftButton, false);
        QCOMPARE(actionCount(input.packets, 2, 0), touches);
        QCOMPARE(actionCount(input.packets, 2, 1), touches);
        if (!touches) {
            QCOMPARE(actionCount(input.packets, 0, 0), 1); QCOMPARE(actionCount(input.packets, 0, 1), 1);
            QCOMPARE(read32(input.packets.first(), 2), quint32(3));
        }
    }
    void clearAndReapplyReleaseHeldInputs()
    {
        const auto root = profile({touch("Key_F"), steering(), sequence("Key_R", 120),
            swipe("Key_T"), QJsonObject{{"type", "KMT_ANDROID_KEY"}, {"key", "Key_H"}, {"androidKey", 3}}});
        Input input(root); input.enable();
        for (int key : {Qt::Key_F, Qt::Key_W, Qt::Key_T, Qt::Key_H, Qt::Key_R}) { input.key(key, true); }
        QCOMPARE(actionCount(input.packets, 2, 0), 3);
        input.controller.updateScript(QString());
        QVERIFY(!input.controller.isCurrentCustomKeymap());
        QCOMPARE(actionCount(input.packets, 2, 1), 3);
        QCOMPARE(actionCount(input.packets, 0, 1), 1);
        const int released = input.packets.size(); QTest::qWait(300); QCOMPARE(input.packets.size(), released);
        input.controller.updateScript(json(root)); input.enable(); input.key(Qt::Key_F, true);
        // Reapplying exactly the same script also releases and resets the old converter.
        input.controller.updateScript(json(root));
        QCOMPARE(actionCount(input.packets, 2, 1), 4);
        input.enable(); input.key(Qt::Key_F, true); input.key(Qt::Key_F, false);
        QCOMPARE(actionCount(input.packets, 2, 0), 5); QCOMPARE(actionCount(input.packets, 2, 1), 5);
    }
    void releaseCaptureClearsSteeringAndNormalInputs()
    {
        Input input(profile({steering(), touch("Key_M", true)}, mouseLook())); input.enable();
        input.key(Qt::Key_W, true); input.key(Qt::Key_M, true); input.key(Qt::Key_M, false);
        QCOMPARE(actionCount(input.packets, 2, 0), actionCount(input.packets, 2, 1));
        const int released = input.packets.size(); QTest::qWait(90); QCOMPARE(input.packets.size(), released);
        input.key(Qt::Key_W, false); QCOMPARE(input.packets.size(), released);
        input.key(Qt::Key_Left, true); input.button(Qt::LeftButton, true);
        input.key(Qt::Key_M, true); input.key(Qt::Key_M, false);
        QCOMPARE(actionCount(input.packets, 0, 0), actionCount(input.packets, 0, 1));
        QCOMPARE(actionCount(input.packets, 2, 0), actionCount(input.packets, 2, 1));
        input.key(Qt::Key_W, true); input.key(Qt::Key_W, false);
        QCOMPARE(actionCount(input.packets, 2, 0), actionCount(input.packets, 2, 1));
    }
    void normalKeyRepeatAndClear()
    {
        Input input(profile({touch("Key_F")}));
        input.key(Qt::Key_Left, true); input.key(Qt::Key_Left, true, true);
        QCOMPARE(actionCount(input.packets, 0, 0), 2);
        input.controller.updateScript(QString());
        QCOMPARE(actionCount(input.packets, 0, 1), 1);
        const int count = input.packets.size(); input.key(Qt::Key_Left, false); QCOMPARE(input.packets.size(), count);
    }
    void sharedAndroidKeysStayDownUntilLastRelease()
    {
        auto a = touch("Key_F"); a["androidKey"] = 3;
        auto b = touch("Key_G"); b["androidKey"] = 3;
        Input input(profile({a, b})); input.enable();
        input.key(Qt::Key_F, true); input.key(Qt::Key_G, true);
        QCOMPARE(actionCount(input.packets, 0, 0), 1);
        input.key(Qt::Key_F, false); QCOMPARE(actionCount(input.packets, 0, 1), 0);
        input.key(Qt::Key_G, false); QCOMPARE(actionCount(input.packets, 0, 1), 1);
    }
    void movementSurvivesBoundaryAndRecentering()
    {
        auto mouse = mouseLook(); mouse["startPos"] = pos(.9, .5);
        Input input(profile({}, mouse)); input.enable(); input.move(500); input.move(700);
        QVERIFY(actionCount(input.packets, 2, 0) >= 2);
        QVERIFY(actionCount(input.packets, 2, 1) >= 1);
        for (int x = 699; x >= 693; --x) {
            const int count = actionCount(input.packets, 2, 2); input.move(x);
            QCOMPARE(actionCount(input.packets, 2, 2), count + 1);
        }
        for (const auto &packet : input.packets) {
            if (quint8(packet[0]) != 2) { continue; }
            QVERIFY(read32(packet, 10) < 1000); QVERIFY(read32(packet, 14) < 1000);
        }
        input.move(20); const int beforeWarp = input.packets.size(); input.move(500);
        QCOMPARE(input.packets.size(), beforeWarp);
        input.move(510); QVERIFY(input.packets.size() > beforeWarp);
    }
    void freeLookAndRotationReleaseTouches()
    {
        Input input(profile({touch("Key_F")}, mouseLook())); input.enable(); input.move(500); input.move(510);
        input.key(Qt::Key_Alt, true); QCOMPARE(actionCount(input.packets, 2, 1), 1);
        QCOMPARE(read32(input.packets.last(), 10), quint32(200));
        QCOMPARE(read32(input.packets.last(), 14), quint32(300));
        input.move(500); input.move(510);
        input.key(Qt::Key_Alt, false); QCOMPARE(actionCount(input.packets, 2, 1), 2);
        input.key(Qt::Key_F, true);
        input.key(Qt::Key_F, false, false, QSize(800, 600));
        QCOMPARE(actionCount(input.packets, 2, 0), actionCount(input.packets, 2, 1));
    }
    void converterDestructionReleasesWhileControllerIsAlive()
    {
        QVector<QByteArray> packets;
        {
            Controller controller([&packets](const QByteArray &p) -> qint64 { packets.append(p); return p.size(); }, json(profile({touch("Key_F")})));
            QKeyEvent toggle(QEvent::KeyPress, Qt::Key_QuoteLeft, Qt::NoModifier);
            QKeyEvent press(QEvent::KeyPress, Qt::Key_F, Qt::NoModifier);
            controller.keyEvent(&toggle, QSize(1000, 1000), QSize(1000, 1000));
            controller.keyEvent(&press, QSize(1000, 1000), QSize(1000, 1000));
            QCOMPARE(actionCount(packets, 2, 1), 0);
        }
        QCOMPARE(actionCount(packets, 2, 1), 1);
    }
    void editorClearUndoApplyAndReapply()
    {
        QWidget video; video.resize(1000, 1000); KeyMapEditor editor(&video);
        const auto script = json(profile({touch("Key_F")}, mouseLook())); editor.setScript(script);
        auto *list = editor.findChild<QListWidget *>("mappingList"); QVERIFY(list); QCOMPARE(list->count(), 2);
        QSignalSpy applied(&editor, &KeyMapEditor::applied);
        auto *clear = editor.findChild<QPushButton *>("clearMappings"); QVERIFY(clear);
        clear->click(); QCOMPARE(list->count(), 0);
        editor.findChild<QPushButton *>("undoMapping")->click(); QCOMPARE(list->count(), 2);
        editor.findChild<QPushButton *>("redoMapping")->click(); QCOMPARE(list->count(), 0);
        editor.findChild<QPushButton *>("applyProfile")->click(); QCOMPARE(applied.count(), 1);
        QVERIFY(applied.last().first().toString().isEmpty());
        editor.setScript(script); QCOMPARE(list->count(), 2);
        editor.findChild<QPushButton *>("applyProfile")->click(); QCOMPARE(applied.count(), 2);
        QCOMPARE(QJsonDocument::fromJson(applied.last().first().toString().toUtf8()).object(),
            QJsonDocument::fromJson(script.toUtf8()).object());
    }
    void propertiesPreserveAdvancedFields()
    {
        auto node = sequence("Key_F"); node["extension"] = "preserve me";
        auto steps = node.value("clickNodes").toArray(); auto step = steps[0].toObject(); step["comment"] = "first"; steps[0] = step; node["clickNodes"] = steps;
        KeyMapUi::Properties properties(node, "KMT_CLICK_MULTI", nullptr);
        const auto result = properties.result();
        QCOMPARE(result.value("extension").toString(), QString("preserve me"));
        QCOMPARE(result.value("clickNodes").toArray(), steps);
        properties.steps->selectRow(0);
        for (auto *button : properties.findChildren<QPushButton *>()) {
            if (button->text() == "Move down") { button->click(); break; }
        }
        const auto reordered = properties.result().value("clickNodes").toArray();
        QCOMPARE(reordered[0].toObject(), steps[1].toObject());
        QCOMPARE(reordered[1].toObject(), steps[0].toObject());
        KeyMapUi::Capture capture(nullptr);
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(&capture, &escape);
        QCOMPARE(capture.binding, QString("Key_Escape"));
        QCOMPARE(capture.result(), int(QDialog::Accepted));
    }
};

QTEST_MAIN(KeymapTests)
#include "test_keymap.moc"

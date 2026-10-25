#include <QtTest>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QTemporaryDir>
#include <QLineEdit>
#include <QLabel>
#include <QSpinBox>
#include <QPushButton>
#include <QCheckBox>
#include <QSettings>
#include <cstdio>
#include "wirelesspanel.h"
#include "wirelessdiscovery.h"
#include "keymapdocument.h"
#include "framemetrics.h"
#include "qyuvopenglwidget.h"
#include "videobuffer.h"
#include "controller.h"
extern "C" {
#include <libavutil/frame.h>
}
#include <cstring>

static bool fill(AVFrame *frame, int w, int h, int value)
{
    av_frame_unref(frame);
    frame->format = AV_PIX_FMT_YUV420P;
    frame->width = w;
    frame->height = h;
    if (av_frame_get_buffer(frame, 32) < 0) { return false; }
    std::memset(frame->data[0], value, frame->linesize[0] * h);
    std::memset(frame->data[1], 128, frame->linesize[1] * h / 2);
    std::memset(frame->data[2], 128, frame->linesize[2] * h / 2);
    return true;
}

class BenchmarkWidget : public QYUVOpenGLWidget
{
public:
    void renderSynchronously()
    {
        makeCurrent();
        context()->functions()->glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
        paintGL();
        context()->functions()->glFinish();
        doneCurrent();
    }
};

class Features : public QObject
{
    Q_OBJECT
private slots:
    void pairingAndDiscovery()
    {
        WirelessPanel panel(QCoreApplication::applicationFilePath());
        auto *host = panel.findChild<QLineEdit *>("phoneIp");
        auto *code = panel.findChild<QLineEdit *>("pairCode");
        auto *pair = panel.findChild<QPushButton *>("pairPhone");
        auto *status = panel.findChild<QLabel *>("connectionStatus");
        panel.findChild<QCheckBox *>("autoDiscovery")->setChecked(false);
        host->setText("127.0.0.1");
        panel.findChild<QSpinBox *>("pairPort")->setValue(9001);
        panel.findChild<QSpinBox *>("connectPort")->setValue(0);
        code->setText("123456");
        QTRY_VERIFY_WITH_TIMEOUT(pair->isEnabled(), 5000);
        pair->click();
        QVERIFY(code->text().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(status->text().startsWith("Connected."), 5000);
        QVERIFY(!QSettings().allKeys().contains("wireless/pairCode"));
        code->setText("000000");
        pair->click();
        QTRY_VERIFY_WITH_TIMEOUT(status->text().startsWith("Pairing failed"), 5000);
        QVERIFY(pair->isEnabled());
    }
    void adbTimeoutLeavesUiResponsive()
    {
        WirelessPanel panel(QCoreApplication::applicationFilePath(), nullptr, 250);
        panel.findChild<QCheckBox *>("autoDiscovery")->setChecked(false);
        panel.findChild<QLineEdit *>("phoneIp")->setText("192.0.2.1");
        panel.findChild<QSpinBox *>("connectPort")->setValue(9002);
        auto *button = panel.findChild<QPushButton *>("connectPhone");
        QTRY_VERIFY(button->isEnabled());
        bool timerFired = false;
        QTimer::singleShot(30, &panel, [&]() { timerFired = true; });
        button->click();
        QTRY_VERIFY_WITH_TIMEOUT(timerFired, 200);
        QTRY_VERIFY_WITH_TIMEOUT(button->isEnabled(), 3000);
        QVERIFY(panel.findChild<QLabel *>("connectionStatus")->text().startsWith("Connection failed"));
    }
    void discovery()
    {
        QCOMPARE(WirelessDiscovery::endpoint("192.168.1.35", 41351), QString("192.168.1.35:41351"));
        QVERIFY(WirelessDiscovery::endpoint("192.168.35", 41351).isEmpty());
        QVERIFY(WirelessDiscovery::endpoint("192.168.1.35;whoami", 41351).isEmpty());
        QVERIFY(WirelessDiscovery::endpoint("192.168.1.35", 0).isEmpty());
        QVERIFY(WirelessDiscovery::endpoint("224.0.0.1", 5555).isEmpty());
        QCOMPARE(WirelessDiscovery::endpoint("::1", 1234), QString("[::1]:1234"));
        const auto result = WirelessDiscovery::connectionServices(
            "List of discovered mdns services\n"
            "phone\t_adb-tls-pairing._tcp\t192.168.1.35:41351\n"
            "phone\t_adb-tls-connect._tcp\t192.168.1.35:40781\n"
            "phone\t_adb-tls-connect._tcp.\t192.168.1.35:40781\n"
            "v6\t_adb-tls-connect._tcp\t[fe80::1%3]:5555\n"
            "broken\t_adb-tls-connect._tcp\t192.168.35:55\n");
        QCOMPARE(result, QStringList({"192.168.1.35:40781", "[fe80::1%3]:5555"}));
    }
    void profiles()
    {
        KeyMapDocument profile;
        QString error;
        const QByteArray data = R"({"switchKey":"Key_QuoteLeft","custom":"preserve",
            "keyMapNodes":[{"type":"KMT_CLICK","key":"Key_F","pos":{"x":0.2,"y":0.8},"switchMap":false},
            {"type":"KMT_STEER_WHEEL","custom":"keep advanced mappings"}]})";
        QVERIFY2(profile.load(data, &error), qPrintable(error));
        KeyMapDocument restored;
        QVERIFY(restored.load(profile.json(), &error));
        QCOMPARE(restored.root, profile.root);
        QCOMPARE(KeyMapDocument::normalized(QPointF(200, 400), QSize(1000, 500)), QPointF(0.2, 0.8));
        QCOMPARE(KeyMapDocument::normalized(QPointF(-100, 600), QSize(1000, 500)), QPointF(0, 1));
        const auto old = profile.json();
        QVERIFY(!profile.load("{}", &error));
        QVERIFY(!profile.load("{broken", &error));
        QCOMPARE(profile.json(), old);
        auto nodes = profile.nodes();
        nodes.append(nodes.first());
        restored.setNodes(nodes);
        QVERIFY(!profile.load(restored.json(), &error));
        QCOMPARE(profile.json(), old);
    }
    void metrics()
    {
        QCOMPARE(FrameMetrics::percentile({5, 1, 3, 4, 2}, .5), 3.0);
        QCOMPARE(FrameMetrics::percentile({}, .95), 0.0);
        FrameMetrics metrics;
        metrics.record(1000000, QSize(1280, 720));
        metrics.record(3000000, QSize(1280, 720));
        QCOMPARE(metrics.report().value("frames").toInt(), 2);
        QCOMPARE(metrics.report().value("callback_to_render_submit_ms").toObject().value("p95").toDouble(), 3.0);
        QTemporaryDir dir;
        QVERIFY(metrics.save(dir.filePath("report.json")));
        QVERIFY(!metrics.save(dir.filePath("missing/report.json")));
    }
    void latestFrameAndReferenceLifetime()
    {
        VideoBuffer buffer;
        QVERIFY(buffer.init());
        buffer.setRenderExpiredFrames(false);
        bool skipped = true;
        QVERIFY(fill(buffer.decodingFrame(), 64, 64, 20));
        buffer.offerDecodedFrame(skipped);
        QVERIFY(!skipped);
        QVERIFY(fill(buffer.decodingFrame(), 64, 64, 30));
        buffer.offerDecodedFrame(skipped);
        QVERIFY(skipped);
        buffer.lock();
        AVFrame *snapshot = av_frame_clone(buffer.consumeRenderedFrame());
        buffer.unLock();
        QVERIFY(snapshot);
        QCOMPARE(snapshot->data[0][0], uint8_t(30));
        for (int i = 40; i < 60; ++i) {
            QVERIFY(fill(buffer.decodingFrame(), 64, 64, i));
            buffer.offerDecodedFrame(skipped);
        }
        QCOMPARE(snapshot->data[0][0], uint8_t(30));
        buffer.lock();
        QCOMPARE(buffer.consumeRenderedFrame()->data[0][0], uint8_t(59));
        buffer.unLock();
        av_frame_free(&snapshot);
        buffer.deInit();
    }
    void screenshotBeforeFirstFrame()
    {
        VideoBuffer buffer;
        QVERIFY(buffer.init());
        bool called = false;
        buffer.peekRenderedFrame([&](int, int, uint8_t *) { called = true; });
        QVERIFY(!called);
        // This would deadlock if the empty/error path leaked the buffer lock.
        buffer.lock();
        buffer.unLock();
        buffer.deInit();
    }
    void reloadReleasesHeldMapping()
    {
        const QString script = R"({"switchKey":"Key_F6","keyMapNodes":[
            {"type":"KMT_CLICK","key":"Key_Space","pos":{"x":0.5,"y":0.5},"switchMap":false}]})";
        QList<QByteArray> packets;
        Controller controller([&](const QByteArray &data) { packets.append(data); return data.size(); }, script);
        const QSize frame(1080, 1920);
        QKeyEvent toggle(QEvent::KeyPress, Qt::Key_F6, Qt::NoModifier);
        controller.keyEvent(&toggle, frame, frame);
        QKeyEvent down(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
        controller.keyEvent(&down, frame, frame);
        QVERIFY(!packets.isEmpty());
        QCOMPARE(static_cast<unsigned char>(packets.last()[0]), 2u);
        QCOMPARE(static_cast<unsigned char>(packets.last()[1]), 0u);
        controller.updateScript(script);
        QCOMPARE(static_cast<unsigned char>(packets.last()[1]), 1u);
    }
    void reloadReleasesNormalKeyAndPinch()
    {
        QList<QByteArray> packets;
        Controller controller([&](const QByteArray &data) { packets.append(data); return data.size(); }, QString());
        const QSize frame(1080, 1920);
        QKeyEvent down(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier);
        controller.keyEvent(&down, frame, frame);
        QMouseEvent touch(QEvent::MouseButtonPress, QPointF(100, 200), Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
        controller.mouseEvent(&touch, frame, frame);
        const int before = packets.size();
        controller.updateScript(QString());
        QCOMPARE(packets.size(), before + 3); // key, primary finger, virtual finger
        for (int i = before; i < packets.size(); ++i) {
            QCOMPARE(static_cast<unsigned char>(packets[i][1]), 1u);
        }
    }
    void renderCurrentPaddedFrame()
    {
        QYUVOpenGLWidget widget;
        widget.resize(384, 256);
        widget.show();
        QTest::qWait(100);
        QVERIFY(widget.isValid());
        const int w = 66, h = 48, stride = 96, uvStride = 64;
        QByteArray y(stride * h, char(235));
        QByteArray uv(uvStride * h / 2, char(128));
        // Make padding conspicuous to catch accidental row-stride/crop errors.
        for (int row = 0; row < h; ++row) { std::memset(y.data() + row * stride + w, 16, stride - w); }
        widget.setFrameSize(QSize(w, h));
        widget.updateTextures(reinterpret_cast<quint8 *>(y.data()), reinterpret_cast<quint8 *>(uv.data()),
                              reinterpret_cast<quint8 *>(uv.data()), stride, uvStride, uvStride);
        auto image = widget.grabFramebuffer();
        image.save("render-white.png");
        qInfo() << "White pixel:" << image.pixelColor(image.width() * 3 / 4, image.height() / 2);
        QVERIFY(image.pixelColor(image.width() * 3 / 4, image.height() / 2).red() > 235);
        y.fill(16);
        widget.setSharpening(true);
        widget.updateTextures(reinterpret_cast<quint8 *>(y.data()), reinterpret_cast<quint8 *>(uv.data()),
                              reinterpret_cast<quint8 *>(uv.data()), stride, uvStride, uvStride);
        image = widget.grabFramebuffer();
        QVERIFY(image.pixelColor(image.width() / 2, image.height() / 2).red() < 20);
        widget.makeCurrent();
        QCOMPARE(widget.context()->functions()->glGetError(), GLenum(GL_NO_ERROR));
        widget.doneCurrent();
    }
    void frameCopyBaseline()
    {
        AVFrame *frame = av_frame_alloc();
        QVERIFY(fill(frame, 1920, 1080, 100));
        QBENCHMARK {
            QByteArray y(reinterpret_cast<char *>(frame->data[0]), frame->linesize[0] * frame->height);
            QByteArray u(reinterpret_cast<char *>(frame->data[1]), frame->linesize[1] * frame->height / 2);
            QByteArray v(reinterpret_cast<char *>(frame->data[2]), frame->linesize[2] * frame->height / 2);
            QVERIFY(y.at(0) == char(100) && !u.isEmpty() && !v.isEmpty());
        }
        av_frame_free(&frame);
    }
    void frameReferenceOptimized()
    {
        AVFrame *frame = av_frame_alloc();
        QVERIFY(fill(frame, 1920, 1080, 100));
        QBENCHMARK {
            AVFrame *reference = av_frame_clone(frame);
            QVERIFY(reference && reference->data[0][0] == 100);
            av_frame_free(&reference);
        }
        av_frame_free(&frame);
    }
    void renderBenchmark_data()
    {
        QTest::addColumn<bool>("sharpening");
        QTest::newRow("bilinear") << false;
        QTest::newRow("light-sharpening") << true;
    }
    void renderBenchmark()
    {
        QFETCH(bool, sharpening);
        BenchmarkWidget widget;
        widget.resize(1920, 1080);
        widget.show();
        QTest::qWait(100);
        QVERIFY(widget.isValid());
        widget.setFrameSize(QSize(1280, 720));
        widget.setSharpening(sharpening);
        widget.makeCurrent();
        qInfo() << "Renderer:" << reinterpret_cast<const char *>(widget.context()->functions()->glGetString(GL_RENDERER));
        widget.doneCurrent();
        QByteArray y(1280 * 720, 100), uv(640 * 360, 128);
        QBENCHMARK {
            widget.updateTextures(reinterpret_cast<quint8 *>(y.data()), reinterpret_cast<quint8 *>(uv.data()),
                                  reinterpret_cast<quint8 *>(uv.data()), 1280, 640, 640);
            widget.renderSynchronously();
        }
    }
};

int main(int argc, char **argv)
{
    // The same executable acts as a deterministic ADB child process in the
    // pairing tests. No real phone or network access is required in CI.
    if (argc > 1 && QByteArray(argv[1]) == "mdns") {
        std::puts("phone\t_adb-tls-connect._tcp\t127.0.0.1:9002");
        return 0;
    }
    if (argc > 1 && QByteArray(argv[1]) == "pair") {
        char code[32] = {};
        if (std::fgets(code, sizeof(code), stdin) && QByteArray(code).trimmed() == "123456") {
            std::puts("Successfully paired");
            return 0;
        }
        std::puts("Pairing failed");
        return 1;
    }
    if (argc > 2 && QByteArray(argv[1]) == "connect") {
        if (QByteArray(argv[2]).startsWith("192.0.2.1")) { QThread::msleep(30000); }
        std::puts("connected to phone");
        return 0;
    }
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("QtScrcpyTests");
    QCoreApplication::setApplicationName("WirelessTests");
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    Features features;
    return QTest::qExec(&features, argc, argv);
}
#include "test_features.moc"

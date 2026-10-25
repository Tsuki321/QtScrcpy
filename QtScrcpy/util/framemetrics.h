#pragma once
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSize>
#include <QVector>
#include <algorithm>
#include <cmath>

class FrameMetrics
{
public:
    static double percentile(QVector<double> values, double quantile)
    {
        if (values.isEmpty()) { return 0; }
        std::sort(values.begin(), values.end());
        const auto index = qBound(0, static_cast<int>(std::ceil(values.size() * quantile)) - 1, values.size() - 1);
        return values[index];
    }
    void record(qint64 submitNs, const QSize &frame)
    {
        if (!m_clock.isValid()) { m_clock.start(); }
        const qint64 now = m_clock.nsecsElapsed();
        if (m_count && m_intervals.size() < 100000) { m_intervals.append((now - m_previous) / 1e6); }
        if (m_submit.size() < 100000) { m_submit.append(submitNs / 1e6); }
        m_previous = now;
        ++m_count;
        m_frame = frame;
    }
    QJsonObject report() const
    {
        const double elapsed = m_clock.isValid() ? m_clock.nsecsElapsed() / 1e9 : 0;
        return {{"schema_version", 1}, {"frames", static_cast<double>(m_count)},
                {"elapsed_seconds", elapsed}, {"delivered_fps", elapsed > 0 ? (m_count - 1) / elapsed : 0},
                {"encoded_width", m_frame.width()}, {"encoded_height", m_frame.height()},
                {"callback_to_render_submit_ms", summary(m_submit)},
                {"frame_interval_ms", summary(m_intervals)},
                {"scope", "Desktop frame callback through GL submission; excludes phone capture, encoding, transport, compositor and display scanout. Not end-to-end latency."}};
    }
    bool save(const QString &path) const
    {
        QSaveFile file(path);
        const auto data = QJsonDocument(report()).toJson();
        return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
    }
private:
    static QJsonObject summary(const QVector<double> &samples)
    {
        return {{"samples", samples.size()}, {"p50", percentile(samples, 0.50)},
                {"p95", percentile(samples, 0.95)}, {"p99", percentile(samples, 0.99)}};
    }
    QElapsedTimer m_clock;
    qint64 m_count = 0;
    qint64 m_previous = 0;
    QSize m_frame;
    QVector<double> m_submit;
    QVector<double> m_intervals;
};

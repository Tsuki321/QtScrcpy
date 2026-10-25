#pragma once
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointF>
#include <QSize>
#include <QSet>
#include <cmath>

// Coordinates stay relative to the video, independent of window size or DPI.
class KeyMapDocument
{
public:
    QJsonObject root{{"switchKey", "Key_QuoteLeft"}, {"keyMapNodes", QJsonArray()}};
    bool load(const QByteArray &data, QString *error)
    {
        QJsonParseError parse;
        auto doc = QJsonDocument::fromJson(data, &parse);
        if (parse.error != QJsonParseError::NoError || !doc.isObject()
                || !doc.object().value("keyMapNodes").isArray()
                || !doc.object().value("switchKey").isString()) {
            *error = "Expected a keymap object with switchKey and keyMapNodes.";
            return false;
        }
        const auto candidate = doc.object();
        QSet<QString> keys;
        for (const auto &value : candidate.value("keyMapNodes").toArray()) {
            if (!value.isObject()) {
                *error = "Every mapping must be an object.";
                return false;
            }
            const auto node = value.toObject();
            if (node.value("type") != "KMT_CLICK") {
                continue; // Preserve advanced upstream mappings unchanged.
            }
            const QString key = node.value("key").toString();
            const auto pos = node.value("pos").toObject();
            if (key.isEmpty() || key == candidate.value("switchKey").toString() || keys.contains(key)
                    || !pos.value("x").isDouble() || !pos.value("y").isDouble()
                    || pos.value("x").toDouble() < 0 || pos.value("x").toDouble() > 1
                    || pos.value("y").toDouble() < 0 || pos.value("y").toDouble() > 1) {
                *error = "Click mappings need unique keys and coordinates between 0 and 1.";
                return false;
            }
            keys.insert(key);
        }
        root = candidate;
        return true;
    }
    QByteArray json() const { return QJsonDocument(root).toJson(QJsonDocument::Indented); }
    QJsonArray nodes() const { return root.value("keyMapNodes").toArray(); }
    void setNodes(const QJsonArray &nodes) { root["keyMapNodes"] = nodes; }
    static QPointF normalized(QPointF point, QSize size)
    {
        return {qBound(0.0, point.x() / qMax(1, size.width()), 1.0),
                qBound(0.0, point.y() / qMax(1, size.height()), 1.0)};
    }
};

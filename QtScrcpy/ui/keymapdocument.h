#pragma once
#include "KeyMapValidation.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointF>
#include <QSize>
#include <QMetaEnum>
#include <QSet>
#include <cmath>

// Coordinates stay relative to the video, independent of window size or DPI.
class KeyMapDocument
{
public:
    static bool validKey(const QString &key) { return KeyMapValidation::bindingId(key) != 0; }
    QJsonObject root{{"switchKey", "Key_QuoteLeft"}, {"keyMapNodes", QJsonArray()}};
    bool load(const QByteArray &data, QString *error)
    {
        return KeyMapValidation::parse(data, &root, error);
    }
    void clear()
    {
        root.remove("mouseMoveMap");
        root["keyMapNodes"] = QJsonArray();
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

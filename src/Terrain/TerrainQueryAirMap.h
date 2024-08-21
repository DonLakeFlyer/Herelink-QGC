/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QObject>
#include <QtNetwork/QNetworkReply>
#include <QtCore/QLoggingCategory>

#include "TerrainQueryInterface.h"

Q_DECLARE_LOGGING_CATEGORY(TerrainQueryAirMapLog)
Q_DECLARE_LOGGING_CATEGORY(TerrainQueryAirMapVerboseLog)

class QGeoCoordinate;
class QNetworkAccessManager;

/// AirMap online implementation of terrain queries
class TerrainAirMapQuery : public TerrainQueryInterface {
    Q_OBJECT

public:
    TerrainAirMapQuery(QObject* parent = nullptr);

    // Overrides from TerrainQueryInterface
    void requestCoordinateHeights   (const QList<QGeoCoordinate>& coordinates) final;
    void requestPathHeights         (const QGeoCoordinate& fromCoord, const QGeoCoordinate& toCoord) final;
    void requestCarpetHeights       (const QGeoCoordinate& swCoord, const QGeoCoordinate& neCoord, bool statsOnly) final;

private slots:
    void _requestError              (QNetworkReply::NetworkError code);
    void _requestFinished           (void);
    void _sslErrors                 (const QList<QSslError> &errors);

private:
    void _sendQuery                 (const QString& path, const QUrlQuery& urlQuery);
    void _requestFailed             (void);
    void _parseCoordinateData       (const QJsonValue& coordinateJson);
    void _parsePathData             (const QJsonValue& pathJson);
    void _parseCarpetData           (const QJsonValue& carpetJson);

    enum QueryMode {
        QueryModeCoordinates,
        QueryModePath,
        QueryModeCarpet
    };

    QNetworkAccessManager   *_networkManager = nullptr;
    QueryMode               _queryMode;
    bool                    _carpetStatsOnly;
};

/// AirMap offline cachable implementation of terrain queries
class TerrainOfflineAirMapQuery : public TerrainQueryInterface {
    Q_OBJECT

public:
    TerrainOfflineAirMapQuery(QObject* parent = nullptr);

    // Overrides from TerrainQueryInterface
    void requestCoordinateHeights(const QList<QGeoCoordinate>& coordinates) final;
    void requestPathHeights(const QGeoCoordinate& fromCoord, const QGeoCoordinate& toCoord) final;
    void requestCarpetHeights(const QGeoCoordinate& swCoord, const QGeoCoordinate& neCoord, bool statsOnly) final;

    // Internal methods
    void _signalCoordinateHeights(bool success, QList<double> heights);
    void _signalPathHeights(bool success, double distanceBetween, double finalDistanceBetween, const QList<double>& heights);
    void _signalCarpetHeights(bool success, double minHeight, double maxHeight, const QList<QList<double>>& carpet);
};

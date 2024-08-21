#pragma once

#include <QtCore/QObject>

class QGeoCoordinate;

/// Base class for offline/online terrain queries
class TerrainQueryInterface : public QObject
{
    Q_OBJECT

public:
    TerrainQueryInterface(QObject* parent) : QObject(parent) { }

    /// Request terrain heights for specified coodinates.
    /// Signals: coordinateHeights when data is available
    virtual void requestCoordinateHeights(const QList<QGeoCoordinate>& coordinates) = 0;

    /// Requests terrain heights along the path specified by the two coordinates.
    /// Signals: pathHeights
    ///     @param coordinates to query
    virtual void requestPathHeights(const QGeoCoordinate& fromCoord, const QGeoCoordinate& toCoord) = 0;

    /// Request terrain heights for the rectangular area specified.
    /// Signals: carpetHeights when data is available
    ///     @param swCoord South-West bound of rectangular area to query
    ///     @param neCoord North-East bound of rectangular area to query
    ///     @param statsOnly true: Return only stats, no carpet data
    virtual void requestCarpetHeights(const QGeoCoordinate& swCoord, const QGeoCoordinate& neCoord, bool statsOnly) = 0;

signals:
    void coordinateHeightsReceived(bool success, QList<double> heights);
    void pathHeightsReceived(bool success, double distanceBetween, double finalDistanceBetween, const QList<double>& heights);
    void carpetHeightsReceived(bool success, double minHeight, double maxHeight, const QList<QList<double>>& carpet);
};

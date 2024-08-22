#pragma once

#include <QtCore/QList>
#include <QtCore/QLoggingCategory>

class QGeoCoordinate;

Q_DECLARE_LOGGING_CATEGORY(TerrainTileLog)

/// Implements an interface for https://developers.airmap.com/v2.0/docs/elevation-api
class TerrainTile
{
public:
    TerrainTile() = default;

    /// Constructor from serialized elevation data (either from file or web)
    ///    @param document
    explicit TerrainTile(const QByteArray &byteArray);
    ~TerrainTile();

    /// Check whether valid data is loaded
    ///    @return true if data is valid
    bool isValid() const { return _isValid; }

    /// Evaluates the elevation at the given coordinate
    ///    @param coordinate
    ///    @return elevation
    double elevation(const QGeoCoordinate &coordinate) const;

    /// Accessor for the minimum elevation of the tile
    ///    @return minimum elevation
    double minElevation() const { return (_isValid ? static_cast<double>(_tileInfo.minElevation) : qQNaN()); }

    /// Accessor for the maximum elevation of the tile
    ///    @return maximum elevation
    double maxElevation() const { return (_isValid ? static_cast<double>(_tileInfo.maxElevation) : qQNaN()); }

    /// Accessor for the average elevation of the tile
    ///    @return average elevation
    double avgElevation() const { return (_isValid ? _tileInfo.avgElevation : qQNaN()); }

    static QByteArray serializeFromAirMapJson(const QByteArray &input);

    static constexpr double tileSizeDegrees = 0.01;                 ///< Each terrain tile represents a square area .01 degrees in lat/lon
    static constexpr double tileValueSpacingDegrees = (1.0 / 3600); ///< 1 Arc-Second spacing of elevation values
    static constexpr double tileValueSpacingMeters = 30.0;

private:
    struct TileInfo_t {
        double  swLat, swLon, neLat, neLon;
        int16_t minElevation;
        int16_t maxElevation;
        double  avgElevation;
        int16_t gridSizeLat;
        int16_t gridSizeLon;
    };

    TileInfo_t _tileInfo{};
    QList<QList<int16_t>> _elevationData;   /// 2D elevation data array
    double _cellSizeLat = 0.0;              /// data grid size in latitude direction
    double _cellSizeLon = 0.0;              /// data grid size in longitude direction
    bool _isValid = false;                  /// data loaded is valid

    // Json keys
    static constexpr const char *_jsonStatusKey = "status";
    static constexpr const char *_jsonDataKey = "data";
    static constexpr const char *_jsonBoundsKey = "bounds";
    static constexpr const char *_jsonSouthWestKey = "sw";
    static constexpr const char *_jsonNorthEastKey = "ne";
    static constexpr const char *_jsonStatsKey = "stats";
    static constexpr const char *_jsonMaxElevationKey = "max";
    static constexpr const char *_jsonMinElevationKey = "min";
    static constexpr const char *_jsonAvgElevationKey = "avg";
    static constexpr const char *_jsonCarpetKey = "carpet";
};

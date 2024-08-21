#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QQueue>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlTableModel>

#include "QGCTile.h"

class QGCCacheTile;
class QGCCachedTileSet;

Q_DECLARE_LOGGING_CATEGORY(QGCTileCacheDBLog)

/*===========================================================================*/

class SetTilesTableModel: public QSqlTableModel
{
    Q_OBJECT

public:
    explicit SetTilesTableModel(QObject *parent = nullptr, QSqlDatabase db = QSqlDatabase());
    ~SetTilesTableModel();

    bool create();
    bool insertSetTile(quint64 setID, quint64 tileID);
    bool deleteTileSet(quint64 setID);
    bool drop();
};

/*===========================================================================*/

class TileSetsTableModel: public QSqlTableModel
{
    Q_OBJECT

public:
    explicit TileSetsTableModel(QObject *parent = nullptr, QSqlDatabase db = QSqlDatabase());
    ~TileSetsTableModel();

    bool create();
    bool insertTileSet(const QGCCachedTileSet &tileSet);
    bool getTileSets(QList<QGCCachedTileSet*> &tileSets);
    bool setName(quint64 setID, const QString &newName);
    bool setNumTiles(quint64 setID, quint64 numTiles);
    bool getTileSetID(quint64 &setID, const QString &name);
    bool getDefaultTileSet(quint64 &setID);
    bool deleteTileSet(quint64 setID);
    bool drop();
    bool createDefaultTileSet();

private:
    static quint64 _defaultSet;
};

/*===========================================================================*/

class TilesTableModel: public QSqlTableModel
{
    Q_OBJECT

public:
    explicit TilesTableModel(QObject *parent = nullptr, QSqlDatabase db = QSqlDatabase());
    ~TilesTableModel();

    bool create();
    bool getTile(QGCCacheTile *tile, const QString &hash);
    bool getTileID(quint64 &tileID, const QString &hash);
    bool insertTile(const QGCCacheTile &tile);
    bool deleteTileSet(quint64 setID);
    bool updateSetTotals(QGCCachedTileSet &set, quint64 setID);
    bool updateTotals(quint32 &totalCount, quint64 &totalSize, quint32 &defaultCount, quint64 &defaultSize, quint64 defaultTileSetID);
    bool prune(quint64 defaultTileSetID, qint64 amount);
    bool deleteBingNoTileImageTiles();
    bool drop();

private:
    static QByteArray _bingNoTileImage;
};

/*===========================================================================*/

class TilesDownloadTableModel: public QSqlTableModel
{
    Q_OBJECT

public:
    explicit TilesDownloadTableModel(QObject *parent = nullptr, QSqlDatabase db = QSqlDatabase());
    ~TilesDownloadTableModel();

    bool create();
    bool setState(quint64 setID, const QString &hash, int state);
    bool setState(quint64 setID, int state);
    bool deleteTileSet(quint64 setID);
    bool getTileDownloadList(QQueue<QGCTile*> &tiles, QGCTile::TileState state, quint64 setID, int count, const QString &hash);
    bool updateTilesDownloadSet(QGCTile::TileState state, quint64 setID, const QString &hash);
    bool drop();
};

/*===========================================================================*/

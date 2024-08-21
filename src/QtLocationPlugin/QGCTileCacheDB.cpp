#include "QGCTileCacheDB.h"
#include "QGCCacheTile.h"
#include "QGCCachedTileSet.h"
#include "QGCMapUrlEngine.h"
#include "QGCLoggingCategory.h"

#include <QtCore/QDateTime>
#include <QtCore/QFile>
#include <QtCore/QSettings>
#include <QtSql/QSqlError>

QGC_LOGGING_CATEGORY(QGCTileCacheDBLog, "qgc.qtlocationplugin.qgctilecachedb")

/*===========================================================================*/

SetTilesTableModel::SetTilesTableModel(QObject *parent, QSqlDatabase db)
    : QSqlTableModel(parent, db)
{
    // qCDebug(QGCTileCacheDBLog) << Q_FUNC_INFO << this;

    setTable("setTiles");
}

SetTilesTableModel::~SetTilesTableModel()
{
    // qCDebug(QGCTileCacheDBLog) << Q_FUNC_INFO << this;
}

bool SetTilesTableModel::create()
{
    QSqlQuery query;
    query.prepare(
        "CREATE TABLE IF NOT EXISTS SetTiles ("
        "setID INTEGER, "
        "tileID INTEGER)"
    );
    return query.exec();
}

bool SetTilesTableModel::insertSetTile(quint64 setID, quint64 tileID)
{
    QSqlQuery query;
    query.prepare("INSERT INTO SetTiles(tileID, setID) VALUES(?, ?)"); // INSERT OR IGNORE?
    query.addBindValue(tileID);
    query.addBindValue(setID);
    if (!query.exec()) {
        qCWarning(QGCTileCacheDBLog) << Q_FUNC_INFO << query.lastError().text();
        return false;
    }

    return true;
}

bool SetTilesTableModel::deleteTileSet(quint64 setID)
{
    QSqlQuery query;
    (void) query.prepare("DELETE FROM SetTiles WHERE setID = ?");
    query.addBindValue(setID);
    if (!query.exec()) {
        qCWarning(QGCTileCacheDBLog) << Q_FUNC_INFO << query.lastError().text();
        return false;
    }

    return true;
}

bool SetTilesTableModel::drop()
{
    QSqlQuery query;
    query.prepare("DROP TABLE SetTiles");
    return query.exec();
}

/*===========================================================================*/

quint64 TileSetsTableModel::_defaultSet = UINT64_MAX;

TileSetsTableModel::TileSetsTableModel(QObject *parent, QSqlDatabase db)
    : QSqlTableModel(parent, db)
{
    setTable("TileSets");

    // qCDebug(QGCTileCacheDBLog) << Q_FUNC_INFO << this;
}

TileSetsTableModel::~TileSetsTableModel()
{
    // qCDebug(QGCTileCacheDBLog) << Q_FUNC_INFO << this;
}

bool TileSetsTableModel::create()
{
    QSqlQuery query;
    query.prepare(
        "CREATE TABLE IF NOT EXISTS TileSets ("
        "setID INTEGER PRIMARY KEY NOT NULL, "
        "name TEXT NOT NULL UNIQUE, "
        "typeStr TEXT, "
        "topleftLat REAL DEFAULT 0.0, "
        "topleftLon REAL DEFAULT 0.0, "
        "bottomRightLat REAL DEFAULT 0.0, "
        "bottomRightLon REAL DEFAULT 0.0, "
        "minZoom INTEGER DEFAULT 3, "
        "maxZoom INTEGER DEFAULT 3, "
        "type INTEGER DEFAULT -1, "
        "numTiles INTEGER DEFAULT 0, "
        "defaultSet INTEGER DEFAULT 0, "
        "date INTEGER DEFAULT 0)"
    );
    return query.exec();
}

bool TileSetsTableModel::insertTileSet(const QGCCachedTileSet &tileSet)
{
    QSqlQuery query;
    (void) query.prepare(
        "INSERT INTO TileSets"
        "(name, typeStr, topleftLat, topleftLon, bottomRightLat, bottomRightLon, minZoom, maxZoom, type, numTiles, date) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
    );
    query.addBindValue(tileSet.name());
    query.addBindValue(tileSet.mapTypeStr());
    query.addBindValue(tileSet.topleftLat());
    query.addBindValue(tileSet.topleftLon());
    query.addBindValue(tileSet.bottomRightLat());
    query.addBindValue(tileSet.bottomRightLon());
    query.addBindValue(tileSet.minZoom());
    query.addBindValue(tileSet.maxZoom());
    query.addBindValue(UrlFactory::getQtMapIdFromProviderType(tileSet.type()));
    query.addBindValue(tileSet.totalTileCount());
    query.addBindValue(QDateTime::currentDateTime().toSecsSinceEpoch());

    if (!query.exec()) {
        qCWarning(QGCTileCacheDBLog) << Q_FUNC_INFO << query.lastError().text();
        return false;
    }

    return true;
}

bool TileSetsTableModel::getTileSets(QList<QGCCachedTileSet*> &tileSets)
{
    QSqlQuery query;
    query.prepare("SELECT * FROM TileSets ORDER BY defaultSet DESC, name ASC");
    if (!query.exec()) {
        qCDebug(QGCTileCacheDBLog) << Q_FUNC_INFO << query.lastError().text();
        return false;
    }

    while (query.next()) {
        const QString name = query.value("name").toString();
        QGCCachedTileSet* const set = new QGCCachedTileSet(name);
        set->setId(query.value("setID").toULongLong());
        set->setMapTypeStr(query.value("typeStr").toString());
        set->setTopleftLat(query.value("topleftLat").toDouble());
        set->setTopleftLon(query.value("topleftLon").toDouble());
        set->setBottomRightLat(query.value("bottomRightLat").toDouble());
        set->setBottomRightLon(query.value("bottomRightLon").toDouble());
        set->setMinZoom(query.value("minZoom").toInt());
        set->setMaxZoom(query.value("maxZoom").toInt());
        set->setType(UrlFactory::getProviderTypeFromQtMapId(query.value("type").toInt()));
        set->setTotalTileCount(query.value("numTiles").toUInt());
        set->setDefaultSet(query.value("defaultSet").toInt() != 0);
        set->setCreationDate(QDateTime::fromSecsSinceEpoch(query.value("date").toUInt()));
        (void) tileSets.append(set);
    }

    return true;
}

bool TileSetsTableModel::setName(quint64 setID, const QString &newName)
{
    QSqlQuery query;
    query.prepare("UPDATE TileSets SET name = ? WHERE setID = ?");
    query.addBindValue(newName);
    query.addBindValue(setID);
    return query.exec();
}

bool TileSetsTableModel::setNumTiles(quint64 setID, quint64 numTiles)
{
    QSqlQuery query;
    query.prepare("UPDATE TileSets SET numTiles = ? WHERE setID = ?");
    query.addBindValue(numTiles);
    query.addBindValue(setID);
    return query.exec();
}

bool TileSetsTableModel::getTileSetID(quint64 &setID, const QString &name)
{
    QSqlQuery query;
    query.prepare("SELECT setID FROM TileSets WHERE name = ?");
    query.addBindValue(name);
    if (query.exec() && query.next()) {
        setID = query.value(0).toULongLong();
        return true;
    }

    return false;
}

bool TileSetsTableModel::getDefaultTileSet(quint64 &setID)
{
    if (_defaultSet != UINT64_MAX) {
        setID = _defaultSet;
        return true;
    }

    QSqlQuery query;
    query.prepare("SELECT setID FROM TileSets WHERE defaultSet = 1");
    if (query.exec() && query.next()) {
        _defaultSet = query.value(0).toULongLong();
        setID = _defaultSet;
        return true;
    }

    setID = 1;
    return false;
}

bool TileSetsTableModel::deleteTileSet(quint64 setID)
{
    QSqlQuery query;
    (void) query.prepare("DELETE FROM TileSets WHERE setID = ?");
    query.addBindValue(setID);
    if (!query.exec()) {
        qCWarning(QGCTileCacheDBLog) << Q_FUNC_INFO << query.lastError().text();
        return false;
    }

    return true;
}

bool TileSetsTableModel::drop()
{
    QSqlQuery query;
    query.prepare("DROP TABLE TileSets");
    return query.exec();
}

bool TileSetsTableModel::createDefaultTileSet()
{
    static const QString kDefaultSet = QStringLiteral("Default Tile Set");

    QSqlQuery query;
    (void) query.prepare("SELECT name FROM TileSets WHERE name = ?");
    query.addBindValue(kDefaultSet);
    if (!query.exec()) {
        qCWarning(QGCTileCacheDBLog) << Q_FUNC_INFO << query.lastError().text();
        return false;
    }

    if (query.next()) {
        return true;
    }

    (void) query.prepare("INSERT INTO TileSets(name, defaultSet, date) VALUES(?, ?, ?)");
    query.addBindValue(kDefaultSet);
    query.addBindValue(1);
    query.addBindValue(QDateTime::currentDateTime().toSecsSinceEpoch());
    if (!query.exec()) {
        qCWarning(QGCTileCacheDBLog) << Q_FUNC_INFO << query.lastError().text();
        return false;
    }

    return true;
}


/*===========================================================================*/

QByteArray TilesTableModel::_bingNoTileImage;

TilesTableModel::TilesTableModel(QObject *parent, QSqlDatabase db)
    : QSqlTableModel(parent, db)
{
    setTable("Tiles");

    // qCDebug(QGCTileCacheDBLog) << Q_FUNC_INFO << this;
}

TilesTableModel::~TilesTableModel()
{
    // qCDebug(QGCTileCacheDBLog) << Q_FUNC_INFO << this;
}

bool TilesTableModel::create()
{
    QSqlQuery query;
    query.prepare(
        "CREATE TABLE IF NOT EXISTS Tiles ("
        "tileID INTEGER PRIMARY KEY NOT NULL, "
        "hash TEXT NOT NULL UNIQUE, "
        "format TEXT NOT NULL, "
        "tile BLOB NULL, "
        "size INTEGER, "
        "type INTEGER, "
        "date INTEGER DEFAULT 0)"
    );
    if (!query.exec()) {
        qCWarning(QGCTileCacheDBLog) << Q_FUNC_INFO << query.lastError().text();
        return false;
    }

    query.prepare(
        "CREATE INDEX IF NOT EXISTS hash ON Tiles ("
        "hash, "
        "size, "
        "type)"
    );

    return query.exec();
}

bool TilesTableModel::getTile(QGCCacheTile *tile, const QString &hash)
{
    QSqlQuery query;
    (void) query.prepare("SELECT tile, format, type FROM Tiles WHERE hash = ?");
    query.addBindValue(hash);
    if (!query.exec() || !query.next()) {
        qCDebug(QGCTileCacheDBLog) << Q_FUNC_INFO << query.lastError().text();;
        return false;
    }

    const QByteArray array = query.value(0).toByteArray();
    const QString format = query.value(1).toString();
    const QString type = query.value(2).toString();
    tile = new QGCCacheTile(hash, array, format, type);
    qCDebug(QGCTileCacheDBLog) << Q_FUNC_INFO << query.lastError().text();;

    return true;
}

bool TilesTableModel::getTileID(quint64 &tileID, const QString &hash)
{
    QSqlQuery query;
    (void) query.prepare("SELECT tileID FROM Tiles WHERE hash = ?");
    query.addBindValue(hash);
    if (query.exec() && query.next()) {
        tileID = query.value(0).toULongLong();
        return true;
    }

    qCDebug(QGCTileCacheDBLog) << Q_FUNC_INFO << query.lastError().text();;
    return false;
}

bool TilesTableModel::insertTile(const QGCCacheTile &tile)
{
    QSqlQuery query;
    (void) query.prepare("INSERT INTO Tiles(hash, format, tile, size, type, date) VALUES(?, ?, ?, ?, ?, ?)");
    query.addBindValue(tile.hash());
    query.addBindValue(tile.format());
    query.addBindValue(tile.img());
    query.addBindValue(tile.img().size());
    query.addBindValue(tile.type());
    query.addBindValue(QDateTime::currentDateTime().toSecsSinceEpoch());
    if (!query.exec()) {
        qCWarning(QGCTileCacheDBLog) << Q_FUNC_INFO << query.lastError().text();
        return false;
    }

    return true;
}

bool TilesTableModel::deleteTileSet(quint64 setID)
{
    QSqlQuery query;
    (void) query.prepare("DELETE FROM Tiles WHERE tileID IN (SELECT A.tileID FROM SetTiles A JOIN SetTiles B ON A.tileID = B.tileID WHERE B.setID = ? GROUP BY A.tileID HAVING COUNT(A.tileID) = 1)");
    query.addBindValue(setID);
    if (!query.exec()) {
        qCWarning(QGCTileCacheDBLog) << Q_FUNC_INFO << query.lastError().text();
        return false;
    }

    return true;
}

bool TilesTableModel::updateSetTotals(QGCCachedTileSet &set, quint64 setID)
{
    QSqlQuery query;
    (void) query.prepare("SELECT COUNT(size), SUM(size) FROM Tiles A INNER JOIN SetTiles B on A.tileID = B.tileID WHERE B.setID = ?");
    query.addBindValue(setID);
    if (!query.exec() || !query.next()) {
        qCWarning(QGCTileCacheDBLog) << Q_FUNC_INFO << "query failed";
        return false;
    }

    set.setSavedTileCount(query.value(0).toUInt());
    set.setSavedTileSize(query.value(1).toULongLong());
    qCDebug(QGCTileCacheDBLog) << "Set" << set.id() << "Totals:" << set.savedTileCount() << set.savedTileSize() << "Expected:" << set.totalTileCount() << set.totalTilesSize();

    quint64 avg = UrlFactory::averageSizeForType(set.type());
    if (set.totalTileCount() <= set.savedTileCount()) {
        set.setTotalTileSize(set.savedTileSize());
    } else {
        if ((set.savedTileCount() > 10) && set.savedTileSize()) {
            avg = (set.savedTileSize() / set.savedTileCount());
        }
        set.setTotalTileSize(avg * set.totalTileCount());
    }

    quint32 ucount = 0;
    quint64 usize = 0;
    (void) query.prepare("SELECT COUNT(size), SUM(size) FROM Tiles WHERE tileID IN (SELECT A.tileID FROM SetTiles A join SetTiles B on A.tileID = B.tileID WHERE B.setID = ? GROUP by A.tileID HAVING COUNT(A.tileID) = 1)");
    query.addBindValue(setID);
    if (query.exec() && query.next()) {
        ucount = query.value(0).toUInt();
        usize = query.value(1).toULongLong();
    }

    quint32 expectedUcount = set.totalTileCount() - set.savedTileCount();
    if (ucount == 0) {
        usize = expectedUcount * avg;
    } else {
        expectedUcount = ucount;
    }
    set.setUniqueTileCount(expectedUcount);
    set.setUniqueTileSize(usize);

    return true;
}

bool TilesTableModel::updateTotals(quint32 &totalCount, quint64 &totalSize, quint32 &defaultCount, quint64 &defaultSize, quint64 defaultTileSetID)
{
    QSqlQuery query;
    (void) query.prepare("SELECT COUNT(size), SUM(size) FROM Tiles");
    if (!query.exec() || !query.next()) {
        return false;
    }
    totalCount = query.value(0).toUInt();
    totalSize = query.value(1).toULongLong();

    (void) query.prepare("SELECT COUNT(size), SUM(size) FROM Tiles WHERE tileID IN (SELECT A.tileID FROM SetTiles A join SetTiles B on A.tileID = B.tileID WHERE B.setID = ? GROUP by A.tileID HAVING COUNT(A.tileID) = 1)");
    query.addBindValue(defaultTileSetID);
    if (!query.exec() || !query.next()) {
        return false;
    }
    defaultCount = query.value(0).toUInt();
    defaultSize = query.value(1).toULongLong();

    return true;
}

bool TilesTableModel::prune(quint64 defaultTileSetID, qint64 amount)
{
    QSqlQuery query;
    (void) query.prepare("SELECT tileID, size, hash FROM Tiles WHERE tileID IN (SELECT A.tileID FROM SetTiles A join SetTiles B on A.tileID = B.tileID WHERE B.setID = ? GROUP by A.tileID HAVING COUNT(A.tileID) = 1) ORDER BY DATE ASC LIMIT 128");
    query.addBindValue(defaultTileSetID);
    if (!query.exec()) {
        qCWarning(QGCTileCacheDBLog) << Q_FUNC_INFO << query.lastError().text();
        return false;
    }

    QQueue<quint64> tilelist;
    while (query.next() && (amount >= 0)) {
        (void) tilelist.enqueue(query.value(0).toULongLong());
        amount -= query.value(1).toULongLong();
    }

    while (!tilelist.isEmpty()) {
        const quint64 tileID = tilelist.dequeue();
        query.prepare("DELETE FROM Tiles WHERE tileID = ?");
        query.addBindValue(tileID);
        if (!query.exec()) {
            return false;
        }
    }

    return true;
}

bool TilesTableModel::deleteBingNoTileImageTiles()
{
    static const QString alreadyDoneKey = QStringLiteral("_deleteBingNoTileTilesDone");

    QSettings settings;
    if (settings.value(alreadyDoneKey, false).toBool()) {
        return true;
    }
    settings.setValue(alreadyDoneKey, true);

    if (_bingNoTileImage.isEmpty()) {
        QFile file("://res/BingNoTileBytes.dat");
        if (file.open(QFile::ReadOnly)) {
            _bingNoTileImage = file.readAll();
            file.close();
            if (_bingNoTileImage.isEmpty()) {
                qCWarning(QGCTileCacheDBLog) << "Unable to read BingNoTileBytes";
                return false;
            }
        } else {
            qCWarning(QGCTileCacheDBLog) << "Open File Failed";
            return false;
        }
    }

    QSqlQuery query;
    query.prepare("SELECT tileID, tile, hash FROM Tiles WHERE LENGTH(tile) = ?");
    query.addBindValue(_bingNoTileImage.length());
    if (!query.exec()) {
        qCWarning(QGCTileCacheDBLog) << Q_FUNC_INFO << query.lastError().text();
        return false;
    }

    QList<quint64> idsToDelete;
    while (query.next()) {
        if (query.value(1).toByteArray() == _bingNoTileImage) {
            (void) idsToDelete.append(query.value(0).toULongLong());
        }
    }

    for (const quint64 tileId: idsToDelete) {
        query.prepare("DELETE FROM Tiles WHERE tileID = ?");
        query.addBindValue(tileId);
        if (!query.exec()) {
            qCWarning(QGCTileCacheDBLog) << Q_FUNC_INFO << query.lastError().text();
        }
    }

    return true;
}

bool TilesTableModel::drop()
{
    QSqlQuery query;
    query.prepare("DROP TABLE Tiles");
    return query.exec();
}

/*===========================================================================*/

TilesDownloadTableModel::TilesDownloadTableModel(QObject *parent, QSqlDatabase db)
    : QSqlTableModel(parent, db)
{
    setTable("TilesDownload");

    // qCDebug(QGCTileCacheDBLog) << Q_FUNC_INFO << this;
}

TilesDownloadTableModel::~TilesDownloadTableModel()
{
    // qCDebug(QGCTileCacheDBLog) << Q_FUNC_INFO << this;
}

bool TilesDownloadTableModel::create()
{
    QSqlQuery query;
    query.prepare(
        "CREATE TABLE IF NOT EXISTS TilesDownload ("
        "setID INTEGER, "
        "hash TEXT NOT NULL UNIQUE, "
        "type INTEGER, "
        "x INTEGER, "
        "y INTEGER, "
        "z INTEGER, "
        "state INTEGER DEFAULT 0)"
    );
    return query.exec();
}

bool TilesDownloadTableModel::setState(quint64 setID, const QString &hash, int state)
{
    QSqlQuery query;
    query.prepare("UPDATE TilesDownload SET state = ? WHERE setID = ? AND hash = ?");
    query.addBindValue(state);
    query.addBindValue(setID);
    query.addBindValue(hash);
    return query.exec();
}

bool TilesDownloadTableModel::setState(quint64 setID, int state)
{
    QSqlQuery query;
    query.prepare("UPDATE TilesDownload SET state = ? WHERE setID = ?");
    query.addBindValue(state);
    query.addBindValue(setID);
    return query.exec();
}

bool TilesDownloadTableModel::deleteTileSet(quint64 setID)
{
    QSqlQuery query;
    (void) query.prepare("DELETE FROM TilesDownload WHERE setID = ?");
    query.addBindValue(setID);
    if (!query.exec()) {
        qCWarning(QGCTileCacheDBLog) << Q_FUNC_INFO << query.lastError().text();
        return false;
    }

    return true;
}

bool TilesDownloadTableModel::getTileDownloadList(QQueue<QGCTile*> &tiles, QGCTile::TileState state, quint64 setID, int count, const QString &hash)
{
    QSqlQuery query;
    query.prepare("SELECT hash, type, x, y, z FROM TilesDownload WHERE setID = ? AND state = 0 LIMIT ?");
    query.addBindValue(setID);
    query.addBindValue(count);
    if (!query.exec()) {
        qCWarning(QGCTileCacheDBLog) << Q_FUNC_INFO << query.lastError().text();
        return false;
    }

    while (query.next()) {
        QGCTile* const tile = new QGCTile();
        // tile->setTileSet(task->setID());
        tile->setHash(query.value("hash").toString());
        tile->setType(UrlFactory::getProviderTypeFromQtMapId(query.value("type").toInt()));
        tile->setX(query.value("x").toInt());
        tile->setY(query.value("y").toInt());
        tile->setZ(query.value("z").toInt());
        (void) tiles.enqueue(tile);
    }

    for (qsizetype i = 0; i < tiles.size(); i++) {
        query.prepare("UPDATE TilesDownload SET state = ? WHERE setID = ? AND hash = ?");
        query.addBindValue(static_cast<int>(QGCTile::StateDownloading));
        query.addBindValue(setID);
        query.addBindValue(hash);
        if (!query.exec()) {
            qCWarning(QGCTileCacheDBLog) << Q_FUNC_INFO << query.lastError().text();
        }
    }

    return true;
}

bool TilesDownloadTableModel::updateTilesDownloadSet(QGCTile::TileState state, quint64 setID, const QString &hash)
{
    QSqlQuery query;
    if (state == QGCTile::StateComplete) {
        query.prepare("DELETE FROM TilesDownload WHERE setID = ? AND hash = ?");
        query.addBindValue(setID);
        query.addBindValue(hash);
    } else if (hash == "*") {
        query.prepare("UPDATE TilesDownload SET state = ? WHERE setID = ?");
        query.addBindValue(state);
        query.addBindValue(setID);
    } else {
        query.prepare("UPDATE TilesDownload SET state = ? WHERE setID = ? AND hash = ?");
        query.addBindValue(state);
        query.addBindValue(setID);
        query.addBindValue(hash);
    }

    if (!query.exec()) {
        qCWarning(QGCTileCacheDBLog) << Q_FUNC_INFO << query.lastError().text();
        return false;
    }

    return true;
}

bool TilesDownloadTableModel::drop()
{
    QSqlQuery query;
    query.prepare("DROP TABLE TilesDownload");
    return query.exec();
}

/*===========================================================================*/

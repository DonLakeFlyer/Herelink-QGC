/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/


/**
 * @file
 *   @brief Map Tile Cache Worker Thread
 *
 *   @author Gus Grubba <gus@auterion.com>
 *
 */

#include "QGCTileCacheWorker.h"
#include "QGCTileCacheDB.h"
#include "QGCCachedTileSet.h"
#include "QGCMapTasks.h"
#include "QGCMapUrlEngine.h"
#include "QGCLoggingCategory.h"

#include <QtCore/QDateTime>
#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QSettings>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>

const QString QGCCacheWorker::kSession = QString(kSessionName);
QByteArray QGCCacheWorker::_bingNoTileImage;

QGC_LOGGING_CATEGORY(QGCTileCacheWorkerLog, "qgc.qtlocationplugin.qgctilecacheworker")

QGCCacheWorker::QGCCacheWorker(QObject *parent)
    : QThread(parent)
{
    // qCDebug(QGCTileCacheWorkerLog) << Q_FUNC_INFO << this;
}

QGCCacheWorker::~QGCCacheWorker()
{
    stop();

    // qCDebug(QGCTileCacheWorkerLog) << Q_FUNC_INFO << this;
}

void QGCCacheWorker::stop()
{
    // TODO: Send Stop Task Instead?
    QMutexLocker lock(&_taskQueueMutex);
    qDeleteAll(_taskQueue);
    lock.unlock();

    if (isRunning()) {
        _waitc.wakeAll();
    }
}

bool QGCCacheWorker::enqueueTask(QGCMapTask *task)
{
    if (!_valid && (task->type() != QGCMapTask::taskInit)) {
        task->setError("Database Not Initialized");
        task->deleteLater();
        return false;
    }

    // TODO: Prepend Stop Task Instead?
    QMutexLocker lock(&_taskQueueMutex);
    _taskQueue.enqueue(task);
    lock.unlock();

    if (isRunning()) {
        _waitc.wakeAll();
    } else {
        start(QThread::HighPriority);
    }

    return true;
}

void QGCCacheWorker::run()
{
    if (!_valid && !_failed) {
        (void) _init();
    }

    if (_valid) {
        if (_connectDB()) {
            _deleteBingNoTileTiles();
        }
    }

    QMutexLocker lock(&_taskQueueMutex);
    while (true) {
        if (!_taskQueue.isEmpty()) {
            QGCMapTask* const task = _taskQueue.dequeue();
            lock.unlock();
            _runTask(task);
            lock.relock();
            task->deleteLater();

            const qsizetype count = _taskQueue.count();
            if (count > 100) {
                _updateTimeout = LONG_TIMEOUT;
            } else if (count < 25) {
                _updateTimeout = SHORT_TIMEOUT;
            }

            if ((count == 0) || _updateTimer.hasExpired(_updateTimeout)) {
                if (_valid) {
                    lock.unlock();
                    _updateTotals();
                    lock.relock();
                }
            }
        } else {
            (void) _waitc.wait(lock.mutex(), 5000);
            if (_taskQueue.isEmpty()) {
                break;
            }
        }
    }
    lock.unlock();

    for (const QString &connection: QSqlDatabase::connectionNames()) {
        QSqlDatabase db = QSqlDatabase::database(connection, false);
        if (db.isOpen()) {
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);
    }
}

void QGCCacheWorker::_runTask(QGCMapTask *task)
{
    switch (task->type()) {
    case QGCMapTask::taskInit:
        break;
    case QGCMapTask::taskCacheTile:
        _saveTile(task);
        break;
    case QGCMapTask::taskFetchTile:
        _getTile(task);
        break;
    case QGCMapTask::taskFetchTileSets:
        _getTileSets(task);
        break;
    case QGCMapTask::taskCreateTileSet:
        _createTileSet(task);
        break;
    case QGCMapTask::taskGetTileDownloadList:
        _getTileDownloadList(task);
        break;
    case QGCMapTask::taskUpdateTileDownloadState:
        _updateTileDownloadState(task);
        break;
    case QGCMapTask::taskDeleteTileSet:
        _deleteTileSet(task);
        break;
    case QGCMapTask::taskRenameTileSet:
        _renameTileSet(task);
        break;
    case QGCMapTask::taskPruneCache:
        _pruneCache(task);
        break;
    case QGCMapTask::taskReset:
        _resetCacheDatabase(task);
        break;
    case QGCMapTask::taskExport:
        _exportSets(task);
        break;
    case QGCMapTask::taskImport:
        _importSets(task);
        break;
    default:
        qCWarning(QGCTileCacheWorkerLog) << Q_FUNC_INFO << "given unhandled task type" << task->type();
        break;
    }
}

void QGCCacheWorker::_deleteBingNoTileTiles()
{
    static const QString alreadyDoneKey = QStringLiteral("_deleteBingNoTileTilesDone");

    QSettings settings;
    if (settings.value(alreadyDoneKey, false).toBool()) {
        return;
    }
    settings.setValue(alreadyDoneKey, true);

    if (_bingNoTileImage.isEmpty()) {
        QFile file("://res/BingNoTileBytes.dat");
        if (file.open(QFile::ReadOnly)) {
            _bingNoTileImage = file.readAll();
            file.close();
        } else {
            qCWarning(QGCTileCacheWorkerLog) << "Open File Failed";
        }
    }

    if (_bingNoTileImage.isEmpty()) {
        qCWarning(QGCTileCacheWorkerLog) << "Unable to read BingNoTileBytes";
        return;
    }

    QSqlDatabase db = QSqlDatabase::database(kSession);
    QSqlQuery query(db);
    query.prepare("SELECT tileID, tile, hash FROM Tiles WHERE LENGTH(tile) = ?");
    query.addBindValue(_bingNoTileImage.length());
    if (!query.exec()) {
        qCWarning(QGCTileCacheWorkerLog) << Q_FUNC_INFO << "query failed";
        return;
    }

    QList<quint64> idsToDelete;
    while (query.next()) {
        if (query.value(1).toByteArray() == _bingNoTileImage) {
            (void) idsToDelete.append(query.value(0).toULongLong());
            qCDebug(QGCTileCacheWorkerLog) << Q_FUNC_INFO << "HASH:" << query.value(2).toString();
        }
    }

    for (const quint64 tileId: idsToDelete) {
        query.prepare("DELETE FROM Tiles WHERE tileID = ?");
        query.addBindValue(tileId);
        if (!query.exec()) {
            qCWarning(QGCTileCacheWorkerLog) << "Delete failed";
        }
    }
}

bool QGCCacheWorker::_findTileSetID(const QString &name, quint64 &setID)
{
    QSqlDatabase db = QSqlDatabase::database(kSession);
    QSqlQuery query(db);
    query.prepare("SELECT setID FROM TileSets WHERE name = ?");
    query.addBindValue(name);
    if (query.exec() && query.next()) {
        setID = query.value(0).toULongLong();
        return true;
    }

    return false;
}

quint64 QGCCacheWorker::_getDefaultTileSet()
{
    if (_defaultSet != UINT64_MAX) {
        return _defaultSet;
    }

    QSqlDatabase db = QSqlDatabase::database(kSession);
    QSqlQuery query(db);
    query.prepare("SELECT setID FROM TileSets WHERE defaultSet = 1");
    if (query.exec() && query.next()) {
        _defaultSet = query.value(0).toULongLong();
        return _defaultSet;
    }

    return 1;
}

void QGCCacheWorker::_saveTile(QGCMapTask *mtask)
{
    if (!_testTask(mtask)) {
        return;
    }

    if (!_valid) {
        qCWarning(QGCTileCacheWorkerLog) << "Map Cache SQL error (saveTile() open db): Not Valid";
        return;
    }

    QGCSaveTileTask* const task = static_cast<QGCSaveTileTask*>(mtask);

    QSqlDatabase db = QSqlDatabase::database(kSession);
    QSqlQuery query(db);
    (void) query.prepare("INSERT INTO Tiles(hash, format, tile, size, type, date) VALUES(?, ?, ?, ?, ?, ?)");
    query.addBindValue(task->tile()->hash());
    query.addBindValue(task->tile()->format());
    query.addBindValue(task->tile()->img());
    query.addBindValue(task->tile()->img().size());
    query.addBindValue(task->tile()->type());
    query.addBindValue(QDateTime::currentDateTime().toSecsSinceEpoch());
    if (!query.exec()) {
        qCWarning(QGCTileCacheWorkerLog) << Q_FUNC_INFO << "query failed";
        return;
    }

    const quint64 tileID = query.lastInsertId().toULongLong();
    const quint64 setID = (task->tile()->tileSet() == UINT64_MAX) ? _getDefaultTileSet() : task->tile()->tileSet();
    query.prepare("INSERT INTO SetTiles(tileID, setID) VALUES(?, ?)"); // INSERT OR IGNORE?
    query.addBindValue(tileID);
    query.addBindValue(setID);
    if (!query.exec()) {
        qCWarning(QGCTileCacheWorkerLog) << "Map Cache SQL error (add tile into SetTiles):" << query.lastError().text();
    }
}


void QGCCacheWorker::_getTile(QGCMapTask *mtask)
{
    if (!_testTask(mtask)) {
        return;
    }

    QGCFetchTileTask* const task = static_cast<QGCFetchTileTask*>(mtask);

    QSqlDatabase db = QSqlDatabase::database(kSession);
    QSqlQuery query(db);
    (void) query.prepare("SELECT tile, format, type FROM Tiles WHERE hash = ?");
    query.addBindValue(task->hash());
    if (query.exec() && query.next()) {
        const QByteArray array = query.value(0).toByteArray();
        const QString format = query.value(1).toString();
        const QString type = query.value(2).toString();
        QGCCacheTile* const tile = new QGCCacheTile(task->hash(), array, format, type);
        task->setTileFetched(tile);
        qCDebug(QGCTileCacheWorkerLog) << Q_FUNC_INFO << "(Found in DB) HASH:" << task->hash();
        return;
    }

    qCDebug(QGCTileCacheWorkerLog) << Q_FUNC_INFO << "(NOT in DB) HASH:" << task->hash();
    task->setError("Tile not in cache database");
}

void QGCCacheWorker::_getTileSets(QGCMapTask *mtask)
{
    if (!_testTask(mtask)) {
        return;
    }

    QGCFetchTileSetTask* const task = static_cast<QGCFetchTileSetTask*>(mtask);

    QSqlDatabase db = QSqlDatabase::database(kSession);
    QSqlQuery query(db);
    query.prepare("SELECT * FROM TileSets ORDER BY defaultSet DESC, name ASC");
    if (!query.exec()) {
        qCDebug(QGCTileCacheWorkerLog) << Q_FUNC_INFO << "No tile set in database";
        task->setError("No tile set in database");
        return;
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
        _updateSetTotals(set);
        set->moveToThread(QCoreApplication::instance()->thread());
        task->setTileSetFetched(set);
    }
}

void QGCCacheWorker::_updateSetTotals(QGCCachedTileSet *set)
{
    if (set->defaultSet()) {
        _updateTotals();
        set->setSavedTileCount(_totalCount);
        set->setSavedTileSize(_totalSize);
        set->setTotalTileCount(_defaultCount);
        set->setTotalTileSize(_defaultSize);
        return;
    }

    QSqlDatabase db = QSqlDatabase::database(kSession);
    QSqlQuery query(db);
    (void) query.prepare("SELECT COUNT(size), SUM(size) FROM Tiles A INNER JOIN SetTiles B on A.tileID = B.tileID WHERE B.setID = ?");
    query.addBindValue(set->id());
    if (!query.exec() || !query.next()) {
        qCWarning(QGCTileCacheWorkerLog) << Q_FUNC_INFO << "query failed";
        return;
    }

    set->setSavedTileCount(query.value(0).toUInt());
    set->setSavedTileSize(query.value(1).toULongLong());
    qCDebug(QGCTileCacheWorkerLog) << "Set" << set->id() << "Totals:" << set->savedTileCount() << " " << set->savedTileSize() << "Expected: " << set->totalTileCount() << " " << set->totalTilesSize();

    quint64 avg = UrlFactory::averageSizeForType(set->type());
    if (set->totalTileCount() <= set->savedTileCount()) {
        set->setTotalTileSize(set->savedTileSize());
    } else {
        if (set->savedTileCount() > 10 && set->savedTileSize()) {
            avg = set->savedTileSize() / set->savedTileCount();
        }
        set->setTotalTileSize(avg * set->totalTileCount());
    }

    quint32 ucount = 0;
    quint64 usize = 0;
    (void) query.prepare("SELECT COUNT(size), SUM(size) FROM Tiles WHERE tileID IN (SELECT A.tileID FROM SetTiles A join SetTiles B on A.tileID = B.tileID WHERE B.setID = ? GROUP by A.tileID HAVING COUNT(A.tileID) = 1)");
    query.addBindValue(set->id());
    if (query.exec() && query.next()) {
        ucount = query.value(0).toUInt();
        usize = query.value(1).toULongLong();
    }

    quint32 expectedUcount = set->totalTileCount() - set->savedTileCount();
    if (ucount == 0) {
        usize = expectedUcount * avg;
    } else {
        expectedUcount = ucount;
    }
    set->setUniqueTileCount(expectedUcount);
    set->setUniqueTileSize(usize);
}

void QGCCacheWorker::_updateTotals()
{
    QSqlDatabase db = QSqlDatabase::database(kSession);
    QSqlQuery query(db);
    (void) query.prepare("SELECT COUNT(size), SUM(size) FROM Tiles");
    if (query.exec() && query.next()) {
        _totalCount = query.value(0).toUInt();
        _totalSize = query.value(1).toULongLong();
    }

    (void) query.prepare("SELECT COUNT(size), SUM(size) FROM Tiles WHERE tileID IN (SELECT A.tileID FROM SetTiles A join SetTiles B on A.tileID = B.tileID WHERE B.setID = ? GROUP by A.tileID HAVING COUNT(A.tileID) = 1)");
    query.addBindValue(_getDefaultTileSet());
    if (query.exec() && query.next()) {
        _defaultCount = query.value(0).toUInt();
        _defaultSize  = query.value(1).toULongLong();
    }

    emit updateTotals(_totalCount, _totalSize, _defaultCount, _defaultSize);
    if (!_updateTimer.isValid()) {
        _updateTimer.start();
    } else {
        (void) _updateTimer.restart();
    }
}

quint64 QGCCacheWorker::_findTile(const QString &hash)
{
    QSqlDatabase db = QSqlDatabase::database(kSession);

    quint64 tileID = 0;
    QSqlQuery query(db);
    (void) query.prepare("SELECT tileID FROM Tiles WHERE hash = ?");
    query.addBindValue(hash);
    if (query.exec() && query.next()) {
        tileID = query.value(0).toULongLong();
    }

    return tileID;
}

void QGCCacheWorker::_createTileSet(QGCMapTask *mtask)
{
    QGCCreateTileSetTask* const task = static_cast<QGCCreateTileSetTask*>(mtask);

    if (!_valid) {
        task->setError("Error saving tile set");
        return;
    }

    QGCCachedTileSet* const tileSet = task->tileSet();

    QSqlDatabase db = QSqlDatabase::database(kSession);
    QSqlQuery query(db);
    static const QString insertTileSet = QStringLiteral(
        "INSERT INTO TileSets"
        "(name, typeStr, topleftLat, topleftLon, bottomRightLat, bottomRightLon, minZoom, maxZoom, type, numTiles, date) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
    );
    (void) query.prepare(insertTileSet);
    query.addBindValue(tileSet->name());
    query.addBindValue(tileSet->mapTypeStr());
    query.addBindValue(tileSet->topleftLat());
    query.addBindValue(tileSet->topleftLon());
    query.addBindValue(tileSet->bottomRightLat());
    query.addBindValue(tileSet->bottomRightLon());
    query.addBindValue(tileSet->minZoom());
    query.addBindValue(tileSet->maxZoom());
    query.addBindValue(UrlFactory::getQtMapIdFromProviderType(tileSet->type()));
    query.addBindValue(tileSet->totalTileCount());
    query.addBindValue(QDateTime::currentDateTime().toSecsSinceEpoch());

    if (!query.exec()) {
        qCWarning(QGCTileCacheWorkerLog) << "Map Cache SQL error (add tileSet into TileSets):" << query.lastError().text();
        task->setError("Error saving tile set");
        return;
    }

    const quint64 setID = query.lastInsertId().toULongLong();
    tileSet->setId(setID);
    (void) db.transaction();

    for (int z = tileSet->minZoom(); z <= tileSet->maxZoom(); z++) {
        const QGCTileSet set = UrlFactory::getTileCount(
            z,
            tileSet->topleftLon(),
            tileSet->topleftLat(),
            tileSet->bottomRightLon(),
            tileSet->bottomRightLat(),
            tileSet->type()
        );
        const QString type = tileSet->type();
        for (int x = set.tileX0; x <= set.tileX1; x++) {
            for (int y = set.tileY0; y <= set.tileY1; y++) {
                const QString hash = UrlFactory::getTileHash(type, x, y, z);
                const quint64 tileID = _findTile(hash);
                if (!tileID) {
                    (void) query.prepare(QStringLiteral("INSERT OR IGNORE INTO TilesDownload(setID, hash, type, x, y, z, state) VALUES(?, ?, ?, ?, ? ,? ,?)"));
                    query.addBindValue(setID);
                    query.addBindValue(hash);
                    query.addBindValue(UrlFactory::getQtMapIdFromProviderType(type));
                    query.addBindValue(x);
                    query.addBindValue(y);
                    query.addBindValue(z);
                    query.addBindValue(0);
                    if (!query.exec()) {
                        qCWarning(QGCTileCacheWorkerLog) << "Map Cache SQL error (add tile into TilesDownload):" << query.lastError().text();
                        mtask->setError("Error creating tile set download list");
                        return;
                    }
                } else {
                    const QString s = QStringLiteral("INSERT OR IGNORE INTO SetTiles(tileID, setID) VALUES(%1, %2)").arg(tileID).arg(setID);
                    if (!query.exec(s)) {
                        qCWarning(QGCTileCacheWorkerLog) << "Map Cache SQL error (add tile into SetTiles):" << query.lastError().text();
                    }
                    qCDebug(QGCTileCacheWorkerLog) << Q_FUNC_INFO << "Already Cached HASH:" << hash;
                }
            }
        }
    }

    (void) db.commit();
    _updateSetTotals(tileSet);
    task->setTileSetSaved();
}

void QGCCacheWorker::_getTileDownloadList(QGCMapTask *mtask)
{
    if (!_testTask(mtask)) {
        return;
    }

    QGCGetTileDownloadListTask* const task = static_cast<QGCGetTileDownloadListTask*>(mtask);

    QQueue<QGCTile*> tiles;

    QSqlDatabase db = QSqlDatabase::database(kSession);
    QSqlQuery query(db);
    query.prepare("SELECT hash, type, x, y, z FROM TilesDownload WHERE setID = ? AND state = 0 LIMIT ?");
    query.addBindValue(task->setID());
    query.addBindValue(task->count());
    if (query.exec()) {
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
            query.addBindValue(task->setID());
            query.addBindValue(tiles[i]->hash());
            if (!query.exec()) {
                qCWarning(QGCTileCacheWorkerLog) << "Map Cache SQL error (set TilesDownload state):" << query.lastError().text();
            }
        }
    }

    task->setTileListFetched(tiles);
}

void QGCCacheWorker::_updateTileDownloadState(QGCMapTask *mtask)
{
    if (!_testTask(mtask)) {
        return;
    }

    QGCUpdateTileDownloadStateTask* const task = static_cast<QGCUpdateTileDownloadStateTask*>(mtask);

    QSqlDatabase db = QSqlDatabase::database(kSession);
    QSqlQuery query(db);
    if (task->state() == QGCTile::StateComplete) {
        query.prepare("DELETE FROM TilesDownload WHERE setID = ? AND hash = ?");
        query.addBindValue(task->setID());
        query.addBindValue(task->hash());
    } else if (task->hash() == "*") {
        query.prepare("UPDATE TilesDownload SET state = ? WHERE setID = ?");
        query.addBindValue(task->state());
        query.addBindValue(task->setID());
    } else {
        query.prepare("UPDATE TilesDownload SET state = ? WHERE setID = ? AND hash = ?");
        query.addBindValue(task->state());
        query.addBindValue(task->setID());
        query.addBindValue(task->hash());
    }

    if (!query.exec()) {
        qCWarning(QGCTileCacheWorkerLog) << Q_FUNC_INFO << "Error:" << query.lastError().text();
    }
}

void QGCCacheWorker::_pruneCache(QGCMapTask *mtask)
{
    if (!_testTask(mtask)) {
        return;
    }

    QGCPruneCacheTask* const task = static_cast<QGCPruneCacheTask*>(mtask);

    QSqlDatabase db = QSqlDatabase::database(kSession);
    QSqlQuery query(db);
    (void) query.prepare("SELECT tileID, size, hash FROM Tiles WHERE tileID IN (SELECT A.tileID FROM SetTiles A join SetTiles B on A.tileID = B.tileID WHERE B.setID = ? GROUP by A.tileID HAVING COUNT(A.tileID) = 1) ORDER BY DATE ASC LIMIT 128");
    query.addBindValue(_getDefaultTileSet());
    if (!query.exec()) {
        qCWarning(QGCTileCacheWorkerLog) << Q_FUNC_INFO << query.lastError().text();
        return;
    }

    qint64 amount = static_cast<qint64>(task->amount());
    QQueue<quint64> tlist;
    while (query.next() && (amount >= 0)) {
        (void) tlist.enqueue(query.value(0).toULongLong());
        amount -= query.value(1).toULongLong();
        qCDebug(QGCTileCacheWorkerLog) << Q_FUNC_INFO << "HASH:" << query.value(2).toString();
    }

    while (!tlist.isEmpty()) {
        const quint64 tileID = tlist.dequeue();
        query.prepare("DELETE FROM Tiles WHERE tileID = ?");
        query.addBindValue(tileID);
        if (!query.exec()) {
            break;
        }
    }

    task->setPruned();
}

void QGCCacheWorker::_deleteTileSet(QGCMapTask *mtask)
{
    if (!_testTask(mtask)) {
        return;
    }

    QGCDeleteTileSetTask* const task = static_cast<QGCDeleteTileSetTask*>(mtask);
    _deleteTileSet(task->setID());
    task->setTileSetDeleted();
}

void QGCCacheWorker::_deleteTileSet(quint64 id)
{
    QSqlDatabase db = QSqlDatabase::database(kSession);
    QSqlQuery query(db);

    (void) query.prepare("DELETE FROM Tiles WHERE tileID IN (SELECT A.tileID FROM SetTiles A JOIN SetTiles B ON A.tileID = B.tileID WHERE B.setID = ? GROUP BY A.tileID HAVING COUNT(A.tileID) = 1)");
    query.addBindValue(id);
    (void) query.exec();

    (void) query.prepare("DELETE FROM TilesDownload WHERE setID = ?");
    query.addBindValue(id);
    (void) query.exec();

    (void) query.prepare("DELETE FROM TileSets WHERE setID = ?");
    query.addBindValue(id);
    (void) query.exec();

    (void) query.prepare("DELETE FROM SetTiles WHERE setID = ?");
    query.addBindValue(id);
    (void) query.exec();

    _updateTotals();
}

void QGCCacheWorker::_renameTileSet(QGCMapTask *mtask)
{
    if (!_testTask(mtask)) {
        return;
    }

    QGCRenameTileSetTask* const task = static_cast<QGCRenameTileSetTask*>(mtask);

    QSqlDatabase db = QSqlDatabase::database(kSession);
    QSqlQuery query(db);
    query.prepare("UPDATE TileSets SET name = ? WHERE setID = ?");
    query.addBindValue(task->newName());
    query.addBindValue(task->setID());
    if (!query.exec()) {
        task->setError("Error renaming tile set");
    }
}


void QGCCacheWorker::_resetCacheDatabase(QGCMapTask *mtask)
{
    if (!_testTask(mtask)) {
        return;
    }

    QGCResetTask* const task = static_cast<QGCResetTask*>(mtask);

    QSqlDatabase db = QSqlDatabase::database(kSession);
    QSqlQuery query(db);

    query.prepare("DROP TABLE Tiles");
    (void) query.exec();

    query.prepare("DROP TABLE TileSets");
    (void) query.exec();

    query.prepare("DROP TABLE SetTiles");
    (void) query.exec();

    query.prepare("DROP TABLE TilesDownload");
    (void) query.exec();

    _valid = _createDB(db);

    task->setResetCompleted();
}

void QGCCacheWorker::_importSets(QGCMapTask *mtask)
{
    if (!_testTask(mtask)) {
        return;
    }

    QGCImportTileTask* const task = static_cast<QGCImportTileTask*>(mtask);

    QSqlDatabase db = QSqlDatabase::database(kSession);

    if (task->replace()) {
        db.close();
        QSqlDatabase::removeDatabase(kSession);
        QFile file(_databasePath);
        (void) file.remove();
        (void) QFile::copy(task->path(), _databasePath);
        task->setProgress(25);
        (void) _init();
        if (_valid) {
            task->setProgress(50);
            (void) _connectDB();
        }
        task->setProgress(100);
        task->setImportCompleted();
        return;
    }

    QSqlDatabase dbImport = QSqlDatabase::addDatabase("QSQLITE", QStringLiteral("QGeoTileImportSession"));
    dbImport.setDatabaseName(task->path());
    dbImport.setConnectOptions(QStringLiteral("QSQLITE_ENABLE_SHARED_CACHE"));
    if (dbImport.open()) {
        quint64 tileCount = 0;
        QSqlQuery query(dbImport);
        query.prepare("SELECT COUNT(tileID) FROM Tiles");
        if (query.exec() && query.next()) {
            tileCount = query.value(0).toULongLong();
        }

        if (tileCount) {
            query.prepare("SELECT * FROM TileSets ORDER BY defaultSet DESC, name ASC");
            if (query.exec()) {
                quint64 currentCount = 0;
                int lastProgress = -1;
                while (query.next()) {
                    QString name = query.value("name").toString();
                    const quint64 setID = query.value("setID").toULongLong();
                    const QString mapType = query.value("typeStr").toString();
                    const double topleftLat = query.value("topleftLat").toDouble();
                    const double topleftLon = query.value("topleftLon").toDouble();
                    const double bottomRightLat = query.value("bottomRightLat").toDouble();
                    const double bottomRightLon = query.value("bottomRightLon").toDouble();
                    const int minZoom = query.value("minZoom").toInt();
                    const int maxZoom = query.value("maxZoom").toInt();
                    const int type = query.value("type").toInt();
                    const quint32 numTiles = query.value("numTiles").toUInt();
                    const int defaultSet = query.value("defaultSet").toInt();
                    quint64 insertSetID = _getDefaultTileSet();
                    if (defaultSet == 0) {
                        if (_findTileSetID(name, insertSetID)) {
                            int testCount = 0;
                            while (true) {
                                const QString testName = QString::asprintf("%s %02d", name.toLatin1().data(), ++testCount);
                                if (!_findTileSetID(testName, insertSetID) || (testCount > 99)) {
                                    name = testName;
                                    break;
                                }
                            }
                        }

                        QSqlQuery cQuery(db);
                        (void) cQuery.prepare("INSERT INTO TileSets("
                            "name, typeStr, topleftLat, topleftLon, bottomRightLat, bottomRightLon, minZoom, maxZoom, type, numTiles, defaultSet, date"
                            ") VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
                        );
                        cQuery.addBindValue(name);
                        cQuery.addBindValue(mapType);
                        cQuery.addBindValue(topleftLat);
                        cQuery.addBindValue(topleftLon);
                        cQuery.addBindValue(bottomRightLat);
                        cQuery.addBindValue(bottomRightLon);
                        cQuery.addBindValue(minZoom);
                        cQuery.addBindValue(maxZoom);
                        cQuery.addBindValue(type);
                        cQuery.addBindValue(numTiles);
                        cQuery.addBindValue(defaultSet);
                        cQuery.addBindValue(QDateTime::currentDateTime().toSecsSinceEpoch());
                        if (!cQuery.exec()) {
                            task->setError("Error adding imported tile set to database");
                            break;
                        } else {
                            insertSetID = cQuery.lastInsertId().toULongLong();
                        }
                    }

                    QSqlQuery cQuery(db);
                    QSqlQuery subQuery(dbImport);
                    subQuery.prepare("SELECT * FROM Tiles WHERE tileID IN (SELECT A.tileID FROM SetTiles A JOIN SetTiles B ON A.tileID = B.tileID WHERE B.setID = ? GROUP BY A.tileID HAVING COUNT(A.tileID) = 1)");
                    subQuery.addBindValue(setID);
                    if (subQuery.exec()) {
                        quint64 tilesFound = 0;
                        quint64 tilesSaved = 0;
                        (void) db.transaction();
                        while (subQuery.next()) {
                            tilesFound++;
                            const QString hash = subQuery.value("hash").toString();
                            const QString format = subQuery.value("format").toString();
                            const QByteArray img = subQuery.value("tile").toByteArray();
                            const int type = subQuery.value("type").toInt();
                            (void) cQuery.prepare("INSERT INTO Tiles(hash, format, tile, size, type, date) VALUES(?, ?, ?, ?, ?, ?)");
                            cQuery.addBindValue(hash);
                            cQuery.addBindValue(format);
                            cQuery.addBindValue(img);
                            cQuery.addBindValue(img.size());
                            cQuery.addBindValue(type);
                            cQuery.addBindValue(QDateTime::currentDateTime().toSecsSinceEpoch());
                            if (cQuery.exec()) {
                                tilesSaved++;
                                const quint64 importTileID = cQuery.lastInsertId().toULongLong();
                                cQuery.prepare("INSERT INTO SetTiles(tileID, setID) VALUES(?, ?)");
                                cQuery.addBindValue(importTileID);
                                cQuery.addBindValue(insertSetID);
                                (void) cQuery.exec();
                                currentCount++;
                                if (tileCount) {
                                    const int progress = static_cast<int>((static_cast<double>(currentCount) / static_cast<double>(tileCount)) * 100.0);
                                    if (lastProgress != progress) {
                                        lastProgress = progress;
                                        task->setProgress(progress);
                                    }
                                }
                            }
                        }

                        (void) db.commit();
                        if (tilesSaved) {
                            cQuery.prepare("SELECT COUNT(size) FROM Tiles A INNER JOIN SetTiles B on A.tileID = B.tileID WHERE B.setID = ?");
                            cQuery.addBindValue(insertSetID);
                            if (cQuery.exec() && cQuery.next()) {
                                const quint64 count = cQuery.value(0).toULongLong();
                                cQuery.prepare("UPDATE TileSets SET numTiles = ? WHERE setID = ?");
                                cQuery.addBindValue(count);
                                cQuery.addBindValue(insertSetID);
                                (void) cQuery.exec();
                            }
                        }

                        const qint64 uniqueTiles = tilesFound - tilesSaved;
                        if (static_cast<quint64>(uniqueTiles) < tileCount) {
                            tileCount -= uniqueTiles;
                        } else {
                            tileCount = 0;
                        }

                        if (!tilesSaved && !defaultSet) {
                            _deleteTileSet(insertSetID);
                            qCDebug(QGCTileCacheWorkerLog) << "No unique tiles in" << name << "Removing it.";
                        }
                    }
                }
            } else {
                task->setError("No tile set in database");
            }
        }

        if (!tileCount) {
            task->setError("No unique tiles in imported database");
        }
    } else {
        task->setError("Error opening import database");
    }

    dbImport.close();
    QSqlDatabase::removeDatabase(dbImport.connectionName());

    task->setImportCompleted();
}

void QGCCacheWorker::_exportSets(QGCMapTask *mtask)
{
    if (!_testTask(mtask)) {
        return;
    }

    QGCExportTileTask* const task = static_cast<QGCExportTileTask*>(mtask);

    QSqlDatabase db = QSqlDatabase::database(kSession);

    QFile file(task->path());
    (void) file.remove();

    QSqlDatabase dbExport = QSqlDatabase::addDatabase("QSQLITE", QStringLiteral("QGeoTileExportSession"));
    dbExport.setDatabaseName(task->path());
    dbExport.setConnectOptions(QStringLiteral("QSQLITE_ENABLE_SHARED_CACHE"));
    if (dbExport.open()) {
        if (_createDB(dbExport, false)) {
            quint64 tileCount = 0;
            quint64 currentCount = 0;
            for (qsizetype i = 0; i < task->sets().count(); i++) {
                const QGCCachedTileSet* const set = task->sets().at(i);
                if (set->defaultSet()) {
                    tileCount += set->totalTileCount();
                } else {
                    tileCount += set->uniqueTileCount();
                }
            }

            if (tileCount == 0) {
                tileCount = 1;
            }

            for (qsizetype i = 0; i < task->sets().count(); i++) {
                const QGCCachedTileSet* const set = task->sets().at(i);

                QSqlQuery exportQuery(dbExport);
                (void) exportQuery.prepare("INSERT INTO TileSets("
                    "name, typeStr, topleftLat, topleftLon, bottomRightLat, bottomRightLon, minZoom, maxZoom, type, numTiles, defaultSet, date"
                    ") VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
                );
                exportQuery.addBindValue(set->name());
                exportQuery.addBindValue(set->mapTypeStr());
                exportQuery.addBindValue(set->topleftLat());
                exportQuery.addBindValue(set->topleftLon());
                exportQuery.addBindValue(set->bottomRightLat());
                exportQuery.addBindValue(set->bottomRightLon());
                exportQuery.addBindValue(set->minZoom());
                exportQuery.addBindValue(set->maxZoom());
                exportQuery.addBindValue(UrlFactory::getQtMapIdFromProviderType(set->type()));
                exportQuery.addBindValue(set->totalTileCount());
                exportQuery.addBindValue(set->defaultSet());
                exportQuery.addBindValue(QDateTime::currentDateTime().toSecsSinceEpoch());
                if (!exportQuery.exec()) {
                    task->setError("Error adding tile set to exported database");
                    break;
                }

                const quint64 exportSetID = exportQuery.lastInsertId().toULongLong();

                QSqlQuery query(db);
                query.prepare("SELECT * FROM SetTiles WHERE setID = ?");
                query.addBindValue(set->id());
                if (query.exec()) {
                    dbExport.transaction();
                    while (query.next()) {
                        const quint64 tileID = query.value("tileID").toULongLong();
                        QSqlQuery subQuery(db);
                        subQuery.prepare("SELECT * FROM Tiles WHERE tileID = ?");
                        subQuery.addBindValue(tileID);
                        if (subQuery.exec() && subQuery.next()) {
                            const QString hash = subQuery.value("hash").toString();
                            const QString format = subQuery.value("format").toString();
                            const QByteArray img = subQuery.value("tile").toByteArray();
                            const int type = subQuery.value("type").toInt();

                            (void) exportQuery.prepare("INSERT INTO Tiles(hash, format, tile, size, type, date) VALUES(?, ?, ?, ?, ?, ?)");
                            exportQuery.addBindValue(hash);
                            exportQuery.addBindValue(format);
                            exportQuery.addBindValue(img);
                            exportQuery.addBindValue(img.size());
                            exportQuery.addBindValue(type);
                            exportQuery.addBindValue(QDateTime::currentDateTime().toSecsSinceEpoch());
                            if (exportQuery.exec()) {
                                const quint64 exportTileID = exportQuery.lastInsertId().toULongLong();
                                exportQuery.prepare("INSERT INTO SetTiles(tileID, setID) VALUES(?, ?)");
                                exportQuery.addBindValue(exportTileID);
                                exportQuery.addBindValue(exportSetID);
                                (void) exportQuery.exec();
                                currentCount++;
                                task->setProgress(static_cast<int>(static_cast<double>(currentCount) / static_cast<double>(tileCount) * 100.0));
                            }
                        }
                    }
                    (void) dbExport.commit();
                }
            }
        } else {
            task->setError("Error creating export database");
        }
    } else {
        qCCritical(QGCTileCacheWorkerLog) << "Map Cache SQL error (create export database):" << dbExport.lastError();
        task->setError("Error opening export database");
    }

    dbExport.close();
    QSqlDatabase::removeDatabase(dbExport.connectionName());

    task->setExportCompleted();
}

bool QGCCacheWorker::_testTask(QGCMapTask *mtask)
{
    if (!_valid) {
        mtask->setError("No Cache Database");
        return false;
    }

    return true;
}

bool QGCCacheWorker::_init()
{
    _failed = false;

    if (_databasePath.isEmpty()) {
        qCCritical(QGCTileCacheWorkerLog) << "Could not find suitable cache directory.";
        _failed = true;
        return _failed;
    }

    qCDebug(QGCTileCacheWorkerLog) << "Mapping cache directory:" << _databasePath;
    if (!_connectDB()) {
        qCCritical(QGCTileCacheWorkerLog) << "Map Cache SQL error (init() open db)";
        _failed = true;
        return _failed;
    }

    QSqlDatabase db = QSqlDatabase::database(kSession);
    _valid = _createDB(db);
    if (!_valid) {
        _failed = true;
    }

    db.close();
    QSqlDatabase::removeDatabase(kSession);

    return _failed;
}

bool QGCCacheWorker::_connectDB()
{
    if (!QSqlDatabase::contains(kSession)) {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", kSession);
        db.setDatabaseName(_databasePath);
        db.setConnectOptions(QStringLiteral("QSQLITE_ENABLE_SHARED_CACHE"));
        _valid = db.open();
    } else {
        QSqlDatabase db = QSqlDatabase::database(kSession);
        if (!db.isOpen()) {
            _valid = db.open();
        } else {
            _valid = true;
        }
    }

    return _valid;
}

bool QGCCacheWorker::_createDB(QSqlDatabase &db, bool createDefault)
{
    static const QString kDefaultSet = QStringLiteral("Default Tile Set");

    static const QString createTilesTable = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS Tiles ("
        "tileID INTEGER PRIMARY KEY NOT NULL, "
        "hash TEXT NOT NULL UNIQUE, "
        "format TEXT NOT NULL, "
        "tile BLOB NULL, "
        "size INTEGER, "
        "type INTEGER, "
        "date INTEGER DEFAULT 0)"
    );

    static const QString createHashTilesIndex = QStringLiteral(
        "CREATE INDEX IF NOT EXISTS hash ON Tiles ("
        "hash, "
        "size, "
        "type)"
    );

    static const QString createTileSetsTable = QStringLiteral(
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

    static const QString createSetTilesTable = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS SetTiles ("
        "setID INTEGER, "
        "tileID INTEGER)"
    );

    static const QString createTilesDownloadTable = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS TilesDownload ("
        "setID INTEGER, "
        "hash TEXT NOT NULL UNIQUE, "
        "type INTEGER, "
        "x INTEGER, "
        "y INTEGER, "
        "z INTEGER, "
        "state INTEGER DEFAULT 0)"
    );

    static const QString selectDefaultSet = QStringLiteral("SELECT name FROM TileSets WHERE name = ?");

    static const QString insertTileSets = QStringLiteral("INSERT INTO TileSets(name, defaultSet, date) VALUES(?, ?, ?)");

    bool res = false;

    QSqlQuery query(db);
    if (!query.exec(createTilesTable)) {
        qCWarning(QGCTileCacheWorkerLog) << "Map Cache SQL error (create Tiles db):" << query.lastError().text();
    } else {
        (void) query.exec(createHashTilesIndex);

        if (!query.exec(createTileSetsTable)) {
            qCWarning(QGCTileCacheWorkerLog) << "Map Cache SQL error (create TileSets db):" << query.lastError().text();
        } else if (!query.exec(createSetTilesTable)) {
            qCWarning(QGCTileCacheWorkerLog) << "Map Cache SQL error (create SetTiles db):" << query.lastError().text();
        } else if (!query.exec(createTilesDownloadTable)) {
            qCWarning(QGCTileCacheWorkerLog) << "Map Cache SQL error (create TilesDownload db):" << query.lastError().text();
        } else {
            res = true;
        }
    }

    if (res && createDefault) {
        query.prepare(selectDefaultSet);
        query.addBindValue(kDefaultSet);
        if (query.exec()) {
            if (!query.next()) {
                (void) query.prepare(insertTileSets);
                query.addBindValue(kDefaultSet);
                query.addBindValue(1);
                query.addBindValue(QDateTime::currentDateTime().toSecsSinceEpoch());
                if (!query.exec()) {
                    qCWarning(QGCTileCacheWorkerLog) << "Map Cache SQL error (Creating default tile set):" << db.lastError();
                    res = false;
                }
            }
        } else {
            qCWarning(QGCTileCacheWorkerLog) << "Map Cache SQL error (Looking for default tile set):" << db.lastError();
        }
    }

    if (!res) {
        QFile file(_databasePath);
        (void) file.remove();
    }

    return res;
}

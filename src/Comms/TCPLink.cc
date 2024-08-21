/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "TCPLink.h"
#include "DeviceInfo.h"
#include "QGCLoggingCategory.h"

#include <QtNetwork/QTcpSocket>

QGC_LOGGING_CATEGORY(TCPLinkLog, "qgc.comms.tcplink")

TCPLink::TCPLink(SharedLinkConfigurationPtr &config, QObject *parent)
    : LinkInterface(config, parent)
    , _tcpConfig(qobject_cast<const TCPConfiguration*>(config.get()))
    , _socket(new QTcpSocket(this))
{
    // qCDebug(TCPLinkLog) << Q_FUNC_INFO << this;

    _socket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
    // _socket->setSocketOption(QAbstractSocket::TypeOfServiceOption, 32);

    (void) QObject::connect(_socket, &QTcpSocket::connected, this, &TCPLink::connected, Qt::AutoConnection);
    (void) QObject::connect(_socket, &QTcpSocket::disconnected, this, &TCPLink::disconnected, Qt::AutoConnection);

    (void) QObject::connect(_socket, &QTcpSocket::errorOccurred, this, [this](QTcpSocket::SocketError error) {
        qCWarning(TCPLinkLog) << "TCP Link Error:" << error << _socket->errorString();
        emit communicationError(QStringLiteral("TCP Link Error"), QStringLiteral("Link: %1, %2.").arg(_tcpConfig->name(), _socket->errorString()));
    }, Qt::AutoConnection);

#ifdef QT_DEBUG
    (void) QObject::connect(_socket, &QTcpSocket::stateChanged, this, [](QTcpSocket::SocketState state) {
        qCDebug(TCPLinkLog) << "TCP State Changed:" << state;
    }, Qt::AutoConnection);

    (void) QObject::connect(_socket, &QTcpSocket::hostFound, this, [this]() {
        qCDebug(TCPLinkLog) << "TCP Host Found";
    }, Qt::AutoConnection);
#endif
}

TCPLink::~TCPLink()
{
    TCPLink::disconnect();

    // qCDebug(TCPLinkLog) << Q_FUNC_INFO << this;
}

bool TCPLink::isConnected() const
{
    return ((_socket->state() != QAbstractSocket::ConnectedState) && (_socket->state() != QAbstractSocket::ConnectingState));
}

void TCPLink::disconnect()
{
    (void) QObject::disconnect(_socket, &QTcpSocket::readyRead, this, &TCPLink::_readBytes);
    _socket->disconnectFromHost();
}

bool TCPLink::_connect()
{
    (void) QObject::connect(_socket, &QTcpSocket::readyRead, this, &TCPLink::_readBytes, Qt::AutoConnection);

    if (isConnected()) {
        return true;
    }

    _socket->connectToHost(_tcpConfig->host(), _tcpConfig->port());

    if (!_socket->isValid()) {
        return false;
    }

    return true;
}

void TCPLink::_writeBytes(const QByteArray &bytes)
{
    if (!_socket->isValid()) {
        return;
    }

    static const QString title = QStringLiteral("TCP Link Write Error");
    static const QString error = QStringLiteral("Link %1: %2.");

    if (!isConnected()) {
        emit communicationError(title, error.arg(_tcpConfig->name(), QStringLiteral("Could Not Send Data - Link is Disconnected!")));
        return;
    }

    if (_socket->write(bytes) < 0) {
        emit communicationError(title, error.arg(_tcpConfig->name(), QStringLiteral("Could Not Send Data - Write Failed!")));
        return;
    }

    emit bytesSent(this, bytes);
}

void TCPLink::_readBytes()
{
    if (!_socket->isValid()) {
        return;
    }

    static const QString title = QStringLiteral("TCP Link Read Error");
    static const QString error = QStringLiteral("Link %1: %2.");

    if (!isConnected()) {
        emit communicationError(title, error.arg(_tcpConfig->name(), QStringLiteral("Could Not Read Data - link is Disconnected!")));
        return;
    }

    const qint64 byteCount = _socket->bytesAvailable();
    if (byteCount <= 0) {
        emit communicationError(title, error.arg(_tcpConfig->name(), QStringLiteral("Could Not Read Data - No Data Available!")));
        return;
    }

    QByteArray buffer(byteCount, Qt::Initialization::Uninitialized);
    if (_socket->read(buffer.data(), buffer.size()) < 0) {
        emit communicationError(title, error.arg(_tcpConfig->name(),  QStringLiteral("Could Not Read Data - Read Failed!")));
        return;
    }

    emit bytesReceived(this, buffer);
}

bool TCPLink::isSecureConnection()
{
    return QGCDeviceInfo::isNetworkWired();
}

/*===========================================================================*/

TCPConfiguration::TCPConfiguration(const QString &name, QObject *parent)
    : LinkConfiguration(name, parent)
{
    // qCDebug(TCPLinkLog) << Q_FUNC_INFO << this;
}

TCPConfiguration::TCPConfiguration(TCPConfiguration *copy, QObject *parent)
    : LinkConfiguration(copy, parent)
    , _host(copy->host())
    , _port(copy->port())
{
    // qCDebug(TCPLinkLog) << Q_FUNC_INFO << this;

    Q_CHECK_PTR(copy);

    LinkConfiguration::copyFrom(copy);
}

TCPConfiguration::~TCPConfiguration()
{
    // qCDebug(TCPLinkLog) << Q_FUNC_INFO << this;
}

void TCPConfiguration::copyFrom(LinkConfiguration *source)
{
    Q_CHECK_PTR(source);
    LinkConfiguration::copyFrom(source);

    const TCPConfiguration* const tcpSource = qobject_cast<const TCPConfiguration*>(source);
    Q_CHECK_PTR(tcpSource);

    setHost(tcpSource->host());
    setPort(tcpSource->port());
}

void TCPConfiguration::loadSettings(QSettings &settings, const QString &root)
{
    settings.beginGroup(root);

    setHost(settings.value(QStringLiteral("host"), host()).toString());
    setPort(static_cast<quint16>(settings.value(QStringLiteral("port"), port()).toUInt()));

    settings.endGroup();
}

void TCPConfiguration::saveSettings(QSettings &settings, const QString &root)
{
    settings.beginGroup(root);

    settings.setValue(QStringLiteral("host"), host());
    settings.setValue(QStringLiteral("port"), port());

    settings.endGroup();
}

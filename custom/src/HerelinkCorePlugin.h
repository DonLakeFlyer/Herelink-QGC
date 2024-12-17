#pragma once

#include "HerelinkOptions.h"

#include "QGCCorePlugin.h"

#include <QObject>

Q_DECLARE_LOGGING_CATEGORY(HerelinkCorePluginLog)

class HerelinkCorePlugin : public QGCCorePlugin
{
    Q_OBJECT

public:
    HerelinkCorePlugin(QObject* parent = nullptr);

    static QGCCorePlugin *instance();

    // Overrides from QGCCorePlugin
    void        init                            (void) override;
    QGCOptions* options                         (void) override { return qobject_cast<QGCOptions*>(_herelinkOptions); }
    bool        overrideSettingsGroupVisibility (const QString &name) const override;
    bool        adjustSettingMetaData           (const QString &settingsGroup, FactMetaData &metaData) const override;

private slots:
    void _activeVehicleChanged(Vehicle* activeVehicle);

private:
    HerelinkOptions* _herelinkOptions = nullptr;
};

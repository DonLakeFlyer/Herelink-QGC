#pragma once

#include "HerelinkOptions.h"

#include "QGCCorePlugin.h"
#include "QGCLoggingCategory.h"

#include <QObject>

Q_DECLARE_LOGGING_CATEGORY(HerelinkCorePluginLog)

class HerelinkCorePlugin : public QGCCorePlugin
{
    Q_OBJECT

public:
    HerelinkCorePlugin(QGCApplication* app, QGCToolbox* toolbox);

    // Overrides from QGCCorePlugin
    QGCOptions* options                         (void) override { return qobject_cast<QGCOptions*>(_herelinkOptions); }
    bool        overrideSettingsGroupVisibility (QString name) override;
    bool        adjustSettingMetaData           (const QString& settingsGroup, FactMetaData& metaData) override;

    // Overrides from QGCTool
    void setToolbox(QGCToolbox* toolbox) override;

private slots:
    void _activeVehicleChanged(Vehicle* activeVehicle);

private:
    HerelinkOptions* _herelinkOptions = nullptr;
};

#include "HerelinkCorePlugin.h"

#include "AutoConnectSettings.h"
#include "VideoSettings.h"
#include "AppSettings.h"
#include "QGCApplication.h"
#include "MultiVehicleManager.h"
#include "JoystickManager.h"
#include "Vehicle.h"
#include "QGCLoggingCategory.h"

#include <list>

QGC_LOGGING_CATEGORY(HerelinkCorePluginLog, "HerelinkCorePluginLog")

Q_APPLICATION_STATIC(HerelinkCorePlugin, _herelinkCorePluginInstance);

HerelinkCorePlugin::HerelinkCorePlugin(QObject* parent)
    : QGCCorePlugin(parent)
    , _herelinkOptions(new HerelinkOptions(this))
{

}

QGCCorePlugin *HerelinkCorePlugin::instance()
{
    return _herelinkCorePluginInstance();
}

void HerelinkCorePlugin::init()
{
    static bool initialized = false;

    if (!initialized) {
        initialized = true;
        QGCCorePlugin::init();
        connect(MultiVehicleManager::instance(), &MultiVehicleManager::activeVehicleChanged, this, &HerelinkCorePlugin::_activeVehicleChanged);
    }
}

bool HerelinkCorePlugin::overrideSettingsGroupVisibility(const QString &name)
{
    // Hide all AutoConnect settings
    return name != AutoConnectSettings::name;
}

bool HerelinkCorePlugin::adjustSettingMetaData(const QString& settingsGroup, FactMetaData& metaData)
{
    if (settingsGroup == AutoConnectSettings::settingsGroup) {
        // We have to adjust the Herelink UDP autoconnect settings for the AirLink
        if (metaData.name() == AutoConnectSettings::udpListenPortName) {
            metaData.setRawDefaultValue(14551);
        } else if (metaData.name() == AutoConnectSettings::udpTargetHostIPName) {
            metaData.setRawDefaultValue(QStringLiteral("127.0.0.1"));
        } else if (metaData.name() == AutoConnectSettings::udpTargetHostPortName) {
            metaData.setRawDefaultValue(15552);
        } else {
            // Disable all the other autoconnect types
            const std::list<const char *> disabledAndHiddenSettings = {
                AutoConnectSettings::autoConnectPixhawkName,
                AutoConnectSettings::autoConnectSiKRadioName,
                AutoConnectSettings::autoConnectRTKGPSName,
                AutoConnectSettings::autoConnectLibrePilotName,
                AutoConnectSettings::autoConnectNmeaPortName,
                AutoConnectSettings::autoConnectZeroConfName,
            };
            for (const char * disabledAndHiddenSetting : disabledAndHiddenSettings) {
                if (disabledAndHiddenSetting == metaData.name()) {
                    metaData.setRawDefaultValue(false);
                }
            }
        }
    } else if (settingsGroup == VideoSettings::settingsGroup) {
        if (metaData.name() == VideoSettings::rtspTimeoutName) {
            metaData.setRawDefaultValue(60);
        } else if (metaData.name() == VideoSettings::videoSourceName) {
            metaData.setRawDefaultValue(VideoSettings::videoSourceHerelinkAirUnit);
        }
    } else if (settingsGroup == AppSettings::settingsGroup) {
        if (metaData.name() == AppSettings::androidSaveToSDCardName) {
            metaData.setRawDefaultValue(true);
        }
    }

    return true; // Show all settings in ui
}

void HerelinkCorePlugin::_activeVehicleChanged(Vehicle* activeVehicle)
{
    if (activeVehicle) {
        QString herelinkButtonsJoystickName("gpio-keys");

        auto joystickManager = JoystickManager::instance();
        if (joystickManager->activeJoystickName() != herelinkButtonsJoystickName) {
            if (!joystickManager->setActiveJoystickName(herelinkButtonsJoystickName)) {
                qgcApp()->showAppMessage("Warning: Herelink buttton setup failed. Buttons will not work.");
                return;
            }           
        }
        activeVehicle->setJoystickEnabled(true);
    }
}

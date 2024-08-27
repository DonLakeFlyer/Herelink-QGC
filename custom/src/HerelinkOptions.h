#pragma once

#include "QGCOptions.h"

class HerelinkCorePlugin;

class HerelinkOptions : public QGCOptions
{
public:
    HerelinkOptions(HerelinkCorePlugin* plugin, QObject* parent = NULL);

    // QGCOptions overrides
    bool wifiReliableForCalibration () const override { return true; }
    bool showFirmwareUpgrade        () const override { return false; }
    bool multiVehicleEnabled        () const override { return false; }
    bool allowJoystickSelection     () const override { return false; }
};

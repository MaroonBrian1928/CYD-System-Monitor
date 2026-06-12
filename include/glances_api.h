#ifndef GLANCES_API_H
#define GLANCES_API_H

#include <ArduinoJson.h>

struct GlancesAPI
{
    static bool fetchData(const char *endpoint, StaticJsonDocument<4096> &doc);
    static void updateCPUData(StaticJsonDocument<4096> &doc);
    static void updateMemoryData(StaticJsonDocument<4096> &doc);
    static void updateContainerData();
};

void updateGlancesData();

#endif
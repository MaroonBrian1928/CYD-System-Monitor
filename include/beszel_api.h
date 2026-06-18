#ifndef BESZEL_API_H
#define BESZEL_API_H

#include <Arduino.h>
#include <ArduinoJson.h>

// Upper bound on the number of Beszel-monitored systems we render. Each one gets
// its own dashboard page; the array below is filled from the systems endpoint.
#define BESZEL_MAX_SYSTEMS 8

// One monitored host, distilled from a Beszel `systems` record's compact `info`
// block. Fields that a given agent doesn't report (temperature, GPU) are flagged
// so the UI can show "--" instead of a bogus zero.
struct BeszelSystem
{
    char id[20];   // PocketBase record id (used to match containers to a host)
    char name[28]; // display name, e.g. "Pop"
    char host[40]; // agent address, e.g. "192.168.1.50"
    float cpu;     // info.cpu  -- CPU usage %
    float mem;     // info.mp   -- memory usage %
    float disk;    // info.dp   -- disk usage %
    float temp;    // info.dt   -- temperature C (hasTemp == false when absent)
    float gpu;     // info.g    -- GPU usage % (hasGpu == false when absent)
    float bw;      // info.bb   -- network throughput, bytes/sec
    float load1;   // info.la[0]-- 1-minute load average
    int cores;     // info.t    -- CPU cores/threads
    unsigned long uptime; // info.u -- uptime, seconds
    bool up;       // status == "up"
    bool hasTemp;
    bool hasGpu;
    // Filled in less often from the system_stats collection (see fetchStats):
    // these aren't in the realtime `info` summary.
    float memTotalGB; // stats.m       -- total RAM, GB
    float vram;       // stats.g.0     -- VRAM used %, mu/mt*100
    bool hasMemTotal;
    bool hasVram;
};

extern BeszelSystem beszel_systems[BESZEL_MAX_SYSTEMS];
extern int beszel_system_count;

// Throttled (BESZEL_UPDATE_INTERVAL) refresh: authenticates if needed, pulls the
// systems list, (re)builds the per-system dashboard pages, and -- when the
// combined container page is showing -- pulls the container list.
void updateBeszelData();

#endif

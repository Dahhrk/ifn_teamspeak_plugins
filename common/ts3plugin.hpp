#pragma once

#ifdef _WIN32
#define PLUGINS_EXPORTDLL __declspec(dllexport)
#else
#define PLUGINS_EXPORTDLL __attribute__((visibility("default")))
#endif

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include "plugin_definitions.h"
#include "teamspeak/public_definitions.h"
#include "teamspeak/public_errors.h"
#include "teamspeak/public_rare_definitions.h"
#include "ts3_functions.h"

static struct TS3Functions ts3Functions;
static char* g_pluginID = NULL;

#define TS3_PLUGIN_IDENTITY(NAME, VERSION, AUTHOR, DESC, APIVER)                                \
    extern "C" {                                                                               \
    PLUGINS_EXPORTDLL const char* ts3plugin_name() { return NAME; }                            \
    PLUGINS_EXPORTDLL const char* ts3plugin_version() { return VERSION; }                      \
    PLUGINS_EXPORTDLL int ts3plugin_apiVersion() { return APIVER; }                            \
    PLUGINS_EXPORTDLL const char* ts3plugin_author() { return AUTHOR; }                        \
    PLUGINS_EXPORTDLL const char* ts3plugin_description() { return DESC; }                     \
    PLUGINS_EXPORTDLL void ts3plugin_setFunctionPointers(const struct TS3Functions funcs) {    \
        ts3Functions = funcs;                                                                  \
    }                                                                                          \
    PLUGINS_EXPORTDLL void ts3plugin_registerPluginID(const char* id) {                        \
        const size_t len = strlen(id) + 1;                                                     \
        g_pluginID = (char*)malloc(len);                                                       \
        memcpy(g_pluginID, id, len);                                                           \
    }                                                                                          \
    PLUGINS_EXPORTDLL void ts3plugin_freeMemory(void* data) { free(data); }                    \
    }

#define TS3_PLUGIN_LIFECYCLE_DEFAULT                                                           \
    extern "C" {                                                                               \
    PLUGINS_EXPORTDLL int ts3plugin_init() { return 0; }                                       \
    PLUGINS_EXPORTDLL void ts3plugin_shutdown() { ts3FreePluginID(); }                         \
    }

static void ts3FreePluginID() {
    if (g_pluginID) {
        free(g_pluginID);
        g_pluginID = NULL;
    }
}

static struct PluginMenuItem* ts3MakeMenuItem(enum PluginMenuType type, int id, const char* text) {
    struct PluginMenuItem* item = (struct PluginMenuItem*)malloc(sizeof(struct PluginMenuItem));
    item->type = type;
    item->id = id;
    strncpy(item->text, text, PLUGIN_MENU_BUFSZ - 1);
    item->text[PLUGIN_MENU_BUFSZ - 1] = '\0';
    item->icon[0] = '\0';
    return item;
}

static std::string ts3Sanitize(const char* s) {
    std::string out = s ? s : "";
    for (auto& c : out) {
        if (c == '[') c = '(';
        else if (c == ']') c = ')';
    }
    return out;
}

static std::vector<anyID> ts3ClientList(uint64 schid) {
    std::vector<anyID> out;
    anyID* ids = NULL;
    if (ts3Functions.getClientList(schid, &ids) == ERROR_ok && ids) {
        for (size_t i = 0; ids[i] != 0; ++i) out.push_back(ids[i]);
        ts3Functions.freeMemory(ids);
    }
    return out;
}

static std::string ts3ClientString(uint64 schid, anyID clid, size_t flag) {
    char* v = NULL;
    std::string out;
    if (ts3Functions.getClientVariableAsString(schid, clid, flag, &v) == ERROR_ok && v) {
        out = v;
        ts3Functions.freeMemory(v);
    }
    return out;
}

static bool ts3FindChannelByName(uint64 schid, const char* needle, uint64* outCid) {
    uint64* channels = NULL;
    if (ts3Functions.getChannelList(schid, &channels) != ERROR_ok || !channels) return false;
    bool found = false;
    for (size_t i = 0; channels[i] != 0 && !found; ++i) {
        char* name = NULL;
        if (ts3Functions.getChannelVariableAsString(schid, channels[i], CHANNEL_NAME, &name) == ERROR_ok && name) {
            std::string lower = name;
            std::string n = needle;
            for (auto& c : lower) c = (char)tolower((unsigned char)c);
            for (auto& c : n) c = (char)tolower((unsigned char)c);
            if (lower.find(n) != std::string::npos) {
                *outCid = channels[i];
                found = true;
            }
            ts3Functions.freeMemory(name);
        }
    }
    ts3Functions.freeMemory(channels);
    return found;
}

static anyID ts3SelfClientID(uint64 schid) {
    anyID id = 0;
    ts3Functions.getClientID(schid, &id);
    return id;
}

#ifdef _WIN32
#define PLUGINS_EXPORTDLL __declspec(dllexport)
#else
#define PLUGINS_EXPORTDLL __attribute__((visibility("default")))
#endif

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "plugin_definitions.h"
#include "teamspeak/public_definitions.h"
#include "teamspeak/public_errors.h"
#include "ts3_functions.h"

static struct TS3Functions ts3Functions;
static char* pluginID = NULL;

#define PLUGIN_NAME "IFN Group Finder"
#define PLUGIN_VERSION "1.0"
#define PLUGIN_AUTHOR "Dahhrk"
#define PLUGIN_DESCRIPTION "Find users by server group and print clickable PM links."
#define PLUGIN_API_VERSION 23
#define RETURN_CODE "gf"
#define MENU_ID_REFRESH 0
#define MENU_ID_GROUP_BASE 1

struct Group {
    uint64 sgid;
    std::string name;
};

struct Member {
    uint64 dbid;
    std::string uid;
    std::string name;
};

struct Pending {
    uint64 sgid;
    std::string groupName;
    std::vector<Member> members;
};

static std::map<uint64, std::vector<Group>> g_groups;
static std::map<uint64, Pending> g_pending;

static std::string sanitize(const char* s) {
    std::string out = s ? s : "";
    std::replace(out.begin(), out.end(), '[', '(');
    std::replace(out.begin(), out.end(), ']', ')');
    return out;
}

static void requestGroupList(uint64 schid) {
    g_groups.erase(schid);
    ts3Functions.requestServerGroupList(schid, "");
}

static void flushPending(uint64 schid) {
    auto pit = g_pending.find(schid);
    if (pit == g_pending.end()) return;
    Pending p = std::move(pit->second);
    g_pending.erase(pit);

    std::map<std::string, anyID> onlineByUid;
    anyID* ids = NULL;
    if (ts3Functions.getClientList(schid, &ids) == ERROR_ok && ids) {
        for (size_t i = 0; ids[i] != 0; ++i) {
            char* uid = NULL;
            if (ts3Functions.getClientVariableAsString(schid, ids[i], CLIENT_UNIQUE_IDENTIFIER, &uid) == ERROR_ok && uid) {
                onlineByUid[uid] = ids[i];
                ts3Functions.freeMemory(uid);
            }
        }
        ts3Functions.freeMemory(ids);
    }

    size_t online = 0;
    for (const auto& m : p.members)
        if (onlineByUid.count(m.uid)) ++online;

    std::string out = "[b]" + sanitize(p.groupName.c_str()) + "[/b] - " +
                      std::to_string(p.members.size()) + " members, " +
                      std::to_string(online) + " online\n";
    for (const auto& m : p.members) {
        std::string name = sanitize(m.name.c_str());
        auto it = onlineByUid.find(m.uid);
        if (it != onlineByUid.end()) {
            out += "[url=client://" + std::to_string(it->second) + "/" + m.uid + "~" + name + "]" + name + "[/url]\n";
        } else {
            out += name + " (offline)\n";
        }
    }
    ts3Functions.printMessage(schid, out.c_str(), PLUGIN_MESSAGE_TARGET_SERVER);
}

extern "C" {

PLUGINS_EXPORTDLL const char* ts3plugin_name() { return PLUGIN_NAME; }
PLUGINS_EXPORTDLL const char* ts3plugin_version() { return PLUGIN_VERSION; }
PLUGINS_EXPORTDLL int ts3plugin_apiVersion() { return PLUGIN_API_VERSION; }
PLUGINS_EXPORTDLL const char* ts3plugin_author() { return PLUGIN_AUTHOR; }
PLUGINS_EXPORTDLL const char* ts3plugin_description() { return PLUGIN_DESCRIPTION; }
PLUGINS_EXPORTDLL void ts3plugin_setFunctionPointers(const struct TS3Functions funcs) { ts3Functions = funcs; }
PLUGINS_EXPORTDLL int ts3plugin_init() { return 0; }

PLUGINS_EXPORTDLL void ts3plugin_shutdown() {
    if (pluginID) {
        free(pluginID);
        pluginID = NULL;
    }
}

PLUGINS_EXPORTDLL void ts3plugin_registerPluginID(const char* id) {
    const size_t len = strlen(id) + 1;
    pluginID = (char*)malloc(len);
    memcpy(pluginID, id, len);
}

PLUGINS_EXPORTDLL void ts3plugin_freeMemory(void* data) { free(data); }

PLUGINS_EXPORTDLL void ts3plugin_initMenus(struct PluginMenuItem*** menuItems, char** menuIcon) {
    uint64 schid = ts3Functions.getCurrentServerConnectionHandlerID();
    auto git = g_groups.find(schid);
    const size_t groupCount = git != g_groups.end() ? git->second.size() : 0;

    const size_t itemCount = groupCount + 1;
    *menuItems = (struct PluginMenuItem**)malloc((itemCount + 1) * sizeof(struct PluginMenuItem*));

    struct PluginMenuItem* refresh = (struct PluginMenuItem*)malloc(sizeof(struct PluginMenuItem));
    refresh->type = PLUGIN_MENU_TYPE_GLOBAL;
    refresh->id = MENU_ID_REFRESH;
    strncpy(refresh->text, "Refresh group list", PLUGIN_MENU_BUFSZ - 1);
    refresh->text[PLUGIN_MENU_BUFSZ - 1] = '\0';
    refresh->icon[0] = '\0';
    (*menuItems)[0] = refresh;

    if (git != g_groups.end()) {
        for (size_t i = 0; i < groupCount; ++i) {
            struct PluginMenuItem* item = (struct PluginMenuItem*)malloc(sizeof(struct PluginMenuItem));
            item->type = PLUGIN_MENU_TYPE_GLOBAL;
            item->id = MENU_ID_GROUP_BASE + (int)i;
            strncpy(item->text, git->second[i].name.c_str(), PLUGIN_MENU_BUFSZ - 1);
            item->text[PLUGIN_MENU_BUFSZ - 1] = '\0';
            item->icon[0] = '\0';
            (*menuItems)[i + 1] = item;
        }
    }
    (*menuItems)[itemCount] = NULL;

    *menuIcon = NULL;
}

PLUGINS_EXPORTDLL void ts3plugin_onMenuItemEvent(uint64 schid, enum PluginMenuType type, int menuItemID, uint64 selectedItemID) {
    if (type != PLUGIN_MENU_TYPE_GLOBAL) return;

    if (menuItemID == MENU_ID_REFRESH) {
        requestGroupList(schid);
        return;
    }

    auto git = g_groups.find(schid);
    if (git == g_groups.end()) return;
    const size_t idx = (size_t)(menuItemID - MENU_ID_GROUP_BASE);
    if (idx >= git->second.size()) return;

    const Group& g = git->second[idx];
    Pending& p = g_pending[schid];
    p.sgid = g.sgid;
    p.groupName = g.name;
    p.members.clear();
    ts3Functions.requestServerGroupClientList(schid, g.sgid, 1, RETURN_CODE);
}

PLUGINS_EXPORTDLL void ts3plugin_onConnectStatusChangeEvent(uint64 schid, int newStatus, unsigned int errorNumber) {
    if (newStatus == STATUS_CONNECTION_ESTABLISHED) {
        ts3Functions.requestChannelSubscribeAll(schid, "");
        requestGroupList(schid);
    } else if (newStatus == STATUS_DISCONNECTED) {
        g_groups.erase(schid);
        g_pending.erase(schid);
    }
}

PLUGINS_EXPORTDLL void ts3plugin_onServerGroupListEvent(uint64 schid, uint64 serverGroupID, const char* name, int type, int iconID, int saveDB) {
    if (type != 1) return;
    g_groups[schid].push_back({serverGroupID, name ? name : ""});
}

PLUGINS_EXPORTDLL void ts3plugin_onServerGroupListFinishedEvent(uint64 schid) {
    auto& groups = g_groups[schid];
    std::sort(groups.begin(), groups.end(), [](const Group& a, const Group& b) { return a.name < b.name; });
}

PLUGINS_EXPORTDLL void ts3plugin_onServerGroupClientListEvent(uint64 schid, uint64 serverGroupID, uint64 clientDatabaseID, const char* clientNameIdentifier, const char* clientUniqueID) {
    auto pit = g_pending.find(schid);
    if (pit == g_pending.end() || pit->second.sgid != serverGroupID) return;
    pit->second.members.push_back({clientDatabaseID,
                                   clientUniqueID ? clientUniqueID : "",
                                   clientNameIdentifier ? clientNameIdentifier : ""});
}

PLUGINS_EXPORTDLL int ts3plugin_onServerErrorEvent(uint64 schid, const char* errorMessage, unsigned int error, const char* returnCode, const char* extraMessage) {
    if (!returnCode || strcmp(returnCode, RETURN_CODE) != 0) return 0;
    if (error != ERROR_ok) {
        std::string msg = std::string("Group Finder: ") + (errorMessage ? errorMessage : "request failed");
        ts3Functions.printMessage(schid, msg.c_str(), PLUGIN_MESSAGE_TARGET_SERVER);
        g_pending.erase(schid);
        return 1;
    }
    flushPending(schid);
    return 1;
}

}

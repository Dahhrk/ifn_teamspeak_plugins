#include <algorithm>
#include <map>
#include <vector>

#include "ts3plugin.hpp"

#define RETURN_CODE "gm"
#define MENU_ID_REFRESH 0
#define MENU_ID_ANNOUNCE 1
#define MENU_ID_POKE_EVENT 2
#define MENU_ID_POKE_MEETING 3
#define MENU_ID_TALK_GRANT 4
#define MENU_ID_TALK_REVOKE 5
#define MENU_ID_PULL_BASE 100

struct Group {
    uint64 sgid;
    std::string name;
};

static std::map<uint64, std::vector<Group>> g_groups;

TS3_PLUGIN_IDENTITY("IFN GM Tools", "1.0", "Dahhrk",
                    "Event tools: pull groups to your channel, channel pokes, channel-wide talk power.", 23)
TS3_PLUGIN_LIFECYCLE_DEFAULT

static std::vector<anyID> channelClients(uint64 schid, uint64 cid) {
    std::vector<anyID> out;
    anyID* ids = NULL;
    if (ts3Functions.getChannelClientList(schid, cid, &ids) == ERROR_ok && ids) {
        for (size_t i = 0; ids[i] != 0; ++i) out.push_back(ids[i]);
        ts3Functions.freeMemory(ids);
    }
    return out;
}

static bool clientInGroup(uint64 schid, anyID clid, uint64 sgid) {
    std::string csv = ts3ClientString(schid, clid, CLIENT_SERVERGROUPS);
    std::string needle = std::to_string(sgid);
    size_t pos = 0;
    while (pos <= csv.size()) {
        size_t comma = csv.find(',', pos);
        std::string tok = csv.substr(pos, comma == std::string::npos ? comma : comma - pos);
        if (tok == needle) return true;
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return false;
}

static void pullGroup(uint64 schid, const Group& g) {
    uint64 myCid = 0;
    if (ts3Functions.getChannelOfClient(schid, ts3SelfClientID(schid), &myCid) != ERROR_ok) return;

    size_t moved = 0;
    for (anyID clid : ts3ClientList(schid)) {
        if (clid == ts3SelfClientID(schid)) continue;
        if (!clientInGroup(schid, clid, g.sgid)) continue;
        uint64 theirCid = 0;
        ts3Functions.getChannelOfClient(schid, clid, &theirCid);
        if (theirCid == myCid) continue;
        ts3Functions.requestClientMove(schid, clid, myCid, "", RETURN_CODE);
        ++moved;
    }
    std::string msg = "GM Tools: pulled " + std::to_string(moved) + " from " + ts3Sanitize(g.name.c_str());
    ts3Functions.printMessage(schid, msg.c_str(), PLUGIN_MESSAGE_TARGET_SERVER);
}

static void pokeChannel(uint64 schid, const char* text) {
    uint64 myCid = 0;
    if (ts3Functions.getChannelOfClient(schid, ts3SelfClientID(schid), &myCid) != ERROR_ok) return;
    anyID self = ts3SelfClientID(schid);
    size_t poked = 0;
    for (anyID clid : channelClients(schid, myCid)) {
        if (clid == self) continue;
        ts3Functions.requestClientPoke(schid, clid, text, RETURN_CODE);
        ++poked;
    }
    std::string msg = "GM Tools: poked " + std::to_string(poked) + " in channel";
    ts3Functions.printMessage(schid, msg.c_str(), PLUGIN_MESSAGE_TARGET_SERVER);
}

static void setChannelTalker(uint64 schid, int talker) {
    uint64 myCid = 0;
    if (ts3Functions.getChannelOfClient(schid, ts3SelfClientID(schid), &myCid) != ERROR_ok) return;
    anyID self = ts3SelfClientID(schid);
    size_t changed = 0;
    for (anyID clid : channelClients(schid, myCid)) {
        if (clid == self) continue;
        ts3Functions.requestClientSetIsTalker(schid, clid, talker, RETURN_CODE);
        ++changed;
    }
    std::string msg = "GM Tools: talk power " + std::string(talker ? "granted" : "revoked") +
                      " for " + std::to_string(changed) + " in channel";
    ts3Functions.printMessage(schid, msg.c_str(), PLUGIN_MESSAGE_TARGET_SERVER);
}

extern "C" {

PLUGINS_EXPORTDLL void ts3plugin_initMenus(struct PluginMenuItem*** menuItems, char** menuIcon) {
    uint64 schid = ts3Functions.getCurrentServerConnectionHandlerID();
    auto git = g_groups.find(schid);
    const size_t groupCount = git != g_groups.end() ? git->second.size() : 0;

    static const struct {
        int id;
        const char* text;
    } fixed[] = {
        {MENU_ID_ANNOUNCE, "Server announce: event starting"},
        {MENU_ID_POKE_EVENT, "Poke my channel: event starting"},
        {MENU_ID_POKE_MEETING, "Poke my channel: meeting in 5 min"},
        {MENU_ID_TALK_GRANT, "Grant talk power to my channel"},
        {MENU_ID_TALK_REVOKE, "Revoke talk power from my channel"},
        {MENU_ID_REFRESH, "Refresh group list"},
    };
    const size_t fixedCount = sizeof(fixed) / sizeof(fixed[0]);
    const size_t itemCount = fixedCount + groupCount;

    *menuItems = (struct PluginMenuItem**)malloc((itemCount + 1) * sizeof(struct PluginMenuItem*));
    for (size_t i = 0; i < fixedCount; ++i)
        (*menuItems)[i] = ts3MakeMenuItem(PLUGIN_MENU_TYPE_GLOBAL, fixed[i].id, fixed[i].text);
    if (git != g_groups.end()) {
        for (size_t i = 0; i < groupCount; ++i) {
            std::string label = "Pull " + git->second[i].name;
            (*menuItems)[fixedCount + i] = ts3MakeMenuItem(PLUGIN_MENU_TYPE_GLOBAL,
                MENU_ID_PULL_BASE + (int)i, label.c_str());
        }
    }
    (*menuItems)[itemCount] = NULL;
    *menuIcon = NULL;
}

PLUGINS_EXPORTDLL void ts3plugin_onMenuItemEvent(uint64 schid, enum PluginMenuType type, int menuItemID, uint64 selectedItemID) {
    if (type != PLUGIN_MENU_TYPE_GLOBAL) return;

    switch (menuItemID) {
        case MENU_ID_ANNOUNCE:
            ts3Functions.requestSendServerTextMsg(schid, "Event starting soon - check the event channels.", RETURN_CODE);
            return;
        case MENU_ID_POKE_EVENT:
            pokeChannel(schid, "Event starting - get ready.");
            return;
        case MENU_ID_POKE_MEETING:
            pokeChannel(schid, "Meeting in 5 minutes.");
            return;
        case MENU_ID_TALK_GRANT:
            setChannelTalker(schid, 1);
            return;
        case MENU_ID_TALK_REVOKE:
            setChannelTalker(schid, 0);
            return;
        case MENU_ID_REFRESH:
            g_groups.erase(schid);
            ts3Functions.requestServerGroupList(schid, "");
            return;
    }

    auto git = g_groups.find(schid);
    if (git == g_groups.end()) return;
    const size_t idx = (size_t)(menuItemID - MENU_ID_PULL_BASE);
    if (idx < git->second.size()) pullGroup(schid, git->second[idx]);
}

PLUGINS_EXPORTDLL void ts3plugin_onConnectStatusChangeEvent(uint64 schid, int newStatus, unsigned int errorNumber) {
    if (newStatus == STATUS_CONNECTION_ESTABLISHED) {
        ts3Functions.requestChannelSubscribeAll(schid, "");
        ts3Functions.requestServerGroupList(schid, "");
    } else if (newStatus == STATUS_DISCONNECTED) {
        g_groups.erase(schid);
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

PLUGINS_EXPORTDLL int ts3plugin_onServerErrorEvent(uint64 schid, const char* errorMessage, unsigned int error, const char* returnCode, const char* extraMessage) {
    if (!returnCode || strcmp(returnCode, RETURN_CODE) != 0) return 0;
    if (error != ERROR_ok) {
        std::string msg = std::string("GM Tools: ") + (errorMessage ? errorMessage : "request failed");
        ts3Functions.printMessage(schid, msg.c_str(), PLUGIN_MESSAGE_TARGET_SERVER);
        return 1;
    }
    return 1;
}

}

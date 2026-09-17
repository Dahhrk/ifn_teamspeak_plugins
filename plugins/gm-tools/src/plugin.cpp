#include <algorithm>
#include <chrono>
#include <map>
#include <set>
#include <vector>

#include "ts3plugin.hpp"

#define RETURN_CODE "gm"
#define MENU_ID_REFRESH 0
#define MENU_ID_ANNOUNCE 1
#define MENU_ID_POKE_EVENT 2
#define MENU_ID_POKE_MEETING 3
#define MENU_ID_TALK_GRANT 4
#define MENU_ID_TALK_REVOKE 5
#define MENU_ID_RADAR 6
#define MENU_ID_WATCH 7
#define MENU_ID_PULL_BASE 100

static const char* WATCH_NEEDLES[] = {"event"};

struct Group {
    uint64 sgid;
    std::string name;
};

static std::map<uint64, std::vector<Group>> g_groups;
static std::map<uint64, std::set<uint64>> g_gmSgids;
static std::map<uint64, std::map<std::string, std::string>> g_gmOnline;
static std::map<uint64, bool> g_radarOn;
static std::map<uint64, std::chrono::steady_clock::time_point> g_radarQuiet;
static std::map<uint64, bool> g_watchOn;
static std::map<uint64, std::map<std::string, std::chrono::steady_clock::time_point>> g_watchLast;

TS3_PLUGIN_IDENTITY("IFN GM Tools", "1.3", "Dahhrk",
                    "Event tools: pull groups, channel pokes, channel-wide talk power, GM radar, event-channel watch.", 23)
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

static void pullGroup(uint64 schid, const Group& g) {
    anyID self = ts3SelfClientID(schid);
    uint64 myCid = 0;
    if (ts3Functions.getChannelOfClient(schid, self, &myCid) != ERROR_ok) return;

    size_t moved = 0;
    for (anyID clid : ts3ClientList(schid)) {
        if (clid == self) continue;
        if (!ts3ClientGroupSet(schid, clid).count(g.sgid)) continue;
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
    anyID self = ts3SelfClientID(schid);
    uint64 myCid = 0;
    if (ts3Functions.getChannelOfClient(schid, self, &myCid) != ERROR_ok) return;
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
    anyID self = ts3SelfClientID(schid);
    uint64 myCid = 0;
    if (ts3Functions.getChannelOfClient(schid, self, &myCid) != ERROR_ok) return;
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

static bool channelWatched(const std::string& channelNameLower) {
    for (auto* n : WATCH_NEEDLES)
        if (channelNameLower.find(n) != std::string::npos) return true;
    return false;
}

static void watchCheck(uint64 schid, anyID clientID, uint64 newChannelID) {
    auto wit = g_watchOn.find(schid);
    if (wit == g_watchOn.end() || !wit->second) return;
    std::string cname = ts3ChannelName(schid, newChannelID);
    if (cname.empty()) return;
    std::string lower = cname;
    for (auto& c : lower) c = (char)tolower((unsigned char)c);
    if (!channelWatched(lower)) return;

    std::string uid = ts3ClientString(schid, clientID, CLIENT_UNIQUE_IDENTIFIER);
    if (uid.empty()) return;
    auto now = std::chrono::steady_clock::now();
    auto& last = g_watchLast[schid][uid];
    if (now - last < std::chrono::seconds(60)) return;
    last = now;
    std::string name = ts3ClientString(schid, clientID, CLIENT_NICKNAME);
    std::string msg = "Watch: " + ts3Sanitize(name.c_str()) + " joined " + ts3Sanitize(cname.c_str());
    ts3Functions.printMessage(schid, msg.c_str(), PLUGIN_MESSAGE_TARGET_SERVER);
}

static bool isGm(uint64 schid, anyID clid) {
    auto sit = g_gmSgids.find(schid);
    if (sit == g_gmSgids.end()) return false;
    std::set<uint64> groups = ts3ClientGroupSet(schid, clid);
    for (uint64 sgid : sit->second)
        if (groups.count(sgid)) return true;
    return false;
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
        {MENU_ID_RADAR, "Toggle GM radar"},
        {MENU_ID_WATCH, "Toggle event-channel watch"},
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
        case MENU_ID_RADAR: {
            bool on = !g_radarOn[schid];
            g_radarOn[schid] = on;
            std::string msg = std::string("GM Tools: GM radar ") + (on ? "on" : "off");
            ts3Functions.printMessage(schid, msg.c_str(), PLUGIN_MESSAGE_TARGET_SERVER);
            return;
        }
        case MENU_ID_WATCH: {
            bool on = !g_watchOn[schid];
            g_watchOn[schid] = on;
            std::string msg = std::string("GM Tools: event-channel watch ") + (on ? "on" : "off");
            ts3Functions.printMessage(schid, msg.c_str(), PLUGIN_MESSAGE_TARGET_SERVER);
            return;
        }
        case MENU_ID_REFRESH:
            g_groups.erase(schid);
            g_gmSgids.erase(schid);
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
        g_radarOn[schid] = true;
        g_radarQuiet[schid] = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        g_watchOn[schid] = true;
    } else if (newStatus == STATUS_DISCONNECTED) {
        g_groups.erase(schid);
        g_gmSgids.erase(schid);
        g_gmOnline.erase(schid);
        g_radarOn.erase(schid);
        g_radarQuiet.erase(schid);
        g_watchOn.erase(schid);
        g_watchLast.erase(schid);
    }
}

PLUGINS_EXPORTDLL void ts3plugin_onClientMoveEvent(uint64 schid, anyID clientID, uint64 oldChannelID, uint64 newChannelID, int visibility, const char* moveMessage) {
    if (clientID == ts3SelfClientID(schid)) return;

    auto rit = g_radarOn.find(schid);
    if (rit != g_radarOn.end() && rit->second) {
        if (visibility == ENTER_VISIBILITY && isGm(schid, clientID)) {
            std::string uid = ts3ClientString(schid, clientID, CLIENT_UNIQUE_IDENTIFIER);
            if (!g_gmOnline[schid].count(uid)) {
                std::string name = ts3ClientString(schid, clientID, CLIENT_NICKNAME);
                g_gmOnline[schid][uid] = name;
                auto qit = g_radarQuiet.find(schid);
                bool quiet = qit != g_radarQuiet.end() &&
                             std::chrono::steady_clock::now() < qit->second;
                if (!quiet) {
                    std::string msg = "GM radar: " + ts3Sanitize(name.c_str()) + " connected";
                    ts3Functions.printMessage(schid, msg.c_str(), PLUGIN_MESSAGE_TARGET_SERVER);
                }
            }
        } else if (visibility == LEAVE_VISIBILITY) {
            std::string uid = ts3ClientString(schid, clientID, CLIENT_UNIQUE_IDENTIFIER);
            auto it = g_gmOnline[schid].find(uid);
            if (it != g_gmOnline[schid].end()) {
                std::string msg = "GM radar: " + ts3Sanitize(it->second.c_str()) + " disconnected";
                g_gmOnline[schid].erase(it);
                ts3Functions.printMessage(schid, msg.c_str(), PLUGIN_MESSAGE_TARGET_SERVER);
            }
        }
    }

    if (newChannelID != 0 && newChannelID != oldChannelID)
        watchCheck(schid, clientID, newChannelID);
}

PLUGINS_EXPORTDLL void ts3plugin_onServerGroupListEvent(uint64 schid, uint64 serverGroupID, const char* name, int type, int iconID, int saveDB) {
    if (type != 1) return;
    g_groups[schid].push_back({serverGroupID, name ? name : ""});
    if (name && strncmp(name, "Game Master", 11) == 0)
        g_gmSgids[schid].insert(serverGroupID);
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

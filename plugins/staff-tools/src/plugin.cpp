#include <algorithm>
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include "ts3plugin.hpp"

#define RETURN_CODE "st"

enum MenuId {
    MENU_PULL = 1,
    MENU_JAIL,
    MENU_TALK_GRANT,
    MENU_TALK_REVOKE,
    MENU_POKE_STAFF,
    MENU_POKE_RULES,
    MENU_KICK_CHANNEL,
    MENU_KICK_SERVER,
    MENU_BAN_1H,
    MENU_BAN_24H,
    MENU_JAIL_5M,
    MENU_JAIL_15M,
    MENU_JAIL_30M,
    MENU_JAIL_60M,
    MENU_RELEASE,
};

struct JailRecord {
    std::string uid;
    uint64 dbid;
    uint64 stickySgid;
    std::string name;
    uint64 returnCid;
    bool timed;
    std::chrono::steady_clock::time_point releaseAt;
};

static std::map<uint64, std::vector<JailRecord>> g_jailed;
static std::map<uint64, uint64> g_stickySgid;
static std::mutex g_jailMutex;
static std::thread g_worker;
static std::atomic<bool> g_running{false};

TS3_PLUGIN_IDENTITY("IFN Staff Tools", "1.1", "Dahhrk",
                    "Right-click client actions: pull, timed jail, talk power, pokes, kicks, bans.", 23)

static void printError(uint64 schid, const char* msg) {
    ts3Functions.printMessage(schid, msg, PLUGIN_MESSAGE_TARGET_SERVER);
}

static anyID onlineClidByUid(uint64 schid, const std::string& uid) {
    for (anyID clid : ts3ClientList(schid))
        if (ts3ClientString(schid, clid, CLIENT_UNIQUE_IDENTIFIER) == uid)
            return clid;
    return 0;
}

static void applySticky(uint64 schid, const JailRecord& rec) {
    if (rec.stickySgid && rec.dbid)
        ts3Functions.requestServerGroupAddClient(schid, rec.stickySgid, rec.dbid, RETURN_CODE);
}

static void releaseJail(uint64 schid, const JailRecord& rec) {
    anyID clid = onlineClidByUid(schid, rec.uid);
    if (clid && rec.returnCid)
        ts3Functions.requestClientMove(schid, clid, rec.returnCid, "", RETURN_CODE);
    if (rec.stickySgid && rec.dbid)
        ts3Functions.requestServerGroupDelClient(schid, rec.stickySgid, rec.dbid, RETURN_CODE);
    std::string msg = "Jail: " + ts3Sanitize(rec.name.c_str()) + " released";
    if (!clid) msg += " (was offline - Sticky removed)";
    printError(schid, msg.c_str());
}

static void jailWorker() {
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        if (!g_running.load()) break;
        auto now = std::chrono::steady_clock::now();
        std::map<uint64, std::vector<JailRecord>> due;
        {
            std::lock_guard<std::mutex> lock(g_jailMutex);
            for (auto& [schid, records] : g_jailed) {
                for (size_t i = records.size(); i-- > 0;) {
                    if (records[i].timed && records[i].releaseAt <= now) {
                        due[schid].push_back(records[i]);
                        records.erase(records.begin() + i);
                    }
                }
            }
        }
        for (auto& [schid, records] : due)
            for (auto& rec : records) releaseJail(schid, rec);
    }
}

static void jailClient(uint64 schid, anyID target, int minutes) {
    uint64 jail = 0;
    if (!ts3FindChannelByName(schid, "jail", &jail)) {
        printError(schid, "Staff Tools: no channel containing 'jail' on this server.");
        return;
    }

    JailRecord rec;
    rec.uid = ts3ClientString(schid, target, CLIENT_UNIQUE_IDENTIFIER);
    rec.name = ts3ClientString(schid, target, CLIENT_NICKNAME);
    uint64 dbid = 0;
    ts3Functions.getClientVariableAsUInt64(schid, target, CLIENT_DATABASE_ID, &dbid);
    rec.dbid = dbid;
    rec.stickySgid = 0;
    {
        auto sit = g_stickySgid.find(schid);
        if (sit != g_stickySgid.end()) rec.stickySgid = sit->second;
    }
    uint64 cid = 0;
    ts3Functions.getChannelOfClient(schid, target, &cid);
    rec.returnCid = cid;
    rec.timed = minutes > 0;
    if (rec.timed)
        rec.releaseAt = std::chrono::steady_clock::now() + std::chrono::minutes(minutes);

    {
        std::lock_guard<std::mutex> lock(g_jailMutex);
        for (auto& r : g_jailed[schid])
            if (r.uid == rec.uid) {
                printError(schid, "Staff Tools: target already jailed.");
                return;
            }
        g_jailed[schid].push_back(rec);
    }

    ts3Functions.requestClientMove(schid, target, jail, "", RETURN_CODE);
    applySticky(schid, rec);

    std::string msg = "Jail: " + ts3Sanitize(rec.name.c_str());
    if (minutes > 0)
        msg += " jailed for " + std::to_string(minutes) + " min - auto-release set";
    else
        msg += " jailed - use Release to free";
    printError(schid, msg.c_str());
}

static void releaseClient(uint64 schid, anyID target) {
    std::string uid = ts3ClientString(schid, target, CLIENT_UNIQUE_IDENTIFIER);
    JailRecord rec;
    {
        std::lock_guard<std::mutex> lock(g_jailMutex);
        auto it = g_jailed.find(schid);
        if (it == g_jailed.end()) return;
        auto rit = std::find_if(it->second.begin(), it->second.end(),
                                [&](const JailRecord& r) { return r.uid == uid; });
        if (rit == it->second.end()) {
            printError(schid, "Staff Tools: target is not jailed.");
            return;
        }
        rec = *rit;
        it->second.erase(rit);
    }
    releaseJail(schid, rec);
}

extern "C" {

PLUGINS_EXPORTDLL int ts3plugin_init() {
    g_running.store(true);
    g_worker = std::thread(jailWorker);
    return 0;
}

PLUGINS_EXPORTDLL void ts3plugin_shutdown() {
    g_running.store(false);
    if (g_worker.joinable()) g_worker.join();
    ts3FreePluginID();
}

PLUGINS_EXPORTDLL void ts3plugin_initMenus(struct PluginMenuItem*** menuItems, char** menuIcon) {
    static const struct {
        int id;
        const char* text;
    } items[] = {
        {MENU_PULL, "Pull to my channel"},
        {MENU_JAIL, "Send to Jail"},
        {MENU_JAIL_5M, "Jail 5 min"},
        {MENU_JAIL_15M, "Jail 15 min"},
        {MENU_JAIL_30M, "Jail 30 min"},
        {MENU_JAIL_60M, "Jail 60 min"},
        {MENU_RELEASE, "Release from jail"},
        {MENU_TALK_GRANT, "Grant talk power"},
        {MENU_TALK_REVOKE, "Revoke talk power"},
        {MENU_POKE_STAFF, "Poke: join staff channel"},
        {MENU_POKE_RULES, "Poke: rules reminder"},
        {MENU_KICK_CHANNEL, "Kick from channel"},
        {MENU_KICK_SERVER, "Kick from server"},
        {MENU_BAN_1H, "Ban 1 hour"},
        {MENU_BAN_24H, "Ban 24 hours"},
    };
    const size_t count = sizeof(items) / sizeof(items[0]);
    *menuItems = (struct PluginMenuItem**)malloc((count + 1) * sizeof(struct PluginMenuItem*));
    for (size_t i = 0; i < count; ++i)
        (*menuItems)[i] = ts3MakeMenuItem(PLUGIN_MENU_TYPE_CLIENT, items[i].id, items[i].text);
    (*menuItems)[count] = NULL;
    *menuIcon = NULL;
}

PLUGINS_EXPORTDLL void ts3plugin_onMenuItemEvent(uint64 schid, enum PluginMenuType type, int menuItemID, uint64 selectedItemID) {
    if (type != PLUGIN_MENU_TYPE_CLIENT) return;
    anyID target = (anyID)selectedItemID;
    if (target == ts3SelfClientID(schid)) {
        printError(schid, "Staff Tools: cannot target yourself.");
        return;
    }

    switch (menuItemID) {
        case MENU_PULL: {
            uint64 myChannel = 0;
            if (ts3Functions.getChannelOfClient(schid, ts3SelfClientID(schid), &myChannel) == ERROR_ok)
                ts3Functions.requestClientMove(schid, target, myChannel, "", RETURN_CODE);
            break;
        }
        case MENU_JAIL:
            jailClient(schid, target, 0);
            break;
        case MENU_JAIL_5M:
            jailClient(schid, target, 5);
            break;
        case MENU_JAIL_15M:
            jailClient(schid, target, 15);
            break;
        case MENU_JAIL_30M:
            jailClient(schid, target, 30);
            break;
        case MENU_JAIL_60M:
            jailClient(schid, target, 60);
            break;
        case MENU_RELEASE:
            releaseClient(schid, target);
            break;
        case MENU_TALK_GRANT:
            ts3Functions.requestClientSetIsTalker(schid, target, 1, RETURN_CODE);
            break;
        case MENU_TALK_REVOKE:
            ts3Functions.requestClientSetIsTalker(schid, target, 0, RETURN_CODE);
            break;
        case MENU_POKE_STAFF:
            ts3Functions.requestClientPoke(schid, target, "Please join the staff channel.", RETURN_CODE);
            break;
        case MENU_POKE_RULES:
            ts3Functions.requestClientPoke(schid, target, "Reminder: please review the TeamSpeak rules.", RETURN_CODE);
            break;
        case MENU_KICK_CHANNEL:
            ts3Functions.requestClientKickFromChannel(schid, target, "Removed from channel by staff.", RETURN_CODE);
            break;
        case MENU_KICK_SERVER:
            ts3Functions.requestClientKickFromServer(schid, target, "Kicked by staff.", RETURN_CODE);
            break;
        case MENU_BAN_1H:
            ts3Functions.banclient(schid, target, 3600, "Banned 1 hour by staff.", RETURN_CODE);
            break;
        case MENU_BAN_24H:
            ts3Functions.banclient(schid, target, 86400, "Banned 24 hours by staff.", RETURN_CODE);
            break;
    }
}

PLUGINS_EXPORTDLL void ts3plugin_onConnectStatusChangeEvent(uint64 schid, int newStatus, unsigned int errorNumber) {
    if (newStatus == STATUS_CONNECTION_ESTABLISHED) {
        ts3Functions.requestChannelSubscribeAll(schid, "");
        ts3Functions.requestServerGroupList(schid, "");
    } else if (newStatus == STATUS_DISCONNECTED) {
        std::lock_guard<std::mutex> lock(g_jailMutex);
        g_jailed.erase(schid);
        g_stickySgid.erase(schid);
    }
}

PLUGINS_EXPORTDLL void ts3plugin_onServerGroupListEvent(uint64 schid, uint64 serverGroupID, const char* name, int type, int iconID, int saveDB) {
    if (type == 1 && name && strcmp(name, "Sticky") == 0)
        g_stickySgid[schid] = serverGroupID;
}

PLUGINS_EXPORTDLL int ts3plugin_onServerErrorEvent(uint64 schid, const char* errorMessage, unsigned int error, const char* returnCode, const char* extraMessage) {
    if (!returnCode || strcmp(returnCode, RETURN_CODE) != 0) return 0;
    if (error != ERROR_ok) {
        std::string msg = std::string("Staff Tools: ") + (errorMessage ? errorMessage : "request failed");
        printError(schid, msg.c_str());
        return 1;
    }
    return 1;
}

}

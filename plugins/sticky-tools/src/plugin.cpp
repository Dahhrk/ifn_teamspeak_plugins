#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include "ts3plugin.hpp"

#define RETURN_CODE "sk"

enum MenuId {
    MENU_BOARD = 1,
    MENU_STICKY,
    MENU_STICKY_15M,
    MENU_STICKY_60M,
    MENU_UNSTICKY,
};

struct StickyRecord {
    std::string uid;
    uint64 dbid;
    std::string name;
    uint64 returnCid;
    bool timed;
    std::chrono::steady_clock::time_point releaseAt;
};

static std::map<uint64, std::vector<StickyRecord>> g_records;
static std::map<uint64, uint64> g_stickySgid;
static std::mutex g_mutex;
static std::thread g_worker;
static std::atomic<bool> g_running{false};

TS3_PLUGIN_IDENTITY("IFN Sticky Tools", "1.1", "Dahhrk",
                    "Send users to the jail channel with the Sticky group - indefinite or timed with auto-release.", 23)

static void printError(uint64 schid, const char* msg) {
    ts3Functions.printMessage(schid, msg, PLUGIN_MESSAGE_TARGET_SERVER);
}

static anyID onlineClidByUid(uint64 schid, const std::string& uid) {
    for (anyID clid : ts3ClientList(schid))
        if (ts3ClientString(schid, clid, CLIENT_UNIQUE_IDENTIFIER) == uid)
            return clid;
    return 0;
}

static void releaseSticky(uint64 schid, const StickyRecord& rec) {
    anyID clid = onlineClidByUid(schid, rec.uid);
    if (clid && rec.returnCid)
        ts3Functions.requestClientMove(schid, clid, rec.returnCid, "", RETURN_CODE);
    auto sit = g_stickySgid.find(schid);
    if (sit != g_stickySgid.end() && sit->second && rec.dbid)
        ts3Functions.requestServerGroupDelClient(schid, sit->second, rec.dbid, RETURN_CODE);
    std::string msg = "Sticky: " + ts3Sanitize(rec.name.c_str()) + " released";
    if (!clid) msg += " (was offline - Sticky removed)";
    printError(schid, msg.c_str());
}

static void timerWorker() {
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        if (!g_running.load()) break;
        auto now = std::chrono::steady_clock::now();
        std::map<uint64, std::vector<StickyRecord>> due;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            for (auto& [schid, records] : g_records) {
                for (size_t i = records.size(); i-- > 0;) {
                    if (records[i].timed && records[i].releaseAt <= now) {
                        due[schid].push_back(records[i]);
                        records.erase(records.begin() + i);
                    }
                }
            }
        }
        for (auto& [schid, records] : due)
            for (auto& rec : records) releaseSticky(schid, rec);
    }
}

static void stickyClient(uint64 schid, anyID target, int minutes) {
    auto sit = g_stickySgid.find(schid);
    if (sit == g_stickySgid.end() || !sit->second) {
        printError(schid, "Sticky: no 'Sticky' server group on this server.");
        return;
    }

    uint64 jail = 0;
    if (!ts3FindChannelByName(schid, "jail", &jail)) {
        printError(schid, "Sticky: no channel containing 'jail' on this server.");
        return;
    }

    StickyRecord rec;
    rec.uid = ts3ClientString(schid, target, CLIENT_UNIQUE_IDENTIFIER);
    rec.name = ts3ClientString(schid, target, CLIENT_NICKNAME);
    uint64 dbid = 0;
    ts3Functions.getClientVariableAsUInt64(schid, target, CLIENT_DATABASE_ID, &dbid);
    rec.dbid = dbid;
    uint64 cid = 0;
    ts3Functions.getChannelOfClient(schid, target, &cid);
    rec.returnCid = cid;
    rec.timed = minutes > 0;
    if (rec.timed)
        rec.releaseAt = std::chrono::steady_clock::now() + std::chrono::minutes(minutes);

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (auto& r : g_records[schid])
            if (r.uid == rec.uid) {
                printError(schid, "Sticky: target already jailed.");
                return;
            }
        g_records[schid].push_back(rec);
    }

    ts3Functions.requestClientMove(schid, target, jail, "", RETURN_CODE);
    if (rec.dbid)
        ts3Functions.requestServerGroupAddClient(schid, sit->second, rec.dbid, RETURN_CODE);

    std::string msg = "Sticky: " + ts3Sanitize(rec.name.c_str()) + " sent to jail";
    msg += minutes > 0 ? " for " + std::to_string(minutes) + " min" : " - use Unsticky to release";
    printError(schid, msg.c_str());
}

static void unstickyClient(uint64 schid, anyID target) {
    std::string uid = ts3ClientString(schid, target, CLIENT_UNIQUE_IDENTIFIER);
    StickyRecord rec;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_records.find(schid);
        if (it != g_records.end()) {
            for (size_t i = it->second.size(); i-- > 0;) {
                if (it->second[i].uid == uid) {
                    rec = it->second[i];
                    it->second.erase(it->second.begin() + i);
                    found = true;
                    break;
                }
            }
        }
    }
    if (!found) {
        printError(schid, "Sticky: target is not stickied.");
        return;
    }
    releaseSticky(schid, rec);
}

static void printBoard(uint64 schid) {
    std::vector<StickyRecord> snapshot;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_records.find(schid);
        if (it != g_records.end()) snapshot = it->second;
    }
    if (snapshot.empty()) {
        printError(schid, "Sticky: nobody is jailed.");
        return;
    }
    auto now = std::chrono::steady_clock::now();
    std::string out = "[b]Jailed users[/b]\n";
    for (auto& rec : snapshot) {
        out += "- " + ts3Sanitize(rec.name.c_str());
        if (rec.timed) {
            auto left = std::chrono::duration_cast<std::chrono::seconds>(rec.releaseAt - now).count();
            if (left < 0) left = 0;
            out += " - " + std::to_string(left / 60) + "m " + std::to_string(left % 60) + "s left";
        } else {
            out += " - indefinite";
        }
        out += "\n";
    }
    out += "Right-click a user and Unsticky to release early.";
    ts3Functions.printMessage(schid, out.c_str(), PLUGIN_MESSAGE_TARGET_SERVER);
}

extern "C" {

PLUGINS_EXPORTDLL int ts3plugin_init() {
    g_running.store(true);
    g_worker = std::thread(timerWorker);
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
        enum PluginMenuType type;
    } items[] = {
        {MENU_BOARD, "Sticky board", PLUGIN_MENU_TYPE_GLOBAL},
        {MENU_STICKY, "Sticky", PLUGIN_MENU_TYPE_CLIENT},
        {MENU_STICKY_15M, "Sticky 15 min", PLUGIN_MENU_TYPE_CLIENT},
        {MENU_STICKY_60M, "Sticky 60 min", PLUGIN_MENU_TYPE_CLIENT},
        {MENU_UNSTICKY, "Unsticky", PLUGIN_MENU_TYPE_CLIENT},
    };
    const size_t count = sizeof(items) / sizeof(items[0]);
    *menuItems = (struct PluginMenuItem**)malloc((count + 1) * sizeof(struct PluginMenuItem*));
    for (size_t i = 0; i < count; ++i)
        (*menuItems)[i] = ts3MakeMenuItem(items[i].type, items[i].id, items[i].text);
    (*menuItems)[count] = NULL;
    *menuIcon = NULL;
}

PLUGINS_EXPORTDLL void ts3plugin_onMenuItemEvent(uint64 schid, enum PluginMenuType type, int menuItemID, uint64 selectedItemID) {
    if (type == PLUGIN_MENU_TYPE_GLOBAL) {
        if (menuItemID == MENU_BOARD) printBoard(schid);
        return;
    }
    if (type != PLUGIN_MENU_TYPE_CLIENT) return;
    anyID target = (anyID)selectedItemID;
    if (target == ts3SelfClientID(schid)) {
        printError(schid, "Sticky: cannot target yourself.");
        return;
    }

    switch (menuItemID) {
        case MENU_STICKY:
            stickyClient(schid, target, 0);
            break;
        case MENU_STICKY_15M:
            stickyClient(schid, target, 15);
            break;
        case MENU_STICKY_60M:
            stickyClient(schid, target, 60);
            break;
        case MENU_UNSTICKY:
            unstickyClient(schid, target);
            break;
    }
}

PLUGINS_EXPORTDLL void ts3plugin_onConnectStatusChangeEvent(uint64 schid, int newStatus, unsigned int errorNumber) {
    if (newStatus == STATUS_CONNECTION_ESTABLISHED) {
        ts3Functions.requestChannelSubscribeAll(schid, "");
        ts3Functions.requestServerGroupList(schid, "");
    } else if (newStatus == STATUS_DISCONNECTED) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_records.erase(schid);
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
        std::string msg = std::string("Sticky: ") + (errorMessage ? errorMessage : "request failed");
        printError(schid, msg.c_str());
        return 1;
    }
    return 1;
}

}

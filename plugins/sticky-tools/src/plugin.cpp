#include <algorithm>
#include <atomic>
#include <chrono>
#include <time.h>
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
    MENU_EXTEND,
};

struct StickyRecord {
    std::string uid;
    uint64 dbid;
    uint64 sgid;
    std::string name;
    std::string channelName;
    uint64 returnCid;
    uint64 jailCid;
    bool timed;
    std::chrono::steady_clock::time_point releaseAt;
};

static std::map<uint64, std::vector<StickyRecord>> g_records;
static std::map<uint64, uint64> g_stickySgid;
static std::map<uint64, std::string> g_serverUid;
static std::mutex g_mutex;
static std::thread g_worker;
static std::atomic<bool> g_running{false};

TS3_PLUGIN_IDENTITY("IFN Sticky Tools", "1.4", "Dahhrk",
                    "Send users to the jail channel with the Sticky group - indefinite or timed with auto-release.", 23)

static void printMsg(uint64 schid, const char* msg) {
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
    if (rec.sgid && rec.dbid)
        ts3Functions.requestServerGroupDelClient(schid, rec.sgid, rec.dbid, RETURN_CODE);
    std::string msg = "Sticky: " + ts3Sanitize(rec.name.c_str()) + " released";
    if (!clid) msg += " (was offline - Sticky removed)";
    printMsg(schid, msg.c_str());
}

static std::string storePath() {
    std::string dir = ts3ConfigDir();
    if (dir.empty()) return dir;
    if (dir.back() != '\\' && dir.back() != '/') dir += '/';
    return dir + "ifn_sticky_records.txt";
}

static void saveRecords() {
    std::string path = storePath();
    if (path.empty()) return;
    std::vector<std::string> current;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (auto& [schid, records] : g_records) {
            auto sit = g_serverUid.find(schid);
            if (!records.empty() && sit != g_serverUid.end()) current.push_back(sit->second);
        }
    }
    std::string body;
    {
        std::ifstream in(path.c_str());
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            std::vector<std::string> f = ts3Split(line);
            if (!f.empty() && std::find(current.begin(), current.end(), f[0]) == current.end())
                body += line + "\n";
        }
    }
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto now = std::chrono::steady_clock::now();
        for (auto& [schid, records] : g_records) {
            auto sit = g_serverUid.find(schid);
            if (sit == g_serverUid.end()) continue;
            for (auto& r : records) {
                uint64 exp = 0;
                if (r.timed)
                    exp = (uint64)((int64_t)time(NULL) +
                          std::chrono::duration_cast<std::chrono::seconds>(r.releaseAt - now).count());
                body += ts3Esc(sit->second) + "|" + ts3Esc(r.uid) + "|" +
                        std::to_string(r.dbid) + "|" + std::to_string(r.sgid) + "|" +
                        ts3Esc(r.channelName) + "|" + std::to_string(exp) + "|" +
                        ts3Esc(r.name) + "\n";
            }
        }
    }
    ts3WriteFile(path, body);
}

static void releaseSticky(uint64 schid, const StickyRecord& rec);

static void loadRecords(uint64 schid) {
    std::string server = ts3ServerUid(schid);
    if (server.empty()) return;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_serverUid[schid] = server;
    }
    std::vector<StickyRecord> live, dead;
    ts3ReadLines(storePath(), server, [&](const std::vector<std::string>& f) {
        if (f.size() < 7) return;
        StickyRecord rec;
        rec.uid = f[1];
        rec.dbid = strtoull(f[2].c_str(), NULL, 10);
        rec.sgid = strtoull(f[3].c_str(), NULL, 10);
        rec.channelName = f[4];
        uint64 exp = strtoull(f[5].c_str(), NULL, 10);
        rec.name = f[6];
        if (rec.uid.empty()) return;
        rec.timed = exp != 0;
        rec.returnCid = 0;
        rec.jailCid = 0;
        uint64 cid = 0;
        if (!rec.channelName.empty() && ts3FindChannelByName(schid, rec.channelName.c_str(), &cid))
            rec.returnCid = cid;
        if (ts3FindChannelByName(schid, "jail", &cid)) rec.jailCid = cid;
        if (rec.timed) {
            int64_t left = (int64_t)exp - (int64_t)time(NULL);
            if (left <= 0) {
                dead.push_back(rec);
                return;
            }
            rec.releaseAt = std::chrono::steady_clock::now() + std::chrono::seconds(left);
        }
        live.push_back(rec);
    });
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (auto& r : live) g_records[schid].push_back(r);
    }
    for (auto& r : dead) releaseSticky(schid, r);
    if (!live.empty()) {
        std::string msg = "Sticky: restored " + std::to_string(live.size()) + " jail record(s) from before disconnect.";
        printMsg(schid, msg.c_str());
    }
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
        if (!due.empty()) saveRecords();
        for (auto& [schid, records] : due)
            for (auto& rec : records) releaseSticky(schid, rec);
    }
}

static void stickyClient(uint64 schid, anyID target, int minutes) {
    auto sit = g_stickySgid.find(schid);
    if (sit == g_stickySgid.end() || !sit->second) {
        printMsg(schid, "Sticky: no 'Sticky' server group on this server.");
        return;
    }

    uint64 jail = 0;
    if (!ts3FindChannelByName(schid, "jail", &jail)) {
        printMsg(schid, "Sticky: no channel containing 'jail' on this server.");
        return;
    }

    StickyRecord rec;
    rec.uid = ts3ClientString(schid, target, CLIENT_UNIQUE_IDENTIFIER);
    if (rec.uid.empty()) {
        printMsg(schid, "Sticky: cannot read target identity.");
        return;
    }
    rec.name = ts3ClientString(schid, target, CLIENT_NICKNAME);
    uint64 dbid = 0;
    ts3Functions.getClientVariableAsUInt64(schid, target, CLIENT_DATABASE_ID, &dbid);
    rec.dbid = dbid;
    rec.sgid = sit->second;
    uint64 cid = 0;
    ts3Functions.getChannelOfClient(schid, target, &cid);
    if (cid != jail) {
        rec.returnCid = cid;
        rec.channelName = cid ? ts3ChannelName(schid, cid) : "";
    } else {
        rec.returnCid = 0;
    }
    rec.jailCid = jail;
    rec.timed = minutes > 0;
    if (rec.timed)
        rec.releaseAt = std::chrono::steady_clock::now() + std::chrono::minutes(minutes);

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (auto& r : g_records[schid])
            if (r.uid == rec.uid) {
                printMsg(schid, "Sticky: target already jailed.");
                return;
            }
        g_records[schid].push_back(rec);
    }
    saveRecords();

    ts3Functions.requestClientMove(schid, target, jail, "", RETURN_CODE);
    if (rec.dbid)
        ts3Functions.requestServerGroupAddClient(schid, rec.sgid, rec.dbid, RETURN_CODE);

    std::string msg = "Sticky: " + ts3Sanitize(rec.name.c_str()) + " sent to jail";
    msg += minutes > 0 ? " for " + std::to_string(minutes) + " min" : " - use Unsticky to release";
    printMsg(schid, msg.c_str());
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
        printMsg(schid, "Sticky: target is not jailed.");
        return;
    }
    saveRecords();
    releaseSticky(schid, rec);
}

static void extendSticky(uint64 schid, anyID target, int minutes) {
    std::string uid = ts3ClientString(schid, target, CLIENT_UNIQUE_IDENTIFIER);
    int n = 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_records.find(schid);
        if (it != g_records.end())
            for (auto& r : it->second)
                if (r.uid == uid && r.timed) {
                    r.releaseAt += std::chrono::minutes(minutes);
                    ++n;
                }
    }
    if (!n) {
        printMsg(schid, "Sticky: no timed jail on target.");
        return;
    }
    saveRecords();
    std::string msg = "Sticky: extended by " + std::to_string(minutes) + " min.";
    printMsg(schid, msg.c_str());
}

static void printBoard(uint64 schid) {
    std::vector<StickyRecord> snapshot;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_records.find(schid);
        if (it != g_records.end()) snapshot = it->second;
    }
    if (snapshot.empty()) {
        printMsg(schid, "Sticky: nobody is jailed.");
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
    saveRecords();
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
        {MENU_EXTEND, "Extend +15 min", PLUGIN_MENU_TYPE_CLIENT},
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
        printMsg(schid, "Sticky: cannot target yourself.");
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
        case MENU_EXTEND:
            extendSticky(schid, target, 15);
            break;
    }
}

PLUGINS_EXPORTDLL void ts3plugin_onClientMoveEvent(uint64 schid, anyID clientID, uint64 oldChannelID, uint64 newChannelID, int visibility, const char* moveMessage) {
    if (newChannelID == 0 || clientID == ts3SelfClientID(schid)) return;
    uint64 jail = 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_records.find(schid);
        if (it == g_records.end() || it->second.empty()) return;
        std::string uid = ts3ClientString(schid, clientID, CLIENT_UNIQUE_IDENTIFIER);
        for (auto& r : it->second)
            if (r.uid == uid) {
                jail = r.jailCid;
                break;
            }
    }
    if (jail && newChannelID != jail) {
        ts3Functions.requestClientMove(schid, clientID, jail, "", RETURN_CODE);
        std::string name = ts3ClientString(schid, clientID, CLIENT_NICKNAME);
        std::string msg = "Sticky: " + ts3Sanitize(name.c_str()) + " moved back to jail";
        printMsg(schid, msg.c_str());
    }
}

PLUGINS_EXPORTDLL void ts3plugin_onConnectStatusChangeEvent(uint64 schid, int newStatus, unsigned int errorNumber) {
    if (newStatus == STATUS_CONNECTION_ESTABLISHED) {
        ts3Functions.requestChannelSubscribeAll(schid, "");
        ts3Functions.requestServerGroupList(schid, "");
        loadRecords(schid);
    } else if (newStatus == STATUS_DISCONNECTED) {
        saveRecords();
        std::lock_guard<std::mutex> lock(g_mutex);
        g_records.erase(schid);
        g_stickySgid.erase(schid);
        g_serverUid.erase(schid);
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
        printMsg(schid, msg.c_str());
        return 1;
    }
    return 1;
}

}

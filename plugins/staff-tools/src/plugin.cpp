#include <algorithm>
#include <atomic>
#include <chrono>
#include <time.h>
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
    MENU_MUTE_10M,
    MENU_MUTE_30M,
    MENU_CMUTE_10M,
    MENU_UNMUTE,
    MENU_STICKY,
    MENU_STICKY_30M,
    MENU_UNSTICKY,
    MENU_EXTEND,
    MENU_BOARD,
};

enum ActionKind { ACT_JAIL, ACT_MUTE, ACT_CMUTE, ACT_STICKY };

struct TimedRecord {
    std::string uid;
    uint64 dbid;
    std::string name;
    std::string channelName;
    ActionKind kind;
    uint64 sgid;
    uint64 cid;
    uint64 cgid;
    uint64 jailCid;
    bool timed;
    std::chrono::steady_clock::time_point releaseAt;
};

static std::map<uint64, std::vector<TimedRecord>> g_records;
static std::map<uint64, uint64> g_stickySgid;
static std::map<uint64, uint64> g_mutedSgid;
static std::map<uint64, uint64> g_cmuteCgid;
static std::map<uint64, uint64> g_defaultCgid;
static std::map<uint64, std::string> g_serverUid;
static std::mutex g_mutex;
static std::thread g_worker;
static std::atomic<bool> g_running{false};

TS3_PLUGIN_IDENTITY("IFN Staff Tools", "1.6", "Dahhrk",
                    "Right-click actions: pull, timed jail/mute/sticky with auto-release, talk power, pokes, kicks, bans.", 23)

static void printMsg(uint64 schid, const char* msg) {
    ts3Functions.printMessage(schid, msg, PLUGIN_MESSAGE_TARGET_SERVER);
}

static anyID onlineClidByUid(uint64 schid, const std::string& uid) {
    for (anyID clid : ts3ClientList(schid))
        if (ts3ClientString(schid, clid, CLIENT_UNIQUE_IDENTIFIER) == uid)
            return clid;
    return 0;
}

static const char* kindName(ActionKind k) {
    switch (k) {
        case ACT_JAIL: return "jail";
        case ACT_MUTE: return "mute";
        case ACT_CMUTE: return "channel mute";
        case ACT_STICKY: return "sticky";
    }
    return "action";
}

static void releaseRecord(uint64 schid, const TimedRecord& rec) {
    anyID clid = onlineClidByUid(schid, rec.uid);
    std::string detail;
    switch (rec.kind) {
        case ACT_JAIL:
            if (clid && rec.cid)
                ts3Functions.requestClientMove(schid, clid, rec.cid, "", RETURN_CODE);
            if (rec.sgid && rec.dbid)
                ts3Functions.requestServerGroupDelClient(schid, rec.sgid, rec.dbid, RETURN_CODE);
            if (!clid) detail = " (was offline - Sticky removed)";
            break;
        case ACT_MUTE:
            if (rec.sgid && rec.dbid)
                ts3Functions.requestServerGroupDelClient(schid, rec.sgid, rec.dbid, RETURN_CODE);
            if (!clid) detail = " (was offline - Muted removed)";
            break;
        case ACT_STICKY:
            if (rec.sgid && rec.dbid)
                ts3Functions.requestServerGroupDelClient(schid, rec.sgid, rec.dbid, RETURN_CODE);
            if (!clid) detail = " (was offline - Sticky removed)";
            break;
        case ACT_CMUTE:
            if (rec.cid && rec.cgid && rec.dbid) {
                const uint64 cgids[] = {rec.cgid};
                const uint64 cids[] = {rec.cid};
                const uint64 dbids[] = {rec.dbid};
                ts3Functions.requestSetClientChannelGroup(schid, cgids, cids, dbids, 1, RETURN_CODE);
            }
            if (!clid) detail = " (was offline)";
            break;
    }
    std::string msg = "Staff Tools: " + ts3Sanitize(rec.name.c_str()) + " " +
                      kindName(rec.kind) + " released" + detail;
    printMsg(schid, msg.c_str());
}

static std::string storePath() {
    std::string dir = ts3ConfigDir();
    if (dir.empty()) return dir;
    if (dir.back() != '\\' && dir.back() != '/') dir += '/';
    return dir + "ifn_staff_records.txt";
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
                body += ts3Esc(sit->second) + "|" + std::to_string((int)r.kind) + "|" +
                        ts3Esc(r.uid) + "|" + std::to_string(r.dbid) + "|" +
                        std::to_string(r.sgid) + "|" + std::to_string(r.cgid) + "|" +
                        ts3Esc(r.channelName) + "|" + std::to_string(exp) + "|" +
                        ts3Esc(r.name) + "\n";
            }
        }
    }
    ts3WriteFile(path, body);
}

static void releaseRecord(uint64 schid, const TimedRecord& rec);

static void loadRecords(uint64 schid) {
    std::string server = ts3ServerUid(schid);
    if (server.empty()) return;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_serverUid[schid] = server;
    }
    std::vector<TimedRecord> live, dead;
    ts3ReadLines(storePath(), server, [&](const std::vector<std::string>& f) {
        if (f.size() < 9) return;
        TimedRecord rec;
        rec.kind = (ActionKind)atoi(f[1].c_str());
        rec.uid = f[2];
        rec.dbid = strtoull(f[3].c_str(), NULL, 10);
        rec.sgid = strtoull(f[4].c_str(), NULL, 10);
        rec.cgid = strtoull(f[5].c_str(), NULL, 10);
        rec.channelName = f[6];
        uint64 exp = strtoull(f[7].c_str(), NULL, 10);
        rec.name = f[8];
        if (rec.uid.empty()) return;
        rec.timed = exp != 0;
        rec.cid = 0;
        rec.jailCid = 0;
        uint64 cid = 0;
        if (!rec.channelName.empty() && ts3FindChannelByName(schid, rec.channelName.c_str(), &cid))
            rec.cid = cid;
        if (rec.kind == ACT_JAIL && ts3FindChannelByName(schid, "jail", &cid))
            rec.jailCid = cid;
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
    for (auto& r : dead) releaseRecord(schid, r);
    if (!live.empty()) {
        std::string msg = "Staff Tools: restored " + std::to_string(live.size()) + " active record(s) from before disconnect.";
        printMsg(schid, msg.c_str());
    }
}

static void timerWorker() {
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        if (!g_running.load()) break;
        auto now = std::chrono::steady_clock::now();
        std::map<uint64, std::vector<TimedRecord>> due;
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
            for (auto& rec : records) releaseRecord(schid, rec);
    }
}

static bool addRecord(uint64 schid, const TimedRecord& rec) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto& r : g_records[schid])
        if (r.uid == rec.uid && r.kind == rec.kind)
            return false;
    g_records[schid].push_back(rec);
    return true;
}

static bool fillBase(uint64 schid, anyID target, ActionKind kind, int minutes, TimedRecord* rec) {
    rec->uid = ts3ClientString(schid, target, CLIENT_UNIQUE_IDENTIFIER);
    if (rec->uid.empty()) {
        printMsg(schid, "Staff Tools: cannot read target identity.");
        return false;
    }
    rec->name = ts3ClientString(schid, target, CLIENT_NICKNAME);
    rec->kind = kind;
    uint64 dbid = 0;
    ts3Functions.getClientVariableAsUInt64(schid, target, CLIENT_DATABASE_ID, &dbid);
    rec->dbid = dbid;
    rec->sgid = 0;
    rec->cid = 0;
    rec->cgid = 0;
    rec->jailCid = 0;
    rec->channelName.clear();
    rec->timed = minutes > 0;
    if (rec->timed)
        rec->releaseAt = std::chrono::steady_clock::now() + std::chrono::minutes(minutes);
    return true;
}

static void jailClient(uint64 schid, anyID target, int minutes) {
    uint64 jail = 0;
    if (!ts3FindChannelByName(schid, "jail", &jail)) {
        printMsg(schid, "Staff Tools: no channel containing 'jail' on this server.");
        return;
    }

    TimedRecord rec;
    if (!fillBase(schid, target, ACT_JAIL, minutes, &rec)) return;
    auto sit = g_stickySgid.find(schid);
    rec.sgid = sit != g_stickySgid.end() ? sit->second : 0;
    uint64 cid = 0;
    ts3Functions.getChannelOfClient(schid, target, &cid);
    if (cid != jail) {
        rec.cid = cid;
        rec.channelName = cid ? ts3ChannelName(schid, cid) : "";
    }
    rec.jailCid = jail;

    if (!addRecord(schid, rec)) {
        printMsg(schid, "Staff Tools: target already jailed.");
        return;
    }
    saveRecords();

    ts3Functions.requestClientMove(schid, target, jail, "", RETURN_CODE);
    if (rec.sgid && rec.dbid)
        ts3Functions.requestServerGroupAddClient(schid, rec.sgid, rec.dbid, RETURN_CODE);

    std::string msg = "Jail: " + ts3Sanitize(rec.name.c_str());
    msg += minutes > 0 ? " jailed for " + std::to_string(minutes) + " min - auto-release set"
                       : " jailed - use Release to free";
    printMsg(schid, msg.c_str());
}

static void muteClient(uint64 schid, anyID target, int minutes, bool channelOnly) {
    TimedRecord rec;
    if (!fillBase(schid, target, channelOnly ? ACT_CMUTE : ACT_MUTE, minutes, &rec)) return;

    if (channelOnly) {
        auto git = g_cmuteCgid.find(schid);
        if (git == g_cmuteCgid.end() || !git->second) {
            printMsg(schid, "Staff Tools: no 'Channel Muted' channel group on this server.");
            return;
        }
        uint64 cid = 0;
        ts3Functions.getChannelOfClient(schid, target, &cid);
        if (!cid) {
            printMsg(schid, "Staff Tools: cannot read target channel.");
            return;
        }
        rec.cid = cid;
        rec.channelName = ts3ChannelName(schid, cid);
        uint64 prev = 0;
        ts3Functions.getClientVariableAsUInt64(schid, target, CLIENT_CHANNEL_GROUP_ID, &prev);
        if (!prev) {
            auto dit = g_defaultCgid.find(schid);
            prev = dit != g_defaultCgid.end() ? dit->second : 0;
        }
        if (!prev) {
            printMsg(schid, "Staff Tools: cannot resolve prior channel group.");
            return;
        }
        rec.cgid = prev;
        rec.sgid = git->second;
    } else {
        auto mit = g_mutedSgid.find(schid);
        if (mit == g_mutedSgid.end() || !mit->second) {
            printMsg(schid, "Staff Tools: no 'Muted' server group on this server.");
            return;
        }
        rec.sgid = mit->second;
    }

    if (!addRecord(schid, rec)) {
        printMsg(schid, "Staff Tools: target already has that action.");
        return;
    }
    saveRecords();

    if (channelOnly) {
        const uint64 cgids[] = {rec.sgid};
        const uint64 cids[] = {rec.cid};
        const uint64 dbids[] = {rec.dbid};
        ts3Functions.requestSetClientChannelGroup(schid, cgids, cids, dbids, 1, RETURN_CODE);
    } else if (rec.dbid) {
        ts3Functions.requestServerGroupAddClient(schid, rec.sgid, rec.dbid, RETURN_CODE);
    }

    std::string msg = "Staff Tools: " + ts3Sanitize(rec.name.c_str()) + " " +
                      kindName(rec.kind) + " for " + std::to_string(minutes) + " min";
    printMsg(schid, msg.c_str());
}

static void stickyClient(uint64 schid, anyID target, int minutes) {
    auto sit = g_stickySgid.find(schid);
    if (sit == g_stickySgid.end() || !sit->second) {
        printMsg(schid, "Staff Tools: no 'Sticky' server group on this server.");
        return;
    }

    TimedRecord rec;
    if (!fillBase(schid, target, ACT_STICKY, minutes, &rec)) return;
    rec.sgid = sit->second;

    if (!addRecord(schid, rec)) {
        printMsg(schid, "Staff Tools: target already stickied.");
        return;
    }
    saveRecords();
    if (rec.dbid)
        ts3Functions.requestServerGroupAddClient(schid, rec.sgid, rec.dbid, RETURN_CODE);

    std::string msg = "Staff Tools: " + ts3Sanitize(rec.name.c_str()) + " stickied";
    msg += minutes > 0 ? " for " + std::to_string(minutes) + " min" : " - use Unsticky to remove";
    printMsg(schid, msg.c_str());
}

static void releaseClient(uint64 schid, anyID target, std::initializer_list<ActionKind> kinds, const char* noneMsg) {
    std::string uid = ts3ClientString(schid, target, CLIENT_UNIQUE_IDENTIFIER);
    std::vector<TimedRecord> found;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_records.find(schid);
        if (it == g_records.end()) {
            printMsg(schid, noneMsg);
            return;
        }
        for (size_t i = it->second.size(); i-- > 0;) {
            bool match = it->second[i].uid == uid &&
                         std::find(kinds.begin(), kinds.end(), it->second[i].kind) != kinds.end();
            if (match) {
                found.push_back(it->second[i]);
                it->second.erase(it->second.begin() + i);
            }
        }
    }
    if (found.empty()) {
        printMsg(schid, noneMsg);
        return;
    }
    saveRecords();
    for (auto& rec : found) releaseRecord(schid, rec);
}

static void extendClient(uint64 schid, anyID target, int minutes) {
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
        printMsg(schid, "Staff Tools: no timed actions on target.");
        return;
    }
    saveRecords();
    std::string msg = "Staff Tools: extended " + std::to_string(n) + " action(s) by " +
                      std::to_string(minutes) + " min.";
    printMsg(schid, msg.c_str());
}

static void printBoard(uint64 schid) {
    std::vector<TimedRecord> snapshot;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_records.find(schid);
        if (it != g_records.end()) snapshot = it->second;
    }
    if (snapshot.empty()) {
        printMsg(schid, "Staff Tools: no active jails or mutes.");
        return;
    }
    auto now = std::chrono::steady_clock::now();
    std::string out = "[b]Active staff actions[/b]\n";
    for (auto& rec : snapshot) {
        out += "- " + ts3Sanitize(rec.name.c_str()) + " [" + kindName(rec.kind) + "]";
        if (rec.timed) {
            auto left = std::chrono::duration_cast<std::chrono::seconds>(rec.releaseAt - now).count();
            if (left < 0) left = 0;
            out += " - " + std::to_string(left / 60) + "m " + std::to_string(left % 60) + "s left";
        } else {
            out += " - indefinite";
        }
        out += "\n";
    }
    out += "Right-click a user to release early.";
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
        {MENU_BOARD, "Jail/mute board", PLUGIN_MENU_TYPE_GLOBAL},
        {MENU_PULL, "Pull to my channel", PLUGIN_MENU_TYPE_CLIENT},
        {MENU_JAIL, "Send to Jail", PLUGIN_MENU_TYPE_CLIENT},
        {MENU_JAIL_5M, "Jail 5 min", PLUGIN_MENU_TYPE_CLIENT},
        {MENU_JAIL_15M, "Jail 15 min", PLUGIN_MENU_TYPE_CLIENT},
        {MENU_JAIL_30M, "Jail 30 min", PLUGIN_MENU_TYPE_CLIENT},
        {MENU_JAIL_60M, "Jail 60 min", PLUGIN_MENU_TYPE_CLIENT},
        {MENU_RELEASE, "Release from jail", PLUGIN_MENU_TYPE_CLIENT},
        {MENU_MUTE_10M, "Mute 10 min", PLUGIN_MENU_TYPE_CLIENT},
        {MENU_MUTE_30M, "Mute 30 min", PLUGIN_MENU_TYPE_CLIENT},
        {MENU_CMUTE_10M, "Channel mute 10 min", PLUGIN_MENU_TYPE_CLIENT},
        {MENU_UNMUTE, "Unmute", PLUGIN_MENU_TYPE_CLIENT},
        {MENU_STICKY, "Sticky", PLUGIN_MENU_TYPE_CLIENT},
        {MENU_STICKY_30M, "Sticky 30 min", PLUGIN_MENU_TYPE_CLIENT},
        {MENU_UNSTICKY, "Unsticky", PLUGIN_MENU_TYPE_CLIENT},
        {MENU_EXTEND, "Extend +15 min", PLUGIN_MENU_TYPE_CLIENT},
        {MENU_TALK_GRANT, "Grant talk power", PLUGIN_MENU_TYPE_CLIENT},
        {MENU_TALK_REVOKE, "Revoke talk power", PLUGIN_MENU_TYPE_CLIENT},
        {MENU_POKE_STAFF, "Poke: join staff channel", PLUGIN_MENU_TYPE_CLIENT},
        {MENU_POKE_RULES, "Poke: rules reminder", PLUGIN_MENU_TYPE_CLIENT},
        {MENU_KICK_CHANNEL, "Kick from channel", PLUGIN_MENU_TYPE_CLIENT},
        {MENU_KICK_SERVER, "Kick from server", PLUGIN_MENU_TYPE_CLIENT},
        {MENU_BAN_1H, "Ban 1 hour", PLUGIN_MENU_TYPE_CLIENT},
        {MENU_BAN_24H, "Ban 24 hours", PLUGIN_MENU_TYPE_CLIENT},
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
        printMsg(schid, "Staff Tools: cannot target yourself.");
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
            releaseClient(schid, target, {ACT_JAIL}, "Staff Tools: target is not jailed.");
            break;
        case MENU_MUTE_10M:
            muteClient(schid, target, 10, false);
            break;
        case MENU_MUTE_30M:
            muteClient(schid, target, 30, false);
            break;
        case MENU_CMUTE_10M:
            muteClient(schid, target, 10, true);
            break;
        case MENU_UNMUTE:
            releaseClient(schid, target, {ACT_MUTE, ACT_CMUTE}, "Staff Tools: target is not muted.");
            break;
        case MENU_STICKY:
            stickyClient(schid, target, 0);
            break;
        case MENU_STICKY_30M:
            stickyClient(schid, target, 30);
            break;
        case MENU_UNSTICKY:
            releaseClient(schid, target, {ACT_STICKY}, "Staff Tools: target is not stickied.");
            break;
        case MENU_EXTEND:
            extendClient(schid, target, 15);
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

PLUGINS_EXPORTDLL void ts3plugin_onClientMoveEvent(uint64 schid, anyID clientID, uint64 oldChannelID, uint64 newChannelID, int visibility, const char* moveMessage) {
    if (newChannelID == 0 || clientID == ts3SelfClientID(schid)) return;
    uint64 jail = 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_records.find(schid);
        if (it == g_records.end() || it->second.empty()) return;
        std::string uid = ts3ClientString(schid, clientID, CLIENT_UNIQUE_IDENTIFIER);
        for (auto& r : it->second)
            if (r.uid == uid && r.kind == ACT_JAIL && r.jailCid) {
                jail = r.jailCid;
                break;
            }
    }
    if (jail && newChannelID != jail) {
        ts3Functions.requestClientMove(schid, clientID, jail, "", RETURN_CODE);
        std::string name = ts3ClientString(schid, clientID, CLIENT_NICKNAME);
        std::string msg = "Staff Tools: " + ts3Sanitize(name.c_str()) + " moved back to jail";
        printMsg(schid, msg.c_str());
    }
}

PLUGINS_EXPORTDLL void ts3plugin_onConnectStatusChangeEvent(uint64 schid, int newStatus, unsigned int errorNumber) {
    if (newStatus == STATUS_CONNECTION_ESTABLISHED) {
        ts3Functions.requestChannelSubscribeAll(schid, "");
        ts3Functions.requestServerGroupList(schid, "");
        ts3Functions.requestChannelGroupList(schid, "");
        loadRecords(schid);
    } else if (newStatus == STATUS_DISCONNECTED) {
        saveRecords();
        std::lock_guard<std::mutex> lock(g_mutex);
        g_records.erase(schid);
        g_stickySgid.erase(schid);
        g_mutedSgid.erase(schid);
        g_cmuteCgid.erase(schid);
        g_defaultCgid.erase(schid);
        g_serverUid.erase(schid);
    }
}

PLUGINS_EXPORTDLL void ts3plugin_onServerGroupListEvent(uint64 schid, uint64 serverGroupID, const char* name, int type, int iconID, int saveDB) {
    if (type != 1 || !name) return;
    if (strcmp(name, "Sticky") == 0) g_stickySgid[schid] = serverGroupID;
    if (strcmp(name, "Muted") == 0) g_mutedSgid[schid] = serverGroupID;
}

PLUGINS_EXPORTDLL void ts3plugin_onChannelGroupListEvent(uint64 schid, uint64 channelGroupID, const char* name, int type, int iconID, int saveDB) {
    if (type != 1 || !name) return;
    if (strcmp(name, "Channel Muted") == 0) g_cmuteCgid[schid] = channelGroupID;
    if (strcmp(name, "No Channel Group") == 0) g_defaultCgid[schid] = channelGroupID;
}

PLUGINS_EXPORTDLL int ts3plugin_onServerErrorEvent(uint64 schid, const char* errorMessage, unsigned int error, const char* returnCode, const char* extraMessage) {
    if (!returnCode || strcmp(returnCode, RETURN_CODE) != 0) return 0;
    if (error != ERROR_ok) {
        std::string msg = std::string("Staff Tools: ") + (errorMessage ? errorMessage : "request failed");
        printMsg(schid, msg.c_str());
        return 1;
    }
    return 1;
}

}

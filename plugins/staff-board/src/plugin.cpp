#include <algorithm>
#include <map>
#include <set>

#include "ts3plugin.hpp"

#define RETURN_CODE "sb"
#define MENU_ID_BOARD 0
#define MENU_ID_REFRESH 1

struct Tier {
    const char* label;
    const char* groups[8];
};

static const Tier STAFF_TIERS[] = {
    {"Executive", {"Owner", "Director", "Supervisor", NULL}},
    {"Leadership", {"Division Leader", "Head Administrator", "Elite Administrator", "Administrator", NULL}},
    {"Moderation", {"Senior Moderator", "Moderator", "Trial Moderator", NULL}},
    {"TeamSpeak", {"TeamSpeak Manager", "TeamSpeak Administrator", "TeamSpeak Moderator", NULL}},
    {"Game Master", {"Game Master Manager", "Game Master Lead", "Game Master Officer", "Game Master", "Game Master Apprentice", NULL}},
    {"Mentors", {"Mentor Manager", "Mentor Officer", "Mentor", NULL}},
    {"Recruitment", {"Recruitment Manager", "Recruitment Lead", "Recruitment Officer", "Recruitment Member", NULL}},
    {"Forums", {"Forum Manager", "Forum Administrator", "Forum Moderator", NULL}},
};

struct Group {
    uint64 sgid;
    std::string name;
};

static std::map<uint64, std::vector<Group>> g_groups;

TS3_PLUGIN_IDENTITY("IFN Staff Board", "1.0", "Dahhrk",
                    "Print online staff grouped by rank tier.", 23)

static std::vector<uint64> parseGroupIds(const std::string& csv) {
    std::vector<uint64> out;
    size_t pos = 0;
    while (pos <= csv.size()) {
        size_t comma = csv.find(',', pos);
        std::string tok = csv.substr(pos, comma == std::string::npos ? comma : comma - pos);
        if (!tok.empty()) out.push_back(strtoull(tok.c_str(), NULL, 10));
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return out;
}

static std::set<uint64> staffGroupIds(uint64 schid, const char* const* names) {
    std::set<uint64> out;
    auto it = g_groups.find(schid);
    if (it == g_groups.end()) return out;
    for (const auto& g : it->second)
        for (size_t i = 0; names[i]; ++i)
            if (g.name == names[i]) out.insert(g.sgid);
    return out;
}

static void printBoard(uint64 schid) {
    auto git = g_groups.find(schid);
    if (git == g_groups.end() || git->second.empty()) {
        ts3Functions.printMessage(schid, "Staff Board: group list not loaded yet - use Refresh first.", PLUGIN_MESSAGE_TARGET_SERVER);
        return;
    }

    const size_t tierCount = sizeof(STAFF_TIERS) / sizeof(STAFF_TIERS[0]);
    std::vector<std::set<uint64>> tierSgids(tierCount);
    for (size_t t = 0; t < tierCount; ++t)
        tierSgids[t] = staffGroupIds(schid, STAFF_TIERS[t].groups);

    std::vector<std::vector<std::string>> rows(tierCount);
    size_t online = 0, staff = 0;
    for (anyID clid : ts3ClientList(schid)) {
        ++online;
        std::string groupsCsv = ts3ClientString(schid, clid, CLIENT_SERVERGROUPS);
        std::set<uint64> clientGroups;
        for (uint64 s : parseGroupIds(groupsCsv)) clientGroups.insert(s);

        size_t tier = tierCount;
        for (size_t t = 0; t < tierCount; ++t) {
            bool hit = false;
            for (uint64 s : clientGroups)
                if (tierSgids[t].count(s)) { hit = true; break; }
            if (hit) { tier = t; break; }
        }
        if (tier == tierCount) continue;

        ++staff;
        std::string name = ts3Sanitize(ts3ClientString(schid, clid, CLIENT_NICKNAME).c_str());
        std::string uid = ts3ClientString(schid, clid, CLIENT_UNIQUE_IDENTIFIER);
        rows[tier].push_back("[url=client://" + std::to_string(clid) + "/" + uid + "~" + name + "]" + name + "[/url]");
    }

    std::string out = "[b]Staff Board[/b] - " + std::to_string(online) + " online, " + std::to_string(staff) + " staff\n";
    for (size_t t = 0; t < tierCount; ++t) {
        if (rows[t].empty()) continue;
        out += "[b]" + std::string(STAFF_TIERS[t].label) + "[/b]  " + std::to_string(rows[t].size()) + "\n";
        for (auto& r : rows[t]) out += "  " + r + "\n";
    }
    ts3Functions.printMessage(schid, out.c_str(), PLUGIN_MESSAGE_TARGET_SERVER);
}

extern "C" {

PLUGINS_EXPORTDLL void ts3plugin_initMenus(struct PluginMenuItem*** menuItems, char** menuIcon) {
    *menuItems = (struct PluginMenuItem**)malloc(3 * sizeof(struct PluginMenuItem*));
    (*menuItems)[0] = ts3MakeMenuItem(PLUGIN_MENU_TYPE_GLOBAL, MENU_ID_BOARD, "Staff Board");
    (*menuItems)[1] = ts3MakeMenuItem(PLUGIN_MENU_TYPE_GLOBAL, MENU_ID_REFRESH, "Refresh group list");
    (*menuItems)[2] = NULL;
    *menuIcon = NULL;
}

PLUGINS_EXPORTDLL void ts3plugin_onMenuItemEvent(uint64 schid, enum PluginMenuType type, int menuItemID, uint64 selectedItemID) {
    if (type != PLUGIN_MENU_TYPE_GLOBAL) return;
    if (menuItemID == MENU_ID_BOARD) printBoard(schid);
    else if (menuItemID == MENU_ID_REFRESH) ts3Functions.requestServerGroupList(schid, "");
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

}

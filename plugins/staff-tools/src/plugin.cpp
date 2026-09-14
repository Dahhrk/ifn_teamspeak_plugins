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
};

TS3_PLUGIN_IDENTITY("IFN Staff Tools", "1.0", "Dahhrk",
                    "Right-click client actions: pull, jail, talk power, pokes, kicks, bans.", 23)

static void printError(uint64 schid, const char* msg) {
    ts3Functions.printMessage(schid, msg, PLUGIN_MESSAGE_TARGET_SERVER);
}

extern "C" {

PLUGINS_EXPORTDLL void ts3plugin_initMenus(struct PluginMenuItem*** menuItems, char** menuIcon) {
    static const struct {
        int id;
        const char* text;
    } items[] = {
        {MENU_PULL, "Pull to my channel"},
        {MENU_JAIL, "Send to Jail"},
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
        case MENU_JAIL: {
            uint64 jail = 0;
            if (ts3FindChannelByName(schid, "jail", &jail))
                ts3Functions.requestClientMove(schid, target, jail, "", RETURN_CODE);
            else
                printError(schid, "Staff Tools: no channel containing 'jail' on this server.");
            break;
        }
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

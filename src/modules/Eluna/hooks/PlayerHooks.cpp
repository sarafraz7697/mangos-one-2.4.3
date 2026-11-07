/*
 * Copyright (C) 2010 - 2024 Eluna Lua Engine <https://elunaluaengine.github.io/>
 * This program is free software licensed under GPL version 3
 * Please see the included DOCS/LICENSE.md for more information
 */

#include "Hooks.h"
#include "HookHelpers.h"
#include "LuaEngine.h"
#include "BindingMap.h"
#include "ElunaIncludes.h"
#include "ElunaTemplate.h"
#include "ElunaLoader.h"
#include <algorithm> // std::transform
#include <cstdlib> // strtol

using namespace Hooks;

#define START_HOOK(EVENT) \
    auto binding = GetBinding<EventKey<PlayerEvents>>(REGTYPE_PLAYER);\
    auto key = EventKey<PlayerEvents>(EVENT);\
    if (!binding->HasBindingsFor(key))\
        return;

#define START_HOOK_WITH_RETVAL(EVENT, RETVAL) \
    auto binding = GetBinding<EventKey<PlayerEvents>>(REGTYPE_PLAYER);\
    auto key = EventKey<PlayerEvents>(EVENT);\
    if (!binding->HasBindingsFor(key))\
        return RETVAL;

void Eluna::OnLearnTalents(Player* pPlayer, uint32 talentId, uint32 talentRank, uint32 spellid)
{
    START_HOOK(PLAYER_EVENT_ON_LEARN_TALENTS);
    HookPush(pPlayer);
    HookPush(talentId);
    HookPush(talentRank);
    HookPush(spellid);
    CallAllFunctions(binding, key);
}

void Eluna::OnSkillChange(Player* pPlayer, uint32 skillId, uint32 skillValue)
{
    START_HOOK(PLAYER_EVENT_ON_SKILL_CHANGE);
    HookPush(pPlayer);
    HookPush(skillId);
    HookPush(skillValue);
    int valueIndex = lua_gettop(L) - 1;
    int n = SetupStack(binding, key, 3);

    while (n > 0)
    {
        int r = CallOneFunction(n--, 3, 1);

        if (lua_isnumber(L, r))
        {
            skillValue = CHECKVAL<uint32>(r);
            // Update the stack for subsequent calls.
            ReplaceArgument(skillValue, valueIndex);
        }

        lua_pop(L, 1);
    }

    CleanUpStack(3);
}

void Eluna::OnLearnSpell(Player* pPlayer, uint32 spellId)
{
    START_HOOK(PLAYER_EVENT_ON_LEARN_SPELL);
    HookPush(pPlayer);
    HookPush(spellId);
    CallAllFunctions(binding, key);
}

bool Eluna::OnCommand(Player* player, const char* text)
{
    // If from console, player is NULL
    if (sElunaConfig->IsReloadCommandEnabled() && (!player || player->GetSession()->GetSecurity() >= sElunaConfig->GetReloadSecurityLevel()))
    {
        std::string reload = text;
        std::transform(reload.begin(), reload.end(), reload.begin(), ::tolower);
        const std::string reload_command = "reload eluna";
        if (reload.find(reload_command) == 0)
        {
            int mapId = RELOAD_ALL_STATES;
            std::string args = reload.substr(reload_command.length());
            if (!args.empty())
                mapId = strtol(args.c_str(), nullptr, 10);

            sElunaLoader->ReloadElunaForMap(mapId);

            return false;
        }
    }

    START_HOOK_WITH_RETVAL(PLAYER_EVENT_ON_COMMAND, true);
    HookPush(player);
    HookPush(text);
    return CallAllFunctionsBool(binding, key, true);
}

void Eluna::OnLootItem(Player* pPlayer, Item* pItem, uint32 count, ObjectGuid guid)
{
    START_HOOK(PLAYER_EVENT_ON_LOOT_ITEM);
    HookPush(pPlayer);
    HookPush(pItem);
    HookPush(count);
    HookPush(guid);
    CallAllFunctions(binding, key);
}

void Eluna::OnLootMoney(Player* pPlayer, uint32 amount)
{
    START_HOOK(PLAYER_EVENT_ON_LOOT_MONEY);
    HookPush(pPlayer);
    HookPush(amount);
    CallAllFunctions(binding, key);
}

void Eluna::OnFirstLogin(Player* pPlayer)
{
    START_HOOK(PLAYER_EVENT_ON_FIRST_LOGIN);
    HookPush(pPlayer);
    CallAllFunctions(binding, key);
}

void Eluna::OnRepop(Player* pPlayer)
{
    START_HOOK(PLAYER_EVENT_ON_REPOP);
    HookPush(pPlayer);
    CallAllFunctions(binding, key);
}

void Eluna::OnResurrect(Player* pPlayer)
{
    START_HOOK(PLAYER_EVENT_ON_RESURRECT);
    HookPush(pPlayer);
    CallAllFunctions(binding, key);
}

void Eluna::OnQuestAbandon(Player* pPlayer, uint32 questId)
{
    START_HOOK(PLAYER_EVENT_ON_QUEST_ABANDON);
    HookPush(pPlayer);
    HookPush(questId);
    CallAllFunctions(binding, key);
}

void Eluna::OnQuestStatusChanged(Player* pPlayer, uint32 questId, uint8 status)
{
    START_HOOK(PLAYER_EVENT_ON_QUEST_STATUS_CHANGED);
    HookPush(pPlayer);
    HookPush(questId);
    HookPush(status);
    CallAllFunctions(binding, key);
}

void Eluna::OnEquip(Player* pPlayer, Item* pItem, uint8 bag, uint8 slot)
{
    START_HOOK(PLAYER_EVENT_ON_EQUIP);
    HookPush(pPlayer);
    HookPush(pItem);
    HookPush(bag);
    HookPush(slot);
    CallAllFunctions(binding, key);
}

InventoryResult Eluna::OnCanUseItem(const Player* pPlayer, uint32 itemEntry)
{
    START_HOOK_WITH_RETVAL(PLAYER_EVENT_ON_CAN_USE_ITEM, EQUIP_ERR_OK);
    InventoryResult result = EQUIP_ERR_OK;
    HookPush(pPlayer);
    HookPush(itemEntry);
    int n = SetupStack(binding, key, 2);

    while (n > 0)
    {
        int r = CallOneFunction(n--, 2, 1);

        if (lua_isnumber(L, r))
            result = (InventoryResult)CHECKVAL<uint32>(r);

        lua_pop(L, 1);
    }

    CleanUpStack(2);
    return result;
}

// Lets a Lua handler refuse a vendor purchase.
// Returns -1 when the purchase may go on, otherwise the BuyResult code for the client.
// A handler refuses by returning false (BUY_ERR_CANT_FIND_ITEM) or a BuyResult number.
// pVendor is NULL (nil in Lua) for the player's fake inventory.
int32 Eluna::OnBuyItem(Player* pPlayer, Creature* pVendor, uint32 itemEntry, uint32 count)
{
    START_HOOK_WITH_RETVAL(PLAYER_EVENT_ON_BUY_ITEM, -1);
    int32 result = -1;
    HookPush(pPlayer);
    HookPush(pVendor);
    HookPush(itemEntry);
    HookPush(count);
    int n = SetupStack(binding, key, 4);

    while (n > 0)
    {
        int r = CallOneFunction(n--, 4, 1);

        // the first handler that refuses decides, the others are still called
        if (result < 0)
        {
            if (lua_isboolean(L, r))
            {
                if (!lua_toboolean(L, r))
                    result = 0;                                 // BUY_ERR_CANT_FIND_ITEM
            }
            else if (lua_isnumber(L, r))
            {
                switch (CHECKVAL<int32>(r))
                {
                    case 1:  result = 1;  break;                // BUY_ERR_ITEM_ALREADY_SOLD
                    case 2:  result = 2;  break;                // BUY_ERR_NOT_ENOUGHT_MONEY
                    case 4:  result = 4;  break;                // BUY_ERR_SELLER_DONT_LIKE_YOU
                    case 5:  result = 5;  break;                // BUY_ERR_DISTANCE_TOO_FAR
                    case 7:  result = 7;  break;                // BUY_ERR_ITEM_SOLD_OUT
                    case 8:  result = 8;  break;                // BUY_ERR_CANT_CARRY_MORE
                    case 11: result = 11; break;                // BUY_ERR_RANK_REQUIRE
                    case 12: result = 12; break;                // BUY_ERR_REPUTATION_REQUIRE
                    default: result = 0;  break;                // BUY_ERR_CANT_FIND_ITEM
                }
            }
        }

        lua_pop(L, 1);
    }

    CleanUpStack(4);
    return result;
}
void Eluna::OnPlayerEnterCombat(Player* pPlayer, Unit* pEnemy)
{
    START_HOOK(PLAYER_EVENT_ON_ENTER_COMBAT);
    HookPush(pPlayer);
    HookPush(pEnemy);
    CallAllFunctions(binding, key);
}

void Eluna::OnPlayerLeaveCombat(Player* pPlayer)
{
    START_HOOK(PLAYER_EVENT_ON_LEAVE_COMBAT);
    HookPush(pPlayer);
    CallAllFunctions(binding, key);
}

void Eluna::OnPVPKill(Player* pKiller, Player* pKilled)
{
    START_HOOK(PLAYER_EVENT_ON_KILL_PLAYER);
    HookPush(pKiller);
    HookPush(pKilled);
    CallAllFunctions(binding, key);
}

void Eluna::OnCreatureKill(Player* pKiller, Creature* pKilled)
{
    START_HOOK(PLAYER_EVENT_ON_KILL_CREATURE);
    HookPush(pKiller);
    HookPush(pKilled);
    CallAllFunctions(binding, key);
}

void Eluna::OnPlayerKilledByCreature(Creature* pKiller, Player* pKilled)
{
    START_HOOK(PLAYER_EVENT_ON_KILLED_BY_CREATURE);
    HookPush(pKiller);
    HookPush(pKilled);
    CallAllFunctions(binding, key);
}

void Eluna::OnPlayerKilledByEnvironment(Player* pKilled, uint8 damageType)
{
    START_HOOK(PLAYER_EVENT_ON_ENVIRONMENTAL_DEATH);
    HookPush(pKilled);
    HookPush(damageType);
    CallAllFunctions(binding, key);
}

void Eluna::OnLevelChanged(Player* pPlayer, uint8 oldLevel)
{
    START_HOOK(PLAYER_EVENT_ON_LEVEL_CHANGE);
    HookPush(pPlayer);
    HookPush(oldLevel);
    CallAllFunctions(binding, key);
}

void Eluna::OnFreeTalentPointsChanged(Player* pPlayer, uint32 newPoints)
{
    START_HOOK(PLAYER_EVENT_ON_TALENTS_CHANGE);
    HookPush(pPlayer);
    HookPush(newPoints);
    CallAllFunctions(binding, key);
}

void Eluna::OnTalentsReset(Player* pPlayer, bool noCost)
{
    START_HOOK(PLAYER_EVENT_ON_TALENTS_RESET);
    HookPush(pPlayer);
    HookPush(noCost);
    CallAllFunctions(binding, key);
}

void Eluna::OnMoneyChanged(Player* pPlayer, int32& amount)
{
    START_HOOK(PLAYER_EVENT_ON_MONEY_CHANGE);
    HookPush(pPlayer);
    HookPush(amount);
    int amountIndex = lua_gettop(L);
    int n = SetupStack(binding, key, 2);

    while (n > 0)
    {
        int r = CallOneFunction(n--, 2, 1);

        if (lua_isnumber(L, r))
        {
            amount = CHECKVAL<int32>(r);
            // Update the stack for subsequent calls.
            ReplaceArgument(amount, amountIndex);
        }

        lua_pop(L, 1);
    }

    CleanUpStack(2);
}

#if ELUNA_EXPANSION >= EXP_CATA
void Eluna::OnMoneyChanged(Player* pPlayer, int64& amount)
{
    START_HOOK(PLAYER_EVENT_ON_MONEY_CHANGE);
    HookPush(pPlayer);
    HookPush(amount);
    int amountIndex = lua_gettop(L);
    int n = SetupStack(binding, key, 2);

    while (n > 0)
    {
        int r = CallOneFunction(n--, 2, 1);

        if (lua_isnumber(L, r))
        {
            amount = CHECKVAL<int32>(r);
            // Update the stack for subsequent calls.
            ReplaceArgument(amount, amountIndex);
        }

        lua_pop(L, 1);
    }

    CleanUpStack(2);
}
#endif

void Eluna::OnGiveXP(Player* pPlayer, uint32& amount, Unit* pVictim)
{
    START_HOOK(PLAYER_EVENT_ON_GIVE_XP);
    HookPush(pPlayer);
    HookPush(amount);
    HookPush(pVictim);
    int amountIndex = lua_gettop(L) - 1;
    int n = SetupStack(binding, key, 3);

    while (n > 0)
    {
        int r = CallOneFunction(n--, 3, 1);

        if (lua_isnumber(L, r))
        {
            amount = CHECKVAL<uint32>(r);
            // Update the stack for subsequent calls.
            ReplaceArgument(amount, amountIndex);
        }

        lua_pop(L, 1);
    }

    CleanUpStack(3);
}

void Eluna::OnReputationChange(Player* pPlayer, uint32 factionID, int32& standing, bool incremental)
{
    START_HOOK(PLAYER_EVENT_ON_REPUTATION_CHANGE);
    HookPush(pPlayer);
    HookPush(factionID);
    HookPush(standing);
    HookPush(incremental);
    int standingIndex = lua_gettop(L) - 1;
    int n = SetupStack(binding, key, 4);

    while (n > 0)
    {
        int r = CallOneFunction(n--, 4, 1);

        if (lua_isnumber(L, r))
        {
            standing = CHECKVAL<int32>(r);
            // Update the stack for subsequent calls.
            ReplaceArgument(standing, standingIndex);
        }

        lua_pop(L, 1);
    }

    CleanUpStack(4);
}

void Eluna::OnDuelRequest(Player* pTarget, Player* pChallenger)
{
    START_HOOK(PLAYER_EVENT_ON_DUEL_REQUEST);
    HookPush(pTarget);
    HookPush(pChallenger);
    CallAllFunctions(binding, key);
}

void Eluna::OnDuelStart(Player* pStarter, Player* pChallenger)
{
    START_HOOK(PLAYER_EVENT_ON_DUEL_START);
    HookPush(pStarter);
    HookPush(pChallenger);
    CallAllFunctions(binding, key);
}

void Eluna::OnDuelEnd(Player* pWinner, Player* pLoser, DuelCompleteType type)
{
    START_HOOK(PLAYER_EVENT_ON_DUEL_END);
    HookPush(pWinner);
    HookPush(pLoser);
    HookPush(type);
    CallAllFunctions(binding, key);
}

void Eluna::OnEmote(Player* pPlayer, uint32 emote)
{
    START_HOOK(PLAYER_EVENT_ON_EMOTE);
    HookPush(pPlayer);
    HookPush(emote);
    CallAllFunctions(binding, key);
}

void Eluna::OnTextEmote(Player* pPlayer, uint32 textEmote, uint32 emoteNum, ObjectGuid guid)
{
    START_HOOK(PLAYER_EVENT_ON_TEXT_EMOTE);
    HookPush(pPlayer);
    HookPush(textEmote);
    HookPush(emoteNum);
    HookPush(guid);
    CallAllFunctions(binding, key);
}

void Eluna::OnSpellCast(Player* pPlayer, Spell* pSpell, bool skipCheck)
{
    START_HOOK(PLAYER_EVENT_ON_SPELL_CAST);
    HookPush(pPlayer);
    HookPush(pSpell);
    HookPush(skipCheck);
    CallAllFunctions(binding, key);
}

void Eluna::OnLogin(Player* pPlayer)
{
    START_HOOK(PLAYER_EVENT_ON_LOGIN);
    HookPush(pPlayer);
    CallAllFunctions(binding, key);
}

void Eluna::OnLogout(Player* pPlayer)
{
    START_HOOK(PLAYER_EVENT_ON_LOGOUT);
    HookPush(pPlayer);
    CallAllFunctions(binding, key);
}

void Eluna::OnCreate(Player* pPlayer)
{
    START_HOOK(PLAYER_EVENT_ON_CHARACTER_CREATE);
    HookPush(pPlayer);
    CallAllFunctions(binding, key);
}

void Eluna::OnDelete(uint32 guidlow)
{
    START_HOOK(PLAYER_EVENT_ON_CHARACTER_DELETE);
    HookPush(guidlow);
    CallAllFunctions(binding, key);
}

void Eluna::OnSave(Player* pPlayer)
{
    START_HOOK(PLAYER_EVENT_ON_SAVE);
    HookPush(pPlayer);
    CallAllFunctions(binding, key);
}

void Eluna::OnBindToInstance(Player* pPlayer, Difficulty difficulty, uint32 mapid, bool permanent)
{
    START_HOOK(PLAYER_EVENT_ON_BIND_TO_INSTANCE);
    HookPush(pPlayer);
    HookPush(difficulty);
    HookPush(mapid);
    HookPush(permanent);
    CallAllFunctions(binding, key);
}

void Eluna::OnUpdateZone(Player* pPlayer, uint32 newZone, uint32 newArea)
{
    START_HOOK(PLAYER_EVENT_ON_UPDATE_ZONE);
    HookPush(pPlayer);
    HookPush(newZone);
    HookPush(newArea);
    CallAllFunctions(binding, key);
}

void Eluna::OnUpdateArea(Player* pPlayer, uint32 oldArea, uint32 newArea)
{
    START_HOOK(PLAYER_EVENT_ON_UPDATE_AREA);
    HookPush(pPlayer);
    HookPush(oldArea);
    HookPush(newArea);
    CallAllFunctions(binding, key);
}

void Eluna::OnMapChanged(Player* player)
{
    START_HOOK(PLAYER_EVENT_ON_MAP_CHANGE);
    HookPush(player);
    CallAllFunctions(binding, key);
}

void Eluna::OnAchievementComplete(Player* player, uint32 achievementId)
{
    START_HOOK(PLAYER_EVENT_ON_ACHIEVEMENT_COMPLETE);
    HookPush(player);
    HookPush(achievementId);
    CallAllFunctions(binding, key);
}

bool Eluna::OnTradeInit(Player* trader, Player* tradee)
{
    START_HOOK_WITH_RETVAL(PLAYER_EVENT_ON_TRADE_INIT, true);
    HookPush(trader);
    HookPush(tradee);
    return CallAllFunctionsBool(binding, key, true);
}

bool Eluna::OnTradeAccept(Player* trader, Player* tradee)
{
    START_HOOK_WITH_RETVAL(PLAYER_EVENT_ON_TRADE_ACCEPT, true);
    HookPush(trader);
    HookPush(tradee);
    return CallAllFunctionsBool(binding, key, true);
}

bool Eluna::OnSendMail(Player* sender, ObjectGuid recipientGuid)
{
    START_HOOK_WITH_RETVAL(PLAYER_EVENT_ON_SEND_MAIL, true);
    HookPush(sender);
    HookPush(recipientGuid);
    return CallAllFunctionsBool(binding, key, true);
}

void Eluna::OnDiscoverArea(Player* player, uint32 area)
{
    START_HOOK(PLAYER_EVENT_ON_DISCOVER_AREA);
    HookPush(player);
    HookPush(area);
    CallAllFunctions(binding, key);
}

bool Eluna::OnChat(Player* pPlayer, uint32 type, uint32 lang, std::string& msg)
{
    if (lang == LANG_ADDON)
        return OnAddonMessage(pPlayer, type, msg, NULL, NULL, NULL, NULL);

    START_HOOK_WITH_RETVAL(PLAYER_EVENT_ON_CHAT, true);
    bool result = true;
    HookPush(pPlayer);
    HookPush(msg);
    HookPush(type);
    HookPush(lang);
    int n = SetupStack(binding, key, 4);

    while (n > 0)
    {
        int r = CallOneFunction(n--, 4, 2);

        if (lua_isboolean(L, r + 0) && !lua_toboolean(L, r + 0))
            result = false;

        if (lua_isstring(L, r + 1))
            msg = std::string(lua_tostring(L, r + 1));

        lua_pop(L, 2);
    }

    CleanUpStack(4);
    return result;
}

bool Eluna::OnChat(Player* pPlayer, uint32 type, uint32 lang, std::string& msg, Group* pGroup)
{
    if (lang == LANG_ADDON)
        return OnAddonMessage(pPlayer, type, msg, NULL, NULL, pGroup, NULL);

    START_HOOK_WITH_RETVAL(PLAYER_EVENT_ON_GROUP_CHAT, true);
    bool result = true;
    HookPush(pPlayer);
    HookPush(msg);
    HookPush(type);
    HookPush(lang);
    HookPush(pGroup);
    int n = SetupStack(binding, key, 5);

    while (n > 0)
    {
        int r = CallOneFunction(n--, 5, 2);

        if (lua_isboolean(L, r + 0) && !lua_toboolean(L, r + 0))
            result = false;

        if (lua_isstring(L, r + 1))
            msg = std::string(lua_tostring(L, r + 1));

        lua_pop(L, 2);
    }

    CleanUpStack(5);
    return result;
}

bool Eluna::OnChat(Player* pPlayer, uint32 type, uint32 lang, std::string& msg, Guild* pGuild)
{
    if (lang == LANG_ADDON)
        return OnAddonMessage(pPlayer, type, msg, NULL, pGuild, NULL, NULL);

    START_HOOK_WITH_RETVAL(PLAYER_EVENT_ON_GUILD_CHAT, true);
    bool result = true;
    HookPush(pPlayer);
    HookPush(msg);
    HookPush(type);
    HookPush(lang);
    HookPush(pGuild);
    int n = SetupStack(binding, key, 5);

    while (n > 0)
    {
        int r = CallOneFunction(n--, 5, 2);

        if (lua_isboolean(L, r + 0) && !lua_toboolean(L, r + 0))
            result = false;

        if (lua_isstring(L, r + 1))
            msg = std::string(lua_tostring(L, r + 1));

        lua_pop(L, 2);
    }

    CleanUpStack(5);
    return result;
}

bool Eluna::OnChat(Player* pPlayer, uint32 type, uint32 lang, std::string& msg, Channel* pChannel)
{
    if (lang == LANG_ADDON)
        return OnAddonMessage(pPlayer, type, msg, NULL, NULL, NULL, pChannel);

    START_HOOK_WITH_RETVAL(PLAYER_EVENT_ON_CHANNEL_CHAT, true);
    bool result = true;
    HookPush(pPlayer);
    HookPush(msg);
    HookPush(type);
    HookPush(lang);
    HookPush(pChannel->GetChannelId());
    int n = SetupStack(binding, key, 5);

    while (n > 0)
    {
        int r = CallOneFunction(n--, 5, 2);

        if (lua_isboolean(L, r + 0) && !lua_toboolean(L, r + 0))
            result = false;

        if (lua_isstring(L, r + 1))
            msg = std::string(lua_tostring(L, r + 1));

        lua_pop(L, 2);
    }

    CleanUpStack(5);
    return result;
}

bool Eluna::OnChat(Player* pPlayer, uint32 type, uint32 lang, std::string& msg, Player* pReceiver)
{
    if (lang == LANG_ADDON)
        return OnAddonMessage(pPlayer, type, msg, pReceiver, NULL, NULL, NULL);

    START_HOOK_WITH_RETVAL(PLAYER_EVENT_ON_WHISPER, true);
    bool result = true;
    HookPush(pPlayer);
    HookPush(msg);
    HookPush(type);
    HookPush(lang);
    HookPush(pReceiver);
    int n = SetupStack(binding, key, 5);

    while (n > 0)
    {
        int r = CallOneFunction(n--, 5, 2);

        if (lua_isboolean(L, r + 0) && !lua_toboolean(L, r + 0))
            result = false;

        if (lua_isstring(L, r + 1))
            msg = std::string(lua_tostring(L, r + 1));

        lua_pop(L, 2);
    }

    CleanUpStack(5);
    return result;
}

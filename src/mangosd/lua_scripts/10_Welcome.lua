--[[
    10_Welcome.lua - the welcome window at login  (round 8)
    =====================================================
    When a character enters the world this script:

      1. greets the player with a message and shows his own character
         data (name, level, class, race, VIP level, Magic Gold, gold,
         honorable kills, played time, players online),
      2. opens a GOSSIP window right away titled "Welcome" that shows the
         server information text (npc_text entry WELCOME_NPC_TEXT below,
         inserted by sql/R8_world_npc_text_welcome.sql - edit the text in
         your world database to fit your server) with a "My Character
         Info" and a "Close" button.

    The hearthstone homebind move to the PvP zone is handled by the core
    (GlobalPvP.SetHomebind in mangosd.conf) and only mentioned here.
]]

-- ===========================================================================
-- CONFIGURATION
-- ===========================================================================

local SERVER_NAME    = "My Fun Server"   -- shown in the welcome messages
local WELCOME_NPC_TEXT = 60000           -- npc_text entry with the server info
local WELCOME_MENU_ID = 8001             -- player gossip menu id of this window

-- ===========================================================================
-- end of CONFIGURATION
-- ===========================================================================

local PLAYER_EVENT_ON_LOGIN = (events and events.player and events.player.on_login) or 3
local GOSSIP_EVENT_ON_SELECT = 2

-- gossip icons
local ICON_TALK   = 7
local ICON_BATTLE = 9
local ICON_CHAT   = 0

-- class and race names (client ids -> readable text)
local CLASS_NAMES =
{
    [1]  = "Warrior",
    [2]  = "Paladin",
    [3]  = "Hunter",
    [4]  = "Rogue",
    [5]  = "Priest",
    [7]  = "Shaman",
    [8]  = "Mage",
    [9]  = "Warlock",
    [11] = "Druid",
}

local RACE_NAMES =
{
    [1]  = "Human",
    [2]  = "Orc",
    [3]  = "Dwarf",
    [4]  = "Night Elf",
    [5]  = "Undead",
    [6]  = "Tauren",
    [7]  = "Gnome",
    [8]  = "Troll",
    [10] = "Blood Elf",
    [11] = "Draenei",
}

local function FormatMoney(copper)
    copper = copper or 0
    local gold = math.floor(copper / 10000)
    local silver = math.floor((copper % 10000) / 100)
    local bronze = copper % 100
    return string.format("|cffffff00%d|r gold |cffffff00%d|r silver |cffffff00%d|r copper", gold, silver, bronze)
end

local function FormatPlayed(seconds)
    seconds = seconds or 0
    local hours = math.floor(seconds / 3600)
    local minutes = math.floor((seconds % 3600) / 60)
    return string.format("%dh %02dm", hours, minutes)
end

-- the character data of the player as chat lines
local function SendCharacterInfo(player)
    local name = player:GetName()
    local level = player:GetLevel()
    local class = CLASS_NAMES[player:GetClass()] or tostring(player:GetClass())
    local race = RACE_NAMES[player:GetRace()] or tostring(player:GetRace())
    local vip = player:GetVIPLevel()
    local mg = player:GetMagicGold()

    player:SendBroadcastMessage(string.format("|cff00ccff==================== |cffffff00Welcome to %s!|cff00ccff ====================|r", SERVER_NAME))
    player:SendBroadcastMessage(string.format("|cff00ccff[You]|r |cffffff00%s|r - level |cffffff00%d|r |cffffff00%s %s|r", name, level, race, class))

    if vip > 0 then
        player:SendBroadcastMessage(string.format("|cff00ccff[You]|r VIP level: |cffffff00%d|r   -   |cffffff00%d|r |cff00ccffMagic Gold|r", vip, mg))
    else
        player:SendBroadcastMessage(string.format("|cff00ccff[You]|r |cffffff00%d|r |cff00ccffMagic Gold|r - earn more by killing other players!", mg))
    end

    player:SendBroadcastMessage(string.format("|cff00ccff[You]|r Money: %s   -   Honorable kills: |cffffff00%d|r",
        FormatMoney(player:GetCoinage()), player:GetLifetimeKills()))
    player:SendBroadcastMessage(string.format("|cff00ccff[You]|r Played time: |cffffff00%s|r   -   Players online: |cffffff00%d|r",
        FormatPlayed(player:GetTotalPlayedTime()), GetPlayerCount()))

    -- the PvP zone hint while it is enabled
    local ok, enabled = pcall(GetGlobalPvPInfo)
    if ok and enabled then
        player:SendBroadcastMessage("|cff00ccff[PvP]|r |cffff6600Free-for-all PvP zone|r is open - use your Hearthstone menu (Global GPS Teleport > PvP Free-For-All). Everyone can attack everyone there!")
        player:SendBroadcastMessage("|cff00ccff[PvP]|r Your Hearthstone is bound to the PvP zone - cast it any time to jump back into the fight.")
    end

    player:SendBroadcastMessage("|cff00ccff[Tip]|r Right-click your |cffffff00Hearthstone|r for all services: GPS, bank, shops, morph, class trainer and more.")
end

-- the welcome gossip window -----------------------------------------------

local function OpenWelcomeGossip(player)
    player:GossipClearMenu()
    player:GossipMenuAddItem(ICON_CHAT, "My Character Info", 0, WELCOME_MENU_ID)
    player:GossipMenuAddItem(ICON_BATTLE, "Close", 0, WELCOME_MENU_ID + 1)
    player:GossipSendMenu(WELCOME_NPC_TEXT, player, WELCOME_MENU_ID)
end

-- menu id constants for the options above
local OPT_INFO  = WELCOME_MENU_ID      -- 8001: show the character info again
local OPT_CLOSE = WELCOME_MENU_ID + 1  -- 8002: close the window

local function OnWelcomeSelect(event, player, sender, intid)
    if intid == OPT_INFO then
        SendCharacterInfo(player)
        OpenWelcomeGossip(player)
    elseif intid == OPT_CLOSE then
        player:GossipComplete()
    end
end

RegisterPlayerGossipEvent(WELCOME_MENU_ID, GOSSIP_EVENT_ON_SELECT, OnWelcomeSelect)

-- login ---------------------------------------------------------------------

local function OnLogin(event, player)
    SendCharacterInfo(player)

    -- open the welcome gossip right away - the client is fully in world
    -- when the login event fires
    OpenWelcomeGossip(player)
end

RegisterPlayerEvent(PLAYER_EVENT_ON_LOGIN, OnLogin)

print(string.format("[Welcome] loaded - welcome gossip (npc_text %d) + character info at login", WELCOME_NPC_TEXT))

--[[
    70_Hearthstone.lua - the pocket menu on the Hearthstone  (round 8)
    =================================================================
    Right clicking the Hearthstone (item 6948) opens a gossip menu instead of
    casting the hearth spell. The menu offers:

        Global GPS Teleport   - teleport to cities, hot spots and the PvP zone
        Hearthstone           - the real hearth cast (normal cooldown)
        Pocket Bank           - open your bank from anywhere
        Action Center         - extra services, see "PLUGGING IN" below
        Gear Sale             - a vendor window: sell your loot anywhere
        Gear Repair           - repair all gear (normal gold cost)
        Learn Skill           - the CLASS TRAINER page (spells + weapon skills)
                                and riding skills (MG)
        VIP Service           - VIP status, Morph My Gear, VIP shops,
                                buy VIP levels with Magic Gold
        Other Service         - remove res sickness, reset talents, unstuck

    Round 8 changes (see the matching core patches):
      * EVERY purchase now asks for a confirmation first:
          - gossip buys pop up the built-in Accept/Cancel dialog,
          - window buys (VIP shops, morph looks) are refused once, a small
            confirm dialog with the full price info opens, and only "Accept"
            runs the purchase.
      * Morph My Gear: the slots now show their item ICONS and a "morphed"
        tag. Clicking a slot opens the LOOK PREVIEW WINDOW (item icons
        included); clicking a look there opens the confirmation, and after
        accepting the look is applied as a FAKE INVENTORY purchase: the shown
        item is NEVER really added to the bags, only its look is used.
      * Rental shop items are marked: the confirmation and the purchase
        messages carry a clearly visible RENTAL tag with the rental time.
      * Learn Skill opens the character's own spell trainer page (all class
        spells and the weapon/shield skills of the class, normal gold prices)
        - no profession / "work" lists anymore, riding stays in the menu.
      * VIP Service: VIP levels can be BOUGHT with Magic Gold, one level at a
        time and with steep prices (VIP_PRICES below) so it is never cheap.
      * GPS: the free-for-all PvP zone (GlobalPvP.* in mangosd.conf) is a
        free destination while it is enabled.

    Everything runs on Eluna + the core patches of rounds 7/8:
        Player:GetMagicGold() / Player:ModifyMagicGold(amount, announce)
        Player:GetVIPLevel() / Player:SetVIPLevel(level)
        Player:ItemMorphSlot(slot, displayId, expireSeconds, persist)
        Player:GetItemVisualOverrideEntry(slot)
        Player:SendClassTrainerList() / Player:BuyInventoryMenuItem(item, cnt)
        Item:SetItemExpiry(seconds) / Item:GetItemExpiry()
        PLAYER_EVENT_ON_BUY_ITEM (55) and PLAYER_EVENT_ON_BUY_ITEM_COMPLETE (57)

    Magic Gold (MG) is earned by killing other characters (MagicGold.* in
    mangosd.conf). Prices and requirements live in the tables right below.

    PLUGGING IN YOUR OWN "Action Center" SERVICES
    ---------------------------------------------
    Any script can add entries to the Action Center menu:

        HearthstoneMenu.Register("My Service", 7, function(player)
            player:SendBroadcastMessage("hello from my service")
        end)

    (name, gossip icon id, callback). If your server runs the ActionSystem
    framework, put its main menu entry point into OpenActionCenter() below
    and the whole ActionSystem appears inside this menu.
]]

-- ===========================================================================
-- CONFIGURATION - edit everything in this block to taste
-- ===========================================================================

local HEARTHSTONE_ENTRY = 6948      -- the item that opens this menu
local HEARTHSTONE_SPELL = 8690      -- the real hearth spell (with cooldown)
local NPC_TEXT_ID       = 100       -- gossip header text id (standard "Greetings")

-- Global GPS Teleport: { name, map, x, y, z, o, mg, minVip }
-- mg = Magic Gold price per port, minVip = required VIP level.
-- The free-for-all PvP zone (GlobalPvP.* in mangosd.conf) is appended
-- automatically as its own category while it is enabled.
local GPS =
{
    ["Cities"] =
    {
        { name = "Stormwind",     map = 0,    x = -8913.23, y = 554.63,   z = 93.79,   o = 0, mg = 5,  minVip = 0 },
        { name = "Ironforge",     map = 0,    x = -4981.88, y = -880.21,  z = 501.66,  o = 0, mg = 5,  minVip = 0 },
        { name = "Darnassus",     map = 1,    x = 9952.59,  y = 2280.32,  z = 1341.39, o = 0, mg = 5,  minVip = 0 },
        { name = "Exodar",        map = 530,  x = -4030.33, y = -11574.4, z = -138.09, o = 0, mg = 5,  minVip = 0 },
        { name = "Orgrimmar",     map = 1,    x = 1484.72,  y = -4417.54, z = 24.48,   o = 0, mg = 5,  minVip = 0 },
        { name = "Thunder Bluff", map = 1,    x = -1290.42, y = 147.11,   z = 129.66,  o = 0, mg = 5,  minVip = 0 },
        { name = "Undercity",     map = 0,    x = 1815.14,  y = 238.79,   z = 59.84,   o = 0, mg = 5,  minVip = 0 },
        { name = "Silvermoon",    map = 530,  x = 9808.13,  y = -7242.13, z = 14.34,   o = 0, mg = 5,  minVip = 0 },
        { name = "Shattrath",     map = 530,  x = -1879.46, y = 5431.61,  z = -10.36,  o = 0, mg = 10, minVip = 0 },
    },
    ["Hot Spots"] =
    {
        { name = "Karazhan",          map = 0,   x = -11100.0, y = -2004.0, z = 49.89,  o = 0, mg = 15, minVip = 0 },
        { name = "Black Temple",      map = 530, x = -3610.72, y = 328.25,  z = 37.29,  o = 0, mg = 15, minVip = 0 },
        { name = "Hyjal Summit",      map = 534, x = 4262.81,  y = -4292.42, z = 95.60, o = 0, mg = 15, minVip = 0 },
        { name = "Isle of Quel'Danas", map = 530, x = 11783.0,  y = -7069.0, z = 14.0,   o = 0, mg = 20, minVip = 0 },
    },
}

-- Learn Skill: riding skills with Magic Gold prices.
-- { name, spellId, mg, minLevel }  (spellId = the spell a trainer teaches)
-- NOTE: class spells and weapon skills are NOT listed here anymore - the
-- "Open my Class Trainer" entry opens the real trainer window for them.
local LEARN_SKILLS =
{
    { name = "Apprentice Riding (skill 75)",  spellId = 33388, mg = 200,  minLevel = 30 },
    { name = "Journeyman Riding (skill 150)", spellId = 33391, mg = 1000, minLevel = 60 },
}

-- Gear Sale: a few items the pocket vendor "sells" so the window opens. The
-- window is mainly there so you can SELL your loot anywhere (full gold price).
local GEAR_SALE_ITEMS =
{
    { item = 4497, price = 250 },    -- bread / water style goods, adjust freely
    { item = 4498, price = 250 },
    { item = 8952, price = 1250 },
}

-- VIP item shops: { item = itemEntry, mg = price, vip = required VIP level,
--                   expire = rental seconds (0 = forever) }
-- Items with expire > 0 are RENTALS: every message and confirmation shows a
-- clearly visible RENTAL tag and the rental time under the item description.
-- EXAMPLE DATA - replace with your own catalogue.
local VIP_SHOPS =
{
    ["Armory"] =
    {
        { item = 32458, mg = 5000,  vip = 0, expire = 0 },    -- Ashes of Al'alar
    },
    ["Weapons"] =
    {
        { item = 19019, mg = 15000, vip = 2, expire = 0 },    -- Thunderfury
        { item = 32837, mg = 20000, vip = 3, expire = 0 },    -- Warglaive of Azzinoth
        { item = 32838, mg = 20000, vip = 3, expire = 0 },
    },
    ["Bows"] =
    {
        { item = 34334, mg = 25000, vip = 4, expire = 0 },    -- Thori'dal
    },
    ["Jewelry"] =
    {
        { item = 29460, mg = 3000,  vip = 1, expire = 0 },
    },
    ["Rental Gear (24h)"] =
    {
        -- expire = 86400: the core removes the item after one day
        { item = 33479, mg = 800,  vip = 1, expire = 86400 },
    },
}

-- Morph My Gear: available looks, per slot group.
-- { item = itemEntry whose look is used, mg = price, vip = required level,
--   expire = how long the look lasts in seconds (0 = forever) }
-- The looks are browsed in a real item window (icons included) - the bought
-- look is a FAKE INVENTORY purchase: nothing is really added to the bags.
local MORPH_LOOKS =
{
    ["Main Hand"] =
    {
        { item = 19019, mg = 250,  vip = 1, expire = 604800 },  -- 7 days
        { item = 32837, mg = 500,  vip = 2, expire = 604800 },
    },
    ["Off Hand"] =
    {
        { item = 32838, mg = 500,  vip = 2, expire = 604800 },
    },
    ["Head"] =
    {
        -- add helmet item entries here, example:
        -- { item = 31087, mg = 200, vip = 1, expire = 604800 },
    },
    ["Back"] =
    {
        -- { item = 28774, mg = 150, vip = 1, expire = 604800 },
    },
}

-- which equipment slots the morph service offers
local MORPH_SLOTS =
{
    { label = "Main Hand", slot = 15 },
    { label = "Off Hand",  slot = 16 },
    { label = "Ranged",    slot = 17 },
    { label = "Head",      slot = 0 },
    { label = "Shoulders", slot = 2 },
    { label = "Back",      slot = 14 },
    { label = "Chest",     slot = 4 },
    { label = "Tabard",    slot = 18 },
}

-- VIP Service: buying VIP levels with Magic Gold. Levels must be bought one
-- after another (no skipping), each level only ever gets more expensive - so
-- a high VIP rank is a real long term goal. Index = the level you BUY.
local VIP_PRICES =
{
    [1]  = 50000,
    [2]  = 150000,
    [3]  = 400000,
    [4]  = 900000,
    [5]  = 1500000,
    [6]  = 2500000,
    [7]  = 3750000,
    [8]  = 5000000,
    [9]  = 7500000,
    [10] = 10000000,
}

-- Other Service prices
local RES_SICKNESS_COST = 25        -- MG to remove resurrection sickness
local UNSTUCK_COST      = 10        -- MG to teleport to your homebind

-- ===========================================================================
-- end of CONFIGURATION
-- ===========================================================================

-- event ids (prefer the engine provided "events" table)
local PLAYER_EVENT_ON_LOGIN             = (events and events.player and events.player.on_login) or 3
local PLAYER_EVENT_ON_BUY_ITEM          = (events and events.player and events.player.on_buy_item) or 55
local PLAYER_EVENT_ON_BUY_ITEM_COMPLETE = (events and events.player and events.player.on_buy_item_complete) or 57

-- gossip event ids
local GOSSIP_EVENT_ON_HELLO  = 1
local GOSSIP_EVENT_ON_SELECT = 2

-- equipment bag constant (INVENTORY_SLOT_BAG_0)
local BAG_EQUIPMENT = 255

-- menu id of the confirmation gossip (player gossip, see RegisterPlayerGossipEvent)
local CONFIRM_MENU_ID = 7001

-- action id namespace (all gossip "intid"s live at 70000+, sender is always
-- the same, one dispatcher sorts everything out by range)
local A = {
    BACK        = 70999,
    CLOSE       = 70998,
    -- main menu entries
    GPS         = 70001,
    HEARTH      = 70002,
    BANK        = 70003,
    ACTIONS     = 70004,
    SALE        = 70005,
    REPAIR      = 70006,
    SKILLS      = 70007,
    VIP         = 70008,
    OTHER       = 70009,
    -- gps: 71000 + category, 71100 + category*100 + destination
    GPS_CAT     = 71000,
    GPS_DEST    = 71100,
    -- action center: 70500 + entry
    ACTIONS_ENT = 70500,
    -- vip: 72000 status, 72001 morph picker, 72100 + shop category,
    --      72200 + slot, 74000 + slot (restore), 72050 buy vip level
    VIP_STATUS  = 72000,
    VIP_MORPH   = 72001,
    VIP_SHOP    = 72100,
    VIP_BUY     = 72050,
    MORPH_SLOT  = 72200,
    MORPH_RESET = 74000,
    -- other service: 76000 + entry
    OTHER_ENT   = 76000,
    -- learn skill: 75000 + entry, 75060 class trainer
    SKILL_ENT   = 75000,
    SKILL_TRAIN = 75060,
}

-- confirmation gossip actions (CONFIRM_MENU_ID namespace)
local C = {
    ACCEPT = 73001,
    CANCEL = 73002,
}

-- gossip icon ids
local ICON_CHAT   = 0
local ICON_VENDOR = 1
local ICON_TAXI   = 2
local ICON_TRAIN  = 3
local ICON_MONEY  = 6
local ICON_TALK   = 7
local ICON_TABARD = 8
local ICON_BATTLE = 9

-- ------------------------------------------------------------- helpers ----

local function IconMarkup(entry, size)
    local ok, icon = pcall(GetItemIcon, entry)
    if ok and icon and icon ~= "" then
        return string.format("|TInterface\\Icons\\%s:%d:%d|t", icon, size or 18, size or 18)
    end
    return ""
end

local function ItemLink(entry)
    local ok, link = pcall(GetItemLink, entry)
    if ok and link and link ~= "" then
        return link
    end
    return "item #" .. tostring(entry)
end

local function MG(amount)
    return string.format("|cffffff00%d|r |cff00ccffMagic Gold|r", amount)
end

local function Send(player, msg)
    player:SendBroadcastMessage(msg)
end

-- the RENTAL tag shown under/next to rental items (expire > 0)
local function RentalTag(expireSeconds)
    if expireSeconds and expireSeconds > 0 then
        return string.format("|cffff8800[RENTAL - removed after %d day(s)]|r", math.ceil(expireSeconds / 86400))
    end
    return "|cff00ff00[permanent - yours to keep]|r"
end

-- -------------------------------------------------------- window state ----
-- which fake inventory window the player has open (per character guid low):
--   { kind = "morph", slotIdx = i }  - the morph look preview of a slot
--   { kind = "shop",  category = s } - a VIP shop
local openWindow = {}

-- a purchase waiting for the player's Accept in the confirm dialog
local pending = {}

-- purchases the player already confirmed: the window buy is re-sent through
-- Player:BuyInventoryMenuItem and allowed through by the ON_BUY_ITEM hook
local confirmed = {}

-- ------------------------------------------------------ action center ----

-- global registry other scripts can hook into:
-- HearthstoneMenu.Register("name", iconId, function(player) ... end)
HearthstoneMenu = HearthstoneMenu or { entries = {} }

function HearthstoneMenu.Register(name, icon, callback)
    table.insert(HearthstoneMenu.entries, { name = name, icon = icon or ICON_TALK, fn = callback })
end

-- If your server runs the ActionSystem framework, put its main menu entry
-- point in here (pcall keeps this script alive when the framework is absent).
local function OpenActionCenter(player, item)
    if ActionSystem ~= nil then
        for _, fnName in ipairs({ "OpenMainMenu", "ShowMainMenu", "MainMenu", "OpenMain", "ShowMain" }) do
            local fn = ActionSystem[fnName]
            if type(fn) == "function" then
                local ok, err = pcall(fn, player)
                if ok then
                    return
                end
                print("[Hearthstone] ActionSystem." .. fnName .. " failed: " .. tostring(err))
            end
        end
    end

    -- built-in: everything registered through HearthstoneMenu.Register
    player:GossipClearMenu()

    if #HearthstoneMenu.entries == 0 then
        player:GossipMenuAddItem(ICON_CHAT, "No extra services registered yet.", A.CLOSE, A.CLOSE)
    else
        for i, entry in ipairs(HearthstoneMenu.entries) do
            player:GossipMenuAddItem(entry.icon, entry.name, A.ACTIONS_ENT, A.ACTIONS_ENT + i)
        end
    end

    player:GossipMenuAddItem(ICON_TALK, "Back", A.BACK, A.BACK)
    player:GossipSendMenu(NPC_TEXT_ID, item)
end

-- --------------------------------------------------------------- menus ----

local function MainMenu(player, item)
    player:GossipClearMenu()
    player:GossipMenuAddItem(ICON_TAXI,   "Global GPS Teleport", A.GPS, A.GPS)
    player:GossipMenuAddItem(ICON_CHAT,   "Hearthstone",         A.HEARTH, A.HEARTH)
    player:GossipMenuAddItem(ICON_MONEY,  "Pocket Bank",         A.BANK, A.BANK)
    player:GossipMenuAddItem(ICON_TALK,   "Action Center",       A.ACTIONS, A.ACTIONS)
    player:GossipMenuAddItem(ICON_VENDOR, "Gear Sale",           A.SALE, A.SALE)
    player:GossipMenuAddItem(ICON_TRAIN,  "Gear Repair",         A.REPAIR, A.REPAIR)
    player:GossipMenuAddItem(ICON_TRAIN,  "Learn Skill",         A.SKILLS, A.SKILLS)
    player:GossipMenuAddItem(ICON_TABARD, "VIP Service",         A.VIP, A.VIP)
    player:GossipMenuAddItem(ICON_TALK,   "Other Service",       A.OTHER, A.OTHER)
    player:GossipMenuAddItem(ICON_BATTLE, "Close",               A.CLOSE, A.CLOSE)
    player:GossipSendMenu(NPC_TEXT_ID, item)
end

-- GPS -----------------------------------------------------------------------

-- append the PvP free-for-all zone as its own category while it is enabled
local pvpEnabled, pvpMap, pvpZone, pvpArea, pvpX, pvpY, pvpZ, pvpO = false, 0, 0, 0, 0, 0, 0, 0
do
    local ok, en, m, z, a, x, y, zz, o = pcall(GetGlobalPvPInfo)
    if ok and en then
        pvpEnabled, pvpMap, pvpZone, pvpArea, pvpX, pvpY, pvpZ, pvpO = true, m, z, a, x, y, zz, o
        GPS["PvP Free-For-All"] =
        {
            { name = "PvP Zone (Old Hillsbrad)", map = m, x = x, y = y, z = zz, o = o, mg = 0, minVip = 0 },
        }
    end
end

local gpsCategoryNames = {}
for name in pairs(GPS) do
    table.insert(gpsCategoryNames, name)
end
table.sort(gpsCategoryNames)

-- which GPS category this player has open (guid low -> category name)
local openGpsCategory = {}

local function GpsCategoryMenu(player, item)
    player:GossipClearMenu()
    for i, name in ipairs(gpsCategoryNames) do
        player:GossipMenuAddItem(ICON_TAXI, name, A.GPS_CAT, A.GPS_CAT + i)
    end
    player:GossipMenuAddItem(ICON_TALK, "Back", A.BACK, A.BACK)
    player:GossipSendMenu(NPC_TEXT_ID, item)
end

local function GpsMenu(player, item, category)
    player:GossipClearMenu()

    local list = GPS[category]
    if list then
        for i, dest in ipairs(list) do
            local vipTag = dest.minVip > 0 and string.format(" (VIP %d+)", dest.minVip) or ""
            local priceTag = dest.mg > 0 and string.format(" - %d MG", dest.mg) or " - free"
            local label = string.format("%s%s%s", dest.name, priceTag, vipTag)
            local popup = string.format("Teleport to |cffffff00%s|r for |cffffff00%s|r?", dest.name,
                                        dest.mg > 0 and (dest.mg .. " Magic Gold") or "free")
            player:GossipMenuAddItem(ICON_TAXI, label, A.GPS_DEST, A.GPS_DEST + i, false, popup, 0)
        end
    end

    player:GossipMenuAddItem(ICON_TALK, "Back", A.BACK, A.BACK)
    player:GossipSendMenu(NPC_TEXT_ID, item)
end

local function DoGpsTeleport(player, category, index)
    local dest = GPS[category] and GPS[category][index]
    if not dest then
        return
    end

    if player:IsInCombat() then
        Send(player, "|cffff0000You are in combat.|r")
        return
    end

    if player:InBattleground() then
        Send(player, "|cffff0000Not inside a battleground.|r")
        return
    end

    if player:GetVIPLevel() < dest.minVip then
        Send(player, string.format("|cffff0000This destination needs VIP level %d.|r", dest.minVip))
        return
    end

    if dest.mg > 0 and not player:ModifyMagicGold(-dest.mg, true) then
        Send(player, "|cffff0000Not enough Magic Gold for this teleport.|r")
        return
    end

    player:GossipComplete()
    player:Teleport(dest.map, dest.x, dest.y, dest.z, dest.o or 0)

    if category == "PvP Free-For-All" then
        Send(player, "|cffff6600Free-for-all PvP zone: everyone can attack everyone here. Good luck!|r")
    end
end

-- Gear Sale -----------------------------------------------------------------

local function OpenGearSale(player)
    openWindow[player:GetGUIDLow()] = nil      -- plain copper window, no confirm flow
    player:GossipComplete()
    player:ClearInventoryMenu()
    for _, entry in ipairs(GEAR_SALE_ITEMS) do
        player:AddInventoryMenuItem(entry.item, entry.price)
    end
    player:SendInventoryMenu(false)      -- false = purchases enabled
    Send(player, "Drop your loot into the window to sell it. Buyback works as usual.")
end

-- Learn Skill ---------------------------------------------------------------

local function LearnSkillMenu(player, item)
    player:GossipClearMenu()

    -- the character's own trainer page: all class spells + weapon skills
    player:GossipMenuAddItem(ICON_TRAIN, "Open my Class Trainer (class spells & weapon skills)",
                             A.SKILL_TRAIN, A.SKILL_TRAIN, false,
                             "Open your class trainer window? Spells are trained with normal gold.", 0)

    -- riding skills with Magic Gold prices
    for i, skill in ipairs(LEARN_SKILLS) do
        local label
        local popup
        if player:HasSpell(skill.spellId) then
            label = string.format("%s |cff00ff00(known)|r", skill.name)
            popup = string.format("You already know %s.", skill.name)
        else
            label = string.format("%s - %d MG", skill.name, skill.mg)
            popup = string.format("Learn |cffffff00%s|r for |cffffff00%d|r Magic Gold?", skill.name, skill.mg)
        end
        player:GossipMenuAddItem(ICON_TRAIN, label, A.SKILL_ENT, A.SKILL_ENT + i, false, popup, 0)
    end

    player:GossipMenuAddItem(ICON_TALK, "Back", A.BACK, A.BACK)
    player:GossipSendMenu(NPC_TEXT_ID, item)
end

local function DoLearnSkill(player, index)
    local skill = LEARN_SKILLS[index]
    if not skill then
        return
    end

    if player:HasSpell(skill.spellId) then
        Send(player, "|cffff0000You already know this.|r")
        return
    end

    if player:GetLevel() < skill.minLevel then
        Send(player, string.format("|cffff0000You need level %d first.|r", skill.minLevel))
        return
    end

    if not player:ModifyMagicGold(-skill.mg, true) then
        Send(player, "|cffff0000Not enough Magic Gold.|r")
        return
    end

    player:LearnSpell(skill.spellId)
    Send(player, string.format("|cff00ff00Learned:|r %s", skill.name))
end

-- VIP service ---------------------------------------------------------------

local vipShopNames = {}
for name in pairs(VIP_SHOPS) do
    table.insert(vipShopNames, name)
end
table.sort(vipShopNames)

-- merged lookup for the buy hooks: itemEntry -> shop entry
local BuyIndex = {}
for _, categoryName in ipairs(vipShopNames) do
    for _, entry in ipairs(VIP_SHOPS[categoryName]) do
        BuyIndex[entry.item] = entry
    end
end

local function VipMenu(player, item)
    player:GossipClearMenu()

    player:GossipMenuAddItem(ICON_CHAT,   "VIP Status",    A.VIP_STATUS, A.VIP_STATUS)
    player:GossipMenuAddItem(ICON_TABARD, "Morph My Gear", A.VIP_MORPH, A.VIP_MORPH)

    for i, name in ipairs(vipShopNames) do
        player:GossipMenuAddItem(ICON_VENDOR, "Shop: " .. name, A.VIP_SHOP, A.VIP_SHOP + i)
    end

    -- buy the next VIP level (only offered when a next level exists);
    -- the Accept/Cancel popup IS the confirmation
    local vip = player:GetVIPLevel()
    local nextLevel = vip + 1
    if VIP_PRICES[nextLevel] then
        local popup = string.format("Upgrade your ACCOUNT to |cffffff00VIP %d|r for |cffffff00%d|r Magic Gold?\nThis unlocks the VIP %d bonuses for every character of the account.", nextLevel, VIP_PRICES[nextLevel], nextLevel)
        player:GossipMenuAddItem(ICON_MONEY, string.format("Buy VIP Level %d - %d MG", nextLevel, VIP_PRICES[nextLevel]),
                                 A.VIP_BUY, A.VIP_BUY + 1000, false, popup, 0)
    end

    player:GossipMenuAddItem(ICON_TALK, "Back", A.BACK, A.BACK)
    player:GossipSendMenu(NPC_TEXT_ID, item)
end

local function VipStatus(player)
    local vip = player:GetVIPLevel()
    Send(player, string.format("|cff00ccff[VIP]|r Your VIP level: |cffffff00%d|r  -  Magic Gold: |cffffff00%d|r", vip, player:GetMagicGold()))
    Send(player, "|cff00ccff[VIP]|r Every level: +1 max level, +2% loot chance, +1.5yd stealth detection, +10% Magic Gold, +5% XP / honor / reputation.")

    local nextLevel = vip + 1
    if VIP_PRICES[nextLevel] then
        Send(player, string.format("|cff00ccff[VIP]|r Next rank (|cffffff00VIP %d|r) costs |cffffff00%d|r Magic Gold - buy it in this menu.", nextLevel, VIP_PRICES[nextLevel]))
    else
        Send(player, "|cff00ccff[VIP]|r You are at the highest VIP level. Congratulations!")
    end
end

local function DoBuyVip(player)
    local vip = player:GetVIPLevel()
    local nextLevel = vip + 1
    local price = VIP_PRICES[nextLevel]

    if not price then
        Send(player, "|cffff0000There is no higher VIP level to buy.|r")
        return
    end

    if not player:ModifyMagicGold(-price, true) then
        Send(player, string.format("|cffff0000VIP %d costs %d Magic Gold - you only have %d.|r", nextLevel, price, player:GetMagicGold()))
        return
    end

    player:SetVIPLevel(nextLevel)
    Send(player, string.format("|cff00ff00VIP level %d unlocked!|r Your whole account now enjoys the VIP %d bonuses.", nextLevel, nextLevel))
end

-- the VIP shop windows: the confirmation runs through the confirm gossip,
-- the item itself is delivered by the core fake vendor purchase
local function OpenVipShop(player, categoryName)
    local list = VIP_SHOPS[categoryName]
    if not list then
        return
    end

    openWindow[player:GetGUIDLow()] = { kind = "shop", category = categoryName }

    player:GossipComplete()
    player:ClearInventoryMenu()

    for _, entry in ipairs(list) do
        -- copper price 0: the real price is Magic Gold, asked in the
        -- confirmation dialog and taken in ON_BUY_ITEM_COMPLETE
        player:AddInventoryMenuItem(entry.item, 0)
    end

    player:SendInventoryMenu(false)      -- false = purchases enabled
    Send(player, "Everything in this window is paid with |cff00ccffMagic Gold|r - your gold is NOT touched.")
    Send(player, "Click an item and confirm the purchase. |cffff8800Rental gear shows its rental time before you buy.|r")
end

-- morph service -------------------------------------------------------------

local function MorphSlotMenu(player, item)
    player:GossipClearMenu()

    for i, info in ipairs(MORPH_SLOTS) do
        local equipped = player:GetItemByPos(BAG_EQUIPMENT, info.slot)
        local label

        if equipped then
            local shownEntry = player:GetItemVisualOverrideEntry(info.slot)
            local icon = IconMarkup(equipped:GetItemId(), 20)
            if shownEntry > 0 and shownEntry ~= equipped:GetItemId() then
                label = string.format("%s: %s %s |cff00ccff(morphed - click to change)|r", info.label, icon, equipped:GetItemLink())
            else
                label = string.format("%s: %s %s", info.label, icon, equipped:GetItemLink())
            end
        else
            label = string.format("%s: |cff888888(empty)|r", info.label)
        end

        player:GossipMenuAddItem(ICON_TABARD, label, A.MORPH_SLOT, A.MORPH_SLOT + i)
    end

    -- restore options only for the slots that currently carry a morph
    for i, info in ipairs(MORPH_SLOTS) do
        local equipped = player:GetItemByPos(BAG_EQUIPMENT, info.slot)
        if equipped then
            local shownEntry = player:GetItemVisualOverrideEntry(info.slot)
            if shownEntry > 0 and shownEntry ~= equipped:GetItemId() then
                player:GossipMenuAddItem(ICON_CHAT, string.format("Restore the real look of my %s (free)", info.label),
                                         A.MORPH_RESET, A.MORPH_RESET + i, false,
                                         string.format("Remove the morph of your |cffffff00%s|r and show the real item again?", info.label), 0)
            end
        end
    end

    player:GossipMenuAddItem(ICON_TALK, "Back", A.BACK, A.BACK)
    player:GossipSendMenu(NPC_TEXT_ID, item)
end

-- the look preview window of one slot: the player browses the available
-- looks with icons and prices, clicking one opens the confirmation
local function OpenMorphPreview(player, slotInfoIndex)
    local slotInfo = MORPH_SLOTS[slotInfoIndex]
    if not slotInfo then
        return
    end

    local looks = MORPH_LOOKS[slotInfo.label] or {}

    if not player:GetItemByPos(BAG_EQUIPMENT, slotInfo.slot) then
        Send(player, string.format("|cffff0000Equip an item in your %s slot first.|r", slotInfo.label))
        return
    end

    if #looks == 0 then
        Send(player, string.format("|cffff0000No looks available for the %s slot yet.|r", slotInfo.label))
        return
    end

    openWindow[player:GetGUIDLow()] = { kind = "morph", slotIdx = slotInfoIndex }

    player:GossipComplete()
    player:ClearInventoryMenu()

    for _, look in ipairs(looks) do
        player:AddInventoryMenuItem(look.item, 0)
    end

    player:SendInventoryMenu(false)      -- the click is intercepted, nothing is really bought
    Send(player, string.format("Pick a new look for your |cffffff00%s|r. Prices are paid in |cff00ccffMagic Gold|r.", slotInfo.label))
    Send(player, "|cffff8800This is a fake purchase: the shown item is never added to your bags, it only lends its look.|r")
end

local function DoMorphRestore(player, slotInfoIndex)
    local slotInfo = MORPH_SLOTS[slotInfoIndex]
    if slotInfo and player:ItemMorphSlot(slotInfo.slot, 0) then
        Send(player, string.format("|cff00ff00Real look restored:|r %s", slotInfo.label))
    end
end

-- other service --------------------------------------------------------------

local function OtherMenu(player, item)
    player:GossipClearMenu()
    player:GossipMenuAddItem(ICON_CHAT,  string.format("Remove Resurrection Sickness - %d MG", RES_SICKNESS_COST),
                             A.OTHER_ENT, A.OTHER_ENT + 1, false,
                             string.format("Remove your resurrection sickness for |cffffff00%d|r Magic Gold?", RES_SICKNESS_COST), 0)
    player:GossipMenuAddItem(ICON_TRAIN, "Reset Talents (free)", A.OTHER_ENT, A.OTHER_ENT + 2, false,
                             "Reset all your talents?", 0)
    player:GossipMenuAddItem(ICON_TAXI,  string.format("Unstuck: teleport to my home - %d MG", UNSTUCK_COST),
                             A.OTHER_ENT, A.OTHER_ENT + 3, false,
                             string.format("Teleport to your homebind for |cffffff00%d|r Magic Gold?", UNSTUCK_COST), 0)
    player:GossipMenuAddItem(ICON_TALK,  "Back", A.BACK, A.BACK)
    player:GossipSendMenu(NPC_TEXT_ID, item)
end

local function DoUnstuck(player)
    local result = CharDBQuery(string.format(
        "SELECT `map`, `position_x`, `position_y`, `position_z` FROM `character_homebind` WHERE `guid` = %d",
        player:GetGUIDLow()))

    if not result then
        Send(player, "|cffff0000No homebind found for your character.|r")
        return
    end

    if player:IsInCombat() then
        Send(player, "|cffff0000You are in combat.|r")
        return
    end

    if not player:ModifyMagicGold(-UNSTUCK_COST, true) then
        Send(player, "|cffff0000Not enough Magic Gold.|r")
        return
    end

    player:GossipComplete()
    player:Teleport(result:GetUInt32(0), result:GetFloat(1), result:GetFloat(2), result:GetFloat(3), 0)
end

-- ------------------------------------------------- purchase confirmation --

-- shows the confirmation gossip: every line is broadcast (so the full price
-- info is always readable) and the Accept option carries a popup with the
-- same text as a second "are you sure" step.
local function ShowConfirmGossip(player, acceptLabel, lines)
    for _, line in ipairs(lines) do
        Send(player, line)
    end

    local popup = table.concat(lines, "\n") .. "\n|cff00ccffProceed?|r"

    player:GossipClearMenu()
    player:GossipMenuAddItem(ICON_MONEY, acceptLabel, 0, C.ACCEPT, false, popup, 0)
    player:GossipMenuAddItem(ICON_TALK, "Cancel", 0, C.CANCEL)
    player:GossipSendMenu(NPC_TEXT_ID, player, CONFIRM_MENU_ID)
end

-- VIP shop window purchase: refuse once, ask, deliver on Accept
local function OpenShopConfirm(player, info, count)
    count = math.max(1, count or 1)
    local lines =
    {
        string.format("|cff00ccffPurchase confirm|r: %s x%d", IconMarkup(info.item, 24) .. " " .. ItemLink(info.item), count),
        string.format("Price: %s", MG(info.mg * count)),
        RentalTag(info.expire),
    }

    pending[player:GetGUIDLow()] = { kind = "shop", info = info, count = count }
    ShowConfirmGossip(player, string.format("Accept - buy for %d MG", info.mg * count), lines)
end

-- morph look window purchase: refuse (fake inventory), ask, morph on Accept
local function OpenMorphConfirm(player, slotInfoIndex, look, count)
    count = math.max(1, count or 1)
    local slotInfo = MORPH_SLOTS[slotInfoIndex]
    local equipped = player:GetItemByPos(BAG_EQUIPMENT, slotInfo.slot)
    local days = (look.expire and look.expire > 0)
        and string.format("|cffffff00%d day(s)|r", math.ceil(look.expire / 86400))
        or "|cffffff00permanent|r"

    local lines =
    {
        string.format("|cff00ccffMorph confirm|r: your |cffffff00%s|r (%s)", slotInfo.label,
                      equipped and equipped:GetItemLink() or "|cff888888nothing|r"),
        string.format("will display as %s", IconMarkup(look.item, 24) .. " " .. ItemLink(look.item)),
        string.format("Look duration: %s   -   Price: %s", days, MG(look.mg * count)),
        "|cffff8800Fake purchase: the shown item is never added to your bags.|r",
    }

    pending[player:GetGUIDLow()] = { kind = "morph", slotIdx = slotInfoIndex, look = look, count = count }
    ShowConfirmGossip(player, string.format("Accept - morph for %d MG", look.mg * count), lines)
end

-- the Accept / Cancel handler of the confirmation gossip
local function OnConfirmSelect(event, player, sender, intid)
    local guid = player:GetGUIDLow()
    local p = pending[guid]

    if intid == C.CANCEL then
        pending[guid] = nil
        Send(player, "|cffff8800Purchase cancelled.|r")
        player:GossipComplete()
        return
    end

    if intid ~= C.ACCEPT then
        return
    end

    player:GossipComplete()
    pending[guid] = nil

    if not p then
        Send(player, "|cffff0000Nothing to confirm (the offer timed out).|r")
        return
    end

    if p.kind == "morph" then
        local slotInfo = MORPH_SLOTS[p.slotIdx]
        local look = p.look

        if not slotInfo or not look then
            return
        end

        if not player:GetItemByPos(BAG_EQUIPMENT, slotInfo.slot) then
            Send(player, "|cffff0000Morph failed: no item is equipped in that slot anymore.|r")
            return
        end

        if player:GetVIPLevel() < (look.vip or 0) then
            Send(player, string.format("|cffff0000This look needs VIP level %d.|r", look.vip or 0))
            return
        end

        if not player:ModifyMagicGold(-(look.mg or 0), true) then
            Send(player, "|cffff0000Not enough Magic Gold.|r")
            return
        end

        -- fake inventory: only the LOOK is applied, nothing enters the bags
        if player:ItemMorphSlot(slotInfo.slot, look.item, look.expire or 0, true) then
            local duration = (look.expire and look.expire > 0)
                and string.format(" (%d days)", math.ceil(look.expire / 86400))
                or " (permanent)"
            Send(player, string.format("|cff00ff00Morph applied:|r %s%s", ItemLink(look.item), duration))
            Send(player, string.format("|cff888888The real stats of your %s are untouched, only the look changed.|r", slotInfo.label))
            OpenMorphPreview(player, p.slotIdx)      -- offer another look right away
        else
            player:ModifyMagicGold(look.mg or 0)      -- give the money back
            Send(player, "|cffff0000Morph failed: unknown look.|r")
        end

    elseif p.kind == "shop" then
        local info = p.info

        if not info then
            return
        end

        if player:GetVIPLevel() < (info.vip or 0) then
            Send(player, string.format("|cffff0000This item needs VIP level %d.|r", info.vip or 0))
            return
        end

        if player:GetMagicGold() < info.mg * p.count then
            Send(player, string.format("|cffff0000Not enough Magic Gold (costs %d).|r", info.mg * p.count))
            return
        end

        -- mark the purchase as confirmed and send it back through the window:
        -- the ON_BUY_ITEM hook lets it pass, the core delivers the item and
        -- ON_BUY_ITEM_COMPLETE takes the Magic Gold
        confirmed[guid] = { item = info.item, deadline = os.time() + 60 }
        player:BuyInventoryMenuItem(info.item, p.count)
    end
end

RegisterPlayerGossipEvent(CONFIRM_MENU_ID, GOSSIP_EVENT_ON_SELECT, OnConfirmSelect)

-- -------------------------------------------------------- the dispatcher --

local function OnSelect(event, player, item, sender, intid, code)
    -- close / back
    if intid == A.CLOSE then
        player:GossipComplete()
        return
    elseif intid == A.BACK then
        MainMenu(player, item)
        return
    end

    -- main menu actions
    if intid == A.GPS then
        GpsCategoryMenu(player, item)
        return
    elseif intid == A.HEARTH then
        player:GossipComplete()
        player:CastSpell(player, HEARTHSTONE_SPELL, true)
        return
    elseif intid == A.BANK then
        player:GossipComplete()
        player:SendShowBank(player)
        return
    elseif intid == A.ACTIONS then
        OpenActionCenter(player, item)
        return
    elseif intid == A.SALE then
        OpenGearSale(player)
        return
    elseif intid == A.REPAIR then
        player:DurabilityRepairAll(true)        -- true = normal gold cost
        Send(player, "|cff00ff00All gear repaired.|r")
        return
    elseif intid == A.SKILLS then
        LearnSkillMenu(player, item)
        return
    elseif intid == A.VIP then
        VipMenu(player, item)
        return
    elseif intid == A.OTHER then
        OtherMenu(player, item)
        return
    end

    -- action center callbacks
    if intid > A.ACTIONS_ENT and intid <= A.ACTIONS_ENT + #HearthstoneMenu.entries then
        local entry = HearthstoneMenu.entries[intid - A.ACTIONS_ENT]
        if entry and type(entry.fn) == "function" then
            pcall(entry.fn, player)
        end
        return
    end

    -- GPS categories
    if intid > A.GPS_CAT and intid < A.GPS_CAT + 51 then
        local category = gpsCategoryNames[intid - A.GPS_CAT]
        if category then
            openGpsCategory[player:GetGUIDLow()] = category
            GpsMenu(player, item, category)
        end
        return
    end

    -- GPS destinations (1..90 per category)
    if intid > A.GPS_DEST and intid < A.GPS_DEST + 91 then
        local category = openGpsCategory[player:GetGUIDLow()]
        if category then
            DoGpsTeleport(player, category, intid - A.GPS_DEST)
        end
        return
    end

    -- learn skill: the class trainer page
    if intid == A.SKILL_TRAIN then
        player:GossipComplete()
        player:SendClassTrainerList()
        Send(player, "|cff00ff00Class trainer opened:|r all class spells and weapon skills of YOUR class, normal gold prices.")
        return
    end

    -- learn skill entries
    if intid > A.SKILL_ENT and intid < A.SKILL_ENT + 51 then
        DoLearnSkill(player, intid - A.SKILL_ENT)
        LearnSkillMenu(player, item)
        return
    end

    -- VIP status / morph picker
    if intid == A.VIP_STATUS then
        VipStatus(player)
        VipMenu(player, item)
        return
    elseif intid == A.VIP_MORPH then
        MorphSlotMenu(player, item)
        return
    end

    -- VIP shop categories
    if intid > A.VIP_SHOP and intid < A.VIP_SHOP + 51 then
        local name = vipShopNames[intid - A.VIP_SHOP]
        if name then
            OpenVipShop(player, name)
        end
        return
    end

    -- VIP level purchase confirmed (the Accept of the popup in the VIP menu)
    if intid == A.VIP_BUY + 1000 then
        DoBuyVip(player)
        VipMenu(player, item)
        return
    end

    -- morph slot entries: open the look preview window
    if intid > A.MORPH_SLOT and intid < A.MORPH_SLOT + 51 then
        OpenMorphPreview(player, intid - A.MORPH_SLOT)
        return
    end

    -- morph restore (confirmed by the popup on the option)
    if intid > A.MORPH_RESET and intid < A.MORPH_RESET + 51 then
        DoMorphRestore(player, intid - A.MORPH_RESET)
        MorphSlotMenu(player, item)
        return
    end

    -- other service entries
    if intid > A.OTHER_ENT and intid < A.OTHER_ENT + 51 then
        local which = intid - A.OTHER_ENT
        if which == 1 then
            if player:ModifyMagicGold(-RES_SICKNESS_COST, true) then
                player:RemoveSpell(15007)       -- Resurrection Sickness
                Send(player, "|cff00ff00Resurrection Sickness removed.|r")
            end
        elseif which == 2 then
            player:ResetTalents(true)
            Send(player, "|cff00ff00Talents reset.|r")
        elseif which == 3 then
            DoUnstuck(player)
            return
        end
        OtherMenu(player, item)
        return
    end
end

-- the item gossip itself -----------------------------------------------------

local function OnHello(event, player, item)
    MainMenu(player, item)
    return false                                 -- false = do not cast the hearth spell
end

RegisterItemGossipEvent(HEARTHSTONE_ENTRY, GOSSIP_EVENT_ON_HELLO, OnHello)
RegisterItemGossipEvent(HEARTHSTONE_ENTRY, GOSSIP_EVENT_ON_SELECT, OnSelect)

-- login greeting (short - the full welcome with the gossip window lives in
-- 10_Welcome.lua) -------------------------------------------------------------

local function OnLogin(event, player)
    local vip = player:GetVIPLevel()
    local msg = string.format("|cff00ccff[Welcome]|r Magic Gold: |cffffff00%d|r", player:GetMagicGold())
    if vip > 0 then
        msg = msg .. string.format("  -  |cff00ccffVIP %d|r", vip)
    end
    player:SendBroadcastMessage(msg)
end

RegisterPlayerEvent(PLAYER_EVENT_ON_LOGIN, OnLogin)

-- MG shop hooks (fake inventory purchases) -----------------------------------

-- 55 = PLAYER_EVENT_ON_BUY_ITEM: fired BEFORE the purchase. Return false or a
-- BuyResult number to refuse; return nothing (or -1) to allow.
RegisterPlayerEvent(PLAYER_EVENT_ON_BUY_ITEM, function(event, player, vendor, itemEntry, count)
    if vendor then
        return                                    -- a real NPC vendor, not our window
    end

    local guid = player:GetGUIDLow()
    local window = openWindow[guid]

    if not window then
        return                                    -- Gear Sale window: plain copper purchases
    end

    -- a purchase that was just confirmed: let it through, the item is
    -- delivered by the core and ON_BUY_ITEM_COMPLETE takes the Magic Gold
    local conf = confirmed[guid]
    if conf and conf.item == itemEntry and os.time() <= conf.deadline then
        confirmed[guid] = nil
        return
    end
    confirmed[guid] = nil

    if window.kind == "morph" then
        -- which look was clicked in the preview window?
        local slotInfo = MORPH_SLOTS[window.slotIdx]
        local look
        if slotInfo then
            for _, l in ipairs(MORPH_LOOKS[slotInfo.label] or {}) do
                if l.item == itemEntry then
                    look = l
                    break
                end
            end
        end

        if not look then
            return 0                              -- not one of our looks: refuse
        end

        if player:GetVIPLevel() < (look.vip or 0) then
            Send(player, string.format("|cffff0000This look needs VIP level %d.|r", look.vip or 0))
            return 0                              -- 0 = BUY_ERR_CANT_FIND_ITEM (refuse)
        end

        if player:GetMagicGold() < look.mg * count then
            Send(player, string.format("|cffff0000Not enough Magic Gold (costs %d).|r", look.mg * count))
            return 2                              -- 2 = BUY_ERR_NOT_ENOUGHT_MONEY (refuse)
        end

        -- refuse the window buy and ask for the confirmation instead -
        -- this is what makes the purchase "fake inventory": after Accept only
        -- the look is applied, no item ever enters the bags
        OpenMorphConfirm(player, window.slotIdx, look, count)
        return 0
    end

    if window.kind == "shop" then
        local info = BuyIndex[itemEntry]
        if not info then
            return                                -- not one of our shop items: allow
        end

        if player:GetVIPLevel() < (info.vip or 0) then
            Send(player, string.format("|cffff0000This item needs VIP level %d.|r", info.vip or 0))
            return 0
        end

        if player:GetMagicGold() < info.mg * count then
            Send(player, string.format("|cffff0000Not enough Magic Gold (costs %d).|r", info.mg * count))
            return 2
        end

        -- refuse the window buy and ask for the confirmation instead
        OpenShopConfirm(player, info, count)
        return 0
    end
end)

-- 57 = PLAYER_EVENT_ON_BUY_ITEM_COMPLETE: fired AFTER the purchase fully
-- succeeded (item stored). The Magic Gold is taken here, because a failed
-- "inventory full" purchase never reaches this point.
RegisterPlayerEvent(PLAYER_EVENT_ON_BUY_ITEM_COMPLETE, function(event, player, vendor, item, count)
    if vendor or not item then
        return
    end

    local info = BuyIndex[item:GetItemId()]
    if not info then
        return
    end

    count = math.max(1, count or 1)

    if player:ModifyMagicGold(-info.mg * count, true) then
        if info.expire and info.expire > 0 then
            item:SetItemExpiry(info.expire)       -- rental: the core removes it later
            Send(player, string.format("|cff00ff00Rented:|r %s %s",
                item:GetItemLink(), RentalTag(info.expire)))
        else
            Send(player, string.format("|cff00ff00Bought:|r %s %s",
                item:GetItemLink(), RentalTag(0)))
        end
    end
end)

print("[Hearthstone] loaded (round 8) - GPS / Hearthstone / Bank / Actions / Sale / Repair / Class Trainer / VIP / Other")

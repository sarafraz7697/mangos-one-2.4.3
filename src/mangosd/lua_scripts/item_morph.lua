--[[
    item_morph.lua - demo for the ITEM_VISUAL_PATCH system
    =====================================================
    Temporary, transmogrification-like display changes for equipped items.

    The change is visible to yourself and everyone around you IMMEDIATELY
    (same mechanism .morph uses for creature models). It is 100% temporary:
    nothing is written to the database, stats/enchants/tooltips/inspection
    keep using the real item, and relogging restores the real look.

    Commands (say them in chat, they are handled by the ON_COMMAND hook):
        .morphitem <slot> <display>   morph the item in an equipment slot
        .morphitem <slot>             restore the item in a slot
        .morphitem item <itemId> <display>
                                       morph the first equipped item with
                                       this item entry
        .morphitem item <itemId>      restore that item again
        .morphitem creature <npcEntry>
                                       copy the weapons a creature carries
                                       (main hand, off hand, ranged) onto
                                       your own weapon slots
        .morphitem reset              restore every slot
        .morphitem help               show the usage

    Equipment slots:
        0 head      1 neck      2 shoulders 3 shirt    4 chest    5 belt
        6 legs      7 feet      8 wrists    9 hands   10 ring1   11 ring2
       12 trinket1 13 trinket2 14 back     15 mainhand 16 offhand
       17 ranged   18 tabard

    Accepted display values (universal, resolved by the core in this order):
      1. an item entry - for example a creature_equip_template.equipentry1-3
         value: that item is shown itself,
      2. a display id that items use - an item_template.displayid value: the
         lowest item with that display is shown,
      3. any other display id - for example a creature_equip_template_raw
         .equipmodel1-3 value (a raw ItemDisplayInfo.dbc id): the server shows
         a synthetic display-only item for it.

    Display ids are ids from ItemDisplayInfo.dbc (NOT the CreatureDisplayInfo
    ids that .morph uses - different list!). Raw display ids must exist in the
    CLIENT's ItemDisplayInfo.dbc, otherwise the item is simply invisible.

    The whole system can be switched off with ItemVisual.Enable = 0 in
    mangosd.conf (live reloadable with .reload config).
]]

-- event ids (prefer the engine provided "events" table, fall back to numbers)
local PLAYER_EVENT_ON_COMMAND     = 42
local PLAYER_EVENT_ON_ITEM_MORPH  = (events and events.player and events.player.on_item_morph) or 56

local SLOT_NAMES =
{
    [0] = "head", [1] = "neck", [2] = "shoulders", [3] = "shirt", [4] = "chest",
    [5] = "belt", [6] = "legs", [7] = "feet", [8] = "wrists", [9] = "hands",
    [10] = "ring 1", [11] = "ring 2", [12] = "trinket 1", [13] = "trinket 2",
    [14] = "back", [15] = "main hand", [16] = "off hand", [17] = "ranged",
    [18] = "tabard",
}

local function ShowUsage(player)
    player:SendBroadcastMessage("|cff00ff00ItemMorph|r - temporary item display changes")
    player:SendBroadcastMessage("  |cffffff00.morphitem <slot> <display>|r - morph a slot (0-18)")
    player:SendBroadcastMessage("  |cffffff00.morphitem <slot>|r - restore a slot")
    player:SendBroadcastMessage("  |cffffff00.morphitem item <itemId> <display>|r - morph by item entry")
    player:SendBroadcastMessage("  |cffffff00.morphitem item <itemId>|r - restore by item entry")
    player:SendBroadcastMessage("  |cffffff00.morphitem creature <npcEntry>|r - copy the weapons of a creature")
    player:SendBroadcastMessage("  |cffffff00.morphitem reset|r - restore everything")
    player:SendBroadcastMessage("Displays: an item entry, an item display id or a raw display id (e.g. creature_equip_template_raw.equipmodel). Changes vanish on relog.")
end

-- .morphitem creature <npcEntry>
-- Copies the weapons a creature carries (main hand, off hand, ranged) onto your
-- own weapon slots. Reads the same tables the core uses for creature equipment:
-- creature_equip_template (item entries, resolved by the core) first, then
-- creature_equip_template_raw (raw display ids) when the new table has nothing.
local function HandleCreatureCommand(player, npcEntry)
    local q = WorldDBQuery("SELECT EquipmentTemplateId FROM creature_template WHERE Entry = " .. npcEntry)
    if not q then
        player:SendBroadcastMessage(string.format("|cffff0000ItemMorph failed.|r creature %d not found.", npcEntry))
        return
    end

    local equipId = q:GetUInt32(0)
    if equipId == 0 then
        player:SendBroadcastMessage(string.format("|cffff0000ItemMorph failed.|r creature %d carries no equipment.", npcEntry))
        return
    end

    local main, off, ranged = 0, 0, 0

    -- creature_equip_template: equipentry1-3 are item entries
    q = WorldDBQuery("SELECT equipentry1, equipentry2, equipentry3 FROM creature_equip_template WHERE entry = " .. equipId)
    if q then
        main, off, ranged = q:GetUInt32(0), q:GetUInt32(1), q:GetUInt32(2)
    end

    -- creature_equip_template_raw: equipmodel1-3 are raw display ids, used when
    -- no item is set (the core itself refuses to mix the two tables)
    if main == 0 and off == 0 and ranged == 0 then
        q = WorldDBQuery("SELECT equipmodel1, equipmodel2, equipmodel3 FROM creature_equip_template_raw WHERE entry = " .. equipId)
        if q then
            main, off, ranged = q:GetUInt32(0), q:GetUInt32(1), q:GetUInt32(2)
        end
    end

    if main == 0 and off == 0 and ranged == 0 then
        player:SendBroadcastMessage(string.format("|cffff0000ItemMorph failed.|r creature %d (equipment %d) has no weapon to show.", npcEntry, equipId))
        return
    end

    local applied = 0
    local weapons = { {15, main}, {16, off}, {17, ranged} }
    for i = 1, 3 do
        local slot, value = weapons[i][1], weapons[i][2]
        if value == 0 then
            player:ItemMorphSlot(slot, 0)              -- restore slots the creature leaves empty
        elseif player:ItemMorphSlot(slot, value) then
            applied = applied + 1
        end
    end

    if applied > 0 then
        player:SendBroadcastMessage(string.format("|cff00ff00ItemMorph:|r %d weapon(s) of creature %d (equipment %d) applied. Use .morphitem reset to restore.", applied, npcEntry, equipId))
    else
        player:SendBroadcastMessage(string.format("|cffff0000ItemMorph failed.|r creature %d: nothing equipped in your main hand / off hand / ranged slots.", npcEntry))
    end
end

local function HandleCommand(event, player, command)
    -- commands typed in the server console have no player, let them pass
    if not player then
        return true
    end

    -- normalize: lowercase, drop a leading dot if it is still there
    local text = string.lower(command)
    if string.sub(text, 1, 1) == "." then
        text = string.sub(text, 2)
    end

    -- only react to our own command word
    if string.sub(text, 1, 9) ~= "morphitem" then
        return true
    end
    local rest = string.sub(text, 10)
    if rest ~= "" and string.sub(rest, 1, 1) ~= " " then
        return true                                     -- different command (e.g. ".morphitemx")
    end

    -- collect the arguments
    local args = {}
    for token in string.gmatch(rest, "%S+") do
        args[#args + 1] = token
    end

    -- .morphitem help
    if args[1] == nil or args[1] == "help" then
        ShowUsage(player)
        return false                                    -- command handled
    end

    -- .morphitem reset
    if args[1] == "reset" then
        player:ItemMorphReset()
        player:SendBroadcastMessage("|cff00ff00ItemMorph:|r all slots restored.")
        return false
    end

    -- .morphitem creature <npcEntry>
    if args[1] == "creature" then
        local npcEntry = tonumber(args[2])
        if not npcEntry then
            ShowUsage(player)
            return false
        end

        HandleCreatureCommand(player, npcEntry)
        return false
    end

    -- .morphitem item <itemId> [display]
    if args[1] == "item" then
        local itemId = tonumber(args[2])
        if not itemId then
            ShowUsage(player)
            return false
        end

        local display = tonumber(args[3]) or 0          -- 0 = restore
        if display < 0 or display > 0xFFFFFFFF then
            display = 0
        end

        if player:ItemMorph(itemId, display) then
            if display == 0 then
                player:SendBroadcastMessage(string.format("|cff00ff00ItemMorph:|r item %d restored.", itemId))
            else
                player:SendBroadcastMessage(string.format("|cff00ff00ItemMorph:|r item %d now shows display/item %d.", itemId, display))
            end
        else
            player:SendBroadcastMessage(string.format("|cffff0000ItemMorph failed.|r item %d: not equipped, unknown display, denied by script or system disabled.", itemId))
        end
        return false
    end

    -- .morphitem <slot> [display]
    local slot = tonumber(args[1])
    if not slot or slot < 0 or slot > 18 then
        ShowUsage(player)
        return false
    end

    local display = tonumber(args[2]) or 0              -- 0 = restore
    if display < 0 or display > 0xFFFFFFFF then
        display = 0
    end

    if player:ItemMorphSlot(slot, display) then
        if display == 0 then
            player:SendBroadcastMessage(string.format("|cff00ff00ItemMorph:|r %s slot restored.", SLOT_NAMES[slot] or slot))
        else
            player:SendBroadcastMessage(string.format("|cff00ff00ItemMorph:|r %s slot now shows display/item %d.", SLOT_NAMES[slot] or slot, display))
        end
    else
        player:SendBroadcastMessage(string.format("|cffff0000ItemMorph failed.|r %s slot: nothing equipped, unknown display, denied by script or system disabled.", SLOT_NAMES[slot] or slot))
    end
    return false
end

-- PLAYER_EVENT_ON_ITEM_MORPH fires in the core before a visual override is
-- applied. A handler returns false to DENY that single change (the Player:ItemMorph*
-- call then reports failure). Do NOT call morph functions from inside this
-- handler - that would recurse.
local function OnItemMorph(event, player, slot, itemEntry, display)
    -- log every attempt to the server console (display = display id or item entry)
    print(string.format("[item_morph] %s: slot %d (item %d) -> display/item %d",
        player:GetName(), slot, itemEntry, display))

    -- Example 1: deny everything (uncomment to see the veto in action):
    -- return false

    -- Example 2: deny while in a battleground:
    -- if player:InBattleGround() then
    --     player:SendBroadcastMessage("|cffff0000ItemMorph is disabled in battlegrounds.|r")
    --     return false
    -- end
end

RegisterPlayerEvent(PLAYER_EVENT_ON_COMMAND, HandleCommand)
RegisterPlayerEvent(PLAYER_EVENT_ON_ITEM_MORPH, OnItemMorph)

print("[item_morph] loaded - commands: .morphitem help / <slot> <display> / item <itemId> <display> / creature <npcEntry> / reset")

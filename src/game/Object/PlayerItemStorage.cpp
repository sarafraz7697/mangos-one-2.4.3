/**
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * MaNGOS is a full featured server for World of Warcraft, supporting
 * the following clients: 1.12.x, 2.4.3, 3.3.5a, 4.3.4a and 5.4.8
 *
 * Copyright (C) 2005-2026 MaNGOS <https://www.getmangos.eu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * World of Warcraft, and all World of Warcraft or Warcraft art, images,
 * and lore are copyrighted by Blizzard Entertainment, Inc.
 */



#include "Player.h"
#include "Log.h"
#include "Opcodes.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "ObjectMgr.h"
#include "World.h"
#include "Database/DatabaseEnv.h"
#include "DBCStores.h"
#include "Chat.h"
#include <ctime>
#ifdef ENABLE_ELUNA
#include "LuaEngine.h"
#endif /* ENABLE_ELUNA */

/**
 * @brief Resolves the universal morph input into the item entry shown to clients.
 *
 * Accepted inputs, resolved in this order:
 *   1. an item entry - for example a creature_equip_template.equipentry1-3 value,
 *   2. a display id that items use - an item_template.displayid value: the lowest
 *      item with that display is shown,
 *   3. any other display id - for example a creature_equip_template_raw
 *      .equipmodel1-3 value: a synthetic display-only item is shown.
 *
 * @param displayId The raw input (display id or item entry).
 * @param baseProto The real item prototype the look is applied to (stat donor
 *                  for synthetic display-only items).
 * @param fakeProtoOut Optional, receives the synthetic prototype when one is used.
 * @return The item entry to place in the visible item fields, 0 when not showable.
 */
static uint32 ResolveItemVisualEntry(uint32 displayId, ItemPrototype const* baseProto, ItemPrototype const** fakeProtoOut = NULL)
{
    if (ItemPrototype const* inputProto = ObjectMgr::GetItemPrototype(displayId))
    {
        return inputProto->ItemId;                          // input is an item entry
    }

    if (uint32 itemEntry = ObjectMgr::GetFirstItemEntryForDisplay(displayId))
    {
        return itemEntry;                                   // input is a display id some item uses
    }

    ItemPrototype const* fakeProto = ObjectMgr::GetOrCreateItemVisualFakePrototype(displayId, baseProto);

    if (fakeProto && fakeProtoOut)
    {
        *fakeProtoOut = fakeProto;
    }

    return fakeProto ? fakeProto->ItemId : 0;               // synthetic item for a raw display id
}

// Return stored item (if stored to stack, it can diff. from pItem). And pItem ca be deleted in this case.
Item* Player::StoreNewItem(ItemPosCountVec const& dest, uint32 item, bool update, int32 randomPropertyId)
{
    uint32 count = 0;
    for (ItemPosCountVec::const_iterator itr = dest.begin(); itr != dest.end(); ++itr)
    {
        count += itr->count;
    }

    Item* pItem = Item::CreateItem(item, count, this, randomPropertyId);
    if (pItem)
    {
        ItemAddedQuestCheck(item, count);
        pItem = StoreItem(dest, pItem, update);
    }
    return pItem;
}

/**
 * @brief Stores an item into one or more resolved destination positions.
 *
 * @param dest The destination positions and quantities.
 * @param pItem The item to store.
 * @param update True to send world updates for the storage change.
 * @return The final stored item instance.
 */
Item* Player::StoreItem(ItemPosCountVec const& dest, Item* pItem, bool update)
{
    if (!pItem)
    {
        return NULL;
    }

    Item* lastItem = pItem;

    for (ItemPosCountVec::const_iterator itr = dest.begin(); itr != dest.end();)
    {
        uint16 pos = itr->pos;
        uint32 count = itr->count;

        ++itr;

        if (itr == dest.end())
        {
            lastItem = _StoreItem(pos, pItem, count, false, update);
            break;
        }

        lastItem = _StoreItem(pos, pItem, count, true, update);
    }

    return lastItem;
}

// Return stored item (if stored to stack, it can diff. from pItem). And pItem ca be deleted in this case.
Item* Player::_StoreItem(uint16 pos, Item* pItem, uint32 count, bool clone, bool update)
{
    if (!pItem)
    {
        return NULL;
    }

    uint8 bag = pos >> 8;
    uint8 slot = pos & 255;

    DEBUG_LOG("STORAGE: StoreItem bag = %u, slot = %u, item = %u, count = %u", bag, slot, pItem->GetEntry(), count);

    Item* pItem2 = GetItemByPos(bag, slot);

    if (!pItem2)
    {
        if (clone)
        {
            pItem = pItem->CloneItem(count, this);
        }
        else
        {
            pItem->SetCount(count);
        }

        if (!pItem)
        {
            return NULL;
        }

        ItemPrototype const* itemProto = pItem->GetProto();
        if (itemProto->Bonding == BIND_WHEN_PICKED_UP ||
            itemProto->Bonding == BIND_QUEST_ITEM ||
            (itemProto->Bonding == BIND_WHEN_EQUIPPED && IsBagPos(pos)))
        {
            pItem->SetBinding(true);
        }

        if (bag == INVENTORY_SLOT_BAG_0)
        {
            m_items[slot] = pItem;
            SetGuidValue(PLAYER_FIELD_INV_SLOT_HEAD + (slot * 2), pItem->GetObjectGuid());
            pItem->SetGuidValue(ITEM_FIELD_CONTAINED, GetObjectGuid());
            pItem->SetGuidValue(ITEM_FIELD_OWNER, GetObjectGuid());

            pItem->SetSlot(slot);
            pItem->SetContainer(NULL);

            if (IsInWorld() && update)
            {
                pItem->AddToWorld();
                pItem->SendCreateUpdateToPlayer(this);
            }

            pItem->SetState(ITEM_CHANGED, this);
        }
        else if (Bag* pBag = (Bag*)GetItemByPos(INVENTORY_SLOT_BAG_0, bag))
        {
            pBag->StoreItem(slot, pItem);
            if (IsInWorld() && update)
            {
                pItem->AddToWorld();
                pItem->SendCreateUpdateToPlayer(this);
            }
            pItem->SetState(ITEM_CHANGED, this);
            pBag->SetState(ITEM_CHANGED, this);
        }

        AddEnchantmentDurations(pItem);
        AddItemDurations(pItem);

        // at place into not appropriate slot (bank, for example) remove aura
        ApplyItemOnStoreSpell(pItem, IsEquipmentPos(pItem->GetBagSlot(), pItem->GetSlot()) || IsInventoryPos(pItem->GetBagSlot(), pItem->GetSlot()));

        return pItem;
    }
    else
    {
        ItemPrototype const* itemProto = pItem2->GetProto();
        if (itemProto->Bonding == BIND_WHEN_PICKED_UP ||
            itemProto->Bonding == BIND_QUEST_ITEM ||
            (itemProto->Bonding == BIND_WHEN_EQUIPPED && IsBagPos(pos)))
        {
            pItem2->SetBinding(true);
        }

        pItem2->SetCount(pItem2->GetCount() + count);
        if (IsInWorld() && update)
        {
            pItem2->SendCreateUpdateToPlayer(this);
        }

        if (!clone)
        {
            // delete item (it not in any slot currently)
            if (IsInWorld() && update)
            {
                pItem->RemoveFromWorld();
                pItem->DestroyForPlayer(this);
            }

            RemoveEnchantmentDurations(pItem);
            RemoveItemDurations(pItem);

            pItem->SetOwnerGuid(GetObjectGuid());           // prevent error at next SetState in case trade/mail/buy from vendor
            pItem->SetState(ITEM_REMOVED, this);
        }

        // AddItemDurations(pItem2); - pItem2 already have duration listed for player
        AddEnchantmentDurations(pItem2);

        pItem2->SetState(ITEM_CHANGED, this);

        return pItem2;
    }
}

/**
 * @brief Creates and equips a new item into a destination slot.
 *
 * @param pos The packed destination position.
 * @param item The item entry to create.
 * @param update True to send world updates for the equip action.
 * @return The equipped item, or null on failure.
 */
Item* Player::EquipNewItem(uint16 pos, uint32 item, bool update)
{
    if (Item* pItem = Item::CreateItem(item, 1, this))
    {
        ItemAddedQuestCheck(item, 1);
        return EquipItem(pos, pItem, update);
    }

    return NULL;
}

/**
 * @brief Equips an item into the specified destination position.
 *
 * @param pos The packed destination position.
 * @param pItem The item to equip.
 * @param update True to send world updates for the equip action.
 * @return The equipped or merged item instance.
 */
Item* Player::EquipItem(uint16 pos, Item* pItem, bool update)
{
    AddEnchantmentDurations(pItem);
    AddItemDurations(pItem);

    uint8 bag = pos >> 8;
    uint8 slot = pos & 255;

    Item* pItem2 = GetItemByPos(bag, slot);

    if (!pItem2)
    {
        VisualizeItem(slot, pItem);

        if (IsAlive())
        {
            ItemPrototype const* pProto = pItem->GetProto();

            // item set bonuses applied only at equip and removed at unequip, and still active for broken items
            if (pProto && pProto->ItemSet)
            {
                AddItemsSetItem(this, pItem);
            }

            _ApplyItemMods(pItem, slot, true);

            ApplyItemOnStoreSpell(pItem, true);

            // Weapons and also Totem/Relic/Sigil/etc
            if (pProto && IsInCombat() && (pProto->Class == ITEM_CLASS_WEAPON || pProto->InventoryType == INVTYPE_RELIC) && m_weaponChangeTimer == 0)
            {
                uint32 cooldownSpell = SPELL_ID_WEAPON_SWITCH_COOLDOWN_1_5s;

                if (getClass() == CLASS_ROGUE)
                {
                    cooldownSpell = SPELL_ID_WEAPON_SWITCH_COOLDOWN_1_0s;
                }

                SpellEntry const* spellProto = sSpellStore.LookupEntry(cooldownSpell);

                if (!spellProto)
                {
                    sLog.outError("Weapon switch cooldown spell %u couldn't be found in Spell.dbc", cooldownSpell);
                }
                else
                {
                    m_weaponChangeTimer = spellProto->StartRecoveryTime;

                    WorldPacket data(SMSG_SPELL_COOLDOWN, 8 + 1 + 4);
                    data << GetObjectGuid();
                    data << uint8(1);
                    data << uint32(cooldownSpell);
                    data << uint32(0);
                    GetSession()->SendPacket(&data);
                }
            }
        }

        if (IsInWorld() && update)
        {
            pItem->AddToWorld();
            pItem->SendCreateUpdateToPlayer(this);
        }

        ApplyEquipCooldown(pItem);

        if (slot == EQUIPMENT_SLOT_MAINHAND)
        {
            UpdateExpertise(BASE_ATTACK);
        }
        else if (slot == EQUIPMENT_SLOT_OFFHAND)
        {
            UpdateExpertise(OFF_ATTACK);
        }
    }
    else
    {
        pItem2->SetCount(pItem2->GetCount() + pItem->GetCount());
        if (IsInWorld() && update)
        {
            pItem2->SendCreateUpdateToPlayer(this);
        }

        // delete item (it not in any slot currently)
        // pItem->DeleteFromDB();
        if (IsInWorld() && update)
        {
            pItem->RemoveFromWorld();
            pItem->DestroyForPlayer(this);
        }

        RemoveEnchantmentDurations(pItem);
        RemoveItemDurations(pItem);

        pItem->SetOwnerGuid(GetObjectGuid());               // prevent error at next SetState in case trade/mail/buy from vendor
        pItem->SetState(ITEM_REMOVED, this);
        pItem2->SetState(ITEM_CHANGED, this);

        ApplyEquipCooldown(pItem2);

        // Used by Eluna
#ifdef ENABLE_ELUNA
        if (Eluna* e = GetEluna())
        {
            e->OnEquip(this, pItem2, bag, slot); // This is depricated and will be removed in the future
            e->OnItemEquip(this, pItem2, slot);
        }
#endif /* ENABLE_ELUNA */

        return pItem2;
    }
    // Used by Eluna
#ifdef ENABLE_ELUNA
    if (Eluna* e = GetEluna())
    {
        e->OnEquip(this, pItem, bag, slot); // This is depricated and will be removed in the future
        e->OnItemEquip(this, pItem, slot);
    }
#endif /* ENABLE_ELUNA */

    return pItem;
}

/**
 * @brief Equips an item without full validation, typically during loading.
 *
 * @param pos The packed destination position.
 * @param pItem The item to equip.
 */
void Player::QuickEquipItem(uint16 pos, Item* pItem)
{
    if (pItem)
    {
        AddEnchantmentDurations(pItem);
        AddItemDurations(pItem);
        ApplyItemOnStoreSpell(pItem, true);

        uint8 slot = pos & 255;
        VisualizeItem(slot, pItem);

        if (IsInWorld())
        {
            pItem->AddToWorld();
            pItem->SendCreateUpdateToPlayer(this);
        }
    }
}

/**
 * @brief Updates the visible equipment fields for an equipment slot.
 *
 * @param slot The equipment slot to update.
 * @param pItem The item to display, or null to clear the slot.
 */
void Player::SetVisibleItemSlot(uint8 slot, Item* pItem)
{
    if (pItem)
    {
        SetGuidValue(PLAYER_VISIBLE_ITEM_1_CREATOR + (slot * MAX_VISIBLE_ITEM_OFFSET), pItem->GetGuidValue(ITEM_FIELD_CREATOR));

        int VisibleBase = PLAYER_VISIBLE_ITEM_1_0 + (slot * MAX_VISIBLE_ITEM_OFFSET);

        // [item visual override] send the overridden item entry instead of the real one,
        // so re-equipping keeps the changed look until logout (the override is never saved).
        // Honored only while the system is enabled, so flipping ItemVisual.Enable off
        // restores the real look on the next equip/unequip of the slot.
        uint32 visibleEntry = pItem->GetEntry();
        bool overrideActive = slot < EQUIPMENT_SLOT_END && sWorld.getConfig(CONFIG_BOOL_ITEM_VISUAL_OVERRIDE_ENABLE);

        if (overrideActive && m_itemVisualOverrideEntries[slot] &&
                (!m_itemVisualOverrideSlotItems[slot] || m_itemVisualOverrideSlotItems[slot] == pItem->GetGUIDLow()))
        {
            // [item visual override] send the overridden item entry instead of the real
            // one, so re-equipping the same item keeps the changed look until logout.
            // The override belongs to the item it was morphed on: another item swapped
            // into the slot shows its own look again.
            visibleEntry = m_itemVisualOverrideEntries[slot];
        }
        else if (overrideActive)
        {
            // ITEM_VISUAL_PERSIST_PATCH: a saved look (bought with the VIP morph service)
            // follows the item itself and survives relogging. Re-apply it whenever the
            // item is equipped, so login and swap-in/swap-out both show the bought look.
            std::map<uint32, ItemMorphRecord>::const_iterator morphItr = m_itemMorphRecords.find(pItem->GetGUIDLow());

            if (morphItr != m_itemMorphRecords.end() &&
                    (!morphItr->second.expireTime || morphItr->second.expireTime > time(NULL))) // skip already expired records
            {
                ItemPrototype const* fakeProto = NULL;
                uint32 morphEntry = ResolveItemVisualEntry(morphItr->second.displayId, pItem->GetProto(), &fakeProto);

                if (morphEntry)
                {
                    visibleEntry = morphEntry;
                    m_itemVisualOverrideEntries[slot] = morphEntry;        // keep the session slot override in sync
                    m_itemVisualOverrideSlotItems[slot] = pItem->GetGUIDLow();

                    // make sure this client knows the (possibly synthetic) item model
                    if (fakeProto && GetSession())
                    {
                        GetSession()->SendItemQuerySingleResponse(fakeProto);
                    }
                }
            }
        }

        SetUInt32Value(VisibleBase + 0, visibleEntry);

        for (int i = 0; i < MAX_INSPECTED_ENCHANTMENT_SLOT; ++i)
        {
            SetUInt32Value(VisibleBase + 1 + i, pItem->GetEnchantmentId(EnchantmentSlot(i)));
        }

        // Use SetInt16Value to prevent set high part to FFFF for negative value
        SetInt16Value(PLAYER_VISIBLE_ITEM_1_PROPERTIES + (slot * MAX_VISIBLE_ITEM_OFFSET), 0, pItem->GetItemRandomPropertyId());
        SetUInt32Value(PLAYER_VISIBLE_ITEM_1_PROPERTIES + 1 + (slot * MAX_VISIBLE_ITEM_OFFSET), pItem->GetItemSuffixFactor());
    }
    else
    {
        SetGuidValue(PLAYER_VISIBLE_ITEM_1_CREATOR + (slot * MAX_VISIBLE_ITEM_OFFSET), ObjectGuid());

        int VisibleBase = PLAYER_VISIBLE_ITEM_1_0 + (slot * MAX_VISIBLE_ITEM_OFFSET);
        SetUInt32Value(VisibleBase + 0, 0);

        for (int i = 0; i < MAX_INSPECTED_ENCHANTMENT_SLOT; ++i)
        {
            SetUInt32Value(VisibleBase + 1 + i, 0);
        }

        SetUInt32Value(PLAYER_VISIBLE_ITEM_1_PROPERTIES + 0 + (slot * MAX_VISIBLE_ITEM_OFFSET), 0);
        SetUInt32Value(PLAYER_VISIBLE_ITEM_1_PROPERTIES + 1 + (slot * MAX_VISIBLE_ITEM_OFFSET), 0);
    }
}

/**
 * @brief Changes the displayed look of one equipped item, for everyone in range.
 *
 * The change is broadcast immediately through the PLAYER_VISIBLE_ITEM update
 * fields, the same mechanism .morph uses for creature models. Enchant glows,
 * the real item stats, tooltips and inspection keep working from the real item.
 *
 * @param slot The equipment slot to change (0-18).
 * @param displayId The display to show: an ItemDisplayInfo.dbc id (raw ids such
 *                  as the equipmodel values of creature_equip_template_raw work)
 *                  or an item entry (for example a creature_equip_template
 *                  .equipentry value). 0 restores the real look.
 * @param expireSeconds ITEM_VISUAL_PERSIST_PATCH: when persist is set, seconds
 *                      the bought look stays (0 = no expiry).
 * @param persist ITEM_VISUAL_PERSIST_PATCH: save the look in the
 *                character_item_morph table so it survives relogging and
 *                follows the item (removed again with displayId 0).
 * @return true when the override was applied (or restored).
 */
bool Player::SetItemVisualOverride(uint8 slot, uint32 displayId, uint32 expireSeconds, bool persist)
{
    if (slot >= EQUIPMENT_SLOT_END)
    {
        return false;
    }

    Item* pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, slot);

    // displayId 0 = remove the override and show the real item again.
    // Resetting is always allowed, even while the system is disabled.
    if (displayId == 0)
    {
        m_itemVisualOverrideEntries[slot] = 0;
        m_itemVisualOverrideSlotItems[slot] = 0;

        // ITEM_VISUAL_PERSIST_PATCH: restoring also forgets a saved look of this item
        if (pItem)
        {
            if (m_itemMorphRecords.erase(pItem->GetGUIDLow()))
            {
                CharacterDatabase.PExecute("DELETE FROM `character_item_morph` WHERE `item_guid` = '%u'", pItem->GetGUIDLow());
            }
        }

        SetVisibleItemSlot(slot, pItem);
        return true;
    }

    if (!sWorld.getConfig(CONFIG_BOOL_ITEM_VISUAL_OVERRIDE_ENABLE))
    {
        return false;
    }

    if (!pItem)
    {
        return false;                                   // nothing equipped in this slot
    }

    // Used by Eluna: a Lua handler of PLAYER_EVENT_ON_ITEM_MORPH can deny the change
#ifdef ENABLE_ELUNA
    if (Eluna* e = GetEluna())
    {
        if (!e->OnItemMorph(this, slot, pItem->GetEntry(), displayId))
        {
            return false;
        }
    }
#endif /* ENABLE_ELUNA */

    // The client renders gear from the item entry, so the input must become an
    // item entry. Slot fitting is NOT validated on purpose (system method).
    ItemPrototype const* fakeProto = NULL;
    uint32 overrideEntry = ResolveItemVisualEntry(displayId, pItem->GetProto(), &fakeProto);

    if (!overrideEntry)
    {
        return false;                                    // no way to show this id
    }

    // Push the item data before the visible update, so the client already knows
    // the model the moment the override entry arrives (other players receive it
    // through their normal item queries).
    ItemPrototype const* pushProto = fakeProto ? fakeProto : ObjectMgr::GetItemPrototype(overrideEntry);
    if (pushProto && GetSession())
    {
        GetSession()->SendItemQuerySingleResponse(pushProto);
    }

    m_itemVisualOverrideEntries[slot] = overrideEntry;
    m_itemVisualOverrideSlotItems[slot] = pItem->GetGUIDLow();
    SetUInt32Value(PLAYER_VISIBLE_ITEM_1_0 + (slot * MAX_VISIBLE_ITEM_OFFSET), overrideEntry);

    // ITEM_VISUAL_PERSIST_PATCH: remember bought looks per item guid, with optional expiry
    if (persist)
    {
        ItemMorphRecord record;
        record.displayId = displayId;
        record.expireTime = expireSeconds ? time(NULL) + expireSeconds : 0;
        m_itemMorphRecords[pItem->GetGUIDLow()] = record;

        CharacterDatabase.BeginTransaction();
        CharacterDatabase.PExecute("DELETE FROM `character_item_morph` WHERE `item_guid` = '%u'", pItem->GetGUIDLow());
        CharacterDatabase.PExecute("INSERT INTO `character_item_morph` (`item_guid`, `player_guid`, `display_id`, `expire_time`) VALUES ('%u', '%u', '%u', '%u')",
                                   pItem->GetGUIDLow(), GetGUIDLow(), record.displayId, uint32(record.expireTime));
        CharacterDatabase.CommitTransaction();
    }

    return true;
}

/**
 * @brief Changes the displayed look of the first equipment slot holding an item.
 *
 * @param itemEntry The equipped item entry to look for.
 * @param displayId The display to show (display id or item entry, see
 *                  SetItemVisualOverride), 0 restores the real look.
 * @param expireSeconds Seconds the look stays when persist is set (0 = no expiry).
 * @param persist Save the look so it survives relogging.
 * @return true when the override was applied (or the item was not found and a
 *         reset was requested).
 */
bool Player::SetItemVisualOverrideByItemEntry(uint32 itemEntry, uint32 displayId, uint32 expireSeconds, bool persist)
{
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        Item* pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (pItem && pItem->GetEntry() == itemEntry)
        {
            return SetItemVisualOverride(slot, displayId, expireSeconds, persist);
        }
    }

    return false;
}

/**
 * @brief Restores the real look of every overridden equipment slot.
 */
void Player::ResetItemVisualOverrides()
{
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        if (m_itemVisualOverrideEntries[slot])
        {
            m_itemVisualOverrideEntries[slot] = 0;
            m_itemVisualOverrideSlotItems[slot] = 0;
            SetVisibleItemSlot(slot, GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
        }
    }
}

/**
 * @brief The item entry an equipment slot currently shows, morphs included.
 *
 * ITEM_VISUAL_PERSIST_PATCH: used when the equipmentCache of the character
 * (the item list of the character selection screen) is saved. A bought look
 * (character_item_morph) of the equipped item is resolved to the item entry it
 * displays, so the login character list shows the morphed gear exactly like
 * the game world does. Looks that only exist as synthetic display-only
 * entries have no database item, so those keep the real item entry.
 *
 * @param slot The equipment slot.
 * @return The shown item entry - the real entry when the slot is empty of
 *         looks or holds no item at all.
 */
uint32 Player::GetShownItemEntryForSlot(uint8 slot) const
{
    Item* pItem = (slot < EQUIPMENT_SLOT_END) ? GetItemByPos(INVENTORY_SLOT_BAG_0, slot) : NULL;

    if (!pItem)
    {
        return 0;
    }

    std::map<uint32, ItemMorphRecord>::const_iterator itr = m_itemMorphRecords.find(pItem->GetGUIDLow());

    if (itr != m_itemMorphRecords.end() && (!itr->second.expireTime || itr->second.expireTime > time(NULL)))
    {
        uint32 entry = ResolveItemVisualEntry(itr->second.displayId, pItem->GetProto());

        if (entry && !ObjectMgr::IsItemVisualFakeEntry(entry))
        {
            return entry;
        }
    }

    return pItem->GetEntry();
}

/**
 * @brief Loads the saved item looks of the character (character_item_morph).
 *
 * Called BEFORE _LoadInventory: SetVisibleItemSlot applies the records while
 * the equipment is placed, so login shows the bought looks without any extra
 * pass. Expired records are removed from the database right away.
 *
 * @param result SELECT item_guid, display_id, expire_time FROM character_item_morph
 */
void Player::_LoadItemMorphs(QueryResult* result)
{
    time_t now = time(NULL);

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            uint32 itemGuid = fields[0].GetUInt32();
            uint32 displayId = fields[1].GetUInt32();
            time_t expireTime = time_t(fields[2].GetUInt64());

            if (expireTime && expireTime <= now)
            {
                CharacterDatabase.PExecute("DELETE FROM `character_item_morph` WHERE `item_guid` = '%u'", itemGuid);
                continue;                                   // the bought look ran out while offline
            }

            ItemMorphRecord record;
            record.displayId = displayId;
            record.expireTime = expireTime;
            m_itemMorphRecords[itemGuid] = record;
        }
        while (result->NextRow());

        delete result;
    }
}

/**
 * @brief Loads the rented items of the character (character_item_expiry).
 *
 * Expired items are destroyed by the first timed-item sweep shortly after
 * login (with a message), non-expired ones stay in m_itemExpiries.
 *
 * @param result SELECT item_guid, expire_time FROM character_item_expiry
 */
void Player::_LoadItemExpiries(QueryResult* result)
{
    time_t now = time(NULL);

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            uint32 itemGuid = fields[0].GetUInt32();
            time_t expireTime = time_t(fields[1].GetUInt64());
            m_itemExpiries[itemGuid] = expireTime;
        }
        while (result->NextRow());

        delete result;
    }

    // forget rows of items the character no longer owns (deleted / mailed away)
    for (std::map<uint32, time_t>::iterator itr = m_itemExpiries.begin(); itr != m_itemExpiries.end();)
    {
        if (!GetItemByGuid(ObjectGuid(HIGHGUID_ITEM, itr->first)))
        {
            CharacterDatabase.PExecute("DELETE FROM `character_item_expiry` WHERE `item_guid` = '%u'", itr->first);
            m_itemExpiries.erase(itr++);
        }
        else
        {
            ++itr;
        }
    }

    // Expired rentals are NOT destroyed here: the player is still loading, so
    // the sweep would run outside the world. The first UpdateTimedItems() pass
    // (3 seconds after login, see the Player constructor) removes them with
    // the proper chat message.
}

/**
 * @brief Puts an item on a rental timer: it is destroyed when the time is over.
 *
 * @param item The item to time (must belong to this player).
 * @param seconds Lifetime in seconds, 0 removes the timer.
 */
void Player::SetItemExpiry(Item* item, uint32 seconds)
{
    if (!item || item->GetOwnerGuid() != GetObjectGuid())
    {
        return;
    }

    uint32 itemGuid = item->GetGUIDLow();

    if (seconds == 0)
    {
        if (m_itemExpiries.erase(itemGuid))
        {
            CharacterDatabase.PExecute("DELETE FROM `character_item_expiry` WHERE `item_guid` = '%u'", itemGuid);
        }
        return;
    }

    time_t expireTime = time(NULL) + seconds;
    m_itemExpiries[itemGuid] = expireTime;

    CharacterDatabase.BeginTransaction();
    CharacterDatabase.PExecute("DELETE FROM `character_item_expiry` WHERE `item_guid` = '%u'", itemGuid);
    CharacterDatabase.PExecute("INSERT INTO `character_item_expiry` (`item_guid`, `player_guid`, `expire_time`) VALUES ('%u', '%u', '%u')",
                               itemGuid, GetGUIDLow(), uint32(expireTime));
    CharacterDatabase.CommitTransaction();
}

/**
 * @brief Seconds until the item expires, 0 when it has no timer.
 *
 * @param item The item to ask.
 * @return Remaining seconds (0 when the item has no rental time).
 */
uint32 Player::GetItemExpiryRemaining(Item* item) const
{
    if (!item)
    {
        return 0;
    }

    std::map<uint32, time_t>::const_iterator itr = m_itemExpiries.find(item->GetGUIDLow());

    if (itr == m_itemExpiries.end())
    {
        return 0;
    }

    time_t now = time(NULL);
    return itr->second > now ? uint32(itr->second - now) : 0;
}

/**
 * @brief Periodic expiry sweep: destroys rented items whose time ran out and
 *        restores item looks whose morph time ran out.
 *
 * Called from Player::Update every TimedItem.CheckInterval seconds and once
 * after login. Runs offline-expired rows are already cleaned at load time.
 */
void Player::UpdateTimedItems()
{
    time_t now = time(NULL);

    // rented items first (destroying an item may also own a morph record)
    for (std::map<uint32, time_t>::iterator itr = m_itemExpiries.begin(); itr != m_itemExpiries.end();)
    {
        if (itr->second > now)
        {
            ++itr;
            continue;                                       // still valid
        }

        uint32 itemGuid = itr->first;
        Item* item = GetItemByGuid(ObjectGuid(HIGHGUID_ITEM, itemGuid));

        if (item)
        {
            std::string name = item->GetProto() ? item->GetProto()->Name1 : "Unknown";

            if (item->IsEquipped())
            {
                DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
            }
            else
            {
                uint32 count = item->GetCount();
                DestroyItemCount(item, count, true);
            }

            if (GetSession())
            {
                GetSession()->SendNotification("%s ran out of time and was removed.", name.c_str());
                ChatHandler(GetSession()).PSendSysMessage("[Rental] %s ran out of time and was removed.", name.c_str());
            }
        }

        // also forget a morph bound to the destroyed item
        m_itemMorphRecords.erase(itemGuid);

        CharacterDatabase.BeginTransaction();
        CharacterDatabase.PExecute("DELETE FROM `character_item_expiry` WHERE `item_guid` = '%u'", itemGuid);
        CharacterDatabase.PExecute("DELETE FROM `character_item_morph` WHERE `item_guid` = '%u'", itemGuid);
        CharacterDatabase.CommitTransaction();

        m_itemExpiries.erase(itr++);
    }

    // bought looks whose time ran out
    for (std::map<uint32, ItemMorphRecord>::iterator itr = m_itemMorphRecords.begin(); itr != m_itemMorphRecords.end();)
    {
        if (!itr->second.expireTime || itr->second.expireTime > now)
        {
            ++itr;
            continue;                                       // permanent or still valid
        }

        uint32 itemGuid = itr->first;

        // restore the real look while the item is equipped
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        {
            Item* pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, slot);

            if (pItem && pItem->GetGUIDLow() == itemGuid)
            {
                m_itemVisualOverrideEntries[slot] = 0;
                m_itemVisualOverrideSlotItems[slot] = 0;
                SetVisibleItemSlot(slot, pItem);
                break;
            }
        }

        if (GetSession())
        {
            ChatHandler(GetSession()).PSendSysMessage("[ItemMorph] The bought look of one of your items ran out of time.");
        }

        CharacterDatabase.PExecute("DELETE FROM `character_item_morph` WHERE `item_guid` = '%u'", itemGuid);
        m_itemMorphRecords.erase(itr++);
    }
}

/**
 * @brief Loads the Magic Gold balance of the character.
 *
 * @param result SELECT mg FROM character_magic_gold
 */
void Player::_LoadMagicGold(QueryResult* result)
{
    if (result)
    {
        m_magicGold = result->Fetch()[0].GetUInt32();
        delete result;
    }
}

/**
 * @brief Adds or removes Magic Gold, clamped to [0, MagicGold.Max].
 *
 * @param amount Positive to add, negative to spend.
 * @param announce Send a chat message about the change.
 * @return false when a deduction was not covered by the balance (nothing changes).
 */
bool Player::ModifyMagicGold(int32 amount, bool announce)
{
    if (amount == 0)
    {
        return true;
    }

    if (amount < 0 && uint32(-amount) > m_magicGold)
    {
        return false;                                       // not enough Magic Gold
    }

    int64 newValue = int64(m_magicGold) + amount;
    uint32 cap = sWorld.getConfig(CONFIG_UINT32_MAGIC_GOLD_MAX);

    if (newValue < 0)
    {
        newValue = 0;
    }

    if (newValue > cap)
    {
        newValue = cap;
    }

    m_magicGold = uint32(newValue);

    if (announce && GetSession())
    {
        if (amount > 0)
        {
            ChatHandler(GetSession()).PSendSysMessage("|cff00ccff[Magic Gold]|r You receive |cff00ff00%u|r Magic Gold (balance: |cffffff00%u|r).", uint32(amount), m_magicGold);
        }
        else
        {
            ChatHandler(GetSession()).PSendSysMessage("|cff00ccff[Magic Gold]|r You spend |cffff5555%u|r Magic Gold (balance: |cffffff00%u|r).", uint32(-amount), m_magicGold);
        }
    }

    return true;
}

/**
 * @brief The VIP level clamped by the VIP.MaxLevel config.
 *
 * @return Effective VIP level (0..VIP.MaxLevel).
 */
uint8 Player::GetEffectiveVIPLevel() const
{
    uint32 max = sWorld.getConfig(CONFIG_UINT32_VIP_MAX_LEVEL);
    return m_vipLevel > max ? uint8(max) : m_vipLevel;
}

/**
 * @brief Sets the VIP level of the account (live, without relogging).
 *
 * @param level The new VIP level (0..VIP.MaxLevel, higher values are clamped).
 * @param writeToDb Also update the realmd table account_vip.
 */
void Player::SetVIPLevel(uint8 level, bool writeToDb)
{
    uint32 max = sWorld.getConfig(CONFIG_UINT32_VIP_MAX_LEVEL);
    m_vipLevel = level > max ? uint8(max) : level;

    if (writeToDb)
    {
        if (m_vipLevel > 0)
        {
            LoginDatabase.PExecute("REPLACE INTO `account_vip` (`id`, `level`) VALUES ('%u', '%u')", GetSession()->GetAccountId(), m_vipLevel);
        }
        else
        {
            LoginDatabase.PExecute("DELETE FROM `account_vip` WHERE `id` = '%u'", GetSession()->GetAccountId());
        }
    }

    // the level cap changed: refresh what the client shows for the XP bar
    SetUInt32Value(PLAYER_FIELD_MAX_LEVEL, GetMaxLevel());

    if (GetSession())
    {
        ChatHandler(GetSession()).PSendSysMessage("[VIP] Your VIP level is now %u. Level cap: %u.", m_vipLevel, GetMaxLevel());
    }
}

/**
 * @brief The character level cap of this account: world cap + VIP levels.
 *
 * @return The per-account maximum character level (capped at the MAX_LEVEL the
 *         core supports, 100).
 */
uint32 Player::GetMaxLevel() const
{
    uint32 cap = sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL) + GetEffectiveVIPLevel();
    return cap > MAX_LEVEL ? MAX_LEVEL : cap;
}

/**
 * @brief The player name with the VIP color markup for chat channels.
 *
 * @return The plain name for non-VIP players or when the feature is off,
 *         otherwise "|cffRRGGBBname|r".
 */
std::string Player::GetVIPChatName() const
{
    if (!sWorld.getConfig(CONFIG_BOOL_VIP_CHAT_COLOR_ENABLE))
    {
        return std::string(GetName());
    }

    uint8 vip = GetEffectiveVIPLevel();

    if (!vip)
    {
        return std::string(GetName());
    }

    uint32 color = sWorld.GetVIPChatColor(vip);

    if (!color)
    {
        return std::string(GetName());
    }

    char buffer[64];
    snprintf(buffer, sizeof(buffer), "|cff%06x%s|r", color, GetName());
    return std::string(buffer);
}

/**
 * @brief Places an item into an equipment slot and updates visible state.
 *
 * @param slot The destination equipment slot.
 * @param pItem The item to visualize.
 */
void Player::VisualizeItem(uint8 slot, Item* pItem)
{
    if (!pItem)
    {
        return;
    }

    // check also  BIND_WHEN_PICKED_UP and BIND_QUEST_ITEM for .additem or .additemset case by GM (not binded at adding to inventory)
    ItemPrototype const* itemProto = pItem->GetProto();
    if (itemProto->Bonding == BIND_WHEN_EQUIPPED || itemProto->Bonding == BIND_WHEN_PICKED_UP || itemProto->Bonding == BIND_QUEST_ITEM)
    {
        pItem->SetBinding(true);
    }

    DEBUG_LOG("STORAGE: EquipItem slot = %u, item = %u", slot, pItem->GetEntry());

    m_items[slot] = pItem;
    SetGuidValue(PLAYER_FIELD_INV_SLOT_HEAD + (slot * 2), pItem->GetObjectGuid());
    pItem->SetGuidValue(ITEM_FIELD_CONTAINED, GetObjectGuid());
    pItem->SetGuidValue(ITEM_FIELD_OWNER, GetObjectGuid());
    pItem->SetSlot(slot);
    pItem->SetContainer(NULL);

    if (slot < EQUIPMENT_SLOT_END)
    {
        SetVisibleItemSlot(slot, pItem);
    }

    pItem->SetState(ITEM_CHANGED, this);
}

/**
 * @brief Temporarily removes an item from player storage without deleting it.
 *
 * @param bag The bag containing the item.
 * @param slot The slot containing the item.
 * @param update True to send inventory updates to the client.
 */
void Player::RemoveItem(uint8 bag, uint8 slot, bool update)
{
    // note: removeitem does not actually change the item
    // it only takes the item out of storage temporarily
    // note2: if removeitem is to be used for delinking
    // the item must be removed from the player's updatequeue

    Item* pItem = GetItemByPos(bag, slot);
    if (pItem)
    {
        DEBUG_LOG("STORAGE: RemoveItem bag = %u, slot = %u, item = %u", bag, slot, pItem->GetEntry());

        RemoveEnchantmentDurations(pItem);
        RemoveItemDurations(pItem);

        if (bag == INVENTORY_SLOT_BAG_0)
        {
            if (slot < INVENTORY_SLOT_BAG_END)
            {
                ItemPrototype const* pProto = pItem->GetProto();
                // item set bonuses applied only at equip and removed at unequip, and still active for broken items

                if (pProto && pProto->ItemSet)
                {
                    RemoveItemsSetItem(this, pProto);
                }

                _ApplyItemMods(pItem, slot, false);

                // remove item dependent auras and casts (only weapon and armor slots)
                if (slot < EQUIPMENT_SLOT_END)
                {
                    RemoveItemDependentAurasAndCasts(pItem);

                    // remove held enchantments, update expertise
                    if (slot == EQUIPMENT_SLOT_MAINHAND)
                    {
                        if (pItem->GetItemSuffixFactor())
                        {
                            pItem->ClearEnchantment(PROP_ENCHANTMENT_SLOT_3);
                            pItem->ClearEnchantment(PROP_ENCHANTMENT_SLOT_4);
                        }
                        else
                        {
                            pItem->ClearEnchantment(PROP_ENCHANTMENT_SLOT_0);
                            pItem->ClearEnchantment(PROP_ENCHANTMENT_SLOT_1);
                        }

                        UpdateExpertise(BASE_ATTACK);
                    }
                    else if (slot == EQUIPMENT_SLOT_OFFHAND)
                    {
                        UpdateExpertise(OFF_ATTACK);
                    }
                }
            }

            m_items[slot] = NULL;
            SetGuidValue(PLAYER_FIELD_INV_SLOT_HEAD + (slot * 2), ObjectGuid());

            if (slot < EQUIPMENT_SLOT_END)
            {
                SetVisibleItemSlot(slot, NULL);
            }
        }
        else
        {
            Bag* pBag = (Bag*)GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
            if (pBag)
            {
                pBag->RemoveItem(slot);
            }
        }
        pItem->SetGuidValue(ITEM_FIELD_CONTAINED, ObjectGuid());
        // pItem->SetGuidValue(ITEM_FIELD_OWNER, ObjectGuid()); not clear owner at remove (it will be set at store). This used in mail and auction code
        pItem->SetSlot(NULL_SLOT);

        // ApplyItemOnStoreSpell, for avoid re-apply will remove at _adding_ to not appropriate slot

        if (IsInWorld() && update)
        {
            pItem->SendCreateUpdateToPlayer(this);
        }
    }
}

// Common operation need to remove item from inventory without delete in trade, auction, guild bank, mail....
void Player::MoveItemFromInventory(uint8 bag, uint8 slot, bool update)
{
    if (Item* it = GetItemByPos(bag, slot))
    {
        ItemRemovedQuestCheck(it->GetEntry(), it->GetCount());
        RemoveItem(bag, slot, update);

        // item atStore spell not removed in RemoveItem (for avoid reappaly in slots changes), so do it directly
        if (IsEquipmentPos(bag, slot) || IsInventoryPos(bag, slot))
        {
            ApplyItemOnStoreSpell(it, false);
        }

        it->RemoveFromUpdateQueueOf(this);
        if (it->IsInWorld())
        {
            it->RemoveFromWorld();
            it->DestroyForPlayer(this);
        }
    }
}

// Common operation need to add item from inventory without delete in trade, guild bank, mail....
void Player::MoveItemToInventory(ItemPosCountVec const& dest, Item* pItem, bool update, bool in_characterInventoryDB)
{
    // update quest counters
    ItemAddedQuestCheck(pItem->GetEntry(), pItem->GetCount());

    // store item
    Item* pLastItem = StoreItem(dest, pItem, update);

    // only set if not merged to existing stack (pItem can be deleted already but we can compare pointers any way)
    if (pLastItem == pItem)
    {
        // update owner for last item (this can be original item with wrong owner
        if (pLastItem->GetOwnerGuid() != GetObjectGuid())
        {
            pLastItem->SetOwnerGuid(GetObjectGuid());
        }

        // if this original item then it need create record in inventory
        // in case trade we already have item in other player inventory
        pLastItem->SetState(in_characterInventoryDB ? ITEM_CHANGED : ITEM_NEW, this);
    }
}

/**
 * @brief Permanently destroys an item from player storage.
 *
 * @param bag The bag containing the item.
 * @param slot The slot containing the item.
 * @param update True to send inventory updates to the client.
 */
void Player::DestroyItem(uint8 bag, uint8 slot, bool update)
{
    Item* pItem = GetItemByPos(bag, slot);
    if (pItem)
    {
        DEBUG_LOG("STORAGE: DestroyItem bag = %u, slot = %u, item = %u", bag, slot, pItem->GetEntry());

        // start from destroy contained items (only equipped bag can have its)
        if (pItem->IsBag() && pItem->IsEquipped())          // this also prevent infinity loop if empty bag stored in bag==slot
        {
            for (int i = 0; i < MAX_BAG_SIZE; ++i)
            {
                DestroyItem(slot, i, update);
            }
        }

        if (pItem->HasFlag(ITEM_FIELD_FLAGS, ITEM_DYNFLAG_WRAPPED))
        {
            static SqlStatementID delGifts ;

            SqlStatement stmt = CharacterDatabase.CreateStatement(delGifts, "DELETE FROM `character_gifts` WHERE `item_guid` = ?");
            stmt.PExecute(pItem->GetGUIDLow());
        }

        RemoveEnchantmentDurations(pItem);
        RemoveItemDurations(pItem);

        if (IsEquipmentPos(bag, slot) || IsInventoryPos(bag, slot))
        {
            ApplyItemOnStoreSpell(pItem, false);
        }

        ItemRemovedQuestCheck(pItem->GetEntry(), pItem->GetCount());
#ifdef ENABLE_ELUNA
        if (Eluna* e = GetEluna())
        {
            e->OnRemove(this, pItem);
        }
#endif /* ENABLE_ELUNA */

        if (bag == INVENTORY_SLOT_BAG_0)
        {
            SetGuidValue(PLAYER_FIELD_INV_SLOT_HEAD + (slot * 2), ObjectGuid());

            // equipment and equipped bags can have applied bonuses
            if (slot < INVENTORY_SLOT_BAG_END)
            {
                ItemPrototype const* pProto = pItem->GetProto();

                // item set bonuses applied only at equip and removed at unequip, and still active for broken items
                if (pProto && pProto->ItemSet)
                {
                    RemoveItemsSetItem(this, pProto);
                }

                _ApplyItemMods(pItem, slot, false);
            }

            if (slot < EQUIPMENT_SLOT_END)
            {
                // remove item dependent auras and casts (only weapon and armor slots)
                RemoveItemDependentAurasAndCasts(pItem);

                // update expertise
                if (slot == EQUIPMENT_SLOT_MAINHAND)
                {
                    UpdateExpertise(BASE_ATTACK);
                }
                else if (slot == EQUIPMENT_SLOT_OFFHAND)
                {
                    UpdateExpertise(OFF_ATTACK);
                }

                // equipment visual show
                SetVisibleItemSlot(slot, NULL);
            }

            m_items[slot] = NULL;
        }
        else if (Bag* pBag = (Bag*)GetItemByPos(INVENTORY_SLOT_BAG_0, bag))
        {
            pBag->RemoveItem(slot);
        }

        if (IsInWorld() && update)
        {
            pItem->RemoveFromWorld();
            pItem->DestroyForPlayer(this);
        }

        // pItem->SetOwnerGUID(0);
        pItem->SetGuidValue(ITEM_FIELD_CONTAINED, ObjectGuid());
        pItem->SetSlot(NULL_SLOT);
        pItem->SetState(ITEM_REMOVED, this);
    }
}

/**
 * @brief Destroys up to a requested count of an item across player storage.
 *
 * @param item The item entry to destroy.
 * @param count The requested quantity to destroy.
 * @param update True to send inventory updates to the client.
 * @param unequip_check True to validate equipped items before destroying them.
 * @param delete_from_bank True to include bank storage in the search.
 * @param delete_from_buyback True to include vendor buyback slots in the search.
 * @return The number of items removed.
 */
uint32 Player::DestroyItemCount(uint32 item, uint32 count, bool update, bool unequip_check, bool delete_from_bank, bool delete_from_buyback)
{
    DEBUG_LOG("STORAGE: DestroyItemCount item = %u, count = %u", item, count);
    uint32 remcount = 0;

    // Search in default bagpack
    for (int i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
    {
        if (Item* pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, i))
        {
            if (pItem->GetEntry() == item && !pItem->IsInTrade())
            {
                if (pItem->GetCount() + remcount <= count)
                {
                    // all items in inventory can unequipped
                    remcount += pItem->GetCount();
                    DestroyItem(INVENTORY_SLOT_BAG_0, i, update);

                    if (remcount >= count)
                    {
                        return remcount;
                    }
                }
                else
                {
                    ItemRemovedQuestCheck(pItem->GetEntry(), count - remcount);
                    pItem->SetCount(pItem->GetCount() - count + remcount);
                    if (IsInWorld() && update)
                    {
                        pItem->SendCreateUpdateToPlayer(this);
                    }
                    pItem->SetState(ITEM_CHANGED, this);
                    return remcount;
                }
            }
        }
    }

    // Search in keyring slots
    for (int i = KEYRING_SLOT_START; i < KEYRING_SLOT_END; ++i)
    {
        if (Item* pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, i))
        {
            if (pItem->GetEntry() == item && !pItem->IsInTrade())
            {
                if (pItem->GetCount() + remcount <= count)
                {
                    // all keys can be unequipped
                    remcount += pItem->GetCount();
                    DestroyItem(INVENTORY_SLOT_BAG_0, i, update);

                    if (remcount >= count)
                    {
                        return remcount;
                    }
                }
                else
                {
                    ItemRemovedQuestCheck(pItem->GetEntry(), count - remcount);
                    pItem->SetCount(pItem->GetCount() - count + remcount);
                    if (IsInWorld() && update)
                    {
                        pItem->SendCreateUpdateToPlayer(this);
                    }
                    pItem->SetState(ITEM_CHANGED, this);
                    return remcount;
                }
            }
        }
    }

    // Search in inventory bags
    for (int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
    {
        if (Bag* pBag = (Bag*)GetItemByPos(INVENTORY_SLOT_BAG_0, i))
        {
            for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
            {
                if (Item* pItem = pBag->GetItemByPos(j))
                {
                    if (pItem->GetEntry() == item && !pItem->IsInTrade())
                    {
                        // all items in bags can be unequipped
                        if (pItem->GetCount() + remcount <= count)
                        {
                            remcount += pItem->GetCount();
                            DestroyItem(i, j, update);

                            if (remcount >= count)
                            {
                                return remcount;
                            }
                        }
                        else
                        {
                            ItemRemovedQuestCheck(pItem->GetEntry(), count - remcount);
                            pItem->SetCount(pItem->GetCount() - count + remcount);
                            if (IsInWorld() && update)
                            {
                                pItem->SendCreateUpdateToPlayer(this);
                            }
                            pItem->SetState(ITEM_CHANGED, this);
                            return remcount;
                        }
                    }
                }
            }
        }
    }

    // Search in Equiped items
    for (int i = EQUIPMENT_SLOT_START; i < EQUIPMENT_SLOT_END; ++i)
    {
        if (Item* pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, i))
        {
            if (pItem && pItem->GetEntry() == item && !pItem->IsInTrade())
            {
                if (pItem->GetCount() + remcount <= count)
                {
                    if (!unequip_check || CanUnequipItem(INVENTORY_SLOT_BAG_0 << 8 | i, false) == EQUIP_ERR_OK)
                    {
                        remcount += pItem->GetCount();
                        DestroyItem(INVENTORY_SLOT_BAG_0, i, update);
                        if (remcount >= count)
                        {
                            return remcount;
                        }
                    }
                }
                else
                {
                    ItemRemovedQuestCheck(pItem->GetEntry(), count - remcount);
                    pItem->SetCount(pItem->GetCount() - count + remcount);
                    if (IsInWorld() && update)
                    {
                        pItem->SendCreateUpdateToPlayer(this);
                    }
                    pItem->SetState(ITEM_CHANGED, this);
                    return remcount;
                }
            }
        }
    }

    // Search in bank items
    if (delete_from_bank)
    {
        // Normal bank slots
        for (int i = BANK_SLOT_ITEM_START; i < BANK_SLOT_ITEM_END; ++i)
        {
            if (Item* pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            {
                if (pItem->GetEntry() == item && !pItem->IsInTrade())
                {
                    if (pItem->GetCount() + remcount <= count)
                    {
                        // all items in inventory can unequipped
                        remcount += pItem->GetCount();
                        DestroyItem(INVENTORY_SLOT_BAG_0, i, update);

                        if (remcount >= count)
                        {
                            return remcount;
                        }
                    }
                    else
                    {
                        ItemRemovedQuestCheck(pItem->GetEntry(), count - remcount);
                        pItem->SetCount(pItem->GetCount() - count + remcount);
                        if (IsInWorld() && update)
                        {
                            pItem->SendCreateUpdateToPlayer(this);
                        }
                        pItem->SetState(ITEM_CHANGED, this);
                        return remcount;
                    }
                }
            }
        }

        // Bank bagslots
        for (int i = BANK_SLOT_BAG_START; i < BANK_SLOT_BAG_END; ++i)
        {
            if (Bag* pBag = (Bag*)GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            {
                for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
                {
                    if (Item* pItem = pBag->GetItemByPos(j))
                    {
                        if (pItem->GetEntry() == item && !pItem->IsInTrade())
                        {
                            // all items in bags can be unequipped
                            if (pItem->GetCount() + remcount <= count)
                            {
                                remcount += pItem->GetCount();
                                DestroyItem(i, j, update);

                                if (remcount >= count)
                                {
                                    return remcount;
                                }
                            }
                            else
                            {
                                ItemRemovedQuestCheck(pItem->GetEntry(), count - remcount);
                                pItem->SetCount(pItem->GetCount() - count + remcount);
                                if (IsInWorld() && update)
                                {
                                    pItem->SendCreateUpdateToPlayer(this);
                                }
                                pItem->SetState(ITEM_CHANGED, this);
                                return remcount;
                            }
                        }
                    }
                }
            }
        }
    }

    // Search in buyback npcs vendor tab
    if (delete_from_buyback)
    {
        for (int i = BUYBACK_SLOT_START; i < BUYBACK_SLOT_END; ++i)
        {
            if (Item* pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            {
                if (pItem->GetEntry() == item && !pItem->IsInTrade())
                {
                    if (pItem->GetCount() + remcount <= count)
                    {
                        // all keys can be unequipped
                        remcount += pItem->GetCount();
                        DestroyItem(INVENTORY_SLOT_BAG_0, i, update);

                        if (remcount >= count)
                        {
                            return remcount;
                        }
                    }
                    else
                    {
                        ItemRemovedQuestCheck(pItem->GetEntry(), count - remcount);
                        pItem->SetCount(pItem->GetCount() - count + remcount);
                        if (IsInWorld() && update)
                        {
                            pItem->SendCreateUpdateToPlayer(this);
                        }
                        pItem->SetState(ITEM_CHANGED, this);
                        return remcount;
                    }
                }
            }
        }
    }

    return remcount;
}

/**
 * @brief Destroys items that are no longer valid in the player's current zone or map.
 *
 * @param update True to send inventory updates to the client.
 * @param new_zone The zone identifier to validate against.
 */
void Player::DestroyZoneLimitedItem(bool update, uint32 new_zone)
{
    DEBUG_LOG("STORAGE: DestroyZoneLimitedItem in map %u and area %u", GetMapId(), new_zone);

    // in inventory
    for (int i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
    {
        if (Item* pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            if (pItem->IsLimitedToAnotherMapOrZone(GetMapId(), new_zone))
            {
                DestroyItem(INVENTORY_SLOT_BAG_0, i, update);
            }
    }

    for (int i = KEYRING_SLOT_START; i < KEYRING_SLOT_END; ++i)
    {
        if (Item* pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            if (pItem->IsLimitedToAnotherMapOrZone(GetMapId(), new_zone))
            {
                DestroyItem(INVENTORY_SLOT_BAG_0, i, update);
            }
    }

    // in inventory bags
    for (int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
    {
        if (Bag* pBag = (Bag*)GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
            {
                if (Item* pItem = pBag->GetItemByPos(j))
                    if (pItem->IsLimitedToAnotherMapOrZone(GetMapId(), new_zone))
                    {
                        DestroyItem(i, j, update);
                    }
            }
    }

    // in equipment and bag list
    for (int i = EQUIPMENT_SLOT_START; i < INVENTORY_SLOT_BAG_END; ++i)
    {
        if (Item* pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            if (pItem->IsLimitedToAnotherMapOrZone(GetMapId(), new_zone))
            {
                DestroyItem(INVENTORY_SLOT_BAG_0, i, update);
            }
    }
}

/**
 * @brief Destroys conjured consumables from player storage.
 *
 * @param update True to send inventory updates to the client.
 */
void Player::DestroyConjuredItems(bool update)
{
    // used when entering arena
    // destroys all conjured items
    DEBUG_LOG("STORAGE: DestroyConjuredItems");

    // in inventory
    for (int i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
    {
        if (Item* pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            if (pItem->IsConjuredConsumable())
            {
                DestroyItem(INVENTORY_SLOT_BAG_0, i, update);
            }
    }

    // in inventory bags
    for (int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
    {
        if (Bag* pBag = (Bag*)GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
            {
                if (Item* pItem = pBag->GetItemByPos(j))
                    if (pItem->IsConjuredConsumable())
                    {
                        DestroyItem(i, j, update);
                    }
            }
    }

    // in equipment and bag list
    for (int i = EQUIPMENT_SLOT_START; i < INVENTORY_SLOT_BAG_END; ++i)
    {
        if (Item* pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            if (pItem->IsConjuredConsumable())
            {
                DestroyItem(INVENTORY_SLOT_BAG_0, i, update);
            }
    }
}

/**
 * @brief Destroys or decrements a specific item instance by a requested count.
 *
 * @param pItem The item instance to modify.
 * @param count The remaining quantity to destroy; updated by the call.
 * @param update True to send inventory updates to the client.
 */
void Player::DestroyItemCount(Item* pItem, uint32& count, bool update)
{
    if (!pItem)
    {
        return;
    }

    DEBUG_LOG("STORAGE: DestroyItemCount item (GUID: %u, Entry: %u) count = %u", pItem->GetGUIDLow(), pItem->GetEntry(), count);

    if (pItem->GetCount() <= count)
    {
        count -= pItem->GetCount();

        DestroyItem(pItem->GetBagSlot(), pItem->GetSlot(), update);
    }
    else
    {
        ItemRemovedQuestCheck(pItem->GetEntry(), count);
        pItem->SetCount(pItem->GetCount() - count);
        count = 0;
        if (IsInWorld() && update)
        {
            pItem->SendCreateUpdateToPlayer(this);
        }
        pItem->SetState(ITEM_CHANGED, this);
    }
}

/**
 * @brief Splits a stack into a new destination position.
 *
 * @param src The packed source position.
 * @param dst The packed destination position.
 * @param count The quantity to split from the source stack.
 */
void Player::SplitItem(uint16 src, uint16 dst, uint32 count)
{
    uint8 srcbag = src >> 8;
    uint8 srcslot = src & 255;

    uint8 dstbag = dst >> 8;
    uint8 dstslot = dst & 255;

    Item* pSrcItem = GetItemByPos(srcbag, srcslot);
    if (!pSrcItem)
    {
        SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, pSrcItem, NULL);
        return;
    }

    if (pSrcItem->HasGeneratedLoot())                       // prevent split looting item (stackable items can has only temporary loot and this meaning that loot window open)
    {
        // best error message found for attempting to split while looting
        SendEquipError(EQUIP_ERR_COULDNT_SPLIT_ITEMS, pSrcItem, NULL);
        return;
    }

    // not let split all items (can be only at cheating)
    if (pSrcItem->GetCount() == count)
    {
        SendEquipError(EQUIP_ERR_COULDNT_SPLIT_ITEMS, pSrcItem, NULL);
        return;
    }

    // not let split more existing items (can be only at cheating)
    if (pSrcItem->GetCount() < count)
    {
        SendEquipError(EQUIP_ERR_TRIED_TO_SPLIT_MORE_THAN_COUNT, pSrcItem, NULL);
        return;
    }

    DEBUG_LOG("STORAGE: SplitItem bag = %u, slot = %u, item = %u, count = %u", dstbag, dstslot, pSrcItem->GetEntry(), count);
    Item* pNewItem = pSrcItem->CloneItem(count, this);
    if (!pNewItem)
    {
        SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, pSrcItem, NULL);
        return;
    }

    if (IsInventoryPos(dst))
    {
        // change item amount before check (for unique max count check)
        pSrcItem->SetCount(pSrcItem->GetCount() - count);

        ItemPosCountVec dest;
        InventoryResult msg = CanStoreItem(dstbag, dstslot, dest, pNewItem, false);
        if (msg != EQUIP_ERR_OK)
        {
            delete pNewItem;
            pSrcItem->SetCount(pSrcItem->GetCount() + count);
            SendEquipError(msg, pSrcItem, NULL);
            return;
        }

        if (IsInWorld())
        {
            pSrcItem->SendCreateUpdateToPlayer(this);
        }
        pSrcItem->SetState(ITEM_CHANGED, this);
        StoreItem(dest, pNewItem, true);
    }
    else if (IsBankPos(dst))
    {
        // change item amount before check (for unique max count check)
        pSrcItem->SetCount(pSrcItem->GetCount() - count);

        ItemPosCountVec dest;
        InventoryResult msg = CanBankItem(dstbag, dstslot, dest, pNewItem, false);
        if (msg != EQUIP_ERR_OK)
        {
            delete pNewItem;
            pSrcItem->SetCount(pSrcItem->GetCount() + count);
            SendEquipError(msg, pSrcItem, NULL);
            return;
        }

        if (IsInWorld())
        {
            pSrcItem->SendCreateUpdateToPlayer(this);
        }
        pSrcItem->SetState(ITEM_CHANGED, this);
        BankItem(dest, pNewItem, true);
    }
    else if (IsEquipmentPos(dst))
    {
        // change item amount before check (for unique max count check), provide space for splitted items
        pSrcItem->SetCount(pSrcItem->GetCount() - count);

        uint16 dest;
        InventoryResult msg = CanEquipItem(dstslot, dest, pNewItem, false);
        if (msg != EQUIP_ERR_OK)
        {
            delete pNewItem;
            pSrcItem->SetCount(pSrcItem->GetCount() + count);
            SendEquipError(msg, pSrcItem, NULL);
            return;
        }

        if (IsInWorld())
        {
            pSrcItem->SendCreateUpdateToPlayer(this);
        }
        pSrcItem->SetState(ITEM_CHANGED, this);
        EquipItem(dest, pNewItem, true);
        AutoUnequipOffhandIfNeed();
    }
}

/**
 * @brief Moves, merges, or swaps items between two storage positions.
 *
 * @param src The packed source position.
 * @param dst The packed destination position.
 */
void Player::SwapItem(uint16 src, uint16 dst)
{
    uint8 srcbag = src >> 8;
    uint8 srcslot = src & 255;

    uint8 dstbag = dst >> 8;
    uint8 dstslot = dst & 255;

    Item* pSrcItem = GetItemByPos(srcbag, srcslot);
    Item* pDstItem = GetItemByPos(dstbag, dstslot);

    if (!pSrcItem)
    {
        return;
    }

    DEBUG_LOG("STORAGE: SwapItem bag = %u, slot = %u, item = %u", dstbag, dstslot, pSrcItem->GetEntry());

    if (!IsAlive())
    {
        SendEquipError(EQUIP_ERR_YOU_ARE_DEAD, pSrcItem, pDstItem);
        return;
    }

    // SRC checks

    // check unequip potability for equipped items and bank bags
    if (IsEquipmentPos(src) || IsBagPos(src))
    {
        // bags can be swapped with empty bag slots, or with empty bag (items move possibility checked later)
        InventoryResult msg = CanUnequipItem(src, !IsBagPos(src) || IsBagPos(dst) || (pDstItem && pDstItem->IsBag() && ((Bag*)pDstItem)->IsEmpty()));
        if (msg != EQUIP_ERR_OK)
        {
            SendEquipError(msg, pSrcItem, pDstItem);
            return;
        }
    }

    // prevent put equipped/bank bag in self
    if (IsBagPos(src) && srcslot == dstbag)
    {
        SendEquipError(EQUIP_ERR_NONEMPTY_BAG_OVER_OTHER_BAG, pSrcItem, pDstItem);
        return;
    }

    // prevent put equipped/bank bag in self
    if (IsBagPos(dst) && dstslot == srcbag)
    {
        SendEquipError(EQUIP_ERR_NONEMPTY_BAG_OVER_OTHER_BAG, pDstItem, pSrcItem);
        return;
    }

    // DST checks

    if (pDstItem)
    {
        // check unequip potability for equipped items and bank bags
        if (IsEquipmentPos(dst) || IsBagPos(dst))
        {
            // bags can be swapped with empty bag slots, or with empty bag (items move possibility checked later)
            InventoryResult msg = CanUnequipItem(dst, !IsBagPos(dst) || IsBagPos(src) || (pSrcItem->IsBag() && ((Bag*)pSrcItem)->IsEmpty()));
            if (msg != EQUIP_ERR_OK)
            {
                SendEquipError(msg, pSrcItem, pDstItem);
                return;
            }
        }
    }

    // NOW this is or item move (swap with empty), or swap with another item (including bags in bag possitions)
    // or swap empty bag with another empty or not empty bag (with items exchange)

    // Move case
    if (!pDstItem)
    {
        if (IsInventoryPos(dst))
        {
            ItemPosCountVec dest;
            InventoryResult msg = CanStoreItem(dstbag, dstslot, dest, pSrcItem, false);
            if (msg != EQUIP_ERR_OK)
            {
                SendEquipError(msg, pSrcItem, NULL);
                return;
            }

            RemoveItem(srcbag, srcslot, true);
            StoreItem(dest, pSrcItem, true);
        }
        else if (IsBankPos(dst))
        {
            ItemPosCountVec dest;
            InventoryResult msg = CanBankItem(dstbag, dstslot, dest, pSrcItem, false);
            if (msg != EQUIP_ERR_OK)
            {
                SendEquipError(msg, pSrcItem, NULL);
                return;
            }

            RemoveItem(srcbag, srcslot, true);
            BankItem(dest, pSrcItem, true);
        }
        else if (IsEquipmentPos(dst))
        {
            uint16 dest;
            InventoryResult msg = CanEquipItem(dstslot, dest, pSrcItem, false);
            if (msg != EQUIP_ERR_OK)
            {
                SendEquipError(msg, pSrcItem, NULL);
                return;
            }

            RemoveItem(srcbag, srcslot, true);
            EquipItem(dest, pSrcItem, true);
            AutoUnequipOffhandIfNeed();
        }

        return;
    }

    // attempt merge to / fill target item
    if (!pSrcItem->IsBag() && !pDstItem->IsBag())
    {
        InventoryResult msg;
        ItemPosCountVec sDest;
        uint16 eDest;
        if (IsInventoryPos(dst))
        {
            msg = CanStoreItem(dstbag, dstslot, sDest, pSrcItem, false);
        }
        else if (IsBankPos(dst))
        {
            msg = CanBankItem(dstbag, dstslot, sDest, pSrcItem, false);
        }
        else if (IsEquipmentPos(dst))
        {
            msg = CanEquipItem(dstslot, eDest, pSrcItem, false);
        }
        else
        {
            return;
        }

        // can be merge/fill
        if (msg == EQUIP_ERR_OK)
        {
            ItemPrototype const* itemProto = pSrcItem->GetProto();
            if (pSrcItem->GetCount() + pDstItem->GetCount() <= itemProto->GetMaxStackSize())
            {
                RemoveItem(srcbag, srcslot, true);

                if (IsInventoryPos(dst))
                {
                    StoreItem(sDest, pSrcItem, true);
                }
                else if (IsBankPos(dst))
                {
                    BankItem(sDest, pSrcItem, true);
                }
                else if (IsEquipmentPos(dst))
                {
                    EquipItem(eDest, pSrcItem, true);
                    AutoUnequipOffhandIfNeed();
                }
            }
            else
            {
                pSrcItem->SetCount(pSrcItem->GetCount() + pDstItem->GetCount() - itemProto->GetMaxStackSize());
                pDstItem->SetCount(itemProto->GetMaxStackSize());
                pSrcItem->SetState(ITEM_CHANGED, this);
                pDstItem->SetState(ITEM_CHANGED, this);
                if (IsInWorld())
                {
                    pSrcItem->SendCreateUpdateToPlayer(this);
                    pDstItem->SendCreateUpdateToPlayer(this);
                }
            }
            return;
        }
    }

    // impossible merge/fill, do real swap
    InventoryResult msg = EQUIP_ERR_YOU_CAN_NEVER_USE_THAT_ITEM; // Initialize msg with a default value which will never be used

    // check src->dest move possibility
    ItemPosCountVec sDest;
    uint16 eDest = 0;
    if (IsInventoryPos(dst))
    {
        msg = CanStoreItem(dstbag, dstslot, sDest, pSrcItem, true);
    }
    else if (IsBankPos(dst))
    {
        msg = CanBankItem(dstbag, dstslot, sDest, pSrcItem, true);
    }
    else if (IsEquipmentPos(dst))
    {
        msg = CanEquipItem(dstslot, eDest, pSrcItem, true);
        if (msg == EQUIP_ERR_OK)
        {
            msg = CanUnequipItem(eDest, true);
        }
    }

    if (msg != EQUIP_ERR_OK)
    {
        SendEquipError(msg, pSrcItem, pDstItem);
        return;
    }

    // check dest->src move possibility
    ItemPosCountVec sDest2;
    uint16 eDest2 = 0;
    if (IsInventoryPos(src))
    {
        msg = CanStoreItem(srcbag, srcslot, sDest2, pDstItem, true);
    }
    else if (IsBankPos(src))
    {
        msg = CanBankItem(srcbag, srcslot, sDest2, pDstItem, true);
    }
    else if (IsEquipmentPos(src))
    {
        msg = CanEquipItem(srcslot, eDest2, pDstItem, true);
        if (msg == EQUIP_ERR_OK)
        {
            msg = CanUnequipItem(eDest2, true);
        }
    }

    if (msg != EQUIP_ERR_OK)
    {
        SendEquipError(msg, pDstItem, pSrcItem);
        return;
    }

    // Check bag swap with item exchange (one from empty in not bag possition (equipped (not possible in fact) or store)
    if (pSrcItem->IsBag() && pDstItem->IsBag())
    {
        Bag* emptyBag = NULL;
        Bag* fullBag = NULL;
        if (((Bag*)pSrcItem)->IsEmpty() && !IsBagPos(src))
        {
            emptyBag = (Bag*)pSrcItem;
            fullBag  = (Bag*)pDstItem;
        }
        else if (((Bag*)pDstItem)->IsEmpty() && !IsBagPos(dst))
        {
            emptyBag = (Bag*)pDstItem;
            fullBag  = (Bag*)pSrcItem;
        }

        // bag swap (with items exchange) case
        if (emptyBag && fullBag)
        {
            ItemPrototype const* emotyProto = emptyBag->GetProto();

            uint32 count = 0;

            for (uint32 i = 0; i < fullBag->GetBagSize(); ++i)
            {
                Item* bagItem = fullBag->GetItemByPos(i);
                if (!bagItem)
                {
                    continue;
                }

                ItemPrototype const* bagItemProto = bagItem->GetProto();
                if (!bagItemProto || !ItemCanGoIntoBag(bagItemProto, emotyProto))
                {
                    // one from items not go to empty target bag
                    SendEquipError(EQUIP_ERR_NONEMPTY_BAG_OVER_OTHER_BAG, pSrcItem, pDstItem);
                    return;
                }

                ++count;
            }

            if (count > emptyBag->GetBagSize())
            {
                // too small targeted bag
                SendEquipError(EQUIP_ERR_ITEMS_CANT_BE_SWAPPED, pSrcItem, pDstItem);
                return;
            }

            // Items swap
            count = 0;                                      // will pos in new bag
            for (uint32 i = 0; i < fullBag->GetBagSize(); ++i)
            {
                Item* bagItem = fullBag->GetItemByPos(i);
                if (!bagItem)
                {
                    continue;
                }

                fullBag->RemoveItem(i);
                emptyBag->StoreItem(count, bagItem);
                bagItem->SetState(ITEM_CHANGED, this);

                ++count;
            }
        }
    }

    // now do moves, remove...
    RemoveItem(dstbag, dstslot, false);
    RemoveItem(srcbag, srcslot, false);

    // add to dest
    if (IsInventoryPos(dst))
    {
        StoreItem(sDest, pSrcItem, true);
    }
    else if (IsBankPos(dst))
    {
        BankItem(sDest, pSrcItem, true);
    }
    else if (IsEquipmentPos(dst))
    {
        EquipItem(eDest, pSrcItem, true);
    }

    // add to src
    if (IsInventoryPos(src))
    {
        StoreItem(sDest2, pDstItem, true);
    }
    else if (IsBankPos(src))
    {
        BankItem(sDest2, pDstItem, true);
    }
    else if (IsEquipmentPos(src))
    {
        EquipItem(eDest2, pDstItem, true);
    }

    AutoUnequipOffhandIfNeed();
}

/**
 * @brief Adds an item to the vendor buyback list.
 *
 * @param pItem The item to place into a buyback slot.
 */
void Player::AddItemToBuyBackSlot(Item* pItem)
{
    if (pItem)
    {
        uint32 slot = m_currentBuybackSlot;
        // if current back slot non-empty search oldest or free
        if (m_items[slot])
        {
            uint32 oldest_time = GetUInt32Value(PLAYER_FIELD_BUYBACK_TIMESTAMP_1);
            uint32 oldest_slot = BUYBACK_SLOT_START;

            for (uint32 i = BUYBACK_SLOT_START + 1; i < BUYBACK_SLOT_END; ++i)
            {
                // found empty
                if (!m_items[i])
                {
                    slot = i;
                    break;
                }

                uint32 i_time = GetUInt32Value(PLAYER_FIELD_BUYBACK_TIMESTAMP_1 + i - BUYBACK_SLOT_START);

                if (oldest_time > i_time)
                {
                    oldest_time = i_time;
                    oldest_slot = i;
                }
            }

            // find oldest
            slot = oldest_slot;
        }

        RemoveItemFromBuyBackSlot(slot, true);
        DEBUG_LOG("STORAGE: AddItemToBuyBackSlot item = %u, slot = %u", pItem->GetEntry(), slot);

        m_items[slot] = pItem;
        time_t base = time(NULL);
        uint32 etime = uint32(base - m_logintime + (30 * 3600));
        uint32 eslot = slot - BUYBACK_SLOT_START;

        SetGuidValue(PLAYER_FIELD_VENDORBUYBACK_SLOT_1 + (eslot * 2), pItem->GetObjectGuid());
        if (ItemPrototype const* pProto = pItem->GetProto())
        {
            SetUInt32Value(PLAYER_FIELD_BUYBACK_PRICE_1 + eslot, pProto->SellPrice * pItem->GetCount());
        }
        else
        {
            SetUInt32Value(PLAYER_FIELD_BUYBACK_PRICE_1 + eslot, 0);
        }
        SetUInt32Value(PLAYER_FIELD_BUYBACK_TIMESTAMP_1 + eslot, (uint32)etime);

        // move to next (for non filled list is move most optimized choice)
        if (m_currentBuybackSlot < BUYBACK_SLOT_END - 1)
        {
            ++m_currentBuybackSlot;
        }
    }
}

/**
 * @brief Gets an item from a vendor buyback slot.
 *
 * @param slot The buyback slot index.
 * @return The item in the slot, or null if none exists.
 */
Item* Player::GetItemFromBuyBackSlot(uint32 slot)
{
    DEBUG_LOG("STORAGE: GetItemFromBuyBackSlot slot = %u", slot);
    if (slot >= BUYBACK_SLOT_START && slot < BUYBACK_SLOT_END)
    {
        return m_items[slot];
    }
    return NULL;
}

/**
 * @brief Removes an item from a vendor buyback slot.
 *
 * @param slot The buyback slot index.
 * @param del True to mark the removed item for deletion.
 */
void Player::RemoveItemFromBuyBackSlot(uint32 slot, bool del)
{
    DEBUG_LOG("STORAGE: RemoveItemFromBuyBackSlot slot = %u", slot);
    if (slot >= BUYBACK_SLOT_START && slot < BUYBACK_SLOT_END)
    {
        Item* pItem = m_items[slot];
        if (pItem)
        {
            pItem->RemoveFromWorld();
            if (del)
            {
                pItem->SetState(ITEM_REMOVED, this);
            }
        }

        m_items[slot] = NULL;

        uint32 eslot = slot - BUYBACK_SLOT_START;
        SetGuidValue(PLAYER_FIELD_VENDORBUYBACK_SLOT_1 + (eslot * 2), ObjectGuid());
        SetUInt32Value(PLAYER_FIELD_BUYBACK_PRICE_1 + eslot, 0);
        SetUInt32Value(PLAYER_FIELD_BUYBACK_TIMESTAMP_1 + eslot, 0);

        // if current backslot is filled set to now free slot
        if (m_items[m_currentBuybackSlot])
        {
            m_currentBuybackSlot = slot;
        }
    }
}

/**
 * @brief Sends an inventory error packet to the client.
 *
 * @param msg The inventory error code.
 * @param pItem The primary item involved in the error.
 * @param pItem2 The secondary item involved in the error.
 * @param itemid An optional item entry used for some error variants.
 */
void Player::SendEquipError(InventoryResult msg, Item* pItem, Item* pItem2, uint32 itemid /*= 0*/) const
{
    DEBUG_LOG("WORLD: Sent SMSG_INVENTORY_CHANGE_FAILURE (%u)", msg);
    WorldPacket data(SMSG_INVENTORY_CHANGE_FAILURE, (msg == EQUIP_ERR_CANT_EQUIP_LEVEL_I ? 22 : (msg == EQUIP_ERR_OK ? 1 : 18)));
    data << uint8(msg);

    if (msg != EQUIP_ERR_OK)
    {
        if (msg == EQUIP_ERR_CANT_EQUIP_LEVEL_I)
        {
            ItemPrototype const* proto = pItem ? pItem->GetProto() : ObjectMgr::GetItemPrototype(itemid);
            data << uint32(proto ? proto->RequiredLevel : 0);
        }
        data << (pItem ? pItem->GetObjectGuid() : ObjectGuid());
        data << (pItem2 ? pItem2->GetObjectGuid() : ObjectGuid());
        data << uint8(0);                                   // bag type subclass, used with EQUIP_ERR_EVENT_AUTOEQUIP_BIND_CONFIRM and EQUIP_ERR_ITEM_DOESNT_GO_INTO_BAG2
    }
    GetSession()->SendPacket(&data);
}

void Player::SendBuyError(BuyResult msg, Creature* pCreature, uint32 item, uint32 param)
{
    DEBUG_LOG("WORLD: Sent SMSG_BUY_FAILED");
    WorldPacket data(SMSG_BUY_FAILED, (8 + 4 + 4 + 1));
    data << (pCreature ? pCreature->GetObjectGuid() : ObjectGuid());
    data << uint32(item);
    if (param > 0)
    {
        data << uint32(param);
    }
    data << uint8(msg);
    GetSession()->SendPacket(&data);
}

/**
 * @brief Sends a sell failure result to the client.
 *
 * @param msg The sell failure code.
 * @param pCreature The vendor involved in the transaction.
 * @param itemGuid The item GUID that failed to sell.
 * @param param Unused extra parameter.
 */
void Player::SendSellError(SellResult msg, Creature* pCreature, ObjectGuid itemGuid, uint32 param)
{
    DEBUG_LOG("WORLD: Sent SMSG_SELL_ITEM");
    WorldPacket data(SMSG_SELL_ITEM, (8 + 8 + (param ? 4 : 0) + 1)); // last check 2.0.10
    data << (pCreature ? pCreature->GetObjectGuid() : ObjectGuid());
    data << ObjectGuid(itemGuid);
    if (param > 0)
    {
        data << uint32(param);
    }
    data << uint8(msg);
    GetSession()->SendPacket(&data);
}

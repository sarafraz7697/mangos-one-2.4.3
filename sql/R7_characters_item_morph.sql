-- ============================================================================
-- Round 7 - persistent item morph looks (ITEM_VISUAL_PERSIST_PATCH)
-- Database: characters
--
-- Bought item looks (the "Morph My Gear" VIP service). Keyed by item guid,
-- so the look follows the item through relogging. expire_time 0 = permanent.
-- The core loads it at login (before the inventory) and re-applies the look
-- whenever the item is equipped; Player:ItemMorphSlot(slot, id, secs, true)
-- writes here.
-- ============================================================================

CREATE TABLE IF NOT EXISTS `character_item_morph` (
  `item_guid` int(10) unsigned NOT NULL COMMENT 'item_instance.guid of the morphed item',
  `player_guid` int(10) unsigned NOT NULL DEFAULT 0 COMMENT 'owning character guid (cleanup aid)',
  `display_id` int(10) unsigned NOT NULL DEFAULT 0 COMMENT 'display id or item entry to show',
  `expire_time` bigint(20) unsigned NOT NULL DEFAULT 0 COMMENT 'unix timestamp, 0 = never expires',
  PRIMARY KEY (`item_guid`),
  KEY `idx_player_guid` (`player_guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='Saved item visual overrides with optional expiry (round 7)';

-- ============================================================================
-- Round 7 - rented / timed items (TIMED_ITEM_PATCH)
-- Database: characters
--
-- Items with a rental time (for example VIP shop "Rental Gear (24h)"). The
-- core checks the timers every TimedItem.CheckInterval seconds while the
-- character is online, destroys expired items with a message, and cleans
-- expired rows at login. Item:SetItemExpiry(seconds) from Eluna writes here.
-- ============================================================================

CREATE TABLE IF NOT EXISTS `character_item_expiry` (
  `item_guid` int(10) unsigned NOT NULL COMMENT 'item_instance.guid of the rented item',
  `player_guid` int(10) unsigned NOT NULL DEFAULT 0 COMMENT 'owning character guid (cleanup aid)',
  `expire_time` bigint(20) unsigned NOT NULL DEFAULT 0 COMMENT 'unix timestamp when the item is destroyed',
  PRIMARY KEY (`item_guid`),
  KEY `idx_player_guid` (`player_guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='Timed items destroyed at expiry (round 7)';

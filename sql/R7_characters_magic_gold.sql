-- ============================================================================
-- Round 7 - Magic Gold currency (MAGIC_GOLD_PATCH)
-- Database: characters
--
-- Per-character Magic Gold (MG) balance. Earned by killing other characters
-- (MagicGold.PvPKill.* in mangosd.conf), spent through the Eluna methods
-- Player:GetMagicGold / ModifyMagicGold / SetMagicGold (hearthstone shops).
-- The core reads it at login and saves it with every character save.
-- ============================================================================

CREATE TABLE IF NOT EXISTS `character_magic_gold` (
  `guid` int(10) unsigned NOT NULL COMMENT 'character guid',
  `mg` int(10) unsigned NOT NULL DEFAULT 0 COMMENT 'Magic Gold balance',
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='Magic Gold currency per character (round 7)';

-- example: give every existing character a starting bonus of 100 MG (optional)
-- INSERT IGNORE INTO `character_magic_gold` (`guid`, `mg`)
--     SELECT `guid`, 100 FROM `characters`;

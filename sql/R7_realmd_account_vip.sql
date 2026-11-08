-- ============================================================================
-- Round 7 - VIP levels (VIP_PATCH)
-- Database: realmd (the login database, the one realmd.conf points to)
--
-- VIP level per ACCOUNT (not per character): every character of the account
-- gets the bonuses. The world server reads it at login; scripts set it live
-- with the Eluna method Player:SetVIPLevel(level). Level 0 = no VIP.
-- No row = level 0; rows are created on first SetVIPLevel(>= 1).
-- ============================================================================

CREATE TABLE IF NOT EXISTS `account_vip` (
  `id` int(10) unsigned NOT NULL COMMENT 'account id (realmd account.id)',
  `level` tinyint(3) unsigned NOT NULL DEFAULT 0 COMMENT 'VIP level 0-10',
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='VIP level per account (round 7)';

-- example: make account 5 a VIP 3
-- REPLACE INTO `account_vip` (`id`, `level`) VALUES (5, 3);

# Round 8 Patch - mangos-one (TBC 2.4.3)

## نحوه اعمال

از داخل ریپازیتوری (پوشه‌ای که `src/` و `sql/` داخلش است):

```bash
git apply mangos-one_round8.patch
```

بعد از اعمال:

1. **کامپایل مجدد سرور** (تغییرات core در C++ است: Player، Channel، MapManager، Trainer و ...)
2. **اجرای SQL روی دیتابیس world:**
   ```bash
   mysql -u <user> -p <world_db> < sql/R8_world_npc_text_welcome.sql
   ```
3. فایل `mangosd.conf.dist.in` آپدیت شده (کلیدهای جدید `GlobalPvP.*`). اگر `mangosd.conf` قدیمی را نگه داشته‌اید نیازی به کپی نیست — مقادیر پیش‌فرض فعال هستند:
   `GlobalPvP.Enable=1, Map=560 (Old Hillsbrad), Zone=2367, Area=2370, X=2040.62, Y=250.017, Z=62.7466, O=6.26859, SetHomebind=1`
4. اسکریپت‌های Lua جدید/آپدیت شده در `src/mangosd/lua_scripts/`:
   - `70_Hearthstone.lua` (بازنویسی‌شده)
   - `10_Welcome.lua` (جدید) — متن Welcome داخل `npc_text` با ID `60000` قابل ویرایش است.

## خلاصه تغییرات

| # | درخواست | راه‌حل |
|---|---------|--------|
| 1 | Earn XP تا max level اکانت | از round 7 موجود بود (GiveXP + GetMaxLevel + جدول XP تا سطح ۸۰) — بدون تغییر، فقط تأیید شد |
| 2 | نمایش morphitem در لیست کاراکترها | `equipmentCache` حالا entry مورف‌شده را ذخیره می‌کند (`Player::GetShownItemEntryForSlot`) — اول morph، وگرنه خود آیتم |
| 3 | نشان RENTAL برای آیتم‌های اجاره‌ای | تگ `|cffff8800[RENTAL - removed after X day(s)]` زیر توضیحات آیتم در پنجره تأیید و پیام‌های خرید |
| 4 | تأییدیه برای تمام خریدها | پاپ‌آپ Accept/Cancel برای همه منوها + پنجره Confirm برای خریدهای پنجره‌ای (شاپ و مورف) |
| 5 | رنگ اسم VIP در کانال | پکت کانال فقط GUID دارد و کلاینت 2.4.3 اسم را با رنگ کلاس رندر می‌کند؛ حالا پیام VIP ها به صورت `[Channel] \|cffRRGGBBName\|r: text` ارسال می‌شود که واقعاً رنگ را نشان می‌دهد |
| 6 | اسکریپت Welcome | `10_Welcome.lua`: پیام اطلاعات کاراکتر + باز شدن gossip بلافاصله بعد از لاگین با متن سرور (npc_text 60000) |
| 7 | مپ PvP برای همه + ذخیره در hearthstone | مپ 560 به صورت **یک map مشترک غیر-instance** ساخته می‌شود (همه همدیگر را می‌بینند)، FFA PvP برای همه در `Player::UpdateArea`، homebind همه به مختصات داده‌شده منتقل می‌شود (`GlobalPvP.SetHomebind`)، مقصد رایگان GPS هم اضافه شد |
| 8 | خرید VIP با مقادیر بالا | منوی "Buy VIP Level" در VIP Service — پله‌ای (بدون پرش)، قیمت‌ها از 50,000 تا 10,000,000 MG (قابل ویرایش در `VIP_PRICES`) |
| 9 | Learn Skill = صفحه spell trainer | `Player:SendClassTrainerList()` پنجره واقعی trainer را با تمام اسپل‌های کلاس + weapon/shield skillهای همان کلاس باز می‌کند (قیمت طلای عادی). لیست شغل‌ها حذف شد، فقط Riding با MG ماند |
| 10 | Morph Gear با پنجره preview | لیست slotها حالا آیکون دارد؛ کلیک روی slot پنجره preview آیتم‌ها (با آیکون) را باز می‌کند؛ کلیک روی آیتم → تأییدیه → اعمال مورف به صورت **fake inventory** (هیچ آیتم واقعی وارد چمدان نمی‌شود، فقط ظاهر) |

## نکات فنی

- **رنگ کانال:** پیام‌های VIP به‌صورت system message با اسم رنگی ارسال می‌شوند (تنها روش قابل‌رندر در کلاینت 2.4.3). right-click روی اسم این خطوط کار نمی‌کند؛ بقیه پیام‌ها عادی‌اند.
- **مورف و لیست کاراکتر:** لیست کاراکترها بعد از اولین save (logout یا هر ۱۵ دقیقه) ظاهر مورف را نشان می‌دهد؛ در دنیای بازی بلافاصله است.
- **خرید از پنجره:** خرید اول رد می‌شود و پنجره Confirm باز می‌شود؛ بعد از Accept خرید از همان پنجره vendor با قیمت copper صفر و کسر MG انجام می‌شود (ON_BUY_ITEM → تأیید → ON_BUY_ITEM_COMPLETE).
- متدهای جدید Eluna: `Player:SendClassTrainerList()`, `Player:BuyInventoryMenuItem(item, count)`, `Player:GetItemVisualOverrideEntry(slot)`, `GetGlobalPvPInfo()`.
- باگ قدیمی round 7 هم رفع شد: `InBattleGround` → `InBattleground` (حروف صحیح متد Eluna).
- تست‌ها: `git apply` روی state فعلی بدون خطا، syntax-check کامل همه فایل‌های C++ ویرایش‌شده (g++ -fsyntax-only) و هر سه اسکریپت Lua.

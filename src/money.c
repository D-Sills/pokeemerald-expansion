#include "global.h"
#include "money.h"
#include "graphics.h"
#include "event_data.h"
#include "string_util.h"
#include "text.h"
#include "menu.h"
#include "window.h"
#include "sprite.h"
#include "strings.h"
#include "decompress.h"
#include "tv.h"

EWRAM_DATA static u8 sMoneyBoxWindowId = 0;
EWRAM_DATA static u8 sMoneyLabelSpriteId = 0;

#define MONEY_LABEL_TAG 0x2722

static const struct OamData sOamData_MoneyLabel =
{
    .y = 0,
    .affineMode = ST_OAM_AFFINE_OFF,
    .objMode = ST_OAM_OBJ_NORMAL,
    .mosaic = FALSE,
    .bpp = ST_OAM_4BPP,
    .shape = SPRITE_SHAPE(32x16),
    .x = 0,
    .matrixNum = 0,
    .size = SPRITE_SIZE(32x16),
    .tileNum = 0,
    .priority = 0,
    .paletteNum = 0,
    .affineParam = 0,
};

static const union AnimCmd sSpriteAnim_MoneyLabel[] =
{
    ANIMCMD_FRAME(0, 0),
    ANIMCMD_END
};

static const union AnimCmd *const sSpriteAnimTable_MoneyLabel[] =
{
    sSpriteAnim_MoneyLabel,
};

static const struct SpriteTemplate sSpriteTemplate_MoneyLabel =
{
    .tileTag = MONEY_LABEL_TAG,
    .paletteTag = MONEY_LABEL_TAG,
    .oam = &sOamData_MoneyLabel,
    .anims = sSpriteAnimTable_MoneyLabel,
    .images = NULL,
    .affineAnims = gDummySpriteAffineAnimTable,
    .callback = SpriteCallbackDummy
};

static const struct CompressedSpriteSheet sSpriteSheet_MoneyLabel =
{
    .data = gShopMenuMoney_Gfx,
    .size = 256,
    .tag = MONEY_LABEL_TAG,
};

static const struct SpritePalette sSpritePalette_MoneyLabel =
{
    .data = gShopMenu_Pal,
    .tag = MONEY_LABEL_TAG
};

u32 GetMoney(u32 *moneyPtr)
{
    return *moneyPtr ^ gSaveBlock2Ptr->encryptionKey;
}

void SetMoney(u32 *moneyPtr, u32 newValue)
{
    *moneyPtr = gSaveBlock2Ptr->encryptionKey ^ newValue;
}

bool8 IsEnoughMoney(u32 *moneyPtr, u32 cost)
{
    if (GetMoney(moneyPtr) >= cost)
        return TRUE;
    else
        return FALSE;
}

void AddMoney(u32 *moneyPtr, u32 toAdd)
{
    u32 toSet = GetMoney(moneyPtr);

    // can't have more money than MAX
    if (toSet + toAdd > MAX_MONEY)
    {
        toSet = MAX_MONEY;
    }
    else
    {
        toSet += toAdd;
        // check overflow, can't have less money after you receive more
        if (toSet < GetMoney(moneyPtr))
            toSet = MAX_MONEY;
    }

    SetMoney(moneyPtr, toSet);
}

void RemoveMoney(u32 *moneyPtr, u32 toSub)
{
    u32 toSet = GetMoney(moneyPtr);

    // can't subtract more than you already have
    if (toSet < toSub)
        toSet = 0;
    else
        toSet -= toSub;

    SetMoney(moneyPtr, toSet);
}

bool8 IsEnoughForCostInVar0x8005(void)
{
    return IsEnoughMoney(&gSaveBlock1Ptr->money, gSpecialVar_0x8005);
}

void SubtractMoneyFromVar0x8005(void)
{
    RemoveMoney(&gSaveBlock1Ptr->money, gSpecialVar_0x8005);
}

void PrintMoneyAmountInMoneyBox(u8 windowId, int amount, u8 speed)
{
    PrintMoneyAmount(windowId, CalculateMoneyTextHorizontalPosition(amount), 1, amount, speed);
}

void PrintMoneyAmountInMoneyBoxOverride(u8 windowId, int amount, u8 speed){
    PrintMoneyAmountOverride(windowId, 38, 1, amount, speed);
}

u32 CalculateLeadingSpacesForMoney(u32 numDigits)
{
    u32 leadingSpaces = CountDigits(INT_MAX) - StringLength(gStringVar1);
    return (numDigits > 8) ? leadingSpaces : leadingSpaces - 2;
}

void PrintMoneyAmount(u8 windowId, u8 x, u8 y, int amount, u8 speed)
{
    u8 *txtPtr = gStringVar4;
    u32 numDigits = CountDigits(amount);
    u32 maxDigits = (numDigits > 6) ? MAX_MONEY_DIGITS: 6;
    u32 leadingSpaces;

    ConvertIntToDecimalStringN(gStringVar1, amount, STR_CONV_MODE_LEFT_ALIGN, maxDigits);

    leadingSpaces = CalculateLeadingSpacesForMoney(numDigits);

    while (leadingSpaces-- > 0)
        *(txtPtr++) = CHAR_SPACER;

    StringExpandPlaceholders(txtPtr, gText_PokedollarVar1);

    if (numDigits > 8)
        PrependFontIdToFit(gStringVar4, txtPtr + 1 + numDigits, FONT_NORMAL, 54);
    AddTextPrinterParameterized(windowId, FONT_NORMAL, gStringVar4, x, y, speed, NULL);
}

void PrintMoneyAmountOverride(u8 windowId, u8 x, u8 y, int amount, u8 speed){
    u8 *txtPtr;
    s32 strLength;
    ConvertIntToDecimalStringN(gStringVar1, amount, STR_CONV_MODE_LEFT_ALIGN, 6);
    strLength = 6 - StringLength(gStringVar1);
    txtPtr = gStringVar4;

    while (strLength-- > 0)
        *(txtPtr++) = CHAR_SPACER;

    StringExpandPlaceholders(txtPtr, gText_PokedollarVar1);
    const u8 colors[3] =  {TEXT_COLOR_DARK_GRAY, TEXT_COLOR_WHITE,  TEXT_COLOR_LIGHT_GRAY};
    FillWindowPixelBuffer(windowId, PIXEL_FILL(FILL_WINDOW_PIXEL));
    AddTextPrinterParameterized4(windowId, FONT_NORMAL, x, y, 0, 0, colors, speed, gStringVar4);
}

void PrintMoneyAmountInMoneyBoxWithBorder(u8 windowId, u16 tileStart, u8 pallete, int amount)
{
    DrawStdFrameWithCustomTileAndPalette(windowId, FALSE, tileStart, pallete);
    PrintMoneyAmountInMoneyBox(windowId, amount, 0);
}

void PrintMoneyAmountInMoneyBoxWithBorderOverride(u8 windowId, u16 tileStart, u8 pallete, int amount){
    DrawStdFrameWithCustomTileAndPalette(windowId, FALSE, tileStart, pallete);
    PrintMoneyAmountInMoneyBoxOverride(windowId, amount, 0);
}

void ChangeAmountInMoneyBox(int amount)
{
    PrintMoneyAmountInMoneyBox(sMoneyBoxWindowId, amount, 0);
}

u32 CalculateMoneyTextHorizontalPosition(u32 amount)
{
    return (CountDigits(amount) > 8) ? 34 : 26;
}

void DrawMoneyBox(int amount, u8 x, u8 y)
{
    struct WindowTemplate template;

    SetWindowTemplateFields(&template, 0, x + 1, y + 1, 10, 2, 15, 8);
    sMoneyBoxWindowId = AddWindow(&template);
    FillWindowPixelBuffer(sMoneyBoxWindowId, PIXEL_FILL(0));
    PutWindowTilemap(sMoneyBoxWindowId);
    CopyWindowToVram(sMoneyBoxWindowId, COPYWIN_MAP);
    PrintMoneyAmountInMoneyBoxWithBorder(sMoneyBoxWindowId, 0x214, 14, amount);
    AddMoneyLabelObject((8 * x) + 19, (8 * y) + 11);
}

void HideMoneyBox(void)
{
    RemoveMoneyLabelObject();
    ClearStdWindowAndFrameToTransparent(sMoneyBoxWindowId, FALSE);
    CopyWindowToVram(sMoneyBoxWindowId, COPYWIN_GFX);
    RemoveWindow(sMoneyBoxWindowId);
}

void AddMoneyLabelObject(u16 x, u16 y)
{
    LoadCompressedSpriteSheet(&sSpriteSheet_MoneyLabel);
    LoadSpritePalette(&sSpritePalette_MoneyLabel);
    sMoneyLabelSpriteId = CreateSprite(&sSpriteTemplate_MoneyLabel, x, y, 0);
}

void RemoveMoneyLabelObject(void)
{
    DestroySpriteAndFreeResources(&gSprites[sMoneyLabelSpriteId]);
}

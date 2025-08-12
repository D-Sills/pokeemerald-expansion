#include "global.h"
#include "ui_fast_travel.h"
#include "strings.h"
#include "bg.h"
#include "data.h"
#include "decompress.h"
#include "event_data.h"
#include "field_weather.h"
#include "gpu_regs.h"
#include "graphics.h"
#include "item.h"
#include "item_menu.h"
#include "item_menu_icons.h"
#include "list_menu.h"
#include "item_icon.h"
#include "item_use.h"
#include "international_string_util.h"
#include "main.h"
#include "malloc.h"
#include "menu.h"
#include "menu_helpers.h"
#include "palette.h"
#include "party_menu.h"
#include "scanline_effect.h"
#include "script.h"
#include "sound.h"
#include "string_util.h"
#include "strings.h"
#include "task.h"
#include "text_window.h"
#include "overworld.h"
#include "event_data.h"
#include "constants/items.h"
#include "constants/field_weather.h"
#include "constants/songs.h"
#include "constants/rgb.h"

/*
 * 
 */
 
//==========DEFINES==========//
struct MenuResources
{
    MainCallback savedCallback;     // determines callback to run when we exit. e.g. where do we want to go after closing the menu
    u8 gfxLoadState;
    u8 switchArrowsTask;           // task ID for the arrows that switch between the two menus
    u32 unlockedRegions;          // which regions are unlocked for fast travel
};

enum WindowIds
{
    WIN_TOPBAR,
    WIN_NAME,
    WIN_DESCRIPTION
};

enum FastTravelOptions
{
    FAST_TRAVEL_PREHISTORIC, // Pangea
    FAST_TRAVEL_WILD_WEST, // Unova
    FAST_TRAVEL_STEAMPUNK, // Galar
    FAST_TRAVEL_MEDIEVAL, // Kalos
    FAST_TRAVEL_CYBERPUNK, // Hoenn
    FAST_TRAVEL_EDO, // Johto
    FAST_TRAVEL_END_OF_ALL_THINGS, 
    FAST_TRAVEL_COUNT // Total number of fast travel options
};

//==========EWRAM==========//
static EWRAM_DATA struct MenuResources *sMenuDataPtr = NULL;
static EWRAM_DATA u8 *sBg1TilemapBuffer = NULL;
static EWRAM_DATA u8 *sBg2TilemapBuffer = NULL;
static EWRAM_DATA u8 *sBg3TilemapBuffer = NULL; 

// holds the position of the menu so that it persists in memory,
static EWRAM_DATA u8 gSelectedOption = 0;

//==========STATIC=DEFINES==========//
static void Menu_RunSetup(void);
static bool8 Menu_DoGfxSetup(void);
static bool8 Menu_InitBgs(void);
static void Menu_FadeAndBail(void);
static bool8 Menu_LoadGraphics(void);
static void Menu_InitWindows(void);
static void PrintToWindow(u8 windowId, u8 colorIdx, u8 x, u8 y, const u8 *str);
static void Task_MenuWaitFadeIn(u8 taskId);
static void Task_MenuMain(u8 taskId);
static void CreateSwitchArrowPair(void);
static void DestroySwitchArrowPair(void);
static void ClearRegionArt(void);
static void LoadRegionArt(u8 regionId);
static void PrintRegionNameAndDescription(u8 regionId);
static void UpdateMenuSelection(u8 taskId);
static void Task_MosaicSwap(u8 taskId);
static s8 FindNextUnlocked(s32 start, s32 dir, u32 mask, s32 count, bool8 wrap);
static void SetArrowMosaic(bool8 on);
static void SnapToValidSelection(void);

//==========CONST=DATA==========//
static const struct BgTemplate sMenuBgTemplates[] =
{
    {
        .bg = 0,    // windows, etc
        .charBaseIndex = 0,
        .mapBaseIndex = 31,
        .priority = 0
    }, 

    {
        .bg = 1,    // this bg loads the UI tilemap
        .charBaseIndex = 3,
        .mapBaseIndex = 30,
        .priority = 2
    },
    {
        .bg = 2,   // this bg loads the region tilemap
        .charBaseIndex = 2,
        .mapBaseIndex = 28,
        .priority = 3
    },
    {
        .bg = 3,   // desc and name
        .charBaseIndex = 0,
        .mapBaseIndex = 29,
        .priority = 1
    },
};

static const struct WindowTemplate sMenuWindowTemplates[] = 
{
    [WIN_TOPBAR] = 
    {
         .bg = 0,
        .tilemapLeft = 0,
        .tilemapTop = 0,
        .width = 30,
        .height = 2,
        .paletteNum = 15,
        .baseBlock = 64
    },
    [WIN_NAME] = // Window ID for the options menu
    {
        .bg = 3,
        .tilemapLeft = 7,
        .tilemapTop = 3,
        .width = 16,
        .height = 2,
        .paletteNum = 15,
        .baseBlock = 128
    },
    [WIN_DESCRIPTION] = // Window ID for the description text
    {
        .bg = 3,            // which bg to print text on
        .tilemapLeft = 2,   // position from left (per 8 pixels)
        .tilemapTop = 15,   // position from top (per 8 pixels)
        .width = 26,        // width (per 8 pixels)
        .height = 4,        // height (per 8 pixels)
        .paletteNum = 15,   // palette index to use for text
        .baseBlock = 500,   // tile start in VRAM
    },
    
    DUMMY_WIN_TEMPLATE,
};

#define TAG_SCROLL_ARROW 111
static const struct ScrollArrowsTemplate sScrollArrowsTemplate = {
    .firstArrowType = SCROLL_ARROW_LEFT,
    .firstX = 50,
    .firstY = 32,
    .secondArrowType = SCROLL_ARROW_RIGHT,
    .secondX = 190,
    .secondY = 32,
    .fullyUpThreshold = -1,
    .fullyDownThreshold = -1,
    .tileTag = TAG_SCROLL_ARROW,
    .palTag = TAG_SCROLL_ARROW,
    .palNum = 0,
};

static const u32 sMenuTiles[] = INCBIN_U32("graphics/ui_fast_travel/tiles.4bpp.lz");
static const u32 sMenuTilemap[] = INCBIN_U32("graphics/ui_fast_travel/tiles.bin.lz");
static const u16 sMenuPalette[] = INCBIN_U16("graphics/ui_fast_travel/palette.gbapal");

// Region Tiles and Maps and Palettes
static const u32 gRegionTiles_Prehistoric[]       = INCBIN_U32("graphics/ui_fast_travel/bgs/pre/tiles.4bpp.lz");
static const u32 gRegionTiles_WildWest[]          = INCBIN_U32("graphics/ui_fast_travel/bgs/wild/tiles.4bpp.lz");
static const u32 gRegionTiles_Steampunk[]         = INCBIN_U32("graphics/ui_fast_travel/bgs/steam/tiles.4bpp.lz");
static const u32 gRegionTiles_Medieval[]          = INCBIN_U32("graphics/ui_fast_travel/bgs/medi/tiles.4bpp.lz");
static const u32 gRegionTiles_Cyberpunk[]         = INCBIN_U32("graphics/ui_fast_travel/bgs/cyber/tiles.4bpp.lz");
static const u32 gRegionTiles_Edo[]               = INCBIN_U32("graphics/ui_fast_travel/bgs/edo/tiles.4bpp.lz");
static const u32 gRegionTiles_End[]               = INCBIN_U32("graphics/ui_fast_travel/bgs/end/tiles.4bpp.lz");

static const u32 gRegionMap_Prehistoric[]         = INCBIN_U32("graphics/ui_fast_travel/bgs/pre/tiles.bin.lz");
static const u32 gRegionMap_WildWest[]            = INCBIN_U32("graphics/ui_fast_travel/bgs/wild/tiles.bin.lz");
static const u32 gRegionMap_Steampunk[]           = INCBIN_U32("graphics/ui_fast_travel/bgs/steam/tiles.bin.lz");
static const u32 gRegionMap_Medieval[]            = INCBIN_U32("graphics/ui_fast_travel/bgs/medi/tiles.bin.lz");
static const u32 gRegionMap_Cyberpunk[]           = INCBIN_U32("graphics/ui_fast_travel/bgs/cyber/tiles.bin.lz");
static const u32 gRegionMap_Edo[]                 = INCBIN_U32("graphics/ui_fast_travel/bgs/edo/tiles.bin.lz");
static const u32 gRegionMap_End[]                 = INCBIN_U32("graphics/ui_fast_travel/bgs/end/tiles.bin.lz");

static const u16 gRegionPal_Prehistoric[]     = INCBIN_U16("graphics/ui_fast_travel/bgs/pre/palette.gbapal");
static const u16 gRegionPal_WildWest[]        = INCBIN_U16("graphics/ui_fast_travel/bgs/wild/palette.gbapal");
static const u16 gRegionPal_Steampunk[]       = INCBIN_U16("graphics/ui_fast_travel/bgs/steam/palette.gbapal");
static const u16 gRegionPal_Medieval[]        = INCBIN_U16("graphics/ui_fast_travel/bgs/medi/palette.gbapal");
static const u16 gRegionPal_Cyberpunk[]       = INCBIN_U16("graphics/ui_fast_travel/bgs/cyber/palette.gbapal");
static const u16 gRegionPal_Edo[]             = INCBIN_U16("graphics/ui_fast_travel/bgs/edo/palette.gbapal");
static const u16 gRegionPal_End[]             = INCBIN_U16("graphics/ui_fast_travel/bgs/end/palette.gbapal");

enum Colors
{
    FONT_BLACK,
    FONT_WHITE,
    FONT_RED,
    FONT_BLUE,
};
static const u8 sMenuWindowFontColors[][3] = 
{
    [FONT_BLACK]  = {TEXT_COLOR_TRANSPARENT,  TEXT_DYNAMIC_COLOR_3,  TEXT_DYNAMIC_COLOR_2},
    [FONT_WHITE]  = {TEXT_COLOR_TRANSPARENT,  TEXT_COLOR_WHITE,  TEXT_COLOR_LIGHT_GRAY},
    [FONT_RED]   = {TEXT_COLOR_TRANSPARENT,  TEXT_COLOR_RED,        TEXT_COLOR_LIGHT_GRAY},
    [FONT_BLUE]  = {TEXT_COLOR_TRANSPARENT,  TEXT_COLOR_BLUE,       TEXT_COLOR_LIGHT_GRAY},
};

static const u8 sText_FastTravelMenu[] = _("Choose a Region for Fast Travel!");

// Region Names
static const u8 sText_RegionPrehistoric[] = _("Pangea (1000000 BC)");
static const u8 sText_RegionWildWest[] = _("Unova (1905 AD)");
static const u8 sText_RegionSteamPunk[] = _("Galar (1850 AD)");
static const u8 sText_RegionMedieval[] = _("Kalos (1200 AD)");
static const u8 sText_RegionCyberPunk[] = _("Hoenn (2050 AD)");
static const u8 sText_RegionEdo[] = _("Johto (1600 AD)");
static const u8 sText_RegionEndOfAllThings[] = _("??? (3000 AD)");

// Descriptions
static const u8 sText_Empty[]                  = _("");
static const u8 sText_Prehistoric[]            = _("A time before recorded history.");
static const u8 sText_WildWest[]               = _("A time of cowboys and outlaws.");
static const u8 sText_SteamPunk[]              = _("A time of steam-powered machines.");
static const u8 sText_Medieval[]               = _("A time of knights and castles.");
static const u8 sText_CyberPunk[]              = _("A time of advanced technology and dystopia.");
static const u8 sText_Edo[]                    = _("A time of samurai and shoguns.");
static const u8 sText_EndOfAllThings[]         = _("The end of all things, a time of uncertainty.");

static const u8 *const sFastTravelRegionNames[] = 
{
    [FAST_TRAVEL_PREHISTORIC] = sText_RegionPrehistoric,
    [FAST_TRAVEL_WILD_WEST] = sText_RegionWildWest,
    [FAST_TRAVEL_STEAMPUNK] = sText_RegionSteamPunk,
    [FAST_TRAVEL_MEDIEVAL] = sText_RegionMedieval,
    [FAST_TRAVEL_CYBERPUNK] = sText_RegionCyberPunk,
    [FAST_TRAVEL_EDO] = sText_RegionEdo,
    [FAST_TRAVEL_END_OF_ALL_THINGS] = sText_RegionEndOfAllThings,
};

static const u8 *const sFastTravelDescriptions[] = 
{
    [FAST_TRAVEL_PREHISTORIC] = sText_Prehistoric,
    [FAST_TRAVEL_WILD_WEST] = sText_WildWest,
    [FAST_TRAVEL_STEAMPUNK] = sText_SteamPunk,
    [FAST_TRAVEL_MEDIEVAL] = sText_Medieval,
    [FAST_TRAVEL_CYBERPUNK] = sText_CyberPunk,
    [FAST_TRAVEL_EDO] = sText_Edo,
    [FAST_TRAVEL_END_OF_ALL_THINGS] = sText_EndOfAllThings,
};

static const u32 *const gRegionTilesLz[FAST_TRAVEL_COUNT] = {
    [FAST_TRAVEL_PREHISTORIC]        = gRegionTiles_Prehistoric,
    [FAST_TRAVEL_WILD_WEST]          = gRegionTiles_WildWest,
    [FAST_TRAVEL_STEAMPUNK]          = gRegionTiles_Steampunk,
    [FAST_TRAVEL_MEDIEVAL]           = gRegionTiles_Medieval,
    [FAST_TRAVEL_CYBERPUNK]          = gRegionTiles_Cyberpunk,
    [FAST_TRAVEL_EDO]                = gRegionTiles_Edo,
    [FAST_TRAVEL_END_OF_ALL_THINGS]  = gRegionTiles_End,
};

static const u32 *const gRegionMapLz[FAST_TRAVEL_COUNT] = {
    [FAST_TRAVEL_PREHISTORIC]        = gRegionMap_Prehistoric,
    [FAST_TRAVEL_WILD_WEST]          = gRegionMap_WildWest,
    [FAST_TRAVEL_STEAMPUNK]          = gRegionMap_Steampunk,
    [FAST_TRAVEL_MEDIEVAL]           = gRegionMap_Medieval,
    [FAST_TRAVEL_CYBERPUNK]          = gRegionMap_Cyberpunk,
    [FAST_TRAVEL_EDO]                = gRegionMap_Edo,
    [FAST_TRAVEL_END_OF_ALL_THINGS]  = gRegionMap_End,
};

static const u16 *const gRegionPal[FAST_TRAVEL_COUNT] = {
    [FAST_TRAVEL_PREHISTORIC]        = gRegionPal_Prehistoric,
    [FAST_TRAVEL_WILD_WEST]          = gRegionPal_WildWest,
    [FAST_TRAVEL_STEAMPUNK]          = gRegionPal_Steampunk,
    [FAST_TRAVEL_MEDIEVAL]           = gRegionPal_Medieval,
    [FAST_TRAVEL_CYBERPUNK]          = gRegionPal_Cyberpunk,
    [FAST_TRAVEL_EDO]                = gRegionPal_Edo,
    [FAST_TRAVEL_END_OF_ALL_THINGS]  = gRegionPal_End,
};

//==========FUNCTIONS==========//
// UI loader template
void Task_OpenFastTravelMenu(u8 taskId)
{
    s16 *data = gTasks[taskId].data;
    if (!gPaletteFade.active)
    {
        CleanupOverworldWindowsAndTilemaps();
        FastTravelMenu_Init(CB2_ReturnToFieldFadeFromBlack);
        DestroyTask(taskId);
    }
}

// This is our main initialization function if you want to call the menu from elsewhere
void FastTravelMenu_Init(MainCallback callback)
{
    if ((sMenuDataPtr = AllocZeroed(sizeof(struct MenuResources))) == NULL)
    {
        SetMainCallback2(callback);
        return;
    }
    
    // initialize stuff
    sMenuDataPtr->gfxLoadState = 0;
    sMenuDataPtr->savedCallback = callback;
    sMenuDataPtr->switchArrowsTask = TASK_NONE;
    sMenuDataPtr->unlockedRegions = VarGet(VAR_REGIONS_UNLOCKED); // Get the unlocked regions from a variable

    SnapToValidSelection(); // Ensure the selected option is valid
    
    SetMainCallback2(Menu_RunSetup);
}

static void Menu_RunSetup(void)
{
    while (1)
    {
        if (Menu_DoGfxSetup() == TRUE)
            break;
    }
}

static void Menu_MainCB(void)
{
    RunTasks();
    AnimateSprites();
    BuildOamBuffer();
    DoScheduledBgTilemapCopiesToVram();
    UpdatePaletteFade();
}

static void Menu_VBlankCB(void)
{
    LoadOam();
    ProcessSpriteCopyRequests();
    TransferPlttBuffer();
}

static bool8 Menu_DoGfxSetup(void)
{
    u8 taskId;
    switch (gMain.state)
    {
    case 0:
        DmaClearLarge16(3, (void *)VRAM, VRAM_SIZE, 0x1000)
        SetVBlankHBlankCallbacksToNull();
        ClearScheduledBgCopiesToVram();
        ResetVramOamAndBgCntRegs();
        gMain.state++;
        break;
    case 1:
        ScanlineEffect_Stop();
        FreeAllSpritePalettes();
        ResetPaletteFade();
        ResetSpriteData();
        ResetTasks();
        gMain.state++;
        break;
    case 2:
        if (Menu_InitBgs())
        {
            sMenuDataPtr->gfxLoadState = 0;
            gMain.state++;
        }
        else
        {
            Menu_FadeAndBail();
            return TRUE;
        }
        break;
    case 3:
        if (Menu_LoadGraphics() == TRUE)
            gMain.state++;
        break;
    case 4:
        LoadMessageBoxAndBorderGfx();
        Menu_InitWindows();
        gMain.state++;
        break;
    case 5:
        LoadRegionArt(gSelectedOption);
        gMain.state++;
        break;
    case 6:
        PrintToWindow(WIN_TOPBAR, FONT_WHITE, 5,1, sText_FastTravelMenu);
        PrintRegionNameAndDescription(gSelectedOption);
        taskId = CreateTask(Task_MenuWaitFadeIn, 0);
        BlendPalettes(0xFFFFFFFF, 16, RGB_BLACK);
        gMain.state++;
        break;
    case 7:
        BeginNormalPaletteFade(0xFFFFFFFF, 0, 16, 0, RGB_BLACK);
        gMain.state++;
        break;
    default:
        SetVBlankCallback(Menu_VBlankCB);
        SetMainCallback2(Menu_MainCB);
        return TRUE;
    }
    return FALSE;
}

#define try_free(ptr) ({        \
    void ** ptr__ = (void **)&(ptr);   \
    if (*ptr__ != NULL)                \
        Free(*ptr__);                  \
})

static void Menu_FreeResources(void)
{
    try_free(sMenuDataPtr);
    try_free(sBg1TilemapBuffer);
    try_free(sBg2TilemapBuffer);
    try_free(sBg3TilemapBuffer);
    DestroySwitchArrowPair();
    FreeAllWindowBuffers();
}


static void Task_MenuWaitFadeAndBail(u8 taskId)
{
    if (!gPaletteFade.active)
    {
        SetMainCallback2(sMenuDataPtr->savedCallback);
        Menu_FreeResources();
        DestroyTask(taskId);
    }
}

static void Menu_FadeAndBail(void)
{
    BeginNormalPaletteFade(0xFFFFFFFF, 0, 0, 16, RGB_BLACK);
    CreateTask(Task_MenuWaitFadeAndBail, 0);
    SetVBlankCallback(Menu_VBlankCB);
    SetMainCallback2(Menu_MainCB);
}

static bool8 Menu_InitBgs(void)
{
    ResetAllBgsCoordinatesAndBgCntRegs();

    sBg1TilemapBuffer = Alloc(0x800);
    if (!sBg1TilemapBuffer) return FALSE;
    memset(sBg1TilemapBuffer, 0, 0x800);

    sBg2TilemapBuffer = Alloc(0x800);
    if (!sBg2TilemapBuffer) return FALSE;
    memset(sBg2TilemapBuffer, 0, 0x800);

    sBg3TilemapBuffer = Alloc(0x800);                 // NEW
    if (!sBg3TilemapBuffer) return FALSE;             // NEW
    memset(sBg3TilemapBuffer, 0, 0x800);              // NEW

    ResetBgsAndClearDma3BusyFlags(0);
    InitBgsFromTemplates(0, sMenuBgTemplates, NELEMS(sMenuBgTemplates));

    SetBgTilemapBuffer(1, sBg1TilemapBuffer);
    SetBgTilemapBuffer(2, sBg2TilemapBuffer);
    SetBgTilemapBuffer(3, sBg3TilemapBuffer);         // NEW

    ScheduleBgCopyTilemapToVram(1);
    ScheduleBgCopyTilemapToVram(2);
    ScheduleBgCopyTilemapToVram(3);                   // NEW

    SetGpuReg(REG_OFFSET_DISPCNT, DISPCNT_OBJ_1D_MAP | DISPCNT_OBJ_ON);
    SetGpuReg(REG_OFFSET_BLDCNT, 0);

    ShowBg(0);
    ShowBg(1);
    ShowBg(2);
    ShowBg(3);                                        // NEW
    return TRUE;
}

static bool8 Menu_LoadGraphics(void)
{
    switch (sMenuDataPtr->gfxLoadState)
    {
    case 0:
        ResetTempTileDataBuffers();
        DecompressAndCopyTileDataToVram(1, sMenuTiles, 0, 0, 0);
        sMenuDataPtr->gfxLoadState++;
        break;
    case 1:
        if (FreeTempTileDataBuffersIfPossible() != TRUE)
        {
            LZDecompressWram(sMenuTilemap, sBg1TilemapBuffer);
            sMenuDataPtr->gfxLoadState++;
        }
        break;
    case 2:
        LoadPalette(sMenuPalette, BG_PLTT_ID(2), PLTT_SIZE_4BPP);
        sMenuDataPtr->gfxLoadState++;
        break;
    default:
        sMenuDataPtr->gfxLoadState = 0;
        return TRUE;
    }
    return FALSE;
}

static void Menu_InitWindows(void)
{
    InitWindows(sMenuWindowTemplates);
    DeactivateAllTextPrinters();
}

static void PrintToWindow(u8 windowId, u8 colorIdx, u8 x, u8 y, const u8 *str)
{
    u8 fillValue = sMenuWindowFontColors[colorIdx][0];
    FillWindowPixelBuffer(windowId, PIXEL_FILL(fillValue));
    AddTextPrinterParameterized4(windowId, 1, x, y, 0, 0, sMenuWindowFontColors[colorIdx], 0xFF, str);
    PutWindowTilemap(windowId);
    CopyWindowToVram(windowId, COPYWIN_FULL);
}

static void Task_MenuWaitFadeIn(u8 taskId)
{
    CreateSwitchArrowPair();

    if (!gPaletteFade.active)
        gTasks[taskId].func = Task_MenuMain;
}

static void Task_MenuTurnOff(u8 taskId)
{
    s16 *data = gTasks[taskId].data;

    if (!gPaletteFade.active)
    {
        SetMainCallback2(sMenuDataPtr->savedCallback);
        Menu_FreeResources();
        DestroyTask(taskId);
    }
}

void Task_FastTravelPrehistoric(u8 taskId)
{
    if (!gPaletteFade.active)
    {
        Menu_FreeResources();
        PlayRainStoppingSoundEffect();
        CleanupOverworldWindowsAndTilemaps();
        SetMainCallback2(CB2_ReturnToFieldFadeFromBlack);
    }
}

static void CreateSwitchArrowPair(void)
{
    if (sMenuDataPtr->switchArrowsTask == TASK_NONE && sMenuDataPtr->unlockedRegions > 1)
        sMenuDataPtr->switchArrowsTask = AddScrollIndicatorArrowPair(&sScrollArrowsTemplate, 0);
}

static void DestroySwitchArrowPair(void)
{
    if (sMenuDataPtr->switchArrowsTask != TASK_NONE)
    {
        RemoveScrollIndicatorArrowPair(sMenuDataPtr->switchArrowsTask);
        sMenuDataPtr->switchArrowsTask = TASK_NONE;
    }
}

static void ClearRegionArt(void)
{
    //FillBgTilemapBufferRect(2, 0, 0, 0, 32, 20, 0);
}

static void LoadRegionArt(u8 regionId) // make part of a task
{
    // Load the tiles, tilemap, and palette for the specified region
    DecompressAndCopyTileDataToVram(2, gRegionTilesLz[regionId], 0, 0, 0);
    
    LZDecompressWram(gRegionMapLz[regionId], sBg2TilemapBuffer);
    
    LoadPalette(gRegionPal[regionId], BG_PLTT_ID(0), PLTT_SIZE_4BPP);

    ScheduleBgCopyTilemapToVram(2);
}

static void PrintRegionNameAndDescription(u8 regionId)
{
    // Print the region name and description in the top bar and description window
    int offset;

    offset = GetStringCenterAlignXOffset(FONT_NORMAL, sFastTravelRegionNames[regionId], 16*8);
    PrintToWindow(WIN_NAME, FONT_WHITE, offset, 1, sFastTravelRegionNames[regionId]);
    PrintToWindow(WIN_DESCRIPTION, FONT_WHITE, 0, 0, sFastTravelDescriptions[regionId]);
}

static s8 FindNextUnlocked(s32 start, s32 dir, u32 mask, s32 count, bool8 wrap)
{
    for (s32 step = 1; step <= count; step++)
    {
        s32 i = start + dir * step;

        if (wrap)
        {
            if (i < 0)      i += count;
            if (i >= count) i -= count;
        }
        else
        {
            if (i < 0 || i >= count)
                return -1; // hit edge with no wrap
        }

        if (mask & (1u << i))
            return (s8)i;  // found an unlocked region
    }
    return -1; // none found
}

static void SnapToValidSelection(void)
{
    u32 mask = sMenuDataPtr->unlockedRegions;

    if (mask == 0)
        return; // nothing unlocked; decide how you want to handle this

    if ((mask & (1u << gSelectedOption)) == 0)
    {
        s8 i = FindNextUnlocked(gSelectedOption, +1, mask, FAST_TRAVEL_COUNT, TRUE);
        if (i < 0)
            i = FindNextUnlocked(gSelectedOption, -1, mask, FAST_TRAVEL_COUNT, TRUE);
        if (i >= 0)
            gSelectedOption = (u8)i;
    }
}

/* This is the meat of the UI. This is where you wait for player inputs and can branch to other tasks accordingly */
static void Task_MenuMain(u8 taskId)
{
    if (JOY_NEW(B_BUTTON))
    {
        PlaySE(SE_PC_OFF);
        BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
        gTasks[taskId].func = Task_MenuTurnOff;
    }
    if (JOY_NEW(DPAD_LEFT) || JOY_NEW(DPAD_RIGHT))
    {
        u32 mask = sMenuDataPtr->unlockedRegions;
        s8 next = -1;

        if (JOY_NEW(DPAD_LEFT))
            next = FindNextUnlocked(gSelectedOption, -1, mask, FAST_TRAVEL_COUNT, TRUE);
        else if (JOY_NEW(DPAD_RIGHT))
            next = FindNextUnlocked(gSelectedOption, +1, mask, FAST_TRAVEL_COUNT, TRUE);

        if (next >= 0 && (u8)next != gSelectedOption)
        {
            PlaySE(SE_RG_BAG_CURSOR);
            gSelectedOption = (u8)next;
            UpdateMenuSelection(taskId);
            // optionally UpdateArrowVisibility() here (see note below)
        }
    }
    if (JOY_NEW(A_BUTTON)) //when A is pressed, load the Task for the Menu the cursor is on, for some they require a flag to be set
    {
        switch(gSelectedOption)
        {
            case FAST_TRAVEL_PREHISTORIC:
                PlaySE(SE_SELECT);
                BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
                gTasks[taskId].func = Task_FastTravelPrehistoric;
                break;
        }
    }
}

static void SetArrowMosaic(bool8 on)
{
    if (sMenuDataPtr->switchArrowsTask == TASK_NONE)
        return;

    u8 t = sMenuDataPtr->switchArrowsTask;
    s16 *d = gTasks[t].data;

    if (d[0] >= 0 && d[0] < MAX_SPRITES) gSprites[d[0]].oam.mosaic = on;
    if (d[1] >= 0 && d[1] < MAX_SPRITES) gSprites[d[1]].oam.mosaic = on;
}

#define tScrollState     data[0]
#define tMosaicStrength  data[1]
#define tNextRegion      data[2]

static void UpdateMenuSelection(u8 taskId) {
    // Clear the previous region art
    ClearRegionArt();
    
    s16 *data = gTasks[taskId].data;
    tScrollState    = 0;
    tMosaicStrength = 0;
    tNextRegion     = gSelectedOption;
    
    SetTaskFuncWithFollowupFunc(taskId, Task_MosaicSwap, gTasks[taskId].func);
}

static void Task_MosaicSwap(u8 taskId)
{
    s16 *data = gTasks[taskId].data;

    if (tScrollState == 0)
    {
        tMosaicStrength = 0;
        SetGpuReg(REG_OFFSET_MOSAIC, 0);
        SetGpuRegBits(REG_OFFSET_BG1CNT, BGCNT_MOSAIC);
        SetGpuRegBits(REG_OFFSET_BG2CNT, BGCNT_MOSAIC);
        SetGpuRegBits(REG_OFFSET_BG3CNT, BGCNT_MOSAIC);
        SetArrowMosaic(TRUE);
    }
    
    // build up mosaic effect (0 -> 7)
    if (tScrollState <= 3)
    {
        tMosaicStrength += 1; // (uncommented vs the reference)
        SetGpuReg(REG_OFFSET_MOSAIC, (tMosaicStrength & 15) * 17);
    }
    else
    {
        if (tScrollState == 4)
        {
            LoadRegionArt(tNextRegion);
            PrintRegionNameAndDescription(tNextRegion);
        }
        if (tMosaicStrength > 0)
            tMosaicStrength -= 1;
        SetGpuReg(REG_OFFSET_MOSAIC, (tMosaicStrength & 15) * 17);
        
    }
    tScrollState++;

    if (tScrollState >= 8)
    {
         // Tear down and finish
        ClearGpuRegBits(REG_OFFSET_BG2CNT, BGCNT_MOSAIC);
        ClearGpuRegBits(REG_OFFSET_BG1CNT, BGCNT_MOSAIC);
        ClearGpuRegBits(REG_OFFSET_BG3CNT, BGCNT_MOSAIC);
        SetArrowMosaic(FALSE);
        SetGpuReg(REG_OFFSET_MOSAIC, 0);

        ScheduleBgCopyTilemapToVram(2);
        ScheduleBgCopyTilemapToVram(1);
        ScheduleBgCopyTilemapToVram(3);

        SwitchTaskToFollowupFunc(taskId);
    }
}

#undef tScrollState
#undef tMosaicStrength
#undef tNextRegion
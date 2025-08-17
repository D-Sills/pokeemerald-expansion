#include "global.h"
#include "ui_character_creation.h"
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
#include "outfit_menu.h"
#include "constants/songs.h"
#include "constants/trainers.h"
#include "trainer_pokemon_sprites.h"
#include "field_player_avatar.h"
#include "constants/rgb.h"
#include "event_object_movement.h"
#include "field_screen_effect.h"
#include "m4a.h"
#include "naming_screen.h"
#include "random.h"

/*
 * 
 */
 
//==========DEFINES==========//
enum WindowIds
{
    WIN_TOPBAR,
    WIN_OPTIONS,
    WIN_YESNO,
};

// Menu items
enum MenuItems
{
    MENUITEM_GENDER,
    MENUITEM_NAME,
    MENUITEM_PALLETTE,
    MENUITEM_PRONOUNS,
    MENUITEM_FINISH,
    MENUITEM_COUNT,
};

enum {
    SPR_CC_OW_S,
    SPR_CC_OW_N,
    SPR_CC_OW_W,
    SPR_CC_OW_E,
    SPR_CC_FRONT,
    SPR_CC_BACK,
    SPR_CC_COUNT
};

struct MenuResources
{
    u8 gfxLoadState;
    u16 curItem; // currently selected option
    u8 selection[MENUITEM_COUNT];
    u8  sprIds[SPR_CC_COUNT];   // NEW: preview sprite ids
    u8 slotId:1;
};


//==========EWRAM==========//
static EWRAM_DATA struct MenuResources *sMenuDataPtr = NULL;
static EWRAM_DATA u8 *sBg1TilemapBuffer = NULL;
static EWRAM_DATA u8 *sBg2TilemapBuffer = NULL;

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
static void CB2_ReturnFromNamingScreen(void);
static void SetDefaultPlayerName(u8 nameId);
static void DrawTopBarText(void);
static void DrawOptionsMenuTexts(void);
static void DrawLeftSideOptionText(int selection, int y);
static void DrawRightSideChoiceText(const u8 *str, int x, int y, bool8 chosen, bool8 active);
static void DrawOptionsMenuChoice(const u8 *text, u8 x, u8 y, u8 style, bool8 active);
static void DrawChoices(u32 id, int y);
static void PrintAllToWindow(void);
static void HighlightOptionsMenuItem(void);
static void ApplyOnChange(u8 item, u8 value);
static int XOptions_ProcessInput(int x, int selection);
static int ProcessInput_Options_One(int selection);
static int ProcessInput_Options_Two(int selection);
static int ProcessInput_Options_Three(int selection);
static int ProcessInput_Options_Four(int selection);
static void DrawChoices_Gender(int selection, int y);
static void DrawChoices_Name(int selection, int y);
static void DrawChoices_Palette(int selection, int y);
static void DrawChoices_Pronouns(int selection, int y);
static void CC_DestroyPreviewSprites(void);
static u8 CC_CreateOverworldPreview(u8 dir, s16 x, s16 y);
static void CC_CreateOverworldRow(void);
static void CC_CreateTrainerPics(void);
static void CC_CreatePreviewSprites(void);
static void CC_RefreshPreviewSprites(void);
static void ShowContextMenu(u8 taskId);
static void ConfirmFinish(u8 taskId);
static void CancelFinish(u8 taskId);

//==========CONST=DATA==========//
static const struct BgTemplate sMenuBgTemplates[] =
{
    {
        .bg = 0,    // text
        .charBaseIndex = 0,
        .mapBaseIndex = 31,
        .priority = 0
    }, 

    {
        .bg = 1,    // ui tilemap
        .charBaseIndex = 3,
        .mapBaseIndex = 30,
        .priority = 2
    },
    {
        .bg = 2,   // scroll bg
        .charBaseIndex = 2,
        .mapBaseIndex = 28,
        .priority = 3
    },
    {
        .bg = 3,   // sprites
        .charBaseIndex = 1,
        .mapBaseIndex = 29,
        .priority = 0
    },
};

static const struct WindowTemplate sMenuWindowTemplates[] = 
{
    [WIN_TOPBAR] = 
    {
         .bg = 3,
        .tilemapLeft = 0,
        .tilemapTop = 0,
        .width = 30,
        .height = 2,
        .paletteNum = 15,
        .baseBlock = 2
    },
    [WIN_OPTIONS] = // Window ID for the options menu
    {
        .bg = 0,
        .tilemapLeft = 1,
        .tilemapTop = 3,
        .width = 19,
        .height = 10,
        .paletteNum = 15,
        .baseBlock = 500
    },
    
    DUMMY_WIN_TEMPLATE,
};

static const struct WindowTemplate sContextMenuWindowTemplates[] =
{
        [WIN_YESNO] = { // Yes/No higher up, positioned above a lower message box
        .bg = 3,
        .tilemapLeft = 21,
        .tilemapTop = 9,
        .width = 5,
        .height = 4,
        .paletteNum = 15,
        .baseBlock = 0x21D,
    },
};

static const struct YesNoFuncTable sYesNoFunctions = {ConfirmFinish, CancelFinish};

static const u8 *const sMalePresetNames[] = {
    COMPOUND_STRING("STU"),
    COMPOUND_STRING("MILTON"),
    COMPOUND_STRING("TOM"),
    COMPOUND_STRING("KENNY"),
    COMPOUND_STRING("REID"),
    COMPOUND_STRING("JUDE"),
    COMPOUND_STRING("JAXSON"),
    COMPOUND_STRING("EASTON"),
    COMPOUND_STRING("WALKER"),
    COMPOUND_STRING("TERU"),
    COMPOUND_STRING("JOHNNY"),
    COMPOUND_STRING("BRETT"),
    COMPOUND_STRING("SETH"),
    COMPOUND_STRING("TERRY"),
    COMPOUND_STRING("CASEY"),
    COMPOUND_STRING("DARREN"),
    COMPOUND_STRING("LANDON"),
    COMPOUND_STRING("COLLIN"),
    COMPOUND_STRING("STANLEY"),
    COMPOUND_STRING("QUINCY")
};

static const u8 *const sFemalePresetNames[] = {
    COMPOUND_STRING("KIMMY"),
    COMPOUND_STRING("TIARA"),
    COMPOUND_STRING("BELLA"),
    COMPOUND_STRING("JAYLA"),
    COMPOUND_STRING("ALLIE"),
    COMPOUND_STRING("LIANNA"),
    COMPOUND_STRING("SARA"),
    COMPOUND_STRING("MONICA"),
    COMPOUND_STRING("CAMILA"),
    COMPOUND_STRING("AUBREE"),
    COMPOUND_STRING("RUTHIE"),
    COMPOUND_STRING("HAZEL"),
    COMPOUND_STRING("NADINE"),
    COMPOUND_STRING("TANJA"),
    COMPOUND_STRING("YASMIN"),
    COMPOUND_STRING("NICOLA"),
    COMPOUND_STRING("LILLIE"),
    COMPOUND_STRING("TERRA"),
    COMPOUND_STRING("LUCY"),
    COMPOUND_STRING("HALIE")
};

// The number of male vs. female names is assumed to be the same.
// If they aren't, the smaller of the two sizes will be used and any extra names will be ignored.
#define NUM_PRESET_NAMES min(ARRAY_COUNT(sMalePresetNames), ARRAY_COUNT(sFemalePresetNames))

#define Y_DIFF 16 // Difference in pixels between items.

// Menu left side option names text
static const u8 sText_Gender[] = _("BODY TYPE");
static const u8 sText_Name[] = _("NAME");
static const u8 sText_Palette[] = _("PALETTE");
static const u8 sText_Pronouns[] = _("PRONOUNS");
static const u8 sText_Finish[] = _("FINISH");

static const u8 *const sMenuItemsNames[MENUITEM_COUNT] =
{
    [MENUITEM_GENDER]   = sText_Gender,
    [MENUITEM_NAME]     = sText_Name,
    [MENUITEM_PALLETTE] = sText_Palette,
    [MENUITEM_PRONOUNS] = sText_Pronouns,
    [MENUITEM_FINISH]   = sText_Finish,
};

// Menu draw and input functions
struct Menu_Custom// MENU_CUSTOM
{
    void (*drawChoices)(int selection, int y);
    int (*processInput)(int selection);
} static const sItemFunctionsCustom[MENUITEM_COUNT] =
{
    [MENUITEM_GENDER]    = {DrawChoices_Gender,  ProcessInput_Options_Two},
    [MENUITEM_NAME]     = {DrawChoices_Name,   NULL},
    [MENUITEM_PALLETTE]     = {DrawChoices_Palette,   ProcessInput_Options_Four},
    [MENUITEM_PRONOUNS]        = {DrawChoices_Pronouns,      ProcessInput_Options_Three},
    [MENUITEM_FINISH]         = {NULL, NULL},
};

// Menu left side text conditions
static bool8 CheckConditions(int selection)
{
    switch(selection)
    {
        case MENUITEM_GENDER:     return TRUE;
        case MENUITEM_NAME:      return TRUE;
        case MENUITEM_PALLETTE:    return TRUE;
        case MENUITEM_PRONOUNS:    return TRUE;
        case MENUITEM_FINISH:      return TRUE;
        case MENUITEM_COUNT:      return TRUE;
        default:  return FALSE;
    }
}

static const u32 sMenuTiles[] = INCBIN_U32("graphics/ui_character_creation/tiles.4bpp.lz");
static const u32 sMenuTilemap[] = INCBIN_U32("graphics/ui_character_creation/tiles.bin.lz");
static const u16 sMenuPalette[] = INCBIN_U16("graphics/ui_character_creation/palette.gbapal");

// Scrolling Background
static const u32 sScrollBgTiles[] = INCBIN_U32("graphics/ui_character_creation/scroll_tiles.4bpp.lz");
static const u32 sScrollBgTilemap[] = INCBIN_U32("graphics/ui_character_creation/scroll_tilemap.bin.lz");
static const u16 sScrollBgPalette[] = INCBIN_U16("graphics/ui_character_creation/scroll_tiles.gbapal");

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

//==========FUNCTIONS==========//
// UI loader template
void Task_OpenCharacterCreation(u8 taskId)
{
    s16 *data = gTasks[taskId].data;
    if (!gPaletteFade.active)
    {
        CleanupOverworldWindowsAndTilemaps();
        CharacterCreation_Init(CB2_ReturnToFieldFadeFromBlack); // change this callback depending on if opened in intro or in game
        DestroyTask(taskId);
    }
}

// This is our main initialization function if you want to call the menu from elsewhere
void CharacterCreation_Init(MainCallback callback)
{
    if ((sMenuDataPtr = AllocZeroed(sizeof(struct MenuResources))) == NULL)
    {
        SetMainCallback2(callback);
        return;
    }
    
    // initialize stuff
    sMenuDataPtr->curItem = 0;
    for (int i = 0; i < SPR_CC_COUNT; i++) sMenuDataPtr->sprIds[i] = SPRITE_NONE;

    // seed initial selections
    sMenuDataPtr->selection[MENUITEM_GENDER] = (gSaveBlock2Ptr->playerGender == FEMALE) ? 1 : 0;
    sMenuDataPtr->selection[MENUITEM_PALLETTE] = 0;  // 0..3 for palette
    sMenuDataPtr->selection[MENUITEM_PRONOUNS] = VarGet(VAR_PRONOUNS);
    sMenuDataPtr->selection[MENUITEM_NAME]     = 0;  // unused, but harmless

    memset(sMenuDataPtr->sprIds, SPRITE_NONE, sizeof(sMenuDataPtr->sprIds));

    if (gSaveBlock2Ptr->playerName[0] == EOS)
        SetDefaultPlayerName(Random() % NUM_PRESET_NAMES);

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

    ChangeBgX(2, 64, BG_COORD_ADD);
    ChangeBgY(2, 64, BG_COORD_ADD);
}

static bool8 Menu_DoGfxSetup(void)
{
    u8 taskId;
    switch (gMain.state)
    {
    case 0:
        DmaClearLarge16(3, (void *)VRAM, VRAM_SIZE, 0x1000)
        DmaClear32(3, OAM, OAM_SIZE);
        DmaClear16(3, PLTT, PLTT_SIZE);
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
        PrintAllToWindow();
        CC_CreatePreviewSprites();
        taskId = CreateTask(Task_MenuWaitFadeIn, 0);
        BlendPalettes(0xFFFFFFFF, 16, RGB_BLACK);
        gMain.state++;
        break;
    case 6:
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
    FreeAllWindowBuffers();
    try_free(sMenuDataPtr);
    try_free(sBg1TilemapBuffer);
    try_free(sBg2TilemapBuffer);
    CC_DestroyPreviewSprites();
    //FREE_AND_SET_NULL(sMenuDataPtr);
    SetGpuReg(REG_OFFSET_WIN0H, 0);
    SetGpuReg(REG_OFFSET_WIN0V, 0);
    SetGpuReg(REG_OFFSET_WININ, 0);
    SetGpuReg(REG_OFFSET_WINOUT, 0);
    SetGpuReg(REG_OFFSET_BLDCNT, 0);
    SetGpuReg(REG_OFFSET_BLDALPHA, 0);
    SetGpuReg(REG_OFFSET_BLDY, 4);
    SetGpuReg(REG_OFFSET_DISPCNT, 0);
    HideBg(2);
    HideBg(3);
}


static void Task_MenuWaitFadeAndBail(u8 taskId)
{
    if (!gPaletteFade.active)
    {
        DestroyTask(taskId);
        Menu_FreeResources();   
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
    SetGpuReg(REG_OFFSET_WIN0H, 0);
    SetGpuReg(REG_OFFSET_WIN0V, 0);
    SetGpuReg(REG_OFFSET_WININ, WININ_WIN0_BG_ALL | WININ_WIN0_OBJ);
    SetGpuReg(REG_OFFSET_WINOUT, WINOUT_WIN01_BG_ALL | WINOUT_WIN01_OBJ | WINOUT_WIN01_CLR);
    SetGpuReg(REG_OFFSET_BLDCNT, BLDCNT_EFFECT_DARKEN | BLDCNT_TGT1_BG0 | BLDCNT_TGT1_BG1);
    SetGpuReg(REG_OFFSET_BLDALPHA, 0);
    SetGpuReg(REG_OFFSET_BLDY, 4);
    SetGpuReg(REG_OFFSET_DISPCNT, DISPCNT_WIN0_ON | DISPCNT_WIN1_ON);

    sBg1TilemapBuffer = Alloc(0x800);
    if (!sBg1TilemapBuffer) return FALSE;
    memset(sBg1TilemapBuffer, 0, 0x800);

    sBg2TilemapBuffer = Alloc(0x800);
    if (!sBg2TilemapBuffer) return FALSE;
    memset(sBg2TilemapBuffer, 0, 0x800);

    ResetAllBgsCoordinates();
    ResetBgsAndClearDma3BusyFlags(0);
    InitBgsFromTemplates(0, sMenuBgTemplates, NELEMS(sMenuBgTemplates));

    SetBgTilemapBuffer(1, sBg1TilemapBuffer);
    SetBgTilemapBuffer(2, sBg2TilemapBuffer);

    ScheduleBgCopyTilemapToVram(1);
    ScheduleBgCopyTilemapToVram(2);
    return TRUE;
}

static bool8 Menu_LoadGraphics(void)
{
    LoadPalette(GetOverworldTextboxPalettePtr(), BG_PLTT_ID(15), PLTT_SIZE_4BPP);
    switch (sMenuDataPtr->gfxLoadState)
    {
    case 0:
        ResetTempTileDataBuffers();
        DecompressAndCopyTileDataToVram(1, sMenuTiles, 0, 0, 0);
        DecompressAndCopyTileDataToVram(2, sScrollBgTiles, 0, 0, 0);
        sMenuDataPtr->gfxLoadState++;
        break;
    case 1:
        if (FreeTempTileDataBuffersIfPossible() != TRUE)
        {
            LZDecompressWram(sMenuTilemap, sBg1TilemapBuffer);
            LZDecompressWram(sScrollBgTilemap, sBg2TilemapBuffer);
            sMenuDataPtr->gfxLoadState++;
        }
        break;
    case 2:
        LoadPalette(sMenuPalette, BG_PLTT_ID(2), PLTT_SIZE_4BPP);
        LoadPalette(sScrollBgPalette, BG_PLTT_ID(3), PLTT_SIZE_4BPP);
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
    //PlaySE(SE_PC_ON);
    DeactivateAllTextPrinters();
}

static void PrintToWindow(u8 windowId, u8 colorIdx, u8 x, u8 y, const u8 *str)
{
    u8 fillValue = sMenuWindowFontColors[colorIdx][0];
    //FillWindowPixelBuffer(windowId, PIXEL_FILL(fillValue));
    AddTextPrinterParameterized4(windowId, 1, x, y, 0, 0, sMenuWindowFontColors[colorIdx], 0xFF, str);
    PutWindowTilemap(windowId);
    CopyWindowToVram(windowId, COPYWIN_FULL);
}

static void Task_MenuWaitFadeIn(u8 taskId)
{
    if (!gPaletteFade.active) {
        gTasks[taskId].func = Task_MenuMain;
        SetGpuReg(REG_OFFSET_WIN0H, 0); // Idk man Im just trying to stop this stupid graphical bug from happening dont judge me
        SetGpuReg(REG_OFFSET_WIN0V, 0);
        SetGpuReg(REG_OFFSET_WININ, WININ_WIN0_BG_ALL | WININ_WIN0_OBJ);
        SetGpuReg(REG_OFFSET_WINOUT, WINOUT_WIN01_BG_ALL | WINOUT_WIN01_OBJ | WINOUT_WIN01_CLR);
        SetGpuReg(REG_OFFSET_BLDCNT, BLDCNT_EFFECT_DARKEN | BLDCNT_TGT1_BG0 | BLDCNT_TGT1_BG1);
        SetGpuReg(REG_OFFSET_BLDALPHA, 0);
        SetGpuReg(REG_OFFSET_BLDY, 4);
        SetGpuReg(REG_OFFSET_DISPCNT, DISPCNT_WIN0_ON | DISPCNT_WIN1_ON | DISPCNT_OBJ_ON | DISPCNT_OBJ_1D_MAP);
        ShowBg(0);
        ShowBg(1);
        ShowBg(2);
        ShowBg(3);
        HighlightOptionsMenuItem();
        return;
    }
}

static void Task_MenuTurnOff(u8 taskId)
{
    s16 *data = gTasks[taskId].data;

    if (!gPaletteFade.active)
    {
        DestroyTask(taskId);
        Menu_FreeResources();
        //PlaySE(SE_PC_OFF);
        //m4aMPlayVolumeControl(&gMPlayInfo_BGM, TRACKS_ALL, 0x100);
    }
}

static const u8 sText_CharacterCreationTopBar[] = _("Character Creation");

static void DrawTopBarText(void)
{
    PrintToWindow(WIN_TOPBAR, FONT_WHITE, 5, 1, sText_CharacterCreationTopBar);
}

static void DrawLeftSideOptionText(int selection, int y)
{
    if (CheckConditions(selection))
        PrintToWindow(WIN_OPTIONS, FONT_BLACK, 2, y, sMenuItemsNames[selection]);
    else
        PrintToWindow(WIN_OPTIONS, FONT_RED, 2, y, sMenuItemsNames[selection]);
}

static void DrawOptionsMenuTexts(void) //left side text
{
    u8 i;

    for (i = 0; i < MENUITEM_COUNT; i++)
        DrawLeftSideOptionText(i, (i * Y_DIFF) + 1);
}

static void DrawRightSideChoiceText(const u8 *text, int x, int y, bool8 chosen, bool8 active)
{
    if (chosen)
        PrintToWindow(WIN_OPTIONS, FONT_RED, x, y, text);
    else
        PrintToWindow(WIN_OPTIONS, FONT_BLACK, x, y, text);
}

static void DrawOptionsMenuChoice(const u8 *text, u8 x, u8 y, u8 style, bool8 active)
{
    bool8 chosen = FALSE;
    if (style != 0)
        chosen = TRUE;

    DrawRightSideChoiceText(text, x, y+1, chosen, active);
}

static void DrawChoices(u32 id, int y) //right side draw function
{
    if (sItemFunctionsCustom[id].drawChoices != NULL)
        sItemFunctionsCustom[id].drawChoices(sMenuDataPtr->selection[id], y);
}

static void HighlightOptionsMenuItem(void)
{
    int cursor = sMenuDataPtr->curItem;

    SetGpuReg(REG_OFFSET_WIN0H, WIN_RANGE(8, 160));
    SetGpuReg(REG_OFFSET_WIN0V, WIN_RANGE(cursor * Y_DIFF + 24, cursor * Y_DIFF + 40));
}

static void PrintAllToWindow(void)
{
    u8 i;

    // Draw the top bar text
    DrawTopBarText();

    // Draw the left side option texts
    DrawOptionsMenuTexts();

    // Draw the right side choices
    for (i = 0; i < MENUITEM_COUNT; i++)
        DrawChoices(i, (i * Y_DIFF) + 1);

    // Highlight the currently selected item
    HighlightOptionsMenuItem();
}

/* This is the meat of the UI. This is where you wait for player inputs and can branch to other tasks accordingly */
static void Task_MenuMain(u8 taskId)
{
    if (JOY_NEW(B_BUTTON))
    {   
        // if opened from intro, do nothing, if opened from in-game, return to the field
        
        PlaySE(SE_POKENAV_HANG_UP);
        BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
        gTasks[taskId].func = Task_MenuTurnOff;
    }
    // Choice navigation (left/right)
    else if (JOY_NEW(DPAD_LEFT) || JOY_NEW(DPAD_RIGHT)) {
        u8 row   = sMenuDataPtr->curItem;
        u8 old   = sMenuDataPtr->selection[row];
        u8 next  = old;

        switch (row) {
        case MENUITEM_GENDER:    next = XOptions_ProcessInput(3, old); break; // 0..2
        case MENUITEM_PALLETTE:  next = XOptions_ProcessInput(4, old); break; // 0..3
        case MENUITEM_PRONOUNS:  next = XOptions_ProcessInput(3, old); break; // 0..2
        default: break;
        }

        if (next != old) {
            PlaySE(SE_RG_BAG_CURSOR);
            sMenuDataPtr->selection[row] = next;
            ApplyOnChange(row, next);
            if (row == MENUITEM_GENDER || row == MENUITEM_PALLETTE)
                CC_RefreshPreviewSprites(); // refresh preview

            // redraw only this row
            DrawLeftSideOptionText(row, row * Y_DIFF + 1);
            DrawChoices(row, row * Y_DIFF + 1);
        }
        return;
    }

    else if (JOY_NEW(DPAD_UP)) {
        PlaySE(SE_RG_BAG_CURSOR);
        sMenuDataPtr->curItem = (sMenuDataPtr->curItem == 0) ? (MENUITEM_COUNT - 1) : (sMenuDataPtr->curItem - 1);
        HighlightOptionsMenuItem();
        return;
    }
    else if (JOY_NEW(DPAD_DOWN)) {
        PlaySE(SE_RG_BAG_CURSOR);
        sMenuDataPtr->curItem = (sMenuDataPtr->curItem + 1) % MENUITEM_COUNT;
        HighlightOptionsMenuItem();
        return;
    }

    else if (JOY_NEW(A_BUTTON))
    {
        u8 row = sMenuDataPtr->curItem;

        if (row == MENUITEM_NAME) {
            if (!gPaletteFade.active) {
                PlaySE(SE_SELECT);

                // Hard stop any per-frame work from this screen
                SetVBlankCallback(NULL);
                ScanlineEffect_Stop();

                // Turn off windows/blend entirely (don’t leave darken targets on)
                SetGpuReg(REG_OFFSET_WIN0H, 0);
                SetGpuReg(REG_OFFSET_WIN0V, 0);
                SetGpuReg(REG_OFFSET_WININ, 0);
                SetGpuReg(REG_OFFSET_WINOUT, 0);
                SetGpuReg(REG_OFFSET_BLDCNT, 0);
                SetGpuReg(REG_OFFSET_BLDALPHA, 0);
                SetGpuReg(REG_OFFSET_BLDY, 0);

                // Optional but safest: stop our sprites to avoid OAM churn between engines
                CC_DestroyPreviewSprites();

                // Free window tiles so the naming screen isn’t fighting for VRAM
                FreeAllWindowBuffers();

                DoNamingScreen(
                    NAMING_SCREEN_PLAYER,
                    gSaveBlock2Ptr->playerName,
                    gSaveBlock2Ptr->playerGender,
                    0,
                    gSaveBlock2Ptr->currOutfitId,
                    CB2_ReturnFromNamingScreen
                );
            }
        }

        else if (row == MENUITEM_FINISH) {
            PlaySE(SE_SELECT);
            
            // Confirm finish
            ShowContextMenu(taskId);
        }
    }
}

static void SetDefaultPlayerName(u8 nameId)
{
    const u8 *name;
    u8 i;

    if (gSaveBlock2Ptr->playerGender == MALE)
        name = sMalePresetNames[nameId];
    else
        name = sFemalePresetNames[nameId];
    for (i = 0; i < PLAYER_NAME_LENGTH; i++)
        gSaveBlock2Ptr->playerName[i] = name[i];
    gSaveBlock2Ptr->playerName[PLAYER_NAME_LENGTH] = EOS;
}

static void CB2_ReturnFromNamingScreen(void)
{
    // Nuke any stale state from the naming screen or previous menu instances
    ResetTasks();

    // Re-run your usual scene setup
    gMain.state = 0;

    // initialize stuff
    sMenuDataPtr->curItem = MENUITEM_NAME;
    for (int i = 0; i < SPR_CC_COUNT; i++) sMenuDataPtr->sprIds[i] = SPRITE_NONE;

    // seed initial selections
    sMenuDataPtr->selection[MENUITEM_GENDER] = (gSaveBlock2Ptr->playerGender == FEMALE) ? 1 : 0;
    sMenuDataPtr->selection[MENUITEM_PALLETTE] = 0;  // 0..3 for palette
    sMenuDataPtr->selection[MENUITEM_PRONOUNS] = VarGet(VAR_PRONOUNS);
    sMenuDataPtr->selection[MENUITEM_NAME]     = 0;  // unused, but harmless
    SetMainCallback2(Menu_RunSetup);
}

static int XOptions_ProcessInput(int x, int selection)
{
    if (JOY_NEW(DPAD_RIGHT))
    {
        if (++selection > (x - 1))
            selection = 0;
    }
    if (JOY_NEW(DPAD_LEFT))
    {
        if (--selection < 0)
            selection = (x - 1);
    }
    return selection;
}

static int ProcessInput_Options_One(int selection)
{
    return selection;
}

static int ProcessInput_Options_Two(int selection)
{
    if (JOY_NEW(DPAD_LEFT | DPAD_RIGHT))
        selection ^= 1;

    return selection;
}

static int ProcessInput_Options_Three(int selection)
{
    return XOptions_ProcessInput(3, selection);
}

static int ProcessInput_Options_Four(int selection)
{
    return XOptions_ProcessInput(4, selection);
}

// Process Input functions ****SPECIFIC****
static const u8 sTextGenderMale[] = _("A");
static const u8 sTextGenderFemale[] = _("B");
static const u8 sTextPalette1[] = _("1");
static const u8 sTextPalette2[] = _("2");
static const u8 sTextPalette3[] = _("3");
static const u8 sTextPalette4[] = _("4");

// --- On-change hooks (put these near the bottom) ---
static void OnChange_Gender(u8 value)      { gSaveBlock2Ptr->playerGender = (value == 1) ? FEMALE : MALE; /* keep NB purely cosmetic or store elsewhere */ }
static void OnChange_Palette(u8 value)     { gSaveBlock2Ptr->currOutfitId = value; /* keep NB purely cosmetic or store elsewhere */ }
static void OnChange_Pronouns(u8 value)    { VarSet(VAR_PRONOUNS, value); }

static void ApplyOnChange(u8 item, u8 value)
{
    switch (item) {
    case MENUITEM_GENDER:    OnChange_Gender(value);    break;
    case MENUITEM_PALLETTE:  OnChange_Palette(value);   break;
    case MENUITEM_PRONOUNS:  OnChange_Pronouns(value);  break;
    default: break;
    }
}

#define choiceX 64 // X position for choices

static void DrawChoices_Gender(int selection, int y)
{
    bool8 active = CheckConditions(MENUITEM_GENDER);
    u8 styles[2] = {0};
    int xSpacerMale, xSpacerFemale;

    styles[selection] = 1;
    xSpacerMale = (GetStringRightAlignXOffset(1, sTextGenderMale, 198) - choiceX) / 2;
    xSpacerFemale = (GetStringRightAlignXOffset(1, sTextGenderFemale, 198) - choiceX) / 2;

    DrawOptionsMenuChoice(sTextGenderMale, choiceX, y, styles[0], active);
    DrawOptionsMenuChoice(sTextGenderFemale, choiceX + xSpacerMale, y, styles[1], active);
}

static void DrawChoices_Name(int selection, int y)
{
    u8 styles[1] = {0};

    const u8 *name = gSaveBlock2Ptr->playerName;
    PrintToWindow(WIN_OPTIONS, FONT_BLACK, choiceX, y, name);
}

static void DrawChoices_Palette(int selection, int y)
{
    bool8 active = CheckConditions(MENUITEM_PALLETTE);
    u8 styles[4] = {0};

    styles[selection] = 1;

    DrawOptionsMenuChoice(sTextPalette1, choiceX, y, styles[0], active);
    DrawOptionsMenuChoice(sTextPalette2, choiceX + 21, y, styles[1], active);
    DrawOptionsMenuChoice(sTextPalette3, choiceX + 43, y, styles[2], active);
    DrawOptionsMenuChoice(sTextPalette4, choiceX + 64, y, styles[3], active);
}

static void DrawChoices_Pronouns(int selection, int y)
{
    bool8 active = CheckConditions(MENUITEM_PRONOUNS);
    u8 styles[3] = {0};

    styles[selection] = 1;

    DrawOptionsMenuChoice(gText_MaleSymbol, choiceX, y, styles[0], active);
    DrawOptionsMenuChoice(gText_FemaleSymbol, choiceX + 32, y, styles[1], active);
    DrawOptionsMenuChoice(gText_LevelSymbol, choiceX + 64, y, styles[2], active);
}


// sprites
#define CC_OW_Y      (136)    // bottom window center-ish
static const s16 sOwX[4] = { 32,  48, 64, 80 }; // S,N,W,E order

#define CC_FRONT_X   (176 + 32 -8)
#define CC_FRONT_Y   (48)
#define CC_BACK_X    (176 + 32 - 8)
#define CC_BACK_Y    (64+96)

static void CC_DestroyPreviewSprites(void)
{
    if (sMenuDataPtr->sprIds[SPR_CC_FRONT] != SPRITE_NONE)
        FreeAndDestroyTrainerPicSprite(sMenuDataPtr->sprIds[SPR_CC_FRONT]), sMenuDataPtr->sprIds[SPR_CC_FRONT] = SPRITE_NONE;
    if (sMenuDataPtr->sprIds[SPR_CC_BACK]  != SPRITE_NONE)
        FreeAndDestroyTrainerPicSprite(sMenuDataPtr->sprIds[SPR_CC_BACK]),  sMenuDataPtr->sprIds[SPR_CC_BACK]  = SPRITE_NONE;

    for (int i = SPR_CC_OW_S; i <= SPR_CC_OW_E; i++) {
        u8 id = sMenuDataPtr->sprIds[i];
        if (id != SPRITE_NONE && gSprites[id].inUse)
            DestroySprite(&gSprites[id]);
        sMenuDataPtr->sprIds[i] = SPRITE_NONE;
    }
}

static void CC_CreateTrainerPics(void)
{
    u32 outfit = gSaveBlock2Ptr->currOutfitId;
    u32 gender = gSaveBlock2Ptr->playerGender;

    u32 frontId = GetPlayerTrainerPicIdByOutfitGenderType(outfit, gender, 0);
    u32 backId  = GetPlayerTrainerPicIdByOutfitGenderType(outfit, gender, 1);

    u32 frontPalSlot = sMenuDataPtr->slotId ? 9 : 10;
    u32 backPalSlot = sMenuDataPtr->slotId ? 12 : 13;

    // Front trainer pic
    sMenuDataPtr->sprIds[SPR_CC_FRONT] =
        CreateTrainerPicSprite(frontId, TRUE, CC_FRONT_X, CC_FRONT_Y,
                               frontPalSlot, TAG_NONE);

    // Back trainer pic
    sMenuDataPtr->sprIds[SPR_CC_BACK] =
        CreateTrainerPicSprite(backId, FALSE, CC_BACK_X, CC_BACK_Y,
                               backPalSlot, TAG_NONE);

    // Ensure the back sprite’s palette is loaded (mirrors your outfit menu)
    LoadPalette(gTrainerBacksprites[backId].palette.data,
                OBJ_PLTT_ID(backPalSlot), PLTT_SIZE_4BPP);

    gSprites[sMenuDataPtr->sprIds[SPR_CC_BACK]].anims = gTrainerBacksprites[backId].animation;
    StartSpriteAnim(&gSprites[sMenuDataPtr->sprIds[SPR_CC_BACK]], 0);

    sMenuDataPtr->slotId ^= 1;
}

static u8 CC_CreateOverworldPreview(u8 dir, s16 x, s16 y)
{
    // Get the standard OW graphics (state NORMAL) for current gender/outfit
    u32 outfit  = gSaveBlock2Ptr->currOutfitId;
    u32 gender  = gSaveBlock2Ptr->playerGender;
    u32 gfxId   = GetPlayerAvatarGraphicsIdByOutfitStateIdAndGender(
                      outfit, PLAYER_AVATAR_STATE_NORMAL, gender);

    u8 spr = CreateObjectGraphicsSprite(gfxId, SpriteCallbackDummy, x, y, 0);

    // Pick a walk anim for this compass dir
    switch (dir) {
    case SPR_CC_OW_S: StartSpriteAnim(&gSprites[spr], ANIM_STD_GO_SOUTH); break;
    case SPR_CC_OW_N: StartSpriteAnim(&gSprites[spr], ANIM_STD_GO_NORTH); break;
    case SPR_CC_OW_W: StartSpriteAnim(&gSprites[spr], ANIM_STD_GO_WEST ); break;
    case SPR_CC_OW_E: StartSpriteAnim(&gSprites[spr], ANIM_STD_GO_EAST ); break;
    }

    return spr;
}

static void CC_CreateOverworldRow(void)
{
    sMenuDataPtr->sprIds[SPR_CC_OW_S] = CC_CreateOverworldPreview(SPR_CC_OW_S, sOwX[0], CC_OW_Y);
    //gSprites[sMenuDataPtr->sprIds[SPR_CC_OW_S]].data[0] = 1; // set a flag to indicate this is an overworld sprite
    sMenuDataPtr->sprIds[SPR_CC_OW_N] = CC_CreateOverworldPreview(SPR_CC_OW_N, sOwX[1], CC_OW_Y);
    sMenuDataPtr->sprIds[SPR_CC_OW_W] = CC_CreateOverworldPreview(SPR_CC_OW_W, sOwX[2], CC_OW_Y);
    sMenuDataPtr->sprIds[SPR_CC_OW_E] = CC_CreateOverworldPreview(SPR_CC_OW_E, sOwX[3], CC_OW_Y);
}

static void CC_CreatePreviewSprites(void)
{
    CC_CreateOverworldRow();
    CC_CreateTrainerPics();
}

static void CC_RefreshPreviewSprites(void)
{
    CC_DestroyPreviewSprites();
    CC_CreatePreviewSprites();
}

// Callback for the Yes/No menu, and window additions
void YesNo(u8 taskId, u8 windowType, const struct YesNoFuncTable *funcTable)
{
    CreateYesNoMenuWithCallbacksOverride(taskId, &sContextMenuWindowTemplates[windowType], 1, 0, 2, 1, 15, funcTable);
}

static void ShowContextMenu(u8 taskId)
{
    s16 *data = gTasks[taskId].data;

    YesNo(taskId, WIN_YESNO, &sYesNoFunctions);
}

static void ConfirmFinish(u8 taskId)
{
    s16 *data = gTasks[taskId].data;
    BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
    //SetMainCallback2(Task_OpenCharacterCreation);
}

static void CancelFinish(u8 taskId)
{
    s16 *data = gTasks[taskId].data;
    BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
    //SetMainCallback2(Task_OpenCharacterCreation);
}
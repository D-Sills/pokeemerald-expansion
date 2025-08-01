#include "global.h"
#include "main.h"
#include "event_data.h"
#include "field_effect.h"
#include "field_specials.h"
#include "item.h"
#include "menu.h"
#include "palette.h"
#include "script.h"
#include "script_menu.h"
#include "sound.h"
#include "string_util.h"
#include "strings.h"
#include "task.h"
#include "text.h"
#include "list_menu.h"
#include "malloc.h"
#include "util.h"
#include "item_icon.h"
#include "constants/field_specials.h"
#include "constants/items.h"
#include "constants/script_menu.h"
#include "constants/songs.h"

#include "data/script_menu.h"

struct DynamicListMenuEventArgs
{
    struct ListMenuTemplate *list;
    u16 selectedItem;
    u8 windowId;
};

typedef void (*DynamicListCallback)(struct DynamicListMenuEventArgs *eventArgs);

struct DynamicListMenuEventCollection
{
    DynamicListCallback OnInit;
    DynamicListCallback OnSelectionChanged;
    DynamicListCallback OnDestroy;
};

static EWRAM_DATA u8 sProcessInputDelay = 0;
static EWRAM_DATA u8 sDynamicMenuEventId = 0;
static EWRAM_DATA struct DynamicMultichoiceStack *sDynamicMultiChoiceStack = NULL;
static EWRAM_DATA u16 *sDynamicMenuEventScratchPad = NULL;

static u8 sLilycoveSSTidalSelections[SSTIDAL_SELECTION_COUNT];

static void FreeListMenuItems(struct ListMenuItem *items, u32 count);
static void Task_HandleScrollingMultichoiceInput(u8 taskId);
static void Task_HandleMultichoiceInput(u8 taskId);
static void Task_HandleYesNoInput(u8 taskId);
static void Task_HandleMultichoiceGridInput(u8 taskId);
static void DrawMultichoiceMenuDynamic(u8 left, u8 top, u8 argc, struct ListMenuItem *items, bool8 ignoreBPress, u32 initialRow, u8 maxBeforeScroll, u32 callbackSet);
static void DrawMultichoiceMenu(u8 left, u8 top, u8 multichoiceId, bool8 ignoreBPress, u8 cursorPos);
static void InitMultichoiceCheckWrap(bool8 ignoreBPress, u8 count, u8 windowId, u8 multichoiceId);
static void DrawLinkServicesMultichoiceMenu(u8 multichoiceId);
static void CreatePCMultichoice(void);
static void CreateLilycoveSSTidalMultichoice(void);
static bool8 IsPicboxClosed(void);
static void CreateStartMenuForPokenavTutorial(void);
static void InitMultichoiceNoWrap(bool8 ignoreBPress, u8 unusedCount, u8 windowId, u8 multichoiceId);
static void MultichoiceDynamicEventDebug_OnInit(struct DynamicListMenuEventArgs *eventArgs);
static void MultichoiceDynamicEventDebug_OnSelectionChanged(struct DynamicListMenuEventArgs *eventArgs);
static void MultichoiceDynamicEventDebug_OnDestroy(struct DynamicListMenuEventArgs *eventArgs);
static void MultichoiceDynamicEventShowItem_OnInit(struct DynamicListMenuEventArgs *eventArgs);
static void MultichoiceDynamicEventShowItem_OnSelectionChanged(struct DynamicListMenuEventArgs *eventArgs);
static void MultichoiceDynamicEventShowItem_OnDestroy(struct DynamicListMenuEventArgs *eventArgs);

static const struct DynamicListMenuEventCollection sDynamicListMenuEventCollections[] =
{
    [DYN_MULTICHOICE_CB_DEBUG] =
    {
        .OnInit = MultichoiceDynamicEventDebug_OnInit,
        .OnSelectionChanged = MultichoiceDynamicEventDebug_OnSelectionChanged,
        .OnDestroy = MultichoiceDynamicEventDebug_OnDestroy
    },
    [DYN_MULTICHOICE_CB_SHOW_ITEM] =
    {
        .OnInit = MultichoiceDynamicEventShowItem_OnInit,
        .OnSelectionChanged = MultichoiceDynamicEventShowItem_OnSelectionChanged,
        .OnDestroy = MultichoiceDynamicEventShowItem_OnDestroy
    }
};

static const struct ListMenuTemplate sScriptableListMenuTemplate =
{
    .item_X = 8,
    .upText_Y = 1,
    .cursorPal = 2,
    .fillValue = 1,
    .cursorShadowPal = 3,
    .lettersSpacing = 1,
    .scrollMultiple = LIST_NO_MULTIPLE_SCROLL,
    .fontId = FONT_NORMAL,
};

bool8 ScriptMenu_MultichoiceDynamic(u8 left, u8 top, u8 argc, struct ListMenuItem *items, bool8 ignoreBPress, u8 maxBeforeScroll, u32 initialRow, u32 callbackSet)
{
    if (FuncIsActiveTask(Task_HandleMultichoiceInput) == TRUE)
    {
        FreeListMenuItems(items, argc);
        return FALSE;
    }
    else
    {
        gSpecialVar_Result = 0xFF;
        DrawMultichoiceMenuDynamic(left, top, argc, items, ignoreBPress, initialRow, maxBeforeScroll, callbackSet);
        return TRUE;
    }
}

bool8 ScriptMenu_Multichoice(u8 left, u8 top, u8 multichoiceId, bool8 ignoreBPress)
{
    if (FuncIsActiveTask(Task_HandleMultichoiceInput) == TRUE)
    {
        return FALSE;
    }
    else
    {
        gSpecialVar_Result = 0xFF;
        DrawMultichoiceMenu(left, top, multichoiceId, ignoreBPress, 0);
        return TRUE;
    }
}

bool8 ScriptMenu_MultichoiceWithDefault(u8 left, u8 top, u8 multichoiceId, bool8 ignoreBPress, u8 defaultChoice)
{
    if (FuncIsActiveTask(Task_HandleMultichoiceInput) == TRUE)
    {
        return FALSE;
    }
    else
    {
        gSpecialVar_Result = 0xFF;
        DrawMultichoiceMenu(left, top, multichoiceId, ignoreBPress, defaultChoice);
        return TRUE;
    }
}

static void MultichoiceDynamicEventDebug_OnInit(struct DynamicListMenuEventArgs *eventArgs)
{
    DebugPrintf("OnInit: %d", eventArgs->windowId);
}

static void MultichoiceDynamicEventDebug_OnSelectionChanged(struct DynamicListMenuEventArgs *eventArgs)
{
    DebugPrintf("OnSelectionChanged: %d", eventArgs->selectedItem);
}

static void MultichoiceDynamicEventDebug_OnDestroy(struct DynamicListMenuEventArgs *eventArgs)
{
    DebugPrintf("OnDestroy: %d", eventArgs->windowId);
}

#define sAuxWindowId sDynamicMenuEventScratchPad[0]
#define sItemSpriteId sDynamicMenuEventScratchPad[1]
#define TAG_CB_ITEM_ICON 3000

static void MultichoiceDynamicEventShowItem_OnInit(struct DynamicListMenuEventArgs *eventArgs)
{
    struct WindowTemplate *template = &gWindows[eventArgs->windowId].window;
    u32 baseBlock = template->baseBlock + template->width * template->height;
    struct WindowTemplate auxTemplate = CreateWindowTemplate(0, template->tilemapLeft + template->width + 2, template->tilemapTop, 4, 4, 15, baseBlock);
    u32 auxWindowId = AddWindow(&auxTemplate);
    SetStandardWindowBorderStyle(auxWindowId, FALSE);
    FillWindowPixelBuffer(auxWindowId, 0x11);
    CopyWindowToVram(auxWindowId, COPYWIN_FULL);
    sAuxWindowId = auxWindowId;
    sItemSpriteId = MAX_SPRITES;
}

static void MultichoiceDynamicEventShowItem_OnSelectionChanged(struct DynamicListMenuEventArgs *eventArgs)
{
    struct WindowTemplate *template = &gWindows[eventArgs->windowId].window;
    u32 x = template->tilemapLeft * 8 + template->width * 8 + 36;
    u32 y = template->tilemapTop * 8 + 20;

    if (sItemSpriteId != MAX_SPRITES)
    {
        FreeSpriteTilesByTag(TAG_CB_ITEM_ICON);
        FreeSpritePaletteByTag(TAG_CB_ITEM_ICON);
        DestroySprite(&gSprites[sItemSpriteId]);
    }

    sItemSpriteId = AddItemIconSprite(TAG_CB_ITEM_ICON, TAG_CB_ITEM_ICON, eventArgs->selectedItem);
    gSprites[sItemSpriteId].oam.priority = 0;
    gSprites[sItemSpriteId].x = x;
    gSprites[sItemSpriteId].y = y;
}

static void MultichoiceDynamicEventShowItem_OnDestroy(struct DynamicListMenuEventArgs *eventArgs)
{
    ClearStdWindowAndFrame(sAuxWindowId, TRUE);
    RemoveWindow(sAuxWindowId);

    if (sItemSpriteId != MAX_SPRITES)
    {
        FreeSpriteTilesByTag(TAG_CB_ITEM_ICON);
        FreeSpritePaletteByTag(TAG_CB_ITEM_ICON);
        DestroySprite(&gSprites[sItemSpriteId]);
    }
}

#undef sAuxWindowId
#undef sItemSpriteId
#undef TAG_CB_ITEM_ICON

static void FreeListMenuItems(struct ListMenuItem *items, u32 count)
{
    u32 i;
    for (i = 0; i < count; ++i)
    {
        // All items were dynamically allocated, so items[i].name is not actually constant.
        Free((void *)items[i].name);
    }
    Free(items);
}

static u16 UNUSED GetLengthWithExpandedPlayerName(const u8 *str)
{
    u16 length = 0;

    while (*str != EOS)
    {
        if (*str == PLACEHOLDER_BEGIN)
        {
            str++;
            if (*str == PLACEHOLDER_ID_PLAYER)
            {
                length += StringLength(gSaveBlock2Ptr->playerName);
                str++;
            }
        }
        else
        {
            str++;
            length++;
        }
    }

    return length;
}

void MultichoiceDynamic_InitStack(u32 capacity)
{
    AGB_ASSERT(sDynamicMultiChoiceStack == NULL);
    sDynamicMultiChoiceStack = AllocZeroed(sizeof(*sDynamicMultiChoiceStack));
    AGB_ASSERT(sDynamicMultiChoiceStack != NULL);
    sDynamicMultiChoiceStack->capacity = capacity;
    sDynamicMultiChoiceStack->top = -1;
    sDynamicMultiChoiceStack->elements = AllocZeroed(capacity * sizeof(struct ListMenuItem));
}

void MultichoiceDynamic_ReallocStack(u32 newCapacity)
{
    struct ListMenuItem *newElements;
    AGB_ASSERT(sDynamicMultiChoiceStack != NULL);
    AGB_ASSERT(sDynamicMultiChoiceStack->capacity < newCapacity);
    newElements = AllocZeroed(newCapacity * sizeof(struct ListMenuItem));
    AGB_ASSERT(newElements != NULL);
    memcpy(newElements, sDynamicMultiChoiceStack->elements, sDynamicMultiChoiceStack->capacity * sizeof(struct ListMenuItem));
    Free(sDynamicMultiChoiceStack->elements);
    sDynamicMultiChoiceStack->elements = newElements;
    sDynamicMultiChoiceStack->capacity = newCapacity;
}

bool32 MultichoiceDynamic_StackFull(void)
{
    AGB_ASSERT(sDynamicMultiChoiceStack != NULL);
    return sDynamicMultiChoiceStack->top == sDynamicMultiChoiceStack->capacity - 1;
}

bool32 MultichoiceDynamic_StackEmpty(void)
{
    AGB_ASSERT(sDynamicMultiChoiceStack != NULL);
    return sDynamicMultiChoiceStack->top == -1;
}

u32 MultichoiceDynamic_StackSize(void)
{
    AGB_ASSERT(sDynamicMultiChoiceStack != NULL);
    return sDynamicMultiChoiceStack->top + 1;
}

void MultichoiceDynamic_PushElement(struct ListMenuItem item)
{
    if (sDynamicMultiChoiceStack == NULL)
        MultichoiceDynamic_InitStack(MULTICHOICE_DYNAMIC_STACK_SIZE);
    if (MultichoiceDynamic_StackFull())
        MultichoiceDynamic_ReallocStack(sDynamicMultiChoiceStack->capacity + MULTICHOICE_DYNAMIC_STACK_INC);
    sDynamicMultiChoiceStack->elements[++sDynamicMultiChoiceStack->top] = item;
}

struct ListMenuItem *MultichoiceDynamic_PopElement(void)
{
    if (sDynamicMultiChoiceStack == NULL)
        return NULL;
    if (MultichoiceDynamic_StackEmpty())
        return NULL;
    return &sDynamicMultiChoiceStack->elements[sDynamicMultiChoiceStack->top--];
}

struct ListMenuItem *MultichoiceDynamic_PeekElement(void)
{
    if (sDynamicMultiChoiceStack == NULL)
        return NULL;
    if (MultichoiceDynamic_StackEmpty())
        return NULL;
    return &sDynamicMultiChoiceStack->elements[sDynamicMultiChoiceStack->top];
}

struct ListMenuItem *MultichoiceDynamic_PeekElementAt(u32 index)
{
    if (sDynamicMultiChoiceStack == NULL)
        return NULL;
    if (sDynamicMultiChoiceStack->top < index)
        return NULL;
    return &sDynamicMultiChoiceStack->elements[index];
}

void MultichoiceDynamic_DestroyStack(void)
{
    TRY_FREE_AND_SET_NULL(sDynamicMultiChoiceStack->elements);
    TRY_FREE_AND_SET_NULL(sDynamicMultiChoiceStack);
}

static void MultichoiceDynamic_MoveCursor(s32 itemIndex, bool8 onInit, struct ListMenu *list)
{
    u8 taskId;
    if (!onInit)
        PlaySE(SE_SELECT);
    taskId = FindTaskIdByFunc(Task_HandleScrollingMultichoiceInput);
    if (taskId != TASK_NONE)
    {
        ListMenuGetScrollAndRow(gTasks[taskId].data[0], &gScrollableMultichoice_ScrollOffset, NULL);
        if (sDynamicMenuEventId != DYN_MULTICHOICE_CB_NONE && sDynamicListMenuEventCollections[sDynamicMenuEventId].OnSelectionChanged && !onInit)
        {
            struct DynamicListMenuEventArgs eventArgs = {.selectedItem = itemIndex, .windowId = list->template.windowId, .list = &list->template};
            sDynamicListMenuEventCollections[sDynamicMenuEventId].OnSelectionChanged(&eventArgs);
        }
    }
}

static void DrawMultichoiceMenuDynamic(u8 left, u8 top, u8 argc, struct ListMenuItem *items, bool8 ignoreBPress, u32 initialRow, u8 maxBeforeScroll, u32 callbackSet)
{
    u32 i;
    u8 windowId;
    s32 width = 0;
    u8 newWidth;
    u8 taskId;
    u32 windowHeight;
    struct ListMenu *list;

    for (i = 0; i < argc; ++i)
    {
        width = DisplayTextAndGetWidth(items[i].name, width);
    }
    LoadMessageBoxAndBorderGfxOverride();
    windowHeight = (argc < maxBeforeScroll) ? argc * 2 : maxBeforeScroll * 2;
    newWidth = ConvertPixelWidthToTileWidth(width);
    left = ScriptMenu_AdjustLeftCoordFromWidth(left, newWidth);
    windowId = CreateWindowFromRect(left, top, newWidth, windowHeight);
    SetStandardWindowBorderStyle(windowId, FALSE);
    CopyWindowToVram(windowId, COPYWIN_FULL);

    // I don't like this being global either, but I could not come up with another solution that
    // does not invade the whole ListMenu infrastructure.
    sDynamicMenuEventId = callbackSet;
    sDynamicMenuEventScratchPad = AllocZeroed(100 * sizeof(u16));
    if (sDynamicMenuEventId != DYN_MULTICHOICE_CB_NONE && sDynamicListMenuEventCollections[sDynamicMenuEventId].OnInit)
    {
        struct DynamicListMenuEventArgs eventArgs = {.selectedItem = initialRow, .windowId = windowId, .list = NULL};
        sDynamicListMenuEventCollections[sDynamicMenuEventId].OnInit(&eventArgs);
    }

    gMultiuseListMenuTemplate = sScriptableListMenuTemplate;
    gMultiuseListMenuTemplate.windowId = windowId;
    gMultiuseListMenuTemplate.items = items;
    gMultiuseListMenuTemplate.totalItems = argc;
    gMultiuseListMenuTemplate.maxShowed = maxBeforeScroll;
    gMultiuseListMenuTemplate.moveCursorFunc = MultichoiceDynamic_MoveCursor;

    taskId = CreateTask(Task_HandleScrollingMultichoiceInput, 80);
    gTasks[taskId].data[0] = ListMenuInit(&gMultiuseListMenuTemplate, 0, 0);
    gTasks[taskId].data[1] = ignoreBPress;
    gTasks[taskId].data[2] = windowId;
    gTasks[taskId].data[5] = argc;
    gTasks[taskId].data[7] = maxBeforeScroll;
    StoreWordInTwoHalfwords((u16*) &gTasks[taskId].data[3], (u32) items);
    list = (void *) gTasks[gTasks[taskId].data[0]].data;
    ListMenuChangeSelectionFull(list, TRUE, FALSE, initialRow, TRUE);

    if (sDynamicMenuEventId != DYN_MULTICHOICE_CB_NONE && sDynamicListMenuEventCollections[sDynamicMenuEventId].OnSelectionChanged)
    {
        struct DynamicListMenuEventArgs eventArgs = {.selectedItem = items[initialRow].id, .windowId = windowId, .list = &gMultiuseListMenuTemplate};
        sDynamicListMenuEventCollections[sDynamicMenuEventId].OnSelectionChanged(&eventArgs);
    }
    ListMenuGetScrollAndRow(gTasks[taskId].data[0], &gScrollableMultichoice_ScrollOffset, NULL);
    if (argc > maxBeforeScroll)
    {
        // Create Scrolling Arrows
        struct ScrollArrowsTemplate template;
        template.firstX = (newWidth / 2) * 8 + 12 + (left) * 8;
        template.firstY = top * 8 + 5;
        template.secondX = template.firstX;
        template.secondY = top * 8 + windowHeight * 8 + 12;
        template.fullyUpThreshold = 0;
        template.fullyDownThreshold = argc - maxBeforeScroll;
        template.firstArrowType = SCROLL_ARROW_UP;
        template.secondArrowType = SCROLL_ARROW_DOWN;
        template.tileTag = 2000;
        template.palTag = 100,
        template.palNum = 0;

        gTasks[taskId].data[6] = AddScrollIndicatorArrowPair(&template, &gScrollableMultichoice_ScrollOffset);
    }
}

void DrawMultichoiceMenuInternal(u8 left, u8 top, u8 multichoiceId, bool8 ignoreBPress, u8 cursorPos, const struct MenuAction *actions, int count)
{
    int i;
    u8 windowId;
    int width = 0;
    u8 newWidth;

    for (i = 0; i < count; i++)
    {
        width = DisplayTextAndGetWidth(actions[i].text, width);
    }

    newWidth = ConvertPixelWidthToTileWidth(width);
    left = ScriptMenu_AdjustLeftCoordFromWidth(left, newWidth);
    windowId = CreateWindowFromRect(left, top, newWidth, count * 2);
    SetStandardWindowBorderStyle(windowId, FALSE);
    PrintMenuTable(windowId, count, actions);
    InitMenuInUpperLeftCornerNormal(windowId, count, cursorPos);
    ScheduleBgCopyTilemapToVram(0);
    InitMultichoiceCheckWrap(ignoreBPress, count, windowId, multichoiceId);
}

static void DrawMultichoiceMenu(u8 left, u8 top, u8 multichoiceId, bool8 ignoreBPress, u8 cursorPos)
{
    DrawMultichoiceMenuInternal(left, top, multichoiceId, ignoreBPress, cursorPos, sMultichoiceLists[multichoiceId].list, sMultichoiceLists[multichoiceId].count);
}

#define tLeft           data[0]
#define tTop            data[1]
#define tRight          data[2]
#define tBottom         data[3]
#define tIgnoreBPress   data[4]
#define tDoWrap         data[5]
#define tWindowId       data[6]
#define tMultichoiceId  data[7]

static void InitMultichoiceCheckWrap(bool8 ignoreBPress, u8 count, u8 windowId, u8 multichoiceId)
{
    u8 i;
    u8 taskId;
    sProcessInputDelay = 2;

    for (i = 0; i < ARRAY_COUNT(sLinkServicesMultichoiceIds); i++)
    {
        if (sLinkServicesMultichoiceIds[i] == multichoiceId)
        {
            sProcessInputDelay = 12;
        }
    }

    taskId = CreateTask(Task_HandleMultichoiceInput, 80);

    gTasks[taskId].tIgnoreBPress = ignoreBPress;

    if (count > 3)
        gTasks[taskId].tDoWrap = TRUE;
    else
        gTasks[taskId].tDoWrap = FALSE;

    gTasks[taskId].tWindowId = windowId;
    gTasks[taskId].tMultichoiceId = multichoiceId;

    DrawLinkServicesMultichoiceMenu(multichoiceId);
}

static void Task_HandleScrollingMultichoiceInput(u8 taskId)
{
    bool32 done = FALSE;
    s32 input = ListMenu_ProcessInput(gTasks[taskId].data[0]);

    switch (input)
    {
    case LIST_HEADER:
    case LIST_NOTHING_CHOSEN:
        break;
    case LIST_CANCEL:
        if (!gTasks[taskId].data[1])
        {
            gSpecialVar_Result = MULTI_B_PRESSED;
            done = TRUE;
        }
        break;
    default:
        gSpecialVar_Result = input;
        done = TRUE;
        break;
    }

    if (done)
    {
        struct ListMenuItem *items;

        PlaySE(SE_SELECT);

        if (sDynamicMenuEventId != DYN_MULTICHOICE_CB_NONE && sDynamicListMenuEventCollections[sDynamicMenuEventId].OnDestroy)
        {
            struct DynamicListMenuEventArgs eventArgs = {.selectedItem = input, .windowId = gTasks[taskId].data[2], .list = NULL};
            sDynamicListMenuEventCollections[sDynamicMenuEventId].OnDestroy(&eventArgs);
        }

        sDynamicMenuEventId = DYN_MULTICHOICE_CB_NONE;

        if (gTasks[taskId].data[5] > gTasks[taskId].data[7])
        {
            RemoveScrollIndicatorArrowPair(gTasks[taskId].data[6]);
        }

        LoadWordFromTwoHalfwords((u16*) &gTasks[taskId].data[3], (u32* )(&items));
        FreeListMenuItems(items, gTasks[taskId].data[5]);
        TRY_FREE_AND_SET_NULL(sDynamicMenuEventScratchPad);
        DestroyListMenuTask(gTasks[taskId].data[0], NULL, NULL);
        ClearStdWindowAndFrame(gTasks[taskId].data[2], TRUE);
        RemoveWindow(gTasks[taskId].data[2]);
        ScriptContext_Enable();
        DestroyTask(taskId);
    }
}

static void Task_HandleMultichoiceInput(u8 taskId)
{
    s8 selection;
    s16 *data = gTasks[taskId].data;

    if (!gPaletteFade.active)
    {
        if (sProcessInputDelay)
        {
            sProcessInputDelay--;
        }
        else
        {
            if (!tDoWrap)
                selection = Menu_ProcessInputNoWrap();
            else
                selection = Menu_ProcessInput();

            if (JOY_NEW(DPAD_UP | DPAD_DOWN))
            {
                DrawLinkServicesMultichoiceMenu(tMultichoiceId);
            }

            if (selection != MENU_NOTHING_CHOSEN)
            {
                if (selection == MENU_B_PRESSED)
                {
                    if (tIgnoreBPress)
                        return;
                    PlaySE(SE_SELECT);
                    gSpecialVar_Result = MULTI_B_PRESSED;
                }
                else
                {
                    gSpecialVar_Result = selection;
                }
                ClearToTransparentAndRemoveWindow(tWindowId);
                DestroyTask(taskId);
                ScriptContext_Enable();
            }
        }
    }
}

bool8 ScriptMenu_YesNo(u8 left, u8 top)
{
    if (FuncIsActiveTask(Task_HandleYesNoInput) == TRUE)
    {
        return FALSE;
    }
    else
    {
        gSpecialVar_Result = 0xFF;
        DisplayYesNoMenuDefaultYes();
        CreateTask(Task_HandleYesNoInput, 0x50);
        return TRUE;
    }
}

// Unused
bool8 IsScriptActive(void)
{
    if (gSpecialVar_Result == 0xFF)
        return FALSE;
    else
        return TRUE;
}

static void Task_HandleYesNoInput(u8 taskId)
{
    if (gTasks[taskId].tRight < 5)
    {
        gTasks[taskId].tRight++;
        return;
    }

    switch (Menu_ProcessInputNoWrapClearOnChoose())
    {
    case MENU_NOTHING_CHOSEN:
        return;
    case MENU_B_PRESSED:
    case 1:
        PlaySE(SE_SELECT);
        gSpecialVar_Result = 0;
        break;
    case 0:
        gSpecialVar_Result = 1;
        break;
    }

    DestroyTask(taskId);
    ScriptContext_Enable();
}

bool8 ScriptMenu_MultichoiceGrid(u8 left, u8 top, u8 multichoiceId, bool8 ignoreBPress, u8 columnCount)
{
    if (FuncIsActiveTask(Task_HandleMultichoiceGridInput) == TRUE)
    {
        return FALSE;
    }
    else
    {
        u8 taskId;
        u8 rowCount, newWidth;
        int i, width;

        gSpecialVar_Result = 0xFF;
        width = 0;

        for (i = 0; i < sMultichoiceLists[multichoiceId].count; i++)
        {
            width = DisplayTextAndGetWidth(sMultichoiceLists[multichoiceId].list[i].text, width);
        }

        newWidth = ConvertPixelWidthToTileWidth(width);

        left = ScriptMenu_AdjustLeftCoordFromWidth(left, columnCount * newWidth);
        rowCount = sMultichoiceLists[multichoiceId].count / columnCount;

        taskId = CreateTask(Task_HandleMultichoiceGridInput, 80);

        gTasks[taskId].tIgnoreBPress = ignoreBPress;
        gTasks[taskId].tWindowId = CreateWindowFromRect(left, top, columnCount * newWidth, rowCount * 2);
        SetStandardWindowBorderStyle(gTasks[taskId].tWindowId, FALSE);
        PrintMenuGridTable(gTasks[taskId].tWindowId, newWidth * 8, columnCount, rowCount, sMultichoiceLists[multichoiceId].list);
        InitMenuActionGrid(gTasks[taskId].tWindowId, newWidth * 8, columnCount, rowCount, 0);
        CopyWindowToVram(gTasks[taskId].tWindowId, COPYWIN_FULL);
        return TRUE;
    }
}

static void Task_HandleMultichoiceGridInput(u8 taskId)
{
    s16 *data = gTasks[taskId].data;
    s8 selection = Menu_ProcessGridInput();

    switch (selection)
    {
    case MENU_NOTHING_CHOSEN:
        return;
    case MENU_B_PRESSED:
        if (tIgnoreBPress)
            return;
        PlaySE(SE_SELECT);
        gSpecialVar_Result = MULTI_B_PRESSED;
        break;
    default:
        gSpecialVar_Result = selection;
        break;
    }

    ClearToTransparentAndRemoveWindow(tWindowId);
    DestroyTask(taskId);
    ScriptContext_Enable();
}

#undef tWindowId

bool16 ScriptMenu_CreatePCMultichoice(void)
{
    if (FuncIsActiveTask(Task_HandleMultichoiceInput) == TRUE)
    {
        return FALSE;
    }
    else
    {
        gSpecialVar_Result = 0xFF;
        CreatePCMultichoice();
        return TRUE;
    }
}

static void CreatePCMultichoice(void)
{
    u8 x = 8;
    u32 pixelWidth = 0;
    u8 width;
    u8 numChoices;
    u8 windowId;
    int i;

    for (i = 0; i < ARRAY_COUNT(sPCNameStrings); i++)
    {
        pixelWidth = DisplayTextAndGetWidth(sPCNameStrings[i], pixelWidth);
    }

    if (FlagGet(FLAG_SYS_GAME_CLEAR))
    {
        pixelWidth = DisplayTextAndGetWidth(gText_HallOfFame, pixelWidth);
    }

    width = ConvertPixelWidthToTileWidth(pixelWidth);

    // Include Hall of Fame option if player is champion
    if (FlagGet(FLAG_SYS_GAME_CLEAR))
    {
        numChoices = 4;
        windowId = CreateWindowFromRect(0, 0, width, 8);
        SetStandardWindowBorderStyle(windowId, FALSE);
        AddTextPrinterParameterized(windowId, FONT_NORMAL, gText_HallOfFame, x, 33, TEXT_SKIP_DRAW, NULL);
        AddTextPrinterParameterized(windowId, FONT_NORMAL, gText_LogOff, x, 49, TEXT_SKIP_DRAW, NULL);
    }
    else
    {
        numChoices = 3;
        windowId = CreateWindowFromRect(0, 0, width, 6);
        SetStandardWindowBorderStyle(windowId, FALSE);
        AddTextPrinterParameterized(windowId, FONT_NORMAL, gText_LogOff, x, 33, TEXT_SKIP_DRAW, NULL);
    }

    // Change PC name if player has met Lanette
    if (FlagGet(FLAG_SYS_PC_LANETTE))
        AddTextPrinterParameterized(windowId, FONT_NORMAL, gText_LanettesPC, x, 1, TEXT_SKIP_DRAW, NULL);
    else
        AddTextPrinterParameterized(windowId, FONT_NORMAL, gText_SomeonesPC, x, 1, TEXT_SKIP_DRAW, NULL);

    StringExpandPlaceholders(gStringVar4, gText_PlayersPC);
    PrintPlayerNameOnWindow(windowId, gStringVar4, x, 17);
    InitMenuInUpperLeftCornerNormal(windowId, numChoices, 0);
    CopyWindowToVram(windowId, COPYWIN_FULL);
    InitMultichoiceCheckWrap(FALSE, numChoices, windowId, MULTI_PC);
}

void ScriptMenu_DisplayPCStartupPrompt(void)
{
    LoadMessageBoxAndFrameGfx(0, TRUE);
    AddTextPrinterParameterized2(0, FONT_NORMAL, gText_WhichPCShouldBeAccessed, 0, NULL, TEXT_COLOR_WHITE, TEXT_COLOR_DARK_GRAY, TEXT_COLOR_LIGHT_GRAY);
}

bool8 ScriptMenu_CreateLilycoveSSTidalMultichoice(void)
{
    if (FuncIsActiveTask(Task_HandleMultichoiceInput) == TRUE)
    {
        return FALSE;
    }
    else
    {
        gSpecialVar_Result = 0xFF;
        CreateLilycoveSSTidalMultichoice();
        return TRUE;
    }
}

// gSpecialVar_0x8004 is 1 if the Sailor was shown multiple event tickets at the same time
// otherwise gSpecialVar_0x8004 is 0
static void CreateLilycoveSSTidalMultichoice(void)
{
    u8 selectionCount = 0;
    u8 count;
    u32 pixelWidth;
    u8 width;
    u8 windowId;
    u8 i;
    u32 j;

    for (i = 0; i < SSTIDAL_SELECTION_COUNT; i++)
    {
        sLilycoveSSTidalSelections[i] = 0xFF;
    }

    GetFontAttribute(FONT_NORMAL, FONTATTR_MAX_LETTER_WIDTH);

    if (gSpecialVar_0x8004 == 0)
    {
        sLilycoveSSTidalSelections[selectionCount] = SSTIDAL_SELECTION_SLATEPORT;
        selectionCount++;

        if (FlagGet(FLAG_MET_SCOTT_ON_SS_TIDAL) == TRUE)
        {
            sLilycoveSSTidalSelections[selectionCount] = SSTIDAL_SELECTION_BATTLE_FRONTIER;
            selectionCount++;
        }
    }

    if (CheckBagHasItem(ITEM_EON_TICKET, 1) == TRUE && FlagGet(FLAG_ENABLE_SHIP_SOUTHERN_ISLAND) == TRUE)
    {
        if (gSpecialVar_0x8004 == 0)
        {
            sLilycoveSSTidalSelections[selectionCount] = SSTIDAL_SELECTION_SOUTHERN_ISLAND;
            selectionCount++;
        }

        if (gSpecialVar_0x8004 == 1 && FlagGet(FLAG_SHOWN_EON_TICKET) == FALSE)
        {
            sLilycoveSSTidalSelections[selectionCount] = SSTIDAL_SELECTION_SOUTHERN_ISLAND;
            selectionCount++;
            FlagSet(FLAG_SHOWN_EON_TICKET);
        }
    }

    if (CheckBagHasItem(ITEM_MYSTIC_TICKET, 1) == TRUE && FlagGet(FLAG_ENABLE_SHIP_NAVEL_ROCK) == TRUE)
    {
        if (gSpecialVar_0x8004 == 0)
        {
            sLilycoveSSTidalSelections[selectionCount] = SSTIDAL_SELECTION_NAVEL_ROCK;
            selectionCount++;
        }

        if (gSpecialVar_0x8004 == 1 && FlagGet(FLAG_SHOWN_MYSTIC_TICKET) == FALSE)
        {
            sLilycoveSSTidalSelections[selectionCount] = SSTIDAL_SELECTION_NAVEL_ROCK;
            selectionCount++;
            FlagSet(FLAG_SHOWN_MYSTIC_TICKET);
        }
    }

    if (CheckBagHasItem(ITEM_AURORA_TICKET, 1) == TRUE && FlagGet(FLAG_ENABLE_SHIP_BIRTH_ISLAND) == TRUE)
    {
        if (gSpecialVar_0x8004 == 0)
        {
            sLilycoveSSTidalSelections[selectionCount] = SSTIDAL_SELECTION_BIRTH_ISLAND;
            selectionCount++;
        }

        if (gSpecialVar_0x8004 == 1 && FlagGet(FLAG_SHOWN_AURORA_TICKET) == FALSE)
        {
            sLilycoveSSTidalSelections[selectionCount] = SSTIDAL_SELECTION_BIRTH_ISLAND;
            selectionCount++;
            FlagSet(FLAG_SHOWN_AURORA_TICKET);
        }
    }

    if (CheckBagHasItem(ITEM_OLD_SEA_MAP, 1) == TRUE && FlagGet(FLAG_ENABLE_SHIP_FARAWAY_ISLAND) == TRUE)
    {
        if (gSpecialVar_0x8004 == 0)
        {
            sLilycoveSSTidalSelections[selectionCount] = SSTIDAL_SELECTION_FARAWAY_ISLAND;
            selectionCount++;
        }

        if (gSpecialVar_0x8004 == 1 && FlagGet(FLAG_SHOWN_OLD_SEA_MAP) == FALSE)
        {
            sLilycoveSSTidalSelections[selectionCount] = SSTIDAL_SELECTION_FARAWAY_ISLAND;
            selectionCount++;
            FlagSet(FLAG_SHOWN_OLD_SEA_MAP);
        }
    }

    sLilycoveSSTidalSelections[selectionCount] = SSTIDAL_SELECTION_EXIT;
    selectionCount++;

    if (gSpecialVar_0x8004 == 0 && FlagGet(FLAG_MET_SCOTT_ON_SS_TIDAL) == TRUE)
    {
        count = selectionCount;
    }

    count = selectionCount;
    if (count == SSTIDAL_SELECTION_COUNT)
    {
        gSpecialVar_0x8004 = SCROLL_MULTI_SS_TIDAL_DESTINATION;
        ShowScrollableMultichoice();
    }
    else
    {
        pixelWidth = 0;

        for (j = 0; j < SSTIDAL_SELECTION_COUNT; j++)
        {
            u8 selection = sLilycoveSSTidalSelections[j];
            if (selection != 0xFF)
            {
                pixelWidth = DisplayTextAndGetWidth(sLilycoveSSTidalDestinations[selection], pixelWidth);
            }
        }

        width = ConvertPixelWidthToTileWidth(pixelWidth);
        windowId = CreateWindowFromRect(MAX_MULTICHOICE_WIDTH - width, (6 - count) * 2, width, count * 2);
        SetStandardWindowBorderStyle(windowId, FALSE);

        for (selectionCount = 0, i = 0; i < SSTIDAL_SELECTION_COUNT; i++)
        {
            if (sLilycoveSSTidalSelections[i] != 0xFF)
            {
                AddTextPrinterParameterized(windowId, FONT_NORMAL, sLilycoveSSTidalDestinations[sLilycoveSSTidalSelections[i]], 8, selectionCount * 16 + 1, TEXT_SKIP_DRAW, NULL);
                selectionCount++;
            }
        }

        InitMenuInUpperLeftCornerNormal(windowId, count, count - 1);
        CopyWindowToVram(windowId, COPYWIN_FULL);
        InitMultichoiceCheckWrap(FALSE, count, windowId, MULTI_SSTIDAL_LILYCOVE);
    }
}

void GetLilycoveSSTidalSelection(void)
{
    if (gSpecialVar_Result != MULTI_B_PRESSED)
    {
        gSpecialVar_Result = sLilycoveSSTidalSelections[gSpecialVar_Result];
    }
}

#define tState       data[0]
#define tMonSpecies  data[1]
#define tMonSpriteId data[2]
#define tWindowX     data[3]
#define tWindowY     data[4]
#define tWindowId    data[5]

static void Task_PokemonPicWindow(u8 taskId)
{
    struct Task *task = &gTasks[taskId];

    switch (task->tState)
    {
    case 0:
        task->tState++;
        break;
    case 1:
        // Wait until state is advanced by ScriptMenu_HidePokemonPic
        break;
    case 2:
        FreeResourcesAndDestroySprite(&gSprites[task->tMonSpriteId], task->tMonSpriteId);
        task->tState++;
        break;
    case 3:
        ClearToTransparentAndRemoveWindow(task->tWindowId);
        DestroyTask(taskId);
        break;
    }
}

bool8 ScriptMenu_ShowPokemonPic(u16 species, u8 x, u8 y)
{
    u8 taskId;
    u8 spriteId;

    if (FindTaskIdByFunc(Task_PokemonPicWindow) != TASK_NONE)
    {
        return FALSE;
    }
    else
    {
        spriteId = CreateMonSprite_PicBox(species, x * 8 + 40, y * 8 + 40, 0);
        taskId = CreateTask(Task_PokemonPicWindow, 0x50);
        gTasks[taskId].tWindowId = CreateWindowFromRect(x, y, 8, 8);
        gTasks[taskId].tState = 0;
        gTasks[taskId].tMonSpecies = species;
        gTasks[taskId].tMonSpriteId = spriteId;
        gSprites[spriteId].callback = SpriteCallbackDummy;
        gSprites[spriteId].oam.priority = 0;
        SetStandardWindowBorderStyle(gTasks[taskId].tWindowId, TRUE);
        ScheduleBgCopyTilemapToVram(0);
        return TRUE;
    }
}

bool8 (*ScriptMenu_HidePokemonPic(void))(void)
{
    u8 taskId = FindTaskIdByFunc(Task_PokemonPicWindow);

    if (taskId == TASK_NONE)
        return NULL;
    gTasks[taskId].tState++;
    return IsPicboxClosed;
}

static bool8 IsPicboxClosed(void)
{
    if (FindTaskIdByFunc(Task_PokemonPicWindow) == TASK_NONE)
        return TRUE;
    else
        return FALSE;
}

#undef tState
#undef tMonSpecies
#undef tMonSpriteId
#undef tWindowX
#undef tWindowY
#undef tWindowId

u8 CreateWindowFromRect(u8 x, u8 y, u8 width, u8 height)
{
    struct WindowTemplate template = CreateWindowTemplate(0, x + 1, y + 1, width, height, 15, 100);
    u8 windowId = AddWindow(&template);
    PutWindowTilemap(windowId);
    return windowId;
}

void ClearToTransparentAndRemoveWindow(u8 windowId)
{
    ClearStdWindowAndFrameToTransparent(windowId, TRUE);
    RemoveWindow(windowId);
}

static void DrawLinkServicesMultichoiceMenu(u8 multichoiceId)
{
    switch (multichoiceId)
    {
    case MULTI_WIRELESS_NO_BERRY:
        FillWindowPixelBuffer(0, PIXEL_FILL(1));
        AddTextPrinterParameterized2(0, FONT_NORMAL, sWirelessOptionsNoBerryCrush[Menu_GetCursorPos()], 0, NULL, TEXT_COLOR_DARK_GRAY, TEXT_COLOR_WHITE, TEXT_COLOR_LIGHT_GRAY);
        break;
    case MULTI_CABLE_CLUB_WITH_RECORD_MIX:
        FillWindowPixelBuffer(0, PIXEL_FILL(1));
        AddTextPrinterParameterized2(0, FONT_NORMAL, sCableClubOptions_WithRecordMix[Menu_GetCursorPos()], 0, NULL, TEXT_COLOR_DARK_GRAY, TEXT_COLOR_WHITE, TEXT_COLOR_LIGHT_GRAY);
        break;
    case MULTI_WIRELESS_NO_RECORD:
        FillWindowPixelBuffer(0, PIXEL_FILL(1));
        AddTextPrinterParameterized2(0, FONT_NORMAL, sWirelessOptions_NoRecordMix[Menu_GetCursorPos()], 0, NULL, TEXT_COLOR_DARK_GRAY, TEXT_COLOR_WHITE, TEXT_COLOR_LIGHT_GRAY);
        break;
    case MULTI_WIRELESS_ALL_SERVICES:
        FillWindowPixelBuffer(0, PIXEL_FILL(1));
        AddTextPrinterParameterized2(0, FONT_NORMAL, sWirelessOptions_AllServices[Menu_GetCursorPos()], 0, NULL, TEXT_COLOR_DARK_GRAY, TEXT_COLOR_WHITE, TEXT_COLOR_LIGHT_GRAY);
        break;
    case MULTI_WIRELESS_NO_RECORD_BERRY:
        FillWindowPixelBuffer(0, PIXEL_FILL(1));
        AddTextPrinterParameterized2(0, FONT_NORMAL, sWirelessOptions_NoRecordMixBerryCrush[Menu_GetCursorPos()], 0, NULL, TEXT_COLOR_DARK_GRAY, TEXT_COLOR_WHITE, TEXT_COLOR_LIGHT_GRAY);
        break;
    case MULTI_CABLE_CLUB_NO_RECORD_MIX:
        FillWindowPixelBuffer(0, PIXEL_FILL(1));
        AddTextPrinterParameterized2(0, FONT_NORMAL, sCableClubOptions_NoRecordMix[Menu_GetCursorPos()], 0, NULL, TEXT_COLOR_DARK_GRAY, TEXT_COLOR_WHITE, TEXT_COLOR_LIGHT_GRAY);
        break;
    }
}

bool16 ScriptMenu_CreateStartMenuForPokenavTutorial(void)
{
    if (FuncIsActiveTask(Task_HandleMultichoiceInput) == TRUE)
    {
        return FALSE;
    }
    else
    {
        gSpecialVar_Result = 0xFF;
        CreateStartMenuForPokenavTutorial();
        return TRUE;
    }
}

static void CreateStartMenuForPokenavTutorial(void)
{
    u8 windowId = CreateWindowFromRect(21, 0, 7, 18);
    SetStandardWindowBorderStyle(windowId, FALSE);
    AddTextPrinterParameterized(windowId, FONT_NORMAL, gText_MenuOptionPokedex, 8, 9, TEXT_SKIP_DRAW, NULL);
    AddTextPrinterParameterized(windowId, FONT_NORMAL, gText_MenuOptionPokemon, 8, 25, TEXT_SKIP_DRAW, NULL);
    AddTextPrinterParameterized(windowId, FONT_NORMAL, gText_MenuOptionBag, 8, 41, TEXT_SKIP_DRAW, NULL);
    AddTextPrinterParameterized(windowId, FONT_NORMAL, gText_MenuOptionPokenav, 8, 57, TEXT_SKIP_DRAW, NULL);
    AddTextPrinterParameterized(windowId, FONT_NORMAL, gSaveBlock2Ptr->playerName, 8, 73, TEXT_SKIP_DRAW, NULL);
    AddTextPrinterParameterized(windowId, FONT_NORMAL, gText_MenuOptionSave, 8, 89, TEXT_SKIP_DRAW, NULL);
    AddTextPrinterParameterized(windowId, FONT_NORMAL, gText_MenuOptionOption, 8, 105, TEXT_SKIP_DRAW, NULL);
    AddTextPrinterParameterized(windowId, FONT_NORMAL, gText_MenuOptionExit, 8, 121, TEXT_SKIP_DRAW, NULL);
    InitMenuNormal(windowId, FONT_NORMAL, 0, 9, 16, ARRAY_COUNT(MultichoiceList_ForcedStartMenu), 0);
    InitMultichoiceNoWrap(FALSE, ARRAY_COUNT(MultichoiceList_ForcedStartMenu), windowId, MULTI_FORCED_START_MENU);
    CopyWindowToVram(windowId, COPYWIN_FULL);
}

#define tWindowId       data[6]

static void InitMultichoiceNoWrap(bool8 ignoreBPress, u8 unusedCount, u8 windowId, u8 multichoiceId)
{
    u8 taskId;
    sProcessInputDelay = 2;
    taskId = CreateTask(Task_HandleMultichoiceInput, 80);
    gTasks[taskId].tIgnoreBPress = ignoreBPress;
    gTasks[taskId].tDoWrap = 0;
    gTasks[taskId].tWindowId = windowId;
    gTasks[taskId].tMultichoiceId = multichoiceId;
}

#undef tLeft
#undef tTop
#undef tRight
#undef tBottom
#undef tIgnoreBPress
#undef tDoWrap
#undef tWindowId
#undef tMultichoiceId

static int DisplayTextAndGetWidthInternal(const u8 *str)
{
    u8 temp[64];
    StringExpandPlaceholders(temp, str);
    return GetStringWidth(FONT_NORMAL, temp, 0);
}

int DisplayTextAndGetWidth(const u8 *str, int prevWidth)
{
    int width = DisplayTextAndGetWidthInternal(str);
    if (width < prevWidth)
    {
        width = prevWidth;
    }
    return width;
}

int ConvertPixelWidthToTileWidth(int width)
{
    return (((width + 9) / 8) + 1) > MAX_MULTICHOICE_WIDTH ? MAX_MULTICHOICE_WIDTH : (((width + 9) / 8) + 1);
}

int ScriptMenu_AdjustLeftCoordFromWidth(int left, int width)
{
    int adjustedLeft = left;

    if (left + width > MAX_MULTICHOICE_WIDTH)
    {
        if (MAX_MULTICHOICE_WIDTH - width < 0)
        {
            adjustedLeft = 0;
        }
        else
        {
            adjustedLeft = MAX_MULTICHOICE_WIDTH - width;
        }
    }

    return adjustedLeft;
}

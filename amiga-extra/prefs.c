/*
  Copyright (C) 1997-2023 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

#include "../src/main/amigaos4/SDL_os4debug.h"

#include "../include/SDL_hints.h"

#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/icon.h>
#include <proto/intuition.h>
#include <proto/locale.h>
#include <proto/chooser.h>

#include <intuition/classes.h>
#include <intuition/menuclass.h>

#include <classes/requester.h>
#include <classes/window.h>

#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/chooser.h>
#include <images/label.h>

#include <stdio.h>
#include <string.h>

#define NAME "SDL2 Prefs"
#define VERSION "1.5"
#define MAX_PATH_LEN 1024
#define MAX_VARIABLE_NAME_LEN 32
#define NAME_VERSION_DATE NAME " " VERSION " (" __AMIGADATE__ ")"

static const char* const versingString __attribute__((used)) = "$VER: " NAME_VERSION_DATE;
static const int minVersion = 53;

struct Library *IntuitionBase;
struct Library *IconBase;
struct Library *LocaleBase;

struct IntuitionIFace *IIntuition;
struct IconIFace *IIcon;
struct LocaleIFace *ILocale;

#define CATCOMP_NUMBERS
#define CATCOMP_BLOCK
#define CATCOMP_CODE
#include "sdl2prefs_strings.h"

static struct LocaleInfo localeInfo;

static struct ClassLibrary* WindowBase;
static struct ClassLibrary* RequesterBase;
static struct ClassLibrary* ButtonBase;
static struct ClassLibrary* LayoutBase;
static struct ClassLibrary* LabelBase;

struct Library* ChooserBase;
struct ChooserIFace* IChooser;

static Class* WindowClass;
static Class* RequesterClass;
static Class* ButtonClass;
static Class* LayoutClass;
static Class* LabelClass;
Class* ChooserClass;

enum EGadgetID
{
    GID_DriverList = 1,
    GID_VsyncList,
    GID_BatchingList,
    GID_ScaleQualityList,
    GID_LogicalSizeModeList,
    GID_ScreenSaverList,
    GID_SaveButton,
    GID_ResetButton,
    GID_CancelButton
};

enum EMenuID
{
    MID_Iconify = 1,
    MID_About,
    MID_Quit
};

static struct Window* window;

struct OptionName
{
    const LONG stringId;
    const char* const envName;
    const char* const envNameAlias;
};

static const struct OptionName driverNames[] =
{
    { MSG_DRIVER_DEFAULT, NULL, NULL },
    { MSG_DRIVER_COMPOSITING, "compositing", NULL },
    { MSG_DRIVER_OPENGL, "opengl", NULL },
    { MSG_DRIVER_OPENGLES2, "opengles2", NULL },
    { MSG_DRIVER_SOFTWARE, "software", NULL },
    { -1, NULL, NULL }
};

static const struct OptionName vsyncNames[] =
{
    { MSG_VERTICAL_SYNC_DEFAULT, NULL, NULL },
    { MSG_VERTICAL_SYNC_ENABLED, "1", NULL },
    { MSG_VERTICAL_SYNC_DISABLED, "0", NULL },
    { -1, NULL, NULL }
};

static const struct OptionName batchingNames[] =
{
    { MSG_BATCHING_MODE_DEFAULT, NULL, NULL },
    { MSG_BATCHING_MODE_ENABLED, "1", NULL },
    { MSG_BATCHING_MODE_DISABLED, "0", NULL },
    { -1, NULL, NULL }
};

static const struct OptionName scaleQualityNames[] =
{
    { MSG_SCALE_QUALITY_DEFAULT, NULL, NULL },
    { MSG_SCALE_QUALITY_NEAREST, "0", "nearest" },
    { MSG_SCALE_QUALITY_LINEAR, "1", "linear" },
    { -1, NULL, NULL }
};

static const struct OptionName logicalSizeModeNames[] =
{
    { MSG_LOGICAL_SIZE_MODE_DEFAULT, NULL, NULL },
    { MSG_LOGICAL_SIZE_MODE_LETTERBOX_OR_SIDEBARS, "0", "letterbox" },
    { MSG_LOGICAL_SIZE_MODE_OVERSCAN, "1", "overscan" },
    { -1, NULL, NULL }
};

static const struct OptionName screenSaverNames[] =
{
    { MSG_SCREEN_SAVER_DEFAULT, NULL, NULL },
    { MSG_SCREEN_SAVER_ENABLED, "1", NULL },
    { MSG_SCREEN_SAVER_DISABLED, "0", NULL },
    { -1, NULL, NULL }
};

struct Variable
{
    const enum EGadgetID gid;
    int index;
    const char* const name;
    char value[MAX_VARIABLE_NAME_LEN];
    Object* object;
    struct List* list;
    const struct OptionName* const names;
};

static struct Variable driverVar = { GID_DriverList, 0, SDL_HINT_RENDER_DRIVER, "", NULL, NULL, driverNames };
static struct Variable vsyncVar = { GID_VsyncList, 0, SDL_HINT_RENDER_VSYNC, "", NULL, NULL, vsyncNames };
static struct Variable batchingVar = { GID_BatchingList, 0, SDL_HINT_RENDER_BATCHING, "", NULL, NULL, batchingNames };
static struct Variable scaleQualityVar = { GID_ScaleQualityList, 0, SDL_HINT_RENDER_SCALE_QUALITY, "", NULL, NULL, scaleQualityNames };
static struct Variable logicalSizeModeVar = { GID_LogicalSizeModeList, 0, SDL_HINT_RENDER_LOGICAL_SIZE_MODE, "", NULL, NULL, logicalSizeModeNames };
static struct Variable screenSaverVar = { GID_ScreenSaverList, 0, SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "", NULL, NULL, screenSaverNames };

static char*
GetVariable(const char* const name)
{
    static char buffer[MAX_VARIABLE_NAME_LEN];
    buffer[0] = '\0';

    const int32 len = IDOS->GetVar(name, buffer, sizeof(buffer), GVF_GLOBAL_ONLY);

    if (len > 0) {
        dprintf("value '%s', len %ld\n", buffer, len);
    } else {
        dprintf("len %ld, IoErr %ld\n", len, IDOS->IoErr());
    }

    return buffer;
}

static void
SaveVariable(const char* const name, const char* const value, uint32 control)
{
#ifdef DEBUG
    const int32 success =
#endif
        IDOS->SetVar(name, value, -1, LV_VAR | GVF_GLOBAL_ONLY | control);

    dprintf("name '%s', value '%s', success %ld\n", name, value, success);
}

static void
DeleteVariable(const char* const name, uint32 control)
{
#ifdef DEBUG
    const int32 success =
#endif
        IDOS->DeleteVar(name, LV_VAR | GVF_GLOBAL_ONLY | control);

    dprintf("name '%s', success %d\n", name, success);
}

static void
LoadVariable(struct Variable* var)
{
    const char* const value = GetVariable(var->name);

    snprintf(var->value, sizeof(var->value), "%s", value);
    var->index = 0;

    if (strlen(var->value) > 0) {
        const char* cmp;
        int i = 1;
        while ((cmp = var->names[i].envName)) {
            if (strcmp(var->value, cmp) == 0) {
                dprintf("Value match '%s', index %d\n", cmp, i);
                var->index = i;
                break;
            } else if ((cmp = var->names[i].envNameAlias)) {
                if (strcmp(var->value, cmp) == 0) {
                    dprintf("Alias value match '%s', index %d\n", cmp, i);
                    var->index = i;
                    break;
                }
            }
            i++;
        }
    }
}

static void
LoadVariables()
{
    LoadVariable(&driverVar);
    LoadVariable(&vsyncVar);
    LoadVariable(&batchingVar);
    LoadVariable(&scaleQualityVar);
    LoadVariable(&logicalSizeModeVar);
    LoadVariable(&screenSaverVar);
}

static void
SaveOrDeleteVariable(const struct Variable* const var)
{
    if (var->index > 0) {
        SaveVariable(var->name, var->names[var->index].envName, GVF_SAVE_VAR);
    } else {
        DeleteVariable(var->name, GVF_SAVE_VAR);
    }
}

static void
SaveVariables()
{
    SaveOrDeleteVariable(&driverVar);
    SaveOrDeleteVariable(&vsyncVar);
    SaveOrDeleteVariable(&batchingVar);
    SaveOrDeleteVariable(&scaleQualityVar);
    SaveOrDeleteVariable(&logicalSizeModeVar);
    SaveOrDeleteVariable(&screenSaverVar);
}

static CONST_STRPTR
GetString(LONG stringNum)
{
    return GetStringGenerated(&localeInfo, stringNum);
}

static BOOL
OpenLibs()
{
    IntuitionBase = IExec->OpenLibrary("intuition.library", minVersion);
    if (!IntuitionBase) {
        return FALSE;
    }

    IIntuition = (struct IntuitionIFace *)IExec->GetInterface(IntuitionBase, "main", 1, NULL);
    if (!IIntuition) {
        return FALSE;
    }

    IconBase = IExec->OpenLibrary("icon.library", minVersion);
    if (!IconBase) {
        return FALSE;
    }

    IIcon = (struct IconIFace *)IExec->GetInterface(IconBase, "main", 1, NULL);
    if (!IIcon) {
        return FALSE;
    }

    LocaleBase = IExec->OpenLibrary("locale.library", minVersion);
    ILocale = (struct LocaleIFace *)IExec->GetInterface(LocaleBase, "main", 1, NULL);

    localeInfo.li_Catalog = NULL;
    if (LocaleBase && ILocale) {
        localeInfo.li_ILocale = ILocale;
        localeInfo.li_Catalog = ILocale->OpenCatalog(NULL, "sdl2prefs.catalog",
                                                     OC_BuiltInLanguage, "english",
                                                     OC_PreferExternal, TRUE,
                                                     TAG_END);
    } else {
        printf("Failed to use catalog system. Using built-in strings.\n");
    }

    return TRUE;
}

static void
CloseLibs()
{
    if (ILocale) {
        ILocale->CloseCatalog(localeInfo.li_Catalog);
    }

    IExec->DropInterface((struct Interface *)ILocale);
    IExec->CloseLibrary(LocaleBase);

    IExec->DropInterface((struct Interface *)IIcon);
    IExec->CloseLibrary(IconBase);

    IExec->DropInterface((struct Interface *)IIntuition);
    IExec->CloseLibrary(IntuitionBase);
}

static struct ClassLibrary*
MyOpenClass(const char* const name, Class** class)
{
    struct ClassLibrary* cl = IIntuition->OpenClass(name, minVersion, class);

    if (!cl) {
        dprintf("Failed to open %s", name);
    }

    return cl;
}

static BOOL
OpenClasses()
{
    dprintf("\n");

    WindowBase = MyOpenClass("window.class", &WindowClass);
    RequesterBase = MyOpenClass("requester.class", &RequesterClass);
    ButtonBase = MyOpenClass("gadgets/button.gadget", &ButtonClass);
    LayoutBase = MyOpenClass("gadgets/layout.gadget", &LayoutClass);
    LabelBase = MyOpenClass("images/label.image", &LabelClass);
    ChooserBase = (struct Library *)MyOpenClass("gadgets/chooser.gadget", &ChooserClass);

    IChooser = (struct ChooserIFace *)IExec->GetInterface(ChooserBase, "main", 1, NULL);
    if (!IChooser) {
        dprintf("Failed to get ChooserIFace\n");
    }

    return WindowBase &&
           RequesterBase &&
           ButtonBase &&
           LayoutBase &&
           LabelBase &&
           ChooserBase &&
           IChooser;
}

static void
CloseClasses()
{
    dprintf("\n");

    if (IChooser) {
        IExec->DropInterface((struct Interface *)(IChooser));
    }

    IIntuition->CloseClass(WindowBase);
    IIntuition->CloseClass(RequesterBase);
    IIntuition->CloseClass(ButtonBase);
    IIntuition->CloseClass(LayoutBase);
    IIntuition->CloseClass(LabelBase);
    IIntuition->CloseClass((struct ClassLibrary *)ChooserBase);
}

static struct List*
CreateList()
{
    dprintf("\n");

    struct List* list = (struct List *)IExec->AllocSysObjectTags(ASOT_LIST, TAG_DONE);

    if (!list) {
        dprintf("Failed to allocate list\n");
    }

    return list;
}

static void
AllocChooserNode(struct List* list, const char* const name)
{
    dprintf("%p '%s'\n", list, name);

    struct Node* node = IChooser->AllocChooserNode(CNA_CopyText, TRUE,
                                                   CNA_Text, name,
                                                   TAG_DONE);

    if (!node) {
        dprintf("Failed to allocate chooser node\n");
        return;
    }

    IExec->AddTail(list, node);
}

static void
PurgeChooserList(struct List* list)
{
    dprintf("%p\n", list);

    if (list) {
        struct Node* node;

        while ((node = IExec->RemTail(list))) {
            IChooser->FreeChooserNode(node);
        }

        IExec->FreeSysObject(ASOT_LIST, list);
    }
}

static void
PopulateChooserList(struct Variable * var)
{
    int i = 0;
    LONG id = 0;
    while ((id = var->names[i++].stringId) > -1) {
        const char* name = GetString(id);
        // dprintf("%d : '%s'\n", i, name);

        AllocChooserNode(var->list, name);
    }
}

static Object*
CreateChooserButtons(struct Variable* var, const char* const name, const char* const hint)
{
    dprintf("gid %d, list %p, name '%s', hint '%s'\n", var->gid, var->list, name, hint);

    var->list = CreateList();
    if (!var->list) {
        return NULL;
    }

    PopulateChooserList(var);

    var->object = IIntuition->NewObject(ChooserClass, NULL,
        GA_ID, var->gid,
        GA_RelVerify, TRUE,
        GA_HintInfo, hint,
        CHOOSER_Labels, var->list,
        CHOOSER_Selected, var->index,
        TAG_DONE);

    if (!var->object) {
        dprintf("Failed to create %s buttons\n", name);
    }

    return var->object;
}

static Object*
CreateDriverButtons()
{
    return CreateChooserButtons(&driverVar, "driver", GetString(MSG_DRIVER_HELP));
}

static Object*
CreateVsyncButtons()
{
    return CreateChooserButtons(&vsyncVar, "vsync", GetString(MSG_VERTICAL_SYNC_HELP));
}

static Object*
CreateBatchingButtons()
{
    return CreateChooserButtons(&batchingVar, "batching", GetString(MSG_BATCHING_MODE_HELP));
}

static Object*
CreateScaleQualityButtons()
{
    return CreateChooserButtons(&scaleQualityVar, "scale quality", GetString(MSG_SCALE_QUALITY_HELP));
}

static Object*
CreateLogicalSizeModeButtons()
{
    return CreateChooserButtons(&logicalSizeModeVar, "logical size mode", GetString(MSG_LOGICAL_SIZE_MODE_HELP));
}

static Object*
CreateScreenSaverButtons()
{
    return CreateChooserButtons(&screenSaverVar, "allow screensaver", GetString(MSG_SCREEN_SAVER_HELP));
}

static Object*
CreateButton(enum EGadgetID gid, const char* const name, const char* const hint)
{
    dprintf("gid %d, name '%s', hint '%s'\n", gid, name, hint);

    Object* b = IIntuition->NewObject(ButtonClass, NULL,
        GA_Text, name,
        GA_ID, gid,
        GA_RelVerify, TRUE,
        GA_HintInfo, hint,
        TAG_DONE);

    if (!b) {
        dprintf("Failed to create '%s' button\n", name);
    }

    return b;
}

static Object*
CreateSaveButton()
{
    return CreateButton(GID_SaveButton, GetString(MSG_SAVE_GAD), GetString(MSG_SAVE_HELP));
}

static Object*
CreateResetButton()
{
    return CreateButton(GID_ResetButton, GetString(MSG_RESET_GAD), GetString(MSG_RESET_HELP));
}

static Object*
CreateCancelButton()
{
    return CreateButton(GID_CancelButton, GetString(MSG_CANCEL_GAD), GetString(MSG_CANCEL_HELP));
}

static Object*
CreateLabel(const char* const name)
{
    Object* l = IIntuition->NewObject(LabelClass, NULL,
        LABEL_Text, name,
        TAG_DONE);

    if (!l) {
        dprintf("Failed to create '%s' label\n", name);
    }

    return l;
}

static Object*
CreateRendererLayout()
{
    Object* layout = IIntuition->NewObject(LayoutClass, NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
        LAYOUT_BevelStyle, BVS_GROUP,
        LAYOUT_Label, GetString(MSG_RENDERER_OPTIONS_GROUP),
        LAYOUT_AddChild, IIntuition->NewObject(LayoutClass, NULL,
            LAYOUT_HorizAlignment, LALIGN_CENTER,
            LAYOUT_AddChild, IIntuition->NewObject(LayoutClass, NULL,
                LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
                LAYOUT_AddChild, CreateDriverButtons(),
                CHILD_Label, CreateLabel(GetString(MSG_DRIVER_GAD)),
                LAYOUT_AddChild, CreateVsyncButtons(),
                CHILD_Label, CreateLabel(GetString(MSG_VERTICAL_SYNC_GAD)),
                LAYOUT_AddChild, CreateBatchingButtons(),
                CHILD_Label, CreateLabel(GetString(MSG_BATCHING_MODE_GAD)),
                LAYOUT_AddChild, CreateScaleQualityButtons(),
                CHILD_Label, CreateLabel(GetString(MSG_SCALE_QUALITY_GAD)),
                LAYOUT_AddChild, CreateLogicalSizeModeButtons(),
                CHILD_Label, CreateLabel(GetString(MSG_LOGICAL_SIZE_MODE_GAD)),
                TAG_DONE), // vertical layout
                CHILD_WeightedWidth, 0,
            TAG_DONE),
        TAG_DONE); // horizontal layout

    if (!layout) {
        dprintf("Failed to create renderer layout\n");
    }

    return layout;
}

static Object*
CreateVideoLayout()
{
    Object* layout = IIntuition->NewObject(LayoutClass, NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
        LAYOUT_BevelStyle, BVS_GROUP,
        LAYOUT_Label, GetString(MSG_VIDEO_OPTIONS_GROUP),

        LAYOUT_AddChild, IIntuition->NewObject(LayoutClass, NULL,
            LAYOUT_HorizAlignment, LALIGN_CENTER,
            LAYOUT_AddChild, IIntuition->NewObject(LayoutClass, NULL,
                LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
                LAYOUT_AddChild, CreateScreenSaverButtons(),
                CHILD_Label, CreateLabel(GetString(MSG_SCREEN_SAVER_GAD)),
                TAG_DONE), // vertical layout
                CHILD_WeightedWidth, 0,
            TAG_DONE),
        TAG_DONE); // horizontal layout

    if (!layout) {
        dprintf("Failed to create video layout\n");
    }

    return layout;
}

static Object*
CreateSettingsLayout()
{
    Object* layout = IIntuition->NewObject(LayoutClass, NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
        LAYOUT_BevelStyle, BVS_GROUP,
        LAYOUT_Label, GetString(MSG_SETTINGS_GROUP),
        LAYOUT_AddChild, CreateSaveButton(),
        LAYOUT_AddChild, CreateResetButton(),
        LAYOUT_AddChild, CreateCancelButton(),
        TAG_DONE); // horizontal layout

    if (!layout) {
        dprintf("Failed to create settings layout\n");
    }

    return layout;
}

static Object*
CreateLayout()
{
    dprintf("\n");

    Object* layout = IIntuition->NewObject(LayoutClass, NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
        LAYOUT_AddChild, CreateRendererLayout(),
        LAYOUT_AddChild, CreateVideoLayout(),
        LAYOUT_AddChild, CreateSettingsLayout(),
            CHILD_WeightedHeight, 0,
        TAG_DONE); // vertical main layout

    if (!layout) {
        dprintf("Failed to create layout object\n");
    }

    return layout;
}

static Object*
CreateMenu()
{
    dprintf("\n");

    Object* menuObject = IIntuition->NewObject(NULL, "menuclass",
        MA_Type, T_ROOT,

        // Main
        MA_AddChild, IIntuition->NewObject(NULL, "menuclass",
            MA_Type, T_MENU,
            MA_Label, GetString(MSG_MAIN_MENU),
            MA_AddChild, IIntuition->NewObject(NULL, "menuclass",
                MA_Type, T_ITEM,
                MA_Label, GetString(MSG_MAIN_ICONIFY),
                MA_ID, MID_Iconify,
                TAG_DONE),
            MA_AddChild, IIntuition->NewObject(NULL, "menuclass",
                MA_Type, T_ITEM,
                MA_Label, GetString(MSG_MAIN_ABOUT),
                MA_ID, MID_About,
                TAG_DONE),
            MA_AddChild, IIntuition->NewObject(NULL, "menuclass",
                MA_Type, T_ITEM,
                MA_Separator, TRUE,
                TAG_DONE),
            MA_AddChild, IIntuition->NewObject(NULL, "menuclass",
                MA_Type, T_ITEM,
                MA_Label, GetString(MSG_MAIN_QUIT),
                MA_ID, MID_Quit,
                TAG_DONE),
            TAG_DONE),
         TAG_DONE); // Main

    if (!menuObject) {
        dprintf("Failed to create menu object\n");
    }

    return menuObject;
}

static char*
GetApplicationName()
{
    dprintf("\n");

    static char pathBuffer[MAX_PATH_LEN];

    if (!IDOS->GetCliProgramName(pathBuffer, sizeof(pathBuffer) - 1)) {
        //dprintf("Failed to get CLI program name, checking task node");
        snprintf(pathBuffer, sizeof(pathBuffer), "%s", ((struct Node *)IExec->FindTask(NULL))->ln_Name);
    }

    dprintf("Application name '%s'\n", pathBuffer);

    return pathBuffer;
}

static struct DiskObject*
MyGetDiskObject()
{
    dprintf("\n");

    BPTR oldDir = IDOS->SetCurrentDir(IDOS->GetProgramDir());
    struct DiskObject* diskObject = IIcon->GetDiskObject(GetApplicationName());

    if (diskObject) {
        diskObject->do_CurrentX = NO_ICON_POSITION;
        diskObject->do_CurrentY = NO_ICON_POSITION;
    }

    IDOS->SetCurrentDir(oldDir);

    return diskObject;
}

static Object*
CreateWindow(Object* menuObject, struct MsgPort* appPort)
{
    dprintf("menu %p, appPort %p\n", menuObject, appPort);

    Object* w = IIntuition->NewObject(WindowClass, NULL,
        WA_Activate, TRUE,
        WA_Title, GetString(MSG_APPLICATION_NAME),
        WA_ScreenTitle, GetString(MSG_APPLICATION_NAME),
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_RAWKEY | IDCMP_MENUPICK,
        WA_CloseGadget, TRUE,
        WA_DragBar, TRUE,
        WA_DepthGadget, TRUE,
        WA_SizeGadget, TRUE,
        WA_MenuStrip, menuObject,
        WINDOW_IconifyGadget, TRUE,
        WINDOW_Icon, MyGetDiskObject(),
        WINDOW_AppPort, appPort, // Iconification needs it
        WINDOW_Position, WPOS_CENTERSCREEN,
        WINDOW_Layout, CreateLayout(),
        WINDOW_GadgetHelp, TRUE,
        TAG_DONE);

    if (!w) {
        dprintf("Failed to create window object\n");
    }

    return w;
}

static void
HandleIconify(Object* windowObject)
{
    dprintf("\n");
    window = NULL;
    IIntuition->IDoMethod(windowObject, WM_ICONIFY);
}

static void
HandleUniconify(Object* windowObject)
{
    dprintf("\n");

    window = (struct Window *)IIntuition->IDoMethod(windowObject, WM_OPEN);

    if (!window) {
        dprintf("Failed to reopen window\n");
    }
}

static void
ShowAboutWindow()
{
    char aboutStr[128];

    snprintf(aboutStr, sizeof(aboutStr), "\033b%s %s (%s)\033n\n%s\n%s",
             GetString(MSG_APPLICATION_NAME),
             VERSION,
             __AMIGADATE__,
             GetString(MSG_ABOUT_AUTHOR),
             GetString(MSG_ABOUT_TRANSLATOR));

    dprintf("\n");

    Object* object = IIntuition->NewObject(RequesterClass, NULL,
        REQ_TitleText, GetString(MSG_ABOUT_WINDOW),
        REQ_BodyText, aboutStr,
        REQ_GadgetText, GetString(MSG_OK),
        REQ_Image, REQIMAGE_INFO,
        REQ_TimeOutSecs, 10,
        TAG_DONE);

    if (object) {
        IIntuition->SetWindowPointer(window, WA_BusyPointer, TRUE, TAG_DONE);
        IIntuition->IDoMethod(object, RM_OPENREQ, NULL, window, NULL);
        IIntuition->SetWindowPointer(window, TAG_DONE);
        IIntuition->DisposeObject(object);
    } else {
        dprintf("Failed to create requester object\n");
    }
}

static BOOL
HandleMenuPick(Object* windowObject)
{
    BOOL running = TRUE;

    uint32 id = NO_MENU_ID;

    while (window && (id = IIntuition->IDoMethod((Object *)window->MenuStrip, MM_NEXTSELECT, 0, id)) != NO_MENU_ID) {
        dprintf("menu id %lu\n", id);
        switch(id) {
            case MID_About:
                ShowAboutWindow();
                break;
            case MID_Quit:
                running = FALSE;
                break;
            case MID_Iconify:
                HandleIconify(windowObject);
                break;
            default:
                dprintf("Unknown menu item %d\n", id);
                break;
         }
    }

    return running;
}

static void
ReadSelection(struct Variable* var)
{
    uint32 selected = 0;

    const ULONG result = IIntuition->GetAttrs(var->object, CHOOSER_Selected, &selected, TAG_DONE);

    if (!result) {
        dprintf("Failed to get radiobutton selection\n");
    }

    var->index = selected;
}

static void
ResetSelection(struct Variable* var)
{
    var->index = 0;
    IIntuition->RefreshSetGadgetAttrs((struct Gadget *)var->object, window, NULL, CHOOSER_Selected, 0, TAG_DONE);
}

static BOOL
HandleGadgets(enum EGadgetID gid)
{
    BOOL running = TRUE;

    dprintf("gid %d\n", gid);

    switch (gid) {
        case GID_DriverList:
            ReadSelection(&driverVar);
            break;
        case GID_VsyncList:
            ReadSelection(&vsyncVar);
            break;
        case GID_BatchingList:
            ReadSelection(&batchingVar);
            break;
        case GID_ScaleQualityList:
            ReadSelection(&scaleQualityVar);
            break;
        case GID_LogicalSizeModeList:
            ReadSelection(&logicalSizeModeVar);
            break;
        case GID_ScreenSaverList:
            ReadSelection(&screenSaverVar);
            break;
        case GID_SaveButton:
            SaveVariables();
            running = FALSE;
            break;
        case GID_ResetButton:
            ResetSelection(&driverVar);
            ResetSelection(&vsyncVar);
            ResetSelection(&batchingVar);
            ResetSelection(&scaleQualityVar);
            ResetSelection(&logicalSizeModeVar);
            ResetSelection(&screenSaverVar);
            break;
        case GID_CancelButton:
            running = FALSE;
            break;
        default:
            dprintf("Unhandled gadget %d\n", gid);
            break;
    }

    return running;
}

static BOOL
HandleEvents(Object* windowObject)
{
    BOOL running = TRUE;

    uint32 winSig = 0;
    if (!IIntuition->GetAttr(WINDOW_SigMask, windowObject, &winSig)) {
        dprintf("GetAttr failed\n");
    }

    const ULONG signals = IExec->Wait(winSig | SIGBREAKF_CTRL_C);
    //dprintf("signals %lu\n", signals);
    if (signals & SIGBREAKF_CTRL_C) {
        dprintf("Control-C\n");
        running = FALSE;
    }

    uint32 result;
    int16 code = 0;

    while ((result = IIntuition->IDoMethod(windowObject, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
        switch (result & WMHI_CLASSMASK) {
            case WMHI_CLOSEWINDOW:
                running = FALSE;
                break;
            case WMHI_ICONIFY:
                HandleIconify(windowObject);
                break;
            case WMHI_UNICONIFY:
                HandleUniconify(windowObject);
                break;
            case WMHI_MENUPICK:
                if (!HandleMenuPick(windowObject)) {
                    running = FALSE;
                }
                break;
            case WMHI_GADGETUP:
                if (!HandleGadgets(result & WMHI_GADGETMASK)) {
                    running = FALSE;
                }
                break;
            default:
                //dprintf("Unhandled event result %lx, code %x\n", result, code);
                break;
        }
    }

    return running;
}

int
main(int argc, char** argv)
{
    if (OpenLibs() && OpenClasses()) {
        LoadVariables();

        struct MsgPort* appPort = IExec->AllocSysObjectTags(ASOT_PORT, TAG_DONE);

        Object* menuObject = CreateMenu();
        Object* windowObject = CreateWindow(menuObject, appPort);

        if (windowObject) {
            window = (struct Window *)IIntuition->IDoMethod(windowObject, WM_OPEN);

            if (window) {
                do {
                } while (HandleEvents(windowObject));
            } else {
                dprintf("Failed to open window\n");
            }

            IIntuition->DisposeObject(windowObject);
            windowObject = NULL;
        }

        if (menuObject) {
            IIntuition->DisposeObject(menuObject);
            menuObject = NULL;
        }

        if (appPort) {
            IExec->FreeSysObject(ASOT_PORT, appPort);
            appPort = NULL;
        }

        PurgeChooserList(driverVar.list);
        PurgeChooserList(vsyncVar.list);
        PurgeChooserList(batchingVar.list);
        PurgeChooserList(scaleQualityVar.list);
        PurgeChooserList(logicalSizeModeVar.list);
        PurgeChooserList(screenSaverVar.list);
    }

    CloseClasses();
    CloseLibs();

    return 0;
}

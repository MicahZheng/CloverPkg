/*
 * refit/menu.c
 * Menu functions
 *
 * Copyright (c) 2006 Christoph Pfisterer
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the
 *    distribution.
 *
 *  * Neither the name of Christoph Pfisterer nor the names of the
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <Library/Platform/Platform.h>

#ifndef DEBUG_ALL
#ifndef DEBUG_MENU
#define DEBUG_MENU -1
#endif
#else
#ifdef DEBUG_MENU
#undef DEBUG_MENU
#endif
#define DEBUG_MENU DEBUG_ALL
#endif

#define DBG(...) DebugLog (DEBUG_MENU, __VA_ARGS__)

// scrolling definitions
STATIC INTN   MaxItemOnScreen = -1;

#define SCROLL_LINE_UP                  (0)
#define SCROLL_LINE_DOWN                (1)
#define SCROLL_PAGE_UP                  (2)
#define SCROLL_PAGE_DOWN                (3)
#define SCROLL_FIRST                    (4)
#define SCROLL_LAST                     (5)
#define SCROLL_NONE                     (6)
#define SCROLL_SCROLLBAR_MOVE           (7)

#define SCROLL_MODE_NONE                (0)
#define SCROLL_MODE_LOOP                (1)

#define TEXT_CORNER_REVISION            (1)
#define TEXT_CORNER_HELP                (2)

// other menu definitions

#define MENU_FUNCTION_INIT              (0)
#define MENU_FUNCTION_CLEANUP           (1)
#define MENU_FUNCTION_PAINT_ALL         (2)
#define MENU_FUNCTION_PAINT_SELECTION   (3)
#define MENU_FUNCTION_PAINT_TIMEOUT     (4)

#define LAYOUT_MAINMENU_HEIGHT          (376)
#define TITLEICON_SPACING               (16)

#define TILE_XSPACING                   (8)
#define TILE_YSPACING                   (24)
#define ROW0_SCROLLSIZE                 (100)

typedef VOID (*MENU_STYLE_FUNC)(IN REFIT_MENU_SCREEN *Screen, IN SCROLL_STATE *State, IN UINTN Function, IN CHAR16 *ParamText);

#define CHAR_SPACE  0x0020

STATIC CHAR16 ArrowUp[2]   = { ARROW_UP, 0 };
STATIC CHAR16 ArrowDown[2] = { ARROW_DOWN, 0 };

STATIC EG_IMAGE     *TextBuffer = NULL,
                    *MainImage;

STATIC UINTN        OldRow = 0;
STATIC INTN         row0Count, row0PosX, row0PosXRunning,
                    row1Count, row1PosX, row1PosXRunning,
                    *itemPosX = NULL,
                    //*itemPosY = NULL,
                    row0PosY, row1PosY, textPosY, FunctextPosY,
                    OldX = 0, OldY = 0,
                    OldTextWidth = 0,
                    OldTimeoutTextWidth = 0,
                    MenuWidth, TimeoutPosY,
                    EntriesPosX, EntriesPosY,
                    EntriesWidth, EntriesHeight, EntriesGap;

BOOLEAN             gMainAnime = FALSE, mGuiReady = FALSE;

CHAR16              *gToolPath = NULL;

INTN                gOldChosenTheme, gOldChosenConfig, gOldChosenTool, gOldChosenDSDT;

REFIT_MENU_ENTRY    MenuEntryOptions  = { MENU_ENTRY_ID_GEN, L"Options", TAG_OPTIONS, 1, 0, 'O', NULL, NULL, NULL, NULL, {0, 0, 0, 0}, NULL },
                    MenuEntryAbout    = { MENU_ENTRY_ID_GEN, L"About Clover", TAG_ABOUT, 1, 0, 'I', NULL, NULL, NULL, NULL, {0, 0, 0, 0},  NULL },
                    MenuEntryReset    = { MENU_ENTRY_ID_GEN, L"Restart Computer", TAG_RESET, 1, 0, 'R', NULL, NULL, NULL, NULL, {0, 0, 0, 0},  NULL },
                    //MenuEntryExit     = { MENU_ENTRY_ID_GEN, L"Exit Clover", TAG_EXIT, 1, 0, 'X', NULL, NULL, NULL, NULL, {0, 0, 0, 0},  NULL },
                    gMenuEntryReturn  = { MENU_ENTRY_ID_GEN, L"Return", TAG_RETURN, 0, 0, 0, NULL, NULL, NULL, NULL, {0, 0, 0, 0},  NULL },
                    MenuEntryHelp     = { MENU_ENTRY_ID_GEN, L"Help", TAG_HELP, 1, 0, 'H', NULL, NULL, NULL, NULL, {0, 0, 0, 0},  NULL };

REFIT_MENU_SCREEN   gMainMenu          = { SCREEN_MAIN, L"Main Menu", NULL, 0, NULL, 0, NULL, 0, L"Automatic boot",
                                           NULL, FALSE, FALSE, 0, 0, 0, 0, {0, 0, 0, 0}, NULL },
                    AboutMenu          = { SCREEN_ABOUT, L"About", NULL, 0, NULL, 0, NULL, 0, NULL,
                                           NULL, FALSE, FALSE, 0, 0, 0, 0, {0, 0, 0, 0}, NULL },
                    HelpMenu           = { SCREEN_HELP, L"Help", NULL, 0, NULL, 0, NULL, 0, NULL,
                                           NULL, FALSE, FALSE, 0, 0, 0, 0, {0, 0, 0, 0}, NULL },
                    gOptionMenu        = { SCREEN_OPTIONS, L"Options", NULL, 0, NULL, 0, NULL, 0, NULL,
                                           NULL, FALSE, FALSE, 0, 0, 0, 0, {0, 0, 0, 0}, NULL };

typedef enum {
  mBootArgs,
  mConfigs,
  mThemes,
  mTools,

  // DSDT
  mDSDTs,
  mDSDTFix,

  // Tables
  mDropOEM,

  // Devices
  mInjectNVidia,
  mInjectATI,
  mInjectIntel,
  mInjectEDID,
  mInjectEFIStrings,
  mNoDefaultProperties,
  mLoadVBios,

  // Patch
  mKextPatchesAllowed,
  mKernelPatchesAllowed,
  mBooterPatchesAllowed,
  mKPKernelPm,

  // Debug
  mDebugKP,
  mDebugDSDT,
  mDebugLog,

  // OTHER
  mFlagsBits,
  mOptionsBits,
} OPT_MENU_K;

typedef struct {
  OPT_MENU_K  ID;
  CHAR16      *Title;
} OPT_MENU_GEN;

OPT_MENU_GEN OPT_MENU_DEVICES[] = {
  { mInjectNVidia,          L"Inject NVidia"        },
  { mInjectATI,             L"Inject ATI"           },
  { mInjectIntel,           L"Inject Intel"         },
  { mInjectEDID,            L"Inject EDID"          },
  { mInjectEFIStrings,      L"Inject EFIString"     },
  { mNoDefaultProperties,   L"NoDefaultProperties"  },
  { mLoadVBios,             L"LoadVBios"            }
};

INTN    OptMenuDevicesNum = ARRAY_SIZE (OPT_MENU_DEVICES);

OPT_MENU_GEN OPT_MENU_PATCHES[] = {
  { mKPKernelPm,            L"KernelPM Patch"       }
};

INTN    OptMenuPatchesNum = ARRAY_SIZE (OPT_MENU_PATCHES);

OPT_MENU_GEN OPT_MENU_DSDT[] = {
  { mDSDTs,                 NULL                    },
  { mDSDTFix,               NULL                    }
};

INTN    OptMenuDSDTFixesNum = ARRAY_SIZE (OPT_MENU_DSDT);

OPT_MENU_GEN OPT_MENU_TABLE[] = {
  { mDropOEM,               L"Drop all OEM SSDT"    }
};

INTN    OptMenuTableNum = ARRAY_SIZE (OPT_MENU_TABLE);

OPT_MENU_GEN OPT_MENU_DEBUG[] = {
  { mDebugKP,               L"Kext Patch"           },
  { mDebugDSDT,             L"DSDT"                 },
  { mDebugLog,              L"Log"                  }
};

INTN    OptMenuDebugNum = ARRAY_SIZE (OPT_MENU_DEBUG);

typedef struct {
  OPT_MENU_K  ID;
  CHAR16      *Title;
  CHAR16      *Args;
  UINTN       Bit;
  INTN        OsType;
} OPT_MENU_OPTBIT_K;

OPT_MENU_OPTBIT_K OPT_MENU_OPTBIT[] = {
  { 0,  L"Verbose",       L"-v",          OPT_VERBOSE,        OSTYPE_DARWIN },
  { 0,  L"Single User",   L"-s",          OPT_SINGLE_USER,    OSTYPE_DARWIN },
  { 0,  L"Safe Mode",     L"-x",          OPT_SAFE,           OSTYPE_DARWIN },
  { 0,  L"Quiet",         L"quiet",       OPT_QUIET,          OSTYPE_LIN    },
  { 0,  L"Splash",        L"splash",      OPT_SPLASH,         OSTYPE_LIN    },
  { 0,  L"Nomodeset",     L"nomodeset",   OPT_NOMODESET,      OSTYPE_LIN    },
  { 0,  L"Verbose",       L"-v",          OPT_VERBOSE,        OSTYPE_WIN    },
  { 0,  L"Single User",   L"-s",          OPT_SINGLE_USER,    OSTYPE_WIN    },
  { 0,  L"Hard Disk",     L"-h",          OPT_HDD,            OSTYPE_WIN    },
  { 0,  L"CD-ROM",        L"-c",          OPT_CDROM,          OSTYPE_WIN    }
};

INTN    OptMenuOptBitNum = ARRAY_SIZE (OPT_MENU_OPTBIT);

OPT_MENU_OPTBIT_K OPT_MENU_FLAGBIT[] = {
  { 0,                      L"Without caches",        NULL,   OSFLAG_NOCACHES,              OSTYPE_DARWIN },
  { 0,                      L"With injected kexts",   NULL,   OSFLAG_WITHKEXTS,             OSTYPE_DARWIN },
  { 0,                      L"Disable SIP",           NULL,   OSFLAG_NOSIP,                 OSTYPE_DARWIN },
  { 0,                      L"Debug Patches",         NULL,   OSFLAG_DBGPATCHES,            OSTYPE_DARWIN },
  { mKextPatchesAllowed,    L"Allow Kext Patches",    NULL,   OSFLAG_ALLOW_KEXT_PATCHES,    OSTYPE_DARWIN },
  { mKernelPatchesAllowed,  L"Allow Kernel Patches",  NULL,   OSFLAG_ALLOW_KERNEL_PATCHES,  OSTYPE_DARWIN },
  { mBooterPatchesAllowed,  L"Allow Booter Patches",  NULL,   OSFLAG_ALLOW_BOOTER_PATCHES,  OSTYPE_DARWIN }
};

INTN    OptMenuFlagBitNum = ARRAY_SIZE (OPT_MENU_FLAGBIT);

BOOLEAN     SubInjectNVidia = FALSE,
            SubInjectATI = FALSE,
            SubInjectIntel = FALSE,
            SubInjectX3 = FALSE;

INTN          OptMenuItemsNum = 0;
INPUT_ITEM    *gInputItems = NULL;

//INTN
//GetSubMenuCount () {
//  return SUBMENU_COUNT;
//}

REFIT_MENU_ENTRY *
GenMenu (
  REFIT_MENU_SCREEN     *Screen,
  CHAR16                *Title,
  INPUT_ITEM            *Item,
  INTN                  ItemNum,
  UINTN                 Tag,
  UINTN                 Row
) {
  REFIT_INPUT_DIALOG    *Input = AllocateZeroPool (sizeof (REFIT_INPUT_DIALOG));

  Input->Entry.Title = EfiStrDuplicate (Title);
  Input->Entry.Tag = Tag;
  Input->Entry.Row = Row;
  Input->Item = Item ? Item : &gInputItems[ItemNum];
  Input->Item->ID = ItemNum;

  AddMenuEntry (Screen, (REFIT_MENU_ENTRY *)Input);

  return (REFIT_MENU_ENTRY *)Input;
}

REFIT_MENU_ENTRY *
AddMenuBOOL (
  REFIT_MENU_SCREEN   *Screen,
  CHAR16              *Title,
  INPUT_ITEM          *Item,
  INTN                ItemNum
) {
  return GenMenu (Screen, Title, Item, ItemNum, TAG_INPUT, 0xFFFF);
}

REFIT_MENU_ENTRY *
AddMenuString (
  REFIT_MENU_SCREEN   *Screen,
  CHAR16              *Title,
  INTN                ItemNum
) {
  return GenMenu (Screen, Title, NULL, ItemNum, TAG_INPUT, StrLen (gInputItems[ItemNum].SValue));
}

REFIT_MENU_ENTRY *
AddMenuCheck (
  REFIT_MENU_SCREEN   *Screen,
  CHAR16              *Title,
  UINTN               Bit,
  INTN                ItemNum
) {
  return GenMenu (Screen, Title, NULL, ItemNum, TAG_CHECKBIT, Bit);
}

REFIT_MENU_ENTRY *
AddMenuRadio (
  REFIT_MENU_SCREEN   *Screen,
  CHAR16              *Title,
  INPUT_ITEM          *Item,
  INTN                ItemNum,
  UINTN               Row
) {
  return GenMenu (Screen, Title, Item, ItemNum, TAG_SWITCH, Row);
}

VOID
FillInputRadio (
  UINTN   Index
) {
  gInputItems[Index].ItemType = RadioSwitch;
}

VOID
FillInputInt (
  UINTN   Index,
  UINT32  IValue
) {
  gInputItems[Index].ItemType = CheckBit;
  gInputItems[Index].IValue = IValue;
}

UINT32
ApplyInputInt (
  UINTN   Index
) {
  return gInputItems[Index].IValue;
}

VOID
FillInputBool (
  UINTN     Index,
  BOOLEAN   BValue
) {
  gInputItems[Index].ItemType = BoolValue;
  gInputItems[Index].BValue = BValue;
}

BOOLEAN
ApplyInputBool (
  UINTN   Index
) {
  return gInputItems[Index].BValue;
}

VOID
FillInputString (
  UINTN     Index,
  CHAR16    *Item,
  UINTN     Len,
  UINTN     Type,
  BOOLEAN   New
) {
  gInputItems[Index].ItemType = Type;
  if (New) {
    gInputItems[Index].SValue = AllocateZeroPool (Len * sizeof (CHAR16));
  }

  UnicodeSPrint (gInputItems[Index].SValue, Len, L"%s", Item);
}

//VOID
//ApplyInputString (
//  UINTN     Index,
//  CHAR16    *Item,
//  UINTN     Len
//) {
//  UnicodeSPrint (Item, sizeof (Item), L"%s", gInputItems[Index].SValue);
//}

VOID
AddMenuInfo (
  REFIT_MENU_SCREEN   *SubScreen,
  CHAR16              *Label
) {
  REFIT_INPUT_DIALOG    *Input = AllocateZeroPool (sizeof (REFIT_INPUT_DIALOG));

  Input->Entry.Title = PoolPrint (L"%s", Label);
  Input->Entry.Tag = TAG_INFO;
  Input->Item = NULL;

  AddMenuEntry (SubScreen, (REFIT_MENU_ENTRY *)Input);
}

VOID
AddMenuLabel (
  REFIT_MENU_SCREEN   *SubScreen,
  CHAR16              *Label
) {
  REFIT_INPUT_DIALOG    *Input = AllocateZeroPool (sizeof (REFIT_INPUT_DIALOG));

  Input->Entry.Title = EfiStrDuplicate (Label);
  Input->Entry.Tag = TAG_LABEL;
  Input->Item = NULL;

  AddMenuEntry (SubScreen, (REFIT_MENU_ENTRY *)Input);
}

VOID
FillInputs (
  BOOLEAN   New
) {
  if (New) {
    INTN    i, FlagsOptCount = 0;

    for (i = 0; i < OptMenuFlagBitNum; i++) {
      if (!OPT_MENU_FLAGBIT[i].ID) {
        continue;
      }

      FlagsOptCount++;
    }

    OptMenuItemsNum = (
      1 + // mBootArgs
      1 + // mConfigs
      OptMenuDebugNum +
      OptMenuDevicesNum +
      OptMenuPatchesNum +
      OptMenuDSDTFixesNum +
      OptMenuTableNum +
      gACPIDropTablesNum +
      gACPIUserLoadNum +
      1 + // mThemes
      1 + // mTools
      1 + // mOptionsBits
      1 + // mFlagsBits
      FlagsOptCount + // FlagsBits inside global option
      0
    );

    gInputItems = AllocateZeroPool (OptMenuItemsNum * sizeof (INPUT_ITEM));
  }

  FillInputString (mBootArgs, PoolPrint (L"%a", gSettings.BootArgs), AVALUE_MAX_SIZE, ASString, New);

  FillInputRadio (mConfigs);
  FillInputRadio (mThemes);
  FillInputRadio (mDSDTs);

  // Tools
  FillInputRadio (mTools);

  // Tables
  FillInputBool (mDropOEM, gSettings.DropSSDT);

  // DSDT
  FillInputInt (mDSDTFix, gSettings.FixDsdt);

  // Devices
  FillInputBool (mInjectNVidia, gSettings.InjectNVidia);
  FillInputBool (mInjectATI, gSettings.InjectATI);
  FillInputBool (mInjectIntel, gSettings.InjectIntel);
  FillInputBool (mInjectEDID, gSettings.InjectEDID);
  FillInputBool (mInjectEFIStrings, gSettings.EFIStringInjector);
  FillInputBool (mNoDefaultProperties, gSettings.NoDefaultProperties);
  FillInputBool (mLoadVBios, gSettings.LoadVBios);

  // Patch
  FillInputBool (mKextPatchesAllowed, gSettings.KextPatchesAllowed);
  FillInputBool (mKernelPatchesAllowed, gSettings.KernelPatchesAllowed);
  FillInputBool (mBooterPatchesAllowed, gSettings.BooterPatchesAllowed);
  FillInputBool (mKPKernelPm, gSettings.KernelAndKextPatches.KPKernelPm);

  // Debug
  FillInputBool (mDebugKP, gSettings.DebugKP);
  FillInputBool (mDebugDSDT, gSettings.DebugDSDT);
  FillInputBool (mDebugLog, gSettings.DebugLog);

  // OTHER
  FillInputInt (mOptionsBits, gSettings.OptionsBits);
  FillInputInt (mFlagsBits, gSettings.FlagsBits);
}

VOID
ApplyInputs () {
  EFI_STATUS    Status = EFI_NOT_FOUND;
  BOOLEAN       NeedSave = TRUE;
  CHAR16        *ch;
  CHAR8         *tBootArgs;
  INTN          i = 0;

  for (i = 0; i < OptMenuItemsNum; i++) {
    if (!gInputItems[i].Valid) {
      continue;
    }

    switch (i) {
      case mBootArgs:
        ZeroMem (&gSettings.BootArgs, ARRAY_SIZE (gSettings.BootArgs));
        gBootChanged = TRUE;
        ch = gInputItems[i].SValue;

        do {
          if (*ch == L'\\') {
            *ch = L'_';
          }
        } while (*(++ch));

        AsciiSPrint (gSettings.BootArgs, ARRAY_SIZE (gSettings.BootArgs), "%s", gInputItems[i].SValue);
        tBootArgs = gSettings.BootArgs;
        AsciiTrimSpaces (&tBootArgs);
        gSettings.OptionsBits = EncodeOptions (PoolPrint (L"%a", gSettings.BootArgs));
        break;

      case mConfigs:
        if (gConfigFiles) {
          S_FILES    *aTmp = gConfigFiles;

          while (aTmp) {
            if (
              (StriCmp (gSettings.ConfigName, aTmp->FileName) != 0) &&
              (aTmp->Index == gOldChosenConfig)
            ) {
              TagPtr    Dict;

              gBootChanged = gThemeChanged = TRUE;

              Status = LoadUserSettings (gSelfRootDir, aTmp->FileName, &Dict);
              if (!EFI_ERROR (Status)) {
                Status = GetUserSettings (Dict);

                if (!EFI_ERROR (Status)) {
                  if (gSettings.ConfigName) {
                    FreePool (gSettings.ConfigName);
                  }

                  gSettings.ConfigName = EfiStrDuplicate (aTmp->FileName);
                }
              }

              if (!EFI_ERROR (Status)) {
                DBG ("Main settings custom (%s.plist) from menu: %r\n", aTmp->FileName, Status);

                FillInputs (FALSE);
                NeedSave = FALSE;
              }

              break;
            }

            aTmp = aTmp->Next;
          }
        }
        break;

      case mThemes:
        if (GlobalConfig.Theme) {
          FreePool (GlobalConfig.Theme);
        }

        if (gThemeFiles) {
          S_FILES    *aTmp = gThemeFiles;

          while (aTmp) {
            if (aTmp->Index == gOldChosenTheme) {
              if (GlobalConfig.Theme) {
                FreePool (GlobalConfig.Theme);
              }

              GlobalConfig.Theme = PoolPrint (aTmp->FileName);
              gThemeChanged = TRUE;
              break;
            }

            aTmp = aTmp->Next;
          }
        }
        break;

      case mTools:
        if (gToolFiles) {
          S_FILES    *aTmp = gToolFiles;

          while (aTmp) {
            if (aTmp->Index == gOldChosenTool) {
              if (gToolPath) {
                FreePool (gToolPath);
              }

              gToolPath = PoolPrint (L"%s\\%s.efi", DIR_TOOLS, aTmp->FileName);
              break;
            }

            aTmp = aTmp->Next;
          }
        }
        break;

      // Tables
      case mDropOEM:
        //gSettings.DropSSDT = gInputItems[i].BValue;
        gSettings.DropSSDT = ApplyInputBool (i);
        break;

      case mDSDTs:
        if (gDSDTFiles) {
          S_FILES    *aTmp = gDSDTFiles;

          while (aTmp) {
            if (aTmp->Index == gOldChosenDSDT) {
              StrCpyS (gSettings.DsdtName, sizeof (gSettings.DsdtName), aTmp->FileName);
              break;
            }

            aTmp = aTmp->Next;
          }
        }
        break;

      case mDSDTFix:
        gSettings.FixDsdt = ApplyInputInt (i);
        break;

      // Devices
      case mInjectNVidia:
        gSettings.InjectNVidia = ApplyInputBool (i);
        break;

      case mInjectATI:
        gSettings.InjectATI = ApplyInputBool (i);
        break;

      case mInjectIntel:
        gSettings.InjectIntel = ApplyInputBool (i);
        break;

      case mInjectEDID:
        gSettings.InjectEDID = ApplyInputBool (i);
        break;

      case mInjectEFIStrings:
        gSettings.EFIStringInjector = ApplyInputBool (i);
        break;

      case mNoDefaultProperties:
        gSettings.NoDefaultProperties = ApplyInputBool (i);
        break;

      case mLoadVBios:
        gSettings.LoadVBios = ApplyInputBool (i);
        break;

      // Patch
      case mKPKernelPm:
        gSettings.KernelAndKextPatches.KPKernelPm = ApplyInputBool (i);
        break;

      // Debug
      case mDebugKP:
        gSettings.DebugKP = ApplyInputBool (i);
        break;

      case mDebugDSDT:
        gSettings.DebugDSDT = ApplyInputBool (i);
        break;

      case mDebugLog:
        gSettings.DebugLog = ApplyInputBool (i);
        break;

      // OTHER
      case mOptionsBits:
        gSettings.OptionsBits = ApplyInputInt (i);
        break;

      case mFlagsBits:
        gSettings.FlagsBits = ApplyInputInt (i);
        break;

      default:
        break;
    }
  }

  if (NeedSave) {
    SaveSettings ();
  }
}

//
// menu helper functions
//

UINT32
EncodeOptions (
  CHAR16  *Options
) {
  INTN    i;
  UINT32  OptionsBits = 0;

  for (i = 0; i < OptMenuOptBitNum; i++) {
    if (BootArgsExists (Options, OPT_MENU_OPTBIT[i].Args)) {
      DBG ("- %s\n", OPT_MENU_OPTBIT[i].Title);
      OptionsBits = (UINT32)BIT_SET (OptionsBits, OPT_MENU_OPTBIT[i].Bit);
    }
  }

  return OptionsBits;
}

VOID
DecodeOptions (
  LOADER_ENTRY    *Entry
) {
  INTN      i, OsType;
  BOOLEAN   State;

  if (OSTYPE_IS_DARWIN_GLOB (Entry->LoaderType)) {
    OsType = OSTYPE_DARWIN;
  } else if (OSTYPE_IS_WINDOWS_GLOB (Entry->LoaderType)) {
    OsType = OSTYPE_WIN;
  } else if  (OSTYPE_IS_LINUX_GLOB (Entry->LoaderType)) {
    OsType = OSTYPE_LIN;
  } else {
    return;
  }

  for (i = 0; i < OptMenuOptBitNum; i++) {
    if (OPT_MENU_OPTBIT[i].OsType != OsType) {
      continue;
    }

    State = BIT_ISSET (gSettings.OptionsBits, OPT_MENU_OPTBIT[i].Bit);

    Entry->LoadOptions = ToggleLoadOptions (
                            State,
                            Entry->LoadOptions,
                            OPT_MENU_OPTBIT[i].Args
                          );
  }

  AsciiSPrint (gSettings.BootArgs, ARRAY_SIZE (gSettings.BootArgs) - 1, "%s", Entry->LoadOptions);
}

VOID
AddSeparator (
  REFIT_MENU_SCREEN   *SubScreen,
  CHAR8               *Label
) {
  AddMenuLabel (SubScreen, Label ? PoolPrint (L"** %a:", Label) : L"");
}

VOID
AddMenuInfoLine (
  IN REFIT_MENU_SCREEN  *Screen,
  IN CHAR16             *InfoLine
) {
  AddListElement ((VOID ***)&(Screen->InfoLines), &Screen->InfoLineCount, InfoLine);
}

VOID
AddMenuEntry (
  IN REFIT_MENU_SCREEN  *Screen,
  IN REFIT_MENU_ENTRY   *Entry
) {
  AddListElement ((VOID ***)&(Screen->Entries), &Screen->EntryCount, Entry);
}

VOID
SplitMenuInfo (
  IN REFIT_MENU_SCREEN  *SubScreen,
  IN CHAR16             *Str,
  IN ADD_MENU_INFO      MenuInfo
) {
  CHAR16    *TmpStr;
  UINTN     CurrentLen = 0,
            StringLen = StrLen (Str),
            MaxWidth = LAYOUT_TEXT_WIDTH/*(GlobalConfig.UGAWidth >> 1)*/ / GlobalConfig.CharWidth;

  //DBG ("### CharWidth: %d | MaxWidth: %d | %s\n", GlobalConfig.CharWidth, MaxWidth, Str);

  while (StringLen > CurrentLen) {
    UINTN    TmpLen = MaxWidth, TmpStrLen;

    CONSTRAIN_MAX (TmpLen, (StringLen - CurrentLen));

    if (!TmpLen) {
      break;
    }

    TmpStrLen = TmpLen + 1;
    TmpStr = AllocateZeroPool (TmpStrLen);
    StrnCpyS (TmpStr, TmpStrLen, Str + CurrentLen, TmpLen);
    //DBG ("### %s | Start -> %d | End -> %d\n", TmpStr, CurrentLen, TmpLen);
    MenuInfo (SubScreen, PoolPrint (L"%a%s", CurrentLen ? "  " : "", TmpStr));

    FreePool (TmpStr);

    CurrentLen += TmpLen;
  }
}

VOID
FreeMenu (
  IN REFIT_MENU_SCREEN    *Screen
) {
  UINTN               i;
  REFIT_MENU_ENTRY    *Tentry = NULL;

  //TODO - here we must FreePool for a list of Entries, Screens, Input
  if (Screen->EntryCount > 0) {
    for (i = 0; i < Screen->EntryCount; i++) {
      Tentry = Screen->Entries[i];
      if (Tentry->SubScreen) {
        if (Tentry->SubScreen->Title) {
          FreePool (Tentry->SubScreen->Title);
          Tentry->SubScreen->Title = NULL;
        }

        FreeMenu (Tentry->SubScreen);
        Tentry->SubScreen = NULL;
      }

      if (Tentry->Tag != TAG_RETURN) { //can't free constants
        if (Tentry->Title) {
          FreePool (Tentry->Title);
          Tentry->Title = NULL;
        }
      }

      FreePool (Tentry);
    }

    Screen->EntryCount = 0;
    FreePool (Screen->Entries);
    Screen->Entries = NULL;
  }

  if (Screen->InfoLineCount > 0) {
    for (i = 0; i < Screen->InfoLineCount; i++) {
      // TODO: call a user-provided routine for each element here
      FreePool (Screen->InfoLines[i]);
    }

    Screen->InfoLineCount = 0;
    FreePool (Screen->InfoLines);
    Screen->InfoLines = NULL;
  }
}

STATIC
INTN
FindMenuShortcutEntry (
  IN REFIT_MENU_SCREEN    *Screen,
  IN CHAR16               Shortcut
) {
  UINTN  i;

  if (IS_DIGIT (Shortcut) || IS_ALFA (Shortcut)) {
    for (i = 0; i < Screen->EntryCount; i++) {
      if (
        (Screen->Entries[i]->ShortcutDigit == Shortcut) ||
        (Screen->Entries[i]->ShortcutLetter == Shortcut)
      ) {
        return i;
      }
    }
  }

  return -1;
}

VOID
AddOptionEntries (
  REFIT_MENU_SCREEN   *SubScreen,
  LOADER_ENTRY        *SubEntry
) {
  INTN  i, OsType, FlagsOptCount = 0;

  if (OSTYPE_IS_DARWIN_GLOB (SubEntry->LoaderType)) {
    OsType = OSTYPE_DARWIN;
  } else if (OSTYPE_IS_WINDOWS_GLOB (SubEntry->LoaderType)) {
    OsType = OSTYPE_WIN;
  } else if  (OSTYPE_IS_LINUX_GLOB (SubEntry->LoaderType)) {
    OsType = OSTYPE_LIN;
  } else {
    return;
  }

  for (i = 0; i < OptMenuFlagBitNum; i++) {
    if (OPT_MENU_FLAGBIT[i].OsType != OsType) {
      continue;
    }

    AddMenuCheck (SubScreen, OPT_MENU_FLAGBIT[i].Title, OPT_MENU_FLAGBIT[i].Bit, mFlagsBits);

    gSettings.FlagsBits = (UINT32)(BIT_ISSET (SubEntry->Flags, OPT_MENU_FLAGBIT[i].Bit)
                            ? BIT_SET (gSettings.FlagsBits, OPT_MENU_FLAGBIT[i].Bit)
                            : BIT_UNSET (gSettings.FlagsBits, OPT_MENU_FLAGBIT[i].Bit));

    FlagsOptCount++;
  }

  if (FlagsOptCount) {
    FillInputInt (mFlagsBits, gSettings.FlagsBits);
    AddSeparator (SubScreen, "boot-args");
  }

  for (i = 0; i < OptMenuOptBitNum; i++) {
    if (OPT_MENU_OPTBIT[i].OsType != OsType) {
      continue;
    }

    AddMenuCheck (
      SubScreen,
      PoolPrint (L"%s (%s)", OPT_MENU_OPTBIT[i].Title, OPT_MENU_OPTBIT[i].Args),
      OPT_MENU_OPTBIT[i].Bit,
      mOptionsBits
    );
  }
}

VOID
DrawFuncIcons () {
  //we should never exclude them
  //if (BIT_ISUNSET (GlobalConfig.HideUIFlags, HIDEUI_FLAG_FUNCS)) {
    MenuEntryOptions.Image = BuiltinIcon (BUILTIN_ICON_FUNC_OPTIONS);
    MenuEntryOptions.ImageHover = GetSmallHover (BUILTIN_ICON_FUNC_OPTIONS);
    AddMenuEntry (&gMainMenu, &MenuEntryOptions);
    MenuEntryAbout.Image = BuiltinIcon (BUILTIN_ICON_FUNC_ABOUT);
    MenuEntryAbout.ImageHover = GetSmallHover (BUILTIN_ICON_FUNC_ABOUT);
    AddMenuEntry (&gMainMenu, &MenuEntryAbout);
  //}

  if (BIT_ISUNSET (GlobalConfig.HideUIFlags, HIDEUI_FLAG_FUNCS) || (gMainMenu.EntryCount == 0)) {
    // We are able to include "real" help button in 2nd row, but confuse how to activate it via theme.plist
    // Check BOOLEAN "Help" entry (HIDEUI_FLAG_HELP) currently reserved for help text on bottom corner.
    // Previously HELP is for ABOUT. Same as SHUTDOWN as EXIT.
    if (BIT_ISUNSET (GlobalConfig.HideUIFlags, HIDEUI_FLAG_HELP)) {
      MenuEntryHelp.Image = BuiltinIcon (BUILTIN_ICON_FUNC_HELP);
      MenuEntryHelp.ImageHover = GetSmallHover (BUILTIN_ICON_FUNC_HELP);
      AddMenuEntry (&gMainMenu, &MenuEntryHelp);
    }

    MenuEntryReset.Image = BuiltinIcon (BUILTIN_ICON_FUNC_RESET);
    MenuEntryReset.ImageHover = GetSmallHover (BUILTIN_ICON_FUNC_RESET);
    AddMenuEntry (&gMainMenu, &MenuEntryReset);
    //MenuEntryExit.Image = BuiltinIcon (BUILTIN_ICON_FUNC_EXIT);
    //MenuEntryExit.ImageHover = GetSmallHover (BUILTIN_ICON_FUNC_EXIT);
    //AddMenuEntry (&gMainMenu, &MenuEntryExit);
  }
}

//
// Scrolling functions
//

STATIC
VOID
InitScroll (
  OUT SCROLL_STATE    *State,
  IN  INTN            ItemCount,
  IN  UINTN           MaxCount,
  IN  UINTN           VisibleSpace,
  IN  INTN            Selected // 0xFFFF -> 0: Label, 1: Current
) {
  //ItemCount - a number to scroll (Row0)
  //MaxCount - total number (Row0 + Row1)
  //VisibleSpace - a number to fit
  // DBG ("InitScroll <= %d %d %d\n", ItemCount, MaxCount, VisibleSpace);
  // main menu  <= 2 8 5
  // about menu <= 21 21 14
  State->LastSelection = State->CurrentSelection = Selected;
  //MaxIndex, MaxScroll, MaxVisible are indexes, 0..N-1
  State->MaxIndex = (INTN)MaxCount - 1;
  State->MaxScroll = ItemCount - 1;

  State->MaxVisible = (VisibleSpace == 0) ? State->MaxScroll : (INTN)VisibleSpace - 1;

  if (State->MaxVisible >= ItemCount) {
    State->MaxVisible = ItemCount - 1;
  }

  State->MaxFirstVisible = State->MaxScroll - State->MaxVisible;
  CONSTRAIN_MIN (State->MaxFirstVisible, 0);

  if (Selected == 0xFFFF) {
    State->FirstVisible = 0;
    State->LastSelection = State->CurrentSelection = 1;
  } else {
    State->FirstVisible = MIN (Selected, State->MaxFirstVisible);
  }
  //State->FirstVisible = (Selected > State->MaxFirstVisible) ? State->MaxFirstVisible : Selected;

  State->IsScrolling = (State->MaxFirstVisible > 0);
  State->PaintAll = TRUE;
  State->PaintSelection = FALSE;

  State->LastVisible = State->FirstVisible + State->MaxVisible;
  //  DBG ("InitScroll => MaxIndex=%d, FirstVisible=%d, MaxVisible=%d, MaxFirstVisible=%d\n",
  //      State->MaxIndex, State->FirstVisible, State->MaxVisible, State->MaxFirstVisible);
  // main menu
  // => MaxIndex=7, FirstVisible=0, MaxVisible=1, MaxFirstVisible=0
  //  about
  // => MaxIndex=20, FirstVisible=0, MaxVisible=13, MaxFirstVisible=7
}

STATIC
VOID
UpdateScroll (
  IN OUT  SCROLL_STATE  *State,
  IN      UINTN         Movement
) {
  State->LastSelection = State->CurrentSelection;
  //DBG ("UpdateScroll on %d\n", Movement);

  switch (Movement) {
    case SCROLL_LINE_UP: //of left = decrement
      if (State->CurrentSelection > 0) {
        State->CurrentSelection--;

        if (State->CurrentSelection < State->FirstVisible) {
          State->PaintAll = TRUE;
          State->FirstVisible = State->CurrentSelection;
        }

        if (State->CurrentSelection == State->MaxScroll) {
          State->PaintAll = TRUE;
        }

        if (
          (State->CurrentSelection < State->MaxScroll) &&
          (State->CurrentSelection > State->LastVisible)
        ) {
          State->PaintAll = TRUE;
          State->LastVisible = State->CurrentSelection;
          State->FirstVisible = State->LastVisible - State->MaxVisible;
        }
      } else if (State->ScrollMode == SCROLL_MODE_LOOP) {
        State->CurrentSelection = State->FirstVisible = 0;
        UpdateScroll (State, SCROLL_LAST);
      }
      break;

    case SCROLL_LINE_DOWN: //or right -- increment
      if (State->CurrentSelection < State->MaxIndex) {
        State->CurrentSelection++;

        if (
          (State->CurrentSelection > State->LastVisible) &&
          (State->CurrentSelection <= State->MaxScroll)
        ) {
          State->PaintAll = TRUE;
          State->FirstVisible++;
          CONSTRAIN_MAX (State->FirstVisible, State->MaxFirstVisible);
        }
      } else if (State->ScrollMode == SCROLL_MODE_LOOP) {
        State->CurrentSelection = State->FirstVisible = State->MaxIndex;
        UpdateScroll (State, SCROLL_FIRST);
      }
      break;

    case SCROLL_PAGE_UP:
      if (State->CurrentSelection > 0) {
        if (State->CurrentSelection == State->MaxIndex) { // currently at last entry, special treatment
          if (State->IsScrolling) {
            State->CurrentSelection -= State->MaxVisible - 1; // move to second line without scrolling
          } else {
            State->CurrentSelection = 0; // move to first entry
          }
        } else {
          if (State->FirstVisible > 0) {
            State->PaintAll = TRUE;
          }

          State->CurrentSelection -= State->MaxVisible; // move one page and scroll synchronously
          State->FirstVisible -= State->MaxVisible;
        }

        CONSTRAIN_MIN (State->CurrentSelection, 0);
        CONSTRAIN_MIN (State->FirstVisible, 0);

        if (State->CurrentSelection < State->FirstVisible) {
          State->PaintAll = TRUE;
          State->FirstVisible = State->CurrentSelection;
        }
      }
      break;

    case SCROLL_PAGE_DOWN:
      if (State->CurrentSelection < State->MaxIndex) {
        if (State->CurrentSelection == 0) { // currently at first entry, special treatment
          if (State->IsScrolling) {
            State->CurrentSelection += State->MaxVisible - 1; // move to second-to-last line without scrolling
          } else {
            State->CurrentSelection = State->MaxIndex; // move to last entry
          }
        } else {
          if (State->FirstVisible < State->MaxFirstVisible) {
            State->PaintAll = TRUE;
          }

          State->CurrentSelection += State->MaxVisible; // move one page and scroll synchronously
          State->FirstVisible += State->MaxVisible;
        }

        CONSTRAIN_MAX (State->CurrentSelection, State->MaxIndex);
        CONSTRAIN_MAX (State->FirstVisible, State->MaxFirstVisible);

        if (
          (State->CurrentSelection > State->LastVisible) &&
          (State->CurrentSelection <= State->MaxScroll)
        ) {
          State->PaintAll = TRUE;
          State->FirstVisible += State->MaxVisible;
          CONSTRAIN_MAX (State->FirstVisible, State->MaxFirstVisible);
        }
      }
      break;

    case SCROLL_FIRST:
      if (State->CurrentSelection > 0) {
        State->CurrentSelection = 0;

        if (State->FirstVisible > 0) {
          State->PaintAll = TRUE;
          State->FirstVisible = 0;
        }
      }
      break;

    case SCROLL_LAST:
      if (State->CurrentSelection < State->MaxIndex) {
        State->CurrentSelection = State->MaxIndex;

        if (State->FirstVisible < State->MaxFirstVisible) {
          State->PaintAll = TRUE;
          State->FirstVisible = State->MaxFirstVisible;
        }
      }
      break;

    case SCROLL_NONE:
      // The caller has already updated CurrentSelection, but we may
      // have to scroll to make it visible.
      if (State->CurrentSelection < State->FirstVisible) {
        State->PaintAll = TRUE;
        State->FirstVisible = State->CurrentSelection; // - (State->MaxVisible >> 1);
        CONSTRAIN_MIN (State->FirstVisible, 0);
      } else if (
        (State->CurrentSelection > State->LastVisible) &&
        (State->CurrentSelection <= State->MaxScroll)
      ) {
        State->PaintAll = TRUE;
        State->FirstVisible = State->CurrentSelection - State->MaxVisible;
        CONSTRAIN_MAX (State->FirstVisible, State->MaxFirstVisible);
      }
      break;
  }

  if (!State->PaintAll && (State->CurrentSelection != State->LastSelection)) {
    State->PaintSelection = TRUE;
  }

  State->LastVisible = State->FirstVisible + State->MaxVisible;
}

//
// generic input menu function
// usr-sse2
//
STATIC
UINTN
InputDialog (
  IN REFIT_MENU_SCREEN    *Screen,
  IN MENU_STYLE_FUNC      StyleFunc,
  IN SCROLL_STATE         *State
) {
  EFI_STATUS        Status;
  EFI_INPUT_KEY     Key;
  UINTN             Ind = 0, i = 0, MenuExit = 0,
                    Pos = (Screen->Entries[State->CurrentSelection])->Row,
                    BackupPos, BackupShift;
  INPUT_ITEM        *Item = ((REFIT_INPUT_DIALOG *)(Screen->Entries[State->CurrentSelection]))->Item;
  CHAR16            *Backup = EfiStrDuplicate (Item->SValue), *Buffer;

  /*
    I would like to see a LineSize that depends on the Title width and the menu width so
    the edit dialog does not extend beyond the menu width.
    There are 3 cases:
    1) Text menu where MenuWidth is min of gConWidth - 6 and max of 50 and all StrLen (Title)
    2) Graphics menu where MenuWidth is measured in pixels and font is fixed width.
       The following works well in my case but depends on font width and minimum screen size.
         LineSize = 76 - StrLen (Screen->Entries[State->CurrentSelection]->Title);
    3) Graphics menu where font is proportional. In this case LineSize would depend on the
       current width of the displayed string which would need to be recalculated after
       every change.
    Anyway, the above will not be implemented for now, and LineSize will remain at 38
    because it works.
  */

  UINTN             LineSize = 48;//38
#define DBG_INPUTDIALOG 0
#if DBG_INPUTDIALOG
  UINTN             Iteration = 0;
#endif

  if (
    (Item->ItemType != BoolValue) &&
    (Item->ItemType != RadioSwitch) &&
    (Item->ItemType != CheckBit)
  ) {
    // Grow Item->SValue to SVALUE_MAX_SIZE if we want to edit a text field
    Item->SValue = EfiReallocatePool (Item->SValue, StrSize (Item->SValue), SVALUE_MAX_SIZE);
  }

  Buffer = Item->SValue;
  BackupShift = Item->LineShift;
  BackupPos = Pos;

  do {
    if (Item->ItemType == BoolValue) {
      Item->BValue = !Item->BValue;
      MenuExit = MENU_EXIT_ENTER;
    } else if (Item->ItemType == RadioSwitch) {
      switch (Item->ID) {
        case mThemes:
          gOldChosenTheme = Pos;
          break;

        case mConfigs:
          gOldChosenConfig = Pos;
          break;

        case mTools:
          gOldChosenTool = Pos;
          break;

        case mDSDTs:
          gOldChosenDSDT = Pos;
          break;
      }

      MenuExit = MENU_EXIT_ENTER;
    } else if (Item->ItemType == CheckBit) {
      Item->IValue ^= Pos;
      MenuExit = MENU_EXIT_ENTER;
    } else {
      Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);

#if 0
\#if DBG_INPUTDIALOG
      // For debugging the InputDialog
      PrintAt (0, 0, L"%5d: Buffer:%x MaxSize:%d Line:%3d", Iteration, Buffer, SVALUE_MAX_SIZE, LineSize);
      PrintAt (0, 1, L"%5d: Size:%3d Len:%3d", Iteration, StrSize (Buffer), StrLen (Buffer));
      PrintAt (0, 2, L"%5d: Pos:%3d Shift:%3d AbsPos:%3d", Iteration, Pos, Item->LineShift, Pos + Item->LineShift);
      PrintAt (0, 3, L"%5d: KeyCode:%4d KeyChar:%4d", Iteration, Key.ScanCode, (UINTN)Key.UnicodeChar);
      PrintAt (0, 4, L"%5d: Title:\"%s\"", Iteration, Screen->Entries[State->CurrentSelection]->Title);
      Iteration++;
#endif

      if (Status == EFI_NOT_READY) {
        gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &Ind);
        continue;
      }

      switch (Key.ScanCode) {
        case SCAN_RIGHT:
          if (Pos + Item->LineShift < StrLen (Buffer)) {
            if (Pos < LineSize) {
              Pos++;
            } else {
              Item->LineShift++;
            }
          }
          break;

        case SCAN_LEFT:
          if (Pos > 0) {
            Pos--;
          } else if (Item->LineShift > 0) {
            Item->LineShift--;
          }
          break;

        case SCAN_HOME:
          Pos = 0;
          Item->LineShift = 0;
          break;

        case SCAN_END:
          if (StrLen (Buffer) < LineSize) {
            Pos = StrLen (Buffer);
          } else {
            Pos = LineSize;
            Item->LineShift = StrLen (Buffer) - LineSize;
          }
          break;

        case SCAN_ESC:
          MenuExit = MENU_EXIT_ESCAPE;
          //continue;
          break;

        case SCAN_F2:
          SaveBooterLog (gSelfRootDir, PREBOOT_LOG);
          break;

        case SCAN_F6:
          /* Status = */ SaveFile (gSelfRootDir, VBIOS_BIN, (UINT8 *)(UINTN)0xc0000, 0x20000);
          break;

        case SCAN_F10:
          ScreenShot ();
          break;

        case SCAN_DELETE:
          // forward delete
          {
            UINTN   Len = StrLen (Buffer),
                    NewPos = Pos + Item->LineShift;

            if (NewPos < Len) {
              for (i = NewPos; i < Len; i++) {
                Buffer[i] = Buffer[i + 1];
              }
              /*
              // Commented this out because it looks weird - Forward Delete should not
              // affect anything left of the cursor even if it's just to shift more of the
              // string into view.
              if (Item->LineShift > 0 && Item->LineShift + LineSize > StrLen (Buffer)) {
                Item->LineShift--;
                Pos++;
              }
              */
            }
          }
          break;
      }

      switch (Key.UnicodeChar) {
        case CHAR_BACKSPACE:
          if ((Buffer[0] != CHAR_NULL) && (Pos != 0)) {
            UINTN   Len = StrLen (Buffer),
                    NewPos = Pos + Item->LineShift;

            for (i = NewPos; i <= Len; i++) {
              Buffer[i - 1] = Buffer[i];
            }

            Item->LineShift > 0 ? Item->LineShift-- : Pos--;
          }
          break;

        case CHAR_LINEFEED:
        case CHAR_CARRIAGE_RETURN:
          MenuExit = MENU_EXIT_ENTER;
          Pos = 0;
          Item->LineShift = 0;
          break;

        default:
          if (
            (Key.UnicodeChar >= 0x20) &&
            (Key.UnicodeChar < 0x80)
          ) {
            if (StrSize (Buffer) < SVALUE_MAX_SIZE) {
              UINTN   Len = StrLen (Buffer) + 1,
                      NewPos = Pos + Item->LineShift;

              for (i = Len; i > NewPos; i--) {
                Buffer[i] = Buffer[i - 1];
              }

              Buffer[i] = Key.UnicodeChar;
              Pos < LineSize ? Pos++ : Item->LineShift++;
            }
          }
          break;
      }
    }

    // Redraw the field
    (Screen->Entries[State->CurrentSelection])->Row = Pos;

    StyleFunc (Screen, State, MENU_FUNCTION_PAINT_SELECTION, NULL);
  } while (!MenuExit);

  switch (MenuExit) {
    case MENU_EXIT_ENTER:
      Item->Valid = TRUE;
      ApplyInputs ();
      if (gToolPath) {
        return MenuExit;
      }
      break;

    case MENU_EXIT_ESCAPE:
      if (StrCmp (Item->SValue, Backup) != 0) {
        UnicodeSPrint (Item->SValue, SVALUE_MAX_SIZE, L"%s", Backup);

        if (Item->ItemType != BoolValue) {
          Item->LineShift = BackupShift;
          (Screen->Entries[State->CurrentSelection])->Row = BackupPos;
        }

        StyleFunc (Screen, State, MENU_FUNCTION_PAINT_SELECTION, NULL);
      }
      break;
  }

  Item->Valid = FALSE;
  FreePool (Backup);

  if (Item->SValue) {
    DBG ("EDITED: %s\n", Item->SValue);
  }

  return 0;
}

VOID
CheckState (
  REFIT_MENU_SCREEN   *Screen,
  SCROLL_STATE        *State,
  UINT16              ScanCode
) {
  INTN      Index = 0;
  BOOLEAN   Allow = FALSE, Down = FALSE;

  switch (ScanCode) {
    case SCAN_UP:
      if (
        (State->ScrollMode != SCROLL_MODE_LOOP) &&
        (Screen->ID == SCREEN_MAIN) &&
        (State->CurrentSelection >= State->InitialRow1)
      ) {
        if (State->MaxIndex > State->InitialRow1) {
          State->CurrentSelection = State->FirstVisible + (State->LastVisible - State->FirstVisible) *
                                    (State->CurrentSelection - State->InitialRow1) /
                                    (State->MaxIndex - State->InitialRow1);
        }

        State->PaintAll = TRUE;
        break;
      }
    case SCAN_LEFT:
      Index = State->CurrentSelection - 1;
      CONSTRAIN_MAX (Index, State->MaxIndex);
      Allow = (Index >= 0);
      break;

    case SCAN_DOWN:
      if (
        (State->ScrollMode != SCROLL_MODE_LOOP) &&
        (Screen->ID == SCREEN_MAIN) &&
        (State->CurrentSelection <= State->InitialRow1)
      ) {
        if (State->LastVisible > State->FirstVisible) {
          State->CurrentSelection = State->InitialRow1 + (State->MaxIndex - State->InitialRow1) *
                                    (State->CurrentSelection - State->FirstVisible) /
                                    (State->LastVisible - State->FirstVisible);
        }

        State->PaintAll = TRUE;
        break;
      }
    case SCAN_RIGHT:
      Index = State->CurrentSelection + 1;
      CONSTRAIN_MAX (Index, State->MaxIndex);
      Allow = (Index < State->MaxIndex);
      Down = TRUE;
      break;

    default:
      return;
  }

  if (
    Down &&
    (
      (State->CurrentSelection == State->MaxIndex) &&
      ((Screen->Entries[0])->Tag == TAG_LABEL)
    )
  ) {
    State->CurrentSelection = State->FirstVisible = 0;
    State->PaintAll = TRUE;
  } else if (((Screen->Entries[Index])->Tag == TAG_LABEL) && Allow) {
    if ((Index == (State->LastVisible + 1)) && Down) {
      State->FirstVisible++;
      #if 0
      DBG ("\n\n - [#%d] CurrentSelection=%d, LastSelection=%d, MaxScroll=%d, MaxIndex=%d, \
            FirstVisible=%d, LastVisible=%d, MaxVisible=%d, MaxFirstVisible=%d\n\n",
          Index, State->CurrentSelection, State->LastSelection, State->MaxScroll, State->MaxIndex,
            State->FirstVisible, State->LastVisible, State->MaxVisible, State->MaxFirstVisible
        );
      #endif
    }

    State->CurrentSelection = Index;
    State->PaintAll = TRUE;
  }
}

// Identify the end of row 0 and the beginning of row 1; store the results in the
// appropriate fields in State. Also reduce MaxVisible if that value is greater
// than the total number of row-0 tags and if we're in an icon-based screen
STATIC
VOID
IdentifyRows (
  IN SCROLL_STATE       *State,
  IN REFIT_MENU_SCREEN  *Screen
) {
  INTN i;

  State->FinalRow0 = 0;
  State->InitialRow1 = State->MaxIndex;

  for (i = 0; i <= State->MaxIndex; i++) {
    if (Screen->Entries[i]->Row == 0) {
      State->FinalRow0 = i;
    } else if ((Screen->Entries[i]->Row == 1) && (State->InitialRow1 > i)) {
      State->InitialRow1 = i;
    }
  }
}

UINTN
RunGenericMenu (
  IN  REFIT_MENU_SCREEN    *Screen,
  IN  MENU_STYLE_FUNC      StyleFunc,
  IN  OUT INTN             *DefaultEntryIndex,
  OUT REFIT_MENU_ENTRY     **ChosenEntry
) {
  SCROLL_STATE                        State;
  EFI_STATUS                          Status;
  INTN                                ShortcutEntry, TimeoutCountdown = 0;
  BOOLEAN                             HaveTimeout = FALSE;
  CHAR16                              *TimeoutMessage, CurrChar = 0;
  UINTN                               MenuExit, CurrentSelectionTag, EventIndex;
  EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL   *SimpleTextInEx;
  EFI_KEY_DATA                        KeyData;
  BOOLEAN                             AltKeyPressed = FALSE, CtrlKeyPressed = FALSE /* , CtrlAltKeyPressed = FALSE */;

  //no default - no timeout!
  if (
    (*DefaultEntryIndex != -1) &&
    (Screen->TimeoutSeconds > 0)
  ) {
    //DBG ("have timeout\n");
    HaveTimeout = TRUE;
    TimeoutCountdown = Screen->TimeoutSeconds;
  }

  MenuExit = 0;

  StyleFunc (Screen, &State, MENU_FUNCTION_INIT, NULL);
  //  DBG ("scroll inited\n");
  // override the starting selection with the default index, if any
  if (
    (*DefaultEntryIndex >= 0) &&
    (*DefaultEntryIndex <= State.MaxIndex)
  ) {
    State.CurrentSelection = *DefaultEntryIndex;
    UpdateScroll (&State, SCROLL_NONE);
  }

  //DBG ("RunGenericMenu CurrentSelection=%d MenuExit=%d\n",
  //    State.CurrentSelection, MenuExit);

  State.ScrollMode = (gSettings.TextOnly || (Screen->ID > SCREEN_MAIN)) ? SCROLL_MODE_LOOP : SCROLL_MODE_NONE;

  if (State.ScrollMode == SCROLL_MODE_NONE) {
    IdentifyRows (&State, Screen);
  }

  Status = gBS->LocateProtocol (
                  &gEfiSimpleTextInputExProtocolGuid,
                  NULL,
                  (VOID **)&SimpleTextInEx
                );

  if (!SimpleTextInEx) {
    return MenuExit;
  }

  // exhaust key buffer and be sure no key is pressed to prevent option selection
  // when coming with a key press from timeout=0, for example
  //while (ReadAllKeyStrokes ()) gBS->Stall (500 * 1000);

  while (!MenuExit) {
    // update the screen
    if (State.PaintAll) {
      StyleFunc (Screen, &State, MENU_FUNCTION_PAINT_ALL, NULL);
      State.PaintAll = FALSE;
    } else if (State.PaintSelection) {
      StyleFunc (Screen, &State, MENU_FUNCTION_PAINT_SELECTION, NULL);
      State.PaintSelection = FALSE;
    }

    if (HaveTimeout) {
      TimeoutMessage = PoolPrint (L"%s in %d seconds", Screen->TimeoutText, TimeoutCountdown);
      StyleFunc (Screen, &State, MENU_FUNCTION_PAINT_TIMEOUT, TimeoutMessage);
      FreePool (TimeoutMessage);
    }

    /*
    if (gEvent) { //for now used at CD eject.
      MenuExit = MENU_EXIT_ESCAPE;
      State.PaintAll = TRUE;
      gEvent = 0; //to prevent looping
      break;
    }
    */

    if (!mGuiReady) {
      mGuiReady = TRUE;
      MsgLog ("GUI ready\n");
    }

    Status = WaitForInputEventPoll (Screen, 1); //wait for 1 seconds.

    if (Status == EFI_TIMEOUT) {
      if (HaveTimeout) {
        if (TimeoutCountdown <= 0) {
          // timeout expired
          MenuExit = MENU_EXIT_TIMEOUT;
          break;
        } else {
          //gBS->Stall (100000);
          TimeoutCountdown--;
        }
      }

      continue;
    }

    Status = SimpleTextInEx->ReadKeyStrokeEx (SimpleTextInEx, &KeyData);
    if (Status == EFI_NOT_READY) {
      gBS->WaitForEvent (1, &SimpleTextInEx->WaitForKeyEx, &EventIndex);
      continue;
    }

    AltKeyPressed = (
      (KeyData.KeyState.KeyShiftState == (EFI_SHIFT_STATE_VALID | EFI_LEFT_ALT_PRESSED)) ||
      (KeyData.KeyState.KeyShiftState == (EFI_SHIFT_STATE_VALID | EFI_RIGHT_ALT_PRESSED))
    );

    CtrlKeyPressed = (
      (KeyData.KeyState.KeyShiftState == (EFI_SHIFT_STATE_VALID | EFI_LEFT_CONTROL_PRESSED)) ||
      (KeyData.KeyState.KeyShiftState == (EFI_SHIFT_STATE_VALID | EFI_RIGHT_CONTROL_PRESSED))
    );

    if (HaveTimeout) {
      // the user pressed a key, cancel the timeout
      StyleFunc (Screen, &State, MENU_FUNCTION_PAINT_TIMEOUT, L"");
      HaveTimeout = FALSE;
    }

    if (KeyData.Key.ScanCode != SCAN_NULL) {
      // react to key press
      switch (KeyData.Key.ScanCode) {
        case SCAN_UP:
        case SCAN_LEFT:
          CheckState (Screen, &State, KeyData.Key.ScanCode);
          UpdateScroll (&State, SCROLL_LINE_UP);
          break;

        case SCAN_DOWN:
        case SCAN_RIGHT:
          CheckState (Screen, &State, KeyData.Key.ScanCode);
          UpdateScroll (&State, SCROLL_LINE_DOWN);
          break;

        case SCAN_HOME:
          UpdateScroll (&State, SCROLL_FIRST);
          break;

        case SCAN_END:
          UpdateScroll (&State, SCROLL_LAST);
          break;

        case SCAN_PAGE_UP:
          UpdateScroll (&State, SCROLL_PAGE_UP);
          StyleFunc (Screen, &State, MENU_FUNCTION_INIT, NULL);
          break;

        case SCAN_PAGE_DOWN:
          UpdateScroll (&State, SCROLL_PAGE_DOWN);
          StyleFunc (Screen, &State, MENU_FUNCTION_INIT, NULL);
          break;

        case SCAN_ESC:
          MenuExit = MENU_EXIT_ESCAPE;
          break;

        case SCAN_INSERT:
          MenuExit = MENU_EXIT_OPTIONS;
          break;

        case SCAN_F1:
          MenuExit = MENU_EXIT_HELP;
          break;

        case SCAN_F2:
          /* Status = */ SaveBooterLog (gSelfRootDir, PREBOOT_LOG);
          break;

        case SCAN_F3:
           MenuExit = MENU_EXIT_HIDE_TOGGLE;
           break;

        case SCAN_F4:
          SaveOemTables ();
          break;

        case SCAN_F5:
          SaveOemDsdt (TRUE, 0); //full patch
          break;

        case SCAN_F6:
          /* Status = */ SaveFile (gSelfRootDir, VBIOS_BIN, (UINT8 *)(UINTN)0xc0000, 0x20000);
          //if (EFI_ERROR (Status)) {
          //  Status = SaveFile (NULL, VBIOS_BIN, (UINT8 *)(UINTN)0xc0000, 0x20000);
          //}
          break;

        /* just a sample code
        case SCAN_F7:
          Status = MkDir (gSelfRootDir,  L"EFI\\CLOVER\\new_folder");
          DBG ("create folder %r\n", Status);
          if (!EFI_ERROR (Status)) {
            Status = SaveFile (gSelfRootDir,  L"EFI\\CLOVER\\new_folder\\new_file.txt", (UINT8 *)SomeText, sizeof (*SomeText) + 1);
            DBG ("create file %r\n", Status);
          }
          break;
        */

        case SCAN_F8:
          //do {
          //  CHAR16 *Str = PoolPrint (L"%s\n%s\n%s", L"ABC", L"123456", L"xy");
          //  if (Str != NULL) {
          //    AlertMessage (L"Sample message", Str);
          //    FreePool (Str);
          //  }
          //} while (0);
          //this way screen is dirty
          //hehe2 ();
          break;

        case SCAN_F9:
          SetNextScreenMode (1);
          MenuExit = MENU_EXIT_ESCAPE;
          break;

        case SCAN_F10:
          ScreenShot ();
          break;

        //case SCAN_F12:
        //  MenuExit = MENU_EXIT_EJECT;
        //  State.PaintAll = TRUE;
        //  break;
      }
    } else {
      CurrentSelectionTag = (Screen->Entries[State.CurrentSelection])->Tag;

      CurrChar = TO_UPPER (KeyData.Key.UnicodeChar);

      if (CtrlKeyPressed) {
        switch (CurrChar) {
          case 'R':
            ResetClover ();
            (Screen->Entries[State.CurrentSelection])->Tag = TAG_RESET;
            MenuExit = MENU_EXIT_OTHER;
            break;
        }
      }

      if (!MenuExit && (CurrentSelectionTag == TAG_LOADER)) {
        if (AltKeyPressed) {
          switch (CurrChar) {
            case 'D':
              gSettings.DebugKP = TRUE;
              MenuExit = MENU_EXIT_ENTER;
              break;
            case 'S':
              gSettings.OptionsBits = BIT_SET (gSettings.OptionsBits, OPT_SINGLE_USER);
              MenuExit = MENU_EXIT_ENTER;
              break;
            case 'V':
              gSettings.OptionsBits = BIT_SET (gSettings.OptionsBits, OPT_VERBOSE);
              MenuExit = MENU_EXIT_ENTER;
              break;
          }
        } else if (CtrlKeyPressed) {
          //
        } else {
          //
        }
      }

      if (!MenuExit && !CtrlKeyPressed && !AltKeyPressed) {
        switch (CurrChar) {
          case CHAR_LINEFEED:
          case CHAR_CARRIAGE_RETURN:
          case CHAR_SPACE:
            if (
              (CurrentSelectionTag == TAG_INPUT) ||
              (CurrentSelectionTag == TAG_CHECKBIT)
            ) {
              MenuExit = InputDialog (Screen, StyleFunc, &State);
            } else if (CurrentSelectionTag == TAG_SWITCH) {
              MenuExit = InputDialog (Screen, StyleFunc, &State);
              State.PaintAll = TRUE;
            } else {
              MenuExit = (CurrChar == CHAR_SPACE) ? MENU_EXIT_DETAILS : MENU_EXIT_ENTER;
            }
            break;

          default:
            ShortcutEntry = FindMenuShortcutEntry (Screen, CurrChar);
            if (ShortcutEntry >= 0) {
              State.CurrentSelection = ShortcutEntry;
              MenuExit = MENU_EXIT_ENTER;
            }
            break;
        }
      }
    }
  }

  StyleFunc (Screen, &State, MENU_FUNCTION_CLEANUP, NULL);

  if (ChosenEntry) {
    *ChosenEntry = Screen->Entries[State.CurrentSelection];
  }

  *DefaultEntryIndex = State.CurrentSelection;

  return MenuExit;
}

//
// text-mode generic style
//

STATIC
VOID
TextMenuStyle (
  IN REFIT_MENU_SCREEN    *Screen,
  IN SCROLL_STATE         *State,
  IN UINTN                Function,
  IN CHAR16               *ParamText
) {
          CHAR16    *TimeoutMessage, ResultString[SVALUE_MAX_SIZE];
  STATIC  UINTN     ThisMenuPosY = 0, TextMenuWidth = 0;
          UINTN     ItemWidth = 0, MenuHeight = 0, iSwitch = 0,
                    ResultStringLen = ARRAY_SIZE (ResultString);
          INTN      i = 0, j = 0;

  switch (Function) {
    case MENU_FUNCTION_INIT:
      // vertical layout
      ThisMenuPosY = 4;

      if (Screen->InfoLineCount > 0) {
        ThisMenuPosY += Screen->InfoLineCount + 1;
      }

      MenuHeight = gConHeight - ThisMenuPosY;

      if (Screen->TimeoutSeconds > 0) {
        MenuHeight -= 2;
      }

      switch (Screen->Entries[0]->Tag) {
        case TAG_SWITCH:
          switch (((REFIT_INPUT_DIALOG *)(Screen->Entries[0]))->Item->ID) {
            case mThemes:
              j = gOldChosenTheme;
              break;

            case mConfigs:
              j = gOldChosenConfig;
              break;

            case mTools:
              j = gOldChosenTool;
              break;

            case mDSDTs:
              j = gOldChosenDSDT;
              break;
          }
          break;

        case TAG_LABEL:
          j = 0xFFFF;
          break;
      }

      InitScroll (State, Screen->EntryCount, Screen->EntryCount, MenuHeight, j);

      // determine width of the menu
      TextMenuWidth = 50;  // minimum

      for (i = 0; i <= State->MaxIndex; i++) {
        ItemWidth = StrLen (Screen->Entries[i]->Title);

        if (TextMenuWidth < ItemWidth) {
          TextMenuWidth = ItemWidth;
        }
      }

      if (TextMenuWidth > (gConWidth - 6)) {
        TextMenuWidth = gConWidth - 6;
      }
      break;

    case MENU_FUNCTION_PAINT_ALL:
      // paint the whole screen (initially and after scrolling)

      BeginTextScreen (Screen->Title);

      if (Screen->InfoLineCount > 0) {
        gST->ConOut->SetAttribute (gST->ConOut, ATTR_BASIC);

        for (i = 0; i < (INTN)Screen->InfoLineCount; i++) {
          gST->ConOut->SetCursorPosition (gST->ConOut, 3, 4 + i);
          gST->ConOut->OutputString (gST->ConOut, Screen->InfoLines[i]);
        }
      }

      for (i = State->FirstVisible; i <= State->LastVisible && i <= State->MaxIndex; i++) {
        REFIT_MENU_ENTRY    *Entry = Screen->Entries[i];

        gST->ConOut->SetCursorPosition (gST->ConOut, 2, ThisMenuPosY + (i - State->FirstVisible));
        if (i == State->CurrentSelection) {
          gST->ConOut->SetAttribute (gST->ConOut, ATTR_CHOICE_CURRENT);
        } else {
          gST->ConOut->SetAttribute (gST->ConOut, ATTR_CHOICE_BASIC);
        }

        ZeroMem (ResultString, ResultStringLen);

        switch (Entry->Tag) {
          case TAG_INPUT:
            if (((REFIT_INPUT_DIALOG *)Entry)->Item->ItemType == BoolValue) {
              UnicodeSPrint (ResultString, ResultStringLen, L"%s %s", ((REFIT_INPUT_DIALOG *)Entry)->Item->BValue ? L"[+]" : L"[ ]", Entry->Title);
            } else {
              UnicodeSPrint (ResultString, ResultStringLen, L"%s: %s ", Entry->Title, ((REFIT_INPUT_DIALOG *)Entry)->Item->SValue);
            }
            break;

          case TAG_SWITCH:
            switch (((REFIT_INPUT_DIALOG *)Entry)->Item->ID) {
              case mThemes:
                iSwitch = (UINTN)gOldChosenTheme;
                break;

              case mConfigs:
                iSwitch = (UINTN)gOldChosenConfig;
                break;

              case mTools:
                iSwitch = (UINTN)gOldChosenTool;
                break;

              case mDSDTs:
                iSwitch = (UINTN)gOldChosenDSDT;
                break;
            }

            UnicodeSPrint (ResultString, ResultStringLen, L"%s %s", (Entry->Row == iSwitch) ? L"(*)" : L"( )", Entry->Title);
            break;

          case TAG_CHECKBIT:
            UnicodeSPrint (ResultString, ResultStringLen, L"%s %s", BIT_ISSET (((REFIT_INPUT_DIALOG *)Entry)->Item->IValue, Entry->Row) ? L"[+]" : L"[ ]", Entry->Title);
            break;

          default:
            StrnCpyS (ResultString, ResultStringLen, Entry->Title, StrSize (Entry->Title));
            break;
        }

        for (j = StrLen (ResultString); j < (INTN)TextMenuWidth; j++) {
          ResultString[j] = L' ';
        }

        ResultString[j] = 0;
        gST->ConOut->OutputString (gST->ConOut, ResultString);
      }

      // scrolling indicators
      gST->ConOut->SetAttribute (gST->ConOut, ATTR_SCROLLARROW);
      gST->ConOut->SetCursorPosition (gST->ConOut, 0, ThisMenuPosY);

      if (State->FirstVisible > 0) {
        gST->ConOut->OutputString (gST->ConOut, ArrowUp);
      } else {
        gST->ConOut->OutputString (gST->ConOut, L" ");
      }

      gST->ConOut->SetCursorPosition (gST->ConOut, 0, ThisMenuPosY + State->MaxVisible);

      if (State->LastVisible < State->MaxIndex) {
        gST->ConOut->OutputString (gST->ConOut, ArrowDown);
      } else {
        gST->ConOut->OutputString (gST->ConOut, L" ");
      }
      break;

    case MENU_FUNCTION_PAINT_SELECTION: {
      REFIT_MENU_ENTRY    *EntryL = Screen->Entries[State->LastSelection],
                          *EntryC = Screen->Entries[State->CurrentSelection];

      // redraw selection cursor
      gST->ConOut->SetCursorPosition (gST->ConOut, 2, ThisMenuPosY + (State->LastSelection - State->FirstVisible));
      gST->ConOut->SetAttribute (gST->ConOut, ATTR_CHOICE_BASIC);

      ZeroMem (ResultString, ResultStringLen);

      switch (EntryL->Tag) {
        case TAG_INPUT:
          if (((REFIT_INPUT_DIALOG *)EntryL)->Item->ItemType == BoolValue) {
            UnicodeSPrint (ResultString, ResultStringLen, L"%s %s", ((REFIT_INPUT_DIALOG *)EntryL)->Item->BValue ? L"[+]" : L"[ ]", EntryL->Title);
          } else {
            UnicodeSPrint (ResultString, ResultStringLen, L"%s: %s ", EntryL->Title, ((REFIT_INPUT_DIALOG *)EntryL)->Item->SValue);
          }
          break;

        case TAG_SWITCH:
          switch (((REFIT_INPUT_DIALOG *)EntryL)->Item->ID) {
            case mThemes:
              iSwitch = (UINTN)gOldChosenTheme;
              break;

            case mConfigs:
              iSwitch = (UINTN)gOldChosenConfig;
              break;

            case mTools:
              iSwitch = (UINTN)gOldChosenTool;
              break;

            case mDSDTs:
              iSwitch = (UINTN)gOldChosenDSDT;
              break;
          }

          UnicodeSPrint (ResultString, ResultStringLen, L"%s %s", (EntryL->Row == iSwitch) ? L"(*)" : L"( )", EntryL->Title);
          break;

        case TAG_CHECKBIT:
          UnicodeSPrint (ResultString, ResultStringLen, L"%s %s", BIT_ISSET (((REFIT_INPUT_DIALOG *)EntryL)->Item->IValue, EntryL->Row) ? L"[+]" : L"[ ]", EntryL->Title);
          break;

        default:
          StrnCpyS (ResultString, ResultStringLen, EntryL->Title, StrSize (EntryL->Title));
          break;
      }

      for (j = StrLen (ResultString); j < (INTN)TextMenuWidth; j++) {
        ResultString[j] = L' ';
      }

      ResultString[j] = 0;
      gST->ConOut->OutputString (gST->ConOut, ResultString);

      // Current selection ///////////////////

      ZeroMem (ResultString, ResultStringLen);

      gST->ConOut->SetCursorPosition (gST->ConOut, 2, ThisMenuPosY + (State->CurrentSelection - State->FirstVisible));
      gST->ConOut->SetAttribute (gST->ConOut, ATTR_CHOICE_CURRENT);

      switch (EntryC->Tag) {
        case TAG_INPUT:
          if (((REFIT_INPUT_DIALOG *)EntryC)->Item->ItemType == BoolValue) {
            UnicodeSPrint (ResultString, ResultStringLen, L"%s %s", ((REFIT_INPUT_DIALOG *)EntryC)->Item->BValue ? L"[+]" : L"[ ]", EntryC->Title);
          } else {
            UnicodeSPrint (ResultString, ResultStringLen, L"%s: %s ", EntryC->Title, ((REFIT_INPUT_DIALOG *)EntryC)->Item->SValue);
          }
          break;

        case TAG_SWITCH:
          switch (((REFIT_INPUT_DIALOG *)EntryC)->Item->ID) {
            case mThemes:
              iSwitch = (UINTN)gOldChosenTheme;
              break;

            case mConfigs:
              iSwitch = (UINTN)gOldChosenConfig;
              break;

            case mTools:
              iSwitch = (UINTN)gOldChosenTool;
              break;

            case mDSDTs:
              iSwitch = (UINTN)gOldChosenDSDT;
              break;
          }

          UnicodeSPrint (ResultString, ResultStringLen, L"%s %s", (EntryC->Row == iSwitch) ? L"(*)" : L"( )", EntryC->Title);
          break;

        case TAG_CHECKBIT:
          UnicodeSPrint (ResultString, ResultStringLen, L"%s %s", BIT_ISSET (((REFIT_INPUT_DIALOG *)EntryC)->Item->IValue, EntryC->Row) ? L"[+]" : L"[ ]", EntryC->Title);
          break;

        default:
          StrnCpyS (ResultString, ResultStringLen, EntryC->Title, StrSize (EntryC->Title));
          break;
      }

      for (j = StrLen (ResultString); j < (INTN)TextMenuWidth; j++) {
        ResultString[j] = L' ';
      }

      ResultString[j] = 0;
      gST->ConOut->OutputString (gST->ConOut, ResultString);

      break;
    }

    case MENU_FUNCTION_PAINT_TIMEOUT:
      if (ParamText[0] == 0) {
        // clear message
        gST->ConOut->SetAttribute (gST->ConOut, ATTR_BASIC);
        gST->ConOut->SetCursorPosition (gST->ConOut, 0, gConHeight - 1);
        gST->ConOut->OutputString (gST->ConOut, gBlankLine + 1);
      } else {
        // paint or update message
        gST->ConOut->SetAttribute (gST->ConOut, ATTR_ERROR);
        gST->ConOut->SetCursorPosition (gST->ConOut, 3, gConHeight - 1);
        TimeoutMessage = PoolPrint (L"%s  ", ParamText);
        gST->ConOut->OutputString (gST->ConOut, TimeoutMessage);
        FreePool (TimeoutMessage);
      }
      break;

    case MENU_FUNCTION_CLEANUP:
      // release temporary memory
      break;
  }

  gST->ConOut->SetAttribute (gST->ConOut, ATTR_CHOICE_BASIC);
}

//
// graphical generic style
//

INTN
DrawTextXY (
  IN CHAR16   *Text,
  IN INTN     XPos,
  IN INTN     YPos,
  IN UINT8    XAlign,
  IN INTN     ClearWidth
) {
  INTN        TextWidth = 0, XText = 0;
  EG_IMAGE    *TextBufferXY = NULL;

  if (!Text) {
    return 0;
  }

  MeasureText (Text, &TextWidth, NULL);

  if (XAlign == X_IS_LEFT) {
    TextWidth = GlobalConfig.UGAWidth - XPos - 1;
    XText = XPos;
  }

  TextBufferXY = CreateFilledImage (ClearWidth ? ClearWidth : TextWidth, gTextHeight, TRUE, &gTransparentBackgroundPixel);
  // render the text
  TextWidth = RenderText (Text, TextBufferXY, 0, 0, 0xFFFF, FALSE);

  if (XAlign != X_IS_LEFT) { // shift 64 is prohibited
    XText = XPos - (TextWidth >> XAlign);
  }

  BltImageAlpha (TextBufferXY, XText, YPos,  &gTransparentBackgroundPixel, 16);
  FreeImage (TextBufferXY);

  return TextWidth;
}

VOID
DrawBCSText (
  IN CHAR16     *Text,
  IN INTN       XPos,
  IN INTN       YPos,
  IN UINT8      XAlign
) {
  INTN        ChrsNum = 12, Ellipsis = 3, TextWidth = 0, XText = 0, i = 0;
  EG_IMAGE    *TextBufferXY = NULL;
  CHAR16      *BCSText = NULL;

  if (!Text) {
    return;
  }

  if (GlobalConfig.TileXSpace >= 25 && GlobalConfig.TileXSpace < 30) {
    ChrsNum = 13;
  } else if ((GlobalConfig.TileXSpace >= 30) && (GlobalConfig.TileXSpace < 35)) {
    ChrsNum = 14;
  } else if ((GlobalConfig.TileXSpace >= 35) && (GlobalConfig.TileXSpace < 40)) {
    ChrsNum = 15;
  } else if ((GlobalConfig.TileXSpace >= 40) && (GlobalConfig.TileXSpace < 45)) {
    ChrsNum = 16;
  } else if ((GlobalConfig.TileXSpace >= 45) && (GlobalConfig.TileXSpace < 50)) {
    ChrsNum = 17;
  } else if ((GlobalConfig.TileXSpace >= 50) && (GlobalConfig.TileXSpace < 55)) {
    ChrsNum = 18;
  }

  TextWidth = (((INTN) StrLen (Text) <= (ChrsNum - Ellipsis)) ? (INTN) StrLen (Text) : ChrsNum) *
              ((gFontWidth > GlobalConfig.CharWidth) ? gFontWidth : GlobalConfig.CharWidth);

  // render the text

  TextBufferXY = CreateFilledImage (TextWidth, gFontHeight, TRUE, &gTransparentBackgroundPixel);

  if (XAlign == X_IS_LEFT) {
    TextWidth = GlobalConfig.UGAWidth - XPos - 1;
    XText = XPos;
  }

  if ((INTN) StrLen (Text) > (ChrsNum - Ellipsis)) {
    BCSText = AllocatePool (sizeof (CHAR16) * ChrsNum);

    for (i = 0; i < ChrsNum; i++) {
      BCSText[i] = (i < (ChrsNum - Ellipsis)) ? Text[i] : L'.';
    }

    BCSText[ChrsNum] = '\0';

    if (!BCSText) {
      return;
    }

    TextWidth = RenderText (BCSText, TextBufferXY, 0, 0, 0xFFFF, FALSE);

    FreePool (BCSText);
  } else {
    TextWidth = RenderText (Text, TextBufferXY, 0, 0, 0xFFFF, FALSE);
  }

  if (XAlign != X_IS_LEFT) { // shift 64 is prohibited
    XText = XPos - (TextWidth >> XAlign);
  }

  BltImageAlpha (TextBufferXY, XText, YPos,  &gTransparentBackgroundPixel, 16);
  FreeImage (TextBufferXY);
}

VOID
DrawMenuText (
  IN CHAR16     *Text,
  IN INTN       SelectedWidth,
  IN INTN       XPos,
  IN INTN       YPos,
  IN INTN       Cursor,
  IN EG_IMAGE   *Button
) {
  INTN    PlaceCentre = (gTextHeight / 2) - 7;

  //use Text=null to reinit the buffer
  if (!Text) {
    if (TextBuffer) {
      FreeImage (TextBuffer);
      TextBuffer = NULL;
    }

    return;
  }

  if (TextBuffer && (TextBuffer->Height != gTextHeight)) {
    FreeImage (TextBuffer);
    TextBuffer = NULL;
  }

  if (TextBuffer == NULL) {
    TextBuffer = CreateImage (GlobalConfig.UGAWidth - XPos, gTextHeight, TRUE);
  }

  FillImage (TextBuffer, &gTransparentBackgroundPixel);

  if (SelectedWidth > 0) {
    // draw selection bar background
    FillImageArea (TextBuffer, 0, 0, SelectedWidth, TextBuffer->Height, &gSelectionBackgroundPixel);
  }

  // render the text
  RenderText (Text, TextBuffer, TEXT_XMARGIN, TEXT_YMARGIN, Cursor, (SelectedWidth > 0));

  if (Button != NULL) {
    ComposeImage (TextBuffer, Button, TEXT_XMARGIN, PlaceCentre);
  }

  BltImageAlpha (TextBuffer, XPos, YPos, &gTransparentBackgroundPixel, 16);
}

VOID
InitSelection () {
  if (!GlobalConfig.AllowGraphicsMode) {
    return;
  }

  gSelectionBackgroundPixel = ToPixel (GlobalConfig.SelectionColor);

  InitUISelection ();
}

//
// graphical main menu style
//

VOID
GraphicsMenuStyle (
  IN REFIT_MENU_SCREEN    *Screen,
  IN SCROLL_STATE         *State,
  IN UINTN                Function,
  IN CHAR16               *ParamText
) {
  INTN        i, j = 0, ItemWidth = 0, X, t1, t2, VisibleHeight = 0; //assume vertical layout
  UINTN       TitleLen, iSwitch = 0;
  BOOLEAN     NeedMarginLeft;
  CHAR16      ResultString[SVALUE_MAX_SIZE];

  switch (Function) {
    case MENU_FUNCTION_INIT:
      GetScreenSize (&GlobalConfig.UGAWidth, &GlobalConfig.UGAHeight);
      InitAnime (Screen);
      SwitchToGraphicsAndClear ();

      EntriesPosY = ((GlobalConfig.UGAHeight - LAYOUT_TOTAL_HEIGHT) >> 1) + GlobalConfig.LayoutBannerOffset + (gTextHeight << 1);

      VisibleHeight = ((GlobalConfig.UGAHeight - EntriesPosY) / gTextHeight) - Screen->InfoLineCount;

      if (VisibleHeight > 2) {
        VisibleHeight -= 2;
      }

      if ((VisibleHeight - GlobalConfig.PruneScrollRows) > 2) {
        VisibleHeight -= GlobalConfig.PruneScrollRows;
      }

      switch (Screen->Entries[0]->Tag) {
        case TAG_SWITCH:
          switch (((REFIT_INPUT_DIALOG *)(Screen->Entries[0]))->Item->ID) {
            case mThemes:
              j = gOldChosenTheme;
              break;

            case mConfigs:
              j = gOldChosenConfig;
              break;

            case mTools:
              j = gOldChosenTool;
              break;

            case mDSDTs:
              j = gOldChosenDSDT;
              break;
          }
          break;

        case TAG_LABEL:
          j = 0xFFFF;
          break;
      }

      InitScroll (State, Screen->EntryCount, Screen->EntryCount, VisibleHeight, j);
      // determine width of the menu -- not working
      //MenuWidth = 80;  // minimum
      MenuWidth = LAYOUT_TEXT_WIDTH; //500
      DrawMenuText (NULL, 0, 0, 0, 0, NULL);

      if (Screen->TitleImage) {
        if (MenuWidth > (INTN)(GlobalConfig.UGAWidth - TITLEICON_SPACING - Screen->TitleImage->Width)) {
          MenuWidth = GlobalConfig.UGAWidth - TITLEICON_SPACING - Screen->TitleImage->Width - 2;
        }

        EntriesPosX = (GlobalConfig.UGAWidth - (Screen->TitleImage->Width + TITLEICON_SPACING + MenuWidth)) >> 1;
        //DBG ("GlobalConfig.UGAWIdth=%d TitleImage=%d MenuWidth=%d\n", GlobalConfig.UGAWidth,
        //Screen->TitleImage->Width, MenuWidth);
        MenuWidth += Screen->TitleImage->Width;
      } else {
        EntriesPosX = (GlobalConfig.UGAWidth - MenuWidth) >> 1;
      }

      TimeoutPosY = EntriesPosY + (Screen->EntryCount + 1) * gTextHeight;

      // initial painting
      MeasureText (Screen->Title, &ItemWidth, NULL);

      if (BIT_ISUNSET (GlobalConfig.HideUIFlags, HIDEUI_FLAG_MENU_TITLE)) {
        DrawTextXY (Screen->Title, (GlobalConfig.UGAWidth >> 1), EntriesPosY - gTextHeight * 2, X_IS_CENTER, 0);
      }

      if (Screen->TitleImage) {
        INTN    FilmXPos = (INTN)(EntriesPosX - (Screen->TitleImage->Width + TITLEICON_SPACING)),
                FilmYPos = (INTN)EntriesPosY;

        BltImageAlpha (Screen->TitleImage, FilmXPos, FilmYPos, &gTransparentBackgroundPixel, 16);

        // Update FilmPlace only if not set by InitAnime
        if ((Screen->FilmPlace.Width == 0) || (Screen->FilmPlace.Height == 0)) {
          Screen->FilmPlace.XPos = FilmXPos;
          Screen->FilmPlace.YPos = FilmYPos;
          Screen->FilmPlace.Width = Screen->TitleImage->Width;
          Screen->FilmPlace.Height = Screen->TitleImage->Height;
        }
      }

      if (Screen->InfoLineCount > 0) {
        DrawMenuText (NULL, 0, 0, 0, 0, NULL);

        for (i = 0; i < (INTN)Screen->InfoLineCount; i++) {
          DrawMenuText (Screen->InfoLines[i], 0, EntriesPosX, EntriesPosY, 0xFFFF, NULL);
          EntriesPosY += gTextHeight;
        }

        EntriesPosY += gTextHeight;  // also add a blank line
      }

      InitBar ();
      break;

    case MENU_FUNCTION_PAINT_ALL:
      DrawMenuText (NULL, 0, 0, 0, 0, NULL); //should clean every line to avoid artefacts
      //DBG ("PAINT_ALL: EntriesPosY=%d MaxVisible=%d\n", EntriesPosY, State->MaxVisible);
      //DBG ("DownButton.Height=%d gTextHeight=%d\n", DownButton.Height, gTextHeight);
      t1 = EntriesPosX + gTextHeight + TEXT_XMARGIN + MenuWidth + 16;
      t2 = EntriesPosY + (State->MaxVisible + 1) * gTextHeight - gDownButton.Height;
      //DBG ("PAINT_ALL: %d %d\n", t1, t2);
      SetBar (t1, EntriesPosY, t2, State);

      // blackosx swapped this around so drawing of selection comes before drawing scrollbar.

      for (i = State->FirstVisible, j = 0; i <= State->LastVisible; i++, j++) {
        REFIT_MENU_ENTRY  *Entry = Screen->Entries[i];

        NeedMarginLeft = (
          (Entry->Tag == TAG_SWITCH) || (Entry->Tag == TAG_CHECKBIT) ||
          ((Entry->Tag == TAG_INPUT) && (((REFIT_INPUT_DIALOG *)Entry)->Item->ItemType == BoolValue))
        );

        ZeroMem (ResultString, SVALUE_MAX_SIZE);
        StrCatS (ResultString, SVALUE_MAX_SIZE, PoolPrint (L"%a%s", NeedMarginLeft ? "   " : "", Entry->Title));

        TitleLen = StrLen (ResultString) + (NeedMarginLeft ? 0 : 2);

        Entry->Place.XPos = EntriesPosX;
        Entry->Place.YPos = EntriesPosY + j * gTextHeight;
        Entry->Place.Width = TitleLen * GlobalConfig.CharWidth;
        Entry->Place.Height = (UINTN)gTextHeight;

        switch (Entry->Tag) {
          case TAG_INPUT:
            if (((REFIT_INPUT_DIALOG *)Entry)->Item->ItemType == BoolValue) {
              Entry->Place.Width = StrLen (ResultString) * GlobalConfig.CharWidth;
              DrawMenuText (
                ResultString,
                (i == State->CurrentSelection) ? MenuWidth : 0,
                EntriesPosX,
                Entry->Place.YPos,
                0xFFFF,
                gButtonsImg[((REFIT_INPUT_DIALOG *)(Entry))->Item->BValue ? kCheckboxCheckedImage : kCheckboxImage].Image
              );
            } else { //text input
              StrCatS (ResultString, SVALUE_MAX_SIZE, PoolPrint (L": %s ", ((REFIT_INPUT_DIALOG *)(Entry))->Item->SValue));

              if (StrLen (ResultString) >= (TitleLen + 48)) {
                ResultString[TitleLen + 48] = '\0';
              }

              Entry->Place.Width = StrLen (ResultString) * GlobalConfig.CharWidth;
              // Slice - suppose to use Row as Cursor in text
              DrawMenuText (
                ResultString,
                (i == State->CurrentSelection) ? MenuWidth : 0,
                EntriesPosX,
                Entry->Place.YPos,
                TitleLen + Entry->Row,
                NULL
              );
            }
            break;

          case TAG_SWITCH:
            switch (((REFIT_INPUT_DIALOG *)(Entry))->Item->ID) {
              case mThemes:
                iSwitch = (UINTN)gOldChosenTheme;
                break;

              case mConfigs:
                iSwitch = (UINTN)gOldChosenConfig;
                break;

              case mTools:
                iSwitch = (UINTN)gOldChosenTool;
                break;

              case mDSDTs:
                iSwitch = (UINTN)gOldChosenDSDT;
                break;
            }

            DrawMenuText (
              ResultString,
              (i == State->CurrentSelection) ? MenuWidth : 0,
              EntriesPosX,
              Entry->Place.YPos,
              0xFFFF,
              gButtonsImg[(Entry->Row == iSwitch) ? kRadioSelectedImage : kRadioImage].Image
            );
            break;

          case TAG_CHECKBIT:
            DrawMenuText (
              ResultString,
              (i == State->CurrentSelection) ? MenuWidth : 0,
              EntriesPosX,
              Entry->Place.YPos,
              0xFFFF,
              gButtonsImg[BIT_ISSET (((REFIT_INPUT_DIALOG *)(Entry))->Item->IValue, Entry->Row) ? kCheckboxCheckedImage : kCheckboxImage].Image
            );
            break;

          default:
            //DBG ("paint entry %d title=%s\n", i, Screen->Entries[i]->Title);
            DrawMenuText (
              ResultString,
              (i == State->CurrentSelection) ? MenuWidth : 0,
              EntriesPosX,
              Entry->Place.YPos,
              0xFFFF,
              NULL
            );
            break;
        }
      }

      ScrollingBar (State);
      break;

    case MENU_FUNCTION_PAINT_SELECTION: {
      REFIT_MENU_ENTRY    *EntryL = Screen->Entries[State->LastSelection],
                          *EntryC = Screen->Entries[State->CurrentSelection];

      NeedMarginLeft = (
        (EntryL->Tag == TAG_SWITCH) || (EntryL->Tag == TAG_CHECKBIT) ||
        ((EntryL->Tag == TAG_INPUT) && (((REFIT_INPUT_DIALOG *)EntryL)->Item->ItemType == BoolValue))
      );

      ZeroMem (ResultString, SVALUE_MAX_SIZE);
      StrCatS (ResultString, SVALUE_MAX_SIZE, PoolPrint (L"%a%s", NeedMarginLeft ? "   " : "", EntryL->Title));

      TitleLen = StrLen (ResultString) + (NeedMarginLeft ? 0 : 2);

      // blackosx swapped this around so drawing of selection comes before drawing scrollbar.

      // redraw selection cursor
      //usr-sse2
      switch (EntryL->Tag) {
        case TAG_INPUT:
          if (((REFIT_INPUT_DIALOG *)EntryL)->Item->ItemType == BoolValue) {
            DrawMenuText (
              ResultString,
              0,
              EntriesPosX,
              EntriesPosY + (State->LastSelection - State->FirstVisible) * gTextHeight,
              0xFFFF,
              gButtonsImg[((REFIT_INPUT_DIALOG *)EntryL)->Item->BValue ? kCheckboxCheckedImage : kCheckboxImage].Image
            );
          } else {
            StrCatS (ResultString, SVALUE_MAX_SIZE, PoolPrint (L": %s ", ((REFIT_INPUT_DIALOG *)(EntryL))->Item->SValue));

            if (StrLen (ResultString) >= (TitleLen + 48)) {
              ResultString[TitleLen + 48] = '\0';
            }

            DrawMenuText (
              ResultString,
              0,
              EntriesPosX,
              EntriesPosY + (State->LastSelection - State->FirstVisible) * gTextHeight,
              TitleLen + EntryL->Row,
              NULL
            );
          }
          break;

        case TAG_SWITCH:
          switch (((REFIT_INPUT_DIALOG *)(EntryL))->Item->ID) {
            case mThemes:
              iSwitch = (UINTN)gOldChosenTheme;
              break;

            case mConfigs:
              iSwitch = (UINTN)gOldChosenConfig;
              break;

            case mTools:
              iSwitch = (UINTN)gOldChosenTool;
              break;

            case mDSDTs:
              iSwitch = (UINTN)gOldChosenDSDT;
              break;
          }

          DrawMenuText (
            ResultString,
            0,
            EntriesPosX,
            EntriesPosY + (State->LastSelection - State->FirstVisible) * gTextHeight,
            0xFFFF,
            gButtonsImg[(EntryL->Row == iSwitch) ? kRadioSelectedImage : kRadioImage].Image
          );
          break;

        case TAG_CHECKBIT:
          DrawMenuText (
            ResultString,
            0,
            EntriesPosX,
            EntryL->Place.YPos,
            0xFFFF,
            gButtonsImg[BIT_ISSET (((REFIT_INPUT_DIALOG *)EntryL)->Item->IValue, EntryL->Row) ? kCheckboxCheckedImage : kCheckboxImage].Image
          );
          break;

        default:
          DrawMenuText (
            ResultString,
            0,
            EntriesPosX,
            EntriesPosY + (State->LastSelection - State->FirstVisible) * gTextHeight,
            0xFFFF,
            NULL
          );
          break;
      }

      // Current selection ///////////////////

      NeedMarginLeft = (
        (EntryC->Tag == TAG_SWITCH) || (EntryC->Tag == TAG_CHECKBIT) ||
        ((EntryC->Tag == TAG_INPUT) && (((REFIT_INPUT_DIALOG *)EntryC)->Item->ItemType == BoolValue))
      );

      ZeroMem (ResultString, SVALUE_MAX_SIZE);
      StrCatS (ResultString, SVALUE_MAX_SIZE, PoolPrint (L"%a%s", NeedMarginLeft ? "   " : "", EntryC->Title));

      TitleLen = StrLen (ResultString) + (NeedMarginLeft ? 0 : 2);

      switch (EntryC->Tag) {
        case TAG_INPUT:
          if (((REFIT_INPUT_DIALOG *)EntryC)->Item->ItemType == BoolValue) {
            DrawMenuText (
              ResultString,
              MenuWidth,
              EntriesPosX,
              EntriesPosY + (State->CurrentSelection - State->FirstVisible) * gTextHeight,
              0xFFFF,
              gButtonsImg[((REFIT_INPUT_DIALOG *)EntryC)->Item->BValue ? kCheckboxCheckedImage : kCheckboxImage].Image
            );
          } else {
            StrCatS (ResultString, SVALUE_MAX_SIZE, PoolPrint (L": %s ", ((REFIT_INPUT_DIALOG *)(EntryC))->Item->SValue));
            DrawMenuText (
              ResultString,
              MenuWidth,
              EntriesPosX,
              EntriesPosY + (State->CurrentSelection - State->FirstVisible) * gTextHeight,
              TitleLen + EntryC->Row,
              NULL
            );
          }
          break;

        case TAG_SWITCH:
          switch (((REFIT_INPUT_DIALOG *)(EntryC))->Item->ID) {
            case mThemes:
              iSwitch = (UINTN)gOldChosenTheme;
              break;

            case mConfigs:
              iSwitch = (UINTN)gOldChosenConfig;
              break;

            case mTools:
              iSwitch = (UINTN)gOldChosenTool;
              break;

            case mDSDTs:
              iSwitch = (UINTN)gOldChosenDSDT;
              break;
          }

          DrawMenuText (
            ResultString,
            MenuWidth,
            EntriesPosX,
            EntriesPosY + (State->CurrentSelection - State->FirstVisible) * gTextHeight,
            0xFFFF,
            gButtonsImg[(EntryC->Row == iSwitch) ? kRadioSelectedImage : kRadioImage].Image
          );
          break;

        case TAG_CHECKBIT:
          DrawMenuText (
            ResultString,
            MenuWidth,
            EntriesPosX,
            EntryC->Place.YPos,
            0xFFFF,
            gButtonsImg[BIT_ISSET (((REFIT_INPUT_DIALOG *)EntryC)->Item->IValue, EntryC->Row) ? kCheckboxCheckedImage : kCheckboxImage].Image
            );
          break;

        default:
          DrawMenuText (
            ResultString,
            MenuWidth,
            EntriesPosX,
            EntriesPosY + (State->CurrentSelection - State->FirstVisible) * gTextHeight,
            0xFFFF,
            NULL
          );
          break;
      }

      gScrollStart.YPos = gScrollbarBackground.YPos + gScrollbarBackground.Height * State->FirstVisible / (State->MaxIndex + 1);
      gScrollbar.YPos = gScrollStart.YPos + gScrollStart.Height;
      gScrollEnd.YPos = gScrollbar.YPos + gScrollbar.Height; // gScrollEnd.Height is already subtracted
      ScrollingBar (State);

      break;
    }

    case MENU_FUNCTION_PAINT_TIMEOUT:
      X = (GlobalConfig.UGAWidth - StrLen (ParamText) * GlobalConfig.CharWidth) >> 1;
      DrawMenuText (ParamText, 0, X, TimeoutPosY, 0xFFFF, NULL);
      break;

    case MENU_FUNCTION_CLEANUP:
      break;
  }
}

STATIC
VOID
DrawMainMenuEntry (
  REFIT_MENU_ENTRY    *Entry,
  BOOLEAN             Selected,
  INTN                XPos,
  INTN                YPos
) {
  INTN  Scale = GlobalConfig.MainEntriesSize >> 3;

  MainImage = (
                (Entry->Tag == TAG_LOADER) &&
                BIT_ISUNSET (GlobalConfig.HideBadges, HDBADGES_SWAP) &&
                (Entry->Row == 0)
              )
                ? Entry->DriveImage
                : Entry->Image;

  if (!MainImage) {
    MainImage = LoadImage (gThemeDir, L"icons\\os_mac.png");

    if (!MainImage) {
      MainImage = DummyImage (Scale << 3);
    }
  }

  //DBG ("Entry title=%s; Width=%d\n", Entry->Title, MainImage->Width);
  Scale = ((Entry->Row == 0) ? (Scale * (Selected ? 1 : -1)) : 16) ;

  BltImageCompositeBadge (
    MainImage,
    gSelectionImg[((Entry->Row == 0) ? kBigImage : kSmallImage) + (Selected ? 0 : 1)].Image,
    Selected
      ? (Entry->ImageHover ? Entry->ImageHover : ((Entry->Row == 0) ? Entry->BadgeImage : NULL))
      : ((Entry->Row == 0) ? Entry->BadgeImage : NULL),
    XPos, YPos, Scale);

  if (GlobalConfig.BootCampStyle && (Entry->Row == 0)) {
    BltImageAlpha (
      gSelectionImg[kIndicatorImage + (Selected ? 0 : 1)].Image,
      XPos + (GlobalConfig.row0TileSize / 2) - (INDICATOR_SIZE / 2),
      row0PosY + GlobalConfig.row0TileSize + (BIT_ISSET (GlobalConfig.HideUIFlags, HIDEUI_FLAG_LABEL)
        ? 10
        : (gFontHeight - TEXT_YMARGIN + 20)),
      &gTransparentBackgroundPixel,
      Scale
    );
  }

  Entry->Place.XPos = XPos;
  Entry->Place.YPos = YPos;
  Entry->Place.Width = MainImage->Width;
  Entry->Place.Height = MainImage->Height;
}

STATIC
VOID
FillRectAreaOfScreen (
  IN INTN       XPos,
  IN INTN       YPos,
  IN INTN       Width,
  IN INTN       Height,
  IN EG_PIXEL   *Color,
  IN UINT8      XAlign
) {
  EG_IMAGE    *TmpBuffer = NULL;
  INTN        X = XPos - (Width >> XAlign);

  if (!Width || !Height) {
    return;
  }

  TmpBuffer = CreateImage (Width, Height, FALSE);
  if (!gBackgroundImage) {
    FillImage (TmpBuffer, Color);
  } else {
    RawCopy (
      TmpBuffer->PixelData,
      gBackgroundImage->PixelData + YPos * gBackgroundImage->Width + X,
      Width, Height,
      TmpBuffer->Width,
      gBackgroundImage->Width
    );
  }

  BltImage (TmpBuffer, X, YPos);
  FreeImage (TmpBuffer);
}

STATIC
VOID
DrawMainMenuLabel (
  IN CHAR16               *Text,
  IN INTN                 XPos,
  IN INTN                 YPos,
  IN REFIT_MENU_SCREEN    *Screen,
  IN SCROLL_STATE         *State
) {
  INTN    TextWidth;

  MeasureText (Text, &TextWidth, NULL);

  //Clear old text
  if (OldTextWidth > TextWidth) {
    FillRectAreaOfScreen (OldX, OldY, OldTextWidth, gTextHeight, &gTransparentBackgroundPixel, X_IS_CENTER);
  }

  if (
    !GlobalConfig.BootCampStyle &&
    BIT_ISSET (GlobalConfig.HideBadges, HDBADGES_INLINE) &&
    !OldRow &&
    OldTextWidth &&
    (OldTextWidth != TextWidth)
  ) {
    //Clear badge
    BltImageAlpha (
      NULL,
      (OldX - (OldTextWidth >> 1) - (BADGE_DIMENSION + 16)),
      (OldY - ((BADGE_DIMENSION - gTextHeight) >> 1)),
      &gTransparentBackgroundPixel,
      BADGE_DIMENSION >> 3
    );
  }

  DrawTextXY (Text, XPos, YPos, X_IS_CENTER, 0);

  //show inline badge
  if (
    !GlobalConfig.BootCampStyle &&
    BIT_ISSET (GlobalConfig.HideBadges, HDBADGES_INLINE) &&
    (Screen->Entries[State->CurrentSelection]->Row == 0)
  ) {
    // Display Inline Badge: small icon before the text
    BltImageAlpha (
      ((LOADER_ENTRY *)Screen->Entries[State->CurrentSelection])->me.Image,
      (XPos - (TextWidth >> 1) - (BADGE_DIMENSION + 16)),
      (YPos - ((BADGE_DIMENSION - gTextHeight) >> 1)),
      &gTransparentBackgroundPixel,
      BADGE_DIMENSION >> 3
    );
  }

  OldX = XPos;
  OldY = YPos;
  OldTextWidth = TextWidth;
  OldRow = Screen->Entries[State->CurrentSelection]->Row;
}

VOID
CountItems (
  IN REFIT_MENU_SCREEN    *Screen
) {
  INTN    i;

  row0PosX = 0;
  row1PosX = Screen->EntryCount;
  // layout
  row0Count = 0; //Nr items in row0
  row1Count = 0;

  for (i = 0; i < (INTN)Screen->EntryCount; i++) {
    if (Screen->Entries[i]->Row == 0) {
      row0Count++;
      CONSTRAIN_MIN (row0PosX, i);
    } else {
      row1Count++;
      CONSTRAIN_MAX (row1PosX, i);
    }
  }
}

VOID
DrawTextCorner (
  UINTN   TextC,
  UINT8   Align
) {
  INTN      Xpos;
  CHAR16    *Text;

  if (
    // HIDEUI_ALL - included
    ((TextC == TEXT_CORNER_REVISION) && (BIT_ISSET (GlobalConfig.HideUIFlags, HIDEUI_FLAG_REVISION_TEXT))) ||
    ((TextC == TEXT_CORNER_HELP) && (BIT_ISSET (GlobalConfig.HideUIFlags, HIDEUI_FLAG_HELP_TEXT)))
  ) {
    return;
  }

  switch (TextC) {
    case TEXT_CORNER_REVISION:
      Text = PoolPrint (L"%a", CLOVER_REVISION);
      break;

    case TEXT_CORNER_HELP:
      Text = PoolPrint (L"F1:Help");
      break;

    default:
      return;
  }

  switch (Align) {
    case X_IS_LEFT:
      Xpos = 5;
      break;

    case X_IS_RIGHT:
      Xpos = GlobalConfig.UGAWidth - 5;//2
      break;

    case X_IS_CENTER: //not used
      Xpos = GlobalConfig.UGAWidth >> 1;
      break;

    default:
      return;
  }

  DrawTextXY (Text, Xpos, GlobalConfig.UGAHeight - 5 - gTextHeight, Align, 0);
  FreePool (Text);
}

VOID
MainMenuStyle (
  IN REFIT_MENU_SCREEN  *Screen,
  IN SCROLL_STATE       *State,
  IN UINTN              Function,
  IN CHAR16             *ParamText
) {
  INTN  i;

  switch (Function) {
    case MENU_FUNCTION_INIT:
      GetScreenSize (&GlobalConfig.UGAWidth, &GlobalConfig.UGAHeight);
      InitAnime (Screen);
      SwitchToGraphicsAndClear ();

      EntriesGap = GlobalConfig.TileXSpace;
      EntriesWidth = GlobalConfig.MainEntriesSize + (16 * GlobalConfig.row0TileSize) / 144;
      EntriesHeight = GlobalConfig.MainEntriesSize + 16;

      MaxItemOnScreen = (GlobalConfig.UGAWidth - ROW0_SCROLLSIZE * 2) / (EntriesWidth + EntriesGap); //8
      CountItems (Screen);
      InitScroll (State, row0Count, Screen->EntryCount, MaxItemOnScreen, 0);

      row0PosX = (GlobalConfig.UGAWidth + 8 - (EntriesWidth + EntriesGap) *
                  ((MaxItemOnScreen < row0Count) ? MaxItemOnScreen : row0Count)) >> 1;

      row0PosY = ((GlobalConfig.UGAHeight - LAYOUT_MAINMENU_HEIGHT) >> 1) + GlobalConfig.LayoutBannerOffset;

      row1PosX = (GlobalConfig.UGAWidth + 8 - (GlobalConfig.row1TileSize + TILE_XSPACING) * row1Count) >> 1;

      row1PosY = GlobalConfig.BootCampStyle
        ? row0PosY + GlobalConfig.row0TileSize +
            GlobalConfig.LayoutButtonOffset + GlobalConfig.TileYSpace + INDICATOR_SIZE +
            (BIT_ISSET (GlobalConfig.HideUIFlags, HIDEUI_FLAG_LABEL) ? 15 : (gFontHeight + 30))
        : row0PosY + EntriesHeight + GlobalConfig.TileYSpace + GlobalConfig.LayoutButtonOffset;

      FunctextPosY = row1PosY + GlobalConfig.row1TileSize + GlobalConfig.TileYSpace + GlobalConfig.LayoutTextOffset;

      textPosY = GlobalConfig.BootCampStyle
        ? row0PosY + GlobalConfig.row0TileSize + 10
        : (row1Count > 0)
            ? FunctextPosY
            : row1PosY;

      if (!itemPosX) {
        itemPosX = AllocatePool (sizeof (UINT64) * Screen->EntryCount);
      }

      row0PosXRunning = row0PosX;
      row1PosXRunning = row1PosX;

      //DBG ("EntryCount =%d\n", Screen->EntryCount);

      for (i = 0; i < (INTN)Screen->EntryCount; i++) {
        if (Screen->Entries[i]->Row == 0) {
          itemPosX[i] = row0PosXRunning;
          row0PosXRunning += EntriesWidth + EntriesGap;
        } else {
          itemPosX[i] = row1PosXRunning;
          row1PosXRunning += GlobalConfig.row1TileSize + TILE_XSPACING;
          //DBG ("next item in row1 at x=%d\n", row1PosXRunning);
        }
      }

      // initial painting
      InitSelection ();

      // Update FilmPlace only if not set by InitAnime
      if ((Screen->FilmPlace.Width == 0) || (Screen->FilmPlace.Height == 0)) {
        CopyMem (&Screen->FilmPlace, &gBannerPlace, sizeof (gBannerPlace));
      }

      //DBG ("main menu inited\n");
      break;

    case MENU_FUNCTION_CLEANUP:
      FreePool (itemPosX);
      itemPosX = NULL;
      break;

    case MENU_FUNCTION_PAINT_ALL:
      for (i = 0; i <= State->MaxIndex; i++) {
        if (Screen->Entries[i]->Row == 0) {
          if ((i >= State->FirstVisible) && (i <= State->LastVisible)) {
            DrawMainMenuEntry (
              Screen->Entries[i], (i == State->CurrentSelection) ? 1 : 0,
              itemPosX[i - State->FirstVisible], row0PosY
            );

            // create static text for the boot options if the BootCampStyle is used
            if (GlobalConfig.BootCampStyle && BIT_ISUNSET (GlobalConfig.HideUIFlags, HIDEUI_FLAG_LABEL)) {
              // clear the screen
              FillRectAreaOfScreen (
                itemPosX[i - State->FirstVisible] + (GlobalConfig.row0TileSize / 2), textPosY,
                EntriesWidth + GlobalConfig.TileXSpace, gTextHeight, &gTransparentBackgroundPixel,
                X_IS_CENTER
              );

              // draw the text
              DrawBCSText (
                Screen->Entries[i]->Title, itemPosX[i - State->FirstVisible] + (GlobalConfig.row0TileSize / 2),
                textPosY, X_IS_CENTER
              );
            }
          }
        } else {
          DrawMainMenuEntry (
            Screen->Entries[i], (i == State->CurrentSelection) ? 1 : 0,
            itemPosX[i], row1PosY
          );
        }
      }

      // clear the text from the second row, required by the BootCampStyle
      if (
        GlobalConfig.BootCampStyle &&
        (Screen->Entries[State->LastSelection]->Row == 1) &&
        (Screen->Entries[State->CurrentSelection]->Row == 0) &&
        BIT_ISUNSET (GlobalConfig.HideUIFlags, HIDEUI_FLAG_LABEL)
      ) {
        FillRectAreaOfScreen (
          (GlobalConfig.UGAWidth >> 1), FunctextPosY,
          OldTextWidth, gTextHeight, &gTransparentBackgroundPixel, X_IS_CENTER
        );
      }

      // something is wrong with the DrawMainMenuLabel or Screen->Entries[State->CurrentSelection]
      // and it's required to create the first selection text from here
      // used for the second row entries, when BootCampStyle is used
      if (
        GlobalConfig.BootCampStyle &&
        (Screen->Entries[State->LastSelection]->Row == 0) &&
        (Screen->Entries[State->CurrentSelection]->Row == 1) &&
        BIT_ISUNSET (GlobalConfig.HideUIFlags, HIDEUI_FLAG_LABEL)
      ) {
        DrawMainMenuLabel (
          Screen->Entries[State->CurrentSelection]->Title,
          (GlobalConfig.UGAWidth >> 1), FunctextPosY, Screen, State
        );
      }

      // something is wrong with the DrawMainMenuLabel or Screen->Entries[State->CurrentSelection]
      // and it's required to create the first selection text from here
      // used for all the entries
      if (!GlobalConfig.BootCampStyle && BIT_ISUNSET (GlobalConfig.HideUIFlags, HIDEUI_FLAG_LABEL)) {
        DrawMainMenuLabel (
          Screen->Entries[State->CurrentSelection]->Title,
          (GlobalConfig.UGAWidth >> 1), textPosY, Screen, State
        );
      }

      DrawTextCorner (TEXT_CORNER_HELP, X_IS_LEFT);
      DrawTextCorner (TEXT_CORNER_REVISION, X_IS_RIGHT);
      break;

    case MENU_FUNCTION_PAINT_SELECTION:
      if (Screen->Entries[State->LastSelection]->Row == 0) {
        DrawMainMenuEntry (
          Screen->Entries[State->LastSelection], FALSE,
          itemPosX[State->LastSelection - State->FirstVisible], row0PosY
        );
      } else {
        DrawMainMenuEntry (
          Screen->Entries[State->LastSelection], FALSE,
          itemPosX[State->LastSelection], row1PosY
        );
      }

      if (Screen->Entries[State->CurrentSelection]->Row == 0) {
        DrawMainMenuEntry (
          Screen->Entries[State->CurrentSelection], TRUE,
          itemPosX[State->CurrentSelection - State->FirstVisible], row0PosY
        );
      } else {
        DrawMainMenuEntry (
          Screen->Entries[State->CurrentSelection], TRUE,
          itemPosX[State->CurrentSelection], row1PosY
        );
      }

      // create dynamic text for the second row if BootCampStyle is used
      if (
        GlobalConfig.BootCampStyle &&
        (BIT_ISUNSET (GlobalConfig.HideUIFlags, HIDEUI_FLAG_LABEL)) &&
        (Screen->Entries[State->CurrentSelection]->Row == 1)
      ) {
        DrawMainMenuLabel (
          Screen->Entries[State->CurrentSelection]->Title,
          (GlobalConfig.UGAWidth >> 1), FunctextPosY, Screen, State
        );
      }

      // create dynamic text for all the entries
      if (!GlobalConfig.BootCampStyle && BIT_ISUNSET (GlobalConfig.HideUIFlags, HIDEUI_FLAG_LABEL)) {
        DrawMainMenuLabel (
          Screen->Entries[State->CurrentSelection]->Title,
          (GlobalConfig.UGAWidth >> 1), textPosY, Screen, State
        );
      }

      DrawTextCorner (TEXT_CORNER_HELP, X_IS_LEFT);
      DrawTextCorner (TEXT_CORNER_REVISION, X_IS_RIGHT);
      break;

    case MENU_FUNCTION_PAINT_TIMEOUT:
      i = BIT_ISSET (GlobalConfig.HideBadges, HDBADGES_INLINE) ? 3 : 1;

      if (BIT_ISUNSET (GlobalConfig.HideUIFlags, HIDEUI_FLAG_LABEL)) {
        FillRectAreaOfScreen (
          (GlobalConfig.UGAWidth >> 1), FunctextPosY + gTextHeight * i,
          OldTimeoutTextWidth, gTextHeight, &gTransparentBackgroundPixel, X_IS_CENTER
        );

        OldTimeoutTextWidth = DrawTextXY (ParamText, (GlobalConfig.UGAWidth >> 1), FunctextPosY + gTextHeight * i, X_IS_CENTER, 0);
      }

      DrawTextCorner (TEXT_CORNER_HELP, X_IS_LEFT);
      DrawTextCorner (TEXT_CORNER_REVISION, X_IS_RIGHT);
      break;
  }
}

VOID
CreateHeaderEntries (
  REFIT_MENU_ENTRY    **Entry,
  REFIT_MENU_SCREEN   **SubScreen,
  CHAR16              *Title,
  UINTN               ID,
  CHAR16              ShortcutLetter
) {
  REFIT_MENU_ENTRY    *TmpEntry = AllocateZeroPool (sizeof (REFIT_MENU_ENTRY));
  REFIT_MENU_SCREEN   *TmpSubScreen = AllocateZeroPool (sizeof (REFIT_MENU_SCREEN));

  TmpEntry->Title = PoolPrint (Title);
  TmpEntry->Image =  gOptionMenu.TitleImage;
  TmpEntry->Tag = TAG_OPTIONS;

  if (ShortcutLetter) {
    TmpEntry->ShortcutLetter = ShortcutLetter;
  }

  // create the submenu
  TmpSubScreen->Title = PoolPrint (TmpEntry->Title);
  TmpSubScreen->TitleImage = TmpEntry->Image;
  TmpSubScreen->ID = ID;
  TmpSubScreen->AnimeRun = GetAnime (TmpSubScreen);

  *Entry = TmpEntry;
  *SubScreen = TmpSubScreen;
}

REFIT_MENU_ENTRY *
SubMenuDevices () {
  REFIT_MENU_ENTRY      *Entry = NULL;
  REFIT_MENU_SCREEN     *SubScreen = NULL;
  INTN                  i = 0;

  CreateHeaderEntries (&Entry, &SubScreen, L"Devices", SCREEN_DEVICES, 'D');

  AddMenuInfoLine (SubScreen, PoolPrint (L"Number of VideoCards: %d", gSettings.NGFX));

  for (i = 0; i < OptMenuDevicesNum; i++) {
    AddMenuBOOL (SubScreen, OPT_MENU_DEVICES[i].Title, NULL, OPT_MENU_DEVICES[i].ID);
  }

  AddMenuEntry (SubScreen, &gMenuEntryReturn);
  Entry->SubScreen = SubScreen;

  return Entry;
}

REFIT_MENU_ENTRY *
SubMenuPatches () {
  REFIT_MENU_ENTRY      *Entry = NULL;
  REFIT_MENU_SCREEN     *SubScreen = NULL;
  INTN                  i = 0;

  CreateHeaderEntries (&Entry, &SubScreen, L"Patches", SCREEN_PATCHES, 'P');

  AddMenuInfoLine (SubScreen, PoolPrint (L"%a", gSettings.CPUStructure.BrandString));
  AddMenuInfoLine (SubScreen, PoolPrint (L"Real CPUID: 0x%06x", gSettings.CPUStructure.Signature));

  for (i = 0; i < OptMenuPatchesNum; i++) {
    AddMenuBOOL (SubScreen, OPT_MENU_PATCHES[i].Title, NULL, OPT_MENU_PATCHES[i].ID);
  }

  for (i = 0; i < OptMenuFlagBitNum; i++) {
    if (!OPT_MENU_FLAGBIT[i].ID) {
      continue;
    }

    AddMenuCheck (SubScreen, OPT_MENU_FLAGBIT[i].Title, OPT_MENU_FLAGBIT[i].Bit, mFlagsBits);
  }

  AddMenuEntry (SubScreen, &gMenuEntryReturn);
  Entry->SubScreen = SubScreen;

  return Entry;
}

REFIT_MENU_ENTRY *
SubMenuDebug () {
  REFIT_MENU_ENTRY      *Entry = NULL;
  REFIT_MENU_SCREEN     *SubScreen = NULL;
  INTN                  i = 0;

  CreateHeaderEntries (&Entry, &SubScreen, L"Debug", SCREEN_DEBUG, 'B');

  for (i = 0; i < OptMenuDebugNum; i++) {
    AddMenuBOOL (SubScreen, OPT_MENU_DEBUG[i].Title, NULL, OPT_MENU_DEBUG[i].ID);
  }

  AddMenuEntry (SubScreen, &gMenuEntryReturn);
  Entry->SubScreen = SubScreen;

  return Entry;
}

REFIT_MENU_ENTRY *
SubMenuAcpi () {
  REFIT_MENU_ENTRY    *Entry = NULL;
  REFIT_MENU_SCREEN   *SubScreen = NULL;
  CHAR8               Signature[ACPI_NAME_SIZE + 1], OemTableId[ACPI_OEM_TABLE_ID_SIZE + 1];
  UINT8               i = 0;
  S_FILES             *aTmp = gDSDTFiles;

  Signature[ACPI_NAME_SIZE] = 0;
  OemTableId[ACPI_OEM_TABLE_ID_SIZE] = 0;

  CreateHeaderEntries (&Entry, &SubScreen, L"ACPI", SCREEN_TABLES, 'A');

  if (gDSDTFiles) {
    AddSeparator (SubScreen, "Available DSDT");

    while (aTmp) {
      AddMenuRadio (
        SubScreen,
        PoolPrint (L"%s: %s", aTmp->Description, aTmp->FileName), //StrToTitle (aTmp->FileName)
        NULL,
        mDSDTs,
        aTmp->Index
      );

      aTmp = aTmp->Next;
    }
  }

  AddSeparator (SubScreen, "DSDT Fixes");

  for (i = 0; i < gOptFixDSDTBitNum; i++) {
    AddMenuCheck (SubScreen, PoolPrint (L"%a", gAFIXDSDT[i].Title), gAFIXDSDT[i].Bit, mDSDTFix);
  }

  AddSeparator (SubScreen, "Drop Table");

  if (gSettings.ACPIDropTables) {
    ACPI_DROP_TABLE   *DropTable = gSettings.ACPIDropTables;

    while (DropTable) {
      CopyMem ((CHAR8 *)&Signature, (CHAR8 *)&(DropTable->Signature), ACPI_NAME_SIZE);
      CopyMem ((CHAR8 *)&OemTableId, (CHAR8 *)&(DropTable->TableId), ACPI_OEM_TABLE_ID_SIZE);

      //MsgLog ("adding to menu %a (%x) %a (%lx) L=%d (0x%x)\n",
      //       Signature, DropTable->Signature,
      //       OemTableId, DropTable->TableId,
      //       DropTable->Length, DropTable->Length);
      AddMenuBOOL (SubScreen, PoolPrint (L"Drop \"%-4a\" \"%-8a\" %d", Signature, OemTableId, DropTable->Length), &(DropTable->MenuItem), 0);
      DropTable = DropTable->Next;
    }
  }

  AddMenuBOOL (SubScreen, L"Drop all OEM SSDT", NULL, mDropOEM);

  if (gACPIUserLoad) {
    ACPI_USER_LOAD    *ACPIUserLoadTmp = gACPIUserLoad;

    AddSeparator (SubScreen, "Patched ACPI");

    while (ACPIUserLoadTmp) {
      CHAR16  *OsSubdir = NULL;

      if (OSTYPE_IS_DARWIN_GLOB (ACPIUserLoadTmp->OSType)) {
        OsSubdir = OSTYPE_DARWIN_STR;
      } else if (OSTYPE_IS_LINUX_GLOB (ACPIUserLoadTmp->OSType)) {
        OsSubdir = OSTYPE_LINUX_STR;
      } else if (OSTYPE_IS_WINDOWS_GLOB (ACPIUserLoadTmp->OSType)) {
        OsSubdir = OSTYPE_WINDOWS_STR;
      } else {
        continue;
      }

      AddMenuBOOL (SubScreen, PoolPrint (L"Drop \"%s: %s\"", OsSubdir, ACPIUserLoadTmp->FileName), &(ACPIUserLoadTmp->MenuItem), 0);

      ACPIUserLoadTmp = ACPIUserLoadTmp->Next;
    }
  }

  AddMenuEntry (SubScreen, &gMenuEntryReturn);
  Entry->SubScreen = SubScreen;

  return Entry;
}

REFIT_MENU_ENTRY *
SubMenuThemes () {
  REFIT_MENU_ENTRY      *Entry = NULL;
  REFIT_MENU_SCREEN     *SubScreen = NULL;
  S_FILES               *aTmp = gThemeFiles;

  CreateHeaderEntries (&Entry, &SubScreen, L"Themes", SCREEN_THEMES, 'T');

  AddMenuInfoLine (SubScreen, L"Available themes:");

  while (aTmp) {
    AddMenuRadio (
      SubScreen,
      StrToTitle (aTmp->FileName),
      NULL,
      mThemes,
      aTmp->Index
    );

    aTmp = aTmp->Next;
  }

  AddMenuEntry (SubScreen, &gMenuEntryReturn);
  Entry->SubScreen = SubScreen;

  return Entry;
}

REFIT_MENU_ENTRY *
SubMenuTools () {
  REFIT_MENU_ENTRY      *Entry = NULL;
  REFIT_MENU_SCREEN     *SubScreen = NULL;
  S_FILES               *aTmp = gToolFiles;

  CreateHeaderEntries (&Entry, &SubScreen, L"Tools", SCREEN_TOOLS, 'X');

  AddMenuInfoLine (SubScreen, L"Available tools:");

  while (aTmp) {
    AddMenuRadio (
      SubScreen,
      StrToTitle (aTmp->FileName),
      NULL,
      mTools,
      aTmp->Index
    );

    aTmp = aTmp->Next;
  }

  AddMenuEntry (SubScreen, &gMenuEntryReturn);
  Entry->SubScreen = SubScreen;

  return Entry;
}

REFIT_MENU_ENTRY *
SubMenuConfigs () {
  REFIT_MENU_ENTRY      *Entry = NULL;
  REFIT_MENU_SCREEN     *SubScreen = NULL;
  S_FILES               *aTmp = gConfigFiles;

  CreateHeaderEntries (&Entry, &SubScreen, L"Configs", SCREEN_CONFIGS, 'C');

  AddMenuInfoLine (SubScreen, L"Available configs:");

  while (aTmp) {
    AddMenuRadio (
      SubScreen,
      StrToTitle (aTmp->FileName),
      NULL,
      mConfigs,
      aTmp->Index
    );

    aTmp = aTmp->Next;
  }

  AddMenuEntry (SubScreen, &gMenuEntryReturn);
  Entry->SubScreen = SubScreen;

  return Entry;
}

VOID
OptionsMenu (
  OUT REFIT_MENU_ENTRY    **ChosenEntry
) {
  REFIT_MENU_ENTRY        *TmpChosenEntry = NULL;
  MENU_STYLE_FUNC         Style = TextMenuStyle, SubStyle;
  UINTN                   MenuExit = 0, SubMenuExit;
  INTN                    EntryIndex = 0, SubMenuIndex;

  //GlobalConfig.Proportional = FALSE; //temporary disable proportional

  if (GlobalConfig.AllowGraphicsMode) {
    Style = GraphicsMenuStyle;
  }

  // remember, if you extended this menu then change procedures

  // FillInputs and ApplyInputs

  gOptionMenu.TitleImage = (BIT_ISUNSET (GlobalConfig.HideUIFlags, HIDEUI_FLAG_MENU_TITLE_IMAGE))
                              ? BuiltinIcon (BUILTIN_ICON_FUNC_OPTIONS)
                              : NULL;

  gThemeOptionsChanged = FALSE;

  if (gOptionMenu.EntryCount == 0) {
    gThemeOptionsChanged = TRUE;
    gOptionMenu.ID = SCREEN_OPTIONS;
    gOptionMenu.AnimeRun = GetAnime (&gOptionMenu);

    *ChosenEntry = (REFIT_MENU_ENTRY *)AllocateZeroPool (sizeof (REFIT_INPUT_DIALOG));

    AddMenuString (&gOptionMenu, L"BootArgs", mBootArgs);

    if (gConfigFiles) {
      AddMenuEntry (&gOptionMenu, SubMenuConfigs ());
    }

    if (GlobalConfig.AllowGraphicsMode && gThemeFiles) {
      AddMenuEntry (&gOptionMenu, SubMenuThemes ());
    }

    AddMenuEntry (&gOptionMenu, SubMenuPatches ());
    AddMenuEntry (&gOptionMenu, SubMenuAcpi ());
    AddMenuEntry (&gOptionMenu, SubMenuDevices ());
    AddMenuEntry (&gOptionMenu, SubMenuDebug ());
    AddMenuEntry (&gOptionMenu, SubMenuTools ());

    AddMenuEntry (&gOptionMenu, &gMenuEntryReturn);
    //DBG ("option menu created entries=%d\n", gOptionMenu.EntryCount);
  }

  while (!MenuExit) {
    MenuExit = RunGenericMenu (&gOptionMenu, Style, &EntryIndex, ChosenEntry);
    if ((MenuExit == MENU_EXIT_ESCAPE) || ((*ChosenEntry)->Tag == TAG_RETURN)) {
      BOOLEAN   OnGraphicsMode = IsGraphicsModeEnabled ();

      if (
        gBootChanged &&
        gThemeChanged &&
        (
          (gSettings.TextOnly && OnGraphicsMode) ||
          (!gSettings.TextOnly && !OnGraphicsMode)
        )
      ) {
        ReInitScreen ();
      }
      break;
    }

    if (MenuExit == MENU_EXIT_ENTER) {
      //enter input dialog or subscreen
      if ((*ChosenEntry)->SubScreen != NULL) {
        SubMenuIndex = -1;
        SubMenuExit = 0;
        SubStyle = Style;

        while (!SubMenuExit) {
          SubMenuExit = RunGenericMenu ((*ChosenEntry)->SubScreen, SubStyle, &SubMenuIndex, &TmpChosenEntry);

          if ((SubMenuExit == MENU_EXIT_ESCAPE) || (TmpChosenEntry->Tag == TAG_RETURN)) {
            ApplyInputs ();
            break;
          }

          if (SubMenuExit == MENU_EXIT_ENTER) {
            //enter input dialog
            SubMenuExit = 0;
            /*if ((*ChosenEntry)->SubScreen->ID == SCREEN_DSDT) {
              ApplyInputs ();
            } else*/ if ((*ChosenEntry)->SubScreen->ID == SCREEN_TOOLS) {
              return StartToolFromMenu ();
            }
          }
        } //while (!SubMenuExit)
      }

      MenuExit = 0;
    } // if MENU_EXIT_ENTER
  }

//exit:
  //GlobalConfig.Proportional = OldFontStyle;
  ApplyInputs ();
}

//
// user-callable dispatcher functions
//

VOID
AboutRefit () {
  if (BIT_ISUNSET (GlobalConfig.HideUIFlags, HIDEUI_FLAG_MENU_TITLE_IMAGE)) {
    AboutMenu.TitleImage = BuiltinIcon (BUILTIN_ICON_FUNC_ABOUT);
  } else {
    AboutMenu.TitleImage = NULL;
  }

  if (AboutMenu.EntryCount == 0) {
    AddMenuInfo (&AboutMenu, PoolPrint (L"%a on %a", CLOVER_REVISION_STR, CLOVER_BUILDDATE));
    AddMenuInfo (&AboutMenu, PoolPrint (L"  %a", CLOVER_BASED_INFO));
    AddMenuInfo (&AboutMenu, PoolPrint (L"  EDK II (rev %a)", EDK2_REVISION));
    SplitMenuInfo (&AboutMenu, PoolPrint (L"  [%a]", CLOVER_BUILDINFOS_STR), AddMenuInfo);

    AddSeparator (&AboutMenu, NULL);

    AddMenuInfo (&AboutMenu, L"Based on rEFIt (c) 2006-2010 Christoph Pfisterer");
    AddMenuInfo (&AboutMenu, L"Portions Copyright (c) Intel Corporation");
    AddMenuInfo (&AboutMenu, L"Developers:");
    AddMenuInfo (&AboutMenu, L"  Slice, dmazar, apianti, JrCs, pene, usrsse2");
    AddMenuInfo (&AboutMenu, L"Credits also:");
    AddMenuInfo (&AboutMenu, L"  Kabyl, pcj, jadran, Blackosx, STLVNUB, ycr.ru");
    AddMenuInfo (&AboutMenu, L"  FrodoKenny, skoczi, crazybirdy, Oscar09, xsmile");
    AddMenuInfo (&AboutMenu, L"  cparm, rehabman, nms42, sherlocks, Zenith432");
    AddMenuInfo (&AboutMenu, L"  stinga11, TheRacerMaster, solstice, SoThOr, DF");
    AddMenuInfo (&AboutMenu, L"  cecekpawon, Micky1979, Needy, joevt");
    AddMenuInfo (&AboutMenu, L"  projectosx.com, applelife.ru, insanelymac.com");

    AddSeparator (&AboutMenu, NULL);

    AddMenuInfo (&AboutMenu, L"Running on:");
    AddMenuInfo (&AboutMenu, PoolPrint (L"  UEFI Revision %d.%02d",
                              gST->Hdr.Revision >> 16,
                              gST->Hdr.Revision & ((1 << 16) - 1)
                            ));
    AddMenuInfo (&AboutMenu, L"  Platform: x86_64 (64 bit)");
    AddMenuInfo (&AboutMenu, PoolPrint (L"  Firmware: %s rev %d.%d",
                              gST->FirmwareVendor,
                              gST->FirmwareRevision >> 16,
                              gST->FirmwareRevision & ((1 << 16) - 1)
                            ));
    AddMenuInfo (&AboutMenu, PoolPrint (L"  Screen Output: %s", ScreenDescription ()));
    AboutMenu.AnimeRun = GetAnime (&AboutMenu);
    //AddMenuEntry (&AboutMenu, &gMenuEntryReturn);
  } else if (AboutMenu.EntryCount >= 2) {
    FreePool (AboutMenu.Entries[AboutMenu.EntryCount - 2]->Title);
    AboutMenu.Entries[AboutMenu.EntryCount - 2]->Title = PoolPrint (L"  Screen Output: %s", ScreenDescription ());
  }

  RunMenu (&AboutMenu, NULL);
}

VOID
HelpRefit () {
  if (BIT_ISUNSET (GlobalConfig.HideUIFlags, HIDEUI_FLAG_MENU_TITLE_IMAGE)) {
    HelpMenu.TitleImage = BuiltinIcon (BUILTIN_ICON_FUNC_HELP);
  } else {
    HelpMenu.TitleImage = NULL;
  }

  if (HelpMenu.EntryCount == 0) {
    //switch (gLanguage) {
    //  case english:
    //  default:
        AddMenuInfo (&HelpMenu, L"ESC - Escape from submenu, Refresh main menu");
        AddMenuInfo (&HelpMenu, L"F1 / H - This help");
        AddMenuInfo (&HelpMenu, PoolPrint (L"F2  - Save '%s' into '%s'", Basename (PREBOOT_LOG), DIR_MISC));
        AddMenuInfo (&HelpMenu, L"F3  - Show hidden entries");
        AddMenuInfo (&HelpMenu, PoolPrint (L"F4  - Save oem DSDT into '%s'", DIR_ACPI_ORIGIN));
        AddMenuInfo (&HelpMenu, PoolPrint (L"F5  - Save patched DSDT into '%s'", DIR_ACPI_PATCHED));
        AddMenuInfo (&HelpMenu, PoolPrint (L"F6  - Save VideoBios into '%s'", DIR_MISC));
        AddMenuInfo (&HelpMenu, L"F9  - Switch screen mode");
        AddMenuInfo (&HelpMenu, PoolPrint (L"F10 - Save screenshot into '%s'", DIR_MISC));
        //AddMenuInfo (&HelpMenu, L"F12 - Eject selected volume (DVD)");
        AddMenuInfo (&HelpMenu, L"Space - Details about selected menu entry");
        AddMenuInfo (&HelpMenu, L"Digits [1-9] - Shortcut to menu entry");
        AddMenuInfo (&HelpMenu, L"I - About");
        AddMenuInfo (&HelpMenu, L"O - Options");
        AddMenuInfo (&HelpMenu, L"R - Soft Reset");
        AddMenuInfo (&HelpMenu, L"X - Exit");
        AddMenuInfo (&HelpMenu, L"ALT+D - Boot debug patches");
        AddMenuInfo (&HelpMenu, L"ALT+S - Boot single-user");
        AddMenuInfo (&HelpMenu, L"ALT+V - Boot verbose");
        AddMenuInfo (&HelpMenu, L"CTRL+R - Reset NVRAM");
        AddMenuInfo (&HelpMenu, L"C - Options - Configs");
        AddMenuInfo (&HelpMenu, L"T - Options - Themes");
        AddMenuInfo (&HelpMenu, L"A - Options - ACPI");
        AddMenuInfo (&HelpMenu, L"P - Options - Patches");
        AddMenuInfo (&HelpMenu, L"D - Options - Devices");
        AddMenuInfo (&HelpMenu, L"B - Options - Debug");
        AddMenuInfo (&HelpMenu, L"X - Options - Tools");
    //    break;
    //}

    HelpMenu.AnimeRun = GetAnime (&HelpMenu);
    //AddMenuEntry (&HelpMenu, &gMenuEntryReturn);
  }

  RunMenu (&HelpMenu, NULL);
}

UINTN
RunMenu (
  IN  REFIT_MENU_SCREEN   *Screen,
  OUT REFIT_MENU_ENTRY    **ChosenEntry
) {
  INTN              index = -1;
  MENU_STYLE_FUNC   Style = TextMenuStyle;

  if (GlobalConfig.AllowGraphicsMode) {
    Style = GraphicsMenuStyle;
  }

  return RunGenericMenu (Screen, Style, &index, ChosenEntry);
}

UINTN
RunMainMenu (
  IN  REFIT_MENU_SCREEN   *Screen,
  IN  INTN                DefaultSelection,
  OUT REFIT_MENU_ENTRY    **ChosenEntry
) {
  MENU_STYLE_FUNC     Style = TextMenuStyle,
                      MainStyle = TextMenuStyle;
  REFIT_MENU_ENTRY    *TempChosenEntry = 0;
  UINTN               MenuExit = 0;
  INTN                DefaultEntryIndex = DefaultSelection, SubMenuIndex;
  LOADER_ENTRY        *TempChosenEntryBkp = NULL;
  BOOLEAN             ESCLoader = FALSE;

  if (GlobalConfig.AllowGraphicsMode && !gSettings.TextOnly) {
    Style = GraphicsMenuStyle;
    MainStyle = MainMenuStyle;
  } else {
    GlobalConfig.AllowGraphicsMode = FALSE;
  }

  while (!MenuExit) {
    Screen->AnimeRun = gMainAnime;
    MenuExit = RunGenericMenu (Screen, MainStyle, &DefaultEntryIndex, &TempChosenEntry);
    Screen->TimeoutSeconds = 0;

    if ((MenuExit == MENU_EXIT_DETAILS) && (TempChosenEntry->SubScreen != NULL)) {
      SubMenuIndex = -1;

      if (TempChosenEntryBkp != NULL) {
        FreePool (TempChosenEntryBkp);
      }

      TempChosenEntryBkp = DuplicateLoaderEntry ((LOADER_ENTRY *)TempChosenEntry);

      DecodeOptions ((LOADER_ENTRY *)TempChosenEntry);

      MenuExit = RunGenericMenu (TempChosenEntry->SubScreen, Style, &SubMenuIndex, &TempChosenEntry);

      if (
        (
          (MenuExit == MENU_EXIT_ENTER) ||
          (TempChosenEntry->ID == MENU_ENTRY_ID_BOOT)
        ) &&
        (TempChosenEntry->Tag == TAG_LOADER)
      ) {
        DecodeOptions ((LOADER_ENTRY *)TempChosenEntry);
        //AsciiSPrint (gSettings.BootArgs, AVALUE_MAX_SIZE - 1, "%s", ((LOADER_ENTRY *)TempChosenEntry)->LoadOptions);
      }
      if ((MenuExit == MENU_EXIT_ESCAPE) || (TempChosenEntry->Tag == TAG_RETURN)) {
        if (((REFIT_MENU_ENTRY *)TempChosenEntryBkp)->Tag == TAG_LOADER) {
          DecodeOptions (TempChosenEntryBkp);
          //AsciiSPrint (gSettings.BootArgs, AVALUE_MAX_SIZE - 1, "%s", TempChosenEntryBkp->LoadOptions);
          ESCLoader = TRUE;
        }

        MenuExit = 0;
      }
    }
  }

  if (ChosenEntry) {
    if (ESCLoader) {
      CopyMem (((LOADER_ENTRY *)TempChosenEntry), TempChosenEntryBkp, sizeof (TempChosenEntryBkp));
      FreePool (TempChosenEntryBkp);
    }

    *ChosenEntry = TempChosenEntry;
  }

  return MenuExit;
}

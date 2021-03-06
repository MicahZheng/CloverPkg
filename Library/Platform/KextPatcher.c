/*
 * Copyright (c) 2011-2012 Frank Peng. All rights reserved.
 *
 */

#include <Library/Platform/KernelPatcher.h>

#ifndef DEBUG_ALL
#ifndef DEBUG_KEXT_PATCHER
#define DEBUG_KEXT_PATCHER 0
#endif
#else
#ifdef DEBUG_KEXT_PATCHER
#undef DEBUG_KEXT_PATCHER
#endif
#define DEBUG_KEXT_PATCHER DEBUG_ALL
#endif

#define DBG(...) DebugLog (DEBUG_KEXT_PATCHER, __VA_ARGS__)

#ifdef LAZY_PARSE_KEXT_PLIST
#define PRELINKKEXTLIST_SIGNATURE SIGNATURE_32 ('C','E','X','T')

typedef struct {
  UINT32            Signature;
  LIST_ENTRY        Link;
  CHAR8             *BundleIdentifier;
  INTN              Address;
  INTN              Size;
  UINTN             Offset;
  UINTN             Taglen;
} PRELINKKEXTLIST;

LIST_ENTRY gPrelinkKextList = INITIALIZE_LIST_HEAD_VARIABLE (gPrelinkKextList)/*,
           gLoadedKextList = INITIALIZE_LIST_HEAD_VARIABLE (gLoadedKextList)*/;
#endif

VOID
FilterKextPatches (
  IN LOADER_ENTRY   *Entry
) {
  if (
    gSettings.KextPatchesAllowed &&
    (Entry->KernelAndKextPatches->KextPatches != NULL) &&
    Entry->KernelAndKextPatches->NrKexts
  ) {
    UINTN    i = 0;

    MsgLog ("Filtering KextPatches:\n");

    for (; i < Entry->KernelAndKextPatches->NrKexts; ++i) {
      BOOLEAN   NeedBuildVersion = (
                  (Entry->OSBuildVersion != NULL) &&
                  (Entry->KernelAndKextPatches->KextPatches[i].MatchBuild != NULL)
                );

      // If dot exist in the patch name, store string after last dot to Filename for FSInject to load kext
      if (CountOccurrences (Entry->KernelAndKextPatches->KextPatches[i].Name, '.') >= 2) {
        CHAR16  *Str = AllocateZeroPool (SVALUE_MAX_SIZE);
        UINTN   Len;

        Str = FindExtension (PoolPrint (L"%a", Entry->KernelAndKextPatches->KextPatches[i].Name));
        Len = StrLen (Str) + 1;
        Entry->KernelAndKextPatches->KextPatches[i].Filename = AllocateZeroPool (Len);

        UnicodeStrToAsciiStrS (
          Str,
          Entry->KernelAndKextPatches->KextPatches[i].Filename,
          Len
        );

        FreePool (Str);
      }

      MsgLog (" - [%02d]: %a | %a | [MatchOS: %a | MatchBuild: %a]",
        i,
        Entry->KernelAndKextPatches->KextPatches[i].Label,
        Entry->KernelAndKextPatches->KextPatches[i].IsPlistPatch ? "PlistPatch" : "BinPatch",
        Entry->KernelAndKextPatches->KextPatches[i].MatchOS
          ? Entry->KernelAndKextPatches->KextPatches[i].MatchOS
          : "All",
        NeedBuildVersion
          ? Entry->KernelAndKextPatches->KextPatches[i].MatchBuild
          : "All"
      );

      if (NeedBuildVersion) {
        Entry->KernelAndKextPatches->KextPatches[i].Disabled = !IsPatchEnabled (
          Entry->KernelAndKextPatches->KextPatches[i].MatchBuild, Entry->OSBuildVersion);

        MsgLog (" | Allowed: %a\n", Entry->KernelAndKextPatches->KextPatches[i].Disabled ? "No" : "Yes");

        //if (!Entry->KernelAndKextPatches->KextPatches[i].Disabled) {
          continue; // If user give MatchOS, should we ignore MatchOS / keep reading 'em?
        //}
      }

      Entry->KernelAndKextPatches->KextPatches[i].Disabled = !IsPatchEnabled (
        Entry->KernelAndKextPatches->KextPatches[i].MatchOS, Entry->OSVersion);

      MsgLog (" | Allowed: %a\n", Entry->KernelAndKextPatches->KextPatches[i].Disabled ? "No" : "Yes");
    }
  }
}

//
// Searches Source for Search pattern of size SearchSize
// and replaces it with Replace up to MaxReplaces times.
// If MaxReplaces <= 0, then there is no restriction on number of replaces.
// Replace should have the same size as Search.
// Returns number of replaces done.
//
BOOLEAN
FindWildcardPattern (
  UINT8     *Source,
  UINT8     *Search,
  UINTN     SearchSize,
  UINT8     *Replace,
  UINT8     **NewReplace,
  UINT8     Wildcard
) {
  UINTN     i, SearchCount = 0, ReplaceCount = 0;
  UINT8     *TmpReplace = AllocateZeroPool (SearchSize);
  BOOLEAN   Ret;

  for (i = 0; i < SearchSize; i++) {
    if ((Search[i] != Wildcard) && (Search[i] != Source[i])) {
      SearchCount = 0;
      break;
    }

    if (Replace[i] == Wildcard) {
      TmpReplace[i] = Source[i];
      ReplaceCount++;
    } else {
      TmpReplace[i] = Replace[i];
    }

    SearchCount++;
  }

  Ret = (SearchSize == SearchCount);

  if (!Ret || !ReplaceCount) {
    FreePool (TmpReplace);
  } else {
    *NewReplace = TmpReplace;
  }

  return Ret;
}

UINTN
SearchAndReplace (
  UINT8     *Source,
  UINT32    SourceSize,
  UINT8     *Search,
  UINTN     SearchSize,
  UINT8     *Replace,
  UINT8     Wildcard,
  INTN      MaxReplaces
) {
  BOOLEAN   NoReplacesRestriction = (MaxReplaces <= 0);
  UINTN     NumReplaces = 0;
  UINT8     *End = Source + SourceSize;

  if (!Source || !Search || !Replace || !SearchSize) {
    return 0;
  }

  while ((Source < End) && (NoReplacesRestriction || (MaxReplaces > 0))) {
    UINT8   *NewReplace = NULL;

    if (
      ((Wildcard != 0xFF) && FindWildcardPattern (Source, Search, SearchSize, Replace, &NewReplace, Wildcard)) ||
      (CompareMem (Source, Search, SearchSize) == 0)
    ) {
      if (NewReplace != NULL) {
        CopyMem (Source, NewReplace, SearchSize);
        FreePool (NewReplace);
      } else {
        CopyMem (Source, Replace, SearchSize);
      }

      NumReplaces++;
      MaxReplaces--;
      Source += SearchSize;
    } else {
      Source++;
    }
  }

  return NumReplaces;
}

UINTN
SearchAndReplaceTxt (
  UINT8     *Source,
  UINT32    SourceSize,
  UINT8     *Search,
  UINTN     SearchSize,
  UINT8     *Replace,
  UINT8     Wildcard,
  INTN      MaxReplaces
) {
  BOOLEAN   NoReplacesRestriction = (MaxReplaces <= 0);
  UINTN     NumReplaces = 0, Skip;
  UINT8     *End = Source + SourceSize, *SearchEnd = Search + SearchSize, *Pos = NULL, *FirstMatch = Source;

  if (!Source || !Search || !Replace || !SearchSize) {
    return 0;
  }

  while (
    ((Source + SearchSize) <= End) &&
    (NoReplacesRestriction || (MaxReplaces > 0))
  ) { // num replaces
    UINT8   *NewReplace = Replace;
    UINTN   i = 0;

    while (*Source != '\0') {  //comparison
      Pos = Search;
      FirstMatch = Source;
      Skip = 0;

      while ((*Source != '\0') && (Pos != SearchEnd)) {
        if (*Source <= 0x20) { //skip invisibles in sources
          Source++;
          Skip++;
          i++;
          continue;
        }

        if (Wildcard != 0xFF) {
          if ((*Source != *Pos) && (Wildcard != *Pos)) {
            break;
          }

          if (Wildcard == NewReplace[i]) {
            NewReplace[i] = *Source;
          }
        }

        //AsciiPrint ("%c", *Source);
        Source++;
        Pos++;
        i++;
      }

      if (Pos == SearchEnd) { // pattern found
        Pos = FirstMatch;
        break;
      } else {
        Pos = NULL;
      }

      Source = FirstMatch + 1;
      /*
      if (Pos != Search) {
        AsciiPrint ("\n");
      }
      */
    }

    if (!Pos) {
      break;
    }

    CopyMem (Pos, NewReplace, SearchSize);
    SetMem (Pos + SearchSize, Skip, 0x20); //fill skip places with spaces
    NumReplaces++;
    MaxReplaces--;
    Source = FirstMatch + SearchSize + Skip;
  }

  return NumReplaces;
}

BOOLEAN
IsPatchNameMatch (
  CHAR8   *BundleIdentifier,
  CHAR8   *Name,
  CHAR8   *InfoPlist,
  INT32   *IsBundle
) {
  // Full BundleIdentifier: com.apple.driver.AppleHDA
  *IsBundle = (CountOccurrences (Name, '.') < 2) ? 0 : 1;
  return
    ((InfoPlist != NULL) && (*IsBundle == 0))
      ? (AsciiStrStr (InfoPlist, Name) != NULL)
      : (AsciiStrCmp (BundleIdentifier, Name) == 0);
}

////////////////////////////////////
//
// ATIConnectors patch
//
// bcc9's patch: http://www.insanelymac.com/forum/index.php?showtopic=249642
//

CHAR8   ATIKextBundleId[2][64]; // ATIConnectorsController's bundle IDs
UINTN   ATIKextBundleIdCount = 0;

//
// Inits patcher: prepares ATIKextBundleIds.
//
VOID
ATIConnectorsPatchInit (
  LOADER_ENTRY    *Entry
) {
  //
  // prepare bundle ids
  //

  if (Entry->KernelAndKextPatches->KPATIConnectorsController == NULL) {
    return;
  }

  AsciiSPrint (
    ATIKextBundleId[0],
    sizeof (ATIKextBundleId[0]),
    "com.apple.kext.AMD%sController",
    Entry->KernelAndKextPatches->KPATIConnectorsController
  );

  AsciiStrCpyS (
    ATIKextBundleId[1],
    sizeof (ATIKextBundleId[1]),
    "com.apple.kext.AMDFramebuffer"
  );
}

//
// Registers kexts that need force-load during WithKexts boot.
//
VOID
ATIConnectorsPatchRegisterKexts (
  FSINJECTION_PROTOCOL  *FSInject,
  FSI_STRING_LIST       *ForceLoadKexts,
  LOADER_ENTRY          *Entry
) {
  CHAR16  *AtiForceLoadKexts[] = {
            L"\\AMDFramebuffer.kext\\Contents\\Info.plist",
            L"\\AMDSupport.kext\\Contents\\Info.plist",
            L"\\AppleGraphicsControl.kext\\Contents\\PlugIns\\AppleGraphicsDeviceControl.kext\\Info.plist"
            L"\\AppleGraphicsControl.kext\\Info.plist",
            L"\\IOGraphicsFamily.kext\\Info.plist",
          };
  UINTN   i = 0, AtiForceLoadKextsCount = ARRAY_SIZE (AtiForceLoadKexts);

  // for future?
  FSInject->AddStringToList (
              ForceLoadKexts,
              PoolPrint (L"\\AMD%sController.kext\\Contents\\Info.plist", Entry->KernelAndKextPatches->KPATIConnectorsController)
            );

  // dependencies
  while (i < AtiForceLoadKextsCount) {
    FSInject->AddStringToList (ForceLoadKexts, AtiForceLoadKexts[i++]);
  }
}

//
// Patch function.
//
VOID
ATIConnectorsPatch (
  UINT8         *Driver,
  UINT32        DriverSize,
  LOADER_ENTRY  *Entry
) {
  UINTN   Num = 0;

  DBG ("ATIConnectorsPatch: driverAddr = %x, driverSize = %x | Controller = %s",
         Driver, DriverSize, Entry->KernelAndKextPatches->KPATIConnectorsController);

  // patch
  Num = SearchAndReplace (
          Driver,
          DriverSize,
          Entry->KernelAndKextPatches->KPATIConnectorsData,
          Entry->KernelAndKextPatches->KPATIConnectorsDataLen,
          Entry->KernelAndKextPatches->KPATIConnectorsPatch,
          Entry->KernelAndKextPatches->KPATIConnectorsWildcard,
          1
        );

  if (Num > 0) {
    DBG (" | patched %d times!\n", Num);
  } else {
    DBG (" | NOT patched!\n");
  }
}

VOID
GetTextSegment (
  IN  UINT8         *binary,
  OUT UINT32        *Addr,
  OUT UINT32        *Size,
  OUT UINT32        *Off
) {
  struct  load_command        *LoadCommand;
  struct  segment_command_64  *SegCmd64;
  struct  section_64          *Sect64;
          UINT32              NCmds, CmdSize, BinaryIndex, SectionIndex;
          UINTN               i;
          BOOLEAN             IsTextSegment = FALSE, Found = FALSE;

  if (MACH_GET_MAGIC (binary) != MH_MAGIC_64) {
    return;
  }

  BinaryIndex = sizeof (struct mach_header_64);

  NCmds = MACH_GET_NCMDS (binary);

  *Size = 0;

  for (i = 0; i < NCmds; i++) {
    LoadCommand = (struct load_command *)(binary + BinaryIndex);

    if (LoadCommand->cmd != LC_SEGMENT_64) {
      continue;
    }

    CmdSize = LoadCommand->cmdsize;
    SegCmd64 = (struct segment_command_64 *)LoadCommand;
    SectionIndex = sizeof (struct segment_command_64);

    while (SectionIndex < SegCmd64->cmdsize) {
      Sect64 = (struct section_64 *)((UINT8 *)SegCmd64 + SectionIndex);
      SectionIndex += sizeof (struct section_64);

      IsTextSegment = (AsciiStrCmp (Sect64->segname, kTextSegment) == 0);

      if (
        IsTextSegment &&
        (Sect64->size > 0)
      ) {
        if (!Found) {
          *Addr = (UINT32)Sect64->addr;
          *Off = Sect64->offset;
          Found = TRUE;
        }

        *Size += (UINT32)Sect64->size;
      }

      if (!IsTextSegment && Found) {
        //DBG ("%a, %a address 0x%x\n", kTextSegment, kTextTextSection, Off);
        break;
      }
    }

    BinaryIndex += CmdSize;
  }
}

////////////////////////////////////
//
// AsusAICPUPM patch
//
// fLaked's SpeedStepper patch for Asus (and some other) boards:
// http://www.insanelymac.com/forum/index.php?showtopic=258611
//
// Credits: Samantha/RevoGirl/DHP
// http://www.insanelymac.com/forum/topic/253642-dsdt-for-asus-p8p67-m-pro/page__st__200#entry1681099
//
VOID
AICPUPowerManagementPatch (
  UINT8     *Driver,
  UINT32    DriverSize
) {
  UINTN   Num;
  UINT32  Addr = 0, Size = 0, Off = 0;
  UINT8   Search[]  = { 0x20, 0xB9, 0xE2, 0x00, 0x00, 0x00, 0x0F, 0x30 },
          Replace[] = { 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0x90, 0x90 };

  GetTextSegment (Driver, &Addr, &Size, &Off);

  if (Off && Size) {
    Driver += Off;
    DriverSize = Size;
  }

  Num = SearchAndReplace (Driver, DriverSize, &Search[0], ARRAY_SIZE (Search), &Replace[0], 0xCC, -1);

  MsgLog (
    "- %a | Addr = 0x%x, Size = %d | BinPatch | %r: %d replaces done\n",
    kPropCFBundleIdentifierAICPUPM,
    Driver,
    DriverSize,
    Num ? EFI_SUCCESS : EFI_NOT_FOUND,
    Num
  );
}

////////////////////////////////////
//
// Place other kext patches here
//

// ...

////////////////////////////////////
//
// Generic kext patch functions
//
//

VOID
AnyKextPatch (
  CHAR8         *BundleIdentifier,
  UINT8         *Driver,
  UINT32        DriverSize,
  CHAR8         *InfoPlist,
  UINT32        InfoPlistSize,
  KEXT_PATCH    *KextPatch
) {
  UINTN   Num = 0;

  MsgLog ("- %a (%a) | Addr = 0x%x, Size = %d",
         BundleIdentifier, KextPatch->Label, Driver, DriverSize);

  if (KextPatch->IsPlistPatch) { // Info plist patch
    MsgLog (" | PlistPatch");

    Num = SearchAndReplaceTxt (
            (UINT8 *)InfoPlist,
            InfoPlistSize,
            KextPatch->Data,
            KextPatch->DataLen,
            KextPatch->Patch,
            KextPatch->Wildcard,
            KextPatch->Count
          );
  } else { // kext binary patch
    UINT32  Addr = 0, Size = 0, Off = 0;;

    GetTextSegment (Driver, &Addr, &Size, &Off);

    MsgLog (" | BinPatch");

    if (Off && Size) {
      Driver += Off;
      DriverSize = Size;
    }

    Num = SearchAndReplace (
            Driver,
            DriverSize,
            KextPatch->Data,
            KextPatch->DataLen,
            KextPatch->Patch,
            KextPatch->Wildcard,
            KextPatch->Count
          );
  }

  MsgLog (" | %r: %d replaces done\n", Num ? EFI_SUCCESS : EFI_NOT_FOUND, Num);
}

//
// Called from SetFSInjection (), before boot.efi is started,
// to allow patchers to prepare FSInject to force load needed kexts.
//
VOID
KextPatcherRegisterKexts (
  FSINJECTION_PROTOCOL    *FSInject,
  FSI_STRING_LIST         *ForceLoadKexts,
  LOADER_ENTRY            *Entry
) {
  UINTN   i;

  if (Entry->KernelAndKextPatches->KPATIConnectorsController != NULL) {
    ATIConnectorsPatchRegisterKexts (FSInject, ForceLoadKexts, Entry);
  }

  // Not sure about this. NrKexts / NrForceKexts, what purposes?
  for (i = 0; i < Entry->KernelAndKextPatches->/* NrKexts */NrForceKexts; i++) {
    FSInject->AddStringToList (
                ForceLoadKexts,
                //PoolPrint (
                //  L"\\%a.kext\\Contents\\Info.plist",
                //    Entry->KernelAndKextPatches->KextPatches[i].Filename
                //      ? Entry->KernelAndKextPatches->KextPatches[i].Filename
                //      : Entry->KernelAndKextPatches->KextPatches[i].Name
                //)
                PoolPrint (L"\\%a\\Contents\\Info.plist", Entry->KernelAndKextPatches->ForceKexts[i])
              );
  }
}

//
// PatchKext is called for every kext from prelinked kernel (kernelcache) or from DevTree (booting with drivers).
// Add kext detection code here and call kext specific patch function.
//
VOID
PatchKext (
  UINT8         *Driver,
  UINT32        DriverSize,
  CHAR8         *InfoPlist,
  UINT32        InfoPlistSize,
  CHAR8         *BundleIdentifier,
  LOADER_ENTRY  *Entry
) {
  INT32  i, IsBundle = 0;

  //
  // ATIConnectors
  //

  if (
    (Entry->KernelAndKextPatches->KPATIConnectorsController != NULL) &&
    (
      IsPatchNameMatch (BundleIdentifier, ATIKextBundleId[0], NULL, &IsBundle) ||
      IsPatchNameMatch (BundleIdentifier, ATIKextBundleId[1], NULL, &IsBundle)
    )
  ) {
    ATIConnectorsPatch (Driver, DriverSize, Entry);

    if (++ATIKextBundleIdCount == ARRAY_SIZE (ATIKextBundleId)) {
      FreePool (Entry->KernelAndKextPatches->KPATIConnectorsController);
      Entry->KernelAndKextPatches->KPATIConnectorsController = NULL;

      if (Entry->KernelAndKextPatches->KPATIConnectorsData != NULL) {
        FreePool (Entry->KernelAndKextPatches->KPATIConnectorsData);
      }

      if (Entry->KernelAndKextPatches->KPATIConnectorsPatch != NULL) {
        FreePool (Entry->KernelAndKextPatches->KPATIConnectorsPatch);
      }
    }

  //
  // AICPUPM
  //

  } else if (
    Entry->KernelAndKextPatches->KPKernelPm &&
    IsPatchNameMatch (BundleIdentifier, kPropCFBundleIdentifierAICPUPM, NULL, &IsBundle)
  ) {
    AICPUPowerManagementPatch (Driver, DriverSize);
    Entry->KernelAndKextPatches->KPKernelPm = FALSE;

  //
  // Others
  //

  } else {
    for (i = 0; i < Entry->KernelAndKextPatches->NrKexts; i++) {
      if (
        Entry->KernelAndKextPatches->KextPatches[i].Patched ||
        Entry->KernelAndKextPatches->KextPatches[i].Disabled || // avoid redundant: if unique / IsBundle
        !IsPatchNameMatch (BundleIdentifier, Entry->KernelAndKextPatches->KextPatches[i].Name, InfoPlist, &IsBundle)
      ) {
        continue;
      }

      AnyKextPatch (
        BundleIdentifier,
        Driver,
        DriverSize,
        InfoPlist,
        InfoPlistSize,
        &Entry->KernelAndKextPatches->KextPatches[i]
      );

      if (IsBundle) {
        Entry->KernelAndKextPatches->KextPatches[i].Patched = TRUE;
      }
    }
  }
}

///////////////////////////////////
//
// Detect FakeSMC (on prelinked / injected InfoPlist), warn users OnExitBootServices if not presented in verbose.
//

VOID
CheckForFakeSMC (
  CHAR8           *InfoPlist,
  LOADER_ENTRY    *Entry
) {
  if (
    !gSettings.FakeSMCLoaded &&
    BIT_ISSET (Entry->Flags, OSFLAG_CHECKFAKESMC)/* &&
    BIT_ISSET (Entry->Flags, OSFLAG_WITHKEXTS)*/
  ) {
    if (
      (AsciiStrStr (InfoPlist, "<string>org.netkas.driver.FakeSMC</string>") != NULL) ||
      (AsciiStrStr (InfoPlist, "<string>org.netkas.FakeSMC</string>") != NULL)
    ) {
      //Entry->Flags = BIT_UNSET (Entry->Flags, OSFLAG_WITHKEXTS);
      gSettings.FakeSMCLoaded = TRUE;
      //DBG ("FakeSMC found\n");
    }
  }
}

/** Extracts kext BundleIdentifier from given Plist into KextBundleIdentifier */

VOID
ExtractKextPropString (
  OUT CHAR8   *Res,
  IN  INTN    Len,
  IN  CHAR8   *Key,
  IN  CHAR8   *Plist
) {
  CHAR8     *Tag, *BIStart, *BIEnd;
  UINTN     DictLevel = 0, KeyLen = AsciiStrLen (Key);

  Res[0] = '\0';

  // start with first <dict>
  Tag = AsciiStrStr (Plist, "<dict>");
  if (Tag == NULL) {
    return;
  }

  Tag += 6;
  DictLevel++;

  while (*Tag != '\0') {
    if (AsciiStrnCmp (Tag, "<dict>", 6) == 0) {
      // opening dict
      DictLevel++;
      Tag += 6;
    } else if (AsciiStrnCmp (Tag, "</dict>", 7) == 0) {
      // closing dict
      DictLevel--;
      Tag += 7;
    } else if ((DictLevel == 1) && (AsciiStrnCmp (Tag, Key, KeyLen) == 0)) {
      // StringValue is next <string>...</string>
      BIStart = AsciiStrStr (Tag + KeyLen, "<string>");
      if (BIStart != NULL) {
        BIStart += 8; // skip "<string>"
        BIEnd = AsciiStrStr (BIStart, "</string>");
        if ((BIEnd != NULL) && ((BIEnd - BIStart + 1) < Len)) {
          CopyMem (Res, BIStart, BIEnd - BIStart);
          Res[BIEnd - BIStart] = '\0';
          break;
        }
      }

      Tag++;
    } else {
      Tag++;
    }

    // advance to next tag
    while ((*Tag != '<') && (*Tag != '\0')) {
      Tag++;
    }
  }
}

//
// Prevent prelinked kext being loaded by kernel:
//
// [+] Kernel will refuse to load prelinked kext with invalid / inexistence "CFBundleIdentifier".
//     By simply renaming "CFBundleIdentifier" key to something invalid (ex: "_FBundleIdentifier")
//     will feed this purpose.
// [-] Zeroing prelinked kext size (in "_PrelinkExecutableSize") will also work, with some extra risks:
//     Prelinked kext entry with ID / IDREF attribute will potentially screw up other entries
//     refer to / by those ID, which totally bad / unnecessary.
//

VOID
BlockListedKextCaches (
  CHAR8   *Plist
) {
  CHAR8   *Value;

  Value = AsciiStrStr (Plist, PropCFBundleIdentifierKey);
  if (Value == NULL) {
    return;
  }

  Value += 5; // skip "<key>"
  *(Value) = '_'; // apply prefix
}

BOOLEAN
IsKextInBlockCachesList (
  CHAR8   *KextBundleIdentifier
) {
  BOOLEAN   Ret = FALSE;

  if (gSettings.BlockKextCachesCount) {
    INT32  i, IsBundle = 0;

    for (i = 0; i < gSettings.BlockKextCachesCount; i++) {
      if (IsPatchNameMatch (gSettings.BlockKextCaches[i], KextBundleIdentifier, KextBundleIdentifier, &IsBundle)) {
        Ret = TRUE;
        break;
      }
    }
  }

  return Ret;
}

//
// Iterates over kexts in kernelcache
// and calls PatchKext () for each.
//
// PrelinkInfo section contains following plist, without spaces:
// <dict>
//   <key>_PrelinkInfoDictionary</key>
//   <array>
//     <!-- start of kext Info.plist -->
//     <dict>
//       <key>CFBundleName</key>
//       <string>MAC Framework Pseudoextension</string>
//       <key>_PrelinkExecutableLoadAddr</key>
//       <integer size="64">0xffffff7f8072f000</integer>
//       <!-- Kext size -->
//       <key>_PrelinkExecutableSize</key>
//       <integer size="64">0x3d0</integer>
//       <!-- Kext address -->
//       <key>_PrelinkExecutableSourceAddr</key>
//       <integer size="64">0xffffff80009a3000</integer>
//       ...
//     </dict>
//     <!-- start of next kext Info.plist -->
//     <dict>
//       ...
//     </dict>
//       ...

#ifdef LAZY_PARSE_KEXT_PLIST

EFI_STATUS
ParsePrelinkKexts (
  CHAR8   *WholePlist
) {
  TagPtr        KextsDict, DictPointer;
  EFI_STATUS    Status = ParseXML (WholePlist, 0, &KextsDict);

  //DBG ("Using LAZY_PARSE_KEXT_PLIST\n")

  if (EFI_ERROR (Status)) {
    KextsDict = NULL;
  } else {
    DictPointer = GetProperty (KextsDict, kPrelinkInfoDictionaryKey);
    if ((DictPointer != NULL) && (DictPointer->type == kTagTypeArray)) {
      INTN    Count = DictPointer->size, i = 0,
              iPrelinkExecutableSourceKey, iPrelinkExecutableSourceKeySize, //KextAddr
              iPrelinkExecutableSizeKey, iPrelinkExecutableSizeKeySize; //KextSize
      TagPtr  Dict, Prop;
      CHAR8   *sPrelinkExecutableSourceKey, *sPrelinkExecutableSizeKey;

      for (i = 0; i < Count; ++i) {
        iPrelinkExecutableSourceKey = iPrelinkExecutableSourceKeySize = 0;
        iPrelinkExecutableSizeKey = iPrelinkExecutableSizeKeySize = 0;

        Status = GetElement (DictPointer, i, Count, &Dict);

        if (
          EFI_ERROR (Status) ||
          (Dict == NULL) ||
          !Dict->offset ||
          !Dict->taglen
        ) {
          //DBG ("%d: %r parsing / empty element\n", i, Status);
          continue;
        }

        Prop = GetProperty (Dict, kPrelinkExecutableSourceKey);
        if (
          (Prop == NULL) ||
          (
            (Prop->ref != -1) &&
            EFI_ERROR (
              GetRefInteger (
                KextsDict,
                Prop->ref,
                &sPrelinkExecutableSourceKey,
                &iPrelinkExecutableSourceKey,
                &iPrelinkExecutableSourceKeySize
              )
            )
          )
        ) {
          continue;
        } else if (!iPrelinkExecutableSourceKey && Prop->integer) {
          iPrelinkExecutableSourceKey = Prop->integer;
        }

        if (!iPrelinkExecutableSourceKey) {
          continue;
        }

        Prop = GetProperty (Dict, kPrelinkExecutableSizeKey);
        if (
          (Prop == NULL) ||
          (
            (Prop->ref != -1) &&
            EFI_ERROR (
              GetRefInteger (
                KextsDict,
                Prop->ref,
                &sPrelinkExecutableSizeKey,
                &iPrelinkExecutableSizeKey,
                &iPrelinkExecutableSizeKeySize
              )
            )
          )
        ) {
          continue;
        } else if (!iPrelinkExecutableSizeKey && Prop->integer) {
          iPrelinkExecutableSizeKey = Prop->integer;
        }

        if (!iPrelinkExecutableSizeKey) {
          continue;
        }

        Prop = GetProperty (Dict, kPropCFBundleIdentifier);
        if ((Prop != NULL) && (Prop->type == kTagTypeString)) {
          CHAR8   *InfoPlist = WholePlist + Dict->offset, SavedValue;

          SavedValue = InfoPlist[Dict->taglen];
          InfoPlist[Dict->taglen] = '\0';

          if (IsKextInBlockCachesList (Prop->string)) {
            DBG ("Blocking KextCaches: %a\n", Prop->string);
            BlockListedKextCaches (InfoPlist);
          } else {
            // To speed up process sure we can apply all patches here immediately.
            // By saving all kexts data into list could be useful for other purposes, I hope.
            PRELINKKEXTLIST   *nKext = AllocateZeroPool (sizeof (PRELINKKEXTLIST));

            if (nKext) {
              nKext->Signature = PRELINKKEXTLIST_SIGNATURE;
              nKext->BundleIdentifier = AllocateCopyPool (AsciiStrSize (Prop->string), Prop->string);
              nKext->Address = iPrelinkExecutableSourceKey;
              nKext->Size = iPrelinkExecutableSizeKey;
              nKext->Offset = Dict->offset;
              nKext->Taglen = Dict->taglen;
              InsertTailList (&gPrelinkKextList, (LIST_ENTRY *)(((UINT8 *)nKext) + OFFSET_OF (PRELINKKEXTLIST, Link)));
            }
          }

          InfoPlist[Dict->taglen] = SavedValue;
        }
      }

      //DBG ("PRELINKKEXTLIST Count %d\n", Count);
    } else {
      DBG ("NO kPrelinkInfoDictionaryKey\n");
    }
  }

  return IsListEmpty (&gPrelinkKextList) ? EFI_UNSUPPORTED : EFI_SUCCESS;
}

VOID
PatchPrelinkedKexts (
  LOADER_ENTRY    *Entry
) {
  CHAR8   *WholePlist = (CHAR8 *)(UINTN)KernelInfo->PrelinkInfoAddr;

  CheckForFakeSMC (WholePlist, Entry);

  if (!EFI_ERROR (ParsePrelinkKexts (WholePlist))) {
    LIST_ENTRY    *Link;

    for (Link = gPrelinkKextList.ForwardLink; Link != &gPrelinkKextList; Link = Link->ForwardLink) {
      PRELINKKEXTLIST   *sKext = CR (Link, PRELINKKEXTLIST, Link, PRELINKKEXTLIST_SIGNATURE);
      CHAR8             *InfoPlist = AllocateCopyPool (sKext->Taglen, WholePlist + sKext->Offset);

      //UINT32            kextBinAddress = (UINT32)sKext->Address - KernelInfo->PrelinkTextAddr + KernelInfo->PrelinkTextOff; // meklort
      //UINT32            kextBinAddress = (UINT32)sKext->Address + KernelInfo->Slide + (UINT32)KernelInfo->RelocBase;

      InfoPlist[sKext->Taglen] = '\0';

      // patch it
      PatchKext (
        (UINT8 *)(UINTN) (
            // get kext address from _PrelinkExecutableSourceAddr
            // truncate to 32 bit to get physical addr
            (UINT32)sKext->Address +
            // KextAddr is always relative to 0x200000
            // and if KernelSlide is != 0 then KextAddr must be adjusted
            KernelInfo->Slide +
            // and adjust for AptioFixDrv's KernelRelocBase
            (UINT32)KernelInfo->RelocBase /* kextBinAddress */
          ),
        (UINT32)sKext->Size,
        InfoPlist,
        (UINT32)sKext->Taglen,
        sKext->BundleIdentifier,
        Entry
      );

      FreePool (InfoPlist);
    }
  }
}

#else

//
// Returns parsed hex integer key.
// Plist - kext pist
// Key - key to find
// WholePlist - _PrelinkInfoDictionary, used to find referenced values
//
// Searches for Key in Plist and it's value:
// a) <integer ID="26" size="64">0x2b000</integer>
//    returns 0x2b000
// b) <integer IDREF="26"/>
//    searches for <integer ID="26"... from WholePlist
//    and returns value from that referenced field
//
// Whole function is here since we should avoid ParseXML () and it's
// memory allocations during ExitBootServices (). And it seems that
// ParseXML () does not support IDREF.
// This func is hard to read and debug and probably not reliable,
// but it seems it works.
//
STATIC
UINT64
GetPlistHexValue (
  CHAR8     *Plist,
  CHAR8     *Key,
  CHAR8     *WholePlist
) {
  UINT64    NumValue = 0;
  CHAR8     *Value, *IntTag, *IDStart, *IDEnd, Buffer[48];
  UINTN     IDLen, BufferLen = ARRAY_SIZE (Buffer);

  // search for Key
  Value = AsciiStrStr (Plist, Key);
  if (Value == NULL) {
    return 0;
  }

  // search for <integer
  IntTag = AsciiStrStr (Value, "<integer");
  if (IntTag == NULL) {
    return 0;
  }

  // find <integer end
  Value = AsciiStrStr (IntTag, ">");
  if (Value == NULL) {
    return 0;
  }

  if (Value[-1] != '/') {
    // normal case: value is here
    NumValue = AsciiStrHexToUint64 (Value + 1);
    return NumValue;
  }

  // it might be a reference: IDREF="173"/>
  Value = AsciiStrStr (IntTag, "<integer IDREF=\"");
  if (Value != IntTag) {
    return 0;
  }

  // compose <integer ID="xxx" in the Buffer
  IDStart = AsciiStrStr (IntTag, "\"") + 1;
  IDEnd = AsciiStrStr (IDStart, "\"");
  IDLen = IDEnd - IDStart;

  if (IDLen > 8) {
    return 0;
  }

  AsciiStrCpyS (Buffer, BufferLen, "<integer ID=\"");
  AsciiStrnCatS (Buffer, BufferLen, IDStart, IDLen);
  AsciiStrCatS (Buffer, BufferLen, "\"");

  // and search whole plist for ID
  IntTag = AsciiStrStr (WholePlist, Buffer);
  if (IntTag == NULL) {
    return 0;
  }

  Value = AsciiStrStr (IntTag, ">");
  if (Value == NULL) {
    return 0;
  }

  if (Value[-1] == '/') {
    return 0;
  }

  // we should have value now
  NumValue = AsciiStrHexToUint64 (Value + 1);

  return NumValue;
}

VOID
PatchPrelinkedKexts (
  LOADER_ENTRY    *Entry
) {
  CHAR8     *WholePlist, *DictPtr, *InfoPlistStart = NULL,
            *InfoPlistEnd = NULL, SavedValue;
  INTN      DictLevel = 0;
  UINT32    KextAddr, KextSize;

  WholePlist = (CHAR8 *)(UINTN)KernelInfo->PrelinkInfoAddr;

  //
  // Detect FakeSMC and if present then
  // disable kext injection InjectKexts ().
  // There is some bug in the folowing code that
  // searches for individual kexts in prelink info
  // and FakeSMC is not found on my SnowLeo although
  // it is present in kernelcache.
  // But searching through the whole prelink info
  // works and that's the reason why it is here.
  //

  CheckForFakeSMC (WholePlist, Entry);

  DictPtr = WholePlist;

  while ((DictPtr = AsciiStrStr (DictPtr, "dict>")) != NULL) {
    if (DictPtr[-1] == '<') {
      // opening dict
      DictLevel++;
      if (DictLevel == 2) {
        // kext start
        InfoPlistStart = DictPtr - 1;
      }

    } else if ((DictPtr[-2] == '<') && (DictPtr[-1] == '/')) {
      // closing dict
      if ((DictLevel == 2) && (InfoPlistStart != NULL)) {
        CHAR8   KextBundleIdentifier[AVALUE_MAX_SIZE];

        // kext end
        InfoPlistEnd = DictPtr + 5 /* "dict>" */;

        // terminate Info.plist with 0
        SavedValue = *InfoPlistEnd;
        *InfoPlistEnd = '\0';

        ExtractKextPropString (KextBundleIdentifier, ARRAY_SIZE (KextBundleIdentifier), PropCFBundleIdentifierKey, InfoPlistStart);

        if (IsKextInBlockCachesList (KextBundleIdentifier)) {
          DBG ("Blocking KextCaches: %a\n", KextBundleIdentifier);
          BlockListedKextCaches (InfoPlistStart);
          goto Next;
        }

        // get kext address from _PrelinkExecutableSourceAddr
        // truncate to 32 bit to get physical addr
        KextAddr = (UINT32)GetPlistHexValue (InfoPlistStart, kPrelinkExecutableSourceKey, WholePlist);
        // KextAddr is always relative to 0x200000
        // and if KernelSlide is != 0 then KextAddr must be adjusted
        KextAddr += KernelInfo->Slide;
        // and adjust for AptioFixDrv's KernelRelocBase
        KextAddr += (UINT32)KernelInfo->RelocBase;

        KextSize = (UINT32)GetPlistHexValue (InfoPlistStart, kPrelinkExecutableSizeKey, WholePlist);

        // patch it
        PatchKext (
          (UINT8 *)(UINTN)KextAddr,
          KextSize,
          InfoPlistStart,
          (UINT32)(InfoPlistEnd - InfoPlistStart),
          KextBundleIdentifier,
          Entry
        );

        Next:

        // return saved char
        *InfoPlistEnd = SavedValue;
      }

      DictLevel--;
    }

    DictPtr += 5;
  }
}

#endif

//
// Iterates over kexts loaded by booter
// and calls PatchKext () for each.
//
VOID
PatchLoadedKexts (
  LOADER_ENTRY    *Entry
) {
          CHAR8                       *PropName, SavedValue, *InfoPlist;
          DTEntry                     MMEntry;
          BooterKextFileInfo          *KextFileInfo;
          DeviceTreeBuffer            *PropEntry;
  struct  OpaqueDTPropertyIterator    OPropIter;
          DTPropertyIterator          PropIter = &OPropIter;

  if (!gDtRoot) {
    return;
  }

  DTInit (gDtRoot);

  if (
    (DTLookupEntry (NULL,"/chosen/memory-map", &MMEntry) == kSuccess) &&
    (DTCreatePropertyIteratorNoAlloc (MMEntry, PropIter) == kSuccess)
  ) {
    while (DTIterateProperties (PropIter, &PropName) == kSuccess) {
      //DBG ("Prop: %a\n", PropName);
      if (AsciiStrStr (PropName, BOOTER_KEXT_PREFIX)) {
#ifdef LAZY_PARSE_KEXT_PLIST
        TagPtr    KextsDict, Dict;
#else
        CHAR8   KextBundleIdentifier[AVALUE_MAX_SIZE];
#endif

        // PropEntry DeviceTreeBuffer is the value of Driver-XXXXXX property
        PropEntry = (DeviceTreeBuffer *)(((UINT8 *)PropIter->currentProperty) + sizeof (DeviceTreeNodeProperty));

        // PropEntry->paddr points to BooterKextFileInfo
        KextFileInfo = (BooterKextFileInfo *)(UINTN)PropEntry->paddr;

        // Info.plist should be terminated with 0, but will also do it just in case
        InfoPlist = (CHAR8 *)(UINTN)KextFileInfo->infoDictPhysAddr;
        SavedValue = InfoPlist[KextFileInfo->infoDictLength];
        InfoPlist[KextFileInfo->infoDictLength] = '\0';

#ifdef LAZY_PARSE_KEXT_PLIST
        // TODO: Store into list first & process all later?
        if (!EFI_ERROR (ParseXML (InfoPlist, 0, &KextsDict))) {
          Dict = GetProperty (KextsDict, kPropCFBundleIdentifier);
          if ((Dict != NULL) && (Dict->type == kTagTypeString)) {
            PatchKext (
              (UINT8 *)(UINTN)KextFileInfo->executablePhysAddr,
              KextFileInfo->executableLength,
              InfoPlist,
              KextFileInfo->infoDictLength,
              Dict->string,
              Entry
            );
          }
        }
#else
        ExtractKextPropString (KextBundleIdentifier, ARRAY_SIZE (KextBundleIdentifier), PropCFBundleIdentifierKey, InfoPlist);

        PatchKext (
          (UINT8 *)(UINTN)KextFileInfo->executablePhysAddr,
          KextFileInfo->executableLength,
          InfoPlist,
          KextFileInfo->infoDictLength,
          KextBundleIdentifier,
          Entry
        );
#endif

        CheckForFakeSMC (InfoPlist, Entry);

        InfoPlist[KextFileInfo->infoDictLength] = SavedValue;
      }
    }
  }
}

//
// Entry for all kext patches.
// Will iterate through kext in prelinked kernel (kernelcache)
// or DevTree (drivers boot) and do patches.
//
VOID
KextPatcher (
  LOADER_ENTRY    *Entry
) {
  MsgLog ("%a: Start\n", __FUNCTION__);

  ATIConnectorsPatchInit (Entry);

  if (KernelInfo->Cached) {
    MsgLog ("Patching kernelcache ...\n");
    PatchPrelinkedKexts (Entry);
  } else {
    MsgLog ("Patching loaded kexts ...\n");
    PatchLoadedKexts (Entry);
  }

  MsgLog ("%a: End\n", __FUNCTION__);
}

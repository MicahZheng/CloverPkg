/** @file

Module Name:

  FSInject.c

  FSInject driver - Replaces EFI_SIMPLE_FILE_SYSTEM_PROTOCOL on target volume
  and injects content of specified source folder on source (injection) volume
  into target folder in target volume.

  initial version - dmazar

**/

#include <Library/BaseLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DebugLib.h>
#include <Library/PrintLib.h>

#include <Library/Common/MemLogLib.h>
#include <Library/Common/CommonLib.h>

#include <Protocol/SimpleFileSystem.h>

#include <Protocol/FSInjectProtocol.h>

#include <Guid/FileInfo.h>

#include "FSInject.h"

/** Composes file name from Parent and FName. Allocates memory for result which should be released by caller. */
CHAR16 *
EFIAPI
GetNormalizedFName (
  IN CHAR16   *Parent,
  IN CHAR16   *FName
) {
  CHAR16    *TmpStr, *TmpStr2;
  UINTN     Len;

  //DBG ("NormFName ('%s' + '%s')", Parent, FName);
  // case: FName starts with \ "\System\Xx"
  // we'll just use it as is, but we are wrong if "\System\Xx\..\Yy\.\Zz" or similar
  if (FName[0] == L'\\') {
    FName = AllocateCopyPool (StrSize (FName), FName); // reusing FName
  }

  // case: FName is "."
  // we'll just copy Parent assuming Parent is normalized, which will be the case if this func will be correct once
  else if ((FName[0] == L'.') && (FName[1] == L'\0')) {
    FName = AllocateCopyPool (StrSize (Parent), Parent);
  }

  // case: FName is ".."
  // we'll extract Parent's parent - also assuming Parent is normalized
  else if ((FName[0] == L'.') && (FName[1] == L'.') && (FName[2] == L'\0')) {
    TmpStr = GetStrLastCharOccurence (Parent, L'\\');
    // if there is L'\\' and not at the beginning ...
    if (TmpStr != NULL && TmpStr != Parent) {
      *TmpStr = L'\0'; // terminating Parent; will return L'\\' back
      FName = AllocateCopyPool (StrSize (Parent), Parent);
      *TmpStr = L'\\'; // return L'\\' back to Parent
    } else {
      // caller is doing something wrong - we'll default to L"\\"
      FName = AllocateCopyPool (StrSize (L"\\"), L"\\");
    }
  }

  // other cases: for now just do Parent + \ + FName
  // but check if Parent already ends with backslash
  else {
    Len = StrSize (Parent) + StrSize (FName); // has place for extra char (\\) if needed
    TmpStr = AllocateZeroPool (Len);
    StrCpyS (TmpStr, Len, Parent);
    TmpStr2 = GetStrLastChar (Parent);

    if ((TmpStr2 == NULL) || (*TmpStr2 != L'\\')) {
      StrCatS (TmpStr, Len, L"\\");
    }

    //FName = StrCat (TmpStr, Len, FName);
    StrCatS (TmpStr, Len, FName);
    FName = AllocateCopyPool (StrSize (TmpStr), TmpStr);
  }

  //DBG ("='%s' ", FName);

  return FName;
}

/** If FName starts with TgtDir, then extracts the rest from FName and copies it to SrcDir and returns it. Or NULL.
  * Caller is responsible for releasing memory for returned string.
  * Example: TgtDir="\S\L\E", SrcDir="\efi\10.7", FName="\S\L\E\Xx.kext\Contents\Info.plist"
  * This should return "\efi\10.7\Xx.kext\Contents\Info.plist"
  */
CHAR16 *
EFIAPI
GetInjectionFName (
  IN CHAR16   *TgtDir,
  IN CHAR16   *SrcDir,
  IN CHAR16   *FName
) {
  CHAR16  Chr1, Chr2, *Str1, *Str2;
  UINTN   Size;

  DBG ("InjFName ('%s', '%s', '%s')", TgtDir, SrcDir, FName);

  if ((TgtDir == NULL) || (SrcDir == NULL) || (FName == NULL)) {
    DBG ("=NULL0 ");
    return NULL;
  }

  // check if FName starts with TgtDir, kind of case insensitive (only ASCII chars)
  Str1 = TgtDir;
  Str2 = FName;
  Chr1 = TO_UPPER (*Str1);
  Chr2 = TO_UPPER (*Str2);

  while ((*Str1 != L'\0') && (Chr1 == Chr2)) {
    Str1++;
    Str2++;
    Chr1 = TO_UPPER (*Str1);
    Chr2 = TO_UPPER (*Str2);
  }

  // if FName starts with TgtDir, then *Str1 should be '\0'
  if (*Str1 != L'\0') {
    DBG ("=NULL1 ");
    return NULL;
  }

  // if FName is a file inside TgtDir, then *Str2 should be == '\\'
  if (*Str2 != L'\\') {
    DBG ("=NULL2 ");
    return NULL;
  }

  // we are at '\\' with Str2 - copy from here to the end to SrcDir
  // determine the buffer size for new string
  Size = StrSize (SrcDir) + StrSize (Str2) - 2;
  Str1 = AllocateZeroPool (Size);

  if (Str1 != NULL) {
    StrCpyS (Str1, Size, SrcDir);
    StrCatS (Str1, Size, Str2);
  }

  DBG ("='%s' ", Str1);

  return Str1;
}

/** Openes EFI_FILE_PROTOCOL on given VolumeFS for given FName. */
EFI_FILE_PROTOCOL *
EFIAPI
OpenFileProtocol (
  IN EFI_SIMPLE_FILE_SYSTEM_PROTOCOL    *VolumeFS,
  IN CHAR16                             *FName,
  IN UINT64                             OpenMode,
  IN UINT64                             Attributes
) {
  EFI_STATUS          Status;
  EFI_FILE_PROTOCOL   *RootFP, *FP;

  // open volume
  Status = VolumeFS->OpenVolume (VolumeFS, &RootFP);
  if (EFI_ERROR (Status)) {
    DBG ("Can not OpenVolume: %r ", Status);
    return NULL;
  }

  // open file/dir
  Status = RootFP->Open (RootFP, &FP, FName, OpenMode, Attributes);
  if (EFI_ERROR (Status)) {
    DBG ("Can not Open '%s': %r ", FName, Status);
    FP = NULL;
  }

  RootFP->Close (RootFP);

  return FP;
}

/**************************************************************************************
 * FSI_FILE_PROTOCOL - our implementation of EFI_FILE_PROTOCOL
 **************************************************************************************/

BOOLEAN
EFIAPI
GetOpen (
  IN  FSI_FILE_PROTOCOL   *FSIThis,
  OUT FSI_FILE_PROTOCOL   *FSINew,
  IN  CHAR16              *FileName,
  IN  UINT64              OpenMode,
  IN  UINT64              Attributes
) {
  //EFI_STATUS    Status = EFI_DEVICE_ERROR;
  CHAR16        *InjFName;
  BOOLEAN       Ret = FALSE;

  if ((FSIThis->FSI_FS->SrcDir != NULL) && (FSIThis->FSI_FS->SrcFS != NULL)) {
    InjFName = GetInjectionFName (L"\0", FSIThis->FSI_FS->SrcDir, FileName);
    if (InjFName != NULL) {
      // if this one exists inside injection dir - should be opened with SrcFP
      FSINew->SrcFP = OpenFileProtocol (FSIThis->FSI_FS->SrcFS, InjFName, OpenMode, Attributes);
      if (FSINew->SrcFP != NULL) {
        FSINew->FromTgt = FALSE;
        //DBG ("Opened with SrcFP ");
        Ret = TRUE;
      } /*else {
        DBG (" no injection, SrcFP->Open=%r ", Status);
      }*/
    }
  }

  return Ret;
}

FSI_FILE_PROTOCOL * EFIAPI CreateFSInjectFP ();

/** EFI_FILE_PROTOCOL.Open - Opens a new file relative to the source file's location. */
EFI_STATUS
EFIAPI
FSI_FP_Open (
  IN EFI_FILE_PROTOCOL    *This,
  OUT EFI_FILE_PROTOCOL   **NewHandle,
  IN CHAR16               *FileName,
  IN UINT64               OpenMode,
  IN UINT64               Attributes
) {
  EFI_STATUS              Status = EFI_DEVICE_ERROR;
  CHAR16                  *NewFName, *InjFName = NULL;
  FSI_FILE_PROTOCOL       *FSIThis, *FSINew;
  FSI_STRING_LIST         *StringList;
  FSI_STRING_LIST_ENTRY   *StringEntry;

  DBG ("FSI_FP %p.Open ('%s', %x, %x) ", This, FileName, OpenMode, Attributes);

  FSIThis = FSI_FROM_FILE_PROTOCOL (This);
  NewFName = GetNormalizedFName (FSIThis->FName, FileName);

  StringList = FSIThis->FSI_FS->Blacklist;
  if ((StringList != NULL) && !IsListEmpty (&StringList->List)) {
    // blocking files in Blacklist
    for (
      StringEntry = (FSI_STRING_LIST_ENTRY *)GetFirstNode (&StringList->List);
      !IsNull (&StringList->List, &StringEntry->List);
      StringEntry = (FSI_STRING_LIST_ENTRY *)GetNextNode (&StringList->List, &StringEntry->List)
    ) {
      if (StriStartsWith (NewFName, StringEntry->String)) {
        DBG ("Blacklisted\n");
        return EFI_NOT_FOUND;
      }
    }
  }

  // create our FP implementation
  FSINew = CreateFSInjectFP ();

  if (FSINew == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    DBG ("CreateFSInjectFP Status=%r\n", Status);
    return Status;
  }

  FSINew->FSI_FS = FSIThis->FSI_FS;   // saving reference to parent FS protocol
  FSINew->FName =NewFName;
  FSINew->TgtFP = NULL;
  FSINew->SrcFP = NULL;

  // mach_kernel - if exists in SrcDir, then inject this one
  if (StriCmp (NewFName, L"\\mach_kernel") == 0) {
    DBG ("mach_kernel ");

    if (GetOpen (FSIThis, FSINew, NewFName, OpenMode, Attributes)) {
      goto SuccessExit;
    }
  }

  // S/L/Kernels/kernel - if exists in SrcDir, then inject this one
  else if (StriCmp (NewFName, L"\\System\\Library\\Kernels\\kernel") == 0) {
    DBG ("kernel ");

    if (
      GetOpen (FSIThis, FSINew, L"\\kernel", OpenMode, Attributes) ||
      GetOpen (FSIThis, FSINew, L"\\mach_kernel", OpenMode, Attributes)
    ) {
      goto SuccessExit;
    }
  }

  // try with target
  if (FSIThis->TgtFP != NULL) {
    // call original target implementation to get NewHandle
    Status = FSIThis->TgtFP->Open (FSIThis->TgtFP, NewHandle, NewFName, OpenMode, Attributes);
    if (EFI_ERROR (Status)) {
      DBG ("TgtFP->Open=%r ", Status);
    } else {
      DBG ("Opened with TgtFP ");
      FSINew->FromTgt = TRUE;
      // save new orig target handle
      FSINew->TgtFP = *NewHandle;
    }
  }

  // if write protected - return now
  if (Status == EFI_WRITE_PROTECTED) {
    goto ErrorExit;
  }

  // handle injection if needed
  if (EFI_ERROR (Status) && (FSIThis->FSI_FS->SrcDir != NULL) && (FSIThis->FSI_FS->SrcFS != NULL)) {
    // if not found and injection requested: try injection dir
    InjFName = GetInjectionFName (FSIThis->FSI_FS->TgtDir, FSIThis->FSI_FS->SrcDir, NewFName);

    if (InjFName != NULL) {
      // this one exists inside injection dir - should be opened with SrcFP
      FSINew->FromTgt = FALSE;
      FSINew->SrcFP = OpenFileProtocol (FSIThis->FSI_FS->SrcFS, InjFName, OpenMode, Attributes);

      if (FSINew->SrcFP == NULL) {
        Status = EFI_DEVICE_ERROR;
        DBG ("SrcFP->Open=%r ", Status);
      } else {
        UINT8   KextsInjected = 1;
        UINTN   DataSize = 0;

        DBG ("Opened with SrcFP ");

        // ok - we are injecting kexts with FSInject driver because user requested
        // it with NoCaches or because boot.efi refused to load kernelcache for some reason.
        // let's inform Clover's kext injection that it does not have to do
        // in-memory kext injection.
        //if (KextsInjected == 0) {
        Status = gRT->GetVariable (L"FSInject.KextsInjected", &gEfiGlobalVariableGuid, NULL, &DataSize, NULL);
        if (Status != EFI_BUFFER_TOO_SMALL) {
          gRT->SetVariable (
                  L"FSInject.KextsInjected",
                  &gEfiGlobalVariableGuid,
                  EFI_VARIABLE_BOOTSERVICE_ACCESS,
                  sizeof (KextsInjected),
                  &KextsInjected
                );

          //AsciiPrint ("\nFSInject.KextsInjected\n");
          //gBS->Stall (3000000);
        }

        //KextsInjected++;
        Status = EFI_SUCCESS;
      }
    }

    if (EFI_ERROR (Status) && (FSIThis->TgtFP == NULL)) {
      // this happens when we are called on FP that is opened with SrcFP (we do not have TgtFP)
      // and then with FName ".." (in shell), and when resulting dir in actually on TgtFP.
      // need to open it with TgtFP
      FSINew->TgtFP = OpenFileProtocol (FSIThis->FSI_FS->TgtFS, NewFName, OpenMode, Attributes);

      if (FSINew->TgtFP != NULL) {
        Status = EFI_SUCCESS;
        DBG ("Opened with TgtFP ");
        FSINew->FromTgt = TRUE;
      } else {
        Status = EFI_DEVICE_ERROR;
        DBG ("TgtFS->OpenVolume Status=%r\n", Status);
      }
    }
  }

  // if still error - quit
  if (EFI_ERROR (Status)) {
    goto ErrorExit;
  }

  // we are here with EFI_SUCCESS

  // check if this is injection point (target dir where we should inject)
  if (
    (FSINew->TgtFP != NULL) &&
    (FSINew->FSI_FS->SrcFS != NULL) &&
    (FSINew->FSI_FS->SrcDir != NULL) &&
    (StriCmp (FSINew->FSI_FS->TgtDir, FSINew->FName) == 0)
  ) {
    // it is - open injection dir also
    // this FP will have both TgtFP and SrcFP - can be used for test later
    // in case of error, it will be NULL - all should run fine then, but without injection
    FSINew->SrcFP = OpenFileProtocol (FSINew->FSI_FS->SrcFS, FSINew->FSI_FS->SrcDir, EFI_FILE_MODE_READ, 0);

    if (FSINew->SrcFP != NULL) {
      DBG ("Opened also with SrcFP ");
    } else {
      DBG ("Error opening with SrcFP ");
    }
  }

SuccessExit:

  // set our implementation as a result
  *NewHandle = &(FSINew->FP);

  DBG ("= EFI_SUCCESS, NewHandle=%p, FName='%s'\n", *NewHandle, FSINew->FName);

  return EFI_SUCCESS;

ErrorExit:

  if (FSINew->FName != NULL) {
    FreePool (FSINew->FName);
  }

  if (FSINew != NULL) {
    FreePool (FSINew);
  }

  DBG ("= %r\n", Status);

  return Status;
}

/** EFI_FILE_PROTOCOL.Close - Closes a specified file handle. */
EFI_STATUS
EFIAPI
FSI_FP_Close (
  IN EFI_FILE_PROTOCOL  *This
) {
  EFI_STATUS          Status = EFI_SUCCESS;
  FSI_FILE_PROTOCOL   *FSIThis;

  DBG ("FSI_FP %p.Close () ", This);

  FSIThis = FSI_FROM_FILE_PROTOCOL (This);

  if (FSIThis->TgtFP != NULL) {
    // close target FP
    Status = FSIThis->TgtFP->Close (FSIThis->TgtFP);
    FSIThis->TgtFP = NULL;
  }

  if (FSIThis->SrcFP != NULL) {
    // close source (injection) FP
    FSIThis->SrcFP->Close (FSIThis->SrcFP);
    FSIThis->SrcFP = NULL;
  }

  DBG ("FName='%s' ", FSIThis->FName);

  if (FSIThis->FName != NULL) {
    FreePool (FSIThis->FName);
    FSIThis->FName = NULL;
  }

  FreePool (FSIThis);

  DBG ("= %r\n", Status);

  return Status;
}

/** EFI_FILE_PROTOCOL.Delete - Close and delete the file handle. */
EFI_STATUS
EFIAPI
FSI_FP_Delete (
  IN EFI_FILE_PROTOCOL  *This
) {
  EFI_STATUS          Status = EFI_WARN_DELETE_FAILURE;
  FSI_FILE_PROTOCOL   *FSIThis;

  DBG ("FSI_FP %p.Delete ()\n", This);

  FSIThis = FSI_FROM_FILE_PROTOCOL (This);

  if (FSIThis->TgtFP != NULL) {
    // do it with target FP
    Status = FSIThis->TgtFP->Delete (FSIThis->TgtFP);
    FSIThis->TgtFP = NULL;
  }

  if (FSIThis->SrcFP != NULL) {
    // close source FP
    FSIThis->SrcFP->Close (FSIThis->SrcFP);
    FSIThis->SrcFP = NULL;
  }

  if (FSIThis->FName != NULL) {
    FreePool (FSIThis->FName);
    FSIThis->FName = NULL;
  }

  FreePool (FSIThis);

  DBG ("= %r\n", Status);

  return Status;
}

/** EFI_FILE_PROTOCOL.Read - Reads data from a file. */
EFI_STATUS
EFIAPI
FSI_FP_Read (
  IN EFI_FILE_PROTOCOL    *This,
  IN OUT UINTN            *BufferSize,
  OUT VOID                *Buffer
) {
  EFI_STATUS              Status = EFI_DEVICE_ERROR;
  FSI_FILE_PROTOCOL       *FSIThis;
#if DBG_TO
  EFI_FILE_INFO           *FInfo;
#endif
  UINTN                   BufferSizeOrig, OrigBufferSize = *BufferSize;
  CHAR8                   *String;
  FSI_STRING_LIST         *StringList;
  FSI_STRING_LIST_ENTRY   *StringEntry;
  VOID                    *TmpBuffer;

  DBG ("FSI_FP %p.Read (%d, %p) ", This, *BufferSize, Buffer);

  FSIThis = FSI_FROM_FILE_PROTOCOL (This);

  if ((FSIThis->TgtFP != NULL) && (FSIThis->SrcFP != NULL)) {
    // this is injection point
    // first read dir entries from Src and then from Tgt
    BufferSizeOrig = *BufferSize;
    Status = FSIThis->SrcFP->Read (FSIThis->SrcFP, BufferSize, Buffer);

    if (*BufferSize == 0) {
      // no more in Src - read from Tgt
      *BufferSize = BufferSizeOrig;
      Status = FSIThis->TgtFP->Read (FSIThis->TgtFP, BufferSize, Buffer);
    }
  } else if (FSIThis->TgtFP != NULL) {
    // do it with target FP
    Status = FSIThis->TgtFP->Read (FSIThis->TgtFP, BufferSize, Buffer);
    StringList = FSIThis->FSI_FS->ForceLoadKexts;

    if ((Status == EFI_INVALID_PARAMETER) && (*BufferSize == 0)) {
      // On some systems FS driver seems to have alignment restrictions on given buffer.
      // UEFIs buffers allocated with standard AllocatePool seem to be aligned properly and reads
      // to them always succeed, so we'll try to overcome this by using new allocated buffer.
      TmpBuffer = AllocateZeroPool (OrigBufferSize);
      *BufferSize = OrigBufferSize;
      Status = FSIThis->TgtFP->Read (FSIThis->TgtFP, BufferSize, TmpBuffer);

      if ((Status == EFI_SUCCESS) && (*BufferSize > 0)) {
        CopyMem (Buffer, TmpBuffer, *BufferSize);
      }

      FreePool (TmpBuffer);
    }

    if (
      (Status == EFI_SUCCESS) &&
      (StringList != NULL) &&
      (FSIThis->FName != NULL)
    ) {
      // check ForceLoadKexts
      for (
        StringEntry = (FSI_STRING_LIST_ENTRY *)GetFirstNode (&StringList->List);
        !IsNull (&StringList->List, &StringEntry->List);
        StringEntry = (FSI_STRING_LIST_ENTRY *)GetNextNode (&StringList->List, &StringEntry->List)
      ) {
        if (StrStr (FSIThis->FName, StringEntry->String) != NULL) {
          //DBG ("Got: %s\n", FSIThis->FName);
          String = AsciiStrStr ((CHAR8 *)Buffer, "<string>Safe Boot</string>");

          if (String != NULL) {
            CopyMem (String, "<string>Root</string>     ", 26);
            DBG ("Forced load: %s\n", FSIThis->FName);
            //gBS->Stall (5000000);
          } else {
            String = AsciiStrStr ((CHAR8 *)Buffer, "<string>Network-Root</string>");

            if (String != NULL) {
              CopyMem (String, "<string>Root</string>        ", 29);
              DBG ("Forced load: %s\n", FSIThis->FName);
              //gBS->Stall (5000000);
            }
          }
        }
      }
    }
  } else if (FSIThis->SrcFP != NULL) {
    // do it with source FP
    Status = FSIThis->SrcFP->Read (FSIThis->SrcFP, BufferSize, Buffer);

    if ((Status == EFI_INVALID_PARAMETER) && (*BufferSize == 0)) {
      // On some systems FS driver seems to have alignment restrictions on given buffer.
      // UEFIs buffers allocated with standard AllocatePool seem to be aligned properly and reads
      // to them always succeed, so we'll try to overcome this by using new allocated buffer.
      TmpBuffer = AllocateZeroPool (OrigBufferSize);
      *BufferSize = OrigBufferSize;
      Status = FSIThis->SrcFP->Read (FSIThis->SrcFP, BufferSize, TmpBuffer);

      if ((Status == EFI_SUCCESS) && (*BufferSize > 0)) {
        CopyMem (Buffer, TmpBuffer, *BufferSize);
      }

      FreePool (TmpBuffer);
    }
  }

#if DBG_TO
  if ((Status == EFI_SUCCESS) && (FSIThis->IsDir) && (*BufferSize > 0)) {
    FInfo = (EFI_FILE_INFO *)Buffer;
    DBG ("= %r, *BufferSize=%d, dir entry FileName='%s'\n", Status, *BufferSize, FInfo->FileName);
  } else {
    DBG ("= %r, *BufferSize=%d\n", Status, *BufferSize);
  }
#endif

  return Status;
}

/** EFI_FILE_PROTOCOL.Write - Writes data to a file. */
EFI_STATUS
EFIAPI
FSI_FP_Write (
  IN EFI_FILE_PROTOCOL  *This,
  IN OUT UINTN          *BufferSize,
  IN VOID               *Buffer
) {
  EFI_STATUS          Status = EFI_DEVICE_ERROR;
  FSI_FILE_PROTOCOL   *FSIThis;

  DBG ("FSI_FP %p.Write (%d, %p) ", This, *BufferSize, Buffer);

  FSIThis = FSI_FROM_FILE_PROTOCOL (This);

  if (FSIThis->TgtFP != NULL) {
    // do it with target FP
    Status = FSIThis->TgtFP->Write (FSIThis->TgtFP, BufferSize, Buffer);
  } else if (FSIThis->SrcFP != NULL) {
    // do it with source FP
    Status = FSIThis->SrcFP->Write (FSIThis->SrcFP, BufferSize, Buffer);
  }

  DBG ("= %r, *BufferSize=%d\n", Status, *BufferSize);

  return Status;
}

/** EFI_FILE_PROTOCOL.SetPosition - Sets a file's current position. */
EFI_STATUS
EFIAPI
FSI_FP_SetPosition (
  IN EFI_FILE_PROTOCOL    *This,
  IN UINT64               Position
) {
  EFI_STATUS          Status = EFI_DEVICE_ERROR;
  FSI_FILE_PROTOCOL   *FSIThis;

  DBG ("FSI_FP %p.SetPosition (%d) ", This, Position);

  FSIThis = FSI_FROM_FILE_PROTOCOL (This);

  if (FSIThis->TgtFP != NULL) {
    // do it with target FP
    Status = FSIThis->TgtFP->SetPosition (FSIThis->TgtFP, Position);
  }

  if (FSIThis->SrcFP != NULL) {
    // and with Src
    Status = FSIThis->SrcFP->SetPosition (FSIThis->SrcFP, Position);
  }

  DBG ("= %r\n", Status);

  return Status;
}

/** EFI_FILE_PROTOCOL.GetPosition - Returns a file's current position. */
EFI_STATUS
EFIAPI
FSI_FP_GetPosition (
  IN EFI_FILE_PROTOCOL   *This,
  IN UINT64              *Position
) {
  EFI_STATUS          Status = EFI_DEVICE_ERROR;
  FSI_FILE_PROTOCOL   *FSIThis;

  DBG ("FSI_FP %p.GetPosition () ", This);

  FSIThis = FSI_FROM_FILE_PROTOCOL (This);

  if (FSIThis->TgtFP != NULL) {
    // do it with target FP
    Status = FSIThis->TgtFP->GetPosition (FSIThis->TgtFP, Position);
  } else if (FSIThis->SrcFP != NULL) {
    // do it with source FP
    Status = FSIThis->SrcFP->GetPosition (FSIThis->SrcFP, Position);
  }

  DBG ("= %r, Position=%d\n", Status, Position);

  return Status;
}

/** EFI_FILE_PROTOCOL.GetInfo - Returns information about a file. */
EFI_STATUS
EFIAPI
FSI_FP_GetInfo (
  IN EFI_FILE_PROTOCOL  *This,
  IN EFI_GUID           *InformationType,
  IN OUT UINTN          *BufferSize,
  OUT VOID              *Buffer
) {
  EFI_STATUS          Status = EFI_DEVICE_ERROR;
  FSI_FILE_PROTOCOL   *FSIThis;
  EFI_FILE_INFO       *FInfo;

  //DBG ("FSI_FP %p.GetInfo (%s, %d, %p) ", This, FSInjectGuidStr (InformationType), *BufferSize, Buffer);

  FSIThis = FSI_FROM_FILE_PROTOCOL (This);

  if (FSIThis->TgtFP != NULL) {
    // do it with target FP
    Status = FSIThis->TgtFP->GetInfo (FSIThis->TgtFP, InformationType, BufferSize, Buffer);

    if (Status == EFI_SUCCESS && CompareGuid (InformationType, &gEfiFileInfoGuid)) {
      FInfo = (EFI_FILE_INFO *)Buffer;
      FSIThis->IsDir = FInfo->Attribute & EFI_FILE_DIRECTORY;
    }
  } else if (FSIThis->SrcFP != NULL) {
    // do it with source FP
    Status = FSIThis->SrcFP->GetInfo (FSIThis->SrcFP, InformationType, BufferSize, Buffer);

    if ((Status == EFI_SUCCESS) && CompareGuid (InformationType, &gEfiFileInfoGuid)) {
      FInfo = (EFI_FILE_INFO *)Buffer;
      FSIThis->IsDir = FInfo->Attribute & EFI_FILE_DIRECTORY;
    }
  }

  DBG ("= %r, BufferSize=%d\n", Status, *BufferSize);

  return Status;
}

/** EFI_FILE_PROTOCOL.SetInfo - Sets information about a file. */
EFI_STATUS
EFIAPI
FSI_FP_SetInfo (
  IN EFI_FILE_PROTOCOL    *This,
  IN EFI_GUID             *InformationType,
  IN UINTN                BufferSize,
  IN VOID                 *Buffer
) {
  EFI_STATUS          Status = EFI_DEVICE_ERROR;
  FSI_FILE_PROTOCOL   *FSIThis;

  //DBG ("FSI_FP %p.SetInfo (%s, %d, %p) ", This, FSInjectGuidStr (InformationType), BufferSize, Buffer);

  FSIThis = FSI_FROM_FILE_PROTOCOL (This);

  if (FSIThis->TgtFP != NULL) {
    // do it with target FP
    Status = FSIThis->TgtFP->SetInfo (FSIThis->TgtFP, InformationType, BufferSize, Buffer);
  } else if (FSIThis->SrcFP != NULL) {
    // do it with source FP
    Status = FSIThis->SrcFP->SetInfo (FSIThis->SrcFP, InformationType, BufferSize, Buffer);
  }

  DBG ("= %r\n", Status);

  return Status;
}

/** EFI_FILE_PROTOCOL.Flush - Flushes all modified data associated with a file to a device. */
EFI_STATUS
EFIAPI
FSI_FP_Flush (
  IN EFI_FILE_PROTOCOL  *This
) {
  EFI_STATUS          Status = EFI_DEVICE_ERROR;
  FSI_FILE_PROTOCOL   *FSIThis;

  DBG ("FSI_FP %p.Flush () ", This);

  FSIThis = FSI_FROM_FILE_PROTOCOL (This);

  if (FSIThis->TgtFP != NULL) {
    // do it with target FP
    Status = FSIThis->TgtFP->Flush (FSIThis->TgtFP);
  } else if (FSIThis->SrcFP != NULL) {
    // do it with source FP
    Status = FSIThis->SrcFP->Flush (FSIThis->SrcFP);
  }

  DBG ("= %r\n", Status);

  return Status;
}

/** Creates our FSI_FILE_PROTOCOL. */
FSI_FILE_PROTOCOL *
EFIAPI
CreateFSInjectFP () {
  FSI_FILE_PROTOCOL   *FSINew;

  // wrap it into our implementation
  FSINew = AllocateZeroPool (sizeof (FSI_FILE_PROTOCOL));

  if (FSINew == NULL) {
    return NULL;
  }

  FSINew->Signature = FSI_FILE_PROTOCOL_SIGNATURE;
  FSINew->FP.Revision = EFI_FILE_PROTOCOL_REVISION;
  FSINew->FP.Open = FSI_FP_Open;
  FSINew->FP.Close = FSI_FP_Close;
  FSINew->FP.Delete = FSI_FP_Delete;
  FSINew->FP.Read = FSI_FP_Read;
  FSINew->FP.Write = FSI_FP_Write;
  FSINew->FP.GetPosition = FSI_FP_GetPosition;
  FSINew->FP.SetPosition = FSI_FP_SetPosition;
  FSINew->FP.GetInfo = FSI_FP_GetInfo;
  FSINew->FP.SetInfo = FSI_FP_SetInfo;
  FSINew->FP.Flush = FSI_FP_Flush;

  FSINew->FSI_FS = NULL;
  FSINew->FName = NULL;
  FSINew->IsDir = FALSE;
  FSINew->TgtFP = NULL;
  FSINew->SrcFP = NULL;
  FSINew->FromTgt = FALSE;

  return FSINew;
}

/**************************************************************************************
 * FSI_SIMPLE_FILE_SYSTEM_PROTOCOL - our implementation of EFI_SIMPLE_FILE_SYSTEM_PROTOCOL
 **************************************************************************************/

/**
 * EFI_SIMPLE_FILE_SYSTEM_PROTOCOL.OpenVolume implementation.
 */
EFI_STATUS
EFIAPI
FSI_SFS_OpenVolume (
  IN  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL   *This,
  OUT EFI_FILE_PROTOCOL                 **Root
) {
  EFI_STATUS                        Status;
  FSI_SIMPLE_FILE_SYSTEM_PROTOCOL   *FSIThis;
  FSI_FILE_PROTOCOL                 *FSINew;

  DBG ("FSI_FS %p.OpenVolume () ", This);

  FSIThis = FSI_FROM_SIMPLE_FILE_SYSTEM (This);

  // do it with target FS
  Status = FSIThis->TgtFS->OpenVolume (FSIThis->TgtFS, Root);

  if (EFI_ERROR (Status)) {
    DBG ("TgtFS->OpenVolume Status=%r\n", Status);
    return Status;
  }

  // wrap it into our implementation
  FSINew = CreateFSInjectFP ();
  if (FSINew == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    DBG ("CreateFSInjectFP: %r\n", Status);
    return Status;
  }

  FSINew->FSI_FS = FSIThis;   // saving reference to parent FS protocol
  FSINew->FName = AllocateCopyPool (StrSize (L"\\"), L"\\");
  FSINew->TgtFP = *Root;
  FSINew->SrcFP = NULL;
  FSINew->FromTgt = TRUE;

  // set it as result
  *Root = &FSINew->FP;

  DBG ("= %r, returning Root=%p\n", Status, *Root);

  return Status;
}

/**************************************************************************************
 * FSINJECTION_PROTOCOL
 **************************************************************************************/

/**
 * FSINJECTION_PROTOCOL.Install implementation.
 */
EFI_STATUS
EFIAPI
FSInjectionInstall (
  IN EFI_HANDLE         TgtHandle,
  IN CHAR16             *TgtDir,
  IN EFI_HANDLE         SrcHandle,
  IN CHAR16             *SrcDir,
  IN FSI_STRING_LIST    *Blacklist,
  IN FSI_STRING_LIST    *ForceLoadKexts
) {
  EFI_STATUS                        Status;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL   *TgtFS, *SrcFS;
  FSI_SIMPLE_FILE_SYSTEM_PROTOCOL   *OurFS;

  DBG ("FSInjectionInstall ...\n");

  // get existing target EFI_SIMPLE_FILE_SYSTEM_PROTOCOL
  Status = gBS->OpenProtocol (TgtHandle, &gEfiSimpleFileSystemProtocolGuid, (void **)&TgtFS, gImageHandle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);

  if (EFI_ERROR (Status)) {
    DBG ("- target OpenProtocol (gEfiSimpleFileSystemProtocolGuid): %r\n", Status);
    return Status;
  }

  // get existing source EFI_SIMPLE_FILE_SYSTEM_PROTOCOL
  Status = gBS->OpenProtocol (SrcHandle, &gEfiSimpleFileSystemProtocolGuid, (void **)&SrcFS, gImageHandle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);

  if (EFI_ERROR (Status)) {
    DBG ("- source OpenProtocol (gEfiSimpleFileSystemProtocolGuid): %r\n", Status);
    return Status;
  }

  // create our implementation
  OurFS = AllocateZeroPool (sizeof (FSI_SIMPLE_FILE_SYSTEM_PROTOCOL));

  if (OurFS == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    DBG ("- AllocateZeroPool (FSI_SIMPLE_FILE_SYSTEM_PROTOCOL): %r\n", Status);
    return Status;
  }

  OurFS->Signature = FSI_SIMPLE_FILE_SYSTEM_PROTOCOL_SIGNATURE;
  OurFS->FS.Revision = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_REVISION;
  OurFS->FS.OpenVolume = FSI_SFS_OpenVolume;
  OurFS->TgtHandle = TgtHandle;
  OurFS->TgtFS = TgtFS;
  OurFS->TgtDir = AllocateCopyPool (StrSize (TgtDir), TgtDir);

  if (OurFS->TgtDir == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    DBG ("- AllocateCopyPool for TgtDir: %r\n", Status);
    goto ErrorExit;
  }

  OurFS->SrcHandle = SrcHandle;
  OurFS->SrcFS = SrcFS;

  // we can run without SrcDir - no injection, but blocking caches for example
  if (SrcDir != NULL) {
    OurFS->SrcDir = AllocateCopyPool (StrSize (SrcDir), SrcDir);
    if (OurFS->SrcDir == NULL) {
      Status = EFI_OUT_OF_RESOURCES;
      DBG ("- AllocateCopyPool for TgtDir or SrcDir: %r\n", Status);
      goto ErrorExit;
    }
  }

  if ((Blacklist != NULL) && !IsListEmpty (&Blacklist->List)) {
    OurFS->Blacklist = Blacklist;
  }

  if ((ForceLoadKexts != NULL) && !IsListEmpty (&ForceLoadKexts->List)) {
    OurFS->ForceLoadKexts = ForceLoadKexts;
  }

  // replace existing tagret EFI_SIMPLE_FILE_SYSTEM_PROTOCOL with out implementation
  Status = gBS->ReinstallProtocolInterface (TgtHandle, &gEfiSimpleFileSystemProtocolGuid, TgtFS, &OurFS->FS);
  if (EFI_ERROR (Status)) {
    DBG ("- ReinstallProtocolInterface (): %r\n", Status);
    return Status;
  }

  DBG ("- Our FSI_SIMPLE_FILE_SYSTEM_PROTOCOL installed on handle: %X\n", TgtHandle);

  return EFI_SUCCESS;

ErrorExit:

  if (OurFS->TgtDir != NULL) {
    FreePool (OurFS->TgtDir);
  }

  if (OurFS->SrcDir != NULL) {
    FreePool (OurFS->SrcDir);
  }

  FreePool (OurFS);

  return Status;
}

/**
 * FSINJECTION_PROTOCOL.CreateStringList () implementation.
 */
FSI_STRING_LIST *
EFIAPI
FSInjectionCreateStringList () {
  FSI_STRING_LIST   *List;

  List = AllocateZeroPool (sizeof (FSI_STRING_LIST));

  if (List != NULL) {
    InitializeListHead (&List->List);
  }

  return List;
}

/**
 * FSINJECTION_PROTOCOL.AddStringToList () implementation.
 */
FSI_STRING_LIST *
EFIAPI
FSInjectionAddStringToList (
        FSI_STRING_LIST   *List,
  CONST CHAR16            *String
) {
  FSI_STRING_LIST_ENTRY   *Entry;

  if ((List == NULL) || (String == NULL)) {
    return NULL;
  }

  Entry = AllocateZeroPool (sizeof (FSI_STRING_LIST_ENTRY));

  if (Entry != NULL) {
    Entry->String = AllocateCopyPool (StrSize (String), String);
    InsertTailList (&List->List, &Entry->List);

    return List;
  }

  return NULL;
}

/**
 * Installs FSINJECTION_PROTOCOL to na new handle/
 */
EFI_STATUS
EFIAPI
InstallFSInjectionProtocol () {
  EFI_STATUS              Status;
  FSINJECTION_PROTOCOL    *FSInjection;
  EFI_HANDLE              FSIHandle;

  // install FSINJECTION_PROTOCOL to new handle
  FSInjection = AllocateZeroPool (sizeof (FSINJECTION_PROTOCOL));

  if (FSInjection == NULL)   {
    DBG ("Can not allocate memory for FSINJECTION_PROTOCOL\n");
    return EFI_OUT_OF_RESOURCES;
  }

  FSInjection->Install = FSInjectionInstall;
  FSInjection->CreateStringList = FSInjectionCreateStringList;
  FSInjection->AddStringToList = FSInjectionAddStringToList;
  FSIHandle = NULL; // install to new handle
  Status = gBS->InstallMultipleProtocolInterfaces (&FSIHandle, &gFSInjectProtocolGuid, FSInjection, NULL);

  if (EFI_ERROR (Status)) {
    DBG ("InstallFSInjectionProtocol: error installing FSINJECTION_PROTOCOL, Status = %r\n", Status);
  }

  return Status;
}

/**************************************************************************************
 * Entry point
 **************************************************************************************/

/**
 * FSInjection entry point. Installs FSINJECTION_PROTOCOL.
 */
EFI_STATUS
EFIAPI
FSInjectEntrypoint (
  IN EFI_HANDLE           ImageHandle,
  IN EFI_SYSTEM_TABLE     *SystemTable
) {
  EFI_STATUS          Status;
#if TEST
  FSI_STRING_LIST     *Blacklist;
#endif

#ifndef EMBED_FSINJECT
  DBG ("Starting module: FSInject\n");
#endif

  Status = InstallFSInjectionProtocol ();

  if (EFI_ERROR (Status)) {
    return Status;
  }

  return EFI_SUCCESS;
}

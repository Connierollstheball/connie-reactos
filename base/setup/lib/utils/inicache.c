/*
 * PROJECT:     ReactOS Setup Library
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     INI file parser that caches contents of INI file in memory.
 * COPYRIGHT:   Copyright 2002-2018 Royce Mitchell III
 */

/* INCLUDES *****************************************************************/

#include "precomp.h"

#include "inicache.h"

#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS ********************************************************/

static
PINI_KEYWORD
IniCacheFreeKey(
    PINI_KEYWORD Key)
{
    PINI_KEYWORD Next;

    if (Key == NULL)
        return NULL;

    Next = Key->Next;
    if (Key->Name != NULL)
    {
        RtlFreeHeap(ProcessHeap, 0, Key->Name);
        Key->Name = NULL;
    }

    if (Key->Data != NULL)
    {
        RtlFreeHeap(ProcessHeap, 0, Key->Data);
        Key->Data = NULL;
    }

    RtlFreeHeap(ProcessHeap, 0, Key);

    return Next;
}

static
PINI_SECTION
IniCacheFreeSection(
    PINI_SECTION Section)
{
    PINI_SECTION Next;

    if (Section == NULL)
        return NULL;

    Next = Section->Next;
    while (Section->FirstKey != NULL)
    {
        Section->FirstKey = IniCacheFreeKey(Section->FirstKey);
    }
    Section->LastKey = NULL;

    if (Section->Name != NULL)
    {
        RtlFreeHeap(ProcessHeap, 0, Section->Name);
        Section->Name = NULL;
    }

    RtlFreeHeap(ProcessHeap, 0, Section);

    return Next;
}

static
PINI_SECTION
IniCacheFindSection(
    _In_ PINICACHE Cache,
    _In_ PCWSTR Name)
{
    PINI_SECTION Section;

    for (Section = Cache->FirstSection; Section; Section = Section->Next)
    {
        if (_wcsicmp(Section->Name, Name) == 0)
            return Section;
    }
    return NULL;
}

static
PINI_KEYWORD
IniCacheFindKey(
    _In_ PINI_SECTION Section,
    _In_ PCWSTR Name)
{
    PINI_KEYWORD Key;

    for (Key = Section->FirstKey; Key; Key = Key->Next)
    {
        if (_wcsicmp(Key->Name, Name) == 0)
            return Key;
    }
    return NULL;
}

static
PINI_KEYWORD
IniCacheAddKeyAorW(
    _In_ PINI_SECTION Section,
    _In_ PINI_KEYWORD AnchorKey,
    _In_ INSERTION_TYPE InsertionType,
    _In_ const VOID* Name,
    _In_ ULONG NameLength,
    _In_ const VOID* Data,
    _In_ ULONG DataLength,
    _In_ BOOLEAN IsUnicode)
{
    PINI_KEYWORD Key;
    PWSTR NameU, DataU;

    if (!Section || !Name || NameLength == 0 || !Data || DataLength == 0)
    {
        DPRINT("Invalid parameter\n");
        return NULL;
    }

    /* Allocate the UNICODE key name */
    NameU = (PWSTR)RtlAllocateHeap(ProcessHeap,
                                   0,
                                   (NameLength + 1) * sizeof(WCHAR));
    if (!NameU)
    {
        DPRINT("RtlAllocateHeap() failed\n");
        return NULL;
    }
    /* Copy the value name (ANSI or UNICODE) */
    if (IsUnicode)
        wcsncpy(NameU, (PCWCH)Name, NameLength);
    else
        _snwprintf(NameU, NameLength, L"%.*S", NameLength, (PCCH)Name);
    NameU[NameLength] = UNICODE_NULL;

    /*
     * Find whether a key with the given name already exists in the section.
     * If so, modify the data and return it; otherwise create a new one.
     */
    Key = IniCacheFindKey(Section, NameU);
    if (Key)
    {
        RtlFreeHeap(ProcessHeap, 0, NameU);

        /* Modify the existing data */

        /* Allocate the UNICODE data buffer */
        DataU = (PWSTR)RtlAllocateHeap(ProcessHeap,
                                       0,
                                       (DataLength + 1) * sizeof(WCHAR));
        if (!DataU)
        {
            DPRINT("RtlAllocateHeap() failed\n");
            return NULL; // We failed, don't modify the original key.
        }
        /* Copy the data (ANSI or UNICODE) */
        if (IsUnicode)
            wcsncpy(DataU, (PCWCH)Data, DataLength);
        else
            _snwprintf(DataU, DataLength, L"%.*S", DataLength, (PCCH)Data);
        DataU[DataLength] = UNICODE_NULL;

        /* Swap the old key data with the new one */
        RtlFreeHeap(ProcessHeap, 0, Key->Data);
        Key->Data = DataU;

        /* Return the modified key */
        return Key;
    }

    /* Allocate the key buffer and name */
    Key = (PINI_KEYWORD)RtlAllocateHeap(ProcessHeap,
                                        HEAP_ZERO_MEMORY,
                                        sizeof(INI_KEYWORD));
    if (!Key)
    {
        DPRINT("RtlAllocateHeap() failed\n");
        RtlFreeHeap(ProcessHeap, 0, NameU);
        return NULL;
    }
    Key->Name = NameU;

    /* Allocate the UNICODE data buffer */
    DataU = (PWSTR)RtlAllocateHeap(ProcessHeap,
                                   0,
                                   (DataLength + 1) * sizeof(WCHAR));
    if (!DataU)
    {
        DPRINT("RtlAllocateHeap() failed\n");
        RtlFreeHeap(ProcessHeap, 0, NameU);
        RtlFreeHeap(ProcessHeap, 0, Key);
        return NULL;
    }
    /* Copy the data (ANSI or UNICODE) */
    if (IsUnicode)
        wcsncpy(DataU, (PCWCH)Data, DataLength);
    else
        _snwprintf(DataU, DataLength, L"%.*S", DataLength, (PCCH)Data);
    DataU[DataLength] = UNICODE_NULL;
    Key->Data = DataU;

    /* Insert the key into section */
    if (Section->FirstKey == NULL)
    {
        Section->FirstKey = Key;
        Section->LastKey = Key;
    }
    else if ((InsertionType == INSERT_FIRST) ||
             ((InsertionType == INSERT_BEFORE) &&
                ((AnchorKey == NULL) || (AnchorKey == Section->FirstKey))))
    {
        /* Insert at the head of the list */
        Section->FirstKey->Prev = Key;
        Key->Next = Section->FirstKey;
        Section->FirstKey = Key;
    }
    else if ((InsertionType == INSERT_BEFORE) && (AnchorKey != NULL))
    {
        /* Insert before the anchor key */
        Key->Next = AnchorKey;
        Key->Prev = AnchorKey->Prev;
        AnchorKey->Prev->Next = Key;
        AnchorKey->Prev = Key;
    }
    else if ((InsertionType == INSERT_LAST) ||
             ((InsertionType == INSERT_AFTER) &&
                ((AnchorKey == NULL) || (AnchorKey == Section->LastKey))))
    {
        Section->LastKey->Next = Key;
        Key->Prev = Section->LastKey;
        Section->LastKey = Key;
    }
    else if ((InsertionType == INSERT_AFTER) && (AnchorKey != NULL))
    {
        /* Insert after the anchor key */
        Key->Next = AnchorKey->Next;
        Key->Prev = AnchorKey;
        AnchorKey->Next->Prev = Key;
        AnchorKey->Next = Key;
    }

    return Key;
}

static
PINI_SECTION
IniCacheAddSectionAorW(
    _In_ PINICACHE Cache,
    _In_ const VOID* Name,
    _In_ ULONG NameLength,
    _In_ BOOLEAN IsUnicode)
{
    PINI_SECTION Section;
    PWSTR NameU;

    if (!Cache || !Name || NameLength == 0)
    {
        DPRINT("Invalid parameter\n");
        return NULL;
    }

    /* Allocate the UNICODE section name */
    NameU = (PWSTR)RtlAllocateHeap(ProcessHeap,
                                   0,
                                   (NameLength + 1) * sizeof(WCHAR));
    if (!NameU)
    {
        DPRINT("RtlAllocateHeap() failed\n");
        return NULL;
    }
    /* Copy the section name (ANSI or UNICODE) */
    if (IsUnicode)
        wcsncpy(NameU, (PCWCH)Name, NameLength);
    else
        _snwprintf(NameU, NameLength, L"%.*S", NameLength, (PCCH)Name);
    NameU[NameLength] = UNICODE_NULL;

    /*
     * Find whether a section with the given name already exists.
     * If so, just return it; otherwise create a new one.
     */
    Section = IniCacheFindSection(Cache, NameU);
    if (Section)
    {
        RtlFreeHeap(ProcessHeap, 0, NameU);
        return Section;
    }

    /* Allocate the section buffer and name */
    Section = (PINI_SECTION)RtlAllocateHeap(ProcessHeap,
                                            HEAP_ZERO_MEMORY,
                                            sizeof(INI_SECTION));
    if (!Section)
    {
        DPRINT("RtlAllocateHeap() failed\n");
        RtlFreeHeap(ProcessHeap, 0, NameU);
        return NULL;
    }
    Section->Name = NameU;

    /* Append the section */
    if (Cache->FirstSection == NULL)
    {
        Cache->FirstSection = Section;
        Cache->LastSection = Section;
    }
    else
    {
        Cache->LastSection->Next = Section;
        Section->Prev = Cache->LastSection;
        Cache->LastSection = Section;
    }

    return Section;
}

static
PCHAR
IniCacheSkipWhitespace(
    PCHAR Ptr)
{
    while (*Ptr != 0 && isspace(*Ptr))
        Ptr++;

    return (*Ptr == 0) ? NULL : Ptr;
}

static
PCHAR
IniCacheSkipToNextSection(
    PCHAR Ptr)
{
    while (*Ptr != 0 && *Ptr != '[')
    {
        while (*Ptr != 0 && *Ptr != L'\n')
        {
            Ptr++;
        }

        Ptr++;
    }

    return (*Ptr == 0) ? NULL : Ptr;
}

static
PCHAR
IniCacheGetSectionName(
    PCHAR Ptr,
    PCHAR *NamePtr,
    PULONG NameSize)
{
    ULONG Size = 0;

    *NamePtr = NULL;
    *NameSize = 0;

    /* Skip whitespace */
    while (*Ptr != 0 && isspace(*Ptr))
    {
        Ptr++;
    }

    *NamePtr = Ptr;

    while (*Ptr != 0 && *Ptr != ']')
    {
        Size++;
        Ptr++;
    }
    Ptr++;

    while (*Ptr != 0 && *Ptr != L'\n')
    {
        Ptr++;
    }
    Ptr++;

    *NameSize = Size;

    DPRINT("SectionName: '%.*s'\n", Size, *NamePtr);

    return Ptr;
}

static
PCHAR
IniCacheGetKeyName(
    PCHAR Ptr,
    PCHAR *NamePtr,
    PULONG NameSize)
{
    ULONG Size = 0;

    *NamePtr = NULL;
    *NameSize = 0;

    while (Ptr && *Ptr)
    {
        *NamePtr = NULL;
        *NameSize = 0;
        Size = 0;

        /* Skip whitespace and empty lines */
        while (isspace(*Ptr) || *Ptr == '\n' || *Ptr == '\r')
        {
            Ptr++;
        }
        if (*Ptr == 0)
        {
            continue;
        }

        *NamePtr = Ptr;

        while (*Ptr != 0 && !isspace(*Ptr) && *Ptr != '=' && *Ptr != ';')
        {
            Size++;
            Ptr++;
        }
        if (*Ptr == ';')
        {
            while (*Ptr != 0 && *Ptr != '\r' && *Ptr != '\n')
            {
                Ptr++;
            }
        }
        else
        {
            *NameSize = Size;
            break;
        }
    }

  return Ptr;
}

static
PCHAR
IniCacheGetKeyValue(
    PCHAR Ptr,
    PCHAR *DataPtr,
    PULONG DataSize,
    BOOLEAN String)
{
    ULONG Size = 0;

    *DataPtr = NULL;
    *DataSize = 0;

    /* Skip whitespace */
    while (*Ptr != 0 && isspace(*Ptr))
    {
        Ptr++;
    }

    /* Check and skip '=' */
    if (*Ptr != '=')
    {
        return NULL;
    }
    Ptr++;

    /* Skip whitespace */
    while (*Ptr != 0 && isspace(*Ptr))
    {
        Ptr++;
    }

    if (*Ptr == '"' && String)
    {
        Ptr++;

        /* Get data */
        *DataPtr = Ptr;
        while (*Ptr != '"')
        {
            Ptr++;
            Size++;
        }
        Ptr++;

        while (*Ptr && *Ptr != '\r' && *Ptr != '\n')
        {
            Ptr++;
        }
    }
    else
    {
        /* Get data */
        *DataPtr = Ptr;
        while (*Ptr != 0 && *Ptr != '\r' && *Ptr != ';')
        {
            Ptr++;
            Size++;
        }
    }

    /* Skip to next line */
    if (*Ptr == '\r')
        Ptr++;
    if (*Ptr == '\n')
        Ptr++;

    *DataSize = Size;

    return Ptr;
}


/* PUBLIC FUNCTIONS *********************************************************/

NTSTATUS
IniCacheLoadFromMemory(
    PINICACHE *Cache,
    PCHAR FileBuffer,
    ULONG FileLength,
    BOOLEAN String)
{
    PCHAR Ptr;

    PINI_SECTION Section;
    PINI_KEYWORD Key;

    PCHAR SectionName;
    ULONG SectionNameSize;

    PCHAR KeyName;
    ULONG KeyNameSize;

    PCHAR KeyValue;
    ULONG KeyValueSize;

    /* Allocate inicache header */
    *Cache = (PINICACHE)RtlAllocateHeap(ProcessHeap,
                                        HEAP_ZERO_MEMORY,
                                        sizeof(INICACHE));
    if (*Cache == NULL)
    {
        DPRINT("RtlAllocateHeap() failed\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Parse ini file */
    Section = NULL;
    Ptr = FileBuffer;
    while (Ptr != NULL && *Ptr != 0)
    {
        Ptr = IniCacheSkipWhitespace(Ptr);
        if (Ptr == NULL)
            continue;

        if (*Ptr == '[')
        {
            Section = NULL;
            Ptr++;

            Ptr = IniCacheGetSectionName(Ptr,
                                         &SectionName,
                                         &SectionNameSize);

            DPRINT("[%.*s]\n", SectionNameSize, SectionName);

            Section = IniCacheAddSectionAorW(*Cache,
                                             SectionName,
                                             SectionNameSize,
                                             FALSE);
            if (Section == NULL)
            {
                DPRINT("IniCacheAddSectionAorW() failed\n");
                Ptr = IniCacheSkipToNextSection(Ptr);
                continue;
            }
        }
        else
        {
            if (Section == NULL)
            {
                Ptr = IniCacheSkipToNextSection(Ptr);
                continue;
            }

            Ptr = IniCacheGetKeyName(Ptr,
                                     &KeyName,
                                     &KeyNameSize);

            Ptr = IniCacheGetKeyValue(Ptr,
                                      &KeyValue,
                                      &KeyValueSize,
                                      String);

            DPRINT("'%.*s' = '%.*s'\n", KeyNameSize, KeyName, KeyValueSize, KeyValue);

            Key = IniCacheAddKeyAorW(Section,
                                     NULL,
                                     INSERT_LAST,
                                     KeyName,
                                     KeyNameSize,
                                     KeyValue,
                                     KeyValueSize,
                                     FALSE);
            if (Key == NULL)
            {
                DPRINT("IniCacheAddKeyAorW() failed\n");
            }
        }
    }

    return STATUS_SUCCESS;
}

NTSTATUS
IniCacheLoadByHandle(
    PINICACHE *Cache,
    HANDLE FileHandle,
    BOOLEAN String)
{
    NTSTATUS Status;
    IO_STATUS_BLOCK IoStatusBlock;
    FILE_STANDARD_INFORMATION FileInfo;
    PCHAR FileBuffer;
    ULONG FileLength;
    LARGE_INTEGER FileOffset;

    *Cache = NULL;

    /* Query file size */
    Status = NtQueryInformationFile(FileHandle,
                                    &IoStatusBlock,
                                    &FileInfo,
                                    sizeof(FILE_STANDARD_INFORMATION),
                                    FileStandardInformation);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("NtQueryInformationFile() failed (Status %lx)\n", Status);
        return Status;
    }

    FileLength = FileInfo.EndOfFile.u.LowPart;

    DPRINT("File size: %lu\n", FileLength);

    /* Allocate file buffer with NULL-terminator */
    FileBuffer = (PCHAR)RtlAllocateHeap(ProcessHeap,
                                        0,
                                        FileLength + 1);
    if (FileBuffer == NULL)
    {
        DPRINT1("RtlAllocateHeap() failed\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Read file */
    FileOffset.QuadPart = 0ULL;
    Status = NtReadFile(FileHandle,
                        NULL,
                        NULL,
                        NULL,
                        &IoStatusBlock,
                        FileBuffer,
                        FileLength,
                        &FileOffset,
                        NULL);

    /* Append NULL-terminator */
    FileBuffer[FileLength] = 0;

    if (!NT_SUCCESS(Status))
    {
        DPRINT("NtReadFile() failed (Status %lx)\n", Status);
        goto Quit;
    }

    Status = IniCacheLoadFromMemory(Cache, FileBuffer, FileLength, String);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IniCacheLoadFromMemory() failed (Status %lx)\n", Status);
    }

Quit:
    /* Free the file buffer, and return */
    RtlFreeHeap(ProcessHeap, 0, FileBuffer);
    return Status;
}

NTSTATUS
IniCacheLoad(
    PINICACHE *Cache,
    PWCHAR FileName,
    BOOLEAN String)
{
    NTSTATUS Status;
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE FileHandle;

    *Cache = NULL;

    /* Open the INI file */
    RtlInitUnicodeString(&Name, FileName);

    InitializeObjectAttributes(&ObjectAttributes,
                               &Name,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);

    Status = NtOpenFile(&FileHandle,
                        FILE_GENERIC_READ | SYNCHRONIZE,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ,
                        FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("NtOpenFile() failed (Status %lx)\n", Status);
        return Status;
    }

    DPRINT("NtOpenFile() successful\n");

    Status = IniCacheLoadByHandle(Cache, FileHandle, String);

    /* Close the INI file */
    NtClose(FileHandle);
    return Status;
}

VOID
IniCacheDestroy(
    PINICACHE Cache)
{
    if (Cache == NULL)
        return;

    while (Cache->FirstSection != NULL)
    {
        Cache->FirstSection = IniCacheFreeSection(Cache->FirstSection);
    }
    Cache->LastSection = NULL;

    RtlFreeHeap(ProcessHeap, 0, Cache);
}


PINI_SECTION
IniGetSection(
    _In_ PINICACHE Cache,
    _In_ PCWSTR Name)
{
    if (!Cache || !Name)
    {
        DPRINT("Invalid parameter\n");
        return NULL;
    }
    return IniCacheFindSection(Cache, Name);
}

NTSTATUS
IniGetKey(
    PINI_SECTION Section,
    PWCHAR KeyName,
    PWCHAR *KeyData)
{
    PINI_KEYWORD Key;

    if (Section == NULL || KeyName == NULL || KeyData == NULL)
    {
        DPRINT("Invalid parameter\n");
        return STATUS_INVALID_PARAMETER;
    }

    *KeyData = NULL;

    Key = IniCacheFindKey(Section, KeyName);
    if (Key == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *KeyData = Key->Data;

    return STATUS_SUCCESS;
}


PINICACHEITERATOR
IniFindFirstValue(
    PINI_SECTION Section,
    PWCHAR *KeyName,
    PWCHAR *KeyData)
{
    PINICACHEITERATOR Iterator;
    PINI_KEYWORD Key;

    if (Section == NULL || KeyName == NULL || KeyData == NULL)
    {
        DPRINT("Invalid parameter\n");
        return NULL;
    }

    Key = Section->FirstKey;
    if (Key == NULL)
    {
        DPRINT("Invalid parameter\n");
        return NULL;
    }

    *KeyName = Key->Name;
    *KeyData = Key->Data;

    Iterator = (PINICACHEITERATOR)RtlAllocateHeap(ProcessHeap,
                                                  0,
                                                  sizeof(INICACHEITERATOR));
    if (Iterator == NULL)
    {
        DPRINT("RtlAllocateHeap() failed\n");
        return NULL;
    }

    Iterator->Section = Section;
    Iterator->Key = Key;

    return Iterator;
}

BOOLEAN
IniFindNextValue(
    PINICACHEITERATOR Iterator,
    PWCHAR *KeyName,
    PWCHAR *KeyData)
{
    PINI_KEYWORD Key;

    if (Iterator == NULL || KeyName == NULL || KeyData == NULL)
    {
        DPRINT("Invalid parameter\n");
        return FALSE;
    }

    Key = Iterator->Key->Next;
    if (Key == NULL)
    {
        DPRINT("No more entries\n");
        return FALSE;
    }

    *KeyName = Key->Name;
    *KeyData = Key->Data;

    Iterator->Key = Key;

    return TRUE;
}

VOID
IniFindClose(
    PINICACHEITERATOR Iterator)
{
    if (Iterator == NULL)
        return;

    RtlFreeHeap(ProcessHeap, 0, Iterator);
}


PINI_SECTION
IniAddSection(
    _In_ PINICACHE Cache,
    _In_ PCWSTR Name)
{
    if (!Cache || !Name || !*Name)
    {
        DPRINT("Invalid parameter\n");
        return NULL;
    }
    return IniCacheAddSectionAorW(Cache, Name, wcslen(Name), TRUE);
}

PINI_KEYWORD
IniInsertKey(
    _In_ PINI_SECTION Section,
    _In_ PINI_KEYWORD AnchorKey,
    _In_ INSERTION_TYPE InsertionType,
    _In_ PCWSTR Name,
    _In_ PCWSTR Data)
{
    if (!Section || !Name || !*Name || !Data || !*Data)
    {
        DPRINT("Invalid parameter\n");
        return NULL;
    }
    return IniCacheAddKeyAorW(Section,
                              AnchorKey, InsertionType,
                              Name, wcslen(Name),
                              Data, wcslen(Data),
                              TRUE);
}

PINI_KEYWORD
IniAddKey(
    _In_ PINI_SECTION Section,
    _In_ PCWSTR Name,
    _In_ PCWSTR Data)
{
    return IniInsertKey(Section, NULL, INSERT_LAST, Name, Data);
}


PINICACHE
IniCacheCreate(VOID)
{
    PINICACHE Cache;

    /* Allocate inicache header */
    Cache = (PINICACHE)RtlAllocateHeap(ProcessHeap,
                                       HEAP_ZERO_MEMORY,
                                       sizeof(INICACHE));
    if (Cache == NULL)
    {
        DPRINT("RtlAllocateHeap() failed\n");
        return NULL;
    }

    return Cache;
}

NTSTATUS
IniCacheSaveByHandle(
    PINICACHE Cache,
    HANDLE FileHandle)
{
    NTSTATUS Status;
    PINI_SECTION Section;
    PINI_KEYWORD Key;
    ULONG BufferSize;
    PCHAR Buffer;
    PCHAR Ptr;
    ULONG Len;
    IO_STATUS_BLOCK IoStatusBlock;
    LARGE_INTEGER Offset;

    /* Calculate required buffer size */
    BufferSize = 0;
    Section = Cache->FirstSection;
    while (Section != NULL)
    {
        BufferSize += (Section->Name ? wcslen(Section->Name) : 0)
                       + 4; /* "[]\r\n" */

        Key = Section->FirstKey;
        while (Key != NULL)
        {
            BufferSize += wcslen(Key->Name)
                          + (Key->Data ? wcslen(Key->Data) : 0)
                          + 3; /* "=\r\n" */
            Key = Key->Next;
        }

        Section = Section->Next;
        if (Section != NULL)
            BufferSize += 2; /* Extra "\r\n" at end of each section */
    }

    DPRINT("BufferSize: %lu\n", BufferSize);

    /* Allocate file buffer with NULL-terminator */
    Buffer = (PCHAR)RtlAllocateHeap(ProcessHeap,
                                    HEAP_ZERO_MEMORY,
                                    BufferSize + 1);
    if (Buffer == NULL)
    {
        DPRINT1("RtlAllocateHeap() failed\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Fill file buffer */
    Ptr = Buffer;
    Section = Cache->FirstSection;
    while (Section != NULL)
    {
        Len = sprintf(Ptr, "[%S]\r\n", Section->Name);
        Ptr += Len;

        Key = Section->FirstKey;
        while (Key != NULL)
        {
            Len = sprintf(Ptr, "%S=%S\r\n", Key->Name, Key->Data);
            Ptr += Len;
            Key = Key->Next;
        }

        Section = Section->Next;
        if (Section != NULL)
        {
            Len = sprintf(Ptr, "\r\n");
            Ptr += Len;
        }
    }

    /* Write to the INI file */
    Offset.QuadPart = 0LL;
    Status = NtWriteFile(FileHandle,
                         NULL,
                         NULL,
                         NULL,
                         &IoStatusBlock,
                         Buffer,
                         BufferSize,
                         &Offset,
                         NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("NtWriteFile() failed (Status %lx)\n", Status);
        RtlFreeHeap(ProcessHeap, 0, Buffer);
        return Status;
    }

    RtlFreeHeap(ProcessHeap, 0, Buffer);
    return STATUS_SUCCESS;
}

NTSTATUS
IniCacheSave(
    PINICACHE Cache,
    PWCHAR FileName)
{
    NTSTATUS Status;
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE FileHandle;

    /* Create the INI file */
    RtlInitUnicodeString(&Name, FileName);

    InitializeObjectAttributes(&ObjectAttributes,
                               &Name,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);

    Status = NtCreateFile(&FileHandle,
                          FILE_GENERIC_WRITE | SYNCHRONIZE,
                          &ObjectAttributes,
                          &IoStatusBlock,
                          NULL,
                          FILE_ATTRIBUTE_NORMAL,
                          0,
                          FILE_SUPERSEDE,
                          FILE_SYNCHRONOUS_IO_NONALERT | FILE_SEQUENTIAL_ONLY | FILE_NON_DIRECTORY_FILE,
                          NULL,
                          0);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("NtCreateFile() failed (Status %lx)\n", Status);
        return Status;
    }

    Status = IniCacheSaveByHandle(Cache, FileHandle);

    /* Close the INI file */
    NtClose(FileHandle);
    return Status;
}

/* EOF */

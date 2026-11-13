















#include "leechcore.h"

#ifndef __VMMDLL_H__
#define __VMMDLL_H__
#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32

#include <Windows.h>
#undef EXPORTED_FUNCTION
#define EXPORTED_FUNCTION
typedef unsigned __int64                    QWORD, *PQWORD;

#endif
#if defined(LINUX) || defined(MACOS)

#include <inttypes.h>
#include <stdarg.h>
#include <stdlib.h>
#undef EXPORTED_FUNCTION
#define EXPORTED_FUNCTION                   __attribute__((visibility("default")))
typedef void                                VOID, *PVOID, *HANDLE, **PHANDLE, *HMODULE;
typedef long long unsigned int              QWORD, *PQWORD, ULONG64, *PULONG64;
typedef size_t                              SIZE_T, *PSIZE_T;
typedef uint64_t                            FILETIME, *PFILETIME;
typedef uint32_t                            DWORD, *PDWORD, *LPDWORD, BOOL, *PBOOL, NTSTATUS;
typedef uint16_t                            WORD, *PWORD;
typedef uint8_t                             BYTE, *PBYTE, *LPBYTE, UCHAR;
typedef char                                CHAR, *PCHAR, *LPSTR;
typedef const char                          *LPCSTR;
typedef uint16_t                            WCHAR, *PWCHAR, *LPWSTR;
typedef const uint16_t                      *LPCWSTR;
#define MAX_PATH                            260
#define _In_
#define _In_z_
#define _In_opt_
#define _In_reads_(x)
#define _In_reads_bytes_(x)
#define _In_reads_opt_(x)
#define _Inout_
#define _Inout_bytecount_(x)
#define _Inout_opt_
#define _Inout_updates_opt_(x)
#define _Out_
#define _Out_opt_
#define _Out_writes_(x)
#define _Out_writes_bytes_(x)
#define _Out_writes_bytes_opt_(x)
#define _Out_writes_opt_(x)
#define _Out_writes_to_(x,y)
#define _When_(x,y)
#define _Frees_ptr_opt_
#define _Post_ptr_invalid_
#define _Check_return_opt_
#define _Printf_format_string_
#define _Success_(x)

#endif

typedef struct tdVMM_HANDLE     *VMM_HANDLE;
typedef struct tdVMMVM_HANDLE   *VMMVM_HANDLE;
typedef BYTE                    OPAQUE_OB_HEADER[0x40];


































































EXPORTED_FUNCTION _Success_(return != NULL)
VMM_HANDLE VMMDLL_Initialize(_In_ DWORD argc, _In_ LPCSTR argv[]);

EXPORTED_FUNCTION _Success_(return != NULL)
VMM_HANDLE VMMDLL_InitializeEx(_In_ DWORD argc, _In_ LPCSTR argv[], _Out_opt_ PPLC_CONFIG_ERRORINFO ppLcErrorInfo);





EXPORTED_FUNCTION
VOID VMMDLL_Close(_In_opt_ _Post_ptr_invalid_ VMM_HANDLE hVMM);




EXPORTED_FUNCTION
VOID VMMDLL_CloseAll();






EXPORTED_FUNCTION _Success_(return != 0)
SIZE_T VMMDLL_MemSize(_In_ PVOID pvMem);





EXPORTED_FUNCTION
VOID VMMDLL_MemFree(_Frees_ptr_opt_ PVOID pvMem);















#define VMMDLL_OPT_CORE_PRINTF_ENABLE                   0x4000000100000000
#define VMMDLL_OPT_CORE_VERBOSE                         0x4000000200000000
#define VMMDLL_OPT_CORE_VERBOSE_EXTRA                   0x4000000300000000
#define VMMDLL_OPT_CORE_VERBOSE_EXTRA_TLP               0x4000000400000000
#define VMMDLL_OPT_CORE_MAX_NATIVE_ADDRESS              0x4000000800000000
#define VMMDLL_OPT_CORE_LEECHCORE_HANDLE                0x4000001000000000
#define VMMDLL_OPT_CORE_VMM_ID                          0x4000002000000000

#define VMMDLL_OPT_CORE_SYSTEM                          0x2000000100000000
#define VMMDLL_OPT_CORE_MEMORYMODEL                     0x2000000200000000

#define VMMDLL_OPT_CONFIG_IS_REFRESH_ENABLED            0x2000000300000000
#define VMMDLL_OPT_CONFIG_TICK_PERIOD                   0x2000000400000000
#define VMMDLL_OPT_CONFIG_READCACHE_TICKS               0x2000000500000000
#define VMMDLL_OPT_CONFIG_TLBCACHE_TICKS                0x2000000600000000
#define VMMDLL_OPT_CONFIG_PROCCACHE_TICKS_PARTIAL       0x2000000700000000
#define VMMDLL_OPT_CONFIG_PROCCACHE_TICKS_TOTAL         0x2000000800000000
#define VMMDLL_OPT_CONFIG_VMM_VERSION_MAJOR             0x2000000900000000
#define VMMDLL_OPT_CONFIG_VMM_VERSION_MINOR             0x2000000A00000000
#define VMMDLL_OPT_CONFIG_VMM_VERSION_REVISION          0x2000000B00000000
#define VMMDLL_OPT_CONFIG_STATISTICS_FUNCTIONCALL       0x2000000C00000000
#define VMMDLL_OPT_CONFIG_IS_PAGING_ENABLED             0x2000000D00000000
#define VMMDLL_OPT_CONFIG_DEBUG                         0x2000000E00000000
#define VMMDLL_OPT_CONFIG_YARA_RULES                    0x2000000F00000000

#define VMMDLL_OPT_WIN_VERSION_MAJOR                    0x2000010100000000
#define VMMDLL_OPT_WIN_VERSION_MINOR                    0x2000010200000000
#define VMMDLL_OPT_WIN_VERSION_BUILD                    0x2000010300000000
#define VMMDLL_OPT_WIN_SYSTEM_UNIQUE_ID                 0x2000010400000000

#define VMMDLL_OPT_FORENSIC_MODE                        0x2000020100000000


#define VMMDLL_OPT_REFRESH_ALL                          0x2001ffff00000000
#define VMMDLL_OPT_REFRESH_FREQ_MEM                     0x2001100000000000
#define VMMDLL_OPT_REFRESH_FREQ_MEM_PARTIAL             0x2001000200000000
#define VMMDLL_OPT_REFRESH_FREQ_TLB                     0x2001080000000000
#define VMMDLL_OPT_REFRESH_FREQ_TLB_PARTIAL             0x2001000400000000
#define VMMDLL_OPT_REFRESH_FREQ_FAST                    0x2001040000000000
#define VMMDLL_OPT_REFRESH_FREQ_MEDIUM                  0x2001000100000000
#define VMMDLL_OPT_REFRESH_FREQ_SLOW                    0x2001001000000000

#define VMMDLL_OPT_REFRESH_SPECIFIC_HEAP_ALLOC          0x2003000100000000
#define VMMDLL_OPT_REFRESH_SPECIFIC_KOBJECT             0x2003000200000000
#define VMMDLL_OPT_REFRESH_SPECIFIC_NET                 0x2003000300000000
#define VMMDLL_OPT_REFRESH_SPECIFIC_PFN                 0x2003000400000000
#define VMMDLL_OPT_REFRESH_SPECIFIC_PHYSMEMMAP          0x2003000500000000
#define VMMDLL_OPT_REFRESH_SPECIFIC_POOL                0x2003000600000000
#define VMMDLL_OPT_REFRESH_SPECIFIC_REGISTRY            0x2003000700000000
#define VMMDLL_OPT_REFRESH_SPECIFIC_SERVICES            0x2003000800000000
#define VMMDLL_OPT_REFRESH_SPECIFIC_THREADCS            0x2003000900000000
#define VMMDLL_OPT_REFRESH_SPECIFIC_USER                0x2003000A00000000
#define VMMDLL_OPT_REFRESH_SPECIFIC_VM                  0x2003000B00000000

#define VMMDLL_OPT_REFRESH_SPECIFIC_PROCESS             0x2002000300000000


#define VMMDLL_OPT_PROCESS_DTB                          0x2002000100000000
#define VMMDLL_OPT_PROCESS_DTB_FAST_LOWINTEGRITY        0x2002000200000000

static LPCSTR VMMDLL_MEMORYMODEL_TOSTRING[5] = { "N/A", "X86", "X86PAE", "X64", "ARM64" };

typedef enum tdVMMDLL_MEMORYMODEL_TP {
    VMMDLL_MEMORYMODEL_NA       = 0,
    VMMDLL_MEMORYMODEL_X86      = 1,
    VMMDLL_MEMORYMODEL_X86PAE   = 2,
    VMMDLL_MEMORYMODEL_X64      = 3,
    VMMDLL_MEMORYMODEL_ARM64    = 4,
} VMMDLL_MEMORYMODEL_TP;

typedef enum tdVMMDLL_SYSTEM_TP {
    VMMDLL_SYSTEM_UNKNOWN_PHYSICAL = 0,
    VMMDLL_SYSTEM_UNKNOWN_64    = 1,
    VMMDLL_SYSTEM_WINDOWS_64    = 2,
    VMMDLL_SYSTEM_UNKNOWN_32    = 3,
    VMMDLL_SYSTEM_WINDOWS_32    = 4,
    VMMDLL_SYSTEM_UNKNOWN_X64   = 1,
    VMMDLL_SYSTEM_WINDOWS_X64   = 2,
    VMMDLL_SYSTEM_UNKNOWN_X86   = 3,
    VMMDLL_SYSTEM_WINDOWS_X86   = 4
} VMMDLL_SYSTEM_TP;










EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_ConfigGet(_In_ VMM_HANDLE hVMM, _In_ ULONG64 fOption, _Out_ PULONG64 pqwValue);










EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_ConfigSet(_In_ VMM_HANDLE hVMM, _In_ ULONG64 fOption, _In_ ULONG64 qwValue);







typedef struct tdVMMDLL_MAP_PFN *PVMMDLL_MAP_PFN;






#if defined(LINUX) || defined(MACOS)

#define IMAGE_SIZEOF_SHORT_NAME              8

typedef struct _IMAGE_DATA_DIRECTORY {
    DWORD   VirtualAddress;
    DWORD   Size;
} IMAGE_DATA_DIRECTORY, *PIMAGE_DATA_DIRECTORY;

typedef struct _IMAGE_SECTION_HEADER {
    BYTE    Name[IMAGE_SIZEOF_SHORT_NAME];
    union {
        DWORD   PhysicalAddress;
        DWORD   VirtualSize;
    } Misc;
    DWORD   VirtualAddress;
    DWORD   SizeOfRawData;
    DWORD   PointerToRawData;
    DWORD   PointerToRelocations;
    DWORD   PointerToLinenumbers;
    WORD    NumberOfRelocations;
    WORD    NumberOfLinenumbers;
    DWORD   Characteristics;
} IMAGE_SECTION_HEADER, *PIMAGE_SECTION_HEADER;

typedef struct _SERVICE_STATUS {
    DWORD   dwServiceType;
    DWORD   dwCurrentState;
    DWORD   dwControlsAccepted;
    DWORD   dwWin32ExitCode;
    DWORD   dwServiceSpecificExitCode;
    DWORD   dwCheckPoint;
    DWORD   dwWaitHint;
} SERVICE_STATUS, *LPSERVICE_STATUS;
#endif











#define VMMDLL_STATUS_SUCCESS                       ((NTSTATUS)0x00000000L)
#define VMMDLL_STATUS_UNSUCCESSFUL                  ((NTSTATUS)0xC0000001L)
#define VMMDLL_STATUS_END_OF_FILE                   ((NTSTATUS)0xC0000011L)
#define VMMDLL_STATUS_FILE_INVALID                  ((NTSTATUS)0xC0000098L)
#define VMMDLL_STATUS_FILE_SYSTEM_LIMITATION        ((NTSTATUS)0xC0000427L)

#define VMMDLL_VFS_FILELIST_EXINFO_VERSION          1
#define VMMDLL_VFS_FILELIST_VERSION                 2
#define VMMDLL_VFS_FILELISTBLOB_VERSION             0xf88f0001

typedef struct tdVMMDLL_VFS_FILELIST_EXINFO {
    DWORD dwVersion;
    BOOL fCompressed;
    union {
        FILETIME ftCreationTime;
        QWORD qwCreationTime;
    };
    union {
        FILETIME ftLastAccessTime;
        QWORD qwLastAccessTime;
    };
    union {
        FILETIME ftLastWriteTime;
        QWORD qwLastWriteTime;
    };
} VMMDLL_VFS_FILELIST_EXINFO, *PVMMDLL_VFS_FILELIST_EXINFO;

typedef struct tdVMMDLL_VFS_FILELIST2 {
    DWORD dwVersion;
    VOID(*pfnAddFile)     (_Inout_ HANDLE h, _In_ LPCSTR uszName, _In_ ULONG64 cb, _In_opt_ PVMMDLL_VFS_FILELIST_EXINFO pExInfo);
    VOID(*pfnAddDirectory)(_Inout_ HANDLE h, _In_ LPCSTR uszName, _In_opt_ PVMMDLL_VFS_FILELIST_EXINFO pExInfo);
    HANDLE h;
} VMMDLL_VFS_FILELIST2, *PVMMDLL_VFS_FILELIST2;

typedef struct tdVMMDLL_VFS_FILELISTBLOB_ENTRY {
    ULONG64 ouszName;
    ULONG64 cbFileSize;
    VMMDLL_VFS_FILELIST_EXINFO ExInfo;
} VMMDLL_VFS_FILELISTBLOB_ENTRY, *PVMMDLL_VFS_FILELISTBLOB_ENTRY;

typedef struct tdVMMDLL_VFS_FILELISTBLOB {
    DWORD dwVersion;
    DWORD cbStruct;
    DWORD cFileEntry;
    DWORD cbMultiText;
    union {
        LPSTR uszMultiText;
        QWORD _Reserved;
    };
    DWORD _FutureUse[8];
    VMMDLL_VFS_FILELISTBLOB_ENTRY FileEntry[0];
} VMMDLL_VFS_FILELISTBLOB, *PVMMDLL_VFS_FILELISTBLOB;




EXPORTED_FUNCTION
VOID VMMDLL_VfsList_AddFile(_In_ HANDLE pFileList, _In_ LPCSTR uszName, _In_ ULONG64 cb, _In_opt_ PVMMDLL_VFS_FILELIST_EXINFO pExInfo);
VOID VMMDLL_VfsList_AddFileW(_In_ HANDLE pFileList, _In_ LPCWSTR wszName, _In_ ULONG64 cb, _In_opt_ PVMMDLL_VFS_FILELIST_EXINFO pExInfo);
EXPORTED_FUNCTION
VOID VMMDLL_VfsList_AddDirectory(_In_ HANDLE pFileList, _In_ LPCSTR uszName, _In_opt_ PVMMDLL_VFS_FILELIST_EXINFO pExInfo);
VOID VMMDLL_VfsList_AddDirectoryW(_In_ HANDLE pFileList, _In_ LPCWSTR wszName, _In_opt_ PVMMDLL_VFS_FILELIST_EXINFO pExInfo);
EXPORTED_FUNCTION BOOL VMMDLL_VfsList_IsHandleValid(_In_ HANDLE pFileList);











EXPORTED_FUNCTION
_Success_(return) BOOL VMMDLL_VfsListU(_In_ VMM_HANDLE hVMM, _In_ LPCSTR  uszPath, _Inout_ PVMMDLL_VFS_FILELIST2 pFileList);
_Success_(return) BOOL VMMDLL_VfsListW(_In_ VMM_HANDLE hVMM, _In_ LPCWSTR wszPath, _Inout_ PVMMDLL_VFS_FILELIST2 pFileList);








EXPORTED_FUNCTION
_Success_(return != NULL) PVMMDLL_VFS_FILELISTBLOB VMMDLL_VfsListBlobU(_In_ VMM_HANDLE hVMM, _In_ LPCSTR uszPath);












EXPORTED_FUNCTION
NTSTATUS VMMDLL_VfsReadU(_In_ VMM_HANDLE hVMM, _In_ LPCSTR  uszFileName, _Out_writes_to_(cb, *pcbRead) PBYTE pb, _In_ DWORD cb, _Out_ PDWORD pcbRead, _In_ ULONG64 cbOffset);
NTSTATUS VMMDLL_VfsReadW(_In_ VMM_HANDLE hVMM, _In_ LPCWSTR wszFileName, _Out_writes_to_(cb, *pcbRead) PBYTE pb, _In_ DWORD cb, _Out_ PDWORD pcbRead, _In_ ULONG64 cbOffset);











EXPORTED_FUNCTION
NTSTATUS VMMDLL_VfsWriteU(_In_ VMM_HANDLE hVMM, _In_ LPCSTR  uszFileName, _In_reads_(cb) PBYTE pb, _In_ DWORD cb, _Out_ PDWORD pcbWrite, _In_ ULONG64 cbOffset);
NTSTATUS VMMDLL_VfsWriteW(_In_ VMM_HANDLE hVMM, _In_ LPCWSTR wszFileName, _In_reads_(cb) PBYTE pb, _In_ DWORD cb, _Out_ PDWORD pcbWrite, _In_ ULONG64 cbOffset);





EXPORTED_FUNCTION NTSTATUS VMMDLL_UtilVfsReadFile_FromPBYTE(_In_ PBYTE pbFile, _In_ ULONG64 cbFile, _Out_writes_to_(cb, *pcbRead) PBYTE pb, _In_ DWORD cb, _Out_ PDWORD pcbRead, _In_ ULONG64 cbOffset);
EXPORTED_FUNCTION NTSTATUS VMMDLL_UtilVfsReadFile_FromQWORD(_In_ ULONG64 qwValue, _Out_writes_to_(cb, *pcbRead) PBYTE pb, _In_ DWORD cb, _Out_ PDWORD pcbRead, _In_ ULONG64 cbOffset, _In_ BOOL fPrefix);
EXPORTED_FUNCTION NTSTATUS VMMDLL_UtilVfsReadFile_FromDWORD(_In_ DWORD dwValue, _Out_writes_to_(cb, *pcbRead) PBYTE pb, _In_ DWORD cb, _Out_ PDWORD pcbRead, _In_ ULONG64 cbOffset, _In_ BOOL fPrefix);
EXPORTED_FUNCTION NTSTATUS VMMDLL_UtilVfsReadFile_FromBOOL(_In_ BOOL fValue, _Out_writes_to_(cb, *pcbRead) PBYTE pb, _In_ DWORD cb, _Out_ PDWORD pcbRead, _In_ ULONG64 cbOffset);
EXPORTED_FUNCTION NTSTATUS VMMDLL_UtilVfsWriteFile_BOOL(_Inout_ PBOOL pfTarget, _In_reads_(cb) PBYTE pb, _In_ DWORD cb, _Out_ PDWORD pcbWrite, _In_ ULONG64 cbOffset);
EXPORTED_FUNCTION NTSTATUS VMMDLL_UtilVfsWriteFile_DWORD(_Inout_ PDWORD pdwTarget, _In_reads_(cb) PBYTE pb, _In_ DWORD cb, _Out_ PDWORD pcbWrite, _In_ ULONG64 cbOffset, _In_ DWORD dwMinAllow);























EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_InitializePlugins(_In_ VMM_HANDLE hVMM);

#define VMMDLL_PLUGIN_CONTEXT_MAGIC                 0xc0ffee663df9301c
#define VMMDLL_PLUGIN_CONTEXT_VERSION               5
#define VMMDLL_PLUGIN_REGINFO_MAGIC                 0xc0ffee663df9301d
#define VMMDLL_PLUGIN_REGINFO_VERSION               18
#define VMMDLL_FORENSIC_JSONDATA_VERSION            0xc0ee0002
#define VMMDLL_FORENSIC_INGEST_VIRTMEM_VERSION      0xc0dd0001
#define VMMDLL_FORENSIC_INGEST_OBJECT_VERSION       0xc0de0001

#define VMMDLL_PLUGIN_NOTIFY_VERBOSITYCHANGE        0x01
#define VMMDLL_PLUGIN_NOTIFY_REFRESH_FAST           0x05
#define VMMDLL_PLUGIN_NOTIFY_REFRESH_MEDIUM         0x02
#define VMMDLL_PLUGIN_NOTIFY_REFRESH_SLOW           0x04

#define VMMDLL_PLUGIN_NOTIFY_FORENSIC_INIT          0x01000100
#define VMMDLL_PLUGIN_NOTIFY_FORENSIC_INIT_COMPLETE 0x01000200
#define VMMDLL_PLUGIN_NOTIFY_VM_ATTACH_DETACH       0x01000400

typedef DWORD                                       VMMDLL_MODULE_ID;
typedef HANDLE                                      *PVMMDLL_PLUGIN_INTERNAL_CONTEXT;
typedef struct tdVMMDLL_CSV_HANDLE                  *VMMDLL_CSV_HANDLE;

#define VMMDLL_MID_MAIN                             ((VMMDLL_MODULE_ID)0x80000001)
#define VMMDLL_MID_PYTHON                           ((VMMDLL_MODULE_ID)0x80000002)
#define VMMDLL_MID_DEBUG                            ((VMMDLL_MODULE_ID)0x80000003)
#define VMMDLL_MID_RUST                             ((VMMDLL_MODULE_ID)0x80000004)

typedef struct tdVMMDLL_PLUGIN_CONTEXT {
    ULONG64 magic;
    WORD wVersion;
    WORD wSize;
    DWORD dwPID;
    PVOID pProcess;
    LPSTR uszModule;
    LPSTR uszPath;
    PVOID pvReserved1;
    PVMMDLL_PLUGIN_INTERNAL_CONTEXT ctxM;
    VMMDLL_MODULE_ID MID;
} VMMDLL_PLUGIN_CONTEXT, *PVMMDLL_PLUGIN_CONTEXT;

typedef struct tdVMMDLL_FORENSIC_JSONDATA {
    DWORD dwVersion;
    DWORD _FutureUse;
    LPSTR szjType;
    DWORD i;
    DWORD dwPID;
    QWORD vaObj;
    BOOL fva[2];
    QWORD va[2];
    BOOL fNum[2];
    QWORD qwNum[2];
    BOOL fHex[2];
    QWORD qwHex[2];

    LPCSTR usz[2];
    LPCWSTR wsz[2];
    BYTE _Reserved[0x4000+256];
} VMMDLL_FORENSIC_JSONDATA, *PVMMDLL_FORENSIC_JSONDATA;

typedef enum tdVMMDLL_FORENSIC_INGEST_OBJECT_TYPE {
    VMMDLL_FORENSIC_INGEST_OBJECT_TYPE_FILE = 1,
} VMMDLL_FORENSIC_INGEST_OBJECT_TYPE;

typedef struct tdVMMDLL_FORENSIC_INGEST_OBJECT {
    OPAQUE_OB_HEADER _Reserved;
    DWORD dwVersion;
    VMMDLL_FORENSIC_INGEST_OBJECT_TYPE tp;
    QWORD vaObject;
    LPSTR uszText;
    PBYTE pb;
    DWORD cb;
    DWORD cbReadActual;
} VMMDLL_FORENSIC_INGEST_OBJECT, *PVMMDLL_FORENSIC_INGEST_OBJECT;

typedef struct tdVMMDLL_FORENSIC_INGEST_PHYSMEM {
    BOOL fValid;
    QWORD pa;
    DWORD cb;
    PBYTE pb;
    DWORD cMEMs;
    PPMEM_SCATTER ppMEMs;
    PVMMDLL_MAP_PFN pPfnMap;
} VMMDLL_FORENSIC_INGEST_PHYSMEM, *PVMMDLL_FORENSIC_INGEST_PHYSMEM;

typedef struct tdVMMDLL_FORENSIC_INGEST_VIRTMEM {
    OPAQUE_OB_HEADER _Reserved;
    DWORD dwVersion;
    BOOL fPte;
    BOOL fVad;
    PVOID pvProcess;
    DWORD dwPID;
    QWORD va;
    PBYTE pb;
    DWORD cb;
    DWORD cbReadActual;
} VMMDLL_FORENSIC_INGEST_VIRTMEM, *PVMMDLL_FORENSIC_INGEST_VIRTMEM;

typedef struct tdVMMDLL_PLUGIN_REGINFO {
    ULONG64 magic;
    WORD wVersion;
    WORD wSize;
    VMMDLL_MEMORYMODEL_TP tpMemoryModel;
    VMMDLL_SYSTEM_TP tpSystem;
    HMODULE hDLL;
    BOOL(*pfnPluginManager_Register)(_In_ VMM_HANDLE H, struct tdVMMDLL_PLUGIN_REGINFO *pPluginRegInfo);
    LPSTR uszPathVmmDLL;
    DWORD _Reserved[30];

    struct {
        BOOL fPythonStandalone;
        DWORD _Reserved;
        HMODULE hReservedDllPython3;
        HMODULE hReservedDllPython3X;
    } python;

    struct {
        PVMMDLL_PLUGIN_INTERNAL_CONTEXT ctxM;
        CHAR uszPathName[128];
        BOOL fRootModule;
        BOOL fProcessModule;
        BOOL fRootModuleHidden;
        BOOL fProcessModuleHidden;
        CHAR sTimelineNameShort[6];
        CHAR _Reserved[2];
        CHAR uszTimelineFile[32];
        CHAR _Reserved2[32];
    } reg_info;

    struct {
        BOOL(*pfnList)(_In_ VMM_HANDLE H, _In_ PVMMDLL_PLUGIN_CONTEXT ctxP, _Inout_ PHANDLE pFileList);
        NTSTATUS(*pfnRead)(_In_ VMM_HANDLE H, _In_ PVMMDLL_PLUGIN_CONTEXT ctxP, _Out_writes_to_(cb, *pcbRead) PBYTE pb, _In_ DWORD cb, _Out_ PDWORD pcbRead,  _In_ ULONG64 cbOffset);
        NTSTATUS(*pfnWrite)(_In_ VMM_HANDLE H, _In_ PVMMDLL_PLUGIN_CONTEXT ctxP, _In_reads_(cb) PBYTE pb, _In_ DWORD cb, _Out_ PDWORD pcbWrite, _In_ ULONG64 cbOffset);
        VOID(*pfnNotify)(_In_ VMM_HANDLE H, _In_ PVMMDLL_PLUGIN_CONTEXT ctxP, _In_ DWORD fEvent, _In_opt_ PVOID pvEvent, _In_opt_ DWORD cbEvent);
        VOID(*pfnClose)(_In_ VMM_HANDLE H, _In_ PVMMDLL_PLUGIN_CONTEXT ctxP);
        BOOL(*pfnVisibleModule)(_In_ VMM_HANDLE H, _In_ PVMMDLL_PLUGIN_CONTEXT ctxP);
        PVOID pvReserved[10];
    } reg_fn;













    struct {
        PVOID(*pfnInitialize)(_In_ VMM_HANDLE H, _In_ PVMMDLL_PLUGIN_CONTEXT ctxP);
        VOID(*pfnFinalize)(_In_ VMM_HANDLE H, _In_opt_ PVOID ctxfc);
        VOID(*pfnTimeline)(
            _In_ VMM_HANDLE H,
            _In_opt_ PVOID ctxfc,
            _In_ HANDLE hTimeline,
            _In_ VOID(*pfnAddEntry)(_In_ VMM_HANDLE H, _In_ HANDLE hTimeline, _In_ QWORD ft, _In_ DWORD dwAction, _In_ DWORD dwPID, _In_ DWORD dwData32, _In_ QWORD qwData64, _In_ LPCSTR uszText),
            _In_ VOID(*pfnEntryAddBySql)(_In_ VMM_HANDLE H, _In_ HANDLE hTimeline, _In_ DWORD cEntrySql, _In_ LPCSTR *pszEntrySql));
        VOID(*pfnIngestObject)(_In_ VMM_HANDLE H, _In_opt_ PVOID ctxfc, _In_ PVMMDLL_FORENSIC_INGEST_OBJECT pIngestObject);
        VOID(*pfnIngestPhysmem)(_In_ VMM_HANDLE H, _In_opt_ PVOID ctxfc, _In_ PVMMDLL_FORENSIC_INGEST_PHYSMEM pIngestPhysmem);
        VOID(*pfnIngestVirtmem)(_In_ VMM_HANDLE H, _In_opt_ PVOID ctxfc, _In_ PVMMDLL_FORENSIC_INGEST_VIRTMEM pIngestVirtmem);
        VOID(*pfnIngestFinalize)(_In_ VMM_HANDLE H, _In_opt_ PVOID ctxfc);
        VOID(*pfnFindEvil)(_In_ VMM_HANDLE H, _In_ VMMDLL_MODULE_ID MID, _In_opt_ PVOID ctxfc);
        PVOID pvReserved[6];
        VOID(*pfnLogCSV)(_In_ VMM_HANDLE H, _In_ PVMMDLL_PLUGIN_CONTEXT ctxP, _In_ VMMDLL_CSV_HANDLE hCSV);
        VOID(*pfnLogJSON)(_In_ VMM_HANDLE H, _In_ PVMMDLL_PLUGIN_CONTEXT ctxP, _In_ VOID(*pfnLogJSON)(_In_ VMM_HANDLE H, _In_ PVMMDLL_FORENSIC_JSONDATA pData));
    } reg_fnfc;

    struct {
        BOOL f32;
        DWORD dwVersionMajor;
        DWORD dwVersionMinor;
        DWORD dwVersionBuild;
        DWORD _Reserved[32];
    } sysinfo;
} VMMDLL_PLUGIN_REGINFO, *PVMMDLL_PLUGIN_REGINFO;
















EXPORTED_FUNCTION _Success_(return != 0)
SIZE_T VMMDLL_ForensicFileAppend(
    _In_ VMM_HANDLE H,
    _In_ LPCSTR uszFileName,
    _In_z_ _Printf_format_string_ LPCSTR uszFormat,
    ...
);









typedef enum tdVMMDLL_LOGLEVEL {
    VMMDLL_LOGLEVEL_CRITICAL = 1,
    VMMDLL_LOGLEVEL_WARNING  = 2,
    VMMDLL_LOGLEVEL_INFO     = 3,
    VMMDLL_LOGLEVEL_VERBOSE  = 4,
    VMMDLL_LOGLEVEL_DEBUG    = 5,
    VMMDLL_LOGLEVEL_TRACE    = 6,
    VMMDLL_LOGLEVEL_NONE     = 7,
} VMMDLL_LOGLEVEL;











EXPORTED_FUNCTION
VOID VMMDLL_Log(
    _In_ VMM_HANDLE hVMM,
    _In_opt_ VMMDLL_MODULE_ID MID,
    _In_ VMMDLL_LOGLEVEL dwLogLevel,
    _In_z_ _Printf_format_string_ LPCSTR uszFormat,
    ...
);











EXPORTED_FUNCTION
VOID VMMDLL_LogEx(
    _In_ VMM_HANDLE hVMM,
    _In_opt_ VMMDLL_MODULE_ID MID,
    _In_ VMMDLL_LOGLEVEL dwLogLevel,
    _In_z_ _Printf_format_string_ LPCSTR uszFormat,
    va_list arglist
);









typedef VOID(*VMMDLL_LOG_CALLBACK_PFN)(_In_ VMM_HANDLE hVMM, _In_ VMMDLL_MODULE_ID MID, _In_ LPCSTR uszModule, _In_ VMMDLL_LOGLEVEL dwLogLevel, _In_ LPCSTR uszLogMessage);












EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_LogCallback(_In_ VMM_HANDLE hVMM, _In_opt_ VMMDLL_LOG_CALLBACK_PFN pfnCB);











#define VMMDLL_PID_PROCESS_WITH_KERNELMEMORY        0x80000000




#define VMMDLL_FLAG_NOCACHE                         0x0001
#define VMMDLL_FLAG_ZEROPAD_ON_FAIL                 0x0002
#define VMMDLL_FLAG_FORCECACHE_READ                 0x0008
#define VMMDLL_FLAG_NOPAGING                        0x0010
#define VMMDLL_FLAG_NOPAGING_IO                     0x0020
#define VMMDLL_FLAG_NOCACHEPUT                      0x0100
#define VMMDLL_FLAG_CACHE_RECENT_ONLY               0x0200
#define VMMDLL_FLAG_NO_PREDICTIVE_READ              0x0400
#define VMMDLL_FLAG_FORCECACHE_READ_DISABLE         0x0800
#define VMMDLL_FLAG_SCATTER_PREPAREEX_NOMEMZERO     0x1000
#define VMMDLL_FLAG_NOMEMCALLBACK                   0x2000
#define VMMDLL_FLAG_SCATTER_FORCE_PAGEREAD          0x4000















EXPORTED_FUNCTION
DWORD VMMDLL_MemReadScatter(_In_ VMM_HANDLE hVMM, _In_ DWORD dwPID, _Inout_ PPMEM_SCATTER ppMEMs, _In_ DWORD cpMEMs, _In_ DWORD flags);












EXPORTED_FUNCTION
DWORD VMMDLL_MemWriteScatter(_In_ VMM_HANDLE hVMM, _In_ DWORD dwPID, _Inout_ PPMEM_SCATTER ppMEMs, _In_ DWORD cpMEMs);









EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_MemReadPage(_In_ VMM_HANDLE hVMM, _In_ DWORD dwPID, _In_ ULONG64 qwA, _Inout_bytecount_(4096) PBYTE pbPage);










EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_MemRead(_In_ VMM_HANDLE hVMM, _In_ DWORD dwPID, _In_ ULONG64 qwA, _Out_writes_(cb) PBYTE pb, _In_ DWORD cb);













EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_MemReadEx(_In_ VMM_HANDLE hVMM, _In_ DWORD dwPID, _In_ ULONG64 qwA, _Out_writes_(cb) PBYTE pb, _In_ DWORD cb, _Out_opt_ PDWORD pcbReadOpt, _In_ ULONG64 flags);











EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_MemPrefetchPages(_In_ VMM_HANDLE hVMM, _In_ DWORD dwPID, _In_reads_(cPrefetchAddresses) PULONG64 pPrefetchAddresses, _In_ DWORD cPrefetchAddresses);















EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_MemWrite(_In_ VMM_HANDLE hVMM, _In_ DWORD dwPID, _In_ ULONG64 qwA, _In_reads_(cb) PBYTE pb, _In_ DWORD cb);










EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_MemVirt2Phys(_In_ VMM_HANDLE hVMM, _In_ DWORD dwPID, _In_ ULONG64 qwVA, _Out_ PULONG64 pqwPA);


typedef struct tdVMMDLL_MEMORY_BASIC_INFORMATION {
    QWORD BaseAddress;
    QWORD AllocationBase;
    DWORD AllocationProtect;
    WORD  PartitionId;
    WORD  _Reserved1;
    DWORD _Reserved2;
    QWORD RegionSize;
    DWORD State;
    DWORD Protect;
    DWORD Type;
    DWORD _Reserved3;
} VMMDLL_MEMORY_BASIC_INFORMATION, *PVMMDLL_MEMORY_BASIC_INFORMATION;












EXPORTED_FUNCTION _Success_(return != 0)
SIZE_T VMMDLL_MemVirtualQuery(
    _In_ VMM_HANDLE hVMM,
    _In_ DWORD dwPID,
    _In_ QWORD lpAddress,
    _Out_writes_bytes_(dwLength) PVMMDLL_MEMORY_BASIC_INFORMATION lpBuffer,
    _In_ SIZE_T dwLength
);





















typedef HANDLE      VMMDLL_SCATTER_HANDLE;









EXPORTED_FUNCTION _Success_(return != NULL)
VMMDLL_SCATTER_HANDLE VMMDLL_Scatter_Initialize(_In_ VMM_HANDLE hVMM, _In_ DWORD dwPID, _In_ DWORD flags);









EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_Scatter_Prepare(_In_ VMMDLL_SCATTER_HANDLE hS, _In_ QWORD va, _In_ DWORD cb);













EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_Scatter_PrepareEx(_In_ VMMDLL_SCATTER_HANDLE hS, _In_ QWORD va, _In_ DWORD cb, _Out_writes_opt_(cb) PBYTE pb, _Out_opt_ PDWORD pcbRead);













EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_Scatter_PrepareWrite(_In_ VMMDLL_SCATTER_HANDLE hS, _In_ QWORD va, _In_reads_(cb) PBYTE pb, _In_ DWORD cb);













EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_Scatter_PrepareWriteEx(_In_ VMMDLL_SCATTER_HANDLE hS, _In_ QWORD va, _In_reads_(cb) PBYTE pb, _In_ DWORD cb);









EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_Scatter_Execute(_In_ VMMDLL_SCATTER_HANDLE hS);







EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_Scatter_ExecuteRead(_In_ VMMDLL_SCATTER_HANDLE hS);











EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_Scatter_Read(_In_ VMMDLL_SCATTER_HANDLE hS, _In_ QWORD va, _In_ DWORD cb, _Out_writes_opt_(cb) PBYTE pb, _Out_opt_ PDWORD pcbRead);








EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_Scatter_Clear(_In_ VMMDLL_SCATTER_HANDLE hS, _In_opt_ DWORD dwPID, _In_ DWORD flags);





EXPORTED_FUNCTION
VOID VMMDLL_Scatter_CloseHandle(_In_opt_ _Post_ptr_invalid_ VMMDLL_SCATTER_HANDLE hS);










typedef enum tdVMMDLL_MEM_CALLBACK_TP {
    VMMDLL_MEM_CALLBACK_READ_PHYSICAL_PRE = 1,
    VMMDLL_MEM_CALLBACK_READ_PHYSICAL_POST = 2,
    VMMDLL_MEM_CALLBACK_WRITE_PHYSICAL_PRE = 3,
    VMMDLL_MEM_CALLBACK_READ_VIRTUAL_PRE = 4,
    VMMDLL_MEM_CALLBACK_READ_VIRTUAL_POST = 5,
    VMMDLL_MEM_CALLBACK_WRITE_VIRTUAL_PRE = 6,
} VMMDLL_MEM_CALLBACK_TP;








typedef VOID(*VMMDLL_MEM_CALLBACK_PFN)(_In_opt_ PVOID ctxUser, _In_ DWORD dwPID, _In_ DWORD cpMEMs, _In_ PPMEM_SCATTER ppMEMs);











EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_MemCallback(_In_ VMM_HANDLE hVMM, _In_ VMMDLL_MEM_CALLBACK_TP tp, _In_opt_ PVOID ctxUser, _In_opt_ VMMDLL_MEM_CALLBACK_PFN pfnCB);










#define VMMDLL_MAP_PTE_VERSION              2
#define VMMDLL_MAP_VAD_VERSION              6
#define VMMDLL_MAP_VADEX_VERSION            4
#define VMMDLL_MAP_MODULE_VERSION           6
#define VMMDLL_MAP_UNLOADEDMODULE_VERSION   2
#define VMMDLL_MAP_EAT_VERSION              3
#define VMMDLL_MAP_IAT_VERSION              2
#define VMMDLL_MAP_HEAP_VERSION             4
#define VMMDLL_MAP_HEAPALLOC_VERSION        1
#define VMMDLL_MAP_THREAD_VERSION           4
#define VMMDLL_MAP_THREAD_CALLSTACK_VERSION 1
#define VMMDLL_MAP_HANDLE_VERSION           3
#define VMMDLL_MAP_POOL_VERSION             2
#define VMMDLL_MAP_KOBJECT_VERSION          1
#define VMMDLL_MAP_KDRIVER_VERSION          1
#define VMMDLL_MAP_KDEVICE_VERSION          1
#define VMMDLL_MAP_NET_VERSION              3
#define VMMDLL_MAP_PHYSMEM_VERSION          2
#define VMMDLL_MAP_USER_VERSION             2
#define VMMDLL_MAP_VM_VERSION               2
#define VMMDLL_MAP_SERVICE_VERSION          3


#define VMMDLL_MEMMAP_FLAG_PAGE_W           0x0000000000000002
#define VMMDLL_MEMMAP_FLAG_PAGE_NS          0x0000000000000004
#define VMMDLL_MEMMAP_FLAG_PAGE_NX          0x8000000000000000
#define VMMDLL_MEMMAP_FLAG_PAGE_MASK        0x8000000000000006

#define VMMDLL_POOLMAP_FLAG_ALL             0
#define VMMDLL_POOLMAP_FLAG_BIG             1

#define VMMDLL_MODULE_FLAG_NORMAL           0
#define VMMDLL_MODULE_FLAG_DEBUGINFO        1
#define VMMDLL_MODULE_FLAG_VERSIONINFO      2

typedef enum tdVMMDLL_PTE_TP {
    VMMDLL_PTE_TP_NA = 0,
    VMMDLL_PTE_TP_HARDWARE = 1,
    VMMDLL_PTE_TP_TRANSITION = 2,
    VMMDLL_PTE_TP_PROTOTYPE = 3,
    VMMDLL_PTE_TP_DEMANDZERO = 4,
    VMMDLL_PTE_TP_COMPRESSED = 5,
    VMMDLL_PTE_TP_PAGEFILE = 6,
    VMMDLL_PTE_TP_FILE = 7,
} VMMDLL_PTE_TP, *PVMMDLL_PTE_TP;

typedef struct tdVMMDLL_MAP_PTEENTRY {
    QWORD vaBase;
    QWORD cPages;
    QWORD fPage;
    BOOL  fWoW64;
    DWORD _FutureUse1;
    union { LPSTR  uszText; LPWSTR wszText; };
    DWORD _Reserved1;
    DWORD cSoftware;
} VMMDLL_MAP_PTEENTRY, *PVMMDLL_MAP_PTEENTRY;

typedef struct tdVMMDLL_MAP_VADENTRY {
    QWORD vaStart;
    QWORD vaEnd;
    QWORD vaVad;

    DWORD VadType           : 3;
    DWORD Protection        : 5;
    DWORD fImage            : 1;
    DWORD fFile             : 1;
    DWORD fPageFile         : 1;
    DWORD fPrivateMemory    : 1;
    DWORD fTeb              : 1;
    DWORD fStack            : 1;
    DWORD fSpare            : 2;
    DWORD HeapNum           : 7;
    DWORD fHeap             : 1;
    DWORD cwszDescription   : 8;

    DWORD CommitCharge      : 31;
    DWORD MemCommit         : 1;
    DWORD u2;
    DWORD cbPrototypePte;
    QWORD vaPrototypePte;
    QWORD vaSubsection;
    union { LPSTR  uszText; LPWSTR wszText; };
    DWORD _FutureUse1;
    DWORD _Reserved1;
    QWORD vaFileObject;
    DWORD cVadExPages;
    DWORD cVadExPagesBase;
    QWORD _Reserved2;
} VMMDLL_MAP_VADENTRY, *PVMMDLL_MAP_VADENTRY;

#define VMMDLL_VADEXENTRY_FLAG_HARDWARE     0x01
#define VMMDLL_VADEXENTRY_FLAG_W            0x10
#define VMMDLL_VADEXENTRY_FLAG_K            0x40
#define VMMDLL_VADEXENTRY_FLAG_NX           0x80

typedef struct tdVMMDLL_MAP_VADEXENTRY {
    VMMDLL_PTE_TP tp;
    BYTE iPML;
    BYTE pteFlags;
    WORD _Reserved2;
    QWORD va;
    QWORD pa;
    QWORD pte;
    struct {
        DWORD _Reserved1;
        VMMDLL_PTE_TP tp;
        QWORD pa;
        QWORD pte;
    } proto;
    QWORD vaVadBase;
} VMMDLL_MAP_VADEXENTRY, *PVMMDLL_MAP_VADEXENTRY;

typedef enum tdVMMDLL_MODULE_TP {
    VMMDLL_MODULE_TP_NORMAL = 0,
    VMMDLL_MODULE_TP_DATA = 1,
    VMMDLL_MODULE_TP_NOTLINKED = 2,
    VMMDLL_MODULE_TP_INJECTED = 3,
} VMMDLL_MODULE_TP;

typedef struct tdVMMDLL_MAP_MODULEENTRY_DEBUGINFO {
    DWORD dwAge;
    DWORD _Reserved;
    BYTE Guid[16];
    union { LPSTR  uszGuid;             LPWSTR wszGuid;                 };
    union { LPSTR  uszPdbFilename;      LPWSTR wszPdbFilename;          };
} VMMDLL_MAP_MODULEENTRY_DEBUGINFO, *PVMMDLL_MAP_MODULEENTRY_DEBUGINFO;

typedef struct tdVMMDLL_MAP_MODULEENTRY_VERSIONINFO {
    union { LPSTR  uszCompanyName;      LPWSTR wszCompanyName;          };
    union { LPSTR  uszFileDescription;  LPWSTR wszFileDescription;      };
    union { LPSTR  uszFileVersion;      LPWSTR wszFileVersion;          };
    union { LPSTR  uszInternalName;     LPWSTR wszInternalName;         };
    union { LPSTR  uszLegalCopyright;   LPWSTR wszLegalCopyright;       };
    union { LPSTR  uszOriginalFilename; LPWSTR wszFileOriginalFilename; };
    union { LPSTR  uszProductName;      LPWSTR wszProductName;          };
    union { LPSTR  uszProductVersion;   LPWSTR wszProductVersion;       };
} VMMDLL_MAP_MODULEENTRY_VERSIONINFO, *PVMMDLL_MAP_MODULEENTRY_VERSIONINFO;

typedef struct tdVMMDLL_MAP_MODULEENTRY {
    QWORD vaBase;
    QWORD vaEntry;
    DWORD cbImageSize;
    BOOL  fWoW64;
    union { LPSTR  uszText; LPWSTR wszText; };
    DWORD _Reserved3;
    DWORD _Reserved4;
    union { LPSTR  uszFullName; LPWSTR wszFullName; };
    VMMDLL_MODULE_TP tp;
    DWORD cbFileSizeRaw;
    DWORD cSection;
    DWORD cEAT;
    DWORD cIAT;
    DWORD _Reserved2;
    QWORD _Reserved1[3];
    PVMMDLL_MAP_MODULEENTRY_DEBUGINFO pExDebugInfo;
    PVMMDLL_MAP_MODULEENTRY_VERSIONINFO pExVersionInfo;
} VMMDLL_MAP_MODULEENTRY, *PVMMDLL_MAP_MODULEENTRY;

typedef struct tdVMMDLL_MAP_UNLOADEDMODULEENTRY {
    QWORD vaBase;
    DWORD cbImageSize;
    BOOL  fWoW64;
    union { LPSTR  uszText; LPWSTR wszText; };
    DWORD _FutureUse1;
    DWORD dwCheckSum;
    DWORD dwTimeDateStamp;
    DWORD _Reserved1;
    QWORD ftUnload;
} VMMDLL_MAP_UNLOADEDMODULEENTRY, *PVMMDLL_MAP_UNLOADEDMODULEENTRY;

typedef struct tdVMMDLL_MAP_EATENTRY {
    QWORD vaFunction;
    DWORD dwOrdinal;
    DWORD oFunctionsArray;
    DWORD oNamesArray;
    DWORD _FutureUse1;
    union { LPSTR  uszFunction; LPWSTR wszFunction; };
    union { LPSTR  uszForwardedFunction; LPWSTR wszForwardedFunction; };
} VMMDLL_MAP_EATENTRY, *PVMMDLL_MAP_EATENTRY;

typedef struct tdVMMDLL_MAP_IATENTRY {
    QWORD vaFunction;
    union { LPSTR  uszFunction; LPWSTR wszFunction; };
    DWORD _FutureUse1;
    DWORD _FutureUse2;
    union { LPSTR  uszModule; LPWSTR wszModule; };
    struct {
        BOOL f32;
        WORD wHint;
        WORD _Reserved1;
        DWORD rvaFirstThunk;
        DWORD rvaOriginalFirstThunk;
        DWORD rvaNameModule;
        DWORD rvaNameFunction;
    } Thunk;
} VMMDLL_MAP_IATENTRY, *PVMMDLL_MAP_IATENTRY;

typedef enum tdVMMDLL_HEAP_TP {
    VMMDLL_HEAP_TP_NA   = 0,
    VMMDLL_HEAP_TP_NT   = 1,
    VMMDLL_HEAP_TP_SEG  = 2,
} VMMDLL_HEAP_TP, *PVMMDLL_HEAP_TP;

typedef enum tdVMMDLL_HEAP_SEGMENT_TP {
    VMMDLL_HEAP_SEGMENT_TP_NA           = 0,
    VMMDLL_HEAP_SEGMENT_TP_NT_SEGMENT   = 1,
    VMMDLL_HEAP_SEGMENT_TP_NT_LFH       = 2,
    VMMDLL_HEAP_SEGMENT_TP_NT_LARGE     = 3,
    VMMDLL_HEAP_SEGMENT_TP_NT_NA        = 4,
    VMMDLL_HEAP_SEGMENT_TP_SEG_HEAP     = 5,
    VMMDLL_HEAP_SEGMENT_TP_SEG_SEGMENT  = 6,
    VMMDLL_HEAP_SEGMENT_TP_SEG_LARGE    = 7,
    VMMDLL_HEAP_SEGMENT_TP_SEG_NA       = 8,
} VMMDLL_HEAP_SEGMENT_TP, *PVMMDLL_HEAP_SEGMENT_TP;

typedef struct tdVMMDLL_MAP_HEAP_SEGMENTENTRY {
    QWORD va;
    DWORD cb;
    VMMDLL_HEAP_SEGMENT_TP tp : 16;
    DWORD iHeap : 16;
} VMMDLL_MAP_HEAP_SEGMENTENTRY, *PVMMDLL_MAP_HEAP_SEGMENTENTRY;

typedef struct tdVMMDLL_MAP_HEAPENTRY {
    QWORD va;
    VMMDLL_HEAP_TP tp;
    BOOL f32;
    DWORD iHeap;
    DWORD dwHeapNum;
} VMMDLL_MAP_HEAPENTRY, *PVMMDLL_MAP_HEAPENTRY;

typedef enum tdVMMDLL_HEAPALLOC_TP {
    VMMDLL_HEAPALLOC_TP_NA          = 0,
    VMMDLL_HEAPALLOC_TP_NT_HEAP     = 1,
    VMMDLL_HEAPALLOC_TP_NT_LFH      = 2,
    VMMDLL_HEAPALLOC_TP_NT_LARGE    = 3,
    VMMDLL_HEAPALLOC_TP_NT_NA       = 4,
    VMMDLL_HEAPALLOC_TP_SEG_VS      = 5,
    VMMDLL_HEAPALLOC_TP_SEG_LFH     = 6,
    VMMDLL_HEAPALLOC_TP_SEG_LARGE   = 7,
    VMMDLL_HEAPALLOC_TP_SEG_NA      = 8,
} VMMDLL_HEAPALLOC_TP, *PVMMDLL_HEAPALLOC_TP;

typedef struct tdVMMDLL_MAP_HEAPALLOCENTRY {
    QWORD va;
    DWORD cb;
    VMMDLL_HEAPALLOC_TP tp;
} VMMDLL_MAP_HEAPALLOCENTRY, *PVMMDLL_MAP_HEAPALLOCENTRY;

typedef struct tdVMMDLL_MAP_THREADENTRY {
    DWORD dwTID;
    DWORD dwPID;
    DWORD dwExitStatus;
    UCHAR bState;
    UCHAR bRunning;
    UCHAR bPriority;
    UCHAR bBasePriority;
    QWORD vaETHREAD;
    QWORD vaTeb;
    QWORD ftCreateTime;
    QWORD ftExitTime;
    QWORD vaStartAddress;
    QWORD vaStackBaseUser;
    QWORD vaStackLimitUser;
    QWORD vaStackBaseKernel;
    QWORD vaStackLimitKernel;
    QWORD vaTrapFrame;
    QWORD vaRIP;
    QWORD vaRSP;
    QWORD qwAffinity;
    DWORD dwUserTime;
    DWORD dwKernelTime;
    UCHAR bSuspendCount;
    UCHAR bWaitReason;
    UCHAR _FutureUse1[2];
    DWORD _FutureUse2[11];
    QWORD vaImpersonationToken;
    QWORD vaWin32StartAddress;
} VMMDLL_MAP_THREADENTRY, *PVMMDLL_MAP_THREADENTRY;

typedef struct tdVMMDLL_MAP_THREAD_CALLSTACKENTRY {
    DWORD i;
    BOOL  fRegPresent;
    QWORD vaRetAddr;
    QWORD vaRSP;
    QWORD vaBaseSP;
    DWORD _FutureUse1;
    DWORD cbDisplacement;
    union { LPSTR uszModule; LPWSTR wszModule; };
    union { LPSTR uszFunction; LPWSTR wszFunction; };
} VMMDLL_MAP_THREAD_CALLSTACKENTRY, *PVMMDLL_MAP_THREAD_CALLSTACKENTRY;

typedef struct tdVMMDLL_MAP_HANDLEENTRY {
    QWORD vaObject;
    DWORD dwHandle;
    DWORD dwGrantedAccess : 24;
    DWORD iType : 8;
    QWORD qwHandleCount;
    QWORD qwPointerCount;
    QWORD vaObjectCreateInfo;
    QWORD vaSecurityDescriptor;
    union { LPSTR  uszText; LPWSTR wszText; };
    DWORD _FutureUse2;
    DWORD dwPID;
    DWORD dwPoolTag;
    DWORD _FutureUse[7];
    union { LPSTR  uszType; LPWSTR wszType; QWORD _Pad1; };
} VMMDLL_MAP_HANDLEENTRY, *PVMMDLL_MAP_HANDLEENTRY;

typedef enum tdVMMDLL_MAP_POOL_TYPE {
    VMMDLL_MAP_POOL_TYPE_Unknown         = 0,
    VMMDLL_MAP_POOL_TYPE_NonPagedPool    = 1,
    VMMDLL_MAP_POOL_TYPE_NonPagedPoolNx  = 2,
    VMMDLL_MAP_POOL_TYPE_PagedPool       = 3
} VMMDLL_MAP_POOL_TYPE;

typedef enum tdVMM_MAP_POOL_TYPE_SUBSEGMENT {
    VMM_MAP_POOL_TYPE_SUBSEGMENT_UNKNOWN = 0,
    VMM_MAP_POOL_TYPE_SUBSEGMENT_NA      = 1,
    VMM_MAP_POOL_TYPE_SUBSEGMENT_BIG     = 2,
    VMM_MAP_POOL_TYPE_SUBSEGMENT_LARGE   = 3,
    VMM_MAP_POOL_TYPE_SUBSEGMENT_VS      = 4,
    VMM_MAP_POOL_TYPE_SUBSEGMENT_LFH     = 5
} VMM_MAP_POOL_TYPE_SUBSEGMENT;

typedef struct tdVMMDLL_MAP_POOLENTRYTAG {
    union {
        CHAR szTag[5];
        struct {
            DWORD dwTag;
            DWORD _Filler;
            DWORD cEntry;
            DWORD iTag2Map;
        };
    };
} VMMDLL_MAP_POOLENTRYTAG, *PVMMDLL_MAP_POOLENTRYTAG;

typedef struct tdVMMDLL_MAP_POOLENTRY {
    QWORD va;
    union {
        CHAR szTag[5];
        struct {
            DWORD dwTag;
            BYTE _ReservedZero;
            BYTE fAlloc;
            BYTE tpPool;
            BYTE tpSS;
        };
    };
    DWORD cb;
    DWORD _Filler;
} VMMDLL_MAP_POOLENTRY, *PVMMDLL_MAP_POOLENTRY;

typedef struct tdVMMDLL_MAP_KDEVICEENTRY {
    QWORD va;
    DWORD iDepth;
    DWORD dwDeviceType;
    union { LPSTR  uszDeviceType; LPWSTR wszDeviceType; };
    QWORD vaDriverObject;
    QWORD vaAttachedDevice;
    QWORD vaFileSystemDevice;
    union { LPSTR  uszVolumeInfo; LPWSTR wszVolumeInfo; };
} VMMDLL_MAP_KDEVICEENTRY, *PVMMDLL_MAP_KDEVICEENTRY;

typedef struct tdVMMDLL_MAP_KDRIVERENTRY {
    QWORD va;
    QWORD vaDriverStart;
    QWORD cbDriverSize;
    QWORD vaDeviceObject;
    union { LPSTR  uszName; LPWSTR wszName; };
    union { LPSTR  uszPath; LPWSTR wszPath; };
    union { LPSTR  uszServiceKeyName; LPWSTR wszServiceKeyName; };
    QWORD MajorFunction[28];
} VMMDLL_MAP_KDRIVERENTRY, *PVMMDLL_MAP_KDRIVERENTRY;

typedef struct tdVMMDLL_MAP_KOBJECTENTRY {
    QWORD va;
    QWORD vaParent;
    DWORD _Filler;
    DWORD cvaChild;
    PQWORD pvaChild;
    union { LPSTR uszName; LPWSTR wszName; };
    union { LPSTR uszType; LPWSTR wszType; };
} VMMDLL_MAP_KOBJECTENTRY, *PVMMDLL_MAP_KOBJECTENTRY;

typedef struct tdVMMDLL_MAP_NETENTRY {
    DWORD dwPID;
    DWORD dwState;
    WORD _FutureUse3[3];
    WORD AF;
    struct {
        BOOL fValid;
        WORD _Reserved;
        WORD port;
        BYTE pbAddr[16];
        union { LPSTR  uszText; LPWSTR wszText; };
    } Src;
    struct {
        BOOL fValid;
        WORD _Reserved;
        WORD port;
        BYTE pbAddr[16];
        union { LPSTR  uszText; LPWSTR wszText; };
    } Dst;
    QWORD vaObj;
    QWORD ftTime;
    DWORD dwPoolTag;
    DWORD _FutureUse4;
    union { LPSTR  uszText; LPWSTR wszText; };
    DWORD _FutureUse2[4];
} VMMDLL_MAP_NETENTRY, *PVMMDLL_MAP_NETENTRY;

typedef struct tdVMMDLL_MAP_PHYSMEMENTRY {
    QWORD pa;
    QWORD cb;
} VMMDLL_MAP_PHYSMEMENTRY, *PVMMDLL_MAP_PHYSMEMENTRY;

typedef struct tdVMMDLL_MAP_USERENTRY {
    DWORD _FutureUse1[2];
    union { LPSTR  uszText; LPWSTR wszText; };
    ULONG64 vaRegHive;
    union { LPSTR  uszSID; LPWSTR wszSID; };
    DWORD _FutureUse2[2];
} VMMDLL_MAP_USERENTRY, *PVMMDLL_MAP_USERENTRY;

typedef enum tdVMMDLL_VM_TP {
    VMMDLL_VM_TP_UNKNOWN = 0,
    VMMDLL_VM_TP_HV      = 1,
    VMMDLL_VM_TP_HV_WHVP = 2
} VMMDLL_VM_TP;

typedef struct tdVMMDLL_MAP_VMENTRY {
    VMMVM_HANDLE hVM;
    union { LPSTR  uszName; LPWSTR wszName; };
    QWORD gpaMax;
    VMMDLL_VM_TP tp;
    BOOL fActive;
    BOOL fReadOnly;
    BOOL fPhysicalOnly;
    DWORD dwPartitionID;
    DWORD dwVersionBuild;
    VMMDLL_SYSTEM_TP tpSystem;
    DWORD dwParentVmmMountID;
    DWORD dwVmMemPID;
} VMMDLL_MAP_VMENTRY, *PVMMDLL_MAP_VMENTRY;

typedef struct tdVMMDLL_MAP_SERVICEENTRY {
    QWORD vaObj;
    DWORD dwOrdinal;
    DWORD dwStartType;
    SERVICE_STATUS ServiceStatus;
    union { LPSTR  uszServiceName; LPWSTR wszServiceName; QWORD _Reserved1; };
    union { LPSTR  uszDisplayName; LPWSTR wszDisplayName; QWORD _Reserved2; };
    union { LPSTR  uszPath;        LPWSTR wszPath;        QWORD _Reserved3; };
    union { LPSTR  uszUserTp;      LPWSTR wszUserTp;      QWORD _Reserved4; };
    union { LPSTR  uszUserAcct;    LPWSTR wszUserAcct;    QWORD _Reserved5; };
    union { LPSTR  uszImagePath;   LPWSTR wszImagePath;   QWORD _Reserved6; };
    DWORD dwPID;
    DWORD _FutureUse1;
    QWORD _FutureUse2;
} VMMDLL_MAP_SERVICEENTRY, *PVMMDLL_MAP_SERVICEENTRY;

typedef struct tdVMMDLL_MAP_PTE {
    DWORD dwVersion;
    DWORD _Reserved1[5];
    PBYTE pbMultiText;
    DWORD cbMultiText;
    DWORD cMap;
    VMMDLL_MAP_PTEENTRY pMap[];
} VMMDLL_MAP_PTE, *PVMMDLL_MAP_PTE;

typedef struct tdVMMDLL_MAP_VAD {
    DWORD dwVersion;
    DWORD _Reserved1[4];
    DWORD cPage;
    PBYTE pbMultiText;
    DWORD cbMultiText;
    DWORD cMap;
    VMMDLL_MAP_VADENTRY pMap[];
} VMMDLL_MAP_VAD, *PVMMDLL_MAP_VAD;

typedef struct tdVMMDLL_MAP_VADEX {
    DWORD dwVersion;
    DWORD _Reserved1[4];
    DWORD cMap;
    VMMDLL_MAP_VADEXENTRY pMap[];
} VMMDLL_MAP_VADEX, *PVMMDLL_MAP_VADEX;

typedef struct tdVMMDLL_MAP_MODULE {
    DWORD dwVersion;
    DWORD _Reserved1[5];
    PBYTE pbMultiText;
    DWORD cbMultiText;
    DWORD cMap;
    VMMDLL_MAP_MODULEENTRY pMap[];
} VMMDLL_MAP_MODULE, *PVMMDLL_MAP_MODULE;

typedef struct tdVMMDLL_MAP_UNLOADEDMODULE {
    DWORD dwVersion;
    DWORD _Reserved1[5];
    PBYTE pbMultiText;
    DWORD cbMultiText;
    DWORD cMap;
    VMMDLL_MAP_UNLOADEDMODULEENTRY pMap[];
} VMMDLL_MAP_UNLOADEDMODULE, *PVMMDLL_MAP_UNLOADEDMODULE;

typedef struct tdVMMDLL_MAP_EAT {
    DWORD dwVersion;
    DWORD dwOrdinalBase;
    DWORD cNumberOfNames;
    DWORD cNumberOfFunctions;
    DWORD cNumberOfForwardedFunctions;
    DWORD _Reserved1[3];
    QWORD vaModuleBase;
    QWORD vaAddressOfFunctions;
    QWORD vaAddressOfNames;
    PBYTE pbMultiText;
    DWORD cbMultiText;
    DWORD cMap;
    VMMDLL_MAP_EATENTRY pMap[];
} VMMDLL_MAP_EAT, *PVMMDLL_MAP_EAT;

typedef struct tdVMMDLL_MAP_IAT {
    DWORD dwVersion;
    DWORD _Reserved1[5];
    QWORD vaModuleBase;
    PBYTE pbMultiText;
    DWORD cbMultiText;
    DWORD cMap;
    VMMDLL_MAP_IATENTRY pMap[];
} VMMDLL_MAP_IAT, *PVMMDLL_MAP_IAT;

typedef struct tdVMMDLL_MAP_HEAP {
    DWORD dwVersion;
    DWORD _Reserved1[7];
    PVMMDLL_MAP_HEAP_SEGMENTENTRY pSegments;
    DWORD cSegments;
    DWORD cMap;
    VMMDLL_MAP_HEAPENTRY pMap[];
} VMMDLL_MAP_HEAP, *PVMMDLL_MAP_HEAP;

typedef struct tdVMMDLL_MAP_HEAPALLOC {
    DWORD dwVersion;
    DWORD _Reserved1[7];
    PVOID _Reserved2[2];
    DWORD cMap;
    VMMDLL_MAP_HEAPALLOCENTRY pMap[];
} VMMDLL_MAP_HEAPALLOC, *PVMMDLL_MAP_HEAPALLOC;

typedef struct tdVMMDLL_MAP_THREAD {
    DWORD dwVersion;
    DWORD _Reserved[8];
    DWORD cMap;
    VMMDLL_MAP_THREADENTRY pMap[];
} VMMDLL_MAP_THREAD, *PVMMDLL_MAP_THREAD;

typedef struct tdVMMDLL_MAP_THREAD_CALLSTACK {
    DWORD dwVersion;
    DWORD _Reserved1[6];
    DWORD dwPID;
    DWORD dwTID;
    DWORD cbText;
    union { LPSTR  uszText; LPWSTR wszText; };
    PBYTE pbMultiText;
    DWORD cbMultiText;
    DWORD cMap;
    VMMDLL_MAP_THREAD_CALLSTACKENTRY pMap[0];
} VMMDLL_MAP_THREAD_CALLSTACK, *PVMMDLL_MAP_THREAD_CALLSTACK;

typedef struct tdVMMDLL_MAP_HANDLE {
    DWORD dwVersion;
    DWORD _Reserved1[5];
    PBYTE pbMultiText;
    DWORD cbMultiText;
    DWORD cMap;
    VMMDLL_MAP_HANDLEENTRY pMap[];
} VMMDLL_MAP_HANDLE, *PVMMDLL_MAP_HANDLE;

typedef struct tdVMMDLL_MAP_POOL {
    DWORD dwVersion;
    DWORD _Reserved1[6];
    DWORD cbTotal;
    PDWORD piTag2Map;
    PVMMDLL_MAP_POOLENTRYTAG pTag;
    DWORD cTag;
    DWORD cMap;
    VMMDLL_MAP_POOLENTRY pMap[];
} VMMDLL_MAP_POOL, *PVMMDLL_MAP_POOL;

typedef struct tdVMMDLL_MAP_KOBJECT {
    DWORD dwVersion;
    DWORD _Reserved1[5];
    PBYTE pbMultiText;
    DWORD cbMultiText;
    DWORD cMap;
    VMMDLL_MAP_KOBJECTENTRY pMap[];
} VMMDLL_MAP_KOBJECT, *PVMMDLL_MAP_KOBJECT;

typedef struct tdVMMDLL_MAP_KDRIVER {
    DWORD dwVersion;
    DWORD _Reserved1[5];
    PBYTE pbMultiText;
    DWORD cbMultiText;
    DWORD cMap;
    VMMDLL_MAP_KDRIVERENTRY pMap[];
} VMMDLL_MAP_KDRIVER, *PVMMDLL_MAP_KDRIVER;

typedef struct tdVMMDLL_MAP_KDEVICE {
    DWORD dwVersion;
    DWORD _Reserved1[5];
    PBYTE pbMultiText;
    DWORD cbMultiText;
    DWORD cMap;
    VMMDLL_MAP_KDEVICEENTRY pMap[];
} VMMDLL_MAP_KDEVICE, *PVMMDLL_MAP_KDEVICE;

typedef struct tdVMMDLL_MAP_NET {
    DWORD dwVersion;
    DWORD _Reserved1;
    PBYTE pbMultiText;
    DWORD cbMultiText;
    DWORD cMap;
    VMMDLL_MAP_NETENTRY pMap[];
} VMMDLL_MAP_NET, *PVMMDLL_MAP_NET;

typedef struct tdVMMDLL_MAP_PHYSMEM {
    DWORD dwVersion;
    DWORD _Reserved1[5];
    DWORD cMap;
    DWORD _Reserved2;
    VMMDLL_MAP_PHYSMEMENTRY pMap[];
} VMMDLL_MAP_PHYSMEM, *PVMMDLL_MAP_PHYSMEM;

typedef struct tdVMMDLL_MAP_USER {
    DWORD dwVersion;
    DWORD _Reserved1[5];
    PBYTE pbMultiText;
    DWORD cbMultiText;
    DWORD cMap;
    VMMDLL_MAP_USERENTRY pMap[];
} VMMDLL_MAP_USER, *PVMMDLL_MAP_USER;

typedef struct tdVMMDLL_MAP_VM {
    DWORD dwVersion;
    DWORD _Reserved1[5];
    PBYTE pbMultiText;
    DWORD cbMultiText;
    DWORD cMap;
    VMMDLL_MAP_VMENTRY pMap[];
} VMMDLL_MAP_VM, *PVMMDLL_MAP_VM;

typedef struct tdVMMDLL_MAP_SERVICE {
    DWORD dwVersion;
    DWORD _Reserved1[5];
    PBYTE pbMultiText;
    DWORD cbMultiText;
    DWORD cMap;
    VMMDLL_MAP_SERVICEENTRY pMap[];
} VMMDLL_MAP_SERVICE, *PVMMDLL_MAP_SERVICE;











EXPORTED_FUNCTION
_Success_(return) BOOL VMMDLL_Map_GetPteU(_In_ VMM_HANDLE hVMM, _In_ DWORD dwPID, _In_ BOOL fIdentifyModules, _Out_ PVMMDLL_MAP_PTE *ppPteMap);
_Success_(return) BOOL VMMDLL_Map_GetPteW(_In_ VMM_HANDLE hVMM, _In_ DWORD dwPID, _In_ BOOL fIdentifyModules, _Out_ PVMMDLL_MAP_PTE *ppPteMap);











EXPORTED_FUNCTION
_Success_(return) BOOL VMMDLL_Map_GetVadU(_In_ VMM_HANDLE hVMM, _In_ DWORD dwPID, _In_ BOOL fIdentifyModules, _Out_ PVMMDLL_MAP_VAD *ppVadMap);
_Success_(return) BOOL VMMDLL_Map_GetVadW(_In_ VMM_HANDLE hVMM, _In_ DWORD dwPID, _In_ BOOL fIdentifyModules, _Out_ PVMMDLL_MAP_VAD *ppVadMap);










EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_Map_GetVadEx(_In_ VMM_HANDLE hVMM, _In_ DWORD dwPID, _In_ DWORD oPage, _In_ DWORD cPage, _Out_ PVMMDLL_MAP_VADEX *ppVadExMap);










EXPORTED_FUNCTION
_Success_(return) BOOL VMMDLL_Map_GetModuleU(_In_ VMM_HANDLE hVMM, _In_ DWORD dwPID, _Out_ PVMMDLL_MAP_MODULE *ppModuleMap, _In_ DWORD flags);
_Success_(return) BOOL VMMDLL_Map_GetModuleW(_In_ VMM_HANDLE hVMM, _In_ DWORD dwPID, _Out_ PVMMDLL_MAP_MODULE *ppModuleMap, _In_ DWORD flags);











EXPORTED_FUNCTION
_Success_(return) BOOL VMMDLL_Map_GetModuleFromNameU(_In_ VMM_HANDLE hVMM, _In_ DWORD dwPID, _In_opt_ LPCSTR  uszModuleName, _Out_ PVMMDLL_MAP_MODULEENTRY *ppModuleMapEntry, _In_ DWORD flags);
_Success_(return) BOOL VMMDLL_Map_GetModuleFromNameW(_In_ VMM_HANDLE hVMM, _In_ DWORD dwPID, _In_opt_ LPCWSTR wszModuleName, _Out_ PVMMDLL_MAP_MODULEENTRY *ppModuleMapEntry, _In_ DWORD flags);









EXPORTED_FUNCTION
_Success_(return) BOOL VMMDLL_Map_GetUnloadedModuleU(_In_ VMM_HANDLE hVMM, _In_ DWORD dwPID, _Out_ PVMMDLL_MAP_UNLOADEDMODULE *ppUnloadedModuleMap);
_Success_(return) BOOL VMMDLL_Map_GetUnloadedModuleW(_In_ VMM_HANDLE hVMM, _In_ DWORD dwPID, _Out_ PVMMDLL_MAP_UNLOADEDMODULE *ppUnloadedModuleMap);










EXPORTED_FUNCTION
_Success_(return) BOOL VMMDLL_Map_GetEATU(_In_ VMM_HANDLE hVMM, _In_ DWORD dwPID, _In_ LPCSTR  uszModuleName, _Out_ PVMMDLL_MAP_EAT *ppEatMap);
_Success_(return) BOOL VMMDLL_Map_GetEATW(_In_ VMM_HANDLE hVMM, _In_ DWORD dwPID, _In_ LPCWSTR wszModuleName, _Out_ PVMMDLL_MAP_EAT *ppEatMap);










EXPORTED_FUNCTION
_Success_(return) BOOL VMMDLL_Map_GetIATU(_In_ VMM_HANDLE hVMM, _In_ DWORD dwPID, _In_ LPCSTR  uszModuleName, _Out_ PVMMDLL_MAP_IAT *ppIatMap);
_Success_(return) BOOL VMMDLL_Map_GetIATW(_In_ VMM_HANDLE hVMM, _In_ DWORD dwPID, _In_ LPCWSTR wszModuleName, _Out_ PVMMDLL_MAP_IAT *ppIatMap);









EXPORTED_FUNCTION
_Success_(return) BOOL VMMDLL_Map_GetHeap(_In_ VMM_HANDLE hVMM, _In_ DWORD dwPID, _Out_ PVMMDLL_MAP_HEAP *ppHeapMap);










EXPORTED_FUNCTION
_Success_(return) BOOL VMMDLL_Map_GetHeapAlloc(_In_ VMM_HANDLE hVMM, _In_ DWORD dwPID, _In_ QWORD qwHeapNumOrAddress, _Out_ PVMMDLL_MAP_HEAPALLOC *ppHeapAllocMap);










EXPORTED_FUNCTION
_Success_(return) BOOL VMMDLL_Map_GetThread(_In_ VMM_HANDLE hVMM, _In_ DWORD dwPID, _Out_ PVMMDLL_MAP_THREAD *ppThreadMap);















EXPORTED_FUNCTION
_Success_(return) BOOL VMMDLL_Map_GetThread_CallstackU(_In_ VMM_HANDLE hVMM, _In_ DWORD dwPID, _In_ DWORD dwTID, _In_ DWORD flags, _Out_ PVMMDLL_MAP_THREAD_CALLSTACK *ppThreadCallstack);
_Success_(return) BOOL VMMDLL_Map_GetThread_CallstackW(_In_ VMM_HANDLE hVMM, _In_ DWORD dwPID, _In_ DWORD dwTID, _In_ DWORD flags, _Out_ PVMMDLL_MAP_THREAD_CALLSTACK *ppThreadCallstack);










EXPORTED_FUNCTION
_Success_(return) BOOL VMMDLL_Map_GetHandleU(_In_ VMM_HANDLE hVMM, _In_ DWORD dwPID, _Out_ PVMMDLL_MAP_HANDLE *ppHandleMap);
_Success_(return) BOOL VMMDLL_Map_GetHandleW(_In_ VMM_HANDLE hVMM, _In_ DWORD dwPID, _Out_ PVMMDLL_MAP_HANDLE *ppHandleMap);








EXPORTED_FUNCTION
_Success_(return) BOOL VMMDLL_Map_GetPhysMem(_In_ VMM_HANDLE hVMM, _Out_ PVMMDLL_MAP_PHYSMEM *ppPhysMemMap);








EXPORTED_FUNCTION
_Success_(return) BOOL VMMDLL_Map_GetKDeviceU(_In_ VMM_HANDLE hVMM, _Out_ PVMMDLL_MAP_KDEVICE *ppKDeviceMap);
_Success_(return) BOOL VMMDLL_Map_GetKDeviceW(_In_ VMM_HANDLE hVMM, _Out_ PVMMDLL_MAP_KDEVICE *ppKDeviceMap);








EXPORTED_FUNCTION
_Success_(return) BOOL VMMDLL_Map_GetKDriverU(_In_ VMM_HANDLE hVMM, _Out_ PVMMDLL_MAP_KDRIVER *ppKDriverMap);
_Success_(return) BOOL VMMDLL_Map_GetKDriverW(_In_ VMM_HANDLE hVMM, _Out_ PVMMDLL_MAP_KDRIVER *ppKDriverMap);








EXPORTED_FUNCTION
_Success_(return) BOOL VMMDLL_Map_GetKObjectU(_In_ VMM_HANDLE hVMM, _Out_ PVMMDLL_MAP_KOBJECT *ppKObjectMap);
_Success_(return) BOOL VMMDLL_Map_GetKObjectW(_In_ VMM_HANDLE hVMM, _Out_ PVMMDLL_MAP_KOBJECT *ppKObjectMap);















EXPORTED_FUNCTION
_Success_(return) BOOL VMMDLL_Map_GetPool(_In_ VMM_HANDLE hVMM, _Out_ PVMMDLL_MAP_POOL *ppPoolMap, _In_ DWORD flags);









EXPORTED_FUNCTION
_Success_(return) BOOL VMMDLL_Map_GetNetU(_In_ VMM_HANDLE hVMM, _Out_ PVMMDLL_MAP_NET *ppNetMap);
_Success_(return) BOOL VMMDLL_Map_GetNetW(_In_ VMM_HANDLE hVMM, _Out_ PVMMDLL_MAP_NET *ppNetMap);











EXPORTED_FUNCTION
_Success_(return) BOOL VMMDLL_Map_GetUsersU(_In_ VMM_HANDLE hVMM, _Out_ PVMMDLL_MAP_USER *ppUserMap);
_Success_(return) BOOL VMMDLL_Map_GetUsersW(_In_ VMM_HANDLE hVMM, _Out_ PVMMDLL_MAP_USER *ppUserMap);









EXPORTED_FUNCTION
_Success_(return) BOOL VMMDLL_Map_GetVMU(_In_ VMM_HANDLE hVMM, _Out_ PVMMDLL_MAP_VM *ppVmMap);
_Success_(return) BOOL VMMDLL_Map_GetVMW(_In_ VMM_HANDLE hVMM, _Out_ PVMMDLL_MAP_VM *ppVmMap);








EXPORTED_FUNCTION
_Success_(return) BOOL VMMDLL_Map_GetServicesU(_In_ VMM_HANDLE hVMM, _Out_ PVMMDLL_MAP_SERVICE *ppServiceMap);
_Success_(return) BOOL VMMDLL_Map_GetServicesW(_In_ VMM_HANDLE hVMM, _Out_ PVMMDLL_MAP_SERVICE *ppServiceMap);







#define VMMDLL_MEM_SEARCH_VERSION           0xfe3e0003
#define VMMDLL_MEM_SEARCH_MAXLENGTH         32

typedef struct tdVMMDLL_MEM_SEARCH_CONTEXT_SEARCHENTRY {
    DWORD cbAlign;
    DWORD cb;
    BYTE pb[VMMDLL_MEM_SEARCH_MAXLENGTH];
    BYTE pbSkipMask[VMMDLL_MEM_SEARCH_MAXLENGTH];
} VMMDLL_MEM_SEARCH_CONTEXT_SEARCHENTRY, *PVMMDLL_MEM_SEARCH_CONTEXT_SEARCHENTRY;




typedef struct tdVMMDLL_MEM_SEARCH_CONTEXT {
    DWORD dwVersion;
    DWORD _Filler[2];
    BOOL fAbortRequested;
    DWORD cMaxResult;
    DWORD cSearch;
    PVMMDLL_MEM_SEARCH_CONTEXT_SEARCHENTRY pSearch;
    QWORD vaMin;
    QWORD vaMax;
    QWORD vaCurrent;
    DWORD _Filler2;
    DWORD cResult;
    QWORD cbReadTotal;
    PVOID pvUserPtrOpt;



    BOOL(*pfnResultOptCB)(_In_ struct tdVMMDLL_MEM_SEARCH_CONTEXT *ctx, _In_ QWORD va, _In_ DWORD iSearch);

    QWORD ReadFlags;
    BOOL fForcePTE;
    BOOL fForceVAD;



    BOOL(*pfnFilterOptCB)(_In_ struct tdVMMDLL_MEM_SEARCH_CONTEXT *ctx, _In_opt_ PVMMDLL_MAP_PTEENTRY pePte, _In_opt_ PVMMDLL_MAP_VADENTRY peVad);
} VMMDLL_MEM_SEARCH_CONTEXT, *PVMMDLL_MEM_SEARCH_CONTEXT;
















EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_MemSearch(
    _In_ VMM_HANDLE hVMM,
    _In_ DWORD dwPID,
    _Inout_ PVMMDLL_MEM_SEARCH_CONTEXT ctx,
    _Out_opt_ PQWORD *ppva,
    _Out_opt_ PDWORD pcva
);










#ifndef VMMYARA_RULE_MATCH_DEFINED
#define VMMYARA_RULE_MATCH_DEFINED

#define VMMYARA_RULE_MATCH_VERSION          0xfedc0005
#define VMMYARA_RULE_MATCH_TAG_MAX          27
#define VMMYARA_RULE_MATCH_META_MAX         32
#define VMMYARA_RULE_MATCH_STRING_MAX       16
#define VMMYARA_RULE_MATCH_OFFSET_MAX       24




typedef struct tdVMMYARA_RULE_MATCH {
    DWORD dwVersion;
    DWORD flags;
    LPSTR szRuleIdentifier;
    DWORD cTags;
    LPSTR szTags[VMMYARA_RULE_MATCH_TAG_MAX];
    DWORD cMeta;
    struct {
        LPSTR szIdentifier;
        LPSTR szString;
    } Meta[VMMYARA_RULE_MATCH_META_MAX];
    DWORD cStrings;
    struct {
        LPSTR szString;
        DWORD cMatch;
        SIZE_T cbMatchOffset[VMMYARA_RULE_MATCH_OFFSET_MAX];
    } Strings[VMMYARA_RULE_MATCH_STRING_MAX];
} VMMYARA_RULE_MATCH, *PVMMYARA_RULE_MATCH;

#endif

#ifndef VMMYARA_SCAN_MEMORY_CALLBACK_DEFINED
#define VMMYARA_SCAN_MEMORY_CALLBACK_DEFINED









typedef BOOL(*VMMYARA_SCAN_MEMORY_CALLBACK)(
    _In_ PVOID pvContext,
    _In_ PVMMYARA_RULE_MATCH pRuleMatch,
    _In_reads_bytes_(cbBuffer) PBYTE pbBuffer,
    _In_ SIZE_T cbBuffer
);

#endif


#define VMMDLL_YARA_CONFIG_VERSION                  0xdec30001
#define VMMDLL_YARA_MEMORY_CALLBACK_CONTEXT_VERSION 0xdec40002
#define VMMDLL_YARA_CONFIG_MAX_RESULT               0x00010000

typedef struct tdVMMDLL_YARA_CONFIG *PVMMDLL_YARA_CONFIG;








typedef BOOL(*VMMYARA_SCAN_FILTER_CALLBACK)(
    _In_ PVMMDLL_YARA_CONFIG ctx,
    _In_opt_ PVMMDLL_MAP_PTEENTRY pePte,
    _In_opt_ PVMMDLL_MAP_VADENTRY peVad
);




typedef struct tdVMMDLL_YARA_CONFIG {
    DWORD dwVersion;
    DWORD _Filler[2];
    BOOL fAbortRequested;
    DWORD cMaxResult;
    DWORD cRules;
    LPSTR *pszRules;
    QWORD vaMin;
    QWORD vaMax;
    QWORD vaCurrent;
    DWORD _Filler2;
    DWORD cResult;
    QWORD cbReadTotal;
    PVOID pvUserPtrOpt;


    VMMYARA_SCAN_MEMORY_CALLBACK pfnScanMemoryCB;

    QWORD ReadFlags;
    BOOL fForcePTE;
    BOOL fForceVAD;



    VMMYARA_SCAN_FILTER_CALLBACK pfnFilterOptCB;
    PVOID pvUserPtrOpt2;
    QWORD _Reserved;
} VMMDLL_YARA_CONFIG, *PVMMDLL_YARA_CONFIG;





typedef struct tdVMMDLL_YARA_MEMORY_CALLBACK_CONTEXT {
    DWORD dwVersion;
    DWORD dwPID;
    PVOID pUserContext;
    QWORD vaObject;
    QWORD va;
    PBYTE pb;
    DWORD cb;
    LPSTR uszTag[1];
} VMMDLL_YARA_MEMORY_CALLBACK_CONTEXT, *PVMMDLL_YARA_MEMORY_CALLBACK_CONTEXT;











EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_YaraSearch(
    _In_ VMM_HANDLE hVMM,
    _In_ DWORD dwPID,
    _In_ PVMMDLL_YARA_CONFIG pYaraConfig,
    _Out_opt_ PQWORD *ppva,
    _Out_opt_ PDWORD pcva
);







#define VMMDLL_MAP_PFN_VERSION              1

#define VMMDLL_PFN_FLAG_NORMAL              0
#define VMMDLL_PFN_FLAG_EXTENDED            1

static LPCSTR VMMDLL_PFN_TYPE_TEXT[] = { "Zero", "Free", "Standby", "Modifiy", "ModNoWr", "Bad", "Active", "Transit" };
static LPCSTR VMMDLL_PFN_TYPEEXTENDED_TEXT[] = { "-", "Unused", "ProcPriv", "PageTable", "LargePage", "DriverLock", "Shareable", "File" };

typedef enum tdVMMDLL_MAP_PFN_TYPE {
    VmmDll_PfnTypeZero = 0,
    VmmDll_PfnTypeFree = 1,
    VmmDll_PfnTypeStandby = 2,
    VmmDll_PfnTypeModified = 3,
    VmmDll_PfnTypeModifiedNoWrite = 4,
    VmmDll_PfnTypeBad = 5,
    VmmDll_PfnTypeActive = 6,
    VmmDll_PfnTypeTransition = 7
} VMMDLL_MAP_PFN_TYPE;

typedef enum tdVMMDLL_MAP_PFN_TYPEEXTENDED {
    VmmDll_PfnExType_Unknown = 0,
    VmmDll_PfnExType_Unused = 1,
    VmmDll_PfnExType_ProcessPrivate = 2,
    VmmDll_PfnExType_PageTable = 3,
    VmmDll_PfnExType_LargePage = 4,
    VmmDll_PfnExType_DriverLocked = 5,
    VmmDll_PfnExType_Shareable = 6,
    VmmDll_PfnExType_File = 7,
} VMMDLL_MAP_PFN_TYPEEXTENDED;

typedef struct tdVMMDLL_MAP_PFNENTRY {
    DWORD dwPfn;
    VMMDLL_MAP_PFN_TYPEEXTENDED tpExtended;
    struct {
        union {
            DWORD dwPid;
            DWORD dwPfnPte[5];
        };
        QWORD va;
    } AddressInfo;
    QWORD vaPte;
    QWORD OriginalPte;
    union {
        DWORD _u3;
        struct {
            WORD ReferenceCount;

            BYTE PageLocation       : 3;
            BYTE WriteInProgress    : 1;
            BYTE Modified           : 1;
            BYTE ReadInProgress     : 1;
            BYTE CacheAttribute     : 2;
            BYTE Priority           : 3;
            BYTE Rom_OnProtectedStandby : 1;
            BYTE InPageError        : 1;
            BYTE KernelStack_SystemChargedPage : 1;
            BYTE RemovalRequested   : 1;
            BYTE ParityError        : 1;
        };
    };
    union {
        QWORD _u4;
        struct {
            DWORD PteFrame;
            DWORD PteFrameHigh      : 4;
            DWORD _Reserved         : 21;
            DWORD PrototypePte      : 1;
            DWORD PageColor         : 6;
        };
    };
    DWORD _FutureUse[6];
} VMMDLL_MAP_PFNENTRY, *PVMMDLL_MAP_PFNENTRY;

typedef struct tdVMMDLL_MAP_PFN {
    DWORD dwVersion;
    DWORD _Reserved1[5];
    DWORD cMap;
    VMMDLL_MAP_PFNENTRY pMap[];
} VMMDLL_MAP_PFN, *PVMMDLL_MAP_PFN;











EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_Map_GetPfn(
    _In_ VMM_HANDLE hVMM,
    _In_reads_(cPfns) DWORD pPfns[],
    _In_ DWORD cPfns,
    _Out_writes_bytes_opt_(*pcbPfnMap) PVMMDLL_MAP_PFN pPfnMap,
    _Inout_ PDWORD pcbPfnMap
);











EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_Map_GetPfnEx(
    _In_ VMM_HANDLE hVMM,
    _In_reads_(cPfns) DWORD pPfns[],
    _In_ DWORD cPfns,
    _Out_ PVMMDLL_MAP_PFN *ppPfnMap,
    _In_ DWORD flags
);


















EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_PidGetFromName(_In_ VMM_HANDLE hVMM, _In_ LPCSTR szProcName, _Out_ PDWORD pdwPID);








EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_PidList(_In_ VMM_HANDLE hVMM, _Out_writes_opt_(*pcPIDs) PDWORD pPIDs, _Inout_ PSIZE_T pcPIDs);

#define VMMDLL_PROCESS_INFORMATION_MAGIC        0xc0ffee663df9301e
#define VMMDLL_PROCESS_INFORMATION_VERSION      7

typedef enum tdVMMDLL_PROCESS_INTEGRITY_LEVEL {
    VMMDLL_PROCESS_INTEGRITY_LEVEL_UNKNOWN      = 0,
    VMMDLL_PROCESS_INTEGRITY_LEVEL_UNTRUSTED    = 1,
    VMMDLL_PROCESS_INTEGRITY_LEVEL_LOW          = 2,
    VMMDLL_PROCESS_INTEGRITY_LEVEL_MEDIUM       = 3,
    VMMDLL_PROCESS_INTEGRITY_LEVEL_MEDIUMPLUS   = 4,
    VMMDLL_PROCESS_INTEGRITY_LEVEL_HIGH         = 5,
    VMMDLL_PROCESS_INTEGRITY_LEVEL_SYSTEM       = 6,
    VMMDLL_PROCESS_INTEGRITY_LEVEL_PROTECTED    = 7,
} VMMDLL_PROCESS_INTEGRITY_LEVEL;

typedef struct tdVMMDLL_PROCESS_INFORMATION {
    ULONG64 magic;
    WORD wVersion;
    WORD wSize;
    VMMDLL_MEMORYMODEL_TP tpMemoryModel;
    VMMDLL_SYSTEM_TP tpSystem;
    BOOL fUserOnly;
    DWORD dwPID;
    DWORD dwPPID;
    DWORD dwState;
    CHAR szName[16];
    CHAR szNameLong[64];
    ULONG64 paDTB;
    ULONG64 paDTB_UserOpt;
    struct {
        ULONG64 vaEPROCESS;
        ULONG64 vaPEB;
        ULONG64 _Reserved1;
        BOOL fWow64;
        DWORD vaPEB32;
        DWORD dwSessionId;
        ULONG64 qwLUID;
        CHAR szSID[MAX_PATH];
        VMMDLL_PROCESS_INTEGRITY_LEVEL IntegrityLevel;
    } win;
} VMMDLL_PROCESS_INFORMATION, *PVMMDLL_PROCESS_INFORMATION;










EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_ProcessGetInformation(
    _In_ VMM_HANDLE hVMM,
    _In_ DWORD dwPID,
    _Inout_opt_ PVMMDLL_PROCESS_INFORMATION pProcessInformation,
    _In_ PSIZE_T pcbProcessInformation
);










EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_ProcessGetInformationAll(
    _In_ VMM_HANDLE hVMM,
    _Out_ PVMMDLL_PROCESS_INFORMATION *ppProcessInformationAll,
    _Out_ PDWORD pcProcessInformation
);

#define VMMDLL_PROCESS_INFORMATION_OPT_STRING_PATH_KERNEL           1
#define VMMDLL_PROCESS_INFORMATION_OPT_STRING_PATH_USER_IMAGE       2
#define VMMDLL_PROCESS_INFORMATION_OPT_STRING_CMDLINE               3












EXPORTED_FUNCTION _Success_(return != NULL)
LPSTR VMMDLL_ProcessGetInformationString(_In_ VMM_HANDLE hVMM, _In_ DWORD dwPID, _In_ DWORD fOptionString);














EXPORTED_FUNCTION
_Success_(return) BOOL VMMDLL_ProcessGetDirectoriesU(_In_ VMM_HANDLE hVMM, _In_ DWORD dwPID, _In_ LPCSTR  uszModule, _Out_writes_(16) PIMAGE_DATA_DIRECTORY pDataDirectories);
_Success_(return) BOOL VMMDLL_ProcessGetDirectoriesW(_In_ VMM_HANDLE hVMM, _In_ DWORD dwPID, _In_ LPCWSTR wszModule, _Out_writes_(16) PIMAGE_DATA_DIRECTORY pDataDirectories);
EXPORTED_FUNCTION
_Success_(return) BOOL VMMDLL_ProcessGetSectionsU(_In_ VMM_HANDLE hVMM, _In_ DWORD dwPID, _In_ LPCSTR  uszModule, _Out_writes_opt_(cSections) PIMAGE_SECTION_HEADER pSections, _In_ DWORD cSections, _Out_ PDWORD pcSections);
_Success_(return) BOOL VMMDLL_ProcessGetSectionsW(_In_ VMM_HANDLE hVMM, _In_ DWORD dwPID, _In_ LPCWSTR wszModule, _Out_writes_opt_(cSections) PIMAGE_SECTION_HEADER pSections, _In_ DWORD cSections, _Out_ PDWORD pcSections);









EXPORTED_FUNCTION
_Success_(return != 0) ULONG64 VMMDLL_ProcessGetProcAddressU(_In_ VMM_HANDLE hVMM, _In_ DWORD dwPID, _In_ LPCSTR  uszModuleName, _In_ LPCSTR szFunctionName);
_Success_(return != 0) ULONG64 VMMDLL_ProcessGetProcAddressW(_In_ VMM_HANDLE hVMM, _In_ DWORD dwPID, _In_ LPCWSTR wszModuleName, _In_ LPCSTR szFunctionName);








EXPORTED_FUNCTION
_Success_(return != 0) ULONG64 VMMDLL_ProcessGetModuleBaseU(_In_ VMM_HANDLE hVMM, _In_ DWORD dwPID, _In_ LPCSTR  uszModuleName);
_Success_(return != 0) ULONG64 VMMDLL_ProcessGetModuleBaseW(_In_ VMM_HANDLE hVMM, _In_ DWORD dwPID, _In_ LPCWSTR wszModuleName);















EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_PdbLoad(
    _In_ VMM_HANDLE hVMM,
    _In_ DWORD dwPID,
    _In_ ULONG64 vaModuleBase,
    _Out_writes_(MAX_PATH) LPSTR szModuleName
);












EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_PdbSymbolName(
    _In_ VMM_HANDLE hVMM,
    _In_ LPCSTR szModule,
    _In_ QWORD cbSymbolAddressOrOffset,
    _Out_writes_(MAX_PATH) LPSTR szSymbolName,
    _Out_opt_ PDWORD pdwSymbolDisplacement
);











EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_PdbSymbolAddress(
    _In_ VMM_HANDLE hVMM,
    _In_ LPCSTR szModule,
    _In_ LPCSTR szSymbolName,
    _Out_ PULONG64 pvaSymbolAddress
);











EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_PdbTypeSize(
    _In_ VMM_HANDLE hVMM,
    _In_ LPCSTR szModule,
    _In_ LPCSTR szTypeName,
    _Out_ PDWORD pcbTypeSize
);












EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_PdbTypeChildOffset(
    _In_ VMM_HANDLE hVMM,
    _In_ LPCSTR szModule,
    _In_ LPCSTR uszTypeName,
    _In_ LPCSTR uszTypeChildName,
    _Out_ PDWORD pcbTypeChildOffset
);







#define VMMDLL_REGISTRY_HIVE_INFORMATION_MAGIC      0xc0ffee653df8d01e
#define VMMDLL_REGISTRY_HIVE_INFORMATION_VERSION    5

typedef struct td_VMMDLL_REGISTRY_HIVE_INFORMATION {
    ULONG64 magic;
    WORD wVersion;
    WORD wSize;
    BYTE _FutureReserved1[0x34];
    ULONG64 vaCMHIVE;
    ULONG64 vaHBASE_BLOCK;
    DWORD cbLength;
    CHAR uszName[128];
    CHAR uszNameShort[32 + 1];
    CHAR uszHiveRootPath[MAX_PATH];
    struct {
        BOOL fValid;
        DWORD dwHandle;
        QWORD vaFileObject;
    } File;
    QWORD _FutureReserved[0x0e];
} VMMDLL_REGISTRY_HIVE_INFORMATION, *PVMMDLL_REGISTRY_HIVE_INFORMATION;









EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_WinReg_HiveList(
    _In_ VMM_HANDLE hVMM,
    _Out_writes_(cHives) PVMMDLL_REGISTRY_HIVE_INFORMATION pHives,
    _In_ DWORD cHives,
    _Out_ PDWORD pcHives
);















EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_WinReg_HiveReadEx(
    _In_ VMM_HANDLE hVMM,
    _In_ ULONG64 vaCMHive,
    _In_ DWORD ra,
    _Out_ PBYTE pb,
    _In_ DWORD cb,
    _Out_opt_ PDWORD pcbReadOpt,
    _In_ ULONG64 flags
);











EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_WinReg_HiveWrite(
    _In_ VMM_HANDLE hVMM,
    _In_ ULONG64 vaCMHive,
    _In_ DWORD ra,
    _In_ PBYTE pb,
    _In_ DWORD cb
);

















EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_WinReg_EnumKeyExU(
    _In_ VMM_HANDLE hVMM,
    _In_ LPCSTR uszFullPathKey,
    _In_ DWORD dwIndex,
    _Out_writes_opt_(*lpcchName) LPSTR lpName,
    _Inout_ LPDWORD lpcchName,
    _Out_opt_ PFILETIME lpftLastWriteTime
);




















EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_WinReg_EnumValueU(
    _In_ VMM_HANDLE hVMM,
    _In_ LPCSTR uszFullPathKey,
    _In_ DWORD dwIndex,
    _Out_writes_opt_(*lpcchValueName) LPSTR lpValueName,
    _Inout_ LPDWORD lpcchValueName,
    _Out_opt_ LPDWORD lpType,
    _Out_writes_opt_(*lpcbData) LPBYTE lpData,
    _Inout_opt_ LPDWORD lpcbData
);









EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_WinReg_QueryNameOriginalU(
    _In_ VMM_HANDLE hVMM,
    _In_ LPCSTR uszFullPath,
    _In_ BOOL fValue,
    _Out_writes_opt_(*pcbName) LPSTR uszName,
    _When_(uszName == NULL, _Out_) _When_(uszName != NULL, _Inout_) PDWORD pcbName
);

















EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_WinReg_QueryValueExU(
    _In_ VMM_HANDLE hVMM,
    _In_ LPCSTR uszFullPathKeyValue,
    _Out_opt_ LPDWORD lpType,
    _Out_writes_opt_(*lpcbData) LPBYTE lpData,
    _When_(lpData == NULL, _Out_opt_) _When_(lpData != NULL, _Inout_opt_) LPDWORD lpcbData
);

















_Success_(return)
BOOL VMMDLL_WinReg_EnumKeyExW(
    _In_ VMM_HANDLE hVMM,
    _In_ LPCWSTR wszFullPathKey,
    _In_ DWORD dwIndex,
    _Out_writes_opt_(*lpcchName) LPWSTR lpName,
    _Inout_ LPDWORD lpcchName,
    _Out_opt_ PFILETIME lpftLastWriteTime
);




















_Success_(return)
BOOL VMMDLL_WinReg_EnumValueW(
    _In_ VMM_HANDLE hVMM,
    _In_ LPCWSTR wszFullPathKey,
    _In_ DWORD dwIndex,
    _Out_writes_opt_(*lpcchValueName) LPWSTR lpValueName,
    _Inout_ LPDWORD lpcchValueName,
    _Out_opt_ LPDWORD lpType,
    _Out_writes_opt_(*lpcbData) LPBYTE lpData,
    _Inout_opt_ LPDWORD lpcbData
);









_Success_(return)
BOOL VMMDLL_WinReg_QueryNameOriginalW(
    _In_ VMM_HANDLE hVMM,
    _In_ LPCWSTR wszFullPath,
    _In_ BOOL fValue,
    _Out_writes_opt_(*pcchName) LPWSTR wszName,
    _When_(wszName == NULL, _Out_) _When_(wszName != NULL, _Inout_) PDWORD pcchName
);

















_Success_(return)
BOOL VMMDLL_WinReg_QueryValueExW(
    _In_ VMM_HANDLE hVMM,
    _In_ LPCWSTR wszFullPathKeyValue,
    _Out_opt_ LPDWORD lpType,
    _Out_writes_opt_(*lpcbData) LPBYTE lpData,
    _When_(lpData == NULL, _Out_opt_) _When_(lpData != NULL, _Inout_opt_) LPDWORD lpcbData
);







typedef struct tdVMMDLL_WIN_THUNKINFO_IAT {
    BOOL fValid;
    BOOL f32;
    ULONG64 vaThunk;
    ULONG64 vaFunction;
    ULONG64 vaNameModule;
    ULONG64 vaNameFunction;
} VMMDLL_WIN_THUNKINFO_IAT, *PVMMDLL_WIN_THUNKINFO_IAT;













EXPORTED_FUNCTION
_Success_(return) BOOL VMMDLL_WinGetThunkInfoIATU(_In_ VMM_HANDLE hVMM, _In_ DWORD dwPID, _In_ LPCSTR  uszModuleName, _In_ LPCSTR szImportModuleName, _In_ LPCSTR szImportFunctionName, _Out_ PVMMDLL_WIN_THUNKINFO_IAT pThunkInfoIAT);
_Success_(return) BOOL VMMDLL_WinGetThunkInfoIATW(_In_ VMM_HANDLE hVMM, _In_ DWORD dwPID, _In_ LPCWSTR wszModuleName, _In_ LPCSTR szImportModuleName, _In_ LPCSTR szImportFunctionName, _Out_ PVMMDLL_WIN_THUNKINFO_IAT pThunkInfoIAT);















EXPORTED_FUNCTION _Success_(return != NULL)
VMM_HANDLE VMMDLL_VmGetVmmHandle(_In_ VMM_HANDLE hVMM, _In_ VMMVM_HANDLE hVM);










EXPORTED_FUNCTION _Success_(return != NULL)
VMMDLL_SCATTER_HANDLE VMMDLL_VmScatterInitialize(_In_ VMM_HANDLE hVMM, _In_ VMMVM_HANDLE hVM);










EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_VmMemRead(_In_ VMM_HANDLE hVMM, _In_ VMMVM_HANDLE hVM, _In_ ULONG64 qwGPA, _Out_writes_(cb) PBYTE pb, _In_ DWORD cb);










EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_VmMemWrite(_In_ VMM_HANDLE hVMM, _In_ VMMVM_HANDLE hVM, _In_ ULONG64 qwGPA, _In_reads_(cb) PBYTE pb, _In_ DWORD cb);











EXPORTED_FUNCTION
DWORD VMMDLL_VmMemReadScatter(_In_ VMM_HANDLE hVMM, _In_ VMMVM_HANDLE hVM, _Inout_ PPMEM_SCATTER ppMEMsGPA, _In_ DWORD cpMEMsGPA, _In_ DWORD flags);










EXPORTED_FUNCTION
DWORD VMMDLL_VmMemWriteScatter(_In_ VMM_HANDLE hVMM, _In_ VMMVM_HANDLE hVM, _Inout_ PPMEM_SCATTER ppMEMsGPA, _In_ DWORD cpMEMsGPA);











EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_VmMemTranslateGPA(_In_ VMM_HANDLE H, _In_ VMMVM_HANDLE hVM, _In_ ULONG64 qwGPA, _Out_opt_ PULONG64 pPA, _Out_opt_ PULONG64 pVA);
















EXPORTED_FUNCTION _Success_(return)
BOOL VMMDLL_UtilFillHexAscii(
    _In_reads_opt_(cb) PBYTE pb,
    _In_ DWORD cb,
    _In_ DWORD cbInitialOffset,
    _Out_writes_opt_(*pcsz) LPSTR sz,
    _Inout_ PDWORD pcsz
);






EXPORTED_FUNCTION _Success_(return != NULL)
LPSTR VMMDLL_LicensedTo();







#ifdef _WIN32
#define VMMDLL_VfsList                  VMMDLL_VfsListW
#define VMMDLL_VfsRead                  VMMDLL_VfsReadW
#define VMMDLL_VfsWrite                 VMMDLL_VfsWriteW
#define VMMDLL_ProcessGetDirectories    VMMDLL_ProcessGetDirectoriesW
#define VMMDLL_ProcessGetSections       VMMDLL_ProcessGetSectionsW
#define VMMDLL_ProcessGetProcAddress    VMMDLL_ProcessGetProcAddressW
#define VMMDLL_ProcessGetModuleBase     VMMDLL_ProcessGetModuleBaseW
#define VMMDLL_Map_GetPte               VMMDLL_Map_GetPteW
#define VMMDLL_Map_GetVad               VMMDLL_Map_GetVadW
#define VMMDLL_Map_GetModule            VMMDLL_Map_GetModuleW
#define VMMDLL_Map_GetModuleFromName    VMMDLL_Map_GetModuleFromNameW
#define VMMDLL_Map_GetUnloadedModule    VMMDLL_Map_GetUnloadedModuleW
#define VMMDLL_Map_GetEAT               VMMDLL_Map_GetEATW
#define VMMDLL_Map_GetIAT               VMMDLL_Map_GetIATW
#define VMMDLL_Map_GetHandle            VMMDLL_Map_GetHandleW
#define VMMDLL_Map_GetNet               VMMDLL_Map_GetNetW
#define VMMDLL_Map_GetUsers             VMMDLL_Map_GetUsersW
#define VMMDLL_Map_GetServices          VMMDLL_Map_GetServicesW
#define VMMDLL_WinGetThunkInfoIAT       VMMDLL_WinGetThunkInfoIATW
#endif

#ifdef __cplusplus
}
#endif
#endif

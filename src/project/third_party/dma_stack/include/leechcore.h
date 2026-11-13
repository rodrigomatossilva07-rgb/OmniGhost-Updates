


















#ifndef __LEECHCORE_H__
#define __LEECHCORE_H__
#ifdef __cplusplus
extern "C" {
#endif





#ifdef _WIN32

#include <Windows.h>
#define EXPORTED_FUNCTION                   __declspec(dllexport)
typedef unsigned __int64                    QWORD, *PQWORD;

#endif
#if defined(LINUX) || defined(MACOS)

#include <inttypes.h>
#include <stdlib.h>
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
#define _In_reads_bytes_opt_(x)
#define _In_reads_opt_(x)
#define _Inout_
#define _Inout_bytecount_(x)
#define _Inout_opt_
#define _Inout_updates_opt_(x)
#define _Out_
#define _Out_opt_
#define _Out_writes_(x)
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














#define LC_CONFIG_VERSION                       0xc0fd0002
#define LC_CONFIG_ERRORINFO_VERSION             0xc0fe0002

#define LC_CONFIG_PRINTF_ENABLED                0x01
#define LC_CONFIG_PRINTF_V                      0x02
#define LC_CONFIG_PRINTF_VV                     0x04
#define LC_CONFIG_PRINTF_VVV                    0x08

typedef struct LC_CONFIG {

    DWORD dwVersion;
    DWORD dwPrintfVerbosity;
    CHAR szDevice[MAX_PATH];
    CHAR szRemote[MAX_PATH];
    _Check_return_opt_ int(*pfn_printf_opt)(_In_z_ _Printf_format_string_ char const *const _Format, ...);

    QWORD paMax;

    BOOL fVolatile;
    BOOL fWritable;
    BOOL fRemote;
    BOOL fRemoteDisableCompress;
    CHAR szDeviceName[MAX_PATH];
} LC_CONFIG, *PLC_CONFIG;

typedef struct tdLC_CONFIG_ERRORINFO {
    DWORD dwVersion;
    DWORD cbStruct;
    DWORD _FutureUse[16];
    BOOL fUserInputRequest;
    DWORD cwszUserText;
    WCHAR wszUserText[];
} LC_CONFIG_ERRORINFO, *PLC_CONFIG_ERRORINFO, **PPLC_CONFIG_ERRORINFO;











EXPORTED_FUNCTION _Success_(return != NULL)
HANDLE LcCreate(
    _Inout_ PLC_CONFIG pLcCreateConfig
);

EXPORTED_FUNCTION _Success_(return != NULL)
HANDLE LcCreateEx(
    _Inout_ PLC_CONFIG pLcCreateConfig,
    _Out_opt_ PPLC_CONFIG_ERRORINFO ppLcCreateErrorInfo
);




EXPORTED_FUNCTION
VOID LcClose(
    _In_opt_ _Post_ptr_invalid_ HANDLE hLC
);
















#define MEM_SCATTER_VERSION                 0xc0fe0002
#define MEM_SCATTER_STACK_SIZE              12

typedef struct tdMEM_SCATTER {
    DWORD version;
    BOOL f;
    QWORD qwA;
    union {
        PBYTE pb;
        QWORD _Filler;
    };
    DWORD cb;
    DWORD iStack;
    QWORD vStack[MEM_SCATTER_STACK_SIZE];
} MEM_SCATTER, *PMEM_SCATTER, **PPMEM_SCATTER;

#define MEM_SCATTER_ADDR_INVALID            ((QWORD)-1)
#define MEM_SCATTER_ADDR_ISINVALID(pMEM)    (pMEM->qwA == (QWORD)-1)
#define MEM_SCATTER_ADDR_ISVALID(pMEM)      (pMEM->qwA != (QWORD)-1)
#define MEM_SCATTER_STACK_PUSH(pMEM, v)     (pMEM->vStack[pMEM->iStack++] = (QWORD)(v))
#define MEM_SCATTER_STACK_PEEK(pMEM, i)     (pMEM->vStack[pMEM->iStack - i])
#define MEM_SCATTER_STACK_SET(pMEM, i, v)   (pMEM->vStack[pMEM->iStack - i] = (QWORD)(v))
#define MEM_SCATTER_STACK_ADD(pMEM, i, v)   (pMEM->vStack[pMEM->iStack - i] += (QWORD)(v))
#define MEM_SCATTER_STACK_POP(pMEM)         (pMEM->vStack[--pMEM->iStack])






EXPORTED_FUNCTION
VOID LcMemFree(
    _Frees_ptr_opt_ PVOID pv
);









EXPORTED_FUNCTION _Success_(return)
BOOL LcAllocScatter1(
    _In_ DWORD cMEMs,
    _Out_ PPMEM_SCATTER *pppMEMs
);











EXPORTED_FUNCTION _Success_(return)
BOOL LcAllocScatter2(
    _In_ DWORD cbData,
    _Inout_updates_opt_(cbData) PBYTE pbData,
    _In_ DWORD cMEMs,
    _Out_ PPMEM_SCATTER *pppMEMs
);












EXPORTED_FUNCTION _Success_(return)
BOOL LcAllocScatter3(
    _Inout_updates_opt_(0x1000) PBYTE pbDataFirstPage,
    _Inout_updates_opt_(0x1000) PBYTE pbDataLastPage,
    _In_ DWORD cbData,
    _Inout_updates_opt_(cbData) PBYTE pbData,
    _In_ DWORD cMEMs,
    _Out_ PPMEM_SCATTER *pppMEMs
);







EXPORTED_FUNCTION
VOID LcReadScatter(
    _In_ HANDLE hLC,
    _In_ DWORD cMEMs,
    _Inout_ PPMEM_SCATTER ppMEMs
);










EXPORTED_FUNCTION _Success_(return)
BOOL LcRead(
    _In_ HANDLE hLC,
    _In_ QWORD pa,
    _In_ DWORD cb,
    _Out_writes_(cb) PBYTE pb
);






typedef DWORD LC_READ_PAGE_RESULT, *PLC_READ_PAGE_RESULT;
#define LC_READ_PAGE_RESULT_NONE                    0x00
#define LC_READ_PAGE_RESULT_SUCCESS                 0x01
#define LC_READ_PAGE_RESULT_SUCCESS_AFTER_RETRY     0x02
#define LC_READ_PAGE_RESULT_UNSUPPORTED_REQUEST     0x03
#define LC_READ_PAGE_RESULT_COMPLETER_ABORT         0x04
#define LC_READ_PAGE_RESULT_NO_COMPLETION           0x05
#define LC_READ_PAGE_RESULT_PARTIAL_COMPLETION      0x06
#define LC_READ_PAGE_RESULT_TRANSPORT_ERROR         0x07
#define LC_READ_PAGE_RESULT_PROTOCOL_ERROR          0x08
#define LC_READ_PAGE_RESULT_UNSPECIFIED_ERROR       0x09
#define LC_READ_PAGE_RESULT_NOT_ISSUED              0x0A











EXPORTED_FUNCTION _Success_(return)
BOOL LcReadScatterEx(
    _In_ HANDLE hLC,
    _In_ DWORD cMEMs,
    _Inout_ PPMEM_SCATTER ppMEMs,
    _Out_writes_(cMEMs) PLC_READ_PAGE_RESULT pResults
);







EXPORTED_FUNCTION
VOID LcWriteScatter(
    _In_ HANDLE hLC,
    _In_ DWORD cMEMs,
    _Inout_ PPMEM_SCATTER ppMEMs
);









EXPORTED_FUNCTION _Success_(return)
BOOL LcWrite(
    _In_ HANDLE hLC,
    _In_ QWORD pa,
    _In_ DWORD cb,
    _In_reads_(cb) PBYTE pb
);
















EXPORTED_FUNCTION _Success_(return)
BOOL LcGetOption(
    _In_ HANDLE hLC,
    _In_ QWORD fOption,
    _Out_ PQWORD pqwValue
);








EXPORTED_FUNCTION _Success_(return)
BOOL LcSetOption(
    _In_ HANDLE hLC,
    _In_ QWORD fOption,
    _In_ QWORD qwValue
);













EXPORTED_FUNCTION _Success_(return)
BOOL LcCommand(
    _In_ HANDLE hLC,
    _In_ QWORD fCommand,
    _In_ DWORD cbDataIn,
    _In_reads_opt_(cbDataIn) PBYTE pbDataIn,
    _Out_opt_ PBYTE *ppbDataOut,
    _Out_opt_ PDWORD pcbDataOut
);

#define LC_OPT_CORE_PRINTF_ENABLE                   0x4000000100000000
#define LC_OPT_CORE_VERBOSE                         0x4000000200000000
#define LC_OPT_CORE_VERBOSE_EXTRA                   0x4000000300000000
#define LC_OPT_CORE_VERBOSE_EXTRA_TLP               0x4000000400000000
#define LC_OPT_CORE_VERSION_MAJOR                   0x4000000500000000
#define LC_OPT_CORE_VERSION_MINOR                   0x4000000600000000
#define LC_OPT_CORE_VERSION_REVISION                0x4000000700000000
#define LC_OPT_CORE_ADDR_MAX                        0x1000000800000000
#define LC_OPT_CORE_STATISTICS_CALL_COUNT           0x4000000900000000
#define LC_OPT_CORE_STATISTICS_CALL_TIME            0x4000000a00000000
#define LC_OPT_CORE_VOLATILE                        0x1000000b00000000
#define LC_OPT_CORE_READONLY                        0x1000000c00000000

#define LC_OPT_MEMORYINFO_VALID                     0x0200000100000000
#define LC_OPT_MEMORYINFO_FLAG_32BIT                0x0200000300000000
#define LC_OPT_MEMORYINFO_FLAG_PAE                  0x0200000400000000
#define LC_OPT_MEMORYINFO_ARCH                      0x0200001200000000
#define LC_OPT_MEMORYINFO_OS_VERSION_MINOR          0x0200000500000000
#define LC_OPT_MEMORYINFO_OS_VERSION_MAJOR          0x0200000600000000
#define LC_OPT_MEMORYINFO_OS_DTB                    0x0200000700000000
#define LC_OPT_MEMORYINFO_OS_PFN                    0x0200000800000000
#define LC_OPT_MEMORYINFO_OS_PsLoadedModuleList     0x0200000900000000
#define LC_OPT_MEMORYINFO_OS_PsActiveProcessHead    0x0200000a00000000
#define LC_OPT_MEMORYINFO_OS_MACHINE_IMAGE_TP       0x0200000b00000000
#define LC_OPT_MEMORYINFO_OS_NUM_PROCESSORS         0x0200000c00000000
#define LC_OPT_MEMORYINFO_OS_SYSTEMTIME             0x0200000d00000000
#define LC_OPT_MEMORYINFO_OS_UPTIME                 0x0200000e00000000
#define LC_OPT_MEMORYINFO_OS_KERNELBASE             0x0200000f00000000
#define LC_OPT_MEMORYINFO_OS_KERNELHINT             0x0200001000000000
#define LC_OPT_MEMORYINFO_OS_KdDebuggerDataBlock    0x0200001100000000

#define LC_OPT_FPGA_PROBE_MAXPAGES                  0x0300000100000000
#define LC_OPT_FPGA_MAX_SIZE_RX                     0x0300000300000000
#define LC_OPT_FPGA_MAX_SIZE_TX                     0x0300000400000000
#define LC_OPT_FPGA_DELAY_PROBE_READ                0x0300000500000000
#define LC_OPT_FPGA_DELAY_PROBE_WRITE               0x0300000600000000
#define LC_OPT_FPGA_DELAY_WRITE                     0x0300000700000000
#define LC_OPT_FPGA_DELAY_READ                      0x0300000800000000
#define LC_OPT_FPGA_RETRY_ON_ERROR                  0x0300000900000000
#define LC_OPT_FPGA_DEVICE_ID                       0x0300008000000000
#define LC_OPT_FPGA_FPGA_ID                         0x0300008100000000
#define LC_OPT_FPGA_VERSION_MAJOR                   0x0300008200000000
#define LC_OPT_FPGA_VERSION_MINOR                   0x0300008300000000
#define LC_OPT_FPGA_ALGO_TINY                       0x0300008400000000
#define LC_OPT_FPGA_ALGO_SYNCHRONOUS                0x0300008500000000
#define LC_OPT_FPGA_CFGSPACE_XILINX                 0x0300008600000000
#define LC_OPT_FPGA_TLP_READ_CB_WITHINFO            0x0300009000000000
#define LC_OPT_FPGA_TLP_READ_CB_FILTERCPL           0x0300009100000000

#define LC_CMD_FPGA_PCIECFGSPACE                    0x0000010300000000
#define LC_CMD_FPGA_CFGREGPCIE                      0x0000010400000000
#define LC_CMD_FPGA_CFGREGCFG                       0x0000010500000000
#define LC_CMD_FPGA_CFGREGDRP                       0x0000010600000000
#define LC_CMD_FPGA_CFGREGCFG_MARKWR                0x0000010700000000
#define LC_CMD_FPGA_CFGREGPCIE_MARKWR               0x0000010800000000
#define LC_CMD_FPGA_CFGREG_DEBUGPRINT               0x0000010a00000000
#define LC_CMD_FPGA_PROBE                           0x0000010b00000000
#define LC_CMD_FPGA_CFGSPACE_SHADOW_RD              0x0000010c00000000
#define LC_CMD_FPGA_CFGSPACE_SHADOW_WR              0x0000010d00000000
#define LC_CMD_FPGA_TLP_WRITE_SINGLE                0x0000011000000000
#define LC_CMD_FPGA_TLP_WRITE_MULTIPLE              0x0000011100000000
#define LC_CMD_FPGA_TLP_TOSTRING                    0x0000011200000000

#define LC_CMD_FPGA_TLP_CONTEXT                     0x2000011400000000
#define LC_CMD_FPGA_TLP_CONTEXT_RD                  0x2000011b00000000
#define LC_CMD_FPGA_TLP_FUNCTION_CALLBACK           0x2000011500000000
#define LC_CMD_FPGA_TLP_FUNCTION_CALLBACK_RD        0x2000011c00000000
#define LC_CMD_FPGA_BAR_CONTEXT                     0x2000012000000000
#define LC_CMD_FPGA_BAR_CONTEXT_RD                  0x2000012100000000
#define LC_CMD_FPGA_BAR_FUNCTION_CALLBACK           0x2000012200000000
#define LC_CMD_FPGA_BAR_FUNCTION_CALLBACK_RD        0x2000012300000000
#define LC_CMD_FPGA_BAR_INFO                        0x0000012400000000

#define LC_CMD_FILE_DUMPHEADER_GET                  0x0000020100000000

#define LC_CMD_STATISTICS_GET                       0x4000010000000000
#define LC_CMD_MEMMAP_GET                           0x4000020000000000
#define LC_CMD_MEMMAP_SET                           0x4000030000000000
#define LC_CMD_MEMMAP_GET_STRUCT                    0x4000040000000000
#define LC_CMD_MEMMAP_SET_STRUCT                    0x4000050000000000

#define LC_CMD_AGENT_EXEC_PYTHON                    0x8000000100000000
#define LC_CMD_AGENT_EXIT_PROCESS                   0x8000000200000000
#define LC_CMD_AGENT_VFS_LIST                       0x8000000300000000
#define LC_CMD_AGENT_VFS_READ                       0x8000000400000000
#define LC_CMD_AGENT_VFS_WRITE                      0x8000000500000000
#define LC_CMD_AGENT_VFS_OPT_GET                    0x8000000600000000
#define LC_CMD_AGENT_VFS_OPT_SET                    0x8000000700000000
#define LC_CMD_AGENT_VFS_INITIALIZE                 0x8000000800000000
#define LC_CMD_AGENT_VFS_CONSOLE                    0x8000000900000000

#define LC_CMD_AGENT_VFS_REQ_VERSION                0xfeed0001
#define LC_CMD_AGENT_VFS_RSP_VERSION                0xfeee0001

#define LC_STATISTICS_VERSION                       0xe1a10002
#define LC_STATISTICS_ID_OPEN                       0x00
#define LC_STATISTICS_ID_READ                       0x01
#define LC_STATISTICS_ID_READSCATTER                0x02
#define LC_STATISTICS_ID_WRITE                      0x03
#define LC_STATISTICS_ID_WRITESCATTER               0x04
#define LC_STATISTICS_ID_GETOPTION                  0x05
#define LC_STATISTICS_ID_SETOPTION                  0x06
#define LC_STATISTICS_ID_COMMAND                    0x07
#define LC_STATISTICS_ID_MAX                        0x07

typedef struct tdLC_CMD_AGENT_VFS_REQ {
    DWORD dwVersion;
    DWORD _FutureUse;
    CHAR uszPathFile[2*MAX_PATH];
    union {
        QWORD qwOffset;
        QWORD fOption;
    };
    DWORD dwLength;
    DWORD cb;
    BYTE pb[0];
} LC_CMD_AGENT_VFS_REQ, *PLC_CMD_AGENT_VFS_REQ;

typedef struct tdLC_CMD_AGENT_VFS_RSP {
    DWORD dwVersion;
    DWORD dwStatus;
    DWORD cbReadWrite;
    DWORD _FutureUse[2];
    DWORD cb;
    BYTE pb[0];
} LC_CMD_AGENT_VFS_RSP, *PLC_CMD_AGENT_VFS_RSP;

static LPCSTR LC_STATISTICS_NAME[] = {
    "LcOpen",
    "LcRead",
    "LcReadScatter",
    "LcWrite",
    "LcWriteScatter",
    "LcGetOption",
    "LcSetOption",
    "LcCommand",
};

typedef struct tdLC_STATISTICS {
    DWORD dwVersion;
    DWORD _Reserved;
    QWORD qwFreq;
    struct {
        QWORD c;
        QWORD tm;
    } Call[LC_STATISTICS_ID_MAX + 1];
} LC_STATISTICS, *PLC_STATISTICS;

typedef struct tdLC_MEMMAP_ENTRY {
    QWORD pa;
    QWORD cb;
    QWORD paRemap;
} LC_MEMMAP_ENTRY, *PLC_MEMMAP_ENTRY;

typedef enum tdLC_ARCH_TP {
    LC_ARCH_NA      = 0,
    LC_ARCH_X86     = 1,
    LC_ARCH_X86PAE  = 2,
    LC_ARCH_X64     = 3,
    LC_ARCH_ARM64   = 4,
} LC_ARCH_TP;










typedef struct tdLC_TLP {
    DWORD cb;
    DWORD _Reserved1;
    PBYTE pb;
} LC_TLP, *PLC_TLP;






typedef VOID(*PLC_TLP_FUNCTION_CALLBACK)(
    _In_opt_ PVOID ctx,
    _In_ DWORD cbTlp,
    _In_ PBYTE pbTlp,
    _In_opt_ DWORD cbInfo,
    _In_opt_ LPSTR szInfo
);

#define LC_TLP_FUNCTION_CALLBACK_DISABLE        (PLC_TLP_FUNCTION_CALLBACK)(NULL)
#define LC_TLP_FUNCTION_CALLBACK_DUMMY          (PLC_TLP_FUNCTION_CALLBACK)(-1)










#define LC_VMM_VERSION                          0x1eef0001

typedef struct tdLC_VMM {
    DWORD dwVersion;
    HANDLE hVMM;
    HANDLE hVMMVM;
    PVOID pfnVMMDLL_ConfigGet;
    PVOID pfnVMMDLL_VmMemReadScatter;
    PVOID pfnVMMDLL_VmMemWriteScatter;
} LC_VMM, *PLC_VMM;







typedef struct tdLC_BAR {
    BOOL fValid;
    BOOL fIO;
    BOOL f64Bit;
    BOOL fPrefetchable;
    DWORD _Filler[3];
    DWORD iBar;
    QWORD pa;
    QWORD cb;
} LC_BAR, *PLC_BAR;

typedef struct tdLC_BAR_REQUEST {
    PVOID ctx;
    PLC_BAR pBar;
    BYTE bTag;
    BYTE bFirstBE;
    BYTE bLastBE;
    BYTE _Filler;
    BOOL f64;
    BOOL fRead;
    BOOL fReadReply;
    BOOL fWrite;
    DWORD cbData;
    QWORD oData;
    BYTE pbData[4096];
} LC_BAR_REQUEST, *PLC_BAR_REQUEST;








typedef VOID(*PLC_BAR_FUNCTION_CALLBACK)(_Inout_ PLC_BAR_REQUEST pBarRequest);

#define LC_BAR_FUNCTION_CALLBACK_DISABLE        (PLC_BAR_FUNCTION_CALLBACK)(NULL)
#define LC_BAR_FUNCTION_CALLBACK_ZEROBAR        (PLC_BAR_FUNCTION_CALLBACK)(-1)


#ifdef __cplusplus
}
#endif
#endif

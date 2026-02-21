#ifndef UEFI_H
#define UEFI_H

#include <stddef.h>
#include <stdint.h>

#define EFIAPI __attribute__((ms_abi))

typedef uint64_t UINT64;
typedef uint32_t UINT32;
typedef uint16_t UINT16;
typedef uint8_t UINT8;
typedef uint64_t UINTN;
typedef UINT8 BOOLEAN;
typedef int64_t INT64;
typedef int32_t INT32;
typedef int16_t INT16;
typedef int8_t INT8;

typedef uint16_t char16_t;
typedef char16_t CHAR16;

typedef UINT64 EFI_STATUS;
typedef void *EFI_HANDLE;

typedef struct {
  UINT32 Data1;
  UINT16 Data2;
  UINT16 Data3;
  UINT8 Data4[8];
} EFI_GUID;

typedef struct {
  UINT64 Signature;
  UINT32 Revision;
  UINT32 HeaderSize;
  UINT32 CRC32;
  UINT32 Reserved;
} EFI_TABLE_HEADER;

typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

typedef EFI_STATUS(EFIAPI *EFI_TEXT_STRING)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *self,
                                           CHAR16 *string);

struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
  void *Reset;
  EFI_TEXT_STRING OutputString;
  void *TestString;
  void *QueryMode;
  void *SetMode;
  void *SetAttribute;
  void *ClearScreen;
  void *SetCursorPosition;
  void *EnableCursor;
  void *Mode;
};

typedef struct EFI_BOOT_SERVICES EFI_BOOT_SERVICES;

typedef struct {
  EFI_TABLE_HEADER Hdr;
  CHAR16 *FirmwareVendor;
  UINT32 FirmwareRevision;
  EFI_HANDLE ConsoleInHandle;
  void *ConIn;
  EFI_HANDLE ConsoleOutHandle;
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
  EFI_HANDLE StandardErrorHandle;
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
  void *RuntimeServices;
  EFI_BOOT_SERVICES *BootServices;
  void *NumberOfTableEntries;
  void *ConfigurationTable;
} EFI_SYSTEM_TABLE;

// Loaded Image Protocol
typedef struct {
  UINT32 Revision;
  EFI_HANDLE ParentHandle;
  EFI_SYSTEM_TABLE *SystemTable;
  EFI_HANDLE DeviceHandle;
  void *FilePath;
  void *Reserved;
  UINT32 LoadOptionsSize;
  void *LoadOptions;
  void *ImageBase;
  UINT64 ImageSize;
  UINT32 ImageCodeType;
  UINT32 ImageDataType;
  EFI_STATUS(EFIAPI *Unload)(EFI_HANDLE ImageHandle);
} EFI_LOADED_IMAGE_PROTOCOL;

#define EFI_SUCCESS 0

#define EFI_LOAD_ERROR            0x8000000000000001ULL
#define EFI_INVALID_PARAMETER     0x8000000000000002ULL
#define EFI_UNSUPPORTED           0x8000000000000003ULL
#define EFI_BAD_BUFFER_SIZE       0x8000000000000004ULL
#define EFI_BUFFER_TOO_SMALL      0x8000000000000005ULL
#define EFI_NOT_READY             0x8000000000000006ULL
#define EFI_DEVICE_ERROR          0x8000000000000007ULL
#define EFI_WRITE_PROTECTED       0x8000000000000008ULL
#define EFI_OUT_OF_RESOURCES      0x8000000000000009ULL
#define EFI_NOT_FOUND             0x8000000000000014ULL

#define EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL 0x00000001
#define EFI_OPEN_PROTOCOL_GET_PROTOCOL       0x00000002

#define EFI_LOCATE_BY_PROTOCOL 2

typedef EFI_STATUS(EFIAPI *EFI_ALLOCATE_POOL)(UINT32 PoolType, UINTN Size,
                                              void **Buffer);

typedef EFI_STATUS(EFIAPI *EFI_ALLOCATE_PAGES)(UINT32 Type, UINT32 MemoryType,
                                               UINTN Pages, UINT64 *Memory);

typedef EFI_STATUS(EFIAPI *EFI_FREE_POOL)(void *Buffer);

typedef EFI_STATUS(EFIAPI *EFI_LOCATE_PROTOCOL)(EFI_GUID *Protocol,
                                                void *Registration,
                                                void **Interface);

typedef EFI_STATUS(EFIAPI *EFI_LOCATE_HANDLE_BUFFER)(
    UINT32 SearchType, EFI_GUID *Protocol, void *SearchKey, UINTN *NoHandles,
    EFI_HANDLE **Buffer);

typedef EFI_STATUS(EFIAPI *EFI_HANDLE_PROTOCOL)(EFI_HANDLE Handle,
                                                EFI_GUID *Protocol,
                                                void **Interface);

typedef struct {
  UINT8 Type;
  UINT8 SubType;
  UINT16 Length;
} EFI_DEVICE_PATH_PROTOCOL;

typedef EFI_STATUS(EFIAPI *EFI_LOCATE_DEVICE_PATH)(EFI_GUID *Protocol,
                                                   EFI_DEVICE_PATH_PROTOCOL **DevicePath,
                                                   EFI_HANDLE *Device);

typedef EFI_STATUS(EFIAPI *EFI_OPEN_PROTOCOL)(
    EFI_HANDLE Handle, EFI_GUID *Protocol, void **Interface, EFI_HANDLE Agent,
    EFI_HANDLE Controller, UINT32 Attributes);

typedef EFI_STATUS(EFIAPI *EFI_CONNECT_CONTROLLER)(
    EFI_HANDLE ControllerHandle, EFI_HANDLE *DriverImageHandle,
    EFI_DEVICE_PATH_PROTOCOL *RemainingDevicePath, BOOLEAN Recursive);

typedef EFI_STATUS(EFIAPI *EFI_GET_MEMORY_MAP)(
    UINTN *MemoryMapSize, void *MemoryMap, UINTN *MapKey, UINTN *DescriptorSize,
    UINT32 *DescriptorVersion);

typedef EFI_STATUS(EFIAPI *EFI_EXIT_BOOT_SERVICES)(EFI_HANDLE ImageHandle,
                                                   UINTN MapKey);

typedef EFI_STATUS(EFIAPI *EFI_SET_WATCHDOG_TIMER)(UINTN Timeout,
                                                   UINT64 WatchdogCode,
                                                   UINTN DataSize,
                                                   CHAR16 *WatchdogData);

struct EFI_BOOT_SERVICES {
  EFI_TABLE_HEADER Hdr;
  void *RaiseTPL;
  void *RestoreTPL;
  EFI_ALLOCATE_PAGES AllocatePages;
  void *FreePages;
  EFI_GET_MEMORY_MAP GetMemoryMap;
  EFI_ALLOCATE_POOL AllocatePool;
  EFI_FREE_POOL FreePool;
  void *CreateEvent;
  void *SetTimer;
  void *WaitForEvent;
  void *SignalEvent;
  void *CloseEvent;
  void *CheckEvent;
  void *InstallProtocolInterface;
  void *ReinstallProtocolInterface;
  void *UninstallProtocolInterface;
  EFI_HANDLE_PROTOCOL HandleProtocol;
  void *Reserved;
  void *RegisterProtocolNotify;
  void *LocateHandle;
  EFI_LOCATE_DEVICE_PATH LocateDevicePath;
  void *InstallConfigurationTable;
  void *LoadImage;
  void *StartImage;
  void *Exit;
  void *UnloadImage;
  EFI_EXIT_BOOT_SERVICES ExitBootServices;
  void *GetNextMonotonicCount;
  void *Stall;
  EFI_SET_WATCHDOG_TIMER SetWatchdogTimer;
  EFI_CONNECT_CONTROLLER ConnectController;
  void *DisconnectController;
  EFI_OPEN_PROTOCOL OpenProtocol;
  void *CloseProtocol;
  void *OpenProtocolInformation;
  void *ProtocolsPerHandle;
  EFI_LOCATE_HANDLE_BUFFER LocateHandleBuffer;
  EFI_LOCATE_PROTOCOL LocateProtocol;
  void *InstallMultipleProtocolInterfaces;
  void *UninstallMultipleProtocolInterfaces;
  void *CalculateCrc32;
  void *CopyMem;
  void *SetMem;
  void *CreateEventEx;
};

// Minimal FILE protocol definitions

typedef struct EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;

typedef EFI_STATUS(EFIAPI *EFI_FILE_OPEN)(EFI_FILE_PROTOCOL *self,
                                          EFI_FILE_PROTOCOL **new_handle,
                                          CHAR16 *filename, UINT64 open_mode,
                                          UINT64 attributes);

typedef EFI_STATUS(EFIAPI *EFI_FILE_CLOSE)(EFI_FILE_PROTOCOL *self);

typedef EFI_STATUS(EFIAPI *EFI_FILE_DELETE)(EFI_FILE_PROTOCOL *self);

typedef EFI_STATUS(EFIAPI *EFI_FILE_READ)(EFI_FILE_PROTOCOL *self,
                                          UINTN *buffer_size, void *buffer);

typedef EFI_STATUS(EFIAPI *EFI_FILE_GET_INFO)(EFI_FILE_PROTOCOL *self,
                                              EFI_GUID *information_type,
                                              UINTN *buffer_size,
                                              void *buffer);

struct EFI_FILE_PROTOCOL {
  UINT64 Revision;
  EFI_FILE_OPEN Open;
  EFI_FILE_CLOSE Close;
  EFI_FILE_DELETE Delete;
  EFI_FILE_READ Read;
  void *Write;
  void *GetPosition;
  void *SetPosition;
  EFI_FILE_GET_INFO GetInfo;
  void *SetInfo;
  void *Flush;
  void *OpenEx;
  void *ReadEx;
  void *WriteEx;
  void *FlushEx;
};

typedef struct {
  UINT64 Size;
  UINT64 FileSize;
  UINT64 PhysicalSize;
  UINT64 CreateTime[3];
  UINT64 LastAccessTime[3];
  UINT64 ModificationTime[3];
  UINT64 Attribute;
  CHAR16 FileName[1];
} EFI_FILE_INFO;

#define EFI_FILE_MODE_READ 0x0000000000000001ULL

// Simple File System Protocol

typedef struct {
  UINT64 Revision;
  EFI_STATUS(EFIAPI *OpenVolume)(void *self, EFI_FILE_PROTOCOL **root);
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

// Graphics Output Protocol

typedef struct {
  UINT32 Version;
  UINT32 HorizontalResolution;
  UINT32 VerticalResolution;
  UINT32 PixelFormat;
  UINT32 PixelInformation[4];
  UINT32 PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
  UINT32 MaxMode;
  UINT32 Mode;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
  UINTN SizeOfInfo;
  UINT64 FrameBufferBase;
  UINTN FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct EFI_GRAPHICS_OUTPUT_PROTOCOL {
  void *QueryMode;
  void *SetMode;
  void *Blt;
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

// GUIDs

static const EFI_GUID EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID = {
    0x0964e5b2, 0x6459, 0x11d2,
    {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};

static const EFI_GUID EFI_BLOCK_IO_PROTOCOL_GUID = {
    0x964e5b21, 0x6459, 0x11d2,
    {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};

static const EFI_GUID EFI_FILE_INFO_GUID = {
    0x09576e92, 0x6d3f, 0x11d2,
    {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};

static const EFI_GUID EFI_DEVICE_PATH_PROTOCOL_GUID = {
    0x09576e91, 0x6d3f, 0x11d2,
    {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};

static const EFI_GUID EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID = {
    0x9042a9de, 0x23dc, 0x4a38,
    {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}};

static const EFI_GUID EFI_LOADED_IMAGE_PROTOCOL_GUID = {
    0x5b1b31a1, 0x9562, 0x11d2,
    {0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};

#endif
#define EFI_ALLOCATE_ANY_PAGES 0
#define EFI_ALLOCATE_MAX_ADDRESS 1
#define EFI_ALLOCATE_ADDRESS 2

#define EFI_MEMORY_TYPE_LOADER_DATA 2

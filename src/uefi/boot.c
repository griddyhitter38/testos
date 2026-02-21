#include "uefi.h"
#include "../kernel/kernel.h"

static void print16(EFI_SYSTEM_TABLE *st, const CHAR16 *s) {
  st->ConOut->OutputString(st->ConOut, (CHAR16 *)s);
}

static void print_hex64(EFI_SYSTEM_TABLE *st, UINT64 value) {
  CHAR16 buf[19];
  buf[0] = u'0';
  buf[1] = u'x';
  for (int i = 0; i < 16; ++i) {
    UINT8 nibble = (value >> (60 - 4 * i)) & 0xF;
    buf[2 + i] = (nibble < 10) ? (CHAR16)(u'0' + nibble)
                               : (CHAR16)(u'A' + (nibble - 10));
  }
  buf[18] = 0;
  print16(st, buf);
}

static void print_status(EFI_SYSTEM_TABLE *st, const CHAR16 *msg,
                         EFI_STATUS status) {
  print16(st, msg);
  print_hex64(st, status);
  print16(st, u"\r\n");
}

static EFI_STATUS read_file(EFI_SYSTEM_TABLE *st, EFI_HANDLE image,
                            const CHAR16 *path, void **out_buf,
                            UINTN *out_size) {
  EFI_STATUS status;
  EFI_LOADED_IMAGE_PROTOCOL *loaded = NULL;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;
  EFI_FILE_PROTOCOL *root = NULL;
  EFI_FILE_PROTOCOL *file = NULL;
  EFI_FILE_INFO *info = NULL;
  UINTN info_size = 0;

  status = st->BootServices->HandleProtocol(
      image, (EFI_GUID *)&EFI_LOADED_IMAGE_PROTOCOL_GUID, (void **)&loaded);
  if (status != EFI_SUCCESS) {
    print_status(st, u"HandleProtocol(LoadedImage) failed: ", status);
    return status;
  }
  print16(st, u"Image handle: ");
  print_hex64(st, (UINT64)(UINTN)image);
  print16(st, u"\r\n");
  print16(st, u"Device handle: ");
  print_hex64(st, (UINT64)(UINTN)loaded->DeviceHandle);
  print16(st, u"\r\n");
  EFI_LOADED_IMAGE_PROTOCOL *loaded2 = NULL;
  EFI_STATUS li_status = st->BootServices->HandleProtocol(
      loaded->DeviceHandle, (EFI_GUID *)&EFI_LOADED_IMAGE_PROTOCOL_GUID,
      (void **)&loaded2);
  if (li_status == EFI_SUCCESS) {
    print16(st, u"DeviceHandle is an image handle\r\n");
  } else {
    print_status(st, u"DeviceHandle LoadedImage status: ", li_status);
  }

  EFI_DEVICE_PATH_PROTOCOL *device_path =
      (EFI_DEVICE_PATH_PROTOCOL *)loaded->FilePath;
  EFI_HANDLE device_handle = loaded->DeviceHandle;
  status = st->BootServices->LocateDevicePath(
      (EFI_GUID *)&EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID, &device_path,
      &device_handle);
  if (status != EFI_SUCCESS) {
    print_status(st, u"LocateDevicePath(SimpleFS) failed: ", status);
  }

  EFI_DEVICE_PATH_PROTOCOL *dp = NULL;
  EFI_STATUS dp_status = st->BootServices->OpenProtocol(
      device_handle, (EFI_GUID *)&EFI_DEVICE_PATH_PROTOCOL_GUID,
      (void **)&dp, image, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
  if (dp_status != EFI_SUCCESS) {
    print_status(st, u"OpenProtocol(DevicePath) failed: ", dp_status);
  } else {
    print16(st, u"OpenProtocol(DevicePath) ok\r\n");
  }

  EFI_HANDLE *dp_handles = NULL;
  UINTN dp_handle_count = 0;
  EFI_STATUS dp_loc_status = st->BootServices->LocateHandleBuffer(
      EFI_LOCATE_BY_PROTOCOL, (EFI_GUID *)&EFI_DEVICE_PATH_PROTOCOL_GUID, NULL,
      &dp_handle_count, &dp_handles);
  if (dp_loc_status != EFI_SUCCESS) {
    print_status(st, u"LocateHandleBuffer(DevicePath) failed: ",
                 dp_loc_status);
  } else {
    print16(st, u"LocateHandleBuffer(DevicePath) ok\r\n");
    for (UINTN i = 0; i < dp_handle_count; ++i) {
      st->BootServices->ConnectController(dp_handles[i], NULL, NULL, 1);
    }
    st->BootServices->FreePool(dp_handles);
  }

  EFI_HANDLE *blk_handles = NULL;
  UINTN blk_handle_count = 0;
  EFI_STATUS blk_loc_status = st->BootServices->LocateHandleBuffer(
      EFI_LOCATE_BY_PROTOCOL, (EFI_GUID *)&EFI_BLOCK_IO_PROTOCOL_GUID, NULL,
      &blk_handle_count, &blk_handles);
  if (blk_loc_status != EFI_SUCCESS) {
    print_status(st, u"LocateHandleBuffer(BlockIO) failed: ", blk_loc_status);
  } else {
    print16(st, u"LocateHandleBuffer(BlockIO) ok\r\n");
    for (UINTN i = 0; i < blk_handle_count; ++i) {
      st->BootServices->ConnectController(blk_handles[i], NULL, NULL, 1);
    }
    st->BootServices->FreePool(blk_handles);
  }

  status = st->BootServices->ConnectController(device_handle, NULL, NULL, 1);
  if (status != EFI_SUCCESS) {
    print_status(st, u"ConnectController failed: ", status);
  }

  status = st->BootServices->OpenProtocol(
      device_handle, (EFI_GUID *)&EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID,
      (void **)&fs, image, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
  if (status != EFI_SUCCESS) {
    print_status(st, u"OpenProtocol(SimpleFS) failed: ", status);
    status = st->BootServices->OpenProtocol(
        device_handle, (EFI_GUID *)&EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID,
        (void **)&fs, image, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
    if (status != EFI_SUCCESS) {
      status = st->BootServices->HandleProtocol(
          device_handle, (EFI_GUID *)&EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID,
          (void **)&fs);
      if (status != EFI_SUCCESS) {
        print_status(st, u"HandleProtocol(SimpleFS) failed: ", status);
      }
    }
  }

  if (status == EFI_SUCCESS) {
    status = fs->OpenVolume(fs, &root);
    if (status == EFI_SUCCESS) {
      status = root->Open(root, &file, (CHAR16 *)path, EFI_FILE_MODE_READ, 0);
      if (status == EFI_SUCCESS) {
        goto file_opened;
      }
      root->Close(root);
      root = NULL;
    }
  }

  // Try LocateProtocol(SimpleFS) as a fallback (some firmwares don't expose
  // handles the way we expect).
  fs = NULL;
  status = st->BootServices->LocateProtocol(
      (EFI_GUID *)&EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID, NULL, (void **)&fs);
  if (status == EFI_SUCCESS && fs) {
    status = fs->OpenVolume(fs, &root);
    if (status == EFI_SUCCESS) {
      status = root->Open(root, &file, (CHAR16 *)path, EFI_FILE_MODE_READ, 0);
      if (status == EFI_SUCCESS) {
        goto file_opened;
      }
      root->Close(root);
      root = NULL;
    }
  }

  // Fallback: connect all BlockIO controllers (recursive), then search all
  // SimpleFS handles for the file.
  blk_handles = NULL;
  blk_handle_count = 0;
  blk_loc_status = st->BootServices->LocateHandleBuffer(
      EFI_LOCATE_BY_PROTOCOL, (EFI_GUID *)&EFI_BLOCK_IO_PROTOCOL_GUID, NULL,
      &blk_handle_count, &blk_handles);
  if (blk_loc_status == EFI_SUCCESS && blk_handle_count > 0) {
    for (UINTN i = 0; i < blk_handle_count; ++i) {
      st->BootServices->ConnectController(blk_handles[i], NULL, NULL, 1);
    }
    st->BootServices->FreePool(blk_handles);
  }

  // Search all SimpleFS handles for the file.
  EFI_HANDLE *handles = NULL;
  UINTN handle_count = 0;
  status = st->BootServices->LocateHandleBuffer(
      EFI_LOCATE_BY_PROTOCOL,
      (EFI_GUID *)&EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID, NULL,
      &handle_count, &handles);
  if (status != EFI_SUCCESS || handle_count == 0) {
    print_status(st, u"LocateHandleBuffer(SimpleFS) failed: ", status);
    return status;
  }

  for (UINTN i = 0; i < handle_count; ++i) {
    fs = NULL;
    status = st->BootServices->HandleProtocol(
        handles[i], (EFI_GUID *)&EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID,
        (void **)&fs);
    if (status != EFI_SUCCESS || !fs) {
      continue;
    }
    status = fs->OpenVolume(fs, &root);
    if (status != EFI_SUCCESS) {
      continue;
    }
    status = root->Open(root, &file, (CHAR16 *)path, EFI_FILE_MODE_READ, 0);
    if (status == EFI_SUCCESS) {
      st->BootServices->FreePool(handles);
      goto file_opened;
    }
    root->Close(root);
    root = NULL;
  }

  st->BootServices->FreePool(handles);
  print_status(st, u"Open file failed on all SimpleFS handles: ", status);
  return status;

file_opened:

  status = file->GetInfo(file, (EFI_GUID *)&EFI_FILE_INFO_GUID, &info_size,
                         NULL);
  if (status != EFI_BUFFER_TOO_SMALL) {
    print_status(st, u"GetInfo size failed: ", status);
    return status;
  }

  status = st->BootServices->AllocatePool(EFI_MEMORY_TYPE_LOADER_DATA, info_size,
                                          (void **)&info);
  if (status != EFI_SUCCESS) {
    print_status(st, u"AllocatePool(info) failed: ", status);
    return status;
  }

  status = file->GetInfo(file, (EFI_GUID *)&EFI_FILE_INFO_GUID, &info_size,
                         info);
  if (status != EFI_SUCCESS) {
    print_status(st, u"GetInfo failed: ", status);
    return status;
  }

  UINTN file_size = (UINTN)info->FileSize;
  status = st->BootServices->AllocatePool(EFI_MEMORY_TYPE_LOADER_DATA, file_size,
                                          out_buf);
  if (status != EFI_SUCCESS) {
    print_status(st, u"AllocatePool(file) failed: ", status);
    return status;
  }

  UINTN read_size = file_size;
  status = file->Read(file, &read_size, *out_buf);
  if (status != EFI_SUCCESS || read_size != file_size) {
    print_status(st, u"Read failed: ", status);
    return EFI_DEVICE_ERROR;
  }

  *out_size = file_size;
  file->Close(file);
  if (root) {
    root->Close(root);
  }
  st->BootServices->FreePool(info);
  return EFI_SUCCESS;
}

extern void jump_to_kernel(void *entry, void *stack_top, struct BootInfo *info);
extern const unsigned char kernel_blob[];
extern const unsigned int kernel_blob_len;

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *st) {
  st->BootServices->SetWatchdogTimer(0, 0, 0, NULL);

  print16(st, u"UEFI bootloader: loading kernel...\r\n");

  void *kernel_buf = NULL;
  UINTN kernel_size = 0;
  EFI_STATUS status = read_file(st, image, u"EFI\\BOOT\\KERNEL.BIN",
                                &kernel_buf, &kernel_size);
  if (status != EFI_SUCCESS) {
    print16(st, u"Failed to read kernel file, using embedded kernel\r\n");
    kernel_buf = (void *)kernel_blob;
    kernel_size = (UINTN)kernel_blob_len;
  }

  UINTN pages = (kernel_size + 0xFFF) / 0x1000;
  UINT64 kernel_addr = 0x100000;
  status = st->BootServices->AllocatePages(EFI_ALLOCATE_ADDRESS,
                                           EFI_MEMORY_TYPE_LOADER_DATA, pages,
                                           &kernel_addr);
  if (status != EFI_SUCCESS) {
    print16(st, u"Failed to allocate kernel pages at 0x100000\r\n");
    return status;
  }

  // Copy kernel to its load address
  UINT8 *dst = (UINT8 *)(UINTN)kernel_addr;
  UINT8 *src = (UINT8 *)kernel_buf;
  for (UINTN i = 0; i < kernel_size; ++i) {
    dst[i] = src[i];
  }

  // Get GOP framebuffer
  EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
  status = st->BootServices->LocateProtocol(
      (EFI_GUID *)&EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID, NULL, (void **)&gop);
  if (status != EFI_SUCCESS) {
    print16(st, u"Failed to locate GOP\r\n");
    return status;
  }

  struct BootInfo info;
  info.fb.base = (UINT8 *)(UINTN)gop->Mode->FrameBufferBase;
  info.fb.size = (UINTN)gop->Mode->FrameBufferSize;
  info.fb.width = gop->Mode->Info->HorizontalResolution;
  info.fb.height = gop->Mode->Info->VerticalResolution;
  info.fb.pixels_per_scanline = gop->Mode->Info->PixelsPerScanLine;
  info.fb.pixel_format = gop->Mode->Info->PixelFormat;

  // Memory map for ExitBootServices
  UINTN map_size = 0;
  UINTN map_key = 0;
  UINTN desc_size = 0;
  UINT32 desc_ver = 0;
  st->BootServices->GetMemoryMap(&map_size, NULL, &map_key, &desc_size,
                                &desc_ver);
  map_size += desc_size * 8;

  void *map = NULL;
  status = st->BootServices->AllocatePool(EFI_MEMORY_TYPE_LOADER_DATA, map_size,
                                          &map);
  if (status != EFI_SUCCESS) {
    print16(st, u"Failed to allocate memory map\r\n");
    return status;
  }

  // Allocate a dedicated stack for the kernel before exiting boot services
  UINT64 stack_pages = 8;
  UINT64 stack_addr = 0x80000;
  status = st->BootServices->AllocatePages(EFI_ALLOCATE_ADDRESS,
                                           EFI_MEMORY_TYPE_LOADER_DATA,
                                           stack_pages, &stack_addr);
  if (status != EFI_SUCCESS) {
    print16(st, u"Failed to allocate kernel stack\r\n");
    return status;
  }
  void *stack_top = (void *)(UINTN)(stack_addr + stack_pages * 0x1000);

  status = st->BootServices->GetMemoryMap(&map_size, map, &map_key, &desc_size,
                                          &desc_ver);
  if (status != EFI_SUCCESS) {
    print16(st, u"Failed to get memory map\r\n");
    return status;
  }

  status = st->BootServices->ExitBootServices(image, map_key);
  if (status != EFI_SUCCESS) {
    print16(st, u"ExitBootServices failed\r\n");
    return status;
  }

  jump_to_kernel((void *)(UINTN)kernel_addr, stack_top, &info);

  return EFI_SUCCESS;
}

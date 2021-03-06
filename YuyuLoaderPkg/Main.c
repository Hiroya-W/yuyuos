#include <Guid/FileInfo.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DiskIo2.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Uefi.h>
#include "elf.hpp"
#include "frame_buffer_config.hpp"

struct MemoryMap {
    UINTN buffer_size;
    VOID* buffer;
    UINTN map_size;
    UINTN map_key;
    UINTN descriptor_size;
    UINT32 descriptor_version;
};

EFI_STATUS GetMemoryMap(struct MemoryMap* map) {
    if (map->buffer == NULL) {
        return EFI_BUFFER_TOO_SMALL;
    }

    map->map_size = map->buffer_size;
    // OSを起動するために必要な機能を提供するブートサービス: gBS
    // 関数呼び出し時点でのメモリマップを取得する
    /* map_size            IN: メモリマップ書き込み用のメモリ領域の大きさ
     *                     OUT: 実際のメモリマップの大きさ
     * map_buffer          IN: メモリマップ書き込み用のメモリ領域の先頭ポインタ
     *                     OUT: メモリマップが書き込まれる
     * map_key             OUT: メモリマップを識別するための値を書き込む変数
     * descriptor_size     OUT: メモリマップの個々の行を表すメモリディスクリプタのバイト数
     * descriptor_version: OUT: メモリディスクリプタの構造体のバージョン番号
     */
    return gBS->GetMemoryMap(&map->map_size,
        (EFI_MEMORY_DESCRIPTOR*)map->buffer,
        &map->map_key,
        &map->descriptor_size,
        &map->descriptor_version);
}

// メモリディスクリプタのタイプ値からタイプ名を取得して返す関数
const CHAR16* GetMemoryTypeUnicode(EFI_MEMORY_TYPE type) {
    switch (type) {
        case EfiReservedMemoryType: return L"EfiReservedMemoryType";
        case EfiLoaderCode: return L"EfiLoaderCode";
        case EfiLoaderData: return L"EfiLoaderData";
        case EfiBootServicesCode: return L"EfiBootServicesCode";
        case EfiBootServicesData: return L"EfiBootServicesData";
        case EfiRuntimeServicesCode: return L"EfiRuntimeServicesCode";
        case EfiRuntimeServicesData: return L"EfiRuntimeServicesData";
        case EfiConventionalMemory: return L"EfiConventionalMemory";
        case EfiUnusableMemory: return L"EfiUnusableMemory";
        case EfiACPIReclaimMemory: return L"EfiACPIReclaimMemory";
        case EfiACPIMemoryNVS: return L"EfiACPIMemoryNVS";
        case EfiMemoryMappedIO: return L"EfiMemoryMappedIO";
        case EfiMemoryMappedIOPortSpace: return L"EfiMemoryMappedIOPortSpace";
        case EfiPalCode: return L"EfiPalCode";
        case EfiPersistentMemory: return L"EfiPersistentMemory";
        case EfiMaxMemoryType: return L"EfiMaxMemoryType";
        default: return L"InvalidMemoryType";
    }
}

// 引数で与えられたメモリマップ情報をCSV形式でファイルに書き出す関数
EFI_STATUS SaveMemoryMap(struct MemoryMap* map, EFI_FILE_PROTOCOL* file) {
    EFI_STATUS status;
    CHAR8 buf[256];
    UINTN len;

    CHAR8* header = "Index, Type, Type(name), PhysicalStart, NumberOfPages, Attribute\n";
    len = AsciiStrLen(header);
    // ヘッダ行を出力する
    status = file->Write(file, &len, header);
    if (EFI_ERROR(status)) {
        return status;
    }

    Print(L"map->buffer = %08lx, map->map_size = %08lx\n", map->buffer, map->map_size);

    EFI_PHYSICAL_ADDRESS iter;
    int i;
    // メモリマップの各行をカンマ区切りで出力
    for (iter = (EFI_PHYSICAL_ADDRESS)map->buffer, i = 0;
         iter < (EFI_PHYSICAL_ADDRESS)map->buffer + map->map_size;
         iter += map->descriptor_size, i++)
    {
        EFI_MEMORY_DESCRIPTOR* desc = (EFI_MEMORY_DESCRIPTOR*)iter;
        // メモリディスクリプタの値を文字列変換する
        len = AsciiSPrint(buf,    // 整形した文字列がbufに書き込まれる
            sizeof(buf),
            "%u, %x, %-ls, %08lx, %lx, %lx\n",
            i,
            desc->Type,
            GetMemoryTypeUnicode(desc->Type),    // メモリディスクリプタのタイプ値からタイプ名を取得
            desc->PhysicalStart,
            desc->NumberOfPages,
            desc->Attribute & 0xffffflu);
        // ファイルに文字列を書き出す
        // len IN: 文字列のバイト数
        //     OUT: 実際にファイルに書き出されたバイト数
        status = file->Write(file, &len, buf);
        if (EFI_ERROR(status)) {
            return status;
        }
    }
    return EFI_SUCCESS;
}

// 書き込み先のファイルを開く
EFI_STATUS OpenRootDir(EFI_HANDLE image_handle, EFI_FILE_PROTOCOL** root) {
    EFI_STATUS status;
    EFI_LOADED_IMAGE_PROTOCOL* loaded_image;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* fs;

    status = gBS->OpenProtocol(image_handle,
        &gEfiLoadedImageProtocolGuid,
        (VOID**)&loaded_image,
        image_handle,
        NULL,
        EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
    if (EFI_ERROR(status)) {
        return status;
    }

    status = gBS->OpenProtocol(loaded_image->DeviceHandle,
        &gEfiSimpleFileSystemProtocolGuid,
        (VOID**)&fs,
        image_handle,
        NULL,
        EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
    if (EFI_ERROR(status)) {
        return status;
    }

    return fs->OpenVolume(fs, root);
}

EFI_STATUS OpenGOP(EFI_HANDLE image_handle, EFI_GRAPHICS_OUTPUT_PROTOCOL** gop) {
    EFI_STATUS status;
    UINTN num_gop_handles = 0;
    EFI_HANDLE* gop_handles = NULL;
    status = gBS->LocateHandleBuffer(ByProtocol,
        &gEfiGraphicsOutputProtocolGuid,
        NULL,
        &num_gop_handles,
        &gop_handles);
    if (EFI_ERROR(status)) {
        return status;
    }

    // プロトコルを用いてgopに値をセットする
    status = gBS->OpenProtocol(gop_handles[0],
        &gEfiGraphicsOutputProtocolGuid,
        (VOID**)gop,
        image_handle,
        NULL,
        EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
    if (EFI_ERROR(status)) {
        return status;
    }

    FreePool(gop_handles);

    return EFI_SUCCESS;
}

const CHAR16* GetPixelFormatUnicode(EFI_GRAPHICS_PIXEL_FORMAT fmt) {
    switch (fmt) {
        case PixelRedGreenBlueReserved8BitPerColor: return L"PixelRedGeenBlueReserved8BitPerColor";
        case PixelBlueGreenRedReserved8BitPerColor: return L"PixelBlueGreenRedReserved8BitPerColor";
        case PixelBitMask: return L"PixelBitMask";
        case PixelBltOnly: return L"PixelBltOnly";
        case PixelFormatMax: return L"PixelFormatMax";
        default: return L"InvalidPixelFormat";
    }
}

// プログラムの実行を無限ループで止めるようにする
void Halt(void) {
    while (1)
        __asm__("hlt");
}

void CalcLoadAddressRange(Elf64_Ehdr* ehdr, UINT64* first, UINT64* last) {
    // プログラムヘッダの配列を指すポインタ
    Elf64_Phdr* phdr = (Elf64_Phdr*)((UINT64)ehdr + ehdr->e_phoff);
    *first = MAX_UINT64;
    *last = 0;
    // カーネルファイル内の全てのLOADセグメントを順に辿り、アドレス範囲を更新する
    for (Elf64_Half i = 0; i < ehdr->e_phnum; i++) {
        // LOADセグメントのときのみ更新する
        if (phdr[i].p_type != PT_LOAD) continue;
        *first = MIN(*first, phdr[i].p_vaddr);
        *last = MAX(*last, phdr[i].p_vaddr + phdr[i].p_memsz);
    }
}

// 一時領域から最終目的地へLOADセグメントをコピーする
void CopyLoadSegments(Elf64_Ehdr* ehdr) {
    Elf64_Phdr* phdr = (Elf64_Phdr*)((UINT64)ehdr + ehdr->e_phoff);
    for (Elf64_Half i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) continue;

        UINT64 segm_in_file = (UINT64)ehdr + phdr[i].p_offset;
        // segm_in_fileが指す一時領域からp_vaddrが指す最終目的地へデータをコピーする
        CopyMem((VOID*)phdr[i].p_vaddr, (VOID*)segm_in_file, phdr[i].p_filesz);

        UINTN remain_bytes = phdr[i].p_memsz - phdr[i].p_filesz;
        // セグメントのメモリ上のサイズがファイル上のサイズより大きい場合、残りを0-paddingする
        SetMem((VOID*)(phdr[i].p_vaddr + phdr[i].p_filesz), remain_bytes, 0);
    }
}

EFI_STATUS EFIAPI UefiMain(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE* system_table) {
    EFI_STATUS status;

    Print(L"Hello, Yuyu World!\n");

    CHAR8 memmap_buf[4096 * 4];    // メモリマップ用に16KiB確保しておく
    struct MemoryMap memmap = {sizeof(memmap_buf), memmap_buf, 0, 0, 0, 0};
    status = GetMemoryMap(&memmap);
    if (EFI_ERROR(status)) {
        Print(L"failed to get memory map: %r\n", status);
        Halt();
    }

    EFI_FILE_PROTOCOL* root_dir;
    status = OpenRootDir(image_handle, &root_dir);
    if (EFI_ERROR(status)) {
        Print(L"failed to open root directory: %r\n", status);
        Halt();
    }

    EFI_FILE_PROTOCOL* memmap_file;
    // memmapというファイルを書き込みモードで開く
    status = root_dir->Open(root_dir,
        &memmap_file,
        L"\\memmap",
        EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
        0);
    if (EFI_ERROR(status)) {
        Print(L"failed to open file '\\memmap': %r\n", status);
        Print(L"Ignored.\n");
    } else {
        // 取得したメモリマップをmemmapに保存する
        status = SaveMemoryMap(&memmap, memmap_file);
        if (EFI_ERROR(status)) {
            Print(L"failed to save memory map: %r\n", status);
            Halt();
        }
        status = memmap_file->Close(memmap_file);
        if (EFI_ERROR(status)) {
            Print(L"failed to close memory map: %r\n", status);
            Halt();
        }
    }

    /*
     * ブートローダからピクセルを描く
     */
    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop;
    status = OpenGOP(image_handle, &gop);    // gopに値をセットする
    if (EFI_ERROR(status)) {
        Print(L"failed to open GOP: %r\n", status);
        Halt();
    }

    Print(L"Resolution: %ux%u, Pixel Format: %s, %u pixels/line\n",
        gop->Mode->Info->HorizontalResolution,
        gop->Mode->Info->VerticalResolution,
        GetPixelFormatUnicode(gop->Mode->Info->PixelFormat),
        gop->Mode->Info->PixelsPerScanLine);

    Print(L"Frame Buffer: 0x%0lx - 0x%0lx, Size: %lu bytes\n",
        gop->Mode->FrameBufferBase,    // フレームバッファの先頭アドレス
        gop->Mode->FrameBufferBase + gop->Mode->FrameBufferSize,
        gop->Mode->FrameBufferSize    // 全体サイズ
    );

    UINT8* frame_buffer = (UINT8*)gop->Mode->FrameBufferBase;
    for (UINTN i = 0; i < gop->Mode->FrameBufferSize; i++) {
        frame_buffer[i] = 255;    // 0xff にすると白になる
    }

    /*
     * カーネルを読み込む
     */
    EFI_FILE_PROTOCOL* kernel_file;
    status = root_dir->Open(root_dir, &kernel_file, L"\\kernel.elf", EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status)) {
        Print(L"failed to open file '\\kernel.elf': %r\n", status);
        Halt();
    }

    // kernel.elf の12文字(塗る文字を含む)分 = sizeof(CHAR16) * 12だけ余分に確保する
    UINTN file_info_size = sizeof(EFI_FILE_INFO) + sizeof(CHAR16) * 12;
    UINT8 file_info_buffer[file_info_size];
    // ファイル情報を取得する
    status
        = kernel_file->GetInfo(kernel_file, &gEfiFileInfoGuid, &file_info_size, file_info_buffer);
    if (EFI_ERROR(status)) {
        Print(L"failed to get file information: %r\n", status);
        Halt();
    }

    // UINT8からEFI_FILE_INFOにキャストして、FileSizeを取り出す
    EFI_FILE_INFO* file_info = (EFI_FILE_INFO*)file_info_buffer;
    UINTN kernel_file_size = file_info->FileSize;    // バイト単位

    VOID* kernel_buffer;
    status = gBS->AllocatePool(EfiLoaderData,    // 確保するメモリ領域の種別
        kernel_file_size,    // 確保するファイルサイズ(バイト単位)
        &kernel_buffer       // 確保したメモリ領域のアドレス
    );
    if (EFI_ERROR(status)) {
        Print(L"failed to allocate pool: %r\n", status);
        Halt();
    }

    // ファイルを読み込む
    status = kernel_file->Read(kernel_file, &kernel_file_size, kernel_buffer);
    if (EFI_ERROR(status)) {
        Print(L"error: %r", status);
        Halt();
    }

    Elf64_Ehdr* kernel_ehdr = (Elf64_Ehdr*)kernel_buffer;
    UINT64 kernel_first_addr, kernel_last_addr;
    // 最終目的地の番地の範囲 = 0x100000から始まるアドレスの範囲を取得する
    CalcLoadAddressRange(kernel_ehdr, &kernel_first_addr, &kernel_last_addr);

    /**
     * kernel_first_addr、kernel_last_addrからページ数を計算する
     * ページ単位の大きさ(1ページ4KiB = 0x1000)
     * 0xfffは切り上げ
     **/
    UINTN num_pages = (kernel_last_addr - kernel_first_addr + 0xfff) / 0x1000;
    status = gBS->AllocatePages(AllocateAddress,    // メモリの確保の仕方
        EfiLoaderData,                              // 確保するメモリ領域の種別
        num_pages,                                  // 確保するページ数
        &kernel_first_addr                          // 確保したメモリ領域のアドレス
    );
    if (EFI_ERROR(status)) {
        Print(L"failed to allocate pages: %r\n", status);
        Halt();
    }

    CopyLoadSegments(kernel_ehdr);
    Print(L"Kernel: 0x%0lx - 0x%0lx\n", kernel_first_addr, kernel_last_addr);

    // 一時領域を解放する
    status = gBS->FreePool(kernel_buffer);
    if (EFI_ERROR(status)) {
        Print(L"failed to free pool: %r\n", status);
        Halt();
    }

    // ブートサービスを停止する
    status = gBS->ExitBootServices(image_handle, memmap.map_key);
    // map_keyが最新ではない時、エラーになる
    if (EFI_ERROR(status)) {
        // 再取得する
        status = GetMemoryMap(&memmap);
        if (EFI_ERROR(status)) {
            Print(L"failed to get memory map: %r\n", status);
            Halt();
        }
        status = gBS->ExitBootServices(image_handle, memmap.map_key);
        if (EFI_ERROR(status)) {
            Print(L"Could not exit boot service: %r\n", status);
            Halt();
        }
    }

    // 64bit ELFにおいて、KernelMain()の実態が置かれているアドレス
    UINT64 entry_addr = *(UINT64*)(kernel_first_addr + 24);

    struct FrameBufferConfig config = {(UINT8*)gop->Mode->FrameBufferBase,
        gop->Mode->Info->PixelsPerScanLine,
        gop->Mode->Info->HorizontalResolution,
        gop->Mode->Info->VerticalResolution,
        0};

    switch (gop->Mode->Info->PixelFormat) {
        case PixelRedGreenBlueReserved8BitPerColor:
            config.pixel_format = kPixelRGBResv8BitPerColor;
            break;
        case PixelBlueGreenRedReserved8BitPerColor:
            config.pixel_format = kPixelBGRResv8BitPerColor;
            break;
        default: Print(L"Unimplemented pixel format: %d\n", gop->Mode->Info->PixelFormat); Halt();
    }

    // 引数と戻り値をvoid型とする関数型をEntryPointTypeとしてエイリアスする
    typedef void EntryPointType(const struct FrameBufferConfig*);
    EntryPointType* entry_point = (EntryPointType*)entry_addr;
    // カーネルを起動する
    entry_point(&config);

    Print(L"All done\n");

    while (1)
        ;
    return EFI_SUCCESS;
}
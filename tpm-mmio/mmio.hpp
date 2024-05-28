//                               _                 
//   ___  _   _  _ __    ___    | |_   ___   _ __  
//  / __|| | | || '_ \  / __|   | __| / _ \ | '_ \ 
//  \__ \| |_| || | | || (__  _ | |_ | (_) || |_) |
//  |___/ \__, ||_| |_| \___|(_) \__| \___/ | .__/ 
//        |___/                             |_|    
//
// © Copyright 2021-2024 sync.top. All rights reserved.

#pragma once

namespace mmio
{
    //
    // Maps a given MMIO (Memory-Mapped I/O) physical address to a virtual address 
    // and writes the specified bytes to the mapped memory.
    //
    // Parameters:
    // - physicalAddress: The physical address in MMIO space that needs to be mapped 
    //                    and written to.
    // - len: The number of bytes to write to the mapped memory.
    // - pData: A pointer to the data to be written to the mapped memory.
    //
    // Returns:
    // - true: If the operation is successful.
    // - false: If the operation fails.
    //
    bool Write(
        _In_ uintptr_t physicalAddress,    
        _In_ uint32_t len,                 
        _In_reads_bytes_(len) PVOID pData  
    )
    {
        PHYSICAL_ADDRESS physAddress;
        RtlZeroMemory(&physAddress, sizeof(PHYSICAL_ADDRESS));        
        physAddress.QuadPart = (LONGLONG)physicalAddress;
        PVOID virtualAddress = MmMapIoSpace(physAddress, len, MmNonCached);
        if (virtualAddress)
        {
            switch (len)
            {
            case 1:
                WRITE_REGISTER_BUFFER_UCHAR((volatile UCHAR*)(virtualAddress), (PUCHAR)pData, 1);
                break;
            case 2:
                WRITE_REGISTER_BUFFER_USHORT((volatile USHORT*)(virtualAddress), (PUSHORT)pData, 1);
                break;
            case 4:
                WRITE_REGISTER_BUFFER_ULONG((volatile ULONG*)(virtualAddress), (PULONG)pData, 1);
                break;
            case 8:
                WRITE_REGISTER_BUFFER_ULONG((volatile ULONG*)(virtualAddress), (PULONG)pData, 2);
                break;
            }
            MmUnmapIoSpace(virtualAddress, len);
            return true;
        }
        DbgError("Failed to map physical address to virtual address. (%s)\n", __FUNCTION__);
        return false;
    }

    //
    // Maps a given MMIO (Memory-Mapped I/O) physical address to a virtual address
    // and reads the specified number of bytes from this mapped memory into the provided buffer.
    //
    // Parameters:
    // - physicalAddress (in): The physical address in MMIO space that needs to be mapped
    //                          and read from.
    // - len (in): The number of bytes to read from the mapped memory.
    // - pData (out): A pointer to the buffer where the read data will be stored.
    //
    // Returns:
    // - true: If the operation is successful.
    // - false: If the operation fails.
    //
    bool Read(
        _In_ uintptr_t physicalAddress,     
        _In_ uint32_t len,                  
        _Out_writes_bytes_(len) PVOID pData 
    )
    {
        PHYSICAL_ADDRESS physAddress;
        RtlZeroMemory(&physAddress, sizeof(PHYSICAL_ADDRESS));
        physAddress.QuadPart = (LONGLONG)physicalAddress;
        PVOID virtualAddress = MmMapIoSpace(physAddress, len, MmNonCached);
        if (virtualAddress)
        {
            switch (len)
            {
            case 1:
                READ_REGISTER_BUFFER_UCHAR((volatile UCHAR*)(virtualAddress), (PUCHAR)pData, 1);
                break;
            case 2:
                READ_REGISTER_BUFFER_USHORT((volatile USHORT*)(virtualAddress), (PUSHORT)pData, 1);
                break;
            case 4:
                READ_REGISTER_BUFFER_ULONG((volatile ULONG*)(virtualAddress), (PULONG)pData, 1);
                break;
            case 8:
                READ_REGISTER_BUFFER_ULONG((volatile ULONG*)(virtualAddress), (PULONG)pData, 2);
                break;
            }
            MmUnmapIoSpace(virtualAddress, len);
            return true;
        }
        DbgError("Failed to map physical address to virtual address. (%s)\n", __FUNCTION__);
        return false;
    }
};
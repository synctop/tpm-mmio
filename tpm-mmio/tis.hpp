//                               _                 
//   ___  _   _  _ __    ___    | |_   ___   _ __  
//  / __|| | | || '_ \  / __|   | __| / _ \ | '_ \ 
//  \__ \| |_| || | | || (__  _ | |_ | (_) || |_) |
//  |___/ \__, ||_| |_| \___|(_) \__| \___/ | .__/ 
//        |___/                             |_|    
//
// © Copyright 2021-2024 sync.top. All rights reserved.

#pragma once

class TpmTis
{
private:
	
    //
    // Polls an 8-bit hardware register at the specified address, waiting for specified bits to be set and/or cleared within a timeout period.
    //
    // Parameters:
    // - registerAddress: Pointer to the hardware register to be polled.
    // - bitSet: Bitmask specifying the bits that must be set in the register.
    // - bitClear: Bitmask specifying the bits that must be clear in the register.
    // - timeOut: Maximum time in milliseconds to wait for the conditions to be met.
    //
    // Returns:
    // - STATUS_SUCCESS: The conditions were met within the timeout period.
    // - STATUS_TIMEOUT: The conditions were not met within the timeout period.
    //
    NTSTATUS TisWaitRegisterBits(
        _In_ uint8_t* registerAddress,
        _In_ uint8_t  bitSet,
        _In_ uint8_t  bitClear,
        _In_ uint32_t  timeOut
    )
    {
        for (uint32_t waitTime = 0; waitTime < timeOut; waitTime += 30)
        {
            uint8_t registerRead = 0;
            if (mmio::Read((uintptr_t)registerAddress, sizeof(uint8_t), &registerRead))
            {
                if (((registerRead & bitSet) == bitSet) && ((registerRead & bitClear) == 0))
                {
                    return STATUS_SUCCESS;
                }
                KeStallExecutionProcessor(30);
            }
        }
        return STATUS_TIMEOUT;
    }

    //
    // Prepares the TPM Interface Specification (TIS) device for a command by setting the READY bit
    // in the TIS PC (Platform Control) registers and waits for the device to be ready.
    //
    // This function writes to the Status register of the TIS PC registers to indicate readiness,
    // and then polls the Status register until the READY bit is set, indicating that the device
    // is ready to accept a command or has completed the previous command.
    //
    // Parameters:
    // - tisReg: Pointer to the TIS PC registers structure. This structure contains all the
    //           necessary register mappings needed to interface with the TPM device.
    //
    // Returns:
    // - STATUS_INVALID_PARAMETER: The input pointer 'tisReg' is nullptr, indicating an invalid
    //   parameter was passed.
    // - STATUS_SUCCESS: The TPM device is successfully prepared and ready.
    // - STATUS_TIMEOUT: The conditions were not met within the timeout period.
    //
    NTSTATUS TisPrepareCommand(
        _In_ TIS_PC_REGISTERS* tisReg
    )
    {
        NTSTATUS status = STATUS_INVALID_PARAMETER;
        if (tisReg != nullptr)
        {
            uint8_t bit = TIS_PC_STS_READY;
            (void)mmio::Write((uintptr_t)&tisReg->Status, sizeof(uint8_t), &bit);
            status = this->TisWaitRegisterBits(
                &tisReg->Status,
                TIS_PC_STS_READY,
                0,
                TIS_TIMEOUT_B
            );
        }
        return status;
    }

    
    //
    // Reads the burst count from the TPM Interface Specification (TIS) device.
    //
    // The burst count indicates the number of bytes the TPM can transfer in one burst. 
    // Since the burst count register is not 2-byte aligned, it needs to be read in two separate 
    // 1-byte reads. This function will poll the burst count register until a non-zero value is read 
    // or a timeout occurs.
    //
    // Parameters:
    // - tisReg: Pointer to the TIS PC registers structure. This structure contains the register mappings 
    //           needed to interface with the TPM device.
    // - burstCount: Pointer to a variable that will receive the burst count value read from the TPM.
    //
    // Returns:
    // - STATUS_SUCCESS: A non-zero burst count is successfully read.
    // - STATUS_TIMEOUT: The burst count remains zero after the timeout period.
    // - STATUS_INVALID_PARAMETER: Either 'tisReg' or 'burstCount' is a nullptr.
    //
    NTSTATUS TisReadBurstCount(
        _In_ TIS_PC_REGISTERS* tisReg,
        _Out_ uint16_t* burstCount
    )
    {
        if (burstCount != nullptr && tisReg != nullptr)
        {
            uint32_t waitTime = 0;
            do
            {
                //
                // The burst count register is a 16-bit value, but it is not 2-byte aligned.
                // Therefore, it needs to be read in two separate 1-byte reads.
                //
                uint8_t dataByte0 = 0;
                (void)mmio::Read((uintptr_t)&tisReg->BurstCount, sizeof(uint8_t), &dataByte0);
                uint8_t dataByte1 = 0;
                (void)mmio::Read((uintptr_t)&tisReg->BurstCount + 1, sizeof(uint8_t), &dataByte1);

                *burstCount = (uint16_t)((dataByte1 << 8) + dataByte0);
                if (*burstCount != 0)
                {
                    return STATUS_SUCCESS;
                }

                KeStallExecutionProcessor(30);
                waitTime += 30;
            } while (waitTime < TIS_TIMEOUT_D);

            return STATUS_TIMEOUT;
        }
        return STATUS_INVALID_PARAMETER;
    }

public:

    //
    // Sends a command to the TPM using the TPM Interface Specification (TIS) device and retrieves the response.
    //
    // This function writes a command buffer to the TPM, issues the command, and then reads the response
    // from the TPM into the provided output buffer. The function ensures that the TPM is ready to receive
    // a command before writing, and waits for the TPM to become ready after the command has been issued.
    //
    // Parameters:
    // - crbReg: Pointer to the CRB registers used to initiate commands and read responses.
    // - bufferIn: Pointer to the buffer containing the command data to be sent to the TPM.
    // - sizeIn: Size of the input buffer in bytes.
    // - bufferOut: Pointer to the buffer where the TPM's response will be stored.
    // - sizeOut: Pointer to a variable that on input specifies the maximum size of the output buffer,
    //            and on output reflects the actual size of the data written to the output buffer.
    //
    // Returns:
    // - STATUS_SUCCESS: The command was successfully sent and a response was received.
    // - STATUS_DEVICE_BUSY: The device is busy or in idle mode.
    // - STATUS_NOT_SUPPORTED: The command or TPM version aren't supported.
    // - STATUS_BUFFER_TOO_SMALL: The response is too small.
    //
    NTSTATUS TisCommand(
        _In_ TIS_PC_REGISTERS* tisReg,
        _In_reads_bytes_(sizeIn) const uint8_t* bufferIn,
        _In_ uint32_t sizeIn,
        _Inout_updates_bytes_(*sizeOut) uint8_t* bufferOut,
        _Inout_ uint32_t* sizeOut
    )
    {
        NTSTATUS status = this->TisPrepareCommand(tisReg);
        if (NT_ERROR(status))
        {
            DbgError("Tpm2 is not ready for a command.\n");
            return STATUS_DEVICE_BUSY;
        }

        //
        // Send the command data to Tpm
        //
        uint32_t i = 0;
        uint16_t burstCount = 0;
        while (i < sizeIn)
        {
            status = this->TisReadBurstCount(tisReg, &burstCount);
            if (NT_ERROR(status))
            {
                status = STATUS_DEVICE_BUSY;
                goto Exit;
            }

            for (; burstCount > 0 && i < sizeIn; burstCount--)
            {
                (void)mmio::Write((uintptr_t)&tisReg->DataFifo, sizeof(uint8_t), const_cast<uint8_t*>(bufferIn + i));
                i++;
            }
        }

        //
        // Check the Tpm status STS_EXPECT change from 1 to 0
        //
        status = this->TisWaitRegisterBits(
            &tisReg->Status,
            (uint8_t)TIS_PC_VALID,
            TIS_PC_STS_EXPECT,
            TIS_TIMEOUT_C
        );

        if (NT_ERROR(status))
        {
            DbgError("The send buffer is too small for a command.\n");
            status = STATUS_BUFFER_TOO_SMALL;
            goto Exit;
        }

        //
        // Executed the TPM command and waiting for the response data ready
        //
        uint8_t bit = TIS_PC_STS_GO;
        (void)mmio::Write((uintptr_t)&tisReg->Status, sizeof(uint8_t), &bit);

        //
        // NOTE: That may take many seconds to minutes for certain commands, such as key generation.
        //
        status = this->TisWaitRegisterBits(
            &tisReg->Status,
            (uint8_t)(TIS_PC_VALID | TIS_PC_STS_DATA),
            0,
            TIS_TIMEOUT_MAX
        );

        if (NT_ERROR(status))
        {
            //
            // dataAvail check timeout. Cancel the currently executing command by writing commandCancel,
            // Expect TPM_RC_CANCELLED or successfully completed response.
            //
            DbgError("Timed out while waiting for TPM. Trying to cancel the command.\n");

            uint32_t bit32 = TIS_PC_STS_CANCEL;
            (void)mmio::Write((uintptr_t)&tisReg->Status, sizeof(uint32_t), &bit32);
            status = this->TisWaitRegisterBits(
                &tisReg->Status,
                (uint8_t)(TIS_PC_VALID | TIS_PC_STS_DATA),
                0,
                TIS_TIMEOUT_B
            );
            //
            // Do not clear CANCEL bit here because Writes of 0 to this bit are ignored
            //
            if (NT_ERROR(status))
            {
                //
                // Cancel executing command fail to get any response
                // Try to abort the command with write of a 1 to commandReady in Command Execution state
                //
                status = STATUS_DEVICE_BUSY;
                goto Exit;
            }
        }

        //
        // Get response data header
        //
        i = 0;
        burstCount = 0;
        while (i < sizeof(TPM2_RESPONSE_HEADER))
        {
            status = this->TisReadBurstCount(tisReg, &burstCount);
            if (NT_ERROR(status)) {
                status = STATUS_DEVICE_BUSY;
                goto Exit;
            }

            for (; burstCount > 0; burstCount--)
            {
                (void)mmio::Read((uintptr_t)&tisReg->DataFifo, sizeof(uint8_t), (bufferOut + i));
                i++;
                if (i == sizeof(TPM2_RESPONSE_HEADER))
                {
                    break;
                }
            }
        }
    
        //
        // Check the response data header (tag,parasize and returncode )
        //
        uint16_t data16 = 0;
        memcpy(&data16, bufferOut, sizeof(uint16_t));
        // TPM2 should not use this RSP_COMMAND
        if (_byteswap_ushort(data16) == TPM_ST_RSP_COMMAND)
        {
            Dbg("TPM_ST_RSP error - %x.\n", TPM_ST_RSP_COMMAND);
            status = STATUS_NOT_SUPPORTED;
            goto Exit;
        }

        uint32_t data32 = 0;
        memcpy(&data32, (bufferOut + 2), sizeof(uint32_t));
        uint32_t tpmOutSize = _byteswap_ulong(data32);
        if (*sizeOut < tpmOutSize)
        {
            status = STATUS_BUFFER_TOO_SMALL;
            goto Exit;
        }

        *sizeOut = tpmOutSize;
        //
        // Continue reading the remaining data
        //
        while (i < tpmOutSize)
        {
            for (; burstCount > 0; burstCount--)
            {
                (void)mmio::Read((uintptr_t)&tisReg->DataFifo, sizeof(uint8_t), (bufferOut + i));
                i++;
                if (i == tpmOutSize)
                {
                    status = STATUS_SUCCESS;
                    goto Exit;
                }
            }

            status = this->TisReadBurstCount(tisReg, &burstCount);
            if (NT_ERROR(status))
            {
                status = STATUS_DEVICE_BUSY;
                goto Exit;
            }
        }

    Exit:

        bit = TIS_PC_STS_READY;
        (void)mmio::Write((uintptr_t)&tisReg->Status, sizeof(uint8_t), &bit);
        return status;
    }


};
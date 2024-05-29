

#pragma once

class TpmCrb 
{
private:

    TpmPtp* ptpInterface = nullptr;

   //
   // Polls a 32-bit hardware register at the specified address, waiting for specified bits to be set and/or cleared within a timeout period.
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
   NTSTATUS CrbWaitRegisterBits(
        _In_ uint32_t* registerAddress,
        _In_ uint32_t  bitSet,
        _In_ uint32_t  bitClear,
        _In_ uint32_t  timeOut
    )
    {
        for (uint32_t waitTime = 0; waitTime < timeOut; waitTime += 30)
        {
            uint32_t registerRead = 0;
            if (mmio::Read((uintptr_t)registerAddress, sizeof(uint32_t), &registerRead))
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

public:

    TpmCrb(TpmPtp* ptpInterface)
    {
        this->ptpInterface = ptpInterface;
    }

    //
    // Sends a command to the TPM CRB (Command Response Buffer) interface and retrieves the response.
    // This function is designed to interact with the TPM hardware via the CRB interface,
    // sending a command buffer and awaiting a response.
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
    NTSTATUS CrbCommand(
        _In_ PTP_CRB_REGISTERS* crbReg,
        _In_reads_bytes_(sizeIn) const uint8_t* bufferIn,
        _In_ uint32_t sizeIn,
        _Inout_updates_bytes_(*sizeOut) uint8_t* bufferOut,
        _Inout_ uint32_t* sizeOut
    )
    {
        NTSTATUS status = STATUS_UNSUCCESSFUL;
        uint8_t retryCnt = 0;
        uint32_t bit = 0;

        while (true)
        {
            //
            // STEP 0:
            // if idleByPassState == 0, enforce Idle state before sending command
            //
            if (this->ptpInterface->idleByPassState == 0)
            {
                if (mmio::Read((uintptr_t)&crbReg->CrbControlStatus, sizeof(uint32_t), &bit))
                {
                    // Check if TPM is not idle
                    if ((bit & PTP_CRB_CONTROL_AREA_STATUS_TPM_IDLE) == 0)
                    {
                        status = this->CrbWaitRegisterBits(
                            &crbReg->CrbControlStatus,
                            PTP_CRB_CONTROL_AREA_STATUS_TPM_IDLE,
                            0,
                            PTP_TIMEOUT_C
                        );

                        if (NT_ERROR(status))
                        {
                            retryCnt++;
                            //
                            // Max retry count according to Spec TCG PC Client Device Driver Design Principles
                            //
                            if (retryCnt < RETRY_CNT_MAX)
                            {
                                bit = PTP_CRB_CONTROL_AREA_REQUEST_GO_IDLE;
                                (void)mmio::Write((uintptr_t)&crbReg->CrbControlRequest, sizeof(uint32_t), &bit);
                                continue;
                            }
                            else
                            {
                                //
                                // Try to goIdle to recover TPM
                                //
                                status = STATUS_DEVICE_BUSY;
                                goto GoIdle_Exit;
                            }
                        }
                    }
                }
            }

            //
            // STEP 1:
            // Ready is any time the TPM is ready to receive a command, following a write
            // of 1 by software to Request.cmdReady, as indicated by the Status field
            // being cleared to 0.
            //

            bit = PTP_CRB_CONTROL_AREA_REQUEST_COMMAND_READY;
            (void)mmio::Write((uintptr_t)&crbReg->CrbControlRequest, sizeof(uint32_t), &bit);

            status = this->CrbWaitRegisterBits(
                &crbReg->CrbControlRequest,
                0,
                PTP_CRB_CONTROL_AREA_REQUEST_COMMAND_READY,
                PTP_TIMEOUT_C
            );

            if (NT_ERROR(status))
            {
                retryCnt++;
                if (retryCnt < RETRY_CNT_MAX)
                {
                    bit = PTP_CRB_CONTROL_AREA_REQUEST_GO_IDLE;
                    (void)mmio::Write((uintptr_t)&crbReg->CrbControlRequest, sizeof(uint32_t), &bit);
                    continue;
                }
                else
                {
                    status = STATUS_DEVICE_BUSY;
                    goto GoIdle_Exit;
                }
            }

            status = this->CrbWaitRegisterBits(
                &crbReg->CrbControlStatus,
                0,
                PTP_CRB_CONTROL_AREA_STATUS_TPM_IDLE,
                PTP_TIMEOUT_C
            );

            if (NT_ERROR(status))
            {
                retryCnt++;
                if (retryCnt < RETRY_CNT_MAX)
                {
                    bit = PTP_CRB_CONTROL_AREA_REQUEST_GO_IDLE;
                    (void)mmio::Write((uintptr_t)&crbReg->CrbControlRequest, sizeof(uint32_t), &bit);
                    continue;
                }
                else
                {
                    status = STATUS_DEVICE_BUSY;
                    goto GoIdle_Exit;
                }
            }

            break;
        }

        //
        // STEP 2:
        // Command Reception occurs following a Ready state between the write of the
        // first byte of a command to the Command Buffer and the receipt of a write
        // of 1 to Start.
        //
        for (uint32_t i = 0; i < sizeIn; i++)
        {
            (void)mmio::Write((uintptr_t)&crbReg->CrbDataBuffer[i], sizeof(uint8_t), const_cast<uint8_t*>(&bufferIn[i]));
        }

        uint32_t highAddressPart = (uint32_t)((uintptr_t)crbReg->CrbDataBuffer >> 32);
        (void)mmio::Write((uintptr_t)&crbReg->CrbControlCommandAddressHigh, sizeof(uint32_t), &highAddressPart);
        uint32_t crbDataBuffer = (uint32_t)(uintptr_t)crbReg->CrbDataBuffer;
        (void)mmio::Write((uintptr_t)&crbReg->CrbControlCommandAddressLow, sizeof(uint32_t), &crbDataBuffer);
        uint32_t bufferSize = sizeof(crbReg->CrbDataBuffer);
        (void)mmio::Write((uintptr_t)&crbReg->CrbControlCommandSize, sizeof(uint32_t), &bufferSize);

        (void)mmio::Write((uintptr_t)&crbReg->CrbControlResponseAddrss, sizeof(uint64_t), &crbDataBuffer);
        (void)mmio::Write((uintptr_t)&crbReg->CrbControlResponseSize, sizeof(uint32_t), &bufferSize);

        //
        // STEP 3:
        // Command Execution occurs after receipt of a 1 to Start and the TPM
        // clearing Start to 0.
        //
        bit = PTP_CRB_CONTROL_START;
        (void)mmio::Write((uintptr_t)&crbReg->CrbControlStart, sizeof(uint32_t), &bit);

        status = this->CrbWaitRegisterBits(
            &crbReg->CrbControlStart,
            0,
            PTP_CRB_CONTROL_START,
            PTP_TIMEOUT_MAX
        );

        if (NT_ERROR(status))
        {
            //
            // Command Completion check timeout. Cancel the currently executing command by writing TPM_CRB_CTRL_CANCEL,
            // Expect TPM_RC_CANCELLED or successfully completed response.
            //
            bit = PTP_CRB_CONTROL_CANCEL;
            (void)mmio::Write((uintptr_t)&crbReg->CrbControlCancel, sizeof(uint32_t), &bit);
            status = this->CrbWaitRegisterBits(
                &crbReg->CrbControlStart,
                0,
                PTP_CRB_CONTROL_START,
                PTP_TIMEOUT_B
            );

            bit = 0;
            (void)mmio::Write((uintptr_t)&crbReg->CrbControlCancel, sizeof(uint32_t), &bit);

            if (NT_ERROR(status))
            {
                //
                // Still in Command Execution state. Try to goIdle, the behavior is agnostic.
                //
                status = STATUS_DEVICE_BUSY;
                goto GoIdle_Exit;
            }
        }

        //
        // STEP 4:
        // Command Completion occurs after completion of a command (indicated by the
        // TPM clearing TPM_CRB_CTRL_Start_x to 0) and before a write of a 1 by the
        // software to Request.goIdle.
        //

        //
        // Get response data header
        //
        for (uint32_t i = 0; i < sizeof(TPM2_RESPONSE_HEADER); i++)
        {
            (void)mmio::Read((uintptr_t)&crbReg->CrbDataBuffer[i], sizeof(uint8_t), &bufferOut[i]);
        }

        //
        // Check the response data header (tag, parasize and returncode)
        //
        uint16_t data16 = 0;
        memcpy(&data16, bufferOut, sizeof(uint16_t));

        // TPM2 should not use this RSP_COMMAND
        if (_byteswap_ushort(data16) == TPM_ST_RSP_COMMAND)
        {
            DbgError("TPM_ST_RSP error: %x\n", TPM_ST_RSP_COMMAND);
            status = STATUS_NOT_SUPPORTED;
            goto GoIdle_Exit;
        }

        uint32_t data32 = 0;
        memcpy(&data32, (bufferOut + 2), sizeof(uint32_t));
        uint32_t tpmOutSize = _byteswap_ulong(data32);
        if (*sizeOut < tpmOutSize)
        {
            //
            // Command completed, but buffer is not enough
            //
            status = STATUS_BUFFER_TOO_SMALL;
            goto GoIdle_Exit;
        }

        *sizeOut = tpmOutSize;
        //
        // Continue reading the remaining data
        //
        for (uint32_t i = sizeof(TPM2_RESPONSE_HEADER); i < tpmOutSize; i++) {
            (void)mmio::Read((uintptr_t)&crbReg->CrbDataBuffer[i], sizeof(uint8_t), &bufferOut[i]);
        }

    GoIdle_Exit:

        //
        //  Return to Idle state by setting TPM_CRB_CTRL_STS_x.Status.goIdle to 1.
        //
        uint32_t bit32 = PTP_CRB_CONTROL_AREA_REQUEST_GO_IDLE;
        (void)mmio::Write((uintptr_t)&crbReg->CrbControlRequest, sizeof(uint32_t), &bit32);

        return status;
    }
};

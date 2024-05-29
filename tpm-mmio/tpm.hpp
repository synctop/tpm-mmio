#pragma once

class Tpm
{
private:

	//
	// TPM register base address.
	//
	uintptr_t tpmBaseAddress = 0;

    //
    // Cached PTP pointer.
    //
	TpmPtp* ptpInterface = nullptr;

    //
    // Reads a value of type T from an unaligned memory address.
    // This function uses RtlCopyMemory to safely read the value without assuming alignment.
    //
    // Parameters:
    // - source: Pointer to the unaligned memory address from which the value is to be read.
    //
    // Returns:
    // - T: The value read from the unaligned memory address.
    //
    template<typename T>
    T ReadUnaligned(const void* source)
    {
        T value;
        RtlCopyMemory(&value, source, sizeof(T));
        return value;
    }

    //
    // Writes a value of type T to an unaligned memory address.
    // This function uses RtlCopyMemory to safely write the value without assuming alignment.
    //
    // Parameters:
    // - destination: Pointer to the unaligned memory address to which the value is to be written.
    // - value: The value to be written to the unaligned memory address.
    //
    template<typename T>
    void WriteUnaligned(void* destination, T value)
    {
        RtlCopyMemory(destination, &value, sizeof(T));
    }
    
    //
    // Submits a command to the TPM through the appropriate TPM interface based on the configured interface type.
    // This function routes the command to either a CRB or FIFO interface handling routine.
    //
    // Parameters:
    // - inputParameterBlockSize: Size of the input parameter block.
    // - inputParameterBlock: Pointer to the input parameters for the TPM command.
    // - outputParameterBlockSize: Pointer to the size of the output parameter block, which may be updated.
    // - outputParameterBlock: Pointer to the buffer that will receive the TPM command's output.
    //
    // Returns:
    // - STATUS_SUCCESS: Command successfully sent and response received.
    // - STATUS_UNSUCCESSFUL: PTP interface not initialized or other failure.
    // - STATUS_DEVICE_NOT_CONNECTED: No valid PTP interface is connected.
    // - STATUS_INVALID_PARAMETER: Unknown or unsupported PTP interface type.
    //
    NTSTATUS SubmitCommand(
        _In_ uint32_t inputParameterBlockSize,
        _In_reads_bytes_(inputParameterBlockSize) const uint8_t* inputParameterBlock,
        _Inout_ uint32_t* outputParameterBlockSize,
        _Out_writes_bytes_(*outputParameterBlockSize) uint8_t* outputParameterBlock
    )
    {
        PTP_INTERFACE_TYPE interfaceType = this->ptpInterface->cachedInterface;

        if (interfaceType == PTP_INTERFACE_TYPE::PtpInterfaceTis || interfaceType == PTP_INTERFACE_TYPE::PtpInterfaceFifo) 
        {
            TpmTis tisInterface;           
            return tisInterface.TisCommand(
                (TIS_PC_REGISTERS*)this->tpmBaseAddress,
                inputParameterBlock,
                inputParameterBlockSize,
                outputParameterBlock,
                outputParameterBlockSize
            );
        }
        else if (interfaceType == PTP_INTERFACE_TYPE::PtpInterfaceCrb) 
        {
            TpmCrb crbInterface(this->ptpInterface);
            return crbInterface.CrbCommand(
                (PTP_CRB_REGISTERS*)this->tpmBaseAddress,
                inputParameterBlock,
                inputParameterBlockSize,
                outputParameterBlock,
                outputParameterBlockSize
            );  
        }
        else if (interfaceType == PTP_INTERFACE_TYPE::PtpInterfaceNull) 
        {
            return STATUS_DEVICE_NOT_CONNECTED; 
        }
        else 
        {
            DbgError("Unknown PTP interface type.\n");
            return STATUS_INVALID_PARAMETER; 
        }
    }

public:

	~Tpm()
	{
		delete this->ptpInterface;
	}

    //
    // Instantiate and initialize TpmPtp class then cache pointer to access PTP interface.
    // 
    // Returns:
    // - true: Instantiated and initialized successfully.
    // - false: Failed to instantiate or initialize.
    //
    bool Init()
    {
        if (!acpi::GetTpm2PhysicalAddress(&this->tpmBaseAddress))
        {
            // Already prints detailed error inside function.
            return false;
        }
        this->ptpInterface = new TpmPtp(this->tpmBaseAddress);
        if (!this->ptpInterface)
        {
            DbgError("Failed to instantiate TpmPtp class.\n");
            return false;
        }
        if (!this->ptpInterface->Init())
        {
            DbgError("Failed to initialize TpmPtp class.\n");
            return false;
        }
        Dbg("Instantiated and initialized TpmPtp class.\n");
        return true;
    }
    
    //
    // Reads the public area of a TPM object.
    //
    // This function retrieves the public area of a TPM object specified by its handle.
    // It constructs the command to send to the TPM, sends the command, receives the response,
    // and extracts the public area, the name, and the qualified name of the object.
    //
    // Parameters:
    // - objectHandle: Handle to the TPM object whose public area is to be read.
    // - outPublic: Pointer to a TPM2B_PUBLIC structure that will receive the public area of the object.
    // - name: Pointer to a TPM2B_NAME structure that will receive the name of the object.
    // - qualifiedName: Pointer to a TPM2B_NAME structure that will receive the qualified name of the object.
    //
    // Returns:
    // - STATUS_SUCCESS: The public area was successfully read.
    // - STATUS_UNSUCCESSFUL: An error occurred while reading the public area.
    // - STATUS_BUFFER_TOO_SMALL: recvBuffer size was too small.
    // - STATUS_INVALID_PARAMETER: One or more of the parameters are invalid.
    // - STATUS_DEVICE_BUSY: TPM device exception.
    // - STATUS_NOT_SUPPORTED: Read operation not supported.
    //
    NTSTATUS ReadPublic(
        _In_ TPMI_DH_OBJECT objectHandle,
        _Out_ TPM2B_PUBLIC* outPublic,
        _Out_ TPM2B_NAME* name,
        _Out_ TPM2B_NAME* qualifiedName
    )
    {
        //
        // Construct command
        //
        TPM2_READ_PUBLIC_COMMAND sendBuffer = { { 0 } };

        sendBuffer.Header.tag = _byteswap_ushort(TPM_ST_NO_SESSIONS);
        sendBuffer.Header.commandCode = _byteswap_ulong(TPM_CC_ReadPublic);

        sendBuffer.ObjectHandle = _byteswap_ulong(objectHandle);

        uint32_t sendBufferSize = (uint32_t)sizeof(sendBuffer);
        sendBuffer.Header.paramSize = _byteswap_ulong(sendBufferSize);

        //
        // send Tpm command
        //
        TPM2_READ_PUBLIC_RESPONSE recvBuffer = { { 0 } };

        uint32_t recvBufferSize = sizeof(recvBuffer);
        NTSTATUS status = this->SubmitCommand(sendBufferSize, (uint8_t*)&sendBuffer, &recvBufferSize, (uint8_t*)&recvBuffer);
        if (NT_ERROR(status)) 
        {
            return status;
        }

        if (recvBufferSize < sizeof(TPM2_RESPONSE_HEADER)) 
        {
            DbgError("ReadPublic - recvBufferSize Error - %x.\n", recvBufferSize);
            return STATUS_BUFFER_TOO_SMALL;
        }

        TPM_RC responseCode = _byteswap_ulong(recvBuffer.Header.responseCode);
        if (responseCode != TPM_RC_SUCCESS) 
        {
            DbgError("ReadPublic - responseCode - 0x%08x.\n", _byteswap_ulong(recvBuffer.Header.responseCode));
        }

        switch (responseCode) {
        case TPM_RC_SUCCESS:
            // return data
            break;
        case TPM_RC_SEQUENCE:
            // objectHandle references a sequence object
            return STATUS_INVALID_PARAMETER;
        default:
            return STATUS_DEVICE_BUSY;
        }

        //
        // Basic check
        //
        uint16_t outPublicSize = _byteswap_ushort(recvBuffer.OutPublic.size);
        if (outPublicSize > sizeof(TPMT_PUBLIC)) 
        {
            DbgError("ReadPublic - outPublicSize error %x.\n", outPublicSize);
            return STATUS_DEVICE_BUSY;
        }

        uint16_t nameSize = _byteswap_ushort(
            this->ReadUnaligned<uint16_t>(
                (uint16_t*)((uint8_t*)&recvBuffer + sizeof(TPM2_RESPONSE_HEADER) +
                    sizeof(uint16_t) + outPublicSize)
            )
        );
        if (nameSize > sizeof(TPMU_NAME)) 
        {
            DbgError("ReadPublic - nameSize error %x.\n", nameSize);
            return STATUS_DEVICE_BUSY;
        }

        uint16_t qualifiedNameSize = _byteswap_ushort(
            this->ReadUnaligned<uint16_t>(
                (uint16_t*)((uint8_t*)&recvBuffer + sizeof(TPM2_RESPONSE_HEADER) +
                    sizeof(uint16_t) + outPublicSize +
                    sizeof(uint16_t) + nameSize)
            )
        );
        if (qualifiedNameSize > sizeof(TPMU_NAME)) 
        {
            DbgError("ReadPublic - qualifiedNameSize error %x.\n", qualifiedNameSize);
            return STATUS_DEVICE_BUSY;
        }

        if (recvBufferSize != sizeof(TPM2_RESPONSE_HEADER) + sizeof(uint16_t) + outPublicSize + sizeof(uint16_t) + nameSize + sizeof(uint16_t) + qualifiedNameSize) 
        {
            DbgError("ReadPublic - recvBufferSize %x Error - outPublicSize %x, nameSize %x, qualifiedNameSize %x.\n", recvBufferSize, outPublicSize, nameSize, qualifiedNameSize);
            return STATUS_DEVICE_BUSY;
        }

        //
        // Return the response
        //
        uint8_t* buffer = (uint8_t*)&recvBuffer.OutPublic;
        memcpy(outPublic, &recvBuffer.OutPublic, sizeof(uint16_t) + outPublicSize);
        outPublic->size = outPublicSize;
        outPublic->publicArea.type = _byteswap_ushort(outPublic->publicArea.type);
        outPublic->publicArea.nameAlg = _byteswap_ushort(outPublic->publicArea.nameAlg);

        this->WriteUnaligned<uint32_t>((uint32_t*)&outPublic->publicArea.objectAttributes, _byteswap_ulong(this->ReadUnaligned<uint32_t>((uint32_t*)&outPublic->publicArea.objectAttributes)));

        buffer = (uint8_t*)&recvBuffer.OutPublic.publicArea.authPolicy;
        outPublic->publicArea.authPolicy.size = _byteswap_ushort(this->ReadUnaligned<uint16_t>((uint16_t*)buffer));
        buffer += sizeof(uint16_t);

        if (outPublic->publicArea.authPolicy.size > sizeof(TPMU_HA)) 
        {
            DbgError("ReadPublic - authPolicy.size error %x.\n", outPublic->publicArea.authPolicy.size);
            return STATUS_DEVICE_BUSY;
        }

        memcpy(outPublic->publicArea.authPolicy.buffer, buffer, outPublic->publicArea.authPolicy.size);
        buffer += outPublic->publicArea.authPolicy.size;

        // TPMU_PUBLIC_PARMS
        switch (outPublic->publicArea.type) 
        {
        case TPM_ALG_KEYEDHASH:
            outPublic->publicArea.parameters.keyedHashDetail.scheme.scheme = _byteswap_ushort(this->ReadUnaligned<uint16_t>((uint16_t*)buffer));
            buffer += sizeof(uint16_t);
            switch (outPublic->publicArea.parameters.keyedHashDetail.scheme.scheme) 
            {
            case TPM_ALG_HMAC:
                outPublic->publicArea.parameters.keyedHashDetail.scheme.details.hmac.hashAlg = _byteswap_ushort(this->ReadUnaligned<uint16_t>((uint16_t*)buffer));
                buffer += sizeof(uint16_t);
                break;
            case TPM_ALG_XOR:
                outPublic->publicArea.parameters.keyedHashDetail.scheme.details. xor .hashAlg = _byteswap_ushort(this->ReadUnaligned<uint16_t>((uint16_t*)buffer));
                buffer += sizeof(uint16_t);
                outPublic->publicArea.parameters.keyedHashDetail.scheme.details. xor .kdf = _byteswap_ushort(this->ReadUnaligned<uint16_t>((uint16_t*)buffer));
                buffer += sizeof(uint16_t);
                break;
            default:
                return STATUS_NOT_SUPPORTED;
            }

        case TPM_ALG_SYMCIPHER:
            outPublic->publicArea.parameters.symDetail.algorithm = _byteswap_ushort(this->ReadUnaligned<uint16_t>((uint16_t*)buffer));
            buffer += sizeof(uint16_t);
            switch (outPublic->publicArea.parameters.symDetail.algorithm) 
            {
            case TPM_ALG_AES:
                outPublic->publicArea.parameters.symDetail.keyBits.aes = _byteswap_ushort(this->ReadUnaligned<uint16_t>((uint16_t*)buffer));
                buffer += sizeof(uint16_t);
                outPublic->publicArea.parameters.symDetail.mode.aes = _byteswap_ushort(this->ReadUnaligned<uint16_t>((uint16_t*)buffer));
                buffer += sizeof(uint16_t);
                break;
            case TPM_ALG_SM4:
                outPublic->publicArea.parameters.symDetail.keyBits.SM4 = _byteswap_ushort(this->ReadUnaligned<uint16_t>((uint16_t*)buffer));
                buffer += sizeof(uint16_t);
                outPublic->publicArea.parameters.symDetail.mode.SM4 = _byteswap_ushort(this->ReadUnaligned<uint16_t>((uint16_t*)buffer));
                buffer += sizeof(uint16_t);
                break;
            case TPM_ALG_XOR:
                outPublic->publicArea.parameters.symDetail.keyBits. xor = _byteswap_ushort(this->ReadUnaligned<uint16_t>((uint16_t*)buffer));
                buffer += sizeof(uint16_t);
                break;
            case TPM_ALG_NULL:
                break;
            default:
                return STATUS_NOT_SUPPORTED;
            }

            break;
        case TPM_ALG_RSA:
            outPublic->publicArea.parameters.rsaDetail.symmetric.algorithm = _byteswap_ushort(this->ReadUnaligned<uint16_t>((uint16_t*)buffer));
            buffer += sizeof(uint16_t);
            switch (outPublic->publicArea.parameters.rsaDetail.symmetric.algorithm) 
            {
            case TPM_ALG_AES:
                outPublic->publicArea.parameters.rsaDetail.symmetric.keyBits.aes = _byteswap_ushort(this->ReadUnaligned<uint16_t>((uint16_t*)buffer));
                buffer += sizeof(uint16_t);
                outPublic->publicArea.parameters.rsaDetail.symmetric.mode.aes = _byteswap_ushort(this->ReadUnaligned<uint16_t>((uint16_t*)buffer));
                buffer += sizeof(uint16_t);
                break;
            case TPM_ALG_SM4:
                outPublic->publicArea.parameters.rsaDetail.symmetric.keyBits.SM4 = _byteswap_ushort(this->ReadUnaligned<uint16_t>((uint16_t*)buffer));
                buffer += sizeof(uint16_t);
                outPublic->publicArea.parameters.rsaDetail.symmetric.mode.SM4 = _byteswap_ushort(this->ReadUnaligned<uint16_t>((uint16_t*)buffer));
                buffer += sizeof(uint16_t);
                break;
            case TPM_ALG_NULL:
                break;
            default:
                return STATUS_NOT_SUPPORTED;
            }

            outPublic->publicArea.parameters.rsaDetail.scheme.scheme = _byteswap_ushort(this->ReadUnaligned<uint16_t>((uint16_t*)buffer));
            buffer += sizeof(uint16_t);
            switch (outPublic->publicArea.parameters.rsaDetail.scheme.scheme) 
            {
            case TPM_ALG_RSASSA:
                outPublic->publicArea.parameters.rsaDetail.scheme.details.rsassa.hashAlg = _byteswap_ushort(this->ReadUnaligned<uint16_t>((uint16_t*)buffer));
                buffer += sizeof(uint16_t);
                break;
            case TPM_ALG_RSAPSS:
                outPublic->publicArea.parameters.rsaDetail.scheme.details.rsapss.hashAlg = _byteswap_ushort(this->ReadUnaligned<uint16_t>((uint16_t*)buffer));
                buffer += sizeof(uint16_t);
                break;
            case TPM_ALG_RSAES:
                break;
            case TPM_ALG_OAEP:
                outPublic->publicArea.parameters.rsaDetail.scheme.details.oaep.hashAlg = _byteswap_ushort(this->ReadUnaligned<uint16_t>((uint16_t*)buffer));
                buffer += sizeof(uint16_t);
                break;
            case TPM_ALG_NULL:
                break;
            default:
                return STATUS_NOT_SUPPORTED;
            }

            outPublic->publicArea.parameters.rsaDetail.keyBits = _byteswap_ushort(this->ReadUnaligned<uint16_t>((uint16_t*)buffer));
            buffer += sizeof(uint16_t);
            outPublic->publicArea.parameters.rsaDetail.exponent = _byteswap_ushort(this->ReadUnaligned<uint16_t>((uint16_t*)buffer));
            buffer += sizeof(uint32_t);
            break;
        case TPM_ALG_ECC:
            outPublic->publicArea.parameters.eccDetail.symmetric.algorithm = _byteswap_ushort(this->ReadUnaligned<uint16_t>((uint16_t*)buffer));
            buffer += sizeof(uint16_t);
            switch (outPublic->publicArea.parameters.eccDetail.symmetric.algorithm) 
            {
            case TPM_ALG_AES:
                outPublic->publicArea.parameters.eccDetail.symmetric.keyBits.aes = _byteswap_ushort(this->ReadUnaligned<uint16_t>((uint16_t*)buffer));
                buffer += sizeof(uint16_t);
                outPublic->publicArea.parameters.eccDetail.symmetric.mode.aes = _byteswap_ushort(this->ReadUnaligned<uint16_t>((uint16_t*)buffer));
                buffer += sizeof(uint16_t);
                break;
            case TPM_ALG_SM4:
                outPublic->publicArea.parameters.eccDetail.symmetric.keyBits.SM4 = _byteswap_ushort(this->ReadUnaligned<uint16_t>((uint16_t*)buffer));
                buffer += sizeof(uint16_t);
                outPublic->publicArea.parameters.eccDetail.symmetric.mode.SM4 = _byteswap_ushort(this->ReadUnaligned<uint16_t>((uint16_t*)buffer));
                buffer += sizeof(uint16_t);
                break;
            case TPM_ALG_NULL:
                break;
            default:
                return STATUS_NOT_SUPPORTED;
            }

            outPublic->publicArea.parameters.eccDetail.scheme.scheme = _byteswap_ushort(this->ReadUnaligned<uint16_t>((uint16_t*)buffer));
            buffer += sizeof(uint16_t);
            switch (outPublic->publicArea.parameters.eccDetail.scheme.scheme) 
            {
            case TPM_ALG_ECDSA:
                outPublic->publicArea.parameters.eccDetail.scheme.details.ecdsa.hashAlg = _byteswap_ushort(this->ReadUnaligned<uint16_t>((uint16_t*)buffer));
                buffer += sizeof(uint16_t);
                break;
            case TPM_ALG_ECDAA:
                outPublic->publicArea.parameters.eccDetail.scheme.details.ecdaa.hashAlg = _byteswap_ushort(this->ReadUnaligned<uint16_t>((uint16_t*)buffer));
                buffer += sizeof(uint16_t);
                break;
            case TPM_ALG_ECSCHNORR:
                outPublic->publicArea.parameters.eccDetail.scheme.details.ecSchnorr.hashAlg = _byteswap_ushort(this->ReadUnaligned<uint16_t>((uint16_t*)buffer));
                buffer += sizeof(uint16_t);
                break;
            case TPM_ALG_ECDH:
                break;
            case TPM_ALG_NULL:
                break;
            default:
                return STATUS_NOT_SUPPORTED;
            }

            outPublic->publicArea.parameters.eccDetail.curveID = _byteswap_ushort(this->ReadUnaligned<uint16_t>((uint16_t*)buffer));
            buffer += sizeof(uint16_t);
            outPublic->publicArea.parameters.eccDetail.kdf.scheme = _byteswap_ushort(this->ReadUnaligned<uint16_t>((uint16_t*)buffer));
            buffer += sizeof(uint16_t);
            switch (outPublic->publicArea.parameters.eccDetail.kdf.scheme) 
            {
            case TPM_ALG_MGF1:
                outPublic->publicArea.parameters.eccDetail.kdf.details.mgf1.hashAlg = _byteswap_ushort(this->ReadUnaligned<uint16_t>((uint16_t*)buffer));
                buffer += sizeof(uint16_t);
                break;
            case TPM_ALG_KDF1_SP800_108:
                outPublic->publicArea.parameters.eccDetail.kdf.details.kdf1_sp800_108.hashAlg = _byteswap_ushort(this->ReadUnaligned<uint16_t>((uint16_t*)buffer));
                buffer += sizeof(uint16_t);
                break;
            case TPM_ALG_KDF1_SP800_56a:
                outPublic->publicArea.parameters.eccDetail.kdf.details.kdf1_SP800_56a.hashAlg = _byteswap_ushort(this->ReadUnaligned<uint16_t>((uint16_t*)buffer));
                buffer += sizeof(uint16_t);
                break;
            case TPM_ALG_KDF2:
                outPublic->publicArea.parameters.eccDetail.kdf.details.kdf2.hashAlg = _byteswap_ushort(this->ReadUnaligned<uint16_t>((uint16_t*)buffer));
                buffer += sizeof(uint16_t);
                break;
            case TPM_ALG_NULL:
                break;
            default:
                return STATUS_NOT_SUPPORTED;
            }

            break;
        default:
            return STATUS_NOT_SUPPORTED;
        }

        // TPMU_PUBLIC_ID
        switch (outPublic->publicArea.type) 
        {
        case TPM_ALG_KEYEDHASH:
            outPublic->publicArea.unique.keyedHash.size = _byteswap_ushort(this->ReadUnaligned<uint16_t>((uint16_t*)buffer));
            buffer += sizeof(uint16_t);
            if (outPublic->publicArea.unique.keyedHash.size > sizeof(TPMU_HA)) 
            {
                DbgError("ReadPublic - keyedHash.size error %x.\n", outPublic->publicArea.unique.keyedHash.size);
                return STATUS_DEVICE_BUSY;
            }

            memcpy(outPublic->publicArea.unique.keyedHash.buffer, buffer, outPublic->publicArea.unique.keyedHash.size);
            buffer += outPublic->publicArea.unique.keyedHash.size;
            break;
        case TPM_ALG_SYMCIPHER:
            outPublic->publicArea.unique.sym.size = _byteswap_ushort(this->ReadUnaligned<uint16_t>((uint16_t*)buffer));
            buffer += sizeof(uint16_t);
            if (outPublic->publicArea.unique.sym.size > sizeof(TPMU_HA)) 
            {
                DbgError("ReadPublic - sym.size error %x.\n", outPublic->publicArea.unique.sym.size);
                return STATUS_DEVICE_BUSY;
            }

            memcpy(outPublic->publicArea.unique.sym.buffer, buffer, outPublic->publicArea.unique.sym.size);
            buffer += outPublic->publicArea.unique.sym.size;
            break;
        case TPM_ALG_RSA:
            outPublic->publicArea.unique.rsa.size = _byteswap_ushort(this->ReadUnaligned<uint16_t>((uint16_t*)buffer));
            buffer += sizeof(uint16_t);
            if (outPublic->publicArea.unique.rsa.size > MAX_RSA_KEY_BYTES) 
            {
                DbgError("ReadPublic - rsa.size error %x.\n", outPublic->publicArea.unique.rsa.size);
                return STATUS_DEVICE_BUSY;
            }

            memcpy(outPublic->publicArea.unique.rsa.buffer, buffer, outPublic->publicArea.unique.rsa.size);
            buffer += outPublic->publicArea.unique.rsa.size;
            break;
        case TPM_ALG_ECC:
            outPublic->publicArea.unique.ecc.x.size = _byteswap_ushort(this->ReadUnaligned<uint16_t>((uint16_t*)buffer));
            buffer += sizeof(uint16_t);
            if (outPublic->publicArea.unique.ecc.x.size > MAX_ECC_KEY_BYTES) 
            {
                DbgError("ReadPublic - ecc.x.size error %x.\n", outPublic->publicArea.unique.ecc.x.size);
                return STATUS_DEVICE_BUSY;
            }

            memcpy(outPublic->publicArea.unique.ecc.x.buffer, buffer, outPublic->publicArea.unique.ecc.x.size);
            buffer += outPublic->publicArea.unique.ecc.x.size;
            outPublic->publicArea.unique.ecc.y.size = _byteswap_ushort(this->ReadUnaligned<uint16_t>((uint16_t*)buffer));
            buffer += sizeof(uint16_t);
            if (outPublic->publicArea.unique.ecc.y.size > MAX_ECC_KEY_BYTES) 
            {
                DbgError("ReadPublic - ecc.y.size error %x.\n", outPublic->publicArea.unique.ecc.y.size);
                return STATUS_DEVICE_BUSY;
            }

            memcpy(outPublic->publicArea.unique.ecc.y.buffer, buffer, outPublic->publicArea.unique.ecc.y.size);
            buffer += outPublic->publicArea.unique.ecc.y.size;
            break;
        default:
            return STATUS_NOT_SUPPORTED;
        }

        memcpy(name->name, (uint8_t*)&recvBuffer + sizeof(TPM2_RESPONSE_HEADER) + sizeof(uint16_t) + outPublicSize + sizeof(uint16_t), nameSize);
        name->size = nameSize;

        memcpy(qualifiedName->name, (uint8_t*)&recvBuffer + sizeof(TPM2_RESPONSE_HEADER) + sizeof(uint16_t) + outPublicSize + sizeof(uint16_t) + nameSize + sizeof(uint16_t), qualifiedNameSize);
        qualifiedName->size = qualifiedNameSize;

        return STATUS_SUCCESS;
    }

};


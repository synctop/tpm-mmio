//                               _                 
//   ___  _   _  _ __    ___    | |_   ___   _ __  
//  / __|| | | || '_ \  / __|   | __| / _ \ | '_ \ 
//  \__ \| |_| || | | || (__  _ | |_ | (_) || |_) |
//  |___/ \__, ||_| |_| \___|(_) \__| \___/ | .__/ 
//        |___/                             |_|    
//
// © Copyright 2021-2024 sync.top. All rights reserved.

#pragma once

class TpmPtp
{
private:

	//
	// Hardcoded TPM register base address.
	//
	const uintptr_t tpmBaseAddress = 0xfed40000;

	//
	// Check whether TPM PTP register exists.
	//
	// Returns:
	// - true: TPM PTP exists.
	// - false: TPM PTP is not found.
	//
	bool IsPtpAvailable()
	{
		uint8_t data = 0xFF;
		if (mmio::Read(this->tpmBaseAddress, sizeof(data), &data))
		{
			if (data != 0xFF)
			{
				return true;
			}
			//
			// No TPM chip
			//
		}
		return false;
	}

	//
	// Retrieves the TPM PTP (Platform TPM Profile) interface so we can communicate with chip correctly.
	// 
	// Returns:
	// - true: Retrieved interface successfully.
	// - false: Failed to retrieve interface. 
	//
	bool GetPtpInterface()
	{
		bool status = false;
		PTP_CRB_INTERFACE_IDENTIFIER interfaceId = { { 0 } };
		PTP_FIFO_INTERFACE_CAPABILITY interfaceCapability = { { 0 } };

		if (this->IsPtpAvailable())
		{
			if (mmio::Read((uintptr_t) & ((PTP_CRB_REGISTERS*)this->tpmBaseAddress)->interfaceId, sizeof(uint32_t), &interfaceId.Uint32))
			{
				if (mmio::Read((uintptr_t) & ((PTP_FIFO_REGISTERS*)this->tpmBaseAddress)->interfaceCapability, sizeof(uint32_t), &interfaceCapability.Uint32))
				{
					if ((interfaceId.Bits.InterfaceType == PTP_INTERFACE_IDENTIFIER_INTERFACE_TYPE_CRB) &&
						(interfaceId.Bits.InterfaceVersion == PTP_INTERFACE_IDENTIFIER_INTERFACE_VERSION_CRB) &&
						(interfaceId.Bits.CapCRB != 0))
					{
						this->cachedInterface = PTP_INTERFACE_TYPE::PtpInterfaceCrb;
						this->idleByPassState = (uint8_t)(interfaceId.Bits.CapCRBIdleBypass);
						// Status is true if idleByPassState is NOT 0xFF, false otherwise.
						status = (this->idleByPassState != 0xFF);
						if (!status)  
						{
							DbgError("Failed to get IdleByPass state, double check PTP awareness in BIOS.\n");
						}
					}

					if ((interfaceId.Bits.InterfaceType == PTP_INTERFACE_IDENTIFIER_INTERFACE_TYPE_FIFO) &&
						(interfaceId.Bits.InterfaceVersion == PTP_INTERFACE_IDENTIFIER_INTERFACE_VERSION_FIFO) &&
						(interfaceId.Bits.CapFIFO != 0) &&
						(interfaceCapability.Bits.InterfaceVersion == INTERFACE_CAPABILITY_INTERFACE_VERSION_PTP))
					{
						this->cachedInterface = PTP_INTERFACE_TYPE::PtpInterfaceFifo;
						status = true;
					}

					if (interfaceId.Bits.InterfaceType == PTP_INTERFACE_IDENTIFIER_INTERFACE_TYPE_TIS) 
					{
						this->cachedInterface = PTP_INTERFACE_TYPE::PtpInterfaceTis;
						status = true;
					}
				}
			}
		}

		return status;
	}

public:

	//
	// Cached PTP interface & CRB IdleByPass state for later use in submit command.
	//
	PTP_INTERFACE_TYPE cachedInterface = PTP_INTERFACE_TYPE::PtpInterfaceNull;
	uint8_t idleByPassState = 0xFF;

	bool Init()
	{
		if (!this->GetPtpInterface())
		{
			DbgError("Failed to get TPM PTP interface, double check you have TPM enabled in BIOS & PTP awareness is enabled.\n");
			return false;
		}
		return true;
	}

};
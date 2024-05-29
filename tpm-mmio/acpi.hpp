#pragma once

namespace acpi
{
	bool GetTpm2PhysicalAddress(uintptr_t* tpmAddress)
	{		
		NTSTATUS status = AuxKlibInitialize();
		if (NT_SUCCESS(status))
		{
			ULONG tableSize = 0;
			//
			//0x324d5054 == TPM2
			//
			status = AuxKlibGetSystemFirmwareTable('ACPI', 0x324d5054, 0, 0, &tableSize);
			if (status == STATUS_BUFFER_TOO_SMALL)
			{
				auto table = (unsigned char* )ExAllocatePool(NonPagedPool, tableSize);
				if (table)
				{
					status = AuxKlibGetSystemFirmwareTable('ACPI', 0x324d5054, table, tableSize, 0);
					if (NT_SUCCESS(status))
					{
						auto tpm2Table = (TPM2_ACPI_TABLE*)table;
						if (tpm2Table->AddressOfControlArea)
						{
							Dbg("Got TPM2 physical address: 0x%llx", tpm2Table->AddressOfControlArea);
							*tpmAddress = tpm2Table->AddressOfControlArea;
							ExFreePool(table);
							return true;
						}
					}
					ExFreePool(table);
				}
			}
		}

		DbgError("Failed to retrieve TPM2 physical address, error code: 0x%x.\n", status);
		return false;
	}
}
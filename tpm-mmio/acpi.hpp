#pragma once

namespace acpi
{
	bool IsIntelCPU()
	{
		int cpuInfoRegisters[4];
		__cpuidex(cpuInfoRegisters, 0, 0);
		return cpuInfoRegisters[2] == 0x6c65746e;
	}

	bool GetTpm2PhysicalAddress(_Inout_ uintptr_t* tpmAddress)
	{		
		if (IsIntelCPU())
		{
			//
			// Hardcode TPM address for Intel CPU's
			//
			*tpmAddress = 0xfed40000;
			return true;
		}
		else
		{
			// AMD CPU's are left as an exercise for the reader.
			DbgError("AMD CPU's are not currently supported.\n");
			return false;
		}
	}
}
//                               _                 
//   ___  _   _  _ __    ___    | |_   ___   _ __  
//  / __|| | | || '_ \  / __|   | __| / _ \ | '_ \ 
//  \__ \| |_| || | | || (__  _ | |_ | (_) || |_) |
//  |___/ \__, ||_| |_| \___|(_) \__| \___/ | .__/ 
//        |___/                             |_|    
//
// © Copyright 2021-2024 sync.top. All rights reserved.

#include <ntddk.h>
#include <bcrypt.h>
#include <ntstrsafe.h>

#include "stdint.hpp"
#include "defs.hpp"
#include "mmio.hpp"
#include "ptp.hpp"
#include "crb.hpp"
#include "tis.hpp"
#include "tpm.hpp"

#pragma warning(push) 
#pragma warning(disable : 4996)  // Disable "ExAllocatePool" is deprecated.
void* operator new(size_t size) { return ExAllocatePool(NonPagedPool, size); }
#pragma warning(pop)  
void operator delete(void* p, size_t /*size*/) { if (p) ExFreePool(p); }

void DriverUnload(_In_ PDRIVER_OBJECT driverObject) 
{
	UNREFERENCED_PARAMETER(driverObject);
	Dbg("Unloading tpm-mmio.sys.\n"); 
}

void PrintBufferContents(const char* label, const uint8_t* buffer, uint16_t size) 
{
    char outputBuffer[1024];
    char tempBuffer[5];
    RtlZeroMemory(outputBuffer, sizeof(outputBuffer));
    RtlZeroMemory(tempBuffer, sizeof(tempBuffer));

    for (uint16_t i = 0; i < size; i++)
    {
        RtlStringCbPrintfA(tempBuffer, sizeof(tempBuffer), "%02x", buffer[i]);
        RtlStringCbCatA(outputBuffer, sizeof(outputBuffer), tempBuffer);
    }
    Dbg("%s (size: %u): %s\n", label, size, outputBuffer);

    BCRYPT_ALG_HANDLE hAlgorithm;
    BCRYPT_HASH_HANDLE hHash;
    UCHAR hashBuffer[64];
    ULONG hashLength = 0;
    ULONG resultLength = 0;

    if (NT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlgorithm, BCRYPT_MD5_ALGORITHM, NULL, 0)))
    {
        if (NT_SUCCESS(BCryptGetProperty(hAlgorithm, BCRYPT_HASH_LENGTH, (PUCHAR)&hashLength, sizeof(hashLength), &resultLength, 0)))
        {
            if (NT_SUCCESS(BCryptCreateHash(hAlgorithm, &hHash, NULL, 0, NULL, 0, 0)))
            {
                if (NT_SUCCESS(BCryptHashData(hHash, (PUCHAR)buffer, size, 0)))
                {
                    if (NT_SUCCESS(BCryptFinishHash(hHash, hashBuffer, hashLength, 0)))
                    {
                        RtlZeroMemory(outputBuffer, sizeof(outputBuffer));
                        RtlStringCbCatA(outputBuffer, sizeof(outputBuffer), "\t[!] MD5: ");
                        for (ULONG i = 0; i < hashLength; i++)
                        {
                            RtlStringCbPrintfA(tempBuffer, sizeof(tempBuffer), "%02x", hashBuffer[i]);
                            RtlStringCbCatA(outputBuffer, sizeof(outputBuffer), tempBuffer);
                        }
                        DbgPrintEx(0, 0, "%s\n", outputBuffer);
                    }
                }
                BCryptDestroyHash(hHash);
            }
        }
        BCryptCloseAlgorithmProvider(hAlgorithm, 0);
    }


    if (NT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlgorithm, BCRYPT_SHA1_ALGORITHM, NULL, 0)))
    {
        if (NT_SUCCESS(BCryptGetProperty(hAlgorithm, BCRYPT_HASH_LENGTH, (PUCHAR)&hashLength, sizeof(hashLength), &resultLength, 0)))
        {
            if (NT_SUCCESS(BCryptCreateHash(hAlgorithm, &hHash, NULL, 0, NULL, 0, 0)))
            {
                if (NT_SUCCESS(BCryptHashData(hHash, (PUCHAR)buffer, size, 0)))
                {
                    if (NT_SUCCESS(BCryptFinishHash(hHash, hashBuffer, hashLength, 0)))
                    {
                        RtlZeroMemory(outputBuffer, sizeof(outputBuffer));
                        RtlStringCbCatA(outputBuffer, sizeof(outputBuffer), "\t[!] SHA-1: ");
                        for (ULONG i = 0; i < hashLength; i++)
                        {
                            RtlStringCbPrintfA(tempBuffer, sizeof(tempBuffer), "%02x", hashBuffer[i]);
                            RtlStringCbCatA(outputBuffer, sizeof(outputBuffer), tempBuffer);
                        }
                        DbgPrintEx(0, 0, "%s\n", outputBuffer);
                    }
                }
                BCryptDestroyHash(hHash);
            }
        }
        BCryptCloseAlgorithmProvider(hAlgorithm, 0);
    }

    if (NT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlgorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0)))
    {
        if (NT_SUCCESS(BCryptGetProperty(hAlgorithm, BCRYPT_HASH_LENGTH, (PUCHAR)&hashLength, sizeof(hashLength), &resultLength, 0)))
        {
            if (NT_SUCCESS(BCryptCreateHash(hAlgorithm, &hHash, NULL, 0, NULL, 0, 0)))
            {
                if (NT_SUCCESS(BCryptHashData(hHash, (PUCHAR)buffer, size, 0)))
                {
                    if (NT_SUCCESS(BCryptFinishHash(hHash, hashBuffer, hashLength, 0)))
                    {
                        RtlZeroMemory(outputBuffer, sizeof(outputBuffer));
                        RtlStringCbCatA(outputBuffer, sizeof(outputBuffer), "\t[!] SHA-256: ");
                        for (ULONG i = 0; i < hashLength; i++)
                        {
                            RtlStringCbPrintfA(tempBuffer, sizeof(tempBuffer), "%02x", hashBuffer[i]);
                            RtlStringCbCatA(outputBuffer, sizeof(outputBuffer), tempBuffer);
                        }
                        DbgPrintEx(0, 0, "%s\n", outputBuffer);
                    }
                }
                BCryptDestroyHash(hHash);
            }
        }
        BCryptCloseAlgorithmProvider(hAlgorithm, 0);
    }
}

SYNC_EXTERN NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT driverObject, _In_ PUNICODE_STRING registryPath)
{
	UNREFERENCED_PARAMETER(registryPath);
	driverObject->DriverUnload = DriverUnload;

	Tpm* tpm = new Tpm();
	if (!tpm)
	{
		DbgError("Failed to instantiate Tpm class.\n");
		return STATUS_INSUFFICIENT_RESOURCES;
	}	
	else if (!tpm->Init())
	{
		DbgError("Failed to initialize Tpm class.\n");
		return STATUS_DEVICE_HARDWARE_ERROR;
	}

    //
    // EK reserved handle from TCG Provisioning Guidance PDF.
    //
	TPMI_DH_OBJECT objectHandle = 0x81010001;
	TPM2B_PUBLIC outPublic = { 0 };
	TPM2B_NAME name = { 0 };
	TPM2B_NAME qualifiedName = { 0 };

	NTSTATUS status = tpm->ReadPublic(objectHandle, &outPublic, &name, &qualifiedName);
	if (NT_SUCCESS(status))
	{
		Dbg("ReadEkPub succeeded.\n");
		PrintBufferContents("EK", outPublic.publicArea.unique.rsa.buffer, outPublic.publicArea.unique.rsa.size);
	}
	else
	{
		Dbg("ReadEkPub failed.\n");
	}	

	delete tpm;

    Dbg("Returning with status code: 0x%x.\n", status);

	return status;
}
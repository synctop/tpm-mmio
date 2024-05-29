# WDK MMIO Implementation for TPM_ReadPublic

Many spoofing providers believe that hooking OS-provided resources like tbs.sys or tpm.sys is sufficient to hide the TPM's Endorsement Key (EK) and presence from anti-cheat systems. This proof of concept (POC) demonstrates how Memory-Mapped I/O (MMIO) can be used to directly query the TPM state and the EK from the chip itself, bypassing any OS hooks.

## Installation

- In CMD shell: ``shutdown /r /t 0 /o`` or Start button -> Power icon -> SHIFT key + Restart
- Navigate: Troubleshooting -> Advanced Settings -> Startup Settings -> Reboot 
- After reset choose F7 or 7 “Disable driver signature checks”
- Load driver using sc start/sc create.

## "Bypassing" or "Hooking" MMIO

Frankly, there is no way to "bypass" or "hook" MMIO. The only viable method to spoof a TPM's EK is through a hypervisor, which traps the guest TPM MMIO registers to redirect them to your own handler.

Creating a hypervisor today is very challenging, especially since anti-cheat systems are becoming increasingly sophisticated and have numerous tricks to fault your hypervisor and cause the guest PC to bugcheck.

## Detection Vectors of a Hypervisor

Even if you manage to create a fully undetected hypervisor and intercept/handle the MMIO TPM commands, you can still be detected.

The primary selling point of the TPM is its Remote Attestation capability, which can attest whether an EK is valid and whether the TPM device is genuine.

### How Remote Attestation Works:

1. Every TPM includes an Endorsement Key (EK) signed by a root EK, which belongs to the TPM vendor. It also includes an Attestation Key (AK). The client sends the TPM EK and AK to a server.
2. The server verifies the EK based on the TPM vendor's root CA certificate. The server generates a random secret and encrypts it, along with the AK, using the EK public key to create a challenge. The server then sends the challenge to the client.
3. The client decrypts the secret with the EK private key and checks the AK. The client then sends the secret back to the server.
4. The server confirms that the client has a genuine TPM.
   
[tpm-attestation](https://github.com/SyncUD/tpm-mmio/assets/109126667/36307325-f330-4e1a-bfe0-4d92acbad789) 
(Source: OpenPower TrustBoot).

## Credits

Code restructured into WDK from EDK2 documentation and sourcecode: https://github.com/tianocore/edk2
Credits to the people @ e&f (mainly everdox & Zepta) for helping me out.


# AdrenoFirmware

Adreno GMU firmware kit. Builds the HFI transport, startup sequence and power votes, and embeds the GMU and SQE firmware.

Current state: GMU cold start and HFI work on sheng hardware. The A8xx performance table (`table_format` 1), the MX dependency votes and the every-bandwidth perfmode rule are checked against mainline in software only (rootstockos, for the Adreno 810).

Built into the kernel with the sheng GPU stack.

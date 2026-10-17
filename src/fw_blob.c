// SPDX-License-Identifier: BSD-3-Clause-Clear
// MediaTek MT7922 RAM firmware; distribution terms in licenses/LICENCE.mediatek.
__asm__(".section __DATA,__const\n"
        ".balign 16\n"
        ".globl _itl_fw_patch\n_itl_fw_patch:\n"
        ".incbin \"fw/WIFI_MT7922_patch_mcu_1_1_hdr.bin\"\n"
        ".globl _itl_fw_patch_end\n_itl_fw_patch_end:\n"
        ".balign 16\n"
        ".globl _itl_fw_ram\n_itl_fw_ram:\n"
        ".incbin \"fw/WIFI_RAM_CODE_MT7922_1.bin\"\n"
        ".globl _itl_fw_ram_end\n_itl_fw_ram_end:\n"
        ".byte 0\n.text\n");

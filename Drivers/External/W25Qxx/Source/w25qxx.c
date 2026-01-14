#include "w25qxx.h"
#include "qspi_cadence.h"
#include <string.h>
#include <stdio.h>

int w25qxx_init(w25qxx_info_t *info, bool want_quad, bool want_4byte_addr)
{
    if (!info) return -1;
    uint8_t id[3] = {0};
    /* 先做一次软件复位，确保器件退出任何连续读/XIP 状态 */
    (void)qspi_software_reset();
    /* 读 JEDEC ID（0x9F） */
    int rc = qspi_read_id(id, 3);
    if (rc != 0) return -2;
    memset(info, 0, sizeof(*info));
    info->manuf_id = id[0];
    info->memory_type = id[1];
    info->capacity = id[2];
    info->page_size = 256u;
    info->sector_size = 4096u;
    info->block_size = 65536u;
    if (id[2] >= 0x14 && id[2] <= 0x26) {
        info->size_bytes = (1u << id[2]);
    }

    /* 若 0x9F 可疑（更鲁棒的判定：非 Winbond(0xEF) 或 容量码不在常见区间(0x14..0x26)），
       则复位并重读；仍异常则回退到 0x90 Manufacturer/Device ID。 */
    bool suspicious = false;
    if ((info->manuf_id != 0xEFu) || !(info->capacity >= 0x14 && info->capacity <= 0x26))
        suspicious = true;
    /* 也将明显错误值视为可疑（历史兼容） */
    if ((info->manuf_id == 0x00u || info->manuf_id == 0xFFu || info->manuf_id == 0x80u) ||
        (info->capacity == 0x00u || info->capacity == 0xFFu))
        suspicious = true;

    if (suspicious)
    {
        (void)qspi_software_reset();
        uint8_t id2[3] = {0};
        if (qspi_read_id(id2, 3) == 0) {
            if (id2[0] != 0x00u && id2[0] != 0xFFu && id2[0] != 0x80u &&
                id2[2] != 0x00u && id2[2] != 0xFFu) {
                info->manuf_id = id2[0];
                info->memory_type = id2[1];
                info->capacity = id2[2];
                if (id2[2] >= 0x14 && id2[2] <= 0x26)
                    info->size_bytes = (1u << id2[2]);
            }
        }
        /* 再判断一次是否仍可疑（非 Winbond 或 容量码越界） */
        if ((info->manuf_id != 0xEFu) || !(info->capacity >= 0x14 && info->capacity <= 0x26))
        {
            uint8_t mfg = 0, dev = 0;
            if (qspi_read_manufacturer_device_id(&mfg, &dev) == 0) {
                info->manuf_id = mfg;
                /* 0x90 返回的 dev 通常等于容量 code（如 W25Q128 为 0x18） */
                info->capacity = dev;
                /* W25Q 系列 memory_type 常见为 0x40；无法区分时设置为通用值以便后续打印 */
                if (info->memory_type == 0u)
                    info->memory_type = 0x40u;
                if (dev >= 0x14 && dev <= 0x26)
                    info->size_bytes = (1u << dev);
            }
        }
    }

    /* 打印 JEDEC ID（与 OpenOCD 风格一致） */
    printf("s300_qspi: JEDEC ID %02x %02x %02x\n", info->manuf_id, info->memory_type, info->capacity);

    /* 如仍无法确定容量，尝试读取 SFDP（仅在目标器件支持时可用）
       这里做一个轻量判定：只读取 SFDP 头并不深入解析 BFPT，以后可完善 */
    if (info->size_bytes == 0u) {
        uint8_t sfdp_hdr[16] = {0};
        if (qspi_read_sfdp(0u, sfdp_hdr, sizeof(sfdp_hdr)) == 0) {
            if (sfdp_hdr[0] == 'S' && sfdp_hdr[1] == 'F' && sfdp_hdr[2] == 'D' && sfdp_hdr[3] == 'P') {
                /* 读取第一个参数表头（通常是 BFPT 0xFF00），并再读若干 DWORD 以后可解析密度；
                   由于实现复杂，这里仅给出占位：后续版本可完善 BFPT 解析。 */
                /* 保持 size_bytes 为 0 表示未知，但其他功能仍可用。 */
            }
        }
    }
    /* Quad Enable */
    if (want_quad)
    {
        (void)qspi_set_quad_enable(true);
        uint8_t sr2 = 0;
        (void)qspi_read_status(NULL, &sr2, NULL);
        info->quad_enabled = !!(sr2 & 0x02u);
    }
    /* 4-byte addressing if size > 16MiB or forced */
    if (want_4byte_addr || info->size_bytes > (16u << 20))
    {
        (void)qspi_set_address_mode_4byte(true);
        info->addr4b = true;
    }
    return 0;
}

int w25qxx_read(uint32_t addr, void *buf, uint32_t len)
{
    return qspi_read(addr, buf, len);
}

int w25qxx_write_page(uint32_t addr, const void *buf, uint32_t len)
{
    if (len > 256u) len = 256u;
    return qspi_page_program(addr, buf, len);
}

int w25qxx_erase_4k(uint32_t addr)
{
    return qspi_erase_4k(addr);
}

int w25qxx_erase_64k(uint32_t addr)
{
    return qspi_erase_64k(addr);
}

int w25qxx_chip_erase(void)
{
    return qspi_chip_erase();
}

/* 扩展 ID 读取功能 */
int w25qxx_read_device_id(uint8_t *dev_id)
{
    return qspi_read_device_id(dev_id);
}

int w25qxx_read_manufacturer_device_id(uint8_t *mfg_id, uint8_t *dev_id)
{
    return qspi_read_manufacturer_device_id(mfg_id, dev_id);
}

int w25qxx_read_unique_id(uint8_t *uid, uint32_t len)
{
    return qspi_read_unique_id(uid, len);
}

int w25qxx_read_sfdp(uint32_t addr, uint8_t *buf, uint32_t len)
{
    return qspi_read_sfdp(addr, buf, len);
}

/* 保留：普通写均使用 STIG 1-1-1，不提供 quad 写接口 */

/* 扩展擦除功能 */
int w25qxx_erase_32k(uint32_t addr)
{
    return qspi_erase_32k(addr);
}

/* 高级功能 */
int w25qxx_software_reset(void)
{
    return qspi_software_reset();
}

/* XIP 1-4-4：仅用于执行/直接 AHB 映射读，不影响 STIG 数据读写 */
int w25qxx_enter_xip_144(unsigned dummy_cycles, uint8_t mode_bits)
{
    /* 进入 XIP 前确保 QE 已打开，且根据容量切换 4B */
    (void)qspi_set_quad_enable(true);
    if (dummy_cycles == 0u) dummy_cycles = 4u; /* 对 0xEB 常见更稳妥的 dummy 个数 */

    /* 为提高通用性，不在此处强制切换 4B；对 16MiB 及以下（如 W25Q128）保持 3B 更稳妥。
       更大容量器件建议先在初始化阶段根据容量显式开启 4B。 */
    unsigned addr_bytes = 3u;
    return qspi_enter_xip_144(addr_bytes, dummy_cycles, mode_bits);
}

void w25qxx_exit_xip(void)
{
    qspi_exit_xip_mode();
}

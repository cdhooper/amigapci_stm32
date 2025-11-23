/*
 * PCI    Version 0.2 2025-11-11
 * -----------------------------
 * Utility to inspect Amiga Firebird PCI configuration space.
 *
 * Copyright 2025 Chris Hooper.  This program and source may be used
 * and distributed freely, for any purpose which benefits the Amiga
 * community. Commercial use of the binary, source, or algorithms requires
 * prior written or email approval from Chris Hooper <amiga@cdh.eebugs.com>.
 * All redistributions must retain this Copyright notice.
 *
 * DISCLAIMER: THE SOFTWARE IS PROVIDED "AS-IS", WITHOUT ANY WARRANTY.
 * THE AUTHOR ASSUMES NO LIABILITY FOR ANY DAMAGE ARISING OUT OF THE USE
 * OR MISUSE OF THIS UTILITY OR INFORMATION REPORTED BY THIS UTILITY.
 */

const char *version = "\0$VER: PCI "VERSION" ("BUILD_DATE") � Chris Hooper";

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <libraries/expansionbase.h>
#include <clib/expansion_protos.h>
#include <inline/exec.h>
#include <inline/expansion.h>
#include <proto/dos.h>
#include <exec/memory.h>
#include <exec/interrupts.h>
#include <exec/execbase.h>
#include <exec/lists.h>

#define ADDR8(x)       (volatile uint8_t *)(x)
#define ADDR16(x)      (volatile uint16_t *)(x)
#define ADDR32(x)      (volatile uint32_t *)(x)

#define ARRAY_SIZE(x)  ((sizeof (x) / sizeof ((x)[0])))
#define BIT(x)         (1U << (x))

#define PCI_MAX_BUS             16

#define ZORRO_MFG_MATAY         0xad47
#define ZORRO_PROD_MATAY_BD     0x0001
#define ZORRO_MFG_E3B           0x0e3b
#define ZORRO_PROD_FIRESTORM    0x00c8

#define FS_PCI_ADDR_CONFIG0     0x1fc00000  // Normal config space
#define FS_PCI_ADDR_CONFIG1     0x1fd00000  // Config1 space
#define FS_PCI_ADDR_IO          0x1fe00000
#define FS_PCI_ADDR_CONTROL     0x1fc08000  // Control (PCI reset) register

#define FS_PCI_CONTROL_NO_RESET 0x80000000  // 0=Bridge in reset
#define FS_PCI_CONTROL_EN_INTS  0x40000000  // 1=Interrupts enabled

#define MAYTAY_PCI_ADDR_CONFIG  0x000f0000

#define PCI_OFF_VENDOR          0x00
#define PCI_OFF_DEVICE          0x02
#define PCI_OFF_CMD             0x04
#define PCI_OFF_STATUS          0x06
#define PCI_OFF_REVISION        0x08
#define PCI_OFF_INTERFACE       0x09
#define PCI_OFF_SUBCLASS        0x0a
#define PCI_OFF_CLASS           0x0b
#define PCI_OFF_CACHELINESIZE   0x0c
#define PCI_OFF_LATENCYTIMER    0x0d
#define PCI_OFF_HEADERTYPE      0x0e
#define PCI_OFF_BIST            0x0f
#define PCI_OFF_BAR0            0x10
#define PCI_OFF_BAR1            0x14
#define PCI_OFF_BAR2            0x18
#define PCI_OFF_BAR3            0x1c
#define PCI_OFF_BAR4            0x20
#define PCI_OFF_BAR5            0x24
#define PCI_OFF_SUBSYSTEM_VID   0x2c
#define PCI_OFF_SUBSYSTEM_DID   0x2e
#define PCI_OFF_ROM_BAR         0x30
#define PCI_OFF_CAP_LIST        0x34
#define PCI_OFF_INT_LINE        0x3c
#define PCI_OFF_INT_PIN         0x3d
#define PCI_OFF_MIN_GNT         0x3e
#define PCI_OFF_MAX_LAT         0x3f

/* Type 1 Configuration Space Header (for PCI bridges) */
#define PCI_OFF_BR_BAR0         0x10
#define PCI_OFF_BR_BAR1         0x14
#define PCI_OFF_BR_PRI_BUS      0x18
#define PCI_OFF_BR_SEC_BUS      0x19
#define PCI_OFF_BR_SUB_BUS      0x1a
#define PCI_OFF_BR_SEC_LATENCY  0x1b
#define PCI_OFF_BR_IO_BASE      0x1c
#define PCI_OFF_BR_IO_LIMIT     0x1d
#define PCI_OFF_BR_SEC_STATUS   0x1e
#define PCI_OFF_BR_W32_BASE     0x20
#define PCI_OFF_BR_W32_LIMIT    0x22
#define PCI_OFF_BR_W64_BASE     0x24
#define PCI_OFF_BR_W64_LIMIT    0x26
#define PCI_OFF_BR_W64_BASE_U   0x28
#define PCI_OFF_BR_W64_LIMIT_U  0x2c
#define PCI_OFF_BR_IO_BASE_U    0x30
#define PCI_OFF_BR_IO_LIMIT_U   0x32
#define PCI_OFF_BR_CAP          0x34
#define PCI_OFF_BR_ROM          0x38
#define PCI_OFF_BR_INT_LINE     0x3c
#define PCI_OFF_BR_INT_PIN      0x3d
#define PCI_OFF_BR_CONTROL      0x3e

#define PCI_STATUS_HAS_CAPS     0x0010  // Device has capabilities list

#define PCI_CLASS_PCI_BRIDGE    0x0604  // PCI-to-PCI bridge

#define FLAG_DUMP               0x00000001
#define FLAG_DDUMP              0x00000002
#define FLAG_VERBOSE            0x00000004
#define FLAG_NO_TRANSLATE       0x00000008

#ifdef __VBCC__
uint32_t swap32(__reg("d0")uint32_t) = "\trol.w\t#8,d0\n\tswap\td0\n\trol.w\t#8,d0\n";
uint16_t swap16(__reg("d0")uint16_t) = "\trol.w\t#8,d0\n";
#else
#define swap16(arg)\
 ({uint16_t __arg = (arg);\
  asm ("ROL.W #8,%0"\
    :"=d" (__arg)\
    :"0" (__arg)\
    :"cc");\
    __arg;})

/* swap long */

#define swap32(arg)\
 ({uint32_t __arg = (arg);\
  asm ("ROL.W #8,%0;\
        SWAP %0;\
        ROL.W #8,%0"\
    :"=d" (__arg)\
    :"0" (__arg)\
    :"cc");\
    __arg;})
#endif

typedef unsigned int uint;
typedef const char * const bits_t;

extern struct ExecBase *SysBase;

static const char * const expansion_library_name = "expansion.library";

APTR     bridge_pci0_base;
APTR     bridge_pci1_base;
APTR     bridge_io_base;
APTR     bridge_mem_base;
APTR     bridge_control_reg;
uint8_t  bridge_is_firestorm = 0;
uint16_t bridge_zorro_mfg;
uint16_t bridge_zorro_prod;
struct ConfigDev *zorro_cdev = NULL;

/* PCI Command (0x4) */
static bits_t bits_pci_command[] = {
    "En_IO", "En_Memory", "En_BusMaster", "En_Special",
    "En_MWI", "En_Snoop", "En_Parity", "En_AddrStep",
    "En_SERR#", "En_FastBtoB", "", "",
    "", "", "", "",
};

/* PCI Primary Status (0x6) */
static bits_t bits_pci_status_primary[] = {
    "", "", "",
    "Int Pending",
    "Has Cap List",
    "66 MHz Capable",
    "UDF Support",
    "Fast Back-to-Back Capable",
    "Master Data Parity Error",
    NULL,
    "DEVSEL:0=fast,1=medium,2=slow,3=reserved",
    "Signaled Target Abort",
    "Received Target Abort",
    "Received Master Abort",
    "Signaled System Error",
    "Detected Parity Error",
};

/* PCI Max_Lat, Min_Gnt, Interrupt Pin, Interrupt Line (0x3c) */
static bits_t bits_pci_lat_gnt_int[] = {
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, "Line",
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, "Pin:1=INTA#,2=INTB#,3=INTC#,4=INTD#",
};

static APTR
find_zorro_pci_bridge(uint bus)
{
    struct Library        *ExpansionBase;
    struct ConfigDev      *cdev = NULL;
    APTR                   base = NULL;
    uint                   curbus = 0;

    if ((ExpansionBase = OpenLibrary(expansion_library_name, 0)) == 0) {
        printf("Could not open %s\n", expansion_library_name);
        return (NULL);
    }
    bridge_zorro_mfg  = ZORRO_MFG_MATAY;
    bridge_zorro_prod = ZORRO_PROD_MATAY_BD;
    for (curbus = 0; curbus < 8; curbus++) {
        cdev = FindConfigDev(cdev, bridge_zorro_mfg, bridge_zorro_prod);
        if (cdev == NULL)
            break;
        if (curbus == bus) {
            base = cdev->cd_BoardAddr;
            bridge_pci0_base = base + MAYTAY_PCI_ADDR_CONFIG;
            bridge_pci1_base = base + MAYTAY_PCI_ADDR_CONFIG;
            bridge_io_base = base;
            bridge_mem_base = base;
            bridge_control_reg = NULL;
            bridge_is_firestorm = 0;
            goto done;
        }
    }
    bridge_zorro_mfg  = ZORRO_MFG_E3B;
    bridge_zorro_prod = ZORRO_PROD_FIRESTORM;
    for (; curbus < 8; curbus++) {
        cdev = FindConfigDev(cdev, bridge_zorro_mfg, bridge_zorro_prod);
        if (cdev == NULL)
            break;
        if (curbus == bus) {
            base = cdev->cd_BoardAddr;
            bridge_pci0_base = base + FS_PCI_ADDR_CONFIG0;
            bridge_pci1_base = base + FS_PCI_ADDR_CONFIG1;
            bridge_io_base = base + FS_PCI_ADDR_IO;
            bridge_mem_base = base;
            bridge_control_reg = base + FS_PCI_ADDR_CONTROL;
            bridge_is_firestorm++;
            break;
        }
    }

done:
    zorro_cdev = cdev;
    CloseLibrary(ExpansionBase);

    return (base);
}

static void
zorro_show_mfgprod(void)
{
    if (bridge_zorro_mfg == ZORRO_MFG_MATAY) {
        printf(" Matay");
        if (bridge_zorro_prod == ZORRO_PROD_MATAY_BD)
            printf(" Prometheus");
    } else if (bridge_zorro_mfg == ZORRO_MFG_E3B) {
        printf(" E3B");
        if (bridge_zorro_prod == ZORRO_PROD_FIRESTORM)
            printf(" Firestorm");
    } else {
        printf("Unknown\n");
    }
}

static APTR
pci_cfg_base(uint bus, uint dev, uint func, uint off)
{
    if (bus == 0)
        return (bridge_pci0_base + (0x10000 << dev) + (func << 8) + off);
    return (bridge_pci1_base + (bus << 16) + (dev << 11) + (func << 8) + off);
}

uint8_t
pci_read8(uint bus, uint dev, uint func, uint off)
{
    return (*ADDR8(pci_cfg_base(bus, dev, func, off)));
}

uint16_t
pci_read16(uint bus, uint dev, uint func, uint off)
{
    return (swap16(*ADDR16(pci_cfg_base(bus, dev, func, off))));
}

uint32_t
pci_read32(uint bus, uint dev, uint func, uint off)
{
    return (swap32(*ADDR32(pci_cfg_base(bus, dev, func, off))));
}

static void
pci_write32(uint bus, uint dev, uint func, uint off, uint32_t value)
{
    *ADDR32(pci_cfg_base(bus, dev, func, off)) = swap32(value);
}

uint32_t
pci_read32v(uint bus, uint dev, uint func, uint off, uint *diffs)
{
    APTR     base = pci_cfg_base(bus, dev, func, off);
    uint32_t rval = *ADDR32(base);
    uint32_t nval;

    while (rval != (nval = *ADDR32(base))) {
        rval = nval;
        (*diffs)++;
        if (*diffs > 5)
            break;
    }
    return (swap32(rval));
}

static const struct {
    uint16_t          pv_vendor;
    const char *const pv_name;
} pci_vendors[] = {
    { 0x1172, "Altera"        },
    { 0x1b21, "ASMedia"       },
    { 0x1a03, "ASPEED"        },
    { 0x1002, "ATI"           },
    { 0x168c, "Atheros"       },
    { 0x1969, "Attansic"      },
    { 0x14e4, "Broadcom"      },
    { 0x121a, "3Dfx"          },
    { 0x103c, "HP"            },
    { 0x111d, "IDT"           },
    { 0x8086, "Intel"         },
    { 0x197b, "JMicron"       },
    { 0x1000, "LSI"           },
    { 0x11ab, "Marvell"       },
    { 0x1b4b, "Marvell"       },
    { 0x102b, "Matrox"        },
    { 0x1033, "NEC"           },
    { 0x10de, "nVidia"        },
    { 0x10b5, "PLX"           },
    { 0x11f8, "PMC-Sierra"    },
    { 0x1077, "QLogic"        },
    { 0x10ec, "Realtek"       },
    { 0x1912, "Renesas"       },
    { 0x1095, "SiliconImage"  },
    { 0x1106, "VIA"           },
    { 0x10ee, "Xilinx"        },
};

static const struct {
    uint16_t          pv_vendor;
    uint16_t          pv_device;
    const char *const pv_name;
} pci_devices[] = {
    { 0x121a, 0x0005, "Voodoo3" },
    { 0x121a, 0x0036, "Voodoo3 2000" },
    { 0x121a, 0x0057, "Voodoo3 3000" },
};

static const struct {
    uint32_t          pc_class_value;
    const char *const pc_name;
} pci_classes[] = {
    { 0x000000, "Legacy" },
    { 0x000100, "VGA" },
    { 0x010000, "SCSI" },
    { 0x010100, "IDE" },
    { 0x010200, "Floppy" },
    { 0x010300, "IPI" },
    { 0x010400, "RAID" },
    { 0x010520, "ATA ADMA SS" },
    { 0x010530, "ATA ADMA Continuous" },
    { 0x010600, "SATA" },
    { 0x010601, "SATA AHCI" },
    { 0x010602, "SATA SSBI" },
    { 0x010700, "SAS" },
    { 0x010701, "SAS SSBI" },
    { 0x010800, "NVM" },
    { 0x010801, "NVM HCI" },
    { 0x010802, "NVMe" },
    { 0x018000, "Other Mass Storage" },
    { 0x020000, "Ethernet" },
    { 0x020100, "Token Ring" },
    { 0x020200, "FDDI" },
    { 0x020300, "ATM" },
    { 0x020400, "ISDN" },
    { 0x020500, "WordFip" },
    { 0x020600, "PICMG" },
    { 0x028000, "Other Network" },
    { 0x030000, "VGA" },
    { 0x030001, "XGA" },
    { 0x030002, "3D" },
    { 0x038000, "Other display" },
    { 0x040000, "Video" },
    { 0x040100, "Audio" },
    { 0x040200, "Telephony" },
    { 0x040300, "HD Audio" },
    { 0x048000, "Other Multimedia" },
    { 0x050000, "RAM" },
    { 0x050100, "Flash" },
    { 0x058000, "Other Memory" },
    { 0x060000, "Host bridge" },
    { 0x060100, "ISA bridge" },
    { 0x060200, "EISA bridge" },
    { 0x060300, "MCA bridge" },
    { 0x060400, "PCI bridge" },
    { 0x060401, "PCI SD bridge" },
    { 0x060500, "PCIMCIA bridge" },
    { 0x060600, "NuBus bridge" },
    { 0x060700, "CardBus bridge" },
    { 0x060800, "RACEway bridge" },
    { 0x060940, "Semi-transparent bridge" },
    { 0x060980, "Semi-transparent reverse bridge" },
    { 0x060a00, "InfiniBand-PCI bridge" },
    { 0x060b00, "ASI-PCI bridge" },
    { 0x060b01, "ASI-PCI portal bridge" },
    { 0x068000, "Other bridge" },
    { 0x070000, "Serial" },
    { 0x070001, "16450 Serial" },
    { 0x070002, "16550 Serial" },
    { 0x070003, "16650 Serial" },
    { 0x070004, "16750 Serial" },
    { 0x070005, "16850 Serial" },
    { 0x070006, "16950 Serial" },
    { 0x070100, "Parallel" },
    { 0x070101, "BiDir Parallel" },
    { 0x070102, "ECP 1.x Parallel" },
    { 0x070103, "IEEE1284" },
    { 0x0701fe, "IEEE1284 target" },
    { 0x070200, "Multiport serial" },
    { 0x070300, "Modem" },
    { 0x070301, "Hayes 16450 model" },
    { 0x070302, "Hayes 16550 model" },
    { 0x070303, "Hayes 16650 model" },
    { 0x070304, "Hayes 16750 model" },
    { 0x070400, "GPIB IEEE488.1/2" },
    { 0x070500, "Smart Card" },
    { 0x078000, "Communication" },
    { 0x080000, "8259 PIC" },
    { 0x080001, "ISA PIC" },
    { 0x080002, "EISA PIC" },
    { 0x080010, "IOAPIC" },
    { 0x080020, "IOxAPIC" },
    { 0x080100, "8237 DMA" },
    { 0x080101, "ISA DMA" },
    { 0x080102, "EISA DMA" },
    { 0x080200, "8254 timer" },
    { 0x080201, "ISA timer" },
    { 0x080202, "EISA timers" },
    { 0x080203, "HP timer" },
    { 0x080300, "RTC" },
    { 0x080301, "ISA RTC" },
    { 0x080400, "PCI HP Controller" },
    { 0x080500, "SD Host" },
    { 0x080600, "IOMMU" },
    { 0x088000, "Other system peripheral" },
    { 0x090000, "Keyboard" },
    { 0x090100, "Digitizer" },
    { 0x090200, "Mouse" },
    { 0x090300, "Scanner" },
    { 0x090400, "Gameport" },
    { 0x098000, "Input Controller" },
    { 0x0a0000, "Docking Station" },
    { 0x0b0000, "386" },
    { 0x0b0100, "486" },
    { 0x0b0200, "Pentium" },
    { 0x0b1000, "Alpha" },
    { 0x0b2000, "PowerPC" },
    { 0x0b3000, "MIPS" },
    { 0x0b4000, "Co-Processor" },
    { 0x0b8000, "Other Processor" },
    { 0x0c0000, "1394 Firewire" },
    { 0x0c0010, "1394 Firewire OHCI" },
    { 0x0c0100, "ACCESS.bus" },
    { 0x0c0200, "SSA" },
    { 0x0c0300, "USB UHCI" },
    { 0x0c0301, "USB OHCI" },
    { 0x0c0310, "USB2 OHCI" },
    { 0x0c0320, "USB2 EHCI" },
    { 0x0c0380, "USB2" },
    { 0x0c03fe, "USB device" },
    { 0x0c0400, "Fibre Channel" },
    { 0x0c0500, "SMBus" },
    { 0x0c0600, "InfiniBand" },
    { 0x0c0700, "IPMI SMIC" },
    { 0x0c0701, "IPMI Keyboard" },
    { 0x0c0702, "IPMI Block Device" },
    { 0x0c0800, "SERCOS" },
    { 0x0c0900, "CANbus" },
    { 0x0c8000, "Serial Bus" },
    { 0x0d0000, "iRDA" },
    { 0x0d0100, "Consumer IR" },
    { 0x0d0110, "UWB Radio" },
    { 0x0d1000, "Wireless" },
    { 0x0d1100, "Bluetooth" },
    { 0x0d1200, "Broadband" },
    { 0x0d2000, "Ethernet 5 GHz" },
    { 0x0d2100, "Ethernet 2.4 GHz" },
    { 0x0d8000, "Other Wireless" },
    { 0x0e0000, "IIO" },
    { 0x0f0100, "Satt. TV" },
    { 0x0f0200, "Satt. Audio" },
    { 0x0f0300, "Satt. Voice" },
    { 0x0f0400, "Satt. Data" },
    { 0x0f8000, "Other Satt." },
    { 0x100000, "Network/compute crypto" },
    { 0x101000, "Entertainment crypto" },
    { 0x108000, "Other crypto" },
    { 0x110000, "DPIO" },
    { 0x110100, "Performance Counter" },
    { 0x111000, "Comm Sync" },
    { 0x112000, "Management" },
    { 0x118000, "Misc Acq/Signal Proc" },
};

const char *
pci_vendor(uint16_t vendor)
{
    uint pos;
    for (pos = 0; pos < ARRAY_SIZE(pci_vendors); pos++) {
        if (pci_vendors[pos].pv_vendor == vendor) {
            return (pci_vendors[pos].pv_name);
        }
    }
    return ("Unknown");
}

static void
pci_show_vendordevice(uint16_t vendor, uint16_t device)
{
    uint pos;
    for (pos = 0; pos < ARRAY_SIZE(pci_vendors); pos++)
        if (pci_vendors[pos].pv_vendor == vendor)
            break;

    if (pos >= ARRAY_SIZE(pci_vendors))
        return;

    printf(" %s", pci_vendors[pos].pv_name);

    for (pos = 0; pos < ARRAY_SIZE(pci_devices); pos++)
        if ((pci_devices[pos].pv_vendor == vendor) &&
            (pci_devices[pos].pv_device == device)) {
            printf(" %s", pci_devices[pos].pv_name);
            return;
        }

    printf(" %04x", device);
}

static void
pci_show_class(uint32_t classrev)
{
    uint        pos;
    uint        class_value = (uint) (classrev >> 8);
    const char *pc_base    = NULL;

    for (pos = 0; pos < ARRAY_SIZE(pci_classes); pos++) {
        uint cv = pci_classes[pos].pc_class_value;
        if (((cv & 0xff) == 0) &&
            ((class_value >> 8) == (cv >> 8))) {
            pc_base = pci_classes[pos].pc_name;
        }
        if (class_value == cv) {
            printf(" %s", pci_classes[pos].pc_name);
            return;
        }
    }

    printf(" %s", pc_base);
}

static const struct {
    uint        dwords;
    const char *name;
} pci_caps[] = {
    [0x00] = { 0x01, "NULL" },
    [0x01] = { 0x02, "Power Management" },
    [0x02] = { 0x01, "Accelerated Graphics Port (AGP)" },
    [0x03] = { 0x02, "Vital Product Data (VPD)" },
    [0x04] = { 0x01, "Slot Identification" },
    [0x05] = { 0x03, "Message Signaled Interrupts (MSI)" },
    [0x06] = { 0x01, "CompactPCI Hotswap" },
    [0x07] = { 0x01, "PCI-X" },
    [0x08] = { 0x01, "HyperTransport" },
    [0x09] = { 0x08, "Vendor-specific" },
    [0x0a] = { 0x01, "Debug Port" },
    [0x0b] = { 0x01, "CompactPCI Central Resource" },
    [0x0c] = { 0x01, "Standard Hotplug Controller" },
    [0x0d] = { 0x02, "Bridge subsystem vendor/device ID" },
    [0x0e] = { 0x01, "AGP Target bridge" },
    [0x0f] = { 0x01, "Secure Device" },
    [0x10] = { 0x0d, "PCI Express" },
    [0x11] = { 0x03, "MSI-X" },
    [0x12] = { 0x01, "SATA Data/Index Config" },
    [0x13] = { 0x02, "Advanced Features" },
};

static void
pci_show_cap(uint bus, uint dev, uint func, uint32_t cap_pos, uint32_t value)
{
    uint8_t cap = value & 0xff;
    uint    num_words;
    uint    word;

    printf("      PCI Cap - %02x ", cap);
    if (cap < ARRAY_SIZE(pci_caps)) {
        printf("%s", pci_caps[cap].name);
        num_words = pci_caps[cap].dwords;
    } else {
        printf("Unknown");
        num_words = 1;
    }
    for (word = 0; word < num_words; word++) {
        if ((word & 0x7) == 0) {
            printf("\n          [%02x]", word);
        }
        if (word > 0)
            value = pci_read32(bus, dev, func, cap_pos + word * 4);
        printf(" %08x", value);
    }
    printf("\n");
}

static void
pci_show_caps(uint bus, uint dev, uint func)
{
    uint8_t cap_pos = pci_read8(bus, dev, func, PCI_OFF_CAP_LIST);
    while ((cap_pos > PCI_OFF_CAP_LIST) && (cap_pos <= 0xfc)) {
        uint32_t value = pci_read32(bus, dev, func, cap_pos);
        pci_show_cap(bus, dev, func, cap_pos, value);
        cap_pos = (value >> 8) & 0xfc;
    }
}

void
print_bits(uint32_t value, uint mode, uint maxmode, bits_t bits[])
{
    uint     i;
    uint     bitvalue;
    uint     num_bits   = mode * 8;
    uint     count      = 0;
    uint     subvalue   = 0;
    uint     accumulate = 0;
    uint     zeros      = mode * 2;
    uint     spaces     = maxmode * 2 - zeros;
    uint32_t mask       = ((1UL << (mode * 8)) - 1);
    uint32_t svalue     = value;

    /* Show header */
    if (spaces > 8)
        spaces = 0;

    /* Mask off high bits */
    if (mask == 0)
        mask = (uint32_t) -1;
    value &= mask;

    printf("[%0*lx]%*s ", zeros, (unsigned long) value, spaces, "");

    for (i = 0; i < num_bits; i++) {
        bitvalue  = svalue & 1;
        subvalue |= (bitvalue << accumulate);
        svalue  >>= 1;
        if (bits == NULL || bits[i] == NULL) {
            accumulate++;
            continue;  // Bit is NULL -- remember this value for next bit
        }
        if (bits[i][0] == '\0') {
            subvalue   = 0;
            accumulate = 0;
            continue;  // Empty string -- don't show or accumulate
        } else if (strchr(bits[i], '=') != NULL) {
            accumulate++;
        }
        if ((bitvalue == 0) && (accumulate == 0))
            continue;  // Bit not set, and no accumulated value -- don't show
        if (count++ > 0) {
            /* Comma separate */
            printf(", ");
        }
        if (accumulate > 0) {
            const char *key = strchr(bits[i], ':');
            if (key == NULL) {
                /* Simple "<name>=<value>" */
                if (bits[i][0] == '\0')
                    printf("%x", subvalue);
                else
                    printf("%s=%x", bits[i], subvalue);
            } else {
                /* Complex "<name>=<string>" from lookup */
                const    char *tmp = NULL;
                const    char *name_end = key;
                uint32_t key_value;
                /*
                 * Format of bits[] in this case is:
                 *    "<name>:<val0>=<name0>,<val1>=<name1>,..."
                 *
                 * Example:
                 *    ...
                 *    NULL,
                 *    "speed:0=low,1=mid,2=high,3=reserved"
                 *    ...
                 */
                key++;
                while (*key != '\0') {
                    if (*key == '?')  // Catch-all is "?=string"
                        break;
                    if (sscanf(key, "%x=", &key_value) != 1) {
                        key = NULL;
                        break;
                    }
                    if (key_value == subvalue)
                        break;
                    tmp = strchr(key, ',');      // Go to next possible match
                    if (tmp == NULL) {
                        key = NULL;
                        break;  // No more possible matches -- just show number
                    }
                    key = tmp + 1;
                }
                if (key != NULL)
                    tmp = strchr(key, '=');
                else
                    tmp = NULL;
                if (tmp != NULL) {
                    /* Found equals -- string to display follows this */
                    key = tmp + 1;
                    tmp = strchr(tmp, ',');      // Stop output at comma
                    if (tmp == NULL)             // ...or end of string
                        tmp = key + strlen(key);

                    if ((int) (tmp - key) > 0) {
                        /* Have string to display */
                        if (name_end > bits[i]) {
                            /* More than just equals sign */
                            printf("%.*s=", (int) (name_end - bits[i]), bits[i]);
                        }
                        printf("%.*s", (int) (tmp - key), key);
                    } else if (count > 0) {
                        /* Back up -- nothing to display this time */
                        count--;
                    }
                } else {
                    /* No equals or no match -- just show number */
                    if (name_end > bits[i]) {
                        /* More than just equals sign */
                        printf("%.*s=", (int) (name_end - bits[i]), bits[i]);
                    }
                    printf("0x%x", subvalue);
                }
            }
        } else {
            printf("%s", bits[i]);
        }
        subvalue   = 0;
        accumulate = 0;
    }
#if 0
    if (bits == NULL)
        printf("[%0*lx]%*s\n", zeros, (unsigned long) value, spaces, "");
    else if (count > 0)
#endif
    printf("\n");
}

static void
lspci(uint flags)
{
    uint    bus;
    uint    dev = 0;
    uint    func = 0;
    uint    off;
    uint    maxbus = PCI_MAX_BUS;

    if (find_zorro_pci_bridge(0) == NULL)
        return;

    printf("B.D.F Vend.Dev  _BAR_ ____Base____ ____Size____ Description\n");
    printf("%x     %04x.%04x Zorro %12x %12x",
           0, bridge_zorro_mfg, bridge_zorro_prod,
           (uint32_t) zorro_cdev->cd_BoardAddr,
           (uint32_t) zorro_cdev->cd_BoardSize);
    zorro_show_mfgprod();
    printf("\n");

    for (bus = 0; bus <= maxbus; bus++) {
        uint maxdev = 0x1f;
        if (bus == 0)
            maxdev = 4;

        for (dev = 0; dev < maxdev; dev++) {
            uint32_t tbase;
            if (flags & FLAG_VERBOSE) {
                if (bus == 0) {
                    if (flags & FLAG_NO_TRANSLATE)
                        tbase = 0;
                    else
                        tbase = (uint32_t) bridge_pci0_base;
                    printf("%26s%08x%13x PCI Config 0.%u.0 (Slot %u)\n", "",
                           tbase + (0x10000 << dev), 0x100, dev, dev);
                } else {
                    if (flags & FLAG_NO_TRANSLATE)
                        tbase = 0;
                    else
                        tbase = (uint32_t) bridge_pci1_base;
                    printf("%26s%08x%13x PCI Config %u.%u.0\n", "",
                           tbase + (bus << 16) + (dev << 11), 0x100, bus, dev);
                }
            }

            for (func = 0; func < 8; func++) {
                uint16_t vendor = pci_read16(bus, dev, func, PCI_OFF_VENDOR);
                uint16_t device = pci_read16(bus, dev, func, PCI_OFF_DEVICE);
                uint32_t classrev;
                uint     printed;
                uint     bar;
                uint     maxbar = 6;

                if (((vendor == 0xffff) && (device == 0xffff)) ||
                    ((vendor == 0x0000) && (device == 0x0000))) {
                    if (bus > 0) {
                        if (func == 0) {
                            if ((dev == 0) && (maxbus == PCI_MAX_BUS))
                                maxbus = bus + 1;  // Just probe 1 more bus
                            dev = maxdev;
                        }
                    }
                    break;
                }

                classrev = pci_read32(bus, dev, func, PCI_OFF_REVISION);
                if ((classrev >> 16) == PCI_CLASS_PCI_BRIDGE)
                    maxbar = 2;
                printf("%x.%x.%x %04x.%04x", bus, dev, func, vendor, device);
                printed = 0;
                for (bar = 0; bar < maxbar; bar++) {
                    uint32_t base;
                    uint32_t pbase;
                    uint32_t size;
                    uint     diffs = 0;
                    const char *btype;

#if 0
readXXv function reads up to 5 times until two reads in a row match
writeXXv function writes then reads back (up to 5 times) until read data matches written
#endif
                    base = pci_read32v(bus, dev, func, PCI_OFF_BAR0 + bar * 4,
                                       &diffs);
                    Disable();  // Disable interrupts
                    pci_write32(bus, dev, func, PCI_OFF_BAR0 + bar * 4,
                                0xffffffff);
                    size = pci_read32(bus, dev, func, PCI_OFF_BAR0 + bar * 4);
                    pci_write32(bus, dev, func, PCI_OFF_BAR0 + bar * 4, base);
                    Enable();  // Enable interrupts
                    pbase = base;
                    if (pbase & BIT(0)) {
                        /* I/O space */
                        btype = "I/O";
                        base &= ~0x3;
                        size &= ~0x3;
                        if ((flags & FLAG_NO_TRANSLATE) == 0)
                            base += (uint32_t) bridge_io_base;
                    } else {
                        /* Memory space */
                        btype = "M32";
                        if ((pbase & (BIT(2) | BIT(1))) == BIT(2))
                            btype = "M64";
                        base &= ~0xf;
                        size &= ~0xf;
                        if ((flags & FLAG_NO_TRANSLATE) == 0)
                            base += (uint32_t) bridge_mem_base;
                    }
                    if (size == 0)
                        continue; // Not a writeable BAR
                    size &= (0 - size);
                    if (printed++ != 0)
                        printf("%15s", "");
                    printf(" %x %s", bar, btype);
                    if (((pbase & 1) == 0) &&
                        ((pbase & (BIT(2) | BIT(1))) == BIT(2))) {
                        uint32_t basehigh;
                        uint32_t sizehigh = 0;
                        bar++;
                        basehigh = pci_read32(bus, dev, func,
                                              PCI_OFF_BAR0 + bar * 4);
                        if (size == 0) {
                            Disable();  // Disable interrupts
                            pci_write32(bus, dev, func, PCI_OFF_BAR0 + bar * 4,
                                        0xffffffff);
                            sizehigh = pci_read32(bus, dev, func,
                                                  PCI_OFF_BAR0 + bar * 4);
                            pci_write32(bus, dev, func, PCI_OFF_BAR0 + bar * 4,
                                        basehigh);
                            Enable();  // Enable interrupts
                            sizehigh &= (0 - sizehigh);
                        }
                        printf(" %03x:%08x %03x:%08x",
                               basehigh, base, sizehigh, size);
                    } else {
                        printf(" %12x %12x", base, size);
                    }
                    if (printed == 1) {  // First line
                        pci_show_vendordevice(vendor, device);
                        pci_show_class(classrev);
                    }
                    printf("\n");
                }
// XXX: Show Expansion ROM
                if ((classrev >> 16) != PCI_CLASS_PCI_BRIDGE) {
                    uint32_t base;
                    uint32_t size;
                    uint     diffs = 0;
                    base = pci_read32v(bus, dev, func, PCI_OFF_ROM_BAR, &diffs);
                    Disable();  // Disable interrupts
                    pci_write32(bus, dev, func, PCI_OFF_ROM_BAR, 0xffffffff);
                    size = pci_read32(bus, dev, func, PCI_OFF_ROM_BAR);
                    pci_write32(bus, dev, func, PCI_OFF_ROM_BAR, base);
                    Enable();  // Enable interrupts
                    if (size & 1) {
                        /* BAR exists */
                        size &= ~1;
                        size &= (0 - size);
                        if ((flags & FLAG_NO_TRANSLATE) == 0)
                            base += (uint32_t) bridge_mem_base;
                        if (printed++ != 0)
                            printf("%15s", "");
                        if (base & 1)
                            printf("   ROM %12x %12x\n", base & ~1, size);
                        else
                            printf("   ROM %12s %12x\n", "-", size);
                    }
                }
                if ((classrev >> 16) == PCI_CLASS_PCI_BRIDGE) {
                    uint32_t temp;
                    uint32_t base;
                    uint32_t limit;
                    uint32_t size;
                    temp = pci_read32(bus, dev, func, PCI_OFF_BR_IO_BASE);
                    base = (temp & 0xf0) << 8;
                    limit = (temp & 0xf000) + 0x1000;
                    if (temp & 1) {
                        /* 32-bit IO window */
                        temp = pci_read32(bus, dev, func,
                                          PCI_OFF_BR_IO_BASE_U);
                        base  += ((temp & 0x0000ffff) << 16);
                        limit += (temp & 0xffff0000);
                        if (limit <= base)
                            size = 0;
                        else
                            size = limit - base;
                        if ((flags & FLAG_NO_TRANSLATE) == 0)
                            base += (uint32_t) bridge_io_base;
                        printf("%21s     %08x     %08x\n", "WIO",
                               base, size);
                    } else {
                        if (limit <= base)
                            size = 0;
                        else
                            size = limit - base;
                        if ((flags & FLAG_NO_TRANSLATE) == 0)
                            base += (uint32_t) bridge_io_base;
                        printf("%21s     %8x     %8x\n",
                               "WIO", base, size);
                    }
                    temp = pci_read32(bus, dev, func, PCI_OFF_BR_W32_BASE);
                    base  = ((temp & 0x0000fff0) << 16);
                    limit = ((temp & 0xfff00000U)) + 0x00100000;
                    if (limit <= base)
                        size = 0;
                    else
                        size = limit - base;
                    if ((flags & FLAG_NO_TRANSLATE) == 0)
                        base += (uint32_t) bridge_mem_base;
                    printf("%21s     %08x     %08x\n", "W32", base, size);

                    temp = pci_read32(bus, dev, func, PCI_OFF_BR_W64_BASE);
                    base  = ((temp & 0x0000fff0) << 16);
                    limit = ((temp & 0xfff00000U)) + 0x00100000;
                    if (limit <= base)
                        size = 0;
                    else
                        size = limit - base;
                    if ((flags & FLAG_NO_TRANSLATE) == 0)
                        base += (uint32_t) bridge_mem_base;
                    if (temp & 0x1) {
                        /* 64-bit prefetchable window */
                        uint32_t base_u;
                        uint32_t limit_u;
                        uint32_t size_u;
                        base_u = pci_read32(bus, dev, func,
                                            PCI_OFF_BR_W64_BASE_U);
                        limit_u = pci_read32(bus, dev, func,
                                             PCI_OFF_BR_W64_LIMIT_U);
                        if ((flags & FLAG_NO_TRANSLATE) == 0)
                            base += (uint32_t) bridge_mem_base;
                        if (base < (uint32_t) bridge_mem_base) // wrapped
                            base_u++;
                        if (limit_u <= base_u)
                            size_u = 0;
                        else
                            size_u = limit_u - base_u;
                        printf("%21s %03x:%08x %03x:%08x\n",
                               "W64", base_u, base, size_u, size);
                    } else {
                        /* 32-bit prefetchable window */
                        printf("%21s     %08x     %08x\n", "W64",
                               base, size);
                    }
                    if ((classrev >> 16) == PCI_CLASS_PCI_BRIDGE) {
                        uint32_t sub = pci_read32(bus, dev, func,
                                                  PCI_OFF_BR_PRI_BUS);
                        if (flags & FLAG_VERBOSE) {
                            printf("      Bus %02x  SecBus %02x  SubBus %02x\n",
                               (uint8_t) sub, (uint8_t) (sub >> 8),
                               (uint8_t) (sub >> 16));
                        }
                        sub >>= 16;
                        sub &= 0xff;
                        if (maxbus < sub)
                            maxbus = sub;
                    }
                }
                if (flags & FLAG_VERBOSE) {
                    uint16_t status;
                    uint32_t subsys;
                    subsys = pci_read32(bus, dev, func, PCI_OFF_SUBSYSTEM_VID);
                    if ((subsys != 0) && (subsys != 0xffffffff)) {
                        printf("      %04x.%04x PCI Subsystem  ",
                               (uint16_t) subsys, subsys >> 16);
                        pci_show_vendordevice((uint16_t) subsys, subsys >> 16);
                        printf("\n");
                    }
                    printf("      CMD       ");
                    print_bits(pci_read16(bus, dev, func, PCI_OFF_CMD),
                               2, 2, bits_pci_command);
                    printf("      STATUS    ");
                    status = pci_read16(bus, dev, func, PCI_OFF_STATUS);
                    print_bits(status, 2, 2, bits_pci_status_primary);
                    printf("      Interrupt ");
                    print_bits(pci_read32(bus, dev, func, PCI_OFF_INT_LINE),
                               2, 2, bits_pci_lat_gnt_int);

                    if (status & PCI_STATUS_HAS_CAPS) {
                        pci_show_caps(bus, dev, func);
                    }
                }
                if (flags & FLAG_DUMP) {
                    uint omax = 64;
                    if (flags & FLAG_DDUMP)
                        omax = 256;
                    for (off = 0; off < omax; off++) {
                        if ((off & 0x0f) == 0)
                            printf("      [%02x]", off);
                        printf(" %02x", pci_read8(bus, dev, func, off));
                        if ((off & 0x0f) == 0x0f)
                            printf("\n");
                    }
                }

                if (func == 0) {
                    uint8_t htype = pci_read8(bus, dev, func, PCI_OFF_HEADERTYPE);
                    if ((htype & BIT(7)) == 0)
                        break;  // Not a multifunction device
                }
            }
        }
    }
}

#define FLAG_ZBRIDGE_RESET 0x01

static void
pci_zbridge_control(int pci_reset_bus, uint flags)
{
    int bus;
    for (bus = 0; bus < PCI_MAX_BUS; bus++) {
        if ((pci_reset_bus != -1) && (pci_reset_bus != bus))
            continue;
        if (find_zorro_pci_bridge(bus) == NULL) {
            if (pci_reset_bus != -1)
                printf("Could not find bus %d\n", pci_reset_bus);
            break;
        }
        if (bridge_control_reg == NULL) {
            if (pci_reset_bus != -1)
                printf("Bus %d does not have reset capability\n",
                       pci_reset_bus);
            continue;
        }
        if (flags & FLAG_ZBRIDGE_RESET) {
            *ADDR32(bridge_control_reg) &= ~FS_PCI_CONTROL_NO_RESET;
            Delay(1);
            *ADDR32(bridge_control_reg) |= FS_PCI_CONTROL_NO_RESET |
                                           FS_PCI_CONTROL_EN_INTS;
            Delay(1);
        }
    }
}


int
main(int argc, char **argv)
{
    int arg;
    int  pci_reset_bus_num  = -1;
    uint flag_pci_ls        = 0;
    uint flag_pci_status    = 0;
    uint flag_pci_reset     = 0;
    uint flags         = 0;

    for (arg = 1; arg < argc; arg++) {
        char *ptr = argv[arg];
        if (*ptr == '-') {
            while (*(++ptr) != '\0') {
                switch (*ptr) {
                    case 'd':
                        if (flags & FLAG_DUMP)
                            flags |= FLAG_DDUMP;
                        flags |= FLAG_DUMP;
                        break;
                    case 'l':
                        flag_pci_ls++;
                        break;
                    case 'n':
                        flags |= FLAG_NO_TRANSLATE;
                        break;
                    case 's':
                        flag_pci_status++;
                        break;
                    case 'v':
                        flags |= FLAG_VERBOSE;
                        break;
                    default:
                        goto usage;
                }
            }
        } else if (strcmp(ptr, "ls") == 0) {
            flag_pci_ls++;
        } else if (strcmp(ptr, "reset") == 0) {
            flag_pci_reset++;
        } else if (strcmp(ptr, "status") == 0) {
            flag_pci_status++;
        } else if (flag_pci_reset && (pci_reset_bus_num == -1)) {
            pci_reset_bus_num = atoi(ptr);
        } else {
usage:
            printf("Options:\n"
//                 "pci enable [bus] - enable Zorro-PCI bridge\n"
                   "pci ls           - show all PCI devices\n"
                   "pci ls -d        - show raw config space bytes\n"
                   "pci ls -n        - "
                        "do not translate shown addresses to be CPU-relative\n"
                   "pci ls -v        - decode config registers\n"
                   "pci reset [bus]  - reset PCI\n"
//                 "pci status       - show PCI status registers\n"
                   );
            exit(1);
        }
    }
    if (find_zorro_pci_bridge(0) == 0) {
        printf("Could not find Zorro-PCI Bridge\n");
        exit(1);
    }
    if ((flag_pci_ls | flag_pci_status | flag_pci_reset) == 0)
        flag_pci_ls++;

    if (flag_pci_ls)
        lspci(flags);
    if (flag_pci_reset)
        pci_zbridge_control(pci_reset_bus_num, FLAG_ZBRIDGE_RESET);

    exit(0);
}

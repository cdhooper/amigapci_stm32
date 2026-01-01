#ifndef _PCI_ACCESS_H
#define _PCI_ACCESS_H

#define PCI_MAX_BUS             16  // Arbitrary limit (max is really 256)
#define PCI_MAX_DEV             32  // PCI bus has maximum 32 devices
#define PCI_MAX_FUNC            8   // PCI device has maximum 8 functions
#define PCI_MAX_PHYS_SLOT       5   // AmigaPCI has 5 slots

#define BRIDGE_TYPE_UNKNOWN     0
#define BRIDGE_TYPE_MAYTAY      1
#define BRIDGE_TYPE_FIRESTORM   2
#define BRIDGE_TYPE_AMIGAPCI    3

#ifndef RC_SUCCESS
#define RC_SUCCESS              0
#define RC_FAILURE              1
#define RC_NO_DATA              2
#define RC_TIMEOUT              3
#define RC_BAD_PARAM            4
#endif

/* Type 0 Configuration Space Header (for PCI Endpoints) */
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
#define PCI_OFF_ECAP          0x0100 // Start of PCIe capabilities

/* Type 1 Configuration Space Header (for PCI Bridges and Switches) */
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
#define PCI_OFF_BR_ROM_BAR      0x38
#define PCI_OFF_BR_INT_LINE     0x3c
#define PCI_OFF_BR_INT_PIN      0x3d
#define PCI_OFF_BR_CONTROL      0x3e

/* Field-specific definitions */
#define PCI_STATUS_INTX           0x0008  // PCIE: INTx interrupt pending
#define PCI_STATUS_HAS_CAPS       0x0010  // Device has capabilities list
#define PCI_STATUS_66MHZ          0x0020  // Support 66 Mhz PCI 2.1 bus
#define PCI_STATUS_UDF            0x0040  // User Definable Feature [obsolete]
#define PCI_STATUS_FAST_BACK      0x0080  // Accept fast-back to back
#define PCI_STATUS_PARITY         0x0100  // Detected parity error
#define PCI_STATUS_DEVSEL_MASK    0x0600  // DEVSEL timing
#define PCI_STATUS_DEVSEL_FAST    0x0000
#define PCI_STATUS_DEVSEL_MEDIUM  0x0200
#define PCI_STATUS_DEVSEL_SLOW    0x0400
#define PCI_STATUS_SIG_TGT_ABORT  0x0800  // Set on target abort
#define PCI_STATUS_REC_TGT_ABORT  0x1000  // Master ack of abort
#define PCI_STATUS_REC_MSTR_ABORT 0x2000  // Set with Master abort
#define PCI_STATUS_SIG_SYS_ERROR  0x4000  // Set when we drive SERR
#define PCI_STATUS_DET_PARITY     0x8000  // Set on parity error

#define PCI_CLASS_PCI_BRIDGE    0x0604  // PCI-to-PCI bridge

/* PCI Capabilities */
#define PCI_CAP_ID_PM           0x01    // Power Management
#define PCI_CAP_ID_AGP          0x02    // AGP video card
#define PCI_CAP_ID_VPD          0x03    // Vital Product Data
#define PCI_CAP_ID_SLOTID       0x04    // Slot info
#define PCI_CAP_ID_MSI          0x05    // Message Signaled Interrupts
#define PCI_CAP_ID_CHSWP        0x06    // CompactPCI HotSwap
#define PCI_CAP_ID_PCIX         0x07    // PCI-X
#define PCI_CAP_ID_HT           0x08    // HyperTransport
#define PCI_CAP_ID_VNDR         0x09    // Vendor-specific
#define PCI_CAP_ID_DBG          0x0a    // Debug port
#define PCI_CAP_ID_CCRC         0x0b    // CompactPCI resource control
#define PCI_CAP_ID_SHPC         0x0c    // PCI standard hotplug controller
#define PCI_CAP_ID_SSVID        0x0d    // Bridge subsystem vendor/device
#define PCI_CAP_ID_AGP3         0x0e    // AGP 8x
#define PCI_CAP_ID_SECURE       0x0f    // Secure device
#define PCI_CAP_ID_PCIE         0x10    // PCI Express
#define PCI_CAP_ID_MSIX         0x11    // MSI-X
#define PCI_CAP_ID_SATA         0x12    // SATA HBA
#define PCI_CAP_ID_AF           0x13    // Advanced features
#define PCI_CAP_ID_EA           0x14    // Enhanced Allocation
#define PCI_CAP_ID_FPB          0x15    // Flattening Portal Bridge

/* Message Signaled Interrupts */
#define PCI_MSI_FLAGS_ENABLE    0x0001  // MSI enable
#define PCI_MSI_FLAGS_64BIT     0x0080  // 64-bit capable
#define PCI_MSI_FLAGS_MASKBIT   0x0100  // Interrupt mask supported

/* Vital Product Data */
#define PCI_VPD_ADDR            2       // Address to access
#define PCI_VPD_ADDR_MASK       0x7fff  // Address mask
#define PCI_VPD_ADDR_F          0x8000  // When 1, VPD read is complete
#define PCI_VPD_DATA            4       // 32-bits of data returned here

/* PCI Express */
#define PCI_EXP_FLAGS_VERS      0x000f  // Version
#define PCI_EXP_FLAGS_TYPE      0x00f0  // Device/Port
#define PCI_EXP_FLAGS_SLOT      0x0100  // Slot
#define PCI_EXP_TYPE_ROOT_PORT  0x04    // Root port

/* Flags for pci_bridge_control() */
#define FLAG_BRIDGE_RESET       0x01    // Reset the PCI bridge
#define FLAG_BRIDGE_RESET_HOLD  0x02    // Put the PCI bridge in reset

#define PCI_DEVFN(slot,func)    ((((slot) & 0x1f) << 3) | ((func) & 0x07))
#define PCI_SLOT(devfn)         (((devfn) >> 3) & 0x1f)
#define PCI_FUNC(devfn)         ((devfn) & 0x07)

typedef unsigned int rc_t;

uint8_t  pci_read8(uint bus, uint dev, uint func, uint off);
uint16_t pci_read16(uint bus, uint dev, uint func, uint off);
uint32_t pci_read32(uint bus, uint dev, uint func, uint off);
uint32_t pci_read(uint bus, uint dev, uint func, uint off, uint bytes);
void     pci_write8(uint bus, uint dev, uint func, uint off, uint8_t value);
void     pci_write16(uint bus, uint dev, uint func, uint off, uint16_t value);
void     pci_write32(uint bus, uint dev, uint func, uint off, uint32_t value);
void     pci_write(uint bus, uint dev, uint func, uint offset, uint bytes, uint32_t value);
uint32_t pci_read32v(uint bus, uint dev, uint func, uint off);
rc_t     pci_read_buf(uint bus, uint dev, uint func, uint offset, uint bytes, void *bufp);
rc_t     pci_write_buf(uint bus, uint dev, uint func, uint offset, uint bytes, void *bufp);
void    *pci_cfg_base(uint bus, uint dev, uint func, uint off);
void     pci_bridge_control(int pci_bridge, uint flags);
int      pci_bridge_is_present(void);

extern struct ConfigDev *pci_zorro_cdev;
extern uint8_t  bridge_type;
extern uint16_t bridge_zorro_mfg;
extern uint16_t bridge_zorro_prod;
extern uint8_t *bridge_pci0_base;
extern uint8_t *bridge_pci1_base;
extern void    *bridge_io_base;
extern void    *bridge_mem_base;

#endif


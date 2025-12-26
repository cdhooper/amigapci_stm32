#ifndef _PCI_ACCESS_H
#define _PCI_ACCESS_H

#define PCI_MAX_BUS  16

#ifndef RC_SUCCESS
#define RC_SUCCESS   0
#define RC_FAILURE   1
#define RC_NO_DATA   2
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
#define PCI_STATUS_HAS_CAPS     0x0010  // Device has capabilities list

#define PCI_CLASS_PCI_BRIDGE    0x0604  // PCI-to-PCI bridge

/* Flags for pci_bridge_control() */
#define FLAG_BRIDGE_RESET 0x01  // Reset the PCI bridge and all subordinates

typedef unsigned int rc_t;

uint8_t  pci_read8(uint bus, uint dev, uint func, uint off);
uint16_t pci_read16(uint bus, uint dev, uint func, uint off);
uint32_t pci_read32(uint bus, uint dev, uint func, uint off);
void     pci_write8(uint bus, uint dev, uint func, uint off, uint8_t value);
void     pci_write16(uint bus, uint dev, uint func, uint off, uint16_t value);
void     pci_write32(uint bus, uint dev, uint func, uint off, uint32_t value);
uint32_t pci_read32v(uint bus, uint dev, uint func, uint off);
rc_t     pci_read(uint bus, uint dev, uint func, uint offset, uint width, void *bufp);
rc_t     pci_write(uint bus, uint dev, uint func, uint offset, uint width, void *bufp);
void     pci_bridge_control(int pci_bridge, uint flags);
int      pci_bridge_is_present(void);

extern struct ConfigDev *pci_zorro_cdev;
extern uint16_t bridge_zorro_mfg;
extern uint16_t bridge_zorro_prod;
extern uint8_t *bridge_pci0_base;
extern uint8_t *bridge_pci1_base;
extern void    *bridge_io_base;
extern void    *bridge_mem_base;

#endif


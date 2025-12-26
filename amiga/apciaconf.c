#include <stdio.h>
#include <exec/types.h>
#include <libraries/configvars.h>
#include <libraries/configregs.h>
#include <proto/exec.h>
#include <inline/exec.h>
#include <proto/expansion.h>
#include <inline/expansion.h>

struct ExpansionBase *ExpansionBase = NULL;

#define DEVICE_MFG  0x0e3b  // E3B
#define DEVICE_PROD 0xc8    // Prometheus Firestorm

int main()
{
    struct ConfigDev *cd;

    /* Open expansion library */
    ExpansionBase = (void *)OpenLibrary("expansion.library", 37);
    if (!ExpansionBase) {
        printf("Failed to open expansion.library\n");
        return (1);
    }

    cd = FindConfigDev(NULL, DEVICE_MFG, DEVICE_PROD);
    if (cd != NULL) {
        printf("Mfg $%04x  Prod $%02x  already exists\n",
               DEVICE_MFG, DEVICE_PROD);
        goto done;
    }

    /* Allocate memory for a new ConfigDev structure */
    cd = AllocVec(sizeof(struct ConfigDev), MEMF_PUBLIC | MEMF_CLEAR);

    if (cd != NULL) {
        cd->cd_Rom.er_Type         = ERT_ZORROIII | 0x05;
        cd->cd_Rom.er_Product      = DEVICE_PROD;
        cd->cd_Rom.er_Flags        = ERFF_EXTENDED | ERFF_ZORRO_III;
//      cd->cd_Rom.er_Reserved03   = 0x00;
        cd->cd_Rom.er_Manufacturer = DEVICE_MFG;
//      cd->cd_Rom.er_SerialNumber = 0x00000000;
//      cd->cd_Rom.er_InitDiagVec  = 0x0000;
//      cd->cd_Rom.er_Reserved0c   = 0x00;
//      cd->cd_Rom.er_Reserved0d   = 0x00;
//      cd->cd_Rom.er_Reserved0e   = 0x00;
//      cd->cd_Rom.er_Reserved0f   = 0x00;

        cd->cd_Flags     = CDF_CONFIGME;
        cd->cd_BoardAddr = (APTR)0x80000000;
        cd->cd_BoardSize = 512 << 10; // 512 KB
//      cd->cd_SlotAddr  = 0x0000;
//      cd->cd_SlotSize  = 0x0000;
//      cd->cd_Driver    = NULL;
//      cd->cd_NextCD    = NULL;
//      cd->cd_Unused[0] = 0x00000000;
//      cd->cd_Unused[1] = 0x00000000;
//      cd->cd_Unused[2] = 0x00000000;
//      cd->cd_Unused[3] = 0x00000000;

        /* Add the entry to the system list */
        AddConfigDev(cd);
        printf("Fake AutoConfig entry added: Mfg $%04x Prod $%02x\n",
                cd->cd_Rom.er_Manufacturer, cd->cd_Rom.er_Product);
    }

done:
    CloseLibrary((struct Library *)ExpansionBase);
    return (0);
}

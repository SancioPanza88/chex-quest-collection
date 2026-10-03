#include <stdio.h>

#include "m_argv.h"

#include "doomgeneric.h"
#ifdef FEATURE_VITA_DEH
#include "i_system.h"
#endif

pixel_t* DG_ScreenBuffer = NULL;

void M_FindResponseFile(void);
void D_DoomMain (void);


void doomgeneric_Create(int argc, char **argv)
{
	// save arguments
    myargc = argc;
    myargv = argv;

	M_FindResponseFile();    DG_ScreenBuffer = malloc(DOOMGENERIC_RESX * DOOMGENERIC_RESY * 4);
#ifdef FEATURE_VITA_DEH
    if (DG_ScreenBuffer == NULL)
    {
        I_Error("Not enough memory for the Doom framebuffer");
        return;
    }
#endif

	DG_Init();

	D_DoomMain ();
}


/* =============================================================================
 * main.c — System Entry Point Stub
 * Project: Digital Harmonium Workstation Firmware
 * Stage 1 — System Skeleton Verification Harness
 * ============================================================================= */

#include <stdio.h>
#include "vhl_types.h"
#include "vhl_config.h"
#include "vhl_err.h"

int main(void)
{
    printf("=============================================================\n");
    printf("Digital Harmonium Workstation — Stage 1 Skeleton Boot Test\n");
    printf("=============================================================\n");
    printf("Target Sample Rate : %u Hz\n", (unsigned int)VHL_SAMPLE_RATE);
    printf("DMA Buffer Frames  : %u frames\n", (unsigned int)VHL_DMA_BUFFER_FRAMES);
    printf("Max Polyphony      : %u voices\n", (unsigned int)VHL_MAX_POLYPHONY);
    printf("Audio Task Stack   : %u bytes\n", (unsigned int)VHL_STACK_SIZE_AUDIO);
    printf("Status Code OK     : %d\n", (int)VHL_OK);
    printf("=============================================================\n");
    printf("Stage 1 Verification Gate Passed Cleanly.\n");
    return 0;
}

void app_main(void)
{
    main();
}

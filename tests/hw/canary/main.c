/*
 * Hardware test: canary. Checks the emulator setup, not titrmlib: it only
 * needs graphx and a launchable program. If this fails, look at the setup
 * (ROM, CE libraries) before looking at the library.
 *
 * Screen: left half black, right half white (8bpp). [clear] exits.
 */

#include <graphx.h>
#include <ti/getcsc.h>

int main(void) {
    gfx_Begin();
    gfx_FillScreen(0x00);
    gfx_SetColor(0xFF);
    gfx_FillRectangle_NoClip(GFX_LCD_WIDTH / 2, 0, GFX_LCD_WIDTH / 2, GFX_LCD_HEIGHT);

    while (os_GetCSC() != sk_Clear) {
    }

    gfx_End();
    return 0;
}

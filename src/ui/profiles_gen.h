/* Made by tools/kbuild.py --profiles from platforms/targets/: do not edit (change the target's file, run it).
 * Included once, by src/ui/draw.c. */
static const char *const KEYS_FM1[K_KEY_ROLES] = {"K1", "K2", "K3", "K4", "OCT+", "OCT-", "SAVE"};
static const KProfile PROFILES[] = {
    {"handheld", 320, 180, K_IN_PAD | K_IN_TOUCH, 0, 4, 0, 0, 0.0f, K_FONT_VECTOR, 0, 0, 0},   /* a small wide handheld (fixture) */
    {"fm1", 240, 240, K_IN_KNOBS | K_IN_KEYS, 4, 4, 0, 0, 0.0f, K_FONT_BITMAP, KEYS_FM1, K_LAY_LEDS | K_LAY_PRESET_POP | K_LAY_USB_AUDIO, K_LAY_CONNECT},   /* M-VAVE FM-1 (target) */
    {"zero", 320, 170, K_IN_KEYBOARD, 0, 4, 0, 0, 0.0f, K_FONT_VECTOR, 0, 0, 0},   /* M5Stack Cardputer Zero (target) */
    {"h700", 320, 240, K_IN_PAD, 0, 4, 0, 0, 0.0f, K_FONT_VECTOR, 0, 0, 0},   /* H700 handhelds (Anbernic RG35XX and the like, PortMaster) (target) */
    {"mac", 640, 360, K_IN_PAD | K_IN_TOUCH | K_IN_KEYBOARD | K_IN_MOUSE, 0, 4, 0, 0, 0.0f, K_FONT_VECTOR, 0, 0, 0},   /* macOS (the SDL2 app, Fune.app) (target) */
    {"cores3", 320, 240, K_IN_TOUCH, 0, 4, 0, 0, 0.0f, K_FONT_VECTOR, 0, 0, 0},   /* M5Stack CoreS3 (kept) */
    {"switch", 640, 360, K_IN_PAD | K_IN_TOUCH | K_IN_KEYBOARD, 0, 4, 0, 0, 0.0f, K_FONT_VECTOR, 0, 0, 0},   /* a TV-sized screen with a pad (640 x 360: the layouts of the former Switch build) (kept) */
};

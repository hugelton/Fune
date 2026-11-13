/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kreisel.h"
#include "kreisel_plugin.h"
#include "kreisel_plugin_load.h"
#include "kreisel_audio.h"
#include "kreisel_song.h"
#include "kreisel_demo.h"
#include "kreisel_io.h"
#include "controllers.h"
#include "midi.h"
#include "ioconf.h"
#include "kreisel_save.h"
#include "kreisel_store_file.h"
#include <sys/stat.h>
#include "draw.h"
#include "gfx.h"
#include "blit.h"
#include "panel.h"
#include "input.h"
#include "desk.h"

/* Render callback: only KAudio is accessed on the SDL audio thread. */
static void audio_callback(void *userdata, Uint8 *bytes, int length)
{
    k_audio_render((KAudio *)userdata, (float *)bytes, (size_t)length / (sizeof(float) * 2u));
}

static void process_event(KApp *app,SDL_Event *e,SDL_Renderer *r,int *running){
    if(e->type==SDL_KEYDOWN&&!e->key.repeat&&e->key.keysym.sym==SDLK_F9){k_dispatch(app,(KAction){K_ACT_SAVE,0,0,SDL_GetTicks()});return;}   /* F9: SAVE (the song slots, the presets: desk_dir) */
    if(e->type==SDL_DROPFILE){if(e->drop.file){desk_open_file(app,e->drop.file);SDL_free(e->drop.file);}return;}   /* a .fune dropped on the window, or opened from the Finder */
    sdl_process_event(app,e,r,running);
}

/* --audio-devices: the names io.conf's audio_out / audio_in take, then out (no window) */
static int list_audio_devices(void){
    int cap,n,k;
    if(SDL_Init(SDL_INIT_AUDIO)!=0){fprintf(stderr,"audio: %s\n",SDL_GetError());return 1;}
    printf("driver: %s\n",SDL_GetCurrentAudioDriver()?SDL_GetCurrentAudioDriver():"(none)");
    for(cap=0;cap<2;cap++){
        n=SDL_GetNumAudioDevices(cap);
        printf("%s (%d):\n",cap?"capture, audio_in":"output, audio_out",n<0?0:n);
        for(k=0;k<n;k++){const char *nm=SDL_GetAudioDeviceName(k,cap);printf("  %s\n",nm?nm:"?");}
    }
    SDL_Quit();
    return 0;
}

static void usage(void){
    int i;
    fprintf(stderr,"usage: kreisel [--profile NAME | --size WxH [--input pad,touch,keyboard,mouse,knobs,keys]] [--theme dark] [--page N] [--font bitmap|vector] [--panel fm1] [--menu] [--touch K] [--free] [--fullscreen] [--shot FILE.bmp] [FILE.fune] | --audio-devices\nprofiles:");
    for(i=0;i<k_profile_count();i++){int w,h;k_profile_size(k_profile_at(i),&w,&h);fprintf(stderr," %s(%dx%d)",k_profile_at(i)->name,w,h);}
    fputc('\n',stderr);
}

static int desk_any_song(void){int i;for(i=0;i<K_SONG_SLOTS;i++)if(k_save_used(K_STORE_SONG,i))return 1;return 0;}   /* (a first start: none saved) */
int main(int argc,char **argv){
    const KProfile *profile=k_profile_find("mac");   /* the desktop's own: 640 x 360, a mouse (--profile another device, --size any screen) */
    const char *shot=NULL,*shot_all=NULL,*panel_name=NULL,*open_path=NULL;int page=0,i,menu=0,touch=-1,free_input=0,font=-1,fullscreen=0;KThemeId theme=K_THEME_DARK;
    for(i=1;i<argc;i++){
        if(!strcmp(argv[i],"--profile")&&i+1<argc){
            profile=k_profile_find(argv[++i]);
            if(!profile){usage();return 2;}
        }else if(!strcmp(argv[i],"--size")&&i+1<argc){   /* any screen: its logical size (a device with no profile yet) */
            int w=0,h=0;
            if(sscanf(argv[++i],"%dx%d",&w,&h)!=2||w<1||h<1){usage();return 2;}
            profile=k_profile_custom(w,h,profile==k_profile_find("mac")?profile->input:K_IN_PAD|K_IN_TOUCH|K_IN_KEYBOARD|K_IN_MOUSE);
        }else if(!strcmp(argv[i],"--input")&&i+1<argc){   /* its inputs, after --size (a comma list) */
            static const char *const N[]={"pad","touch","keyboard","mouse","knobs","keys"};
            static const unsigned B[]={K_IN_PAD,K_IN_TOUCH,K_IN_KEYBOARD,K_IN_MOUSE,K_IN_KNOBS,K_IN_KEYS};
            unsigned in=0;size_t k;const char *q=argv[++i];int w,h;
            while(*q){
                for(k=0;k<sizeof N/sizeof *N;k++){size_t n=strlen(N[k]);if(!strncmp(q,N[k],n)&&(q[n]==','||!q[n])){in|=B[k];q+=n;break;}}
                if(k==sizeof N/sizeof *N){usage();return 2;}
                if(*q==',')q++;
            }
            k_profile_size(profile,&w,&h);profile=k_profile_custom(w,h,in);
        }else if(!strcmp(argv[i],"--page")&&i+1<argc)page=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--audio-devices"))return list_audio_devices();
        else if(!strcmp(argv[i],"--menu"))menu=1;
        else if(!strcmp(argv[i],"--free"))free_input=1;   /* every input, whatever the device has (for testing) */
        else if(!strcmp(argv[i],"--fullscreen"))fullscreen=1;   /* the whole screen (a device under a window manager: the Cardputer Zero's labwc) */
        else if(!strcmp(argv[i],"--touch")&&i+1<argc)touch=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--panel")&&i+1<argc)panel_name=argv[++i];
        else if(!strcmp(argv[i],"--shot-all")&&i+1<argc)shot_all=argv[++i];
        else if(!strcmp(argv[i],"--font")&&i+1<argc){
            ++i;
            if(!strcmp(argv[i],"vector"))font=1;
            else if(!strcmp(argv[i],"bitmap"))font=0;
            else {usage();return 2;}
        }
        else if(!strcmp(argv[i],"--theme")&&i+1<argc){
            ++i;
            if(!strcmp(argv[i],"dark"))theme=K_THEME_DARK;
            else {usage();return 2;}
        }else if(!strcmp(argv[i],"--shot")&&i+1<argc)shot=argv[++i];
        else if(argv[i][0]!='-')open_path=argv[i];   /* a song or preset file (.fune) to open */
        else if(!strncmp(argv[i],"-psn_",5))continue;   /* (macOS's process serial number, from an old Finder launch) */
        else {usage();return 2;}
    }
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS,"0");
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER|SDL_INIT_TIMER)!=0){
        fprintf(stderr,"SDL initialization: %s\n",SDL_GetError());return 1;
    }
    int pw,ph;k_profile_size(profile,&pw,&ph);   /* the UI's size, in dp */
    int lw=pw+(panel_name?8+340:0),lh=ph>240?ph:240,scale=profile->px_w?1:panel_name?2:3;   /* a real screen: one window pixel a dp */
    if(!panel_name)lh=ph;
    if(!profile->px_w){   /* the largest whole scale that fits the display (less its menu bar and dock): a 640 x 360 screen at 2 on a laptop, not 3 */
        SDL_Rect ub;
        if(SDL_GetDisplayUsableBounds(0,&ub)==0){
            while(scale>1&&(lw*scale>ub.w*9/10||lh*scale>ub.h*9/10))scale--;
            while(lw*(scale+1)<=ub.w*6/10&&lh*(scale+1)<=ub.h*6/10&&scale<4)scale++;
        }
    }
    char data_dir[1024]="";DeskWindow keep={0};
    const int session=!(shot||shot_all)&&!panel_name&&profile==k_profile_find("mac");   /* the desktop app as itself: its session, its window */
    if(!(shot||shot_all)&&!desk_dir(data_dir,sizeof data_dir))data_dir[0]=0;
    SDL_Window*window=SDL_CreateWindow("Fune",SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,lw*scale,lh*scale,SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI|(fullscreen?SDL_WINDOW_FULLSCREEN_DESKTOP:0));
    if(!window){fprintf(stderr,"window: %s\n",SDL_GetError());SDL_Quit();return 1;}
    SDL_Renderer*renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
    if(!renderer)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
    if(!renderer){fprintf(stderr,"renderer: %s\n",SDL_GetError());SDL_DestroyWindow(window);SDL_Quit();return 1;}
    SDL_RenderSetLogicalSize(renderer,lw,lh);
    SDL_RenderSetIntegerScale(renderer,profile==k_profile_find("mac")?SDL_FALSE:SDL_TRUE);   /* the desktop's window fills as it is sized; a device's stays in whole pixels */
    if(!ui_draw_init(profile,theme)||!sdl_blit_init(renderer,pw,ph)){fprintf(stderr,"draw init failed\n");return 1;}
    if(font>=0)g_font_vector(font);   /* --font: over the profile's */
    if(panel_name&&!panel_init(renderer,panel_name,pw+8)){fprintf(stderr,"unknown panel: %s\n",panel_name);return 2;}
    SDL_GameController*controller=NULL;
    for(i=0;i<SDL_NumJoysticks();i++)if(SDL_IsGameController(i)){
        controller=SDL_GameControllerOpen(i);if(controller)break;
    }
    KApp app;KCursorVisual cursor={0};int running=1;
    k_init(&app);k_set_shown(&app,profile->shown);k_set_layout(&app,k_profile_layout(profile));
    {static const KDeviceCaps caps={44100u,1u<<20,2000u,K_AUDIO_VOICES,K_NEED_FLOAT};k_plugin_set_caps(&caps);}   /* a desktop: room for any plugin built at 44.1 kHz */
    if(data_dir[0]){   /* outside plugins: <data>/plugins (before the song: it may name them) */
        char dir[1100];
        snprintf(dir,sizeof dir,"%s/plugins",data_dir);
        {int n=k_plugin_load_dir(dir);if(n)fprintf(stderr,"plugins: %d loaded from %s\n",n,dir);}
    }
    if(data_dir[0]&&k_store_set(k_store_file(data_dir))==0){   /* SAVE's slots: <data>/songs, presets (screenshots: none, they stay deterministic) */
        k_settings_load(&app);
        if(session&&desk_session_load(&app,data_dir,&keep)==0)fprintf(stderr,"session: opened as it was left\n");   /* the desktop: where it was left */
        else if(KREISEL_DEMO_SONG==1&&!(shot||shot_all)){if(k_demo_load(&app)==0)fprintf(stderr,"song: COLDWIRE (the demo, while developing)\n");}   /* (kreisel_demo.h) */
        else if(!app.set_v[K_SET_RESTORE]&&k_save_boot(&app)==0)fprintf(stderr,"song: slot %d loaded\n",app.song_from);   /* else the song written last */
        else if(KREISEL_DEMO_SONG==2&&!(shot||shot_all)&&!desk_any_song()){if(k_demo_load(&app)==0)fprintf(stderr,"song: COLDWIRE (the demo: nothing saved yet)\n");}
        if(session&&keep.ok){   /* the window where it was (only if that place is still on a display) */
            SDL_Rect d;int k2,on=0;
            for(k2=0;k2<SDL_GetNumVideoDisplays();k2++)if(SDL_GetDisplayUsableBounds(k2,&d)==0&&keep.x+40>=d.x&&keep.y>=d.y-8&&keep.x+40<d.x+d.w&&keep.y<d.y+d.h)on=1;
            if(on)SDL_SetWindowPosition(window,keep.x,keep.y);   /* (its size not: it opens dot by dot, a whole scale of the screen) */
        }
    }
    if(open_path)desk_open_file(&app,open_path);   /* over the session: the file asked for */
    sdl_input_set(free_input?~0u:profile->input|(panel_name?K_IN_KNOBS|K_IN_KEYS:0));   /* a device is given the controls it has */
    if(page>0&&page<K_PAGE_COUNT)k_open_page(&app,(KPage)page);
    if(menu)k_dispatch(&app,(KAction){K_ACT_PAGE_MENU,0,0,0});
    if(touch>=0)k_dispatch(&app,(KAction){K_ACT_PARAM_DELTA,touch,0,SDL_GetTicks()});   /* turn knob K by nothing: its card opens */
    if(shot||shot_all){
        uint32_t t0=SDL_GetTicks();
        for(i=0;i<3;i++){   /* the first frame places the focus rectangles; the others let a card open (200 ms apart) */
            SDL_SetRenderDrawColor(renderer,8,8,8,255);SDL_RenderClear(renderer);
            ui_draw(&app,&cursor,t0+(uint32_t)i*200);sdl_blit(renderer,ui_draw_canvas());
            if(panel_origin()>=0)panel_draw(renderer,&app);
        }
        i=0;
        if(shot)i|=sdl_blit_save(shot,ui_draw_canvas());
        if(shot_all){
            int ow,oh;SDL_GetRendererOutputSize(renderer,&ow,&oh);
            SDL_Surface*s=SDL_CreateRGBSurfaceWithFormat(0,ow,oh,32,SDL_PIXELFORMAT_ARGB8888);
            if(!s||SDL_RenderReadPixels(renderer,NULL,SDL_PIXELFORMAT_ARGB8888,s->pixels,s->pitch)!=0||SDL_SaveBMP(s,shot_all)!=0)i|=1;
            if(s)SDL_FreeSurface(s);
        }
        panel_shutdown();sdl_blit_shutdown();ui_draw_shutdown();SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();
        return i==0?0:1;
    }
    /* Audio is optional, and screenshot mode above stays deterministic. */
    KAudio audio;
    SDL_AudioDeviceID audio_dev=0;
    if(SDL_InitSubSystem(SDL_INIT_AUDIO)==0){
        SDL_AudioSpec want={0},have={0};
        SdlAudioConf ac;
        sdl_ioconf_audio(data_dir,&ac);   /* io.conf [audio]: the device, rate, buffer (defaults: the system's, the device's rate, 512) */
        want.freq=ac.rate>0?ac.rate:48000;
        want.format=AUDIO_F32SYS;
        want.channels=2;
        want.samples=(Uint16)ac.buffer;
        want.callback=audio_callback;
        want.userdata=&audio;
        {const int flags=ac.rate>0?0:SDL_AUDIO_ALLOW_FREQUENCY_CHANGE;   /* a rate asked for: SDL converts to it */
        audio_dev=SDL_OpenAudioDevice(ac.out[0]?ac.out:NULL,0,&want,&have,flags);
        if(!audio_dev&&ac.out[0]){
            fprintf(stderr,"audio: \"%s\" did not open (%s); the default instead\n",ac.out,SDL_GetError());
            audio_dev=SDL_OpenAudioDevice(NULL,0,&want,&have,flags);
        }}
        if(audio_dev){
            k_audio_init(&audio,(uint32_t)have.freq);
            k_audio_sync(&audio,&app);
            SDL_PauseAudioDevice(audio_dev,0);
        }else fprintf(stderr,"audio unavailable (silent UI): %s\n",SDL_GetError());
    }else fprintf(stderr,"audio subsystem unavailable (silent UI): %s\n",SDL_GetError());
    k_io_midi_map(k_io_map_desk,k_io_map_desk_n);   /* MIDI in: notes, the clock, the core's CCs */
    sdl_ctl_load(data_dir);   /* the controllers' profiles (built in, then <data>/controllers/NAME.conf): before io.conf, whose [keys] may name one */
    if(data_dir[0])sdl_ioconf_load(&app,data_dir);   /* io.conf: which MIDI in, its channel, the clock, the keys (written with its defaults the first time) */
    sdl_midi_open("Fune");
    if(sdl_midi_outputs()&&sdl_ioconf_midi_out())k_io_send_to(sdl_midi_send);   /* MIDI out: the key bed's and the sequencer's notes, the clock */
    int busy=1,dens=0;
    const int hidpi=session;   /* (the desktop app as itself; a device's profile stays in its own pixels) */
    if(hidpi)SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"1");   /* (the canvas's pixels, a little more or less than the window's: smoothly) */
    while(running){
        SDL_Event e;
        if(busy){while(SDL_PollEvent(&e))process_event(&app,&e,renderer,&running);}
        else if(SDL_WaitEventTimeout(&e,100)){   /* still: sleep until input (or a tenth of a second) */
            process_event(&app,&e,renderer,&running);
            while(SDL_PollEvent(&e))process_event(&app,&e,renderer,&running);
        }
        sdl_midi_poll(&app,SDL_GetTicks());   /* (what came in, before the frame's tick) */
        sdl_ioconf_sync(&app,data_dir);   /* (the Connect tab changed something: io.conf follows) */
        k_tick(&app,SDL_GetTicks());
        if(ui_pad_tick(&app,SDL_GetTicks()))busy=1;   /* (SELECT held: the menu at the cursor) */
        if(audio_dev){
            int playhead;
            KNoteEv ne;
            SDL_LockAudioDevice(audio_dev);
            k_audio_sync(&audio,&app);
            while(k_take_note(&app,&ne))k_audio_note(&audio,ne.track,ne.note,ne.vel,ne.vel>0);
            playhead=audio.current_step;
            k_audio_song_follow(&audio,&app);   /* (a song: its scene and patterns; stopped at its end) */
            SDL_UnlockAudioDevice(audio_dev);
            k_io_send_audio(&audio);   /* the sequencer's notes and the clock out (its ring: no lock) */
            k_audio_take_peaks(&audio, &app, SDL_GetTicks(), 1);   /* the meters: the tracks', the output's */
            /* Use the sample clock as the playhead when audio output is running.
             * Other platforms keep the k_tick visual clock. */
            if(app.playing && playhead>=0){
                app.step=playhead;
                app.beat=(playhead / 4) & 3;
            }
        }
        if(hidpi){   /* the desktop: the canvas at the screen's own density (its pixels over the UI's units), followed as the window is sized */
            int ow,oh,s2;SDL_GetRendererOutputSize(renderer,&ow,&oh);
            s2=ow/lw<oh/lh?ow/lw:oh/lh;
            if(s2!=dens){dens=s2;ui_draw_set_scale(s2);busy=1;}
        }
        SDL_SetRenderDrawColor(renderer,8,8,8,255);SDL_RenderClear(renderer);
        busy=ui_draw(&app,&cursor,SDL_GetTicks());sdl_blit(renderer,ui_draw_canvas());
        if(panel_origin()>=0)panel_draw(renderer,&app);
        SDL_RenderPresent(renderer);
        if(busy)SDL_Delay(1);
    }
    k_io_send_to(NULL);sdl_midi_close();
    if(session&&data_dir[0]){   /* the session kept for the next start (the window's place with it) */
        DeskWindow w={0,0,0,0,1};
        SDL_GetWindowPosition(window,&w.x,&w.y);SDL_GetWindowSize(window,&w.w,&w.h);
        if(desk_session_save(&app,data_dir,&w)!=0)fprintf(stderr,"session: not written to %s\n",data_dir);
    }
    if(controller)SDL_GameControllerClose(controller);
    if(audio_dev)SDL_CloseAudioDevice(audio_dev);
    panel_shutdown();sdl_blit_shutdown();ui_draw_shutdown();
    SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();return 0;
}

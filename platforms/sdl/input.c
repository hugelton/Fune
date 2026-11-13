/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#include "input.h"
#include "kreisel_io.h"
#include "draw.h"
#include "panel.h"

static void send(KApp*a,KActionType t,int x,int y,uint32_t ms){k_dispatch(a,(KAction){t,x,y,ms});}
static int key_group(SDL_Keycode key){
    switch(key){case SDLK_F1:return K_GROUP_PATTERN;case SDLK_F2:return K_GROUP_GLO;
        case SDLK_F3:return K_GROUP_SOUND;case SDLK_F4:return K_GROUP_ENV;
        case SDLK_F5:return K_GROUP_FX;case SDLK_F6:return K_GROUP_EDIT;default:return -1;}
}
static unsigned mask=~0u;
/* an SDL key as the input layer names it (kreisel_io.h): a letter, a digit as itself; the arrows, PgUp, Home, Ins, F1 .. F12 its own */
static int io_code(SDL_Keycode k){
    if((k>='a'&&k<='z')||(k>='0'&&k<='9'))return (int)k;
    if(k==SDLK_RETURN||k==SDLK_KP_ENTER)return '\r';   /* (OK) */
    switch(k){case SDLK_UP:return KIO_K_UP;case SDLK_DOWN:return KIO_K_DOWN;case SDLK_LEFT:return KIO_K_LEFT;case SDLK_RIGHT:return KIO_K_RIGHT;
        case SDLK_PAGEUP:return KIO_K_PGUP;case SDLK_PAGEDOWN:return KIO_K_PGDN;case SDLK_HOME:return KIO_K_HOME;case SDLK_INSERT:return KIO_K_INS;default:break;}
    if(k>=SDLK_F1&&k<=SDLK_F12)return KIO_K_F1+(int)(k-SDLK_F1);
    return -1;
}
static int reset_click;   /* the button went down with option (a reset): its drag and release do nothing */
void sdl_input_set(unsigned m){mask=m;k_io_key_map((m&K_IN_KEYBOARD)&&!(m&(K_IN_PAD|K_IN_MOUSE))?k_io_map_zero:0,k_io_map_zero_n);}   /* (a keyboard alone: the Zero's key table) */
#define NAV (mask&(K_IN_PAD|K_IN_KEYBOARD))
#define ARROWS (mask&K_IN_PAD)   /* cursor keys stand in for a D-pad */
#define KEYS_ONLY ((mask&K_IN_KEYBOARD)&&!(mask&K_IN_PAD))   /* a keyboard alone (the Zero): its arrows (Fn + F Z X C on the device) the cursor */
#define KNOBS (mask&K_IN_KNOBS)
#define TOUCH (mask&K_IN_TOUCH)
void sdl_process_event(KApp*a,const SDL_Event*e,SDL_Renderer*r,int*running){
    uint32_t ms=SDL_GetTicks();
    if(e->type==SDL_QUIT){*running=0;return;}
    if((e->type==SDL_KEYDOWN||e->type==SDL_KEYUP)&&ARROWS&&!(e->key.keysym.mod&KMOD_GUI)){   /* the keys as a pad: arrows, Enter A, Esc B, Tab SELECT */
        const SDL_Keycode k=e->key.keysym.sym;const int dn=e->type==SDL_KEYDOWN;
        int pb=k==SDLK_UP?KP_UP:k==SDLK_DOWN?KP_DOWN:k==SDLK_LEFT?KP_LEFT:k==SDLK_RIGHT?KP_RIGHT:k==SDLK_RETURN||k==SDLK_KP_ENTER?KP_A:
               k==SDLK_ESCAPE?KP_B:k==SDLK_TAB?KP_SELECT:-1;
        if(pb>=0){if(!e->key.repeat||pb<=KP_RIGHT)ui_pad(a,pb,dn,ms);return;}   /* (a held arrow repeats; the rest do not) */
    }
    if(e->type==SDL_KEYDOWN){
        SDL_Keycode key=e->key.keysym.sym;
        if(!ui_popup_open()&&k_io_key(a,io_code(key),1,e->key.repeat,ms))return;   /* the key table (kreisel_io.c; the Zero's: its orange keys) */
        if(e->key.repeat)return;
        if(ui_popup_open()){if(key==SDLK_ESCAPE)ui_popup_close();return;}   /* (the popup up: Esc closes it; the keys wait) */
        if((mask&K_IN_MOUSE)&&(e->key.keysym.mod&KMOD_GUI)&&(key==SDLK_c||key==SDLK_v)){ui_edit(a,key==SDLK_c?0:1,ms);return;}   /* Cmd+C, Cmd+V */
        if((mask&K_IN_MOUSE)&&(e->key.keysym.mod&KMOD_GUI)&&key==SDLK_z){send(a,K_ACT_UNDO,0,0,ms);return;}   /* Cmd+Z (again: redone) */
        if((mask&K_IN_MOUSE)&&(e->key.keysym.mod&KMOD_GUI)&&key==SDLK_s){send(a,K_ACT_SAVE,0,0,ms);return;}   /* Cmd+S: SAVE, as its button */
        if((mask&K_IN_MOUSE)&&(key==SDLK_DELETE||key==SDLK_BACKSPACE)){ui_edit(a,2,ms);return;}   /* Delete: emptied, or its default */
        int group=key_group(key);
        if(group>=0){if(KNOBS)send(a,K_ACT_PAGE_GROUP_DOWN,group,0,ms);return;}   /* the F-keys stand in for page buttons */
        switch(key){
            /* a device with a D-pad or a keyboard moves a cursor; one with only knobs has no cursor keys: , . stand in
             * for SELECT (the track) and ; ' for ALGORITHM (the focus: the rows, the lanes, the menus) */
            case SDLK_UP:if(ARROWS||KEYS_ONLY)send(a,K_ACT_UP,0,0,ms);break;   /* (the Zero: W A S D no longer; its arrows are Fn + F Z X C) */
            case SDLK_DOWN:if(ARROWS||KEYS_ONLY)send(a,K_ACT_DOWN,0,0,ms);break;
            case SDLK_LEFT:if(ARROWS||KEYS_ONLY)send(a,K_ACT_LEFT,0,0,ms);break;
            case SDLK_RIGHT:if(ARROWS||KEYS_ONLY)send(a,K_ACT_RIGHT,0,0,ms);break;
            case SDLK_COMMA:if(!NAV&&KNOBS)send(a,K_ACT_SELECT_DELTA,-1,0,ms);break;   /* , . SELECT (HOME held: the track) */
            case SDLK_PERIOD:if(!NAV&&KNOBS)send(a,K_ACT_SELECT_DELTA,1,0,ms);break;
            case SDLK_SEMICOLON:if(!NAV&&KNOBS)send(a,K_ACT_TYPE_DELTA,-1,0,ms);break;   /* ; ' ALGORITHM: the engine, the effect */
            case SDLK_QUOTE:if(!NAV&&KNOBS)send(a,K_ACT_TYPE_DELTA,1,0,ms);break;
            case SDLK_RETURN:case SDLK_KP_ENTER:if(NAV)send(a,K_ACT_ACCEPT,0,0,ms);break;
            case SDLK_ESCAPE:case SDLK_HOME:send(a,K_ACT_BACK,0,0,ms);break;
            case SDLK_TAB:if(NAV)send(a,K_ACT_PAGE_MENU,0,0,ms);break;
            case SDLK_t:send(a,K_ACT_TOOLS,0,0,ms);break;   /* T: the page's tools (k_tools_page) */
            case SDLK_SPACE:send(a,K_ACT_PLAY,0,0,ms);break;
            case SDLK_PAGEUP:if(NAV)send(a,K_ACT_TRACK_DELTA,-1,0,ms);break;
            case SDLK_PAGEDOWN:if(NAV)send(a,K_ACT_TRACK_DELTA,1,0,ms);break;
            case SDLK_EQUALS:case SDLK_KP_PLUS:if(NAV)send(a,K_ACT_VALUE_DELTA,5,0,ms);break;
            case SDLK_MINUS:case SDLK_KP_MINUS:if(NAV)send(a,K_ACT_VALUE_DELTA,-5,0,ms);break;
            case SDLK_1:case SDLK_2:case SDLK_3:case SDLK_4:
                if(KNOBS)send(a,K_ACT_PARAM_DELTA,key-SDLK_1,(e->key.keysym.mod & KMOD_SHIFT)?-5:5,ms);
                break;
            default:break;
        }
    } else if(e->type==SDL_KEYUP&&k_io_key(a,io_code(e->key.keysym.sym),0,0,ms)){
        /* (the table's key let go) */
    } else if(e->type==SDL_KEYUP){int group=key_group(e->key.keysym.sym);
        if(group>=0&&KNOBS)send(a,K_ACT_PAGE_GROUP_UP,group,0,ms);
    } else if(e->type==SDL_MOUSEWHEEL&&ui_popup_open()){
        /* (the popup up: the wheel turns nothing) */
    } else if(e->type==SDL_MOUSEWHEEL){
        int delta=e->wheel.y>0?-1:e->wheel.y<0?1:0;
        if(panel_origin()>=0){      /* over the panel the wheel turns the knob under the pointer */
            const float x=(float)e->wheel.mouseX,y=(float)e->wheel.mouseY;   /* (the renderer gives them in the screen's own pixels already) */
            if((int)x>=panel_origin()){
                panel_wheel(a,(int)x-panel_origin(),(int)y,e->wheel.y,(SDL_GetModState()&KMOD_SHIFT)!=0,ms);
                return;
            }
        }
        if(mask&K_IN_MOUSE){   /* a mouse: the value under the pointer turns (up: more) */
            const float x=(float)e->wheel.mouseX,y=(float)e->wheel.mouseY;   /* (the renderer gives them in the screen's own pixels already) */
            ui_wheel(a,(int)x,(int)y,e->wheel.y>0?1:e->wheel.y<0?-1:0,ms);
        }
        else if(delta&&(NAV||KNOBS))send(a,delta>0?K_ACT_FOCUS_NEXT:K_ACT_FOCUS_PREV,0,0,ms);
    } else if(e->type==SDL_MOUSEMOTION){
        const float x=(float)e->motion.x,y=(float)e->motion.y;   /* (SDL's renderer has scaled them to the logical size: not again) */
        if(panel_origin()>=0)panel_hover((int)x-panel_origin(),(int)y);
        if(mask&K_IN_MOUSE)ui_hover(a,(int)x,(int)y,(e->motion.state&SDL_BUTTON_LMASK)!=0);   /* (what the pointer is over: lit) */
        if(TOUCH&&!reset_click&&(e->motion.state&SDL_BUTTON_LMASK))ui_touch(a,1,(int)x,(int)y,ms);   /* a drag: the value under it slides */
    } else if(e->type==SDL_WINDOWEVENT&&e->window.event==SDL_WINDOWEVENT_LEAVE){
        if(mask&K_IN_MOUSE)ui_hover(a,-1,-1,0);   /* (the pointer left: nothing lit) */
    } else if(e->type==SDL_MOUSEBUTTONUP && e->button.button==SDL_BUTTON_LEFT){
        const float x=(float)e->button.x,y=(float)e->button.y;
        if(panel_origin()>=0)panel_release(a,ms);
        if(TOUCH&&!reset_click)ui_touch(a,2,(int)x,(int)y,ms);
        if(mask&K_IN_MOUSE)ui_hover(a,(int)x,(int)y,0);
        reset_click=0;
    } else if(e->type==SDL_MOUSEBUTTONDOWN && e->button.button==SDL_BUTTON_RIGHT && (mask&K_IN_MOUSE)){
        const float x=(float)e->button.x,y=(float)e->button.y;
        if(ui_popup_open())ui_popup_close();   /* (a right click with the popup up: it closes) */
        else if(a->menu_open||a->settings_open||a->tools_open||a->save_open||a->save_ask||a->name_open)
            send(a,K_ACT_BACK,0,0,ms);
        else ui_context(a,(int)x,(int)y,ms);   /* the menu for what is under the pointer */   /* a right click closes what is open (a list, a try let go); on a page it does nothing (not HOME:
                                          * the place of a context menu, if one comes) */
    } else if(e->type==SDL_MOUSEBUTTONDOWN && e->button.button==SDL_BUTTON_LEFT){
        const float x=(float)e->button.x,y=(float)e->button.y;
        if(panel_origin()>=0 && (int)x>=panel_origin()){panel_press(a,(int)x-panel_origin(),(int)y,ms);return;}
        if(ui_popup_open()){reset_click=1;ui_popup_click(a,(int)x,(int)y,ms);return;}   /* the popup's: a row, or closed (its drag and release nothing) */
        if((mask&K_IN_MOUSE)&&(SDL_GetModState()&KMOD_ALT)){reset_click=1;ui_reset_at(a,(int)x,(int)y,ms);return;}   /* option-click: back to its default */
        reset_click=0;
        if(TOUCH)ui_touch(a,0,(int)x,(int)y,ms);   /* only a device with a touch screen has one */
    } else if(e->type==SDL_FINGERDOWN||e->type==SDL_FINGERMOTION||e->type==SDL_FINGERUP){
        int lw,lh;float x,y;SDL_RenderGetLogicalSize(r,&lw,&lh);   /* (0..1 over the logical screen, as the renderer gives them) */
        x=e->tfinger.x*(float)lw;y=e->tfinger.y*(float)lh;
        if(TOUCH)ui_touch(a,e->type==SDL_FINGERDOWN?0:e->type==SDL_FINGERMOTION?1:2,(int)x,(int)y,ms);
    } else if((e->type==SDL_CONTROLLERBUTTONDOWN||e->type==SDL_CONTROLLERBUTTONUP) && (mask&K_IN_PAD)){   /* a game pad (ui_pad) */
        static const int8_t M[SDL_CONTROLLER_BUTTON_MAX]={
            [SDL_CONTROLLER_BUTTON_DPAD_UP]=KP_UP+1,[SDL_CONTROLLER_BUTTON_DPAD_DOWN]=KP_DOWN+1,[SDL_CONTROLLER_BUTTON_DPAD_LEFT]=KP_LEFT+1,
            [SDL_CONTROLLER_BUTTON_DPAD_RIGHT]=KP_RIGHT+1,[SDL_CONTROLLER_BUTTON_A]=KP_A+1,[SDL_CONTROLLER_BUTTON_B]=KP_B+1,
            [SDL_CONTROLLER_BUTTON_X]=KP_X+1,[SDL_CONTROLLER_BUTTON_Y]=KP_Y+1,[SDL_CONTROLLER_BUTTON_LEFTSHOULDER]=KP_L+1,
            [SDL_CONTROLLER_BUTTON_RIGHTSHOULDER]=KP_R+1,[SDL_CONTROLLER_BUTTON_START]=KP_START+1,[SDL_CONTROLLER_BUTTON_BACK]=KP_SELECT+1};
        const int bt=e->cbutton.button;
        if(bt>=0&&bt<SDL_CONTROLLER_BUTTON_MAX&&M[bt])ui_pad(a,M[bt]-1,e->type==SDL_CONTROLLERBUTTONDOWN,ms);
    } else if(e->type==SDL_CONTROLLERAXISMOTION && (mask&K_IN_PAD) &&
              (e->caxis.axis==SDL_CONTROLLER_AXIS_TRIGGERLEFT||e->caxis.axis==SDL_CONTROLLER_AXIS_TRIGGERRIGHT)){   /* L2 R2: buttons past half way */
        static int held[2];
        const int k=e->caxis.axis==SDL_CONTROLLER_AXIS_TRIGGERRIGHT,on=e->caxis.value>16000;
        if(on!=held[k]){held[k]=on;ui_pad(a,k?KP_R2:KP_L2,on,ms);}
    }
}

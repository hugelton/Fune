/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#include "kreisel.h"
#include "kreisel_engine.h"
#include "kreisel_plugin.h"
#include "kreisel_song.h"
#include "theme.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void act(KApp *a,KActionType t,int x,int y,unsigned ms){k_dispatch(a,(KAction){t,x,y,ms});}
/* the engine of the track in hand, d steps, as ALGORITHM turns it on SOUND (the page put back after) */
static void eng(KApp *a,int d,unsigned ms){KPage pg=a->page;int w=a->win;a->page=K_PAGE_SOUND;for(;d;d+=d>0?-1:1)act(a,K_ACT_TYPE_DELTA,d>0?1:-1,0,ms);a->page=pg;a->win=w;}
int main(void){
    KApp a; k_init(&a);k_open_page(&a,K_PAGE_STEP);
    assert(a.page==K_PAGE_STEP && a.focus_count==20 && a.bpm==120);
    {KApp h;k_init(&h);assert(h.page==K_PAGE_MIXER && h.tcolor[3]==6);   /* it starts on the mixer (HOME's first) */
     k_open_page(&h,K_PAGE_HOME);assert(h.focus_count==K_TRACKS+4);   /* the macros: four tracks, four levels */
     {int t=h.track,m=4+k_engine_macro(h.sound[t],1),was=h.params[t][K_PAGE_SOUND][m],e,k;   /* the first row: the track in hand's macros, as its engine declares them */
      assert(h.win==0);act(&h,K_ACT_PARAM_DELTA,1,3,0);assert(h.params[t][K_PAGE_SOUND][m]!=was || was==k_engine_at(h.sound[t])->edit[m-4].max);
      assert(k_engine_macro(0,0)==12 && k_engine_macro(0,1)==13);   /* ANALOG: CUT, RES */
      for(e=0;e<K_ENGINE_COUNT;e++)for(k=0;k<4;k++)assert(k_engine_at(e)->macro[k]<k_engine_at(e)->nparams);}   /* every engine declares four */
     act(&h,K_ACT_FOCUS_NEXT,0,0,0);assert(h.win==0);   /* HOME: the macros alone, one row */
     act(&h,K_ACT_SELECT_DELTA,1,0,0);assert(h.track==1 && h.win==0);   /* SELECT: the track */
     act(&h,K_ACT_SELECT_DELTA,-1,0,0);assert(h.track==0);
     {KApp r;k_init(&r);k_open_page(&r,K_PAGE_TRACKS);   /* TRACKS: the levels, pans and colours, a row each four tracks */
      act(&r,K_ACT_FOCUS_NEXT,0,0,0);assert(r.win==4);
      act(&r,K_ACT_PARAM_DELTA,2,7,0);assert(r.params[2][K_PAGE_MIXER][0]==95);   /* the knobs are the levels of the tracks */
      {int q;for(q=0;q<3*K_TRACKS/4-1;q++)act(&r,K_ACT_FOCUS_NEXT,0,0,0);}assert(r.win==K_HOME_COLOR0+K_TRACKS-4);   /* the last row: colours */
      act(&r,K_ACT_PARAM_DELTA,1,5,0);assert(r.tcolor[K_TRACKS-3]==(K_TRACKS==4?2:r.tcolor[K_TRACKS-3]));   /* a colour of the palette, one at a time */
      assert(*k_param(&r,K_PAGE_TRACKS,K_HOME_COLOR0+1)==r.tcolor[1] && k_param(&r,K_PAGE_TRACKS,4+K_TRACKS+2)==&r.params[2][K_PAGE_MIXER][1]);   /* PAN of track 3 */
      act(&r,K_ACT_FOCUS_NEXT,0,0,0);assert(r.win==K_HOME_COLOR0+K_TRACKS-4);   /* SELECT stops at the last row (no wrap) */
      {int q;for(q=0;q<3*K_TRACKS/4+2;q++)act(&r,K_ACT_FOCUS_PREV,0,0,0);}assert(r.win==0);}   /* and at the first */
     {KNoteEv n;while(k_take_note(&h,&n));
      act(&h,K_ACT_KEY,0,1,0);assert(h.mute==0 && (h.keys_down&1) && (k_is_drum(&h,h.track) || k_take_note(&h,&n)));   /* HOME: a key plays */
      act(&h,K_ACT_KEY,0,0,1);assert(!(h.keys_down&1));}
     {int t=3,i;int o=h.track;h.track=t;for(i=0;i<40&&!k_is_drum(&h,t);i++)eng(&h,1,0);h.page=K_PAGE_HOME;h.track=o;
      assert(k_is_drum(&h,t));
      {KNoteEv n;uint16_t was;h.track=t;while(k_take_note(&h,&n));   /* a drum track, any page: a black key picks its lane and hits it */
        act(&h,K_ACT_KEY,16+3,1,0);assert(h.cur_lane[t]==3 && k_take_note(&h,&n) && n.track==t && n.note==3 && n.vel>0);
        assert(k_key_lit(&h,16+3) && !k_key_lit(&h,16+2));   /* (its black key lit) */
        act(&h,K_ACT_KEY,16+3,0,1);assert(k_take_note(&h,&n) && n.note==3 && n.vel==0);   /* (the release: a synth that is a drum hears it) */
        was=h.lane_on[t][3];   /* a white key: a step of the lane in hand, lit; no sound */
        act(&h,K_ACT_KEY,5,1,2);assert(!k_take_note(&h,&n) && h.lane_on[t][3]==(uint16_t)(was^(1u<<5)) && k_key_lit(&h,5)==!!(h.lane_on[t][3]>>5&1));
        act(&h,K_ACT_KEY,5,0,3);assert(!k_take_note(&h,&n));
        act(&h,K_ACT_KEY,5,1,4);act(&h,K_ACT_KEY,5,0,5);assert(h.lane_on[t][3]==was);
        act(&h,K_ACT_KEY,16+9,1,6);assert(!k_take_note(&h,&n) && h.cur_lane[t]==3);act(&h,K_ACT_KEY,16+9,0,7);   /* (no ninth lane) */
        k_note_in(&h,38,100);assert(k_take_note(&h,&n) && n.note==1);   /* GM 38: the snare */
        h.track=o;}
      h.track=t;for(i=0;i<40&&k_is_drum(&h,t);i++)eng(&h,-1,0);h.page=K_PAGE_HOME;h.track=o;}
     act(&h,K_ACT_PAGE_GROUP_DOWN,K_GROUP_GLO,0,0);   /* GLO held + a black key: mute / solo; the release stays on the page */
     act(&h,K_ACT_KEY,16,1,0);act(&h,K_ACT_KEY,16,0,1);assert(h.mute==1 && !k_track_audible(&h,0) && k_track_audible(&h,1));   /* mute track 1 */
     act(&h,K_ACT_KEY,21,1,2);assert(h.solo==2 && k_track_audible(&h,1) && !k_track_audible(&h,2));   /* ... solo track 2: the others are silent */
     act(&h,K_ACT_KEY,21,1,3);act(&h,K_ACT_KEY,16,1,4);assert(h.solo==0 && h.mute==0);
     k_tick(&h,2000);act(&h,K_ACT_PAGE_GROUP_UP,K_GROUP_GLO,0,2000);assert(h.page==K_PAGE_HOME && !h.menu_open && h.group_held==-1);
     act(&h,K_ACT_BACK,0,0,0);assert(h.page==K_PAGE_MIXER);act(&h,K_ACT_BACK,0,0,0);assert(h.page==K_PAGE_HOME);   /* HOME: the mixer, again the macros */
     k_open_page(&h,K_PAGE_FX);act(&h,K_ACT_BACK,0,0,0);assert(h.page==K_PAGE_MIXER);   /* from a page: the mixer */
     h.octave=0;act(&h,K_ACT_OCTAVE_DELTA,-1,0,0);assert(h.page==K_PAGE_MIXER&&h.octave==-1);k_open_page(&h,K_PAGE_SOUND);act(&h,K_ACT_OCTAVE_DELTA,1,0,0);
     assert(h.page==K_PAGE_SOUND&&h.octave==0&&h.balloon.kind==K_BALLOON_OCTAVE);   /* OCT on a page: the octave, the page stays */
     k_open_page(&h,K_PAGE_SONG);act(&h,K_ACT_OCTAVE_DELTA,-1,0,0);assert(h.page==K_PAGE_MIXER&&h.octave==0);   /* SONG: OCT- back (its cells take OCT+) */
     k_open_page(&h,K_PAGE_HOME);
     act(&h,K_ACT_HOME_DOWN,0,0,3000);k_tick(&h,3600);act(&h,K_ACT_HOME_UP,0,0,3700);assert(h.page==K_PAGE_HOME);   /* held: nothing on its release (the settings) */
     k_open_page(&h,K_PAGE_FX);act(&h,K_ACT_HOME_DOWN,0,0,4000);k_tick(&h,4600);act(&h,K_ACT_HOME_UP,0,0,4700);assert(h.page==K_PAGE_FX);}
    assert(a.group_held==-1);
    act(&a,K_ACT_TRACK_DELTA,1,0,0);assert(a.track==1);
    act(&a,K_ACT_TRACK_DELTA,-1,0,0);assert(a.track==0);
    a.focused=0;uint16_t bits=a.steps[0][0];act(&a,K_ACT_ACCEPT,0,0,0);
    assert(a.steps[0][0]==(uint16_t)(bits^1));
    act(&a,K_ACT_PARAM_DELTA,0,99,0);assert(a.bpm==120+99);   /* SEQ's first card is the tempo */
    act(&a,K_ACT_PAGE_GROUP_DOWN,K_GROUP_ENV,0,100);act(&a,K_ACT_PAGE_GROUP_UP,K_GROUP_ENV,0,150);assert(a.page==K_PAGE_ENV1);   /* a press: its first page */
    act(&a,K_ACT_PAGE_GROUP_DOWN,K_GROUP_ENV,0,200);act(&a,K_ACT_PAGE_GROUP_UP,K_GROUP_ENV,0,250);assert(a.page==K_PAGE_ENV2);   /* again: the next */
    act(&a,K_ACT_PAGE_GROUP_DOWN,K_GROUP_ENV,0,300);k_tick(&a,900);assert(!a.menu_open && !a.tools_open);   /* held alone: nothing */
    act(&a,K_ACT_PAGE_GROUP_UP,K_GROUP_ENV,0,950);assert(a.page==K_PAGE_ENV2 && a.group_held==-1 && !a.menu_open);   /* (nor its release) */
    {   KApp b;k_init(&b);k_open_page(&b,K_PAGE_SOUND);
        /* the universal menu during a held page key absorbs its key-up */
        act(&b,K_ACT_PAGE_GROUP_DOWN,K_GROUP_ENV,0,1100);
        act(&b,K_ACT_PAGE_MENU,0,0,1110);
        assert(b.menu_open && !b.menu_is_hold && b.group_held==-1);
        act(&b,K_ACT_PAGE_GROUP_UP,K_GROUP_ENV,0,1120);
        assert(b.menu_open && b.page==K_PAGE_SOUND);
        act(&b,K_ACT_BACK,0,0,1130);assert(!b.menu_open);
    }
    act(&a,K_ACT_PAGE_GROUP_DOWN,K_GROUP_FX,0,500);
    act(&a,K_ACT_PAGE_GROUP_UP,K_GROUP_FX,0,550);assert(a.page==K_PAGE_FX);
    act(&a,K_ACT_PAGE_MENU,0,0,560);assert(a.menu_open && k_menu_count(&a)==16);
    act(&a,K_ACT_BACK,0,0,570);assert(!a.menu_open && a.page==K_PAGE_FX);
    act(&a,K_ACT_PLAY,0,0,580);assert(a.playing);
    act(&a,K_ACT_BACK,0,0,590);assert(a.page==K_PAGE_MIXER && a.playing);act(&a,K_ACT_BACK,0,0,591);assert(a.page==K_PAGE_HOME);k_open_page(&a,K_PAGE_ROLL);
    {int i;for(i=0;i<K_STEPS;i++)k_set_bounds(&a,K_NODE_STEP,i,(KRect){10+i*19,77,17,26});
     for(i=0;i<K_TRACKS;i++)k_set_bounds(&a,K_NODE_TRACK,i,(KRect){10+i*77,36,68,22});}
    act(&a,K_ACT_POINTER,15,80,600);assert(a.focused==0);
    act(&a,K_ACT_RIGHT,0,0,610);assert(a.focused==1);
    act(&a,K_ACT_DOWN,0,0,620);assert(a.focus[a.focused].kind==K_NODE_PARAM || a.focused==1);   /* (the cards are not placed here) */
    {   /* every theme keeps its tokens readable (the pairings and limits are Felucca's: tools/gen_ui_palettes.py) */
        int id;
        for(id=0;id<K_THEME_COUNT;id++){
            KTheme t;k_theme(&t,(KThemeId)id);
            assert(k_contrast(t.text,t.bg)>=12.0);   assert(k_contrast(t.text,t.surf)>=9.0);
            assert(k_contrast(t.mid,t.bg)>=5.0);     assert(k_contrast(t.mid,t.surf)>=4.0);
            assert(k_contrast(t.dim,t.bg)>=2.2);     assert(k_contrast(t.line,t.bg)>=1.25);
            assert(k_contrast(t.theme,t.bg)>=4.5);   assert(k_contrast(t.theme,t.surf)>=4.5);
            assert(k_contrast(t.accent,t.bg)>=4.5);  assert(k_contrast(t.accent,t.surf)>=4.5);
            assert(k_contrast(t.ink,t.sel)>=4.5);    assert(k_contrast(t.ink,t.theme)>=4.5);
            assert(k_contrast(t.sel,t.bg)>=1.8);     assert(k_contrast(t.rec,t.bg)>=3.0);
            assert(k_contrast(t.rec,t.surf)>=3.0);   assert(k_contrast(t.text,t.raise)>=7.0);
        }
    }
    {   /* a row of steps over wide cards: right goes to the next step, down to the card below, up back to the step above it */
        KApp b;int i;k_init(&b);k_open_page(&b,K_PAGE_STEP);
        for(i=0;i<K_STEPS;i++)k_set_bounds(&b,K_NODE_STEP,i,(KRect){9+i*19,34,16,20});
        for(i=0;i<4;i++)k_set_bounds(&b,K_NODE_PARAM,i,(KRect){9+i*76,60,72,22});
        b.focused=0;act(&b,K_ACT_RIGHT,0,0,0);
        assert(b.focus[b.focused].kind==K_NODE_STEP && b.focus[b.focused].index==1);
        b.focused=0;act(&b,K_ACT_DOWN,0,0,0);
        assert(b.focus[b.focused].kind==K_NODE_PARAM && b.focus[b.focused].index==0);
        b.focused=K_STEPS+2;act(&b,K_ACT_UP,0,0,0);
        assert(b.focus[b.focused].kind==K_NODE_STEP);
    }
    {   /* the controls of a panel with encoders and a key bed */
        KApp b;uint16_t before;k_init(&b);k_open_page(&b,K_PAGE_STEP);
        eng(&b,1,0);assert(b.sound[0]==1);
        eng(&b,-9,0);assert(b.sound[0]==0);
        act(&b,K_ACT_MASTER_DELTA,50,0,10);assert(b.master==100 && b.balloon.kind==K_BALLOON_MASTER && b.balloon.held_ms==10);
        act(&b,K_ACT_HOME_DOWN,0,0,20);for(int k=0;k<5;k++)act(&b,K_ACT_SOUND_DELTA,-1,0,20);act(&b,K_ACT_HOME_UP,0,0,21);   /* HOME held + PRESETS: the octave */
        assert(b.octave==-3 && b.balloon.kind==K_BALLOON_OCTAVE && b.page==K_PAGE_STEP);   /* (stops at -3; HOME's release does nothing) */
        act(&b,K_ACT_OCTAVE_DELTA,1,0,22);assert(b.octave==-2);   /* OCT+ alone on a page: the octave too */
        before=b.steps[0][0];act(&b,K_ACT_STEP_TOGGLE,5,0,30);
        assert(b.steps[0][0]==(uint16_t)(before^(1u<<5)));
        assert(b.focus[b.focused].kind==K_NODE_STEP && b.focus[b.focused].index==5);   /* focus follows the key */
        act(&b,K_ACT_PARAM_DELTA,2,3,40);
        assert(b.focus[b.focused].kind==K_NODE_PARAM && b.focus[b.focused].index==2);  /* ... and the knob */
        act(&b,K_ACT_PAGE_MENU,0,0,50);act(&b,K_ACT_MASTER_DELTA,-1,0,55);assert(b.menu_open && b.master==99);  /* MASTER works in a menu */
        b.menu_selected=4;act(&b,K_ACT_SAVE,0,0,60);assert(!b.menu_open && b.page==K_PAGE_SOUND);   /* SAVE confirms there */
        before=b.steps[0][0];act(&b,K_ACT_STEP_TOGGLE,1,0,70);assert(b.steps[0][0]==before);       /* keys step only on STEP and DRUM */
    }
    {   /* SEQ follows the engine: a drum engine has lanes, the others have steps */
        KApp d;k_init(&d);k_open_page(&d,K_PAGE_STEP);
        assert(k_seq_page(&d)==K_PAGE_ROLL && !k_is_drum(&d,0));   /* a synth track: the piano roll */
        d.sound[0]=k_engine_find(K_UID('D','R','U','M'));assert(k_is_drum(&d,0) && k_seq_page(&d)==K_PAGE_DRUM);
        k_open_page(&d,K_PAGE_DRUM);assert(d.focus_count==K_LANES*K_STEPS+4);
        d.focus_count=d.focus_count;act(&d,K_ACT_FOCUS_NEXT,0,0,0);assert(d.cur_lane[0]==1);   /* SELECT picks the lane */
        act(&d,K_ACT_FOCUS_PREV,0,0,0);act(&d,K_ACT_FOCUS_PREV,0,0,0);assert(d.cur_lane[0]==K_LANES-1);   /* round the end */
        act(&d,K_ACT_FOCUS_NEXT,0,0,0);act(&d,K_ACT_FOCUS_NEXT,0,0,0);assert(d.cur_lane[0]==1);
        act(&d,K_ACT_STEP_TOGGLE,2,0,10);assert((d.lane_on[0][1]>>2)&1 && k_key_lit(&d,2) && k_step_on(&d,0,2));
        act(&d,K_ACT_TRACK_DELTA,1,0,20);assert(d.page==K_PAGE_ROLL && d.track==1);   /* the next track is a synth: SEQ shows its piano roll */
        k_open_page(&d,K_PAGE_STEP);act(&d,K_ACT_PARAM_DELTA,1,5,30);assert(d.swing==5);   /* the step view's cards: the tempo, the swing */
        assert(k_seq_page(&d)==K_PAGE_ROLL);
        eng(&d,20,50);assert(d.sound[1]==k_engine_find(K_UID('D','R','U','M')) && k_seq_page(&d)==K_PAGE_DRUM);   /* a drum engine on the track in hand: SEQ shows its lanes */
    }
    {   /* the engine registry: ids are unique and never an index; a foreign or corrupt value cannot reach past the table */
        int i,j;
        for(i=0;i<K_ENGINE_COUNT;i++){
            assert(k_engine_uid(i)!=0 && k_engine_find(k_engine_uid(i))==i);
            for(j=i+1;j<K_ENGINE_COUNT;j++)assert(k_engine_uid(i)!=k_engine_uid(j));
        }
        assert(k_engine_find(K_UID('K','A','N','L'))==0 && k_engine_find(K_UID('A','N','L','G'))==-1 && k_engine_find(K_UID('D','R','U','M'))==K_ENGINE_COUNT-1);
#if K_WITH_ENSEMBLE
        assert(k_engine_find(K_UID('K','E','N','S'))>=0 && k_engine_find(K_UID('K','E','N','S'))<K_ENGINE_COUNT-1);   /* (built in, before DRUM) */
        assert(k_engine_at(k_engine_find(K_UID('K','E','N','S')))->nparams==16 && k_engine_at(k_engine_find(K_UID('K','E','N','S')))->page2==11);
#else
        assert(k_engine_find(K_UID('K','E','N','S'))==-1);   /* (out of the MVP) */
#endif
        assert(k_engine_find(K_UID('z','z','z','1'))==-1 && k_engine_find(0)==-1);   /* an engine that is not here: not found, not another */
        assert(k_engine_at(-1)==&K_ENGINE[0] && k_engine_at(9999)==&K_ENGINE[0] && k_engine_uid(9999)==0);
        {   KApp c;k_init(&c);c.sound[0]=9999;c.sound[1]=-5;   /* a song saved with an engine that is gone: a bad index in every track */
            assert(!k_is_drum(&c,0));k_open_page(&c,K_PAGE_SOUND);k_set_shown(&c,4);
            act(&c,K_ACT_PARAM_DELTA,2,50,0);act(&c,K_ACT_FOCUS_NEXT,0,0,1);act(&c,K_ACT_PARAM_DELTA,1,50,2);   /* turning its knobs does not read past the table */
            assert(k_param_clamp(&K_ENGINE[0].edit[0],-9999)>=K_ENGINE[0].edit[0].min && k_param_clamp(&K_ENGINE[0].edit[0],99999)<=K_ENGINE[0].edit[0].max);
        }
    }
    {   /* plugins: the built-in engines are plugins, each of the kind its descriptor says; an outside one is checked and joins them */
        static KEngine fz, dz, bad;   /* a synth, a drum, and a broken one, made from the built-in ones */
        static const int dummy_dsp = 0;   /* (the core never looks inside a DSP) */
        KPlugin p = {sizeof(KPlugin), K_PLUGIN_ABI, 3, K_PLUG_SYNTH, &fz, &dummy_dsp};
        int i, n0 = k_plugin_count(), fi, di;
        static uint8_t buf[8192];size_t len;assert(k_song_max_size()<=sizeof buf);
        assert(n0 == K_ENGINE_COUNT && k_engine_count() == n0);
        for (i = 0; i < K_ENGINE_COUNT; i++) assert(k_plugin_at(i)->desc == &K_ENGINE[i] && (k_plugin_at(i)->kind == K_PLUG_DRUM) == K_ENGINE[i].drum);
        fz = K_ENGINE[0]; fz.name = "FUZZ"; fz.uid = K_UID('f','u','z','z');
        dz = K_ENGINE[k_engine_find(K_UID('D','R','U','M'))]; dz.name = "BOOM"; dz.uid = K_UID('b','o','o','m');
        assert(k_plugin_register(0) == K_PLUG_E_NULL);
        p.abi = 2; assert(k_plugin_register(&p) == K_PLUG_E_ABI); p.abi = K_PLUGIN_ABI;
        p.size = 8; assert(k_plugin_register(&p) == K_PLUG_E_ABI); p.size = sizeof(KPlugin);
        fz.uid = K_UID('F','U','Z','Z'); assert(k_plugin_register(&p) == K_PLUG_E_UID);   /* upper case and digits: Hügelton's */
        fz.uid = K_UID('A','N','L','G'); assert(k_plugin_register(&p) == K_PLUG_E_UID);
        fz.uid = K_UID('f','u','z',0); assert(k_plugin_register(&p) == K_PLUG_E_UID);
        fz.uid = K_UID('f','u','z','z');
        p.dsp = 0; assert(k_plugin_register(&p) == K_PLUG_E_KIND); p.dsp = &dummy_dsp;   /* a synth with no sound */
        fz.drum = 1; assert(k_plugin_register(&p) == K_PLUG_E_KIND); fz.drum = 0;   /* a synth that says it is a drum, with no lanes named */
        assert(k_plugin_lane_note(0, 0) == 36 && k_plugin_lane_note(0, 1) == 38 && k_plugin_lane_note(0, 3) == 46);   /* (General MIDI's when not said) */
        p.kind = K_PLUG_EFFECT; p.dsp = 0; assert(k_plugin_register(&p) == K_PLUG_E_KIND); p.kind = K_PLUG_SYNTH; p.dsp = &dummy_dsp;
        bad = fz; bad.uid = K_UID('b','a','d','1'); bad.macro[2] = (uint8_t)bad.nparams; p.desc = &bad; assert(k_plugin_register(&p) == K_PLUG_E_DESC);
        bad.macro[2] = 0; bad.edit[3].min = 50; bad.edit[3].max = 10; assert(k_plugin_register(&p) == K_PLUG_E_DESC);
        p.desc = &fz; fi = k_plugin_register(&p); assert(fi == K_ENGINE_COUNT && k_plugin_count() == n0 + 1);
        assert(k_plugin_register(&p) == K_PLUG_E_TAKEN);
        {   KPlugin q = {sizeof(KPlugin), K_PLUGIN_ABI, 1, K_PLUG_DRUM, &dz, 0};
            di = k_plugin_register(&q); assert(di == K_ENGINE_COUNT + 1); }
        assert(k_engine_find(K_UID('f','u','z','z')) == fi && k_engine_at(fi) == &fz && k_engine_uid(di) == K_UID('b','o','o','m'));
        {   KApp c; k_init(&c);   /* a track can have it: SOUND steps past the built-in ones; a drum plugin plays lanes */
            for (i = 0; i < 20; i++) eng(&c, 1, (unsigned)i);
            assert(c.sound[c.track] == k_plugin_count() - 1 && k_is_drum(&c, c.track));
            c.sound[2] = fi;   /* a song names it by its id, and finds it again */
            len = k_song_save(&c, buf, sizeof buf); assert(len > 0);
            { KApp d; k_init(&d); assert(k_song_load(&d, buf, len) == 0 && d.sound[2] == fi && d.sound[c.track] == di); }
        }
        for (i = 0; i < K_PLUGIN_SLOTS; i++) { static KEngine more[K_PLUGIN_SLOTS]; KPlugin m = p; more[i] = fz; more[i].uid = K_UID('m','o','r',(char)('a'+i)); m.desc = &more[i];
            int r = k_plugin_register(&m); assert(i < K_PLUGIN_SLOTS - 2 ? r >= 0 : r == K_PLUG_E_FULL); }
        k_plugin_reset(); assert(k_plugin_count() == K_ENGINE_COUNT && k_engine_find(K_UID('f','u','z','z')) == -1);
        {   /* what a plugin needs against a device's room: the installer asks k_plugin_fits, registering asks it again */
            static const KDeviceCaps fm1 = {44100u, 16384u, 120u, 6u, 0u};
            KPlugin n = p; n.desc = &fz;
            assert(k_plugin_fits(&n, &fm1, 0) == 0);                          /* nothing said: fits */
            n.fs = 48000u; assert(k_plugin_fits(&n, &fm1, 0) == K_PLUG_E_RATE); n.fs = 44100u;
            n.needs = K_NEED_FLOAT; assert(k_plugin_fits(&n, &fm1, 0) == K_PLUG_E_NEED); n.needs = 0;
            n.ram = 12000u; assert(k_plugin_fits(&n, &fm1, 0) == 0 && k_plugin_fits(&n, &fm1, 8000u) == K_PLUG_E_RAM);
            n.cost = 21u; assert(k_plugin_fits(&n, &fm1, 0) == K_PLUG_E_CPU); n.cost = 20u; assert(k_plugin_fits(&n, &fm1, 0) == 0);
            k_plugin_set_caps(&fm1);
            assert(k_plugin_register(&n) == K_ENGINE_COUNT);                  /* 12 KB taken */
            {   static KEngine f2; KPlugin m = n; f2 = fz; f2.uid = K_UID('f','u','z','2'); m.desc = &f2;
                assert(k_plugin_register(&m) == K_PLUG_E_RAM); }              /* another 12 KB does not fit beside it */
            {   KPlugin old = p; old.size = K_PLUGIN_SIZE_1; static KEngine f3; f3 = fz; f3.uid = K_UID('f','u','z','3'); old.desc = &f3;
                assert(k_plugin_register(&old) >= 0 && k_plugin_at(k_plugin_count() - 1)->ram == 0); }   /* a record from before the needs */
            bad = fz; bad.uid = K_UID('b','a','d','2'); bad.page2 = (uint8_t)(bad.nparams + 1); n.desc = &bad; n.ram = 0;
            assert(k_plugin_register(&n) == K_PLUG_E_DESC);                  /* EDIT past its parameters */
            k_plugin_set_caps(0); k_plugin_reset();
        }
        {   /* a song naming a plugin that is not here, as a DAW keeps it: the track silent and NO PLUGIN, its id, version and values
             * kept untouched and written back; the plugin back, the sound is as it was; another engine chosen lets it go */
            KApp m, back; static uint8_t b2[8192]; size_t l2; int f2, v2;
            fz.uid = K_UID('f','u','z','z');
            f2 = k_plugin_register(&p); assert(f2 == K_ENGINE_COUNT);
            k_init(&m); m.sound[2] = f2; m.params[2][K_PAGE_SOUND][4 + 2] = 77; m.params[2][K_PAGE_SOUND][4 + 12] = 33;   /* (DTN, CUT: wide ranges) */
            len = k_song_save(&m, buf, sizeof buf); assert(len > 0);
            k_plugin_reset();   /* the plugin is gone */
            k_init(&m); m.clock_ms = 500; assert(k_song_load(&m, buf, len) == 0);
            assert(k_track_missing(&m, 2) && m.missing[2].uid == K_UID('f','u','z','z') && m.missing[2].version == 3 && m.missing[2].n == fz.nparams);
            assert(m.sound[2] == 0 && !k_track_missing(&m, 0) && m.balloon.kind == K_BALLOON_MISSING);
            m.track = 2; k_open_page(&m, K_PAGE_SOUND); k_set_shown(&m, 4);
            v2 = m.params[2][K_PAGE_SOUND][4]; act(&m, K_ACT_FOCUS_NEXT, 0, 0, 600); act(&m, K_ACT_PARAM_DELTA, 0, 9, 601);
            assert(m.params[2][K_PAGE_SOUND][4] == v2);   /* its knobs do not turn */
            k_open_page(&m, K_PAGE_HOME); act(&m, K_ACT_PARAM_DELTA, 0, 9, 602); assert(m.params[2][K_PAGE_SOUND][4] == v2);
            l2 = k_song_save(&m, b2, sizeof b2); assert(l2 > 0);   /* saved again without the plugin ... */
            f2 = k_plugin_register(&p); assert(f2 == K_ENGINE_COUNT);   /* ... and the plugin back */
            k_init(&back); assert(k_song_load(&back, b2, l2) == 0);
            assert(!k_track_missing(&back, 2) && back.sound[2] == f2 && back.params[2][K_PAGE_SOUND][4 + 2] == 77 && back.params[2][K_PAGE_SOUND][4 + 12] == 33);
            k_plugin_reset(); k_init(&m); assert(k_song_load(&m, b2, l2) == 0 && k_track_missing(&m, 2));
            m.track = 2; eng(&m, 1, 700); assert(!k_track_missing(&m, 2) && m.sound[2] == 1);   /* replaced: let go */
            l2 = k_song_save(&m, b2, sizeof b2); k_init(&back); assert(k_song_load(&back, b2, l2) == 0 && !k_track_missing(&back, 2) && back.sound[2] == 1);
        }
    }
    {   /* the balloon: dissolves in once, holds while a knob keeps turning (no blink), dissolves out */
        KBalloon b={0};
        assert(k_balloon_alpha(&b,0)==0 && !k_balloon_active(&b,0));
        k_balloon_show(&b,K_BALLOON_MASTER,1000);
        assert(k_balloon_alpha(&b,1000)==255);   /* there at once */
        k_balloon_show(&b,K_BALLOON_MASTER,1500);assert(b.in_ms==1000 && k_balloon_alpha(&b,1500)==255);                                    /* the same again: it holds, no new dissolve */
        assert(k_balloon_alpha(&b,3000)==255 && k_balloon_alpha(&b,3125)>100 && k_balloon_alpha(&b,3125)<150 && k_balloon_alpha(&b,3300)==0);   /* out, from the last refresh */
        k_balloon_show(&b,K_BALLOON_OCTAVE,3400);assert(b.in_ms==3400);                                                                    /* another one: in again */
        assert(k_balloon_alpha(&b,3390)==255);                                                                                             /* (a frame a little before the action's clock: not a glitch) */
    }
    {   /* ROLL step entry, after Felucca: armed and stopped the keys held together write a chord at the step in hand (the first of them
         * clearing it), and the cursor moves on when they are all up; playing, a key adds to the step being played */
        KApp r;k_init(&r);k_open_page(&r,K_PAGE_ROLL);
        assert(k_key_pitch(&r,4)==60 && k_key_pitch(&r,16)==54);                   /* the key of C: 53 + 7; the first black key: F# */
        act(&r,K_ACT_KEY,4,1,0);act(&r,K_ACT_KEY,4,0,1);assert(k_roll_count(&r,0,0,0)==1 && k_roll_at(&r,0,0,0,60)>=0 && r.cur[0]==0);   /* not armed: nothing written */
        act(&r,K_ACT_REC,0,0,2);assert(r.recording);
        act(&r,K_ACT_KEY,2,1,3);assert(k_roll_count(&r,0,0,0)==1 && k_roll_at(&r,0,0,0,k_key_pitch(&r,2))>=0 && (r.steps[0][0]&1) && r.cur[0]==0);
        act(&r,K_ACT_KEY,2,0,4);assert(r.cur[0]==1);                                  /* released: on to the next step */
        r.octave=1;act(&r,K_ACT_KEY,16,1,6);act(&r,K_ACT_KEY,7,1,7);   /* a black key an octave up, and a white one: a chord */
        assert(k_roll_count(&r,0,0,1)==2 && k_roll_at(&r,0,0,1,54+12)>=0 && k_roll_at(&r,0,0,1,k_key_pitch(&r,7))>=0);
        act(&r,K_ACT_KEY,16,0,8);assert(r.cur[0]==1);                                 /* another still down: not yet */
        act(&r,K_ACT_KEY,7,0,9);assert(r.cur[0]==2);
        act(&r,K_ACT_KEY,4,1,10);act(&r,K_ACT_KEY,4,0,11);act(&r,K_ACT_KEY,4,1,12);act(&r,K_ACT_KEY,4,0,13);   /* steps 2 and 3, one note each */
        r.cur[0]=1;act(&r,K_ACT_KEY,9,1,14);assert(k_roll_count(&r,0,0,1)==1);act(&r,K_ACT_KEY,9,0,15);   /* again on step 1: the chord replaced */
        act(&r,K_ACT_PLAY,0,0,16);r.step=5;act(&r,K_ACT_KEY,4,1,17);act(&r,K_ACT_KEY,7,1,18);   /* live: the step being played, both of them */
        assert(k_roll_count(&r,0,0,5)==2 && (r.steps[0][0]>>5&1));
        act(&r,K_ACT_KEY,4,0,19);act(&r,K_ACT_KEY,7,0,20);assert(r.cur[0]==5);
        {   /* MIDI, stopped and armed: the same (a chord while notes are held, on when all are let go) */
            act(&r,K_ACT_PLAY,0,0,21);r.cur[0]=8;
            k_note_in(&r,48,90);k_note_in(&r,52,80);assert(k_roll_count(&r,0,0,8)==2 && r.cur[0]==8);
            k_note_in(&r,48,0);assert(r.cur[0]==8);k_note_in(&r,52,0);assert(r.cur[0]==9);
        }
        {   /* the roll's own: add (the same step and pitch again: that one), at (over its length), set, remove, full */
            int i,n;k_init(&r);
            n=k_roll_add(&r,0,1,4,64,3,100);assert(n>=0 && k_roll_add(&r,0,1,4,64,2,50)==n && r.roll[0][n].len==2 && (r.steps[0][1]>>4&1));
            assert(k_roll_at(&r,0,1,5,64)==n && k_roll_at(&r,0,1,6,64)<0 && k_roll_at(&r,0,0,4,64)<0);
            k_roll_set(&r,0,n,14,70,8);assert((r.roll[0][n].at&15)==14 && r.roll[0][n].len==2 && !(r.steps[0][1]>>4&1) && (r.steps[0][1]>>14&1));
            k_roll_remove(&r,0,n);assert(!r.steps[0][1]);
            for(i=0;i<K_ROLL_NOTES;i++)assert(k_roll_add(&r,1,i/16%4,i%16,40+i/64,1,100)>=0);
            assert(k_roll_add(&r,1,0,0,99,1,100)<0);   /* (full) */
        }
    }
    {   /* a device with three knobs: the parameters are a window of three over four, and it slides with the focus */
        KApp b;int i;k_init(&b);k_open_page(&b,K_PAGE_BUS1);   /* (four plain parameters) */
        assert(b.shown==4 && k_params_visible(&b)==4 && b.win==0);
        k_set_shown(&b,0);assert(b.shown==1);
        k_set_shown(&b,99);assert(b.shown==K_MAX_PARAMS && k_params_visible(&b)==4);   /* never more than the page has */
        k_set_shown(&b,3);assert(k_params_visible(&b)==3 && b.focus_count==3);
        for(i=0;i<b.focus_count;i++)if(b.focus[i].kind==K_NODE_PARAM)assert(b.focus[i].index<3);
        act(&b,K_ACT_PARAM_DELTA,2,7,0);assert(b.params[0][b.page][2]==57);
        assert(b.focus[b.focused].kind==K_NODE_PARAM && b.focus[b.focused].index==2);
        act(&b,K_ACT_PARAM_DELTA,3,7,0);assert(b.params[0][b.page][3]==88);          /* a knob that is not there does nothing (RTN: 0 dB) */
        act(&b,K_ACT_RIGHT,0,0,0);assert(b.win==1);                                    /* past the edge: the window slides */
        assert(b.focus[b.focused].kind==K_NODE_PARAM && b.focus[b.focused].index==3);
        act(&b,K_ACT_PARAM_DELTA,0,5,0);assert(b.params[0][b.page][1]==55);           /* knob 1 now turns parameter 2 */
        act(&b,K_ACT_PARAM_DELTA,2,5,0);assert(b.params[0][b.page][3]==93 && b.focus[b.focused].index==3);
        act(&b,K_ACT_FOCUS_NEXT,0,0,0);assert(b.win==1);                               /* nothing beyond: it wraps, no slide */
        b.focused=0;
        for(i=0;i<b.focus_count;i++)if(b.focus[i].kind==K_NODE_PARAM&&b.focus[i].index==1)b.focused=i;
        act(&b,K_ACT_LEFT,0,0,0);assert(b.win==0 && b.focus[b.focused].index==0);     /* and back */
        act(&b,K_ACT_FOCUS_PREV,0,0,0);assert(b.win==0);
        b.win=1;k_open_page(&b,K_PAGE_FX);assert(b.win==0 && b.focus_count==3);        /* a page opens at its first parameters */
    }
    {   /* a card opens for each parameter touched or turned; knobs can turn together */
        KApp b;k_init(&b);k_open_page(&b,K_PAGE_ENV1);
        assert(b.touched==0);
        act(&b,K_ACT_PARAM_DELTA,1,2,100);assert(b.touched==2u && b.touch_ms[1]==100);
        act(&b,K_ACT_PARAM_DELTA,3,2,150);assert(b.touched==10u && b.touch_ms[1]==100 && b.touch_ms[3]==150);   /* two at once */
        k_open_page(&b,K_PAGE_FX);assert(b.touched==0);                                 /* a new page starts shut */
        b.focused=0;act(&b,K_ACT_VALUE_DELTA,3,0,200);assert(b.touched==1u && b.touch_ms[0]==200);
        b.touched=0;
        b.focus[1].bounds=(KRect){10,10,20,20};act(&b,K_ACT_POINTER,15,15,300);assert(b.touched==2u && b.touch_ms[1]==300);
    }
    {   /* the beat counts while playing: 500 ms a beat at 120 BPM */
        KApp b;k_init(&b);k_open_page(&b,K_PAGE_STEP);
        assert(b.beat==-1);k_tick(&b,100);assert(b.beat==-1);
        act(&b,K_ACT_PLAY,0,0,1000);assert(b.playing && b.play_ms==1000);
        k_tick(&b,1000);assert(b.beat==0);k_tick(&b,1499);assert(b.beat==0);k_tick(&b,1500);assert(b.beat==1);
        k_tick(&b,2999);assert(b.beat==3);k_tick(&b,3000);assert(b.beat==0);
        act(&b,K_ACT_PLAY,0,0,3100);assert(!b.playing && b.beat==-1);
    }
    {   /* the battery is the platform's to report */
        KApp b;k_init(&b);k_open_page(&b,K_PAGE_STEP);assert(b.battery==-1);
        k_set_battery(&b,3,1);assert(b.battery==3 && b.charging);
        k_set_battery(&b,9,0);assert(b.battery==4 && !b.charging);
        k_set_battery(&b,-5,0);assert(b.battery==-1);
    }
    {   /* live REC (playing, armed): a note's length is the steps its key was held, decided at its release */
        KApp r;int k;k_init(&r);k_open_page(&r,K_PAGE_ROLL);memset(r.roll,0,sizeof r.roll);memset(r.steps,0,sizeof r.steps);   /* (an empty pattern: no note to stop at) */
        act(&r,K_ACT_REC,0,0,1);r.playing=1;r.step=2;
        act(&r,K_ACT_KEY,4,1,2);k=k_roll_at(&r,0,0,2,60);assert(k>=0 && r.roll[0][k].len==1);
        r.step=5;act(&r,K_ACT_KEY,4,0,3);assert(r.roll[0][k].len==3);
        r.step=14;act(&r,K_ACT_KEY,4,1,4);r.step=1;act(&r,K_ACT_KEY,4,0,5);   /* held over the pattern's end: to its end */
        k=k_roll_at(&r,0,0,14,60);assert(k>=0 && r.roll[0][k].len==2);
        r.step=7;act(&r,K_ACT_KEY,4,1,6);act(&r,K_ACT_KEY,4,0,7);k=k_roll_at(&r,0,0,7,60);assert(k>=0 && r.roll[0][k].len==1);   /* a tap: a step */
    }
    {   /* ROLL (the FM-1's column): SELECT walks a chord's notes low to high before the next step, and back high to low */
        KApp r;k_init(&r);k_open_page(&r,K_PAGE_ROLL);
        assert(k_roll_add(&r,0,0,1,67,1,100)>=0&&k_roll_add(&r,0,0,1,60,1,100)>=0&&k_roll_add(&r,0,0,1,64,1,100)>=0);
        r.cur[0]=0;act(&r,K_ACT_FOCUS_NEXT,0,0,1);assert(r.cur[0]==1&&r.rnote[0]==60);
        act(&r,K_ACT_FOCUS_NEXT,0,0,2);assert(r.cur[0]==1&&r.rnote[0]==64);
        act(&r,K_ACT_FOCUS_NEXT,0,0,3);assert(r.cur[0]==1&&r.rnote[0]==67);
        act(&r,K_ACT_FOCUS_NEXT,0,0,4);assert(r.cur[0]==2);
        act(&r,K_ACT_FOCUS_PREV,0,0,5);assert(r.cur[0]==1&&r.rnote[0]==67);   /* (back into a chord: at its top) */
        act(&r,K_ACT_FOCUS_PREV,0,0,6);assert(r.cur[0]==1&&r.rnote[0]==64);
    }
    {   /* REC toggles recording */
        KApp b;k_init(&b);k_open_page(&b,K_PAGE_STEP);assert(!b.recording);
        act(&b,K_ACT_REC,0,0,1);assert(b.recording);act(&b,K_ACT_REC,0,0,2);assert(!b.recording);
    }
    {   /* the tracks sound on their steps (a 16th is 125 ms at 120 BPM): the meter's hits */
        KApp b;k_init(&b);k_open_page(&b,K_PAGE_STEP);   /* track 0 has steps 0, 4, 8, 12 */
        act(&b,K_ACT_PLAY,0,0,1000);k_tick(&b,1000);
        assert(b.step==0 && b.hit_ms[0]==1000 && b.hit_v[0]==79 && b.hit_v[1]==0);
        k_tick(&b,1130);assert(b.step==1 && b.hit_ms[0]==1000);
        k_tick(&b,1500);assert(b.step==4 && b.hit_ms[0]==1500);
        b.params[0][K_PAGE_MIXER][0]=100;k_tick(&b,2000);assert(b.step==8 && b.hit_v[0]==90);   /* as hard as its fader is up */
        act(&b,K_ACT_PLAY,0,0,3000);assert(b.step==-1);
    }
    {   /* the mixer: four knobs are the track in hand's level, pan and two sends; ALGORITHM picks the track */
        KApp b;k_init(&b);k_open_page(&b,K_PAGE_MIXER);
        assert(k_param_count(K_PAGE_MIXER)==4 && k_params_visible(&b)==4 && b.focus_count==4+K_TRACKS);   /* (and the strips) */
        act(&b,K_ACT_PARAM_DELTA,0,10,1);assert(b.params[0][K_PAGE_MIXER][0]==98 && b.params[1][K_PAGE_MIXER][0]==88);   /* knob 1: track 1's level */
        act(&b,K_ACT_PARAM_DELTA,0,-13,1);assert(b.params[0][K_PAGE_MIXER][0]==88);   /* across 0 dB: it stops there */
        act(&b,K_ACT_PARAM_DELTA,0,-2,1);assert(b.params[0][K_PAGE_MIXER][0]==86);   /* and goes on */
        act(&b,K_ACT_PARAM_DELTA,0,2,1);
        act(&b,K_ACT_TRACK_DELTA,1,0,2);act(&b,K_ACT_PARAM_DELTA,1,-20,3);assert(b.params[1][K_PAGE_MIXER][1]==30 && b.params[0][K_PAGE_MIXER][1]==50);   /* track 2: its pan */
        act(&b,K_ACT_PARAM_DELTA,1,25,3);assert(b.params[1][K_PAGE_MIXER][1]==50);act(&b,K_ACT_PARAM_DELTA,1,-20,3);   /* the pan: the centre on the way */
        act(&b,K_ACT_PARAM_DELTA,3,5,4);assert(b.params[1][K_PAGE_MIXER][3]==5);                                    /* BUS2 */
        act(&b,K_ACT_SELECT_DELTA,1,0,5);assert(b.track==2 && b.page==K_PAGE_MIXER);act(&b,K_ACT_SELECT_DELTA,-2,0,6);assert(b.track==0);   /* SELECT: the strip */
    }
    {   /* the inserts: two slots, each an effect (a knob steps it), two parameters and a mix */
        KApp b;k_init(&b);k_open_page(&b,K_PAGE_FX);
        assert(k_param_count(K_PAGE_FX)==K_FX_ITEMS && k_page_count(&b, K_PAGE_FX)==8 && b.params[0][K_PAGE_FX][0]==0 && b.params[0][K_PAGE_FX][4]==0);   /* both empty */
        act(&b,K_ACT_PARAM_DELTA,0,5,1);assert(b.params[0][K_PAGE_FX][0]==1);                                 /* a detent: the next effect, not 5 */
        act(&b,K_ACT_PARAM_DELTA,0,-5,2);act(&b,K_ACT_PARAM_DELTA,0,-5,3);assert(b.params[0][K_PAGE_FX][0]==0);   /* back, and no lower than NONE */
        for(int i=0;i<K_FX_TYPES+5;i++)act(&b,K_ACT_PARAM_DELTA,0,2,4+i);assert(b.params[0][K_PAGE_FX][0]==K_FX_TYPES-1);    /* no higher than the last */
        act(&b,K_ACT_PARAM_DELTA,3,5,20);assert(b.params[0][K_PAGE_FX][3]==55);                                /* the mix: a percentage */
        act(&b,K_ACT_FOCUS_NEXT,0,0,21);assert(b.win==4);                                                     /* SELECT: the second slot */
        act(&b,K_ACT_PARAM_DELTA,0,1,22);  /* give FX2 an effect before editing it */
        act(&b,K_ACT_PARAM_DELTA,1,10,23);assert(b.params[0][K_PAGE_FX][5]==60);                              /* its first parameter */
    }
    {   /* the synth: the engine's own from K1, in its order (Kreisel's ANALOG: PITCH GLIDE DTN SYNC | WAVE1 LVL1 WAVE2 LVL2 |
         * SUB SUBP NOIS COLOR; EDIT CUT RES DRV KTR | TYPE ENV LEVEL VEL); item i is the engine's ith, kept at params[SOUND][4 + i] */
        KApp b;k_init(&b);k_open_page(&b,K_PAGE_SOUND);
        assert(k_param_count(K_PAGE_SOUND)==K_ENGINE_PARAMS && b.sound[0]==0 && b.params[0][K_PAGE_SOUND][8]==0 && b.params[0][K_PAGE_SOUND][16]==90);   /* ANALOG's defaults */
        assert(k_param(&b,K_PAGE_SOUND,0)==&b.params[0][K_PAGE_SOUND][4]);
        act(&b,K_ACT_TYPE_DELTA,5,0,1);assert(b.sound[0]==1);act(&b,K_ACT_TYPE_DELTA,-5,0,2);assert(b.sound[0]==0);     /* ALGORITHM: the engine, one at a time */
        act(&b,K_ACT_TYPE_DELTA,1,0,3);assert(b.sound[0]==1 && b.params[0][K_PAGE_SOUND][4]==0);                     /* LOFI at its defaults (CHIP 0) */
        for(int i=0;i<12;i++)act(&b,K_ACT_TYPE_DELTA,2,0,4+i);assert(b.sound[0]==K_ENGINE_COUNT-1);                     /* the last engine, no more */
        for(int i=0;i<12;i++)act(&b,K_ACT_TYPE_DELTA,-2,0,21+i);assert(b.sound[0]==0);                                   /* back to ANALOG */
        act(&b,K_ACT_PARAM_DELTA,0,5,30);assert(b.params[0][K_PAGE_SOUND][4]==1);                                      /* K1: PITCH, a semitone a detent */
        act(&b,K_ACT_PARAM_DELTA,2,40,31);assert(b.params[0][K_PAGE_SOUND][6]==48);                                    /* K3: DTN, 0..127, by the turn */
        for(int i=0;i<5;i++)act(&b,K_ACT_PARAM_DELTA,2,100,32+i);assert(b.params[0][K_PAGE_SOUND][6]==127);            /* clamped to the range */
        act(&b,K_ACT_PARAM_DELTA,3,1,38);assert(b.params[0][K_PAGE_SOUND][7]==1);                                      /* K4: SYNC on */
        act(&b,K_ACT_FOCUS_NEXT,0,0,59);assert(b.win==4);                                                              /* WAVE1 LVL1 WAVE2 LVL2 */
        act(&b,K_ACT_PARAM_DELTA,0,5,60);assert(b.params[0][K_PAGE_SOUND][8]==1);                                      /* WAVE1, an enum, one at a time */
        for(int i=0;i<9;i++)act(&b,K_ACT_PARAM_DELTA,0,2,61+i);assert(b.params[0][K_PAGE_SOUND][8]==6);                /* ... to its last (P37) */
        act(&b,K_ACT_PARAM_DELTA,2,5,70);assert(b.params[0][K_PAGE_SOUND][10]==1);                                     /* WAVE2 */
        act(&b,K_ACT_FOCUS_NEXT,0,0,71);assert(b.win==8);act(&b,K_ACT_PARAM_DELTA,1,-5,72);assert(b.params[0][K_PAGE_SOUND][13]==0);   /* SUBP: -2 */
        act(&b,K_ACT_FOCUS_NEXT,0,0,73);assert(b.win==8);                                                              /* SOUND ends there: it stays */
        k_open_page(&b,K_PAGE_EDIT);   /* EDIT: VCF (and VCA): ANALOG's second half, CUT RES DRV KTR */
        assert(!strcmp(k_engine_at(b.sound[0])->edit[k_engine_at(b.sound[0])->page2].label,"CUT") && k_param(&b,K_PAGE_EDIT,0)==&b.params[0][K_PAGE_SOUND][16]);
        act(&b,K_ACT_PARAM_DELTA,0,5,74);assert(b.params[0][K_PAGE_SOUND][16]==95);
        act(&b,K_ACT_PARAM_DELTA,3,-10,75);assert(b.params[0][K_PAGE_SOUND][19]==54);                                   /* KTR */
        act(&b,K_ACT_FOCUS_NEXT,0,0,76);assert(b.win==4);                                                              /* TYPE ENV | LEVEL VEL */
        act(&b,K_ACT_PARAM_DELTA,0,5,77);assert(b.params[0][K_PAGE_SOUND][20]==1);                                     /* TYPE: BP */
        {int e0=b.params[0][K_PAGE_ENV_DEST][1];act(&b,K_ACT_PARAM_DELTA,1,10,78);assert(b.params[0][K_PAGE_ENV_DEST][1]==e0+10);}   /* ENV: the host's ENV 1 -> filter */
        act(&b,K_ACT_PARAM_DELTA,2,-27,79);assert(b.params[0][K_PAGE_SOUND][22]==100);                                 /* LEVEL */
        act(&b,K_ACT_FOCUS_NEXT,0,0,80);assert(b.win==4);                                                              /* two rows: it stays on the last */
        eng(&b,1,80);assert(b.sound[0]==1);                                                                           /* ALGORITHM steps it */
    }
    {   /* HOME: a short press goes HOME (and back); held, the settings, whose release does nothing */
        KApp s;k_init(&s);
        act(&s,K_ACT_SETTINGS,0,0,900);assert(s.settings_open&&s.settings_sel==K_SET_SCREEN_OFF);act(&s,K_ACT_BACK,0,0,910);   /* (no LEDs, no Animation: their rows not shown) */
        k_set_layout(&s,K_LAY_LEDS);k_open_page(&s,K_PAGE_MIXER);   /* (the FM-1: its LEDs) */
        act(&s,K_ACT_HOME_DOWN,0,0,1000);k_tick(&s,1100);act(&s,K_ACT_HOME_UP,0,0,1120);assert(s.page==K_PAGE_HOME && !s.settings_open);
        k_open_page(&s,K_PAGE_FX);
        act(&s,K_ACT_HOME_DOWN,0,0,2000);k_tick(&s,2510);assert(s.settings_open && s.settings_sel==0);act(&s,K_ACT_HOME_UP,0,0,2600);assert(s.settings_open && s.page==K_PAGE_FX);   /* held: the settings over the page */
        act(&s,K_ACT_PARAM_DELTA,0,2,2610);assert(s.set_v[K_SET_LEDS]==1 && !strcmp(k_setting_value(&s,K_SET_LEDS),"Dim"));
        act(&s,K_ACT_TYPE_DELTA,1,0,2620);assert(k_setting_tab(s.settings_sel)==1 && s.settings_sel==K_SET_RESTORE);   /* ALGORITHM: the tab */
        act(&s,K_ACT_FOCUS_NEXT,0,0,2630);assert(s.settings_sel==K_SET_CLEAR);
        k_roll_fill(&s,0,0,0x5555,60);s.bpm=90;
        act(&s,K_ACT_PARAM_DELTA,1,2,2640);assert(s.set_ask && s.steps[0][0]==0x5555);   /* CLEAR ALL: once arms it */
        act(&s,K_ACT_FOCUS_NEXT,0,0,2650);assert(!s.set_ask && s.settings_sel==K_SET_CLEAR);   /* anything else: no */
        act(&s,K_ACT_PARAM_DELTA,1,2,2660);act(&s,K_ACT_PARAM_DELTA,1,2,2670);
        assert(!s.settings_open && s.steps[0][0]==0 && s.bpm==120 && s.set_v[K_SET_LEDS]==1);   /* twice: a new song, the settings kept */
        act(&s,K_ACT_SETTINGS,0,0,2700);assert(s.settings_open);act(&s,K_ACT_BACK,0,0,2710);assert(!s.settings_open);
        k_roll_fill(&s,0,0,0x00FF,60);act(&s,K_ACT_SETTINGS,0,0,2720);   /* CLEAR ALL by taps: the row, armed, cleared */
        act(&s,K_ACT_POINTER,-1,K_SET_CLEAR,2730);assert(s.settings_sel==K_SET_CLEAR&&!s.set_ask);
        act(&s,K_ACT_POINTER,-1,K_SET_CLEAR,2740);assert(s.set_ask);
        act(&s,K_ACT_POINTER,-1,K_SET_CLEAR,2750);assert(!s.settings_open&&s.steps[0][0]==0);
        act(&s,K_ACT_SETTINGS,0,0,2760);act(&s,K_ACT_SAVE,0,0,2770);assert(s.settings_open&&!s.save_open);act(&s,K_ACT_BACK,0,0,2780);   /* SAVE there is theirs */
    }
    {   /* SYNTH's rows: SOUND has the engine's first half from K1 (ANALOG: twelve, three rows), EDIT the rest; SELECT steps no
         * further; the page says how to move its rows in a balloon */
        KApp w; k_init(&w); w.track = 0; k_open_page(&w, K_PAGE_SOUND); k_set_shown(&w, 4);
        assert(w.balloon.kind == K_BALLOON_HINT && k_engine_at(0)->nparams == 20 && k_engine_at(0)->page2 == 12 && k_engine_at(k_engine_find(K_UID('K','O','G','N')))->nparams == 18);
        assert(!strcmp(k_engine_at(0)->edit[0].label, "PITCH") && !strcmp(k_engine_at(0)->edit[19].label, "VEL"));
        w.sound[0] = k_engine_find(K_UID('K','O','G','N')); k_open_page(&w, K_PAGE_SOUND);   /* ORGAN: ROTR VIB PERC 16', then the other eight bars = three rows */
        act(&w, K_ACT_FOCUS_NEXT, 0, 0, 6); assert(w.win == 4); act(&w, K_ACT_FOCUS_NEXT, 0, 0, 7); assert(w.win == 8);
        act(&w, K_ACT_FOCUS_NEXT, 0, 0, 8); assert(w.win == 8);   /* the last: it stays */
        k_open_page(&w, K_PAGE_MIXER); assert(K_PAGE_BALLOON ? w.balloon.kind == K_BALLOON_PAGE : w.balloon.kind != K_BALLOON_PAGE);   /* (the page's balloon: hidden) */
    }
    {   /* DRUM: SOUND turns the lane in hand; another lane, another piece */
        KApp d; int dr = k_engine_find(K_UID('D','R','U','M')), p0; k_init(&d); d.track = 0; d.sound[0] = dr; k_set_shown(&d, 4); k_open_page(&d, K_PAGE_SOUND);
        d.cur_lane[0] = 2; p0 = d.lane_p[0][2][0];
        act(&d, K_ACT_PARAM_DELTA, 0, 1, 1);   /* K1: PIECE */
        assert(d.lane_p[0][2][0] == p0 + 1 && d.lane_p[0][3][0] == 22 && k_param_get(&d, K_PAGE_SOUND, 0) == p0 + 1);
        d.cur_lane[0] = 0; assert(k_param_get(&d, K_PAGE_SOUND, 0) == d.lane_p[0][0][0]);
        assert(k_engine_at(dr)->nparams == 14 && k_engine_at(dr)->page2 == 10 && K_LANES == 8);
        /* SOUND has all of the lane's: PIECE LVL PAN TUNE | TONE DCAY CLIK ATK | PDEP PTIM; no ENV or LFO page */
        d.cur_lane[0] = 1;
        act(&d, K_ACT_PARAM_DELTA, 2, 5, 2); assert(d.lane_p[0][1][K_LANE_PAN] == 55);   /* K3: PAN */
        assert(k_param(&d, K_PAGE_SOUND, 6) == &d.lane_p[0][1][K_LANE_ENV + 3] && k_param(&d, K_PAGE_SOUND, 7) == &d.lane_p[0][1][K_LANE_ENV]);   /* CLIK, ATK */
        assert(k_param(&d, K_PAGE_SOUND, 3) == &d.lane_p[0][1][1] && k_param(&d, K_PAGE_SOUND, 9) == &d.lane_p[0][1][K_LANE_ENV + 2]);   /* TUNE, PTIM */
        assert(k_page_unused(&d, K_PAGE_ENV1) && k_page_unused(&d, K_PAGE_LFO));
        d.sound[0] = 0; assert(k_page_count(&d, K_PAGE_LFO) == 8 && k_page_count(&d, K_PAGE_ENV1) == 8);   /* a synth: as before */
        {   /* LFO PIT, bipolar: an FM-1 detent is 2, and from an odd value the next lands on 0, not past it */
            KApp l; k_init(&l); k_open_page(&l, K_PAGE_LFO); l.params[0][K_PAGE_LFO][4] = 3;
            act(&l, K_ACT_FOCUS_NEXT, 0, 0, 0); assert(l.win == 4);   /* the second row: PIT FLT SHP AMP */
            act(&l, K_ACT_PARAM_DELTA, 0, -2, 1); assert(l.params[0][K_PAGE_LFO][4] == 1);
            act(&l, K_ACT_PARAM_DELTA, 0, -2, 2); assert(l.params[0][K_PAGE_LFO][4] == 0);
            act(&l, K_ACT_PARAM_DELTA, 0, -2, 3); assert(l.params[0][K_PAGE_LFO][4] == -2);
        }
    }
    {   /* an organ plays its own gate and reads no cutoff or shape modulation (shown dim); the track's ENV 1 is not touched,
         * so the engine after it gets the envelope back (it once kept the organ's gate) */
        KApp o; int og = k_engine_find(K_UID('K','O','G','N')), i; KVal env[4]; k_init(&o); o.track = 0; k_set_shown(&o, 4); k_open_page(&o, K_PAGE_SOUND);
        o.params[0][K_PAGE_ENV1][0] = 37; o.params[0][K_PAGE_ENV1][3] = 61; memcpy(env, o.params[0][K_PAGE_ENV1], sizeof env);
        assert(og >= 0 && k_engine_at(og)->gate && k_engine_at(og)->nomod == (K_NOMOD_FILTER | K_NOMOD_SHAPE) && !k_engine_at(0)->gate && !k_engine_at(0)->nomod);
        for (i = 0; i < og; i++) act(&o, K_ACT_TYPE_DELTA, 1, 0, (unsigned)(10 + i));
        assert(o.sound[0] == og && !memcmp(o.params[0][K_PAGE_ENV1], env, sizeof env));
        act(&o, K_ACT_TYPE_DELTA, 1, 0, 90); assert(o.sound[0] != og && !memcmp(o.params[0][K_PAGE_ENV1], env, sizeof env));
    }
    {   /* a wide screen pairs ENV 1 and ENV 2 on one page: ENV 2 opens it, items 8 .. are ENV 2's, four rows */
        KApp e; int v; k_init(&e); k_set_shown(&e, 4); k_set_layout(&e, K_LAY_ENV_PAIR);
        k_open_page(&e, K_PAGE_ENV2); assert(e.page == K_PAGE_ENV1 && e.win == 8 && k_page_count(&e, K_PAGE_ENV1) == 16);
        assert(k_param(&e, K_PAGE_ENV1, 8) == &e.params[e.track][K_PAGE_ENV2][0] && k_param(&e, K_PAGE_ENV1, 12) == &e.params[e.track][K_PAGE_ENV_DEST][3]);
        v = e.params[e.track][K_PAGE_ENV2][1]; act(&e, K_ACT_PARAM_DELTA, 1, 3, 1); assert(e.params[e.track][K_PAGE_ENV2][1] == v + 3);   /* K2: HOLD on selected right page */
        act(&e, K_ACT_FOCUS_NEXT, 0, 0, 2); assert(e.win == 12); act(&e, K_ACT_FOCUS_NEXT, 0, 0, 3); assert(e.win == 12);   /* (no wrap) */
        act(&e, K_ACT_FOCUS_PREV, 0, 0, 4); assert(e.win == 8); act(&e, K_ACT_FOCUS_PREV, 0, 0, 5); assert(e.win == 4);
        k_set_layout(&e, 0); k_open_page(&e, K_PAGE_ENV2); assert(e.page == K_PAGE_ENV2 && k_page_count(&e, K_PAGE_ENV1) == 8);   /* a square screen: two pages */
        k_set_layout(&e, K_LAY_ENV_PAIR);
        k_open_page(&e, K_PAGE_ENV2); assert(e.page == K_PAGE_ENV1 && e.win == 8);   /* (ENV 2 on a wide screen: ENV 1's second half) */
        act(&e, K_ACT_PAGE_MENU, 0, 0, 103);
        assert(e.menu_open && e.menu_group == K_GROUP_ALL);
        assert(k_menu_page(&e, e.menu_selected) == K_PAGE_ENV2);
        act(&e, K_ACT_BACK, 0, 0, 104);
        assert(!e.menu_open && e.page == K_PAGE_ENV1 && e.win == 8);
    }
    {   /* a switch of engine loses nothing: each engine's values are kept for the track, turning back brings the sound back */
        KApp m; int v0; k_init(&m); m.track = 1; k_open_page(&m, K_PAGE_SOUND); k_set_shown(&m, 4);
        m.params[1][K_PAGE_SOUND][4 + 2] = 99; v0 = m.sound[1];   /* (ANALOG DTN) */
        act(&m, K_ACT_TYPE_DELTA, 1, 0, 10); assert(m.sound[1] == v0 + 1);   /* ALGORITHM: the next engine, at its defaults */
        assert(m.params[1][K_PAGE_SOUND][4 + 2] == k_engine_at(v0 + 1)->edit[2].def);
        m.params[1][K_PAGE_SOUND][4 + 2] = 5;   /* a sound made on it too */
        act(&m, K_ACT_TYPE_DELTA, -1, 0, 11); assert(m.sound[1] == v0 && m.params[1][K_PAGE_SOUND][4 + 2] == 99);   /* back: as it was */
        eng(&m, 1, 12); assert(m.params[1][K_PAGE_SOUND][4 + 2] == 5);   /* (again: kept) */
        assert(m.eng_mem_set[0] == 0);   /* another track's are its own */
    }
    {   /* ORGAN has no ENV or LFO pages: they say so, their knobs turn nothing; ANALOG's still do */
        KApp o; int org = k_engine_find(K_UID('K','O','G','N')), v;
        k_init(&o); o.sound[0] = org; k_open_page(&o, K_PAGE_ENV1);
        assert(k_page_unused(&o, K_PAGE_ENV1) && k_page_unused(&o, K_PAGE_LFO) && !k_page_unused(&o, K_PAGE_SOUND) && !k_page_unused(&o, K_PAGE_FX));
        v = o.params[0][K_PAGE_ENV1][0]; act(&o, K_ACT_PARAM_DELTA, 0, 5, 1); assert(o.params[0][K_PAGE_ENV1][0] == v);
        o.sound[0] = 0; assert(!k_page_unused(&o, K_PAGE_ENV1));
        act(&o, K_ACT_PARAM_DELTA, 0, 5, 2); assert(o.params[0][K_PAGE_ENV1][0] != v);
    }
    {   /* the FM-1's encoders: SELECT the cursor, HOME held + SELECT the track (and HOME then stays), ALGORITHM the engine or the effect;
         * K1 over the engine turns nothing */
        KApp s; int f, e0; k_init(&s); k_set_shown(&s, 4); k_open_page(&s, K_PAGE_SOUND);
        f = s.win; act(&s, K_ACT_SELECT_DELTA, 1, 0, 1); assert(s.win == f + 4 && s.track == 0);   /* a row down */
        act(&s, K_ACT_HOME_DOWN, 0, 0, 2); act(&s, K_ACT_SELECT_DELTA, 1, 0, 3); assert(s.track == 1);
        act(&s, K_ACT_HOME_UP, 0, 0, 4); assert(s.page == K_PAGE_SOUND && s.track == 1);   /* (the hold was used: not HOME) */
        s.win = 0; e0 = s.sound[1]; act(&s, K_ACT_PARAM_DELTA, 0, 3, 5); assert(s.sound[1] == e0);   /* K1: not the engine */
        act(&s, K_ACT_TYPE_DELTA, 1, 0, 6); assert(s.sound[1] == e0 + 1);   /* ALGORITHM: it */
        k_open_page(&s, K_PAGE_FX); s.win = 0; act(&s, K_ACT_TYPE_DELTA, 1, 0, 7); assert(s.params[1][K_PAGE_FX][0] == 1);   /* FX: the effect in hand */
        k_open_page(&s, K_PAGE_MIXER); e0 = s.sound[1]; act(&s, K_ACT_TYPE_DELTA, 1, 0, 8); assert(s.sound[1] == e0);   /* elsewhere: nothing */
    }
    {   /* FX with ALGORITHM picking the type (K_LAY_FX_ALGO, the FM-1): a slot's row is P1 P2 P3 MIX, the rest four a row after */
        KApp f; int s, k;
        k_init(&f); k_set_layout(&f, K_LAY_FX_ALGO); f.params[0][K_PAGE_FX][0] = 1;   /* DIST: DRIV TONE LVL */
        assert(k_fx_item(&f, 0, &s, &k) == 1 && s == 0 && k == 0);
        assert(k_fx_item(&f, 1, &s, &k) == 2 && k == 1);
        assert(k_fx_item(&f, 2, &s, &k) == k_fx_store_param(0, 2) && k == 2);
        assert(k_fx_item(&f, 3, &s, &k) == 3 && k == -2);
        assert(k_page_count(&f, K_PAGE_FX) == 8);   /* (three parameters: no row more) */
        {   /* ALGORITHM turns the type of the slot in hand, not its P1 (item 0 under this layout) */
            int p1 = f.params[0][K_PAGE_FX][1];
            k_open_page(&f, K_PAGE_FX); f.win = 0;
            act(&f, K_ACT_TYPE_DELTA, 1, 0, 1); assert(f.params[0][K_PAGE_FX][0] == 2 && f.params[0][K_PAGE_FX][1] == p1);
            f.win = 4; act(&f, K_ACT_TYPE_DELTA, 1, 0, 2); assert(f.params[0][K_PAGE_FX][4] == 1);   /* the second slot */
            f.win = 0; act(&f, K_ACT_TYPE_DELTA, -1, 0, 3); f.params[0][K_PAGE_FX][4] = 0;
        }
        k_set_layout(&f, 0);
        assert(k_fx_item(&f, 0, &s, &k) == 0 && k == -1);   /* without it: TYPE first, as before */
    }
    {   /* the factory presets: every word sets something (a label, a list's name the engine has) */
        char bad[32] = "";
        int n = k_factory_check(bad, sizeof bad);
        if (n) printf("factory presets: %d words set nothing (first in %s)\n", n, bad);
        assert(n == 0);
    }
    {   /* stuck notes (2-3, 2-4): a release goes to the track the key played, whatever is in hand; a reset lets held notes go */
        KApp k;KNoteEv n;int got=0;k_init(&k);k.sound[1]=K_ENGINE_COUNT-1;   /* track 2 a drum track */
        while(k_take_note(&k,&n));
        act(&k,K_ACT_KEY,4,1,0);act(&k,K_ACT_TRACK_DELTA,1,0,1);assert(k.track==1);
        act(&k,K_ACT_KEY,4,0,2);
        while(k_take_note(&k,&n))if(n.vel==0&&n.track==0&&n.note==60)got=1;
        assert(got);   /* the synth's note-off reached track 1, not the drum track */
        k_note_in(&k,36,100);act(&k,K_ACT_TRACK_DELTA,-1,0,3);assert(k.track==0);k_note_in(&k,36,0);got=0;   /* (36: BD, the drum's first lane) */
        while(k_take_note(&k,&n))if(n.vel==0&&n.track==1&&n.note==0)got=1;
        assert(got);   /* MIDI too: to the track (the drum's lane) it went to */
        act(&k,K_ACT_KEY,5,1,4);k_init_released(&k);got=0;
        while(k_take_note(&k,&n))if(n.vel==0&&n.track==0)got=1;
        assert(got);   /* held through a reset: its note-off is queued after it */
        k_init(&k);for(int i=0;i<60;i++)k_note_in(&k,40+(i%20),100);k_note_in(&k,45,0);got=0;
        while(k_take_note(&k,&n))if(n.vel==0)got=1;
        assert(got);   /* the queue full: a note-off still gets in */
        {KBalloon b={0};k_balloon_show(&b,K_BALLOON_MASTER,1000);assert(!k_balloon_active(&b,1000u+0x80000000u)&&k_balloon_alpha(&b,1000u+0x80000000u)==0);}   /* 24.8 days on: gone */
    }
    {   /* the header's meter: a peak's dB over -48, at once up, ~30 dB a second down */
        KApp m;k_init(&m);
        k_set_out_peak(&m,32767,16384,1000);assert(m.out_lv[0]==100 && m.out_lv[1]>=85 && m.out_lv[1]<=89);   /* 0 dB; -6 dB ~ 87 */
        k_set_out_peak(&m,0,0,1500);assert(m.out_lv[0]>=66 && m.out_lv[0]<=70);   /* half a second: ~15 dB down */
        k_set_out_peak(&m,0,0,4000);assert(m.out_lv[0]==0 && m.out_lv[1]==0);
        k_set_out_peak(&m,150,0,4100);assert(m.out_lv[0]>0 && m.out_lv[0]<6);k_set_out_peak(&m,100,0,9000);assert(m.out_lv[0]==0);   /* -47 dB: just over the floor; -50: none */
    }
    {   /* GLO pressed again and again: SONG, BUS 1, BUS 2, round (the mixer is HOME's) */
        KApp g;int i;static const KPage W[4]={K_PAGE_SONG,K_PAGE_BUS1,K_PAGE_BUS2,K_PAGE_SONG};k_init(&g);
        for(i=0;i<4;i++){act(&g,K_ACT_PAGE_GROUP_DOWN,K_GROUP_GLO,0,100u*i);act(&g,K_ACT_PAGE_GROUP_UP,K_GROUP_GLO,0,100u*i+20);assert(g.page==W[i]);}
    }
    {   /* ARP: its notes kept for the page, the key of the one just played lit (folded onto the bed) */
        KApp r;int i,lit=-1;k_init(&r);k_open_page(&r,K_PAGE_ARP);r.clock_ms=1000;
        k_arp_note(&r,0,60,1000);assert(r.arp_n[0]==1 && r.arp_hist[0][0]==60);
        for(i=0;i<27;i++)if(k_key_lit(&r,i)){assert(lit<0);lit=i;}
        assert(lit==4);   /* C4: the bed's fifth white key (F G A B C) */
        k_arp_note(&r,0,96,1000);lit=-1;for(i=0;i<27;i++)if(k_key_lit(&r,i))lit=i;assert(lit>=0 && k_key_pitch(&r,lit)%12==0);   /* C7: folded in */
        r.clock_ms=1500;for(i=0;i<27;i++)assert(!k_key_lit(&r,i) || k_is_drum(&r,0));   /* 500 ms on, no next note: out */
    }
    {   /* LOFI's WAVE ends with OFF (the noise alone): in the generator's table, not only in a hand edit of engines_data.c */
        const KEngine *e=k_engine_at(k_engine_find(K_UID('K','L','O','F')));
        assert(e->edit[0].max==3 && e->edit[0].names && !strcmp(e->edit[0].names[3],"OFF"));
    }
    {   /* the Switch's SOUND (SYNTH_ALL): every item it shows maps to a parameter of its own, none past the page's slots */
        static KApp r;int L,e,i;const unsigned LAY[2]={K_LAY_SYNTH_ALL,K_LAY_SYNTH_ALL|K_LAY_ENV_PAIR};
        for(L=0;L<2;L++)for(e=0;e<k_engine_count();e++){
            const int np=k_engine_at(e)->nparams;int vis;
            k_init(&r);k_set_layout(&r,LAY[L]);r.sound[0]=e;k_open_page(&r,K_PAGE_SOUND);vis=k_params_visible(&r);
            for(i=np;i<vis;i++){KPage pg;int j;assert(k_synth_all_sub(&r,K_PAGE_SOUND,i,&pg,&j));}
            k_dispatch(&r,(KAction){K_ACT_PARAM_DELTA,vis+5,3,100});   /* (past the end: nothing out of bounds) */
        }
    }
    {   /* a full note queue: 40 notes on, then off, in one frame (none taken between): none left sounding */
        static KApp r;KNoteEv ev;int on[128]={0},i;k_init(&r);
        for(i=0;i<40;i++)k_note_in(&r,40+i,100);
        for(i=0;i<40;i++)k_note_in(&r,40+i,0);
        while(k_take_note(&r,&ev))on[ev.note]+=ev.vel?1:-1;
        for(i=0;i<128;i++)assert(on[i]<=0);
    }
    {   /* a MIDI note held on a synth track, the same note again on a drum track: the synth's let go */
        static KApp r;KNoteEv ev;int synth_on=0;k_init(&r);
        r.track=0;k_note_in(&r,38,100);
        r.sound[1]=k_engine_find(K_UID('D','R','U','M'));r.track=1;assert(k_is_drum(&r,1));k_note_in(&r,38,100);
        while(k_take_note(&r,&ev))if(ev.track==0)synth_on+=ev.vel?1:-1;
        assert(synth_on==0);
    }
    {   /* the arp's count never comes back to 0 ("none yet") */
        static KApp r;int i;k_init(&r);for(i=0;i<600;i++){k_arp_note(&r,0,60,1000);assert(r.arp_n[0]);}
    }
    {   /* arrived at a drum track (another track, or this one's engine now DRUM): the black keys' hint, HELP ON; not on a synth track */
        static KApp r;k_init(&r);r.sound[1]=k_engine_find(K_UID('D','R','U','M'));
        k_dispatch(&r,(KAction){K_ACT_TRACK_DELTA,1,0,2000});assert(r.hint.kind==K_BALLOON_HINT && r.hint.held_ms==2000);
        k_dispatch(&r,(KAction){K_ACT_TRACK_DELTA,1,0,3000});assert(r.hint.held_ms==2000);   /* (a synth track: none new) */
        r.set_v[K_SET_HELP]=1;k_dispatch(&r,(KAction){K_ACT_TRACK_DELTA,-1,0,4000});assert(r.hint.held_ms==2000);   /* HELP OFF: none */
    }
    {   /* a drum track playing: the key of the step playing, on every page; a synth track, or stopped: none */
        static KApp r;int i,n=0;k_init(&r);r.sound[0]=k_engine_find(K_UID('D','R','U','M'));
        r.playing=1;r.step=21;for(i=0;i<27;i++)n+=k_key_playing(&r,i);assert(n==1 && k_key_playing(&r,5));
        r.playing=0;assert(!k_key_playing(&r,5));r.playing=1;r.track=1;assert(!k_key_playing(&r,5));
    }
    {   /* the page menu as tiles (menu_cols): up / down a row, not past the ends; left / right one */
        static KApp r;k_init(&r);r.menu_open=1;r.menu_group=K_GROUP_ALL;r.menu_selected=5;r.menu_cols=4;
        k_dispatch(&r,(KAction){K_ACT_DOWN,0,0,1000});assert(r.menu_selected==9);
        k_dispatch(&r,(KAction){K_ACT_UP,0,0,1000});k_dispatch(&r,(KAction){K_ACT_UP,0,0,1000});assert(r.menu_selected==1);
        k_dispatch(&r,(KAction){K_ACT_UP,0,0,1000});assert(r.menu_selected==k_menu_count(&r));   /* up from the first row: Config */
        k_dispatch(&r,(KAction){K_ACT_ACCEPT,0,0,1000});assert(r.settings_open && !r.menu_open);   /* chosen: the settings */
        r.settings_open=0;r.menu_open=1;r.menu_selected=k_menu_count(&r);
        k_dispatch(&r,(KAction){K_ACT_DOWN,0,0,1000});assert(r.menu_selected==0);   /* down from Config: the first */
        k_dispatch(&r,(KAction){K_ACT_RIGHT,0,0,1000});k_dispatch(&r,(KAction){K_ACT_RIGHT,0,0,1000});assert(r.menu_selected==2);
        r.menu_cols=0;k_dispatch(&r,(KAction){K_ACT_DOWN,0,0,1000});assert(r.menu_selected==3);   /* (a list: one) */
    }
    {   /* MIDI CC: 7 the level, 10 the pan of the track in hand, 16..19 its macros, set where the controller is; others nothing */
        static KApp r;int lo,hi;const KParamDesc *d;k_init(&r);r.track=1;
        k_cc_in(&r,7,127);assert(r.params[1][K_PAGE_MIXER][0]==100);k_cc_in(&r,7,0);assert(r.params[1][K_PAGE_MIXER][0]==0);
        k_cc_in(&r,10,64);assert(r.params[1][K_PAGE_MIXER][1]==50);assert(r.params[0][K_PAGE_MIXER][1]==50);
        d=&k_engine_at(r.sound[1])->edit[k_engine_macro(r.sound[1],2)];lo=d->min;hi=d->max;
        k_cc_in(&r,18,0);assert(k_param_get(&r,K_PAGE_HOME,2)==lo);k_cc_in(&r,18,127);assert(k_param_get(&r,K_PAGE_HOME,2)==hi);
        {int was=r.params[1][K_PAGE_MIXER][0];k_cc_in(&r,1,99);k_cc_in(&r,20,5);assert(r.params[1][K_PAGE_MIXER][0]==was);}
    }
    {   /* the metronome (the settings' Metronome, Sound, Count-in; the fourth card is the step's chance): armed with
         * Count-in, PLAY counts a bar in (120 BPM: 2 s), no step and nothing written meanwhile */
        static KApp r;
        k_init(&r);r.page=K_PAGE_STEP;
        assert(k_param_count(K_PAGE_STEP)==4&&k_param_count(K_PAGE_DRUM)==4);
        assert(!strcmp(k_setting_name(K_SET_METRO),"Metronome")&&!strcmp(k_setting_value(&r,K_SET_METRO),"Off")&&!strcmp(k_setting_value(&r,K_SET_CLICK),"Beep"));
        r.set_v[K_SET_COUNT]=1;
        act(&r,K_ACT_PLAY,0,0,1000);assert(r.playing&&!r.count_in&&r.play_ms==1000);act(&r,K_ACT_PLAY,0,0,1100);
        act(&r,K_ACT_REC,0,0,1200);act(&r,K_ACT_PLAY,0,0,1300);assert(r.playing&&r.count_in&&r.play_ms==3300);
        k_tick(&r,1400);assert(r.step==-1&&r.beat==0);k_tick(&r,2900);assert(r.step==-1&&r.beat==3);
        r.page=K_PAGE_ROLL;r.track=0;{int before=k_roll_count(&r,0,r.pattern[0],r.cur[0]);
            k_note_in(&r,72,100);k_note_in(&r,72,0);assert(k_roll_count(&r,0,r.pattern[0],r.cur[0])==before);}   /* (counted in: not written) */
        k_tick(&r,3300);assert(r.step==0&&!r.count_in);
        k_tick(&r,3500);assert(r.step==1);
        act(&r,K_ACT_PLAY,0,0,3600);act(&r,K_ACT_REC,0,0,3700);
        r.clock_src=1;act(&r,K_ACT_REC,0,0,3800);act(&r,K_ACT_PLAY,0,0,3900);assert(r.playing&&!r.count_in);   /* (an external clock: none) */
    }
    {   /* the settings as a cursor's sheet (the Zero): up / down the rows, left / right the value, ALGORITHM the tab; a knobs device's left /
         * right (none on the FM-1, a desktop's arrows): the rows */
        static KApp s;
        k_init(&s);k_set_layout(&s,K_LAY_SHEET_CURSOR);act(&s,K_ACT_SETTINGS,0,0,10);s.settings_sel=K_SET_METRO;
        act(&s,K_ACT_RIGHT,0,0,20);assert(s.settings_sel==K_SET_METRO&&s.set_v[K_SET_METRO]==1);
        act(&s,K_ACT_LEFT,0,0,30);assert(s.set_v[K_SET_METRO]==0);
        act(&s,K_ACT_DOWN,0,0,40);assert(s.settings_sel==K_SET_CLICK);
        act(&s,K_ACT_TYPE_DELTA,-1,0,50);assert(k_setting_tab(s.settings_sel)==1);   /* (the tab before: System) */
        k_set_layout(&s,0);s.settings_sel=K_SET_METRO;act(&s,K_ACT_RIGHT,0,0,60);assert(s.settings_sel==K_SET_CLICK&&s.set_v[K_SET_METRO]==0);
    }
    puts("kreisel core tests: OK");
    return 0;
}

#include "external_assets.h"
#include "wave_schedule.h"
#include <string.h>

static uint8_t arena[WAVE_SCHEDULE_ARENA_BYTES];
static uint32_t loaded_id=WAVE_SCHEDULE_NONE, count, duration, spawn_end, po, io, crc;
static WaveScheduleState s;
static uint16_t u16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0]|((uint16_t)p[1]<<8)); }
static uint32_t u32(const uint8_t *p) { return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static const uint8_t *event(uint32_t i) { return arena+64u+12u*i; }
static int validate(uint32_t bytes) {
    uint32_t n,d,end,p,i,t,j=0,expected;
    if(bytes<64u || memcmp(arena,"ADWV1\0\0\0",8)!=0) return -1;
    n=u32(arena+20); d=u32(arena+24); end=u32(arena+60);
    if(!n || n>=65535u || !end || end>d || d>200000u) return -2;
    p=64u+12u*n; i=p+4096u;
    if(u32(arena+8)!=1u || u32(arena+12)!=64u || u32(arena+16)!=12u ||
       u32(arena+28)!=64u || u32(arena+32)!=p || u32(arena+36)!=1024u ||
       u32(arena+40)!=i || u32(arena+44)!=d+2001u || u32(arena+48)!=1u ||
       u32(arena+52)!=bytes || i+2u*(d+2001u)!=bytes) return -3;
    if(external_crc32(arena+64,bytes-64u)!=u32(arena+56)) return -4;
    for(t=0;t<n;t++) {
        const uint8_t *e=event(t);
        if(u32(e)>=end || (t && u32(e)<=u32(event(t-1u))) || e[6]>2u || e[7]>2u ||
           (e[6]==2u && e[7]==2u) || u16(e+4)>=(e[7]==2u?15u:430u) ||
           e[8]>1u || e[9] || u16(e+10)) return -5;
    }
    for(t=0;t<1024u;t++) {
        const uint8_t *c=arena+p+4u*t;
        /* ADWV1 pressure semantics are part of the runtime contract. */
        if(c[2]!=(uint8_t)(t<750u) || c[3]!=(uint8_t)(t<=720u) ||
           (c[2] && !u16(c)) || (!c[2] && u16(c))) return -6;
    }
    for(t=0;t<d+2001u;t++) {
        while(j<n && u32(event(j))<=t) ++j;
        expected=(j && t<end)?j-1u:65535u;
        if(u16(arena+i+2u*t)!=expected) return -7;
    }
    count=n; duration=d; spawn_end=end; po=p; io=i; crc=u32(arena+56);
    return 0;
}
int wave_schedule_init(uint32_t id) {
    const ExternalEntry *e; int rc;
    if(id>WAVE_HIGH) return -1;
    if(loaded_id==id) return 0;
    loaded_id=WAVE_SCHEDULE_NONE;
    memset(&s,0,sizeof(s));
    rc=external_assets_init(); if(rc) return rc;
    e=external_find(EXTERNAL_WAVE,id);
    if(!e || e->raw>sizeof(arena) || e->raw<64u || e->codec!=1u || e->predictor) return -2;
    rc=external_decode(EXTERNAL_WAVE,id,arena,(uint32_t)e->raw); if(rc) return rc;
    rc=validate((uint32_t)e->raw); if(rc) return rc;
    loaded_id=id;
    s.magic=WAVE_SCHEDULE_STATE_MAGIC; s.asset_id=id; s.asset_crc=crc;
    s.last_consumed=s.pending_slot=WAVE_SCHEDULE_NONE;
    s.stats.scheduled=count; s.stats.phase=WAVE_SCHEDULE_READY;
    return 0;
}
int wave_schedule_reset(void) {
    if(loaded_id==WAVE_SCHEDULE_NONE || s.pending_slot!=WAVE_SCHEDULE_NONE) return -1;
    memset(&s,0,sizeof(s));
    s.magic=WAVE_SCHEDULE_STATE_MAGIC;s.asset_id=loaded_id;s.asset_crc=crc;
    s.last_consumed=s.pending_slot=WAVE_SCHEDULE_NONE;
    s.stats.scheduled=count;s.stats.phase=WAVE_SCHEDULE_READY;
    return 0;
}
int wave_schedule_start(uint32_t now) {
    if(loaded_id==WAVE_SCHEDULE_NONE || s.stats.phase!=WAVE_SCHEDULE_READY) return -1;
    s.last_clock_ms=now; s.elapsed_ms=0; s.clock_seen=0;
    s.stats.phase=WAVE_SCHEDULE_SPAWNING; return 0;
}
static void close_pending(void) {
    if(s.pending_slot!=WAVE_SCHEDULE_NONE) { ++s.stats.failed; s.pending_slot=WAVE_SCHEDULE_NONE; }
}
int wave_schedule_poll(uint32_t now,uint32_t pressure,WaveSpawnEvent *out) {
    uint32_t delta,j,previous; const uint8_t *c,*e;
    if(!out || loaded_id==WAVE_SCHEDULE_NONE || s.stats.phase==WAVE_SCHEDULE_READY) return 0;
    if(s.clock_seen && now==s.last_clock_ms) return 0;
    delta=now-s.last_clock_ms;
    /* Unsigned clock wrap is intentional. Clamp only elapsed time, never actors. */
    s.elapsed_ms=delta>=duration-s.elapsed_ms?duration:s.elapsed_ms+delta;
    s.last_clock_ms=now; s.clock_seen=1;
    close_pending();
    c=pressure<=1023u?arena+po+4u*pressure:0;
    if(!c || !c[2]) s.stats.pressure_latched=1;
    else if(c[3]) s.stats.pressure_latched=0;
    if(s.elapsed_ms>=spawn_end) {
        previous=s.last_consumed==WAVE_SCHEDULE_NONE?0u:s.last_consumed+1u;
        s.stats.expired+=count-previous; s.last_consumed=count-1u;
        if(s.stats.phase!=WAVE_SCHEDULE_FINISHED)
            s.stats.phase=s.elapsed_ms>=duration?WAVE_SCHEDULE_FINISHED:WAVE_SCHEDULE_DRAIN;
        return 0;
    }
    j=u16(arena+io+2u*s.elapsed_ms);
    if(j==65535u || (s.last_consumed!=WAVE_SCHEDULE_NONE && j<=s.last_consumed)) return 0;
    previous=s.last_consumed==WAVE_SCHEDULE_NONE?0u:s.last_consumed+1u;
    s.stats.expired+=j-previous; s.last_consumed=j; ++s.stats.considered;
    if(!c || s.stats.pressure_latched || !c[2] ||
       (s.have_success && s.elapsed_ms-s.last_success_ms<u16(c))) {
        ++s.stats.suppressed; return 0;
    }
    e=event(j); out->slot_id=j; out->entry_id=u16(e+4); out->kind=e[6]; out->side=e[7]; out->variant=e[8];
    s.pending_slot=j; s.pending_ms=s.elapsed_ms; return 1;
}
int wave_schedule_commit(uint32_t slot,int success) {
    if(slot==WAVE_SCHEDULE_NONE || slot!=s.pending_slot || loaded_id==WAVE_SCHEDULE_NONE) return -1;
    s.pending_slot=WAVE_SCHEDULE_NONE;
    if(success) { ++s.stats.spawned; s.have_success=1; s.last_success_ms=s.pending_ms; }
    else ++s.stats.failed;
    return 0;
}
const WaveScheduleStats *wave_schedule_stats(void) { return &s.stats; }
void wave_schedule_observe_alive(uint32_t alive) {
    if(loaded_id!=WAVE_SCHEDULE_NONE && !alive && s.stats.phase==WAVE_SCHEDULE_DRAIN)
        s.stats.phase=WAVE_SCHEDULE_FINISHED;
}
size_t wave_schedule_state_size(void) { return sizeof(WaveScheduleState); }
int wave_schedule_save(WaveScheduleState *out) {
    if(!out || loaded_id==WAVE_SCHEDULE_NONE || s.pending_slot!=WAVE_SCHEDULE_NONE) return -1;
    *out=s; return 0;
}
static int state_valid(const WaveScheduleState *v) {
    uint32_t consumed;
    if(v->magic!=WAVE_SCHEDULE_STATE_MAGIC || v->asset_id!=loaded_id || v->asset_crc!=crc ||
       v->elapsed_ms>duration || v->pending_slot!=WAVE_SCHEDULE_NONE ||
       v->pending_ms>v->elapsed_ms || v->last_success_ms>v->elapsed_ms ||
       v->have_success>1u || v->clock_seen>1u || v->stats.scheduled!=count ||
       v->stats.phase>WAVE_SCHEDULE_FINISHED || v->stats.pressure_latched>1u ||
       v->stats.considered>count || v->stats.expired>count || v->stats.spawned>count ||
       v->stats.suppressed>count || v->stats.failed>count) return 0;
    consumed=v->last_consumed==WAVE_SCHEDULE_NONE?0u:v->last_consumed+1u;
    if(consumed>count || v->stats.considered+v->stats.expired!=consumed ||
       v->stats.spawned+v->stats.suppressed+v->stats.failed!=v->stats.considered ||
       v->have_success!=(uint32_t)(v->stats.spawned!=0u)) return 0;
    if(v->last_consumed!=WAVE_SCHEDULE_NONE && u32(event(v->last_consumed))>v->elapsed_ms) return 0;
    if(v->have_success && (v->last_success_ms>=spawn_end || v->last_success_ms<u32(event(0)))) return 0;
    if(v->stats.phase==WAVE_SCHEDULE_READY && (v->elapsed_ms || consumed || v->clock_seen)) return 0;
    if(v->stats.phase==WAVE_SCHEDULE_SPAWNING && v->elapsed_ms>=spawn_end) return 0;
    if(v->stats.phase>=WAVE_SCHEDULE_DRAIN && (v->elapsed_ms<spawn_end || consumed!=count)) return 0;
    return 1;
}
int wave_schedule_restore(const WaveScheduleState *v,uint32_t now) {
    if(!v || loaded_id==WAVE_SCHEDULE_NONE || !state_valid(v)) return -1;
    s=*v; s.last_clock_ms=now;
    /* This instant was already serviced before pause; no duplicate poll. */
    s.clock_seen=s.stats.phase==WAVE_SCHEDULE_READY?0u:1u; return 0;
}

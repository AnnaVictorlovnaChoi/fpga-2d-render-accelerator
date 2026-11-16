from g225_contract import MAP_VERSION, platform_contract
"""G2.19 final ELF32 range/resource/4KiB-stack gate; no third-party parser."""
from pathlib import Path
import hashlib,json,struct,sys

def probe_sizes(asm):
    import re
    rows=re.findall(r'^\s*\.(?:size|comm)\s+g225_size_(\w+)\s*,\s*(\d+)(?:\s*,\s*\d+)?\s*$',asm,re.M)
    result={name:int(size) for name,size in rows}
    if len(rows)!=len(result) or set(result)!={'system','pause','snapshot','hot','cold','visual','cursor_cache','hud','player','pause_control','world','metrics','metrics_snapshot'} or any(n<=0 for n in result.values()):
        raise ValueError('missing/duplicate/invalid current compiler sizeof probes')
    return result

def verify(path):
    data=path.read_bytes()
    def need(ok,msg):
        if not ok:raise ValueError(msg)
    need(data[:7]==b'\x7fELF\x01\x01\x01','expected little-endian ELF32')
    h=struct.unpack_from('<16sHHIIIIIHHHHHH',data)
    need(h[2]==243,'expected RISC-V')
    phoff,shoff,phsize,phnum,shsize,shnum,shstr=h[5],h[6],h[9],h[10],h[11],h[12],h[13]
    need(shsize==40 and phsize==32,'ELF header sizes')
    sections=[struct.unpack_from('<IIIIIIIIII',data,shoff+i*shsize) for i in range(shnum)]
    names=data[sections[shstr][4]:sections[shstr][4]+sections[shstr][5]]
    def cstr(buf,off):return buf[off:].split(b'\0',1)[0].decode('utf-8')
    alloc=[];symbols={};all_symbols=[];symbol_names=[]
    for s in sections:
        name=cstr(names,s[0])
        if s[2]&2 and s[5]:
            need(0x1000<=s[3] and s[3]+s[5]<=0x04f01000,'allocated section outside app79MiB: '+name)
            alloc.append(dict(name=name,address=s[3],bytes=s[5],nobits=s[1]==8))
        if s[1]==2:
            st=sections[s[6]];strings=data[st[4]:st[4]+st[5]]
            need(s[9]==16,'symbol entry size')
            for off in range(s[4],s[4]+s[5],16):
                n,value,size,info,other,index=struct.unpack_from('<IIIBBH',data,off)
                if n: symbol_names.append(cstr(strings,n))
                if n and index:
                    name=cstr(strings,n);symbols[name]=dict(address=value,bytes=size)
                    all_symbols.append(dict(symbol=name,address=value,bytes=size,section_index=index,binding=info>>4))
    for expected,value in [('__external_stage_start',0x04f01000),('__external_stage_end',0x05f01000),('__stack_size',4096)]:
        need(expected in symbols and symbols[expected]['address']==value,'fixed linker symbol: '+expected)
    stack=next((s for s in alloc if s['name']=='.stack'),None)
    need(stack is not None and stack['bytes']==4096,'stack section must be exactly4096')
    forbidden=[n for n in symbols if any(x in n.lower() for x in ('_blob_start','_blob_end','_binary_resources','_pack_start','_pack_end'))]
    need(not forbidden,'embedded resource symbols: '+str(forbidden))
    loads=[]
    for i in range(phnum):
        p=struct.unpack_from('<IIIIIIII',data,phoff+i*phsize)
        if p[0]==1 and p[5]:
            need(0x1000<=p[2] and p[2]+p[5]<=0x04f01000,'PT_LOAD outside app79MiB')
            need(p[4]<=2<<20,'PT_LOAD file exceeds2MiB')
            loads.append(dict(address=p[2],file_bytes=p[4],memory_bytes=p[5]))
    need(bool(loads),'no load segments')
    need(0x05f01000<=0x06000000,'stage/framebuffer overlap')
    # Preserve TU-local duplicate names (e.g. each consumer's arena) separately.
    measured_bss=sorted([s for s in all_symbols if s['bytes'] and s['section_index']<len(sections) and sections[s['section_index']][1]==8],key=lambda x:(-x['bytes'],x['address']))
    # G16 navigation graph lives inside static System s; vectors stay static.
    required_bss=('enemy_frame','snapshot','enemy_owned_plan','enemy_pause_plan','enemy_dirty','saved_enemy','s','queue','snapshot_epoch','snapshot_last','snapshot_last_version','snapshot_last_epoch','snapshot_cached','player_view','enemy_pending_tiles','wave_rects','wave_bounds','wave_sources','pipeline_stats','gpu_wait_total')
    bss_names={s['symbol'].split('.')[0] for s in measured_bss}
    need(all(n in bss_names for n in required_bss),'missing static BSS evidence: '+str([n for n in required_bss if n not in bss_names]))
    plan=[s for s in all_symbols if s['symbol']=='enemy_plan']
    need(len(plan)==1 and plan[0]['bytes']==4 and sections[plan[0]['section_index']][1]!=8,'enemy_plan must be initialized 4-byte pointer')
    need('render_copy' not in bss_names,'retired render_copy still linked')
    config=json.loads((path.parent/'build_config.json').read_text(encoding='utf-8-sig'))
    need(config.get('project')=='G2.25_V10Display' and config.get('probe_enabled') in (0,1),'fresh probe build configuration required')
    need((path.parent/'src/cpu_scheduler.c.o').is_file() and (path.parent/'src/render_templates.c.o').is_file(),'Explicit scheduler/template objects missing')
    timer=[s for s in all_symbols if s['symbol']=='cpu_scheduler_timer_isr']
    need(len(timer)==1 and timer[0]['binding']==1,'Timer hook must link strong scheduler implementation, not weak fallback')
    need(config.get('scheduler_timer_irq_enabled') is True,'Fresh one-shot timer configuration required')
    need(all(n in symbols for n in ('cpu_scheduler_arm_deadline','cpu_scheduler_cancel')),'One-shot scheduler code not linked')
    owned=[s for s in measured_bss if s['symbol']=='timer_owned']
    need(len(owned)==1 and owned[0]['bytes']==4,'Timer ownership must be exact 4-byte BSS')
    due=[s for s in all_symbols if s['symbol']=='cpu_scheduler_due']
    need(len(due)==1 and due[0]['bytes']==4 and sections[due[0]['section_index']][1]!=8,'RAM due flag must be initialized 4-byte data')
    if config['probe_enabled']:
        need(all(n in symbols for n in ('cpu_probe_start','cpu_probe_phase','cpu_probe_event','cpu_probe_continue','cpu_probe_take')),'cpu_probe TU not linked')
        need('accum' in bss_names,'probe accumulation must remain static BSS')
        probe_bss=[s for s in measured_bss if s['symbol'].split('.')[0] in ('accum','origin','mark','gmark','gorigin','reader_fee','active','started')]
        need(sum(s['bytes'] for s in probe_bss)<=16384,'unexpectedly large probe BSS')
    # Readonly generated metadata must not become mutable BSS.
    readonly=[s for s in all_symbols if s['symbol'].startswith(('render_template_ops','render_template_ranges'))]
    need(bool(readonly),'Compiled command template metadata missing')
    need(sum(s['bytes'] for s in readonly)<65536,'Command templates exceed small metadata budget')
    need(all(s['section_index']<len(sections) and sections[s['section_index']][1]!=8 for s in readonly),'Command templates must not be BSS')
    # The unchanged linker merges .rodata into writable .data; prove constness
    # from this run's actual input object, not the output section's merged flag.
    obj=(path.parent/'src/render_templates.c.o').read_bytes()
    oh=struct.unpack_from('<16sHHIIIIIHHHHHH',obj)
    os=[struct.unpack_from('<IIIIIIIIII',obj,oh[6]+i*oh[11]) for i in range(oh[12])]
    ns=os[oh[13]];strings=obj[ns[4]:ns[4]+ns[5]]
    for symbol in readonly:
        candidates=[sec for sec in os if cstr(strings,sec[0])=='.rodata.'+symbol['symbol']]
        need(len(candidates)==1 and candidates[0][1]!=8 and not(candidates[0][2]&1) and candidates[0][5]==symbol['bytes'],'Fresh object readonly template proof: '+symbol['symbol'])
        sec=candidates[0]; final=sections[symbol['section_index']]
        offset=final[4]+symbol['address']-final[3]
        need(obj[sec[4]:sec[4]+sec[5]]==data[offset:offset+symbol['bytes']],'Template object/final ELF bytes differ: '+symbol['symbol'])
    # Exact route storage is BSS, never embedded application payload.
    root=Path(__file__).resolve().parents[1]
    # Alpha keyed support is generated readonly metadata, not a copied pixel payload.
    support=json.loads((root/'resources/alpha_support/manifest.json').read_text(encoding='utf-8'))
    expected_support={'as_'+owner+'_'+part for owner in ('dragon','flow') for part in ('offsets','masks','runs')}
    alpha_tables=[s for s in all_symbols if s['symbol'] in expected_support]
    need({s['symbol'] for s in alpha_tables}==expected_support and len(alpha_tables)==6,'Alpha support tables missing/duplicate')
    need(sum(s['bytes'] for s in alpha_tables)==support['table_bytes']<200000,'Alpha support table size mismatch')
    ao=(path.parent/'src/alpha_support.c.o').read_bytes();ah=struct.unpack_from('<16sHHIIIIIHHHHHH',ao)
    asecs=[struct.unpack_from('<IIIIIIIIII',ao,ah[6]+i*ah[11]) for i in range(ah[12])]
    an=asecs[ah[13]];astr=ao[an[4]:an[4]+an[5]]
    for symbol in alpha_tables:
        secs=[s for s in asecs if cstr(astr,s[0])=='.rodata.'+symbol['symbol']]
        need(len(secs)==1 and secs[0][1]!=8 and not(secs[0][2]&1) and secs[0][5]==symbol['bytes'],'Alpha table is not readonly in fresh object')
        sec=secs[0];final=sections[symbol['section_index']];off=final[4]+symbol['address']-final[3]
        need(final[1]!=8 and ao[sec[4]:sec[4]+sec[5]]==data[off:off+symbol['bytes']],'Alpha table object/ELF mismatch')
    import re
    need(path.stem=='G2.25_V10Display','old artifact identity')
    app=path.with_suffix('.bin').read_bytes()
    banner=b'AD G2.25_V10Display clock=%u icache=%u dcache=%u profile=%s probe=%u formal_wfi=%u rtl_metadata=unknown_newRTL'
    from g225_contract import variant_contract, game_rules_contract
    variant_contract(config, config.get('runtime_log_enabled'))
    game_rules_contract(root)
    if config['runtime_log_enabled']:
        need(banner in app and b'G221_CPU300_I8K_D16K\x00' in app,'actual APP startup banner/profile mismatch')
    else:
        forbidden_print=[n for n in symbol_names if re.search(r'printf|vsnprintf|(^|_)puts$|uart_write|logf_line|log_drain', n)]
        need(not forbidden_print,'Silent ELF retains defined/undefined print path: '+str(forbidden_print))
        need(b'AD ' not in app,'Silent APP retains AD boot/runtime log text')
    metric=[s for s in all_symbols if s['symbol']=='render_metrics_alpha_accepted']
    need(len(metric)==1 and metric[0]['binding']==1,'Strong alpha metric implementation required in both variants')
    need(all(config.get(k)==v for k,v in platform_contract(root).items()),'fresh platform metadata mismatch')
    asm=(path.parent/'g225_sizes.s').read_text(encoding='utf-8-sig')
    sizes=probe_sizes(asm)
    need(set(sizes)=={'system','pause','snapshot','hot','cold','visual','cursor_cache','hud','player','pause_control','world','metrics','metrics_snapshot'},'missing current compiler sizeof probes')
    need(sizes['hot']>0 and sizes['cold']>0 and sizes['visual']>0,'invalid fresh Hot/Cold/VisualCache sizeof evidence')
    need(sizes['player']==116,'fresh Player ABI must be116')
    need(sizes['hud']>=32,'fresh HudState ABI too small')
    need(sizes['system']<=320000,'System exceeds exact 320000-byte pause capacity')
    def exact(name,size):
        found=[s for s in measured_bss if s['symbol'].split('.')[0]==name and s['bytes']==size]
        need(len(found)==1,'exact BSS sizeof mismatch: '+name)
    exact('mob_route',sizes['cursor_cache']);exact('s',sizes['system']);exact('saved',sizes['pause']);exact('saved_enemy',320000)
    owner_sizes={}
    for owner in ('main','renderer','render_metrics','scene','hud'):
        owner_asm=(path.parent/f'g225_{owner}_sizes.s').read_text(encoding='utf-8-sig')
        rows=re.findall(r'^\s*\.(?:size|comm)\s+g225_size_(\w+)\s*,\s*(\d+)(?:\s*,\s*\d+)?\s*$',owner_asm,re.M)
        need(len(rows)==len(dict(rows)) and bool(rows),'missing/duplicate owner probes')
        owner_sizes.update({n:int(v) for n,v in rows})
    need(set(owner_sizes)=={'pipeline','submitted_enemy','submitted_world','submitted_valid','hud_plan','pause_world_metadata','pause_metadata_valid','collected_alpha_bits','collected_alpha_count','collecting_alpha','pause_alpha','flow_alpha','flow_world_alpha','pause_player_alpha','flow_player_alpha','alpha_bits','frozen_alpha','pending_player_alpha','completed_player_alpha','built_alpha_mask','display_index','display_epoch'},'owner storage probes incomplete')
    need(owner_sizes['alpha_bits']==512 and owner_sizes['collected_alpha_bits']==512,'Alpha object token bitsets must cover4096 tokens')
    need(owner_sizes['submitted_enemy']==3*sizes['snapshot'] and owner_sizes['submitted_world']==3*sizes['world'],'per-FB cache array ABI mismatch')
    need(owner_sizes['pause_world_metadata']==sizes['world'] and owner_sizes['pause_metadata_valid']==4,'pause copied metadata ABI mismatch')
    for name,size in owner_sizes.items():exact(name,size)
    exact('player',sizes['player']);exact('pause',sizes['pause_control']);exact('snapshot',sizes['world']);exact('latest_safe',sizes['world'])
    exact('pending',sizes['metrics']);exact('completed',sizes['metrics'])
    exact('prepared',16);exact('pending',160);exact('restore',160)
    for name in ('enemy_frame','enemy_owned_plan','enemy_pause_plan'):exact(name,sizes['snapshot'])
    for module in ('main','game_rules','hud','hud_data','render_metrics'):
        need((path.parent/('src/'+module+'.c.o')).is_file(),'Explicit current module object missing: '+module)
    hud_header=(root/'resources/hud/layout.h').read_text(encoding='utf-8-sig')
    hud_bytes=int(re.search(r'#define HUD_PAYLOAD_BYTES\s+(\d+)',hud_header).group(1))
    hud_streams=json.loads((root/'resources/hud/external_streams.json').read_text(encoding='utf-8-sig'))['streams']
    need(len(hud_streams)==1 and hud_streams[0]['raw_bytes']==hud_bytes,'HUD arena differs from current stream')
    exact('arena',hud_bytes)
    wave_header=(root/'src/wave_schedule.h').read_text(encoding='utf-8')
    wave_bytes=int(re.search(r'#define WAVE_SCHEDULE_ARENA_BYTES\s+(\d+)',wave_header).group(1))
    wave_streams=json.loads((root/'resources/waves/external_streams.json').read_text(encoding='utf-8-sig'))['streams']
    need(wave_bytes==max(s['raw_bytes'] for s in wave_streams),'wave arena differs from current maximum stream');exact('arena',wave_bytes)
    size_header=(root/'resources/precomputed/routes_sizes.h').read_text()
    api_header=(root/'src/precomputed_data.h').read_text()
    raw_bytes=int(re.search(r'#define PC_RAW_BYTES\s+(\d+)u?',size_header).group(1))
    route_count=int(re.search(r'#define PC_ROUTE_COUNT\s+(\d+)u?',api_header).group(1))
    route_arena=[s for s in measured_bss if s['symbol'].split('.')[0]=='arena' and s['bytes']==raw_bytes]
    route_data=[s for s in measured_bss if s['symbol'].split('.')[0]=='routes' and s['bytes']==route_count*32]
    need(len(route_arena)==1 and len(route_data)==1,'missing exact route arena/descriptor BSS')
    owner_pool=[s for s in measured_bss if s['symbol'].split('.')[0]=='enemy_rows']
    retired=('enemy_rows','enemy_surface','owner_cursor','owner_events','owner_active','owner_position','enemy_bins','enemy_parent','enemy_complex')
    need(not any(n in bss_names for n in retired),'retired renderer scratch still linked')
    tiles=[s for s in measured_bss if s['symbol'].split('.')[0]=='enemy_pending_tiles']
    need(len(tiles)==1 and tiles[0]['bytes']==160,'G16 tile hazard set must be static 160 bytes')
    metadata=[s for s in measured_bss if s['symbol'].split('.')[0] in ('player_view','enemy_origins','enemy_cached','enemy_frames')]
    need(all(s['bytes']<=65536 for s in metadata),'view cache metadata unexpectedly large')
    enemy_bss=[s for s in measured_bss if s['symbol'].split('.')[0] in required_bss or 'arena' in s['symbol'] or 'enemy' in s['symbol']]
    return dict(runtime_log_enabled=config['runtime_log_enabled'],map_version=MAP_VERSION,runtime_sizeof=sizes,owner_storage_sizeof=owner_sizes,wave_arena_bytes=wave_bytes,data_total_bytes=sum(a['bytes'] for a in alloc if a['name'] in ('.data','.sdata')),route_arena_bss=route_arena,route_descriptor_bss=route_data,route_raw_bytes=raw_bytes,route_descriptor_bytes=route_count*32,owner_pool_static_bytes=sum(s['bytes'] for s in owner_pool),tile_hazard_static_bytes=tiles[0]['bytes'],view_cache_metadata_bss=metadata,enemy_snapshot_arena_bss=enemy_bss,required_static_bss=list(required_bss),bss_symbols=measured_bss,bss_total_bytes=sum(a['bytes'] for a in alloc if a['nobits'] and a['name']!='.stack'),framebuffer_start=0x06000000,stage_framebuffer_nonoverlap=True,project='G2.25_V10Display',status='PASS',elf_sha256=hashlib.sha256(data).hexdigest(),alloc_sections=alloc,load_segments=loads,stack_bytes=4096,stage_start=0x04f01000,stage_end=0x05f01000,forbidden_resource_symbols=forbidden,notes=['Source .incbin prohibition is enforced before compilation.','ELF BSS/scratch (including dead snapshot/grey buffers) measured, never estimated.'])

if __name__=='__main__':
    p=Path(sys.argv[1]);report=verify(p);(p.parent/'elf_verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))

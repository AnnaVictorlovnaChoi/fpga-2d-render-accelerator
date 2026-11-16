from g225_contract import MAP_VERSION
"""RV32 compiler stack evidence. Call chains are deliberately conservative, not WCET."""
import json,re,sys,hashlib
from pathlib import Path
stage=Path(sys.argv[1]); root=Path(__file__).resolve().parents[1]
frames=[]
for p in stage.rglob('*.su'):
 if re.fullmatch(r'g225_(?:(?:main|renderer|render_metrics|scene|hud)_)?sizes\.su',p.relative_to(stage).as_posix()):continue # ABI probe is not linked into APP
 for line in p.read_text().splitlines():
  fields=line.split('\t')
  if len(fields)>=3:
   frames.append(dict(function=fields[0].rsplit(':',1)[-1],bytes=int(fields[1]),kind=fields[2],source=fields[0],su=str(p.relative_to(stage))))
def frame(name):
 matches=[f['bytes'] for f in frames if f['function']==name or f['function'].startswith(name+'.')]
 return max(matches,default=0)
chains={
 'startup_assets':['main','runtime_init','external_decode','puff','codes','decode','bits'],
 'startup_arrows':['main','projectile_init','external_decode','puff','codes','decode','bits'],
 'startup_scene':['main','scene_init','scene_assets_init','external_decode','puff','codes','decode','bits'],
 'scene_rebuild':['main','scene_rebuild','compose','layer','blit','bitblt','frame_pipeline_poll'],
 'scene_alpha':['main','renderer_draw','sprite','mask_alpha','scene_mask_span'],
 'scene_alpha_enqueue':['main','renderer_draw','sprite','pic_alphasprite','frame_pipeline_poll'],
 'startup_construct':['main','runtime_init','external_decode','puff','construct'],
 'draw_player':['main','renderer_draw','sprite','pic_sprite','frame_pipeline_poll'],
 'draw_arrow':['main','renderer_draw_next','sprite','pic_sprite','frame_pipeline_poll'],
 'restore':['main','renderer_restore','copy_rect','pic_blt','frame_pipeline_poll'],
 'report_source_only':['main','report','logf_line','bsp_printf'],
}
chains.update({
 'flow_freeze_copy':['main','renderer_flow_freeze','dragon_gpu_copy','bitblt','frame_pipeline_poll'],
 'flow_freeze_draw':['main','renderer_flow_freeze','dragon_gpu_draw','bitblt','frame_pipeline_poll'],
 'flow_freeze_mask':['main','renderer_flow_freeze','mask_alpha','scene_mask_span'],
 'flow_freeze_runs':['main','renderer_flow_freeze','runs_add','runs_flush','bitblt','frame_pipeline_poll'],
 'flow_ending_sprite':['main','renderer_flow_sprite','renderer_flow_draw_surface','bitblt','frame_pipeline_poll'],
 'flow_scene_altar':['main','scene_rebuild','compose','renderer_flow_draw_surface','bitblt','frame_pipeline_poll'],
 'flow_death_gray':['main','renderer_flow_death_restore','flow_gray_surface','bitblt','frame_pipeline_poll'],
 'flow_ending_gray':['main','renderer_flow_gray','flow_gray_surface','bitblt','frame_pipeline_poll'],
 'flow_ending_restore':['main','renderer_flow_restore','dragon_gpu_copy','bitblt','frame_pipeline_poll'],
 'startup_flow':['main','flow_assets_init','external_decode','puff','codes','decode','bits'],
 'startup_flow_construct':['main','flow_assets_init','external_decode','puff','construct'],
 'startup_dragon':['main','dragon_assets_init','external_decode','puff','codes','decode','bits'],
 'startup_dragon_construct':['main','dragon_assets_init','external_decode','puff','construct'],
 'startup_scene_construct':['main','scene_init','scene_assets_init','external_decode','puff','construct'],
 'startup_flash_read_tx':['main','external_assets_init','flash_storage_read','read4','tx','deadline','now_ticks'],
 'startup_flash_read_end':['main','external_assets_init','flash_storage_read','read4','command_end','drain','deadline','now_ticks'],
 'startup_flash_probe':['main','external_assets_init','flash_storage_init','read_jedec','tx','deadline','now_ticks'],
 'startup_flash_header':['main','external_assets_init','header','external_crc32'],
 'startup_flash_bind_header':['main','external_assets_init','external_assets_bind','header','external_crc32'],
 'startup_flash_bind_crc':['main','external_assets_init','external_assets_bind','external_crc32'],
 'startup_decode_cached_init':['main','scene_init','scene_assets_init','external_decode','external_assets_init'],
 'startup_decode_raw_crc':['main','scene_init','scene_assets_init','external_decode','external_crc32'],
})
chains.update({
 'pause_metadata_capture':['main','pause_save','scene_snapshot_save'],
 'pause_capture_checksum':['main','pause_save','checksum'],
 'pause_restore_checksum':['main','pause_restore','checksum'],
 'pause_metadata_restore':['main','pause_restore','scene_snapshot_restore'],
 'pause_gpu_capture':['main','renderer_pause_capture','dragon_gpu_copy','bitblt','frame_pipeline_poll'],
 'pause_gpu_capture_fence':['main','renderer_pause_capture','dragon_gpu_fence','frame_pipeline_poll'],
 'pause_gpu_restore':['main','renderer_pause_restore','dragon_gpu_copy','bitblt','frame_pipeline_poll'],
 'pause_overlay_restore':['main','renderer_pause_show','renderer_pause_restore','dragon_gpu_copy','bitblt','frame_pipeline_poll'],
 'pause_overlay_sprite':['main','renderer_pause_show','renderer_flow_sprite','renderer_flow_draw_surface','bitblt','frame_pipeline_poll'],
 'pause_end_restore':['main','renderer_pause_end_freeze','renderer_pause_restore','dragon_gpu_copy','bitblt','frame_pipeline_poll'],
 'pause_end_freeze':['main','renderer_pause_end_freeze','renderer_flow_freeze','dragon_gpu_copy','bitblt','frame_pipeline_poll'],
 'pause_end_draw':['main','renderer_pause_end_freeze','renderer_flow_freeze','dragon_gpu_draw','bitblt','frame_pipeline_poll'],
 'pause_end_mask':['main','renderer_pause_end_freeze','renderer_flow_freeze','mask_alpha','scene_mask_span'],
 'pause_end_runs':['main','renderer_pause_end_freeze','renderer_flow_freeze','runs_add','runs_flush','bitblt','frame_pipeline_poll'],
})
# G2.12 source-observed paths; frame bytes always come from this run's .su.
chains.update({
 'startup_countdown':['main','countdown_assets_init','external_decode','puff','codes','decode','bits'],
 'startup_countdown_construct':['main','countdown_assets_init','external_decode','puff','construct'],
 'countdown_bind':['main','countdown_assets_init','countdown_assets_bind','external_crc32'],
 'wave_overlay_draw':['main','renderer_countdown_finish','scene_countdown_overlay','renderer_countdown_draw_surface','bitblt','frame_pipeline_poll'],
 'wave_overlay_framegetter':['main','renderer_countdown_finish','scene_countdown_overlay','wave_rect','countdown_frame','countdown_frame_count'],
 'wave_overlay_bounds':['main','renderer_countdown_finish','scene_countdown_bounds','wave_rect','countdown_frame','countdown_frame_count'],
 'wave_freeze_overlay':['main','renderer_flow_freeze','scene_countdown_overlay','renderer_countdown_draw_surface','bitblt','frame_pipeline_poll'],
 'wave_freeze_framegetter':['main','renderer_flow_freeze','scene_countdown_overlay','wave_rect','countdown_frame','countdown_frame_count'],
 'wave_pause_end_overlay':['main','renderer_pause_end_freeze','renderer_flow_freeze','scene_countdown_overlay','renderer_countdown_draw_surface','bitblt','frame_pipeline_poll'],
 'wave_component_player':['main','renderer_draw','sprite','component_player','dragon_gpu_draw','bitblt','frame_pipeline_poll'],
 'wave_component_storm':['main','renderer_occlude','component_storm','dragon_gpu_draw','bitblt','frame_pipeline_poll'],
 'wave_scene_damage':['main','scene_rebuild','wave_damage','wave_rect','countdown_frame','countdown_frame_count'],
 'wave_altar_hint_helper':['main','scene_rebuild','compose','altar_hint_center_q8','flow_frame','flow_frame_count'],
 'wave_tick':['main','newwave_update'],
 'wave_safe_begin':['main','newwave_begin_safe'],
})
# Enemy paths observed in G2.13 source; every non-emitted frame needs GCC inline evidence.
chains.update({
 'startup_enemy':['main','enemy_assets_init','external_decode','puff','codes','decode','bits'],
 'startup_enemy_construct':['main','enemy_assets_init','external_decode','puff','construct'],
 'startup_enemy_bind':['main','enemy_assets_init','enemy_assets_bind','external_crc32'],
 'startup_enemy_cachewarm_emit':['main','enemy_gpu_warm','emit','bitblt','frame_pipeline_poll'],
 'startup_enemy_cachewarm_fence':['main','enemy_gpu_warm','dragon_gpu_fence','frame_pipeline_poll'],
 'startup_enemy_cachewarm_metadata':['main','enemy_gpu_warm','enemy_cache_pixels'],
 'startup_enemy_cachewarm_resident':['main','enemy_gpu_resident'],
 'enemy_cachewarm_emit':['main','renderer_enemy_draw','enemy_component','enemy_gpu_warm','emit','bitblt','frame_pipeline_poll'],
 'enemy_cachewarm_fence':['main','renderer_enemy_draw','enemy_component','enemy_gpu_warm','dragon_gpu_fence','frame_pipeline_poll'],
 'enemy_bind':['main','enemy_assets_init','enemy_assets_bind','external_crc32'],
 'enemy_nav':['main','enemy_update','navigation','field','edge','segment_clear','scene_enemy_hard'],
 'enemy_collision':['main','enemy_update','arrows_hit','segment_impact'],
 'enemy_collision_buckets':['main','enemy_update','bucket_build'],
 'enemy_move':['main','enemy_update','move_toward','segment_clear','scene_enemy_hard'],
 'enemy_snapshot':['main','enemy_snapshot'],
 'enemy_cachewarm':['main','renderer_enemy_draw','enemy_component','enemy_gpu_warm','bitblt','frame_pipeline_poll'],
 'enemy_mix':['main','renderer_enemy_draw','enemy_component','enemy_gpu_draw','bitblt','frame_pipeline_poll'],
 'enemy_owner':['main','renderer_enemy_draw','enemy_component','enemy_owner_span'],
 'enemy_dirtytile':['main','renderer_enemy_draw','enemy_component','enemy_touch'],
 'enemy_mask':['main','renderer_enemy_draw','enemy_component','mask_alpha','scene_mask_span'],
 'enemy_runs':['main','renderer_enemy_draw','enemy_component','runs_add','runs_flush','bitblt','frame_pipeline_poll'],
 'enemy_plan':['main','renderer_enemy_snapshot','enemy_gpu_bounds','enemy_gpu_pose','enemy_cache_frame'],
 'pause_enemy_save':['main','pause_save','enemy_save'],
 'pause_enemy_hash':['main','pause_save','enemy_hash','enemy_save_size'],
 'pause_enemy_restore':['main','pause_restore','enemy_restore'],
 'pause_enemy_restore_hash':['main','pause_restore','enemy_hash','enemy_save_size'],
})
# G2.14 actual fixed-step and event-sweep paths (not the retired row insertion path).
chains.pop('enemy_owner', None)
for key in ('enemy_nav', 'enemy_collision', 'enemy_collision_buckets', 'enemy_move'):
 names=chains[key];names.insert(names.index('enemy_update')+1,'enemy_step')
chains['enemy_nav']=['main','enemy_update','enemy_step','navigation','edge_geometry','segment_clear','scene_enemy_hard']
chains.update({
 'perf_nav_cached_field':['main','enemy_update','enemy_step','navigation','field'],
 'perf_source_draw':['main','renderer_enemy_draw','enemy_component','enemy_batch_draw','enemy_gpu_draw','bitblt','frame_pipeline_poll'],
 'perf_source_fence':['main','renderer_enemy_draw','enemy_component','enemy_batch_draw','enemy_fence','dragon_gpu_fence','frame_pipeline_poll'],
 'perf_classify':['main','renderer_enemy_draw','enemy_component','enemy_root'],
 'perf_owner_event':['main','renderer_enemy_draw','enemy_component','owner_build','owner_event_down','owner_less'],
 'perf_owner_active':['main','renderer_enemy_draw','enemy_component','owner_build','owner_activate','owner_swap'],
 'perf_owner_next':['main','renderer_enemy_draw','enemy_component','owner_build','owner_next'],
 'perf_timing_clock':['main','renderer_enemy_draw','enemy_component','enemy_fence','dragon_gpu_clock'],
 'perf_restore_union':['main','renderer_restore','copy_rect','pic_blt','frame_pipeline_poll'],
 'perf_countdown_partial':['main','renderer_countdown_finish','scene_countdown_overlay','renderer_countdown_draw_surface','bitblt','frame_pipeline_poll'],
 'perf_sample_render':['main','sample_enemy_render','renderer_enemy_stats'],
})
# G2.15 cached metadata replaces repeated bounds/frame resolution, not pixels.
for key in ('wave_overlay_draw','wave_freeze_overlay','wave_pause_end_overlay','perf_countdown_partial'):
 chains[key]=['renderer_countdown_draw_resolved' if n=='renderer_countdown_draw_surface' else n for n in chains[key]]
for key in ('wave_overlay_framegetter','wave_freeze_framegetter','wave_scene_damage'):
 chains[key]=[n for n in chains[key] if n not in ('countdown_frame','countdown_frame_count')]
chains['wave_overlay_bounds']=['main','renderer_countdown_finish','scene_countdown_bounds']
chains['enemy_plan']=['main','renderer_enemy_snapshot']
chains.update({
 'lean_countdown_geometry':['main','scene_init','wave_geometry_init','wave_rect_compute','countdown_frame','countdown_frame_count'],
 'lean_player_view':['main','renderer_draw','player_support_view'],
 'lean_player_rows':['main','renderer_draw','sprite','component_player','player_support_view_row'],
 'lean_classify_rows':['main','renderer_enemy_draw','enemy_component','player_interval','player_support_view_row'],
 'lean_enemy_resolve':['main','renderer_enemy_draw','enemy_component','enemy_resolve','enemy_gpu_pose','enemy_cache_frame'],
 'lean_enemy_support':['main','renderer_enemy_draw','enemy_component','enemy_resolved_support','enemy_cache_support'],
 'lean_nav_entry':['main','enemy_update','enemy_step','nav_move','entry_project','segment_clear','scene_enemy_hard'],
 'lean_nav_recover':['main','enemy_update','enemy_step','nav_move','recover_node','segment_clear','scene_enemy_hard'],
 'lean_pipeline_stats':['main','frame_pipeline_get_stats','pipeline_lock'],
 'lean_pipeline_consumed':['main','frame_pipeline_consumed_seq'],
 'lean_pipeline_submit':['main','submit_tracked','frame_pipeline_submit','clint_getTime'],
 'lean_submit_clock':['main','submit_tracked','millis','ticks','clint_getTime'],
 'lean_consume_watchdog_clock':['main','consume_deadline','millis','ticks','clint_getTime'],
 'lean_consume_watchdog_fail_log':['main','consume_deadline','fail','logf_line','bsp_printf'],
 'lean_consume_watchdog_fail_poll':['main','consume_deadline','fail','frame_pipeline_poll','pipeline_lock'],
 'lean_failure_log':['main','fail','logf_line','bsp_printf'],
 'lean_pipeline_poll':['main','frame_pipeline_poll','pipeline_lock'],
 'interrupt_frame':['trap','isrRoutine','frame_switch_isr','clint_getTime','frame_pipeline_tick_isr'],
})
# external_assets_bind is host-only; RV32 startup calls validate_bind directly.
chains.pop('startup_flash_bind_header',None)
chains.pop('startup_flash_bind_crc',None)
chains['startup_flash_validate_header']=['main','external_assets_init','validate_bind','header','external_crc32']
chains['startup_flash_validate_crc']=['main','external_assets_init','validate_bind','external_crc32']
# Source-retired recover_node is replaced by explicit graph/route paths below;
# this is not a missing-frame zero-byte allowance.
chains.pop('lean_nav_recover',None)
# G16 replaces owner/pair graph and per-pixel enemy mixing with bbox/tile paths.
for key in ('enemy_mix','enemy_mask','enemy_runs','perf_source_draw','perf_source_fence','perf_classify','perf_owner_event','perf_owner_active','perf_owner_next','perf_timing_clock','lean_classify_rows','lean_enemy_resolve','lean_enemy_support'):
 chains.pop(key,None)
chains.update({
 'budget_enemy_draw':['main','renderer_enemy_draw','renderer_enemy_draw_impl','enemy_component','enemy_gpu_draw_resolved','emit','bitblt','frame_pipeline_poll'],
 'budget_enemy_hazard':['main','renderer_enemy_draw','enemy_component','enemy_hazard'],
 'budget_enemy_fence':['main','renderer_enemy_draw','enemy_component','enemy_gpu_fence','dragon_gpu_fence','gpu_wait_record'],
 'budget_nav_graph_build':['main','enemy_update','enemy_step','navigation','graph_build','route_clear','segment_clear','scene_enemy_hard'],
 'budget_nav_graph_field':['main','enemy_update','enemy_step','navigation','graph_build','graph_field','route_clear','segment_clear','scene_enemy_hard'],
 'budget_nav_route':['main','enemy_update','enemy_step','nav_move','route_clear','segment_clear','scene_enemy_hard'],
 'budget_nav_route_root':['main','enemy_update','enemy_step','nav_move','move_toward','root'],
 'budget_nav_graph_root':['main','enemy_update','enemy_step','navigation','graph_build','graph_field','nav_length','root'],
 'budget_gpu_wait_clock':['main','dragon_gpu_fence','clint_getTime'],
 'budget_gpu_wait_total':['main','budget_loop','gpu_wait_ticks_total'],
 'budget_loop_clock':['main','budget_loop','budget_tick','ticks','clint_getTime'],
 'budget_frame':['main','budget_frame','budget_us'],
 'budget_report':['main','budget_report','logf_line','bsp_printf'],
 'budget_idle':['main','budget_idle'],
 'budget_frame_loop_clock':['main','budget_frame','budget_loop','budget_tick','ticks','clint_getTime'],
 'budget_frame_loop_wait':['main','budget_frame','budget_loop','gpu_wait_ticks_total'],
 'budget_idle_clock':['main','budget_idle','budget_tick','ticks','clint_getTime'],
 'budget_idle_wait':['main','budget_idle','gpu_wait_ticks_total'],
 'budget_scene_retry':['main','scene_rebuild','compose','layer','blit','gpu_wait_fifo_ready','gpu_wait_record'],
 'budget_enemy_fifo':['main','renderer_enemy_draw','enemy_component','enemy_gpu_draw_resolved','emit','gpu_wait_fifo_ready','gpu_wait_record'],
 'budget_dragon_fifo':['main','dragon_gpu_draw','enqueue','gpu_wait_fifo_ready','gpu_wait_record'],
 'budget_bitblt_wait':['main','bitblt_wait','gpu_wait_record'],
})
# G217 external route startup; frame sizes must come from this run, never G216 proof.
chains.update({
 'startup_routes_decode':['main','precomputed_data_init','external_decode','puff','codes','decode','bits'],
 'startup_routes_construct':['main','precomputed_data_init','external_decode','puff','construct'],
 'startup_routes_raw_crc':['main','precomputed_data_init','external_decode','external_crc32'],
 'startup_routes_bind_crc':['main','precomputed_data_init','precomputed_data_bind','crc'],
 'startup_routes_bind_u32':['main','precomputed_data_init','precomputed_data_bind','u32','u16'],
})
# G217 foreground UART ring drain is independent of BOOT/fatal blocking printf.
chains.update({
 'uart_nonblocking_drain':['main','log_drain'],
 'uart_enqueue_source':['main','logf_line'],
 'uart_budget_enqueue':['main','budget_report','logf_line'],
 'g217_renderer_wait_counter':['main','renderer_enemy_draw','render_wait_total','gpu_wait_ticks_total'],
 'g217_renderer_clock':['main','renderer_enemy_draw','dragon_gpu_clock'],
})
# G219 paths read from current source; only this stage's fresh SU/inline/cgraph counts.
for key in ('restore','perf_restore_union'):
 chains[key]=['main','renderer_restore','copy_rect','gpu_enqueue_copy','pipeline_lock']
chains.update({
 'g219_copy_prepare':['main','renderer_restore','copy_rect','gpu_prepare_copy','frame_base'],
 'g219_copy_packet':['main','renderer_restore','copy_rect','gpu_enqueue_copy','gpu_send_word'],
 'g219_copy_wait':['main','renderer_restore','copy_rect','gpu_wait_fifo_ready','gpu_wait_record'],
 'startup_waves_decode':['main','wave_schedule_init','external_decode','puff','codes','decode','bits'],
 'startup_waves_construct':['main','wave_schedule_init','external_decode','puff','construct'],
 'startup_waves_validate':['main','wave_schedule_init','validate','external_crc32'],
 'g219_wave_pause_save':['main','pause_save','wave_schedule_save'],
 'g219_wave_pause_restore':['main','pause_restore','wave_schedule_restore'],
 'g219_enemy_resolve':['main','renderer_enemy_draw','enemy_component','enemy_gpu_resolve','enemy_gpu_pose','prepare_metadata','enemy_frame'],
 'startup_enemy_metadata':['main','enemy_gpu_warm','prepare_metadata','enemy_plane'],
 'g219_enemy_snapshot_sort':['main','enemy_snapshot'],
 'g219_idle_gate':['main','visible_gate'],
 'g219_budget_span':['main','budget_span','budget_tick','ticks','clint_getTime'],
 'g219_budget_background':['main','budget_background','budget_loop','budget_tick','ticks','clint_getTime'],
 'g219_wave_poll':['main','wave_schedule_poll','close_pending'],
 'g219_wave_commit':['main','wave_schedule_commit'],
 'g219_enemy_scheduled':['main','enemy_spawn_scheduled'],
 'g219_prepare_graph':['main','enemy_prepare_navigation','navigation','graph_build','route_clear','segment_clear','scene_enemy_hard'],
 'g219_prepare_graph_field':['main','enemy_prepare_navigation','navigation','graph_build','graph_field','route_clear','segment_clear','scene_enemy_hard'],
 'g219_prepare_field':['main','enemy_prepare_navigation','navigation','field'],
 'g219_prepare_player_field':['main','enemy_prepare_navigation','navigation','graph_field','nav_length','root'],
 'g219_prepare_edge':['main','enemy_prepare_navigation','navigation','edge_geometry','segment_clear','scene_enemy_hard'],
 'g219_prepare_hard':['main','enemy_prepare_navigation','navigation','scene_enemy_hard'],
 'g219_prepare_entry':['main','enemy_prepare_navigation','navigation','entry_build','entry_project','segment_clear','scene_enemy_hard'],
 'g219_enemy_bucket_sync':['main','enemy_update','enemy_step','bucket_sync'],
 'g219_enemy_bucket_detach':['main','enemy_update','enemy_step','bucket_detach'],
})
# G219 probe paths: include nested continue/phase and report/take, not only leaf readers.
config=json.loads((stage/'build_config.json').read_text(encoding='utf-8-sig'))
if config.get('project')!='G2.25_V10Display' or config.get('probe_enabled') not in (0,1):
 raise SystemExit('Missing/invalid fresh G225 build configuration')
probe_enabled=config['probe_enabled']==1
if not (stage/'src/cpu_scheduler.c.o').is_file():raise SystemExit('Explicit scheduler object missing')
chains['interrupt_timer']=['trap','cpu_scheduler_timer_isr']
if config.get('scheduler_timer_irq_enabled') is not True:raise SystemExit('Current timer-IRQ stage configuration required')
chains.update({
 'g221_deadline_compare':['main','scheduler_idle','cpu_scheduler_arm_deadline','clint_setCmp'],
 'g221_deadline_clock':['main','scheduler_idle','cpu_scheduler_arm_deadline','clint_getTime'],
 'g221_deadline_earliest':['main','scheduler_idle','cpu_scheduler_earliest'],
 'g221_deadline_cancel':['main','cpu_scheduler_cancel'],
 'g221_deadline_due':['main','cpu_scheduler_is_due'],
})
if config.get('scheduler_wfi_enabled')==1:
 chains['scheduler_sleep']=['main','cpu_scheduler_sleep_until']
 chains['scheduler_compare']=['main','cpu_scheduler_sleep_until','clint_setCmp']
 chains['scheduler_clock']=['main','cpu_scheduler_sleep_until','clint_getTime']
if not (stage/'src/cpu_probe.c.o').is_file():raise SystemExit('Explicit cpu_probe object missing')
if probe_enabled:
 for label,prefix in {
  'start':['main','cpu_probe_start'],
  'phase':['main','cpu_probe_phase'],
  'continue':['main','cpu_probe_continue','cpu_probe_phase'],
  'take':['main','probe_report','cpu_probe_take'],
 }.items():
  if label!='start':
   chains['probe_'+label+'_close_clock']=prefix+['close_phase','stamp','cpu_probe_clock','ticks','clint_getTime']
   chains['probe_'+label+'_close_gpu']=prefix+['close_phase','cpu_probe_gpu','gpu_wait_ticks_total']
  if label!='take':
   chains['probe_'+label+'_open_clock']=prefix+['open_phase','stamp','cpu_probe_clock','ticks','clint_getTime']
   chains['probe_'+label+'_open_gpu']=prefix+['open_phase','cpu_probe_gpu','gpu_wait_ticks_total']
 chains['probe_take_stamp']=['main','probe_report','cpu_probe_take','stamp','cpu_probe_clock','ticks','clint_getTime']
 chains['probe_event']=['main','cpu_probe_event']
 chains['probe_relabel']=['main','cpu_probe_relabel']
 chains['probe_start_clock']=['main','cpu_probe_start','cpu_probe_clock','ticks','clint_getTime']
 chains['probe_take_gpu']=['main','probe_report','cpu_probe_take','cpu_probe_gpu','gpu_wait_ticks_total']
 chains['probe_report_log']=['main','probe_report','logf_line']
 chains['probe_report_units']=['main','probe_report','budget_us']
# G221 source-observed visual and template paths; no fabricated zero frames.
chains.update({
 'g221_player_visual':['main','renderer_draw','runtime_get_visual','runtime_visual'],
 'g221_player_frame':['main','renderer_draw','runtime_get_visual','runtime_get_frame'],
 'g221_player_support':['main','renderer_draw','player_support_visual'],
 'g221_template_startup':['main','enemy_gpu_warm','prepare_metadata','render_template_validate','render_template_enemy'],
 'g221_template_resolve':['main','renderer_enemy_draw','enemy_component','enemy_gpu_resolve','prepare_metadata','render_template_validate','render_template_enemy'],
 'g221_template_draw':['main','renderer_enemy_draw','enemy_component','enemy_gpu_draw_resolved','render_template_enemy'],
 'g221_enemy_visual':['main','enemy_update','enemy_step','visual_sync'],
 'g221_spawn_visual':['main','enemy_spawn_scheduled','visual_sync'],
 'g221_animation':['main','enemy_update','enemy_step','animation_tick','visual_sync'],
 'g221_replan_schedule':['main','enemy_update','enemy_step','replan_schedule','replan_needed'],
 'g221_replan_nav':['main','enemy_update','enemy_step','nav_move','replan_needed'],
 'g221_birth_unlink':['main','enemy_update','enemy_step','remove_mob','bucket_sync'],
 'g221_birth_snapshot':['main','enemy_snapshot'],
 'g221_birth_restore':['main','pause_restore','enemy_restore'],
})
# G221 source-observed queue/cursor/expiry and always-on load diagnostics.
chains.pop('g221_replan_schedule', None) # scheduler no longer calls replan_needed
chains.update({
 'g221_queue_schedule':['main','enemy_update','enemy_step','replan_schedule'],
 'g221_queue_enqueue':['main','enemy_update','enemy_step','replan_enqueue'],
 'g221_queue_cancel':['main','enemy_update','enemy_step','remove_mob','replan_cancel'],
 'g221_expiry_prepare':['main','enemy_update','enemy_step','expiry_prepare','expiry_time'],
 'g221_expiry_arm':['main','enemy_update','enemy_step','kill','expiry_arm','expiry_time'],
 'g221_expiry_cancel':['main','enemy_update','enemy_step','remove_mob','expiry_cancel'],
 'g221_cursor_spawn':['main','enemy_spawn_scheduled','pc_route_cursor_init'],
 'g221_cursor_restore':['main','pause_restore','enemy_restore','pc_route_cursor_init'],
 'g221_route_move':['main','enemy_update','enemy_step','route_move'],
 'g221_enemy_init':['main','enemy_init','bucket_reset'],
 'g221_load_report':['main','load_report','logf_line','bsp_printf'],
 'g221_load_sample':['main','load_sample','enemy_stats'],
 'g221_timer_count':['main','budget_report','cpu_scheduler_timer_irqs'],
})
# Actual G221 movement wrapper is not necessarily inlined (fresh SU: 80B).
for names in chains.values():
 if 'move_toward' in names:
  names.insert(names.index('move_toward')+1,'move_toward_proven')
chains.update({
 'g221_arrow_prepare':['main','enemy_update','enemy_step','arrows_hit','segment_prepare'],
 'g221_arrow_impact':['main','enemy_update','enemy_step','arrows_hit','prepared_impact'],
 'g221_arrow_resolve':['main','enemy_resolve_projectiles','arrows_hit','prepared_impact'],
 'g221_bullet_prepare':['main','enemy_update','enemy_step','segment_box','segment_impact','segment_prepare'],
 'g221_bullet_impact':['main','enemy_update','enemy_step','segment_box','segment_impact','prepared_impact'],
 'g221_proven_move':['main','enemy_update','enemy_step','nav_move','move_toward_proven','scene_enemy_hard'],
 'g221_expiry_due':['main','enemy_update','enemy_step','expiry_is_due'],
 'g221_queue_granted':['main','enemy_update','enemy_step','nav_move','replan_granted'],
 'g221_pit_sweep':['main','enemy_update','enemy_step','pit_sweep','scene_enemy_pit'],
 'g221_pit_active':['main','enemy_update','enemy_step','scene_enemy_pit_active'],
 'g221_restore_expiry':['main','pause_restore','enemy_restore','expiry_arm','expiry_time'],
 'g221_restore_queue':['main','pause_restore','enemy_restore','replan_enqueue'],
})
# Timing wrappers retain their implementation frame when not inlined.
renderer_source=(root/'src/renderer.c').read_text(encoding='utf-8')
wrapped=set(re.findall(r'\b(renderer_\w+)_impl\s*\(',renderer_source))
for key,names in chains.items():
 expanded=[]
 for i,name in enumerate(names):
  expanded.append(name)
  if name in wrapped and (i+1==len(names) or names[i+1]!=name+'_impl'):
   expanded.append(name+'_impl')
 chains[key]=expanded
chains.update({
 'startup_hud':['main','hud_assets_init','external_decode','puff','codes','decode','bits'],
 'startup_hud_bind':['main','hud_assets_init','hud_assets_bind'],
 'hud_draw':['main','renderer_countdown_finish','renderer_countdown_finish_impl','hud_draw','draw_impl','hud_tile','dragon_gpu_copy','bitblt','frame_pipeline_poll'],
 'hud_victory':['main','renderer_countdown_finish','renderer_countdown_finish_impl','hud_draw','draw_impl','hud_victory','hud_tile','dragon_gpu_copy','bitblt','frame_pipeline_poll'],
 'hud_bar':['main','renderer_countdown_finish','renderer_countdown_finish_impl','hud_draw','draw_impl','hud_bar_rect','dragon_gpu_origin'],
 'hud_terminal_top':['main','renderer_flow_freeze','hud_draw_terminal_top','draw_impl','hud_tile','dragon_gpu_copy','bitblt','frame_pipeline_poll'],
 'hud_terminal_fence':['main','renderer_flow_freeze','hud_draw_terminal_top','draw_impl','dragon_gpu_fence','frame_pipeline_poll'],
 'hud_terminal_capture':['main','renderer_flow_freeze','hud_capture'],
 'hud_terminal_restore':['main','renderer_pause_end_freeze','renderer_flow_freeze','hud_draw_terminal_top','draw_impl','hud_tile','dragon_gpu_copy','bitblt','frame_pipeline_poll'],
})
# Current private freeze helper replaces the old pause -> live-FB freeze path.
for key,names in chains.items():
 if 'renderer_pause_end_freeze' in names and 'renderer_flow_freeze' in names:
  names.remove('renderer_flow_freeze')
 for caller in ('renderer_pause_end_freeze','renderer_flow_freeze'):
  if caller in names:
   names.insert(names.index(caller)+1,'flow_freeze_source')
   names.insert(names.index(caller)+2,'flow_freeze_source_private')
chains.update({
 'g225_restart_renderer':['main','renderer_reset_for_new_game','hud_reset_state'],
 'g225_restart_hud_capture':['main','renderer_reset_for_new_game','hud_capture'],
 'g225_restart_scene':['main','scene_reset_runtime'],
 'g225_startup_scene_reset':['main','scene_init','scene_reset_runtime'],
 'g225_restart_schedule':['main','wave_schedule_reset'],
 'g225_gray_suspend':['main','renderer_flow_death_restore','render_metrics_suspend'],
 'g225_gray_resume':['main','renderer_flow_death_restore','render_metrics_resume'],
 'g225_pause_frozen':['main','renderer_pause_show','renderer_pause_restore','render_metrics_frozen'],
 'g225_pause_frozen_player':['main','renderer_pause_show','renderer_pause_restore','render_metrics_frozen_player'],
 'g225_flow_frozen':['main','renderer_flow_restore','render_metrics_frozen'],
 'g225_pause_completed':['main','renderer_pause_capture','render_metrics_completed'],
 'g225_pause_player':['main','renderer_pause_capture','render_metrics_completed_has_player'],
 'g225_hud_object':['main','renderer_countdown_finish','renderer_countdown_finish_impl','hud_draw','draw_impl','render_metrics_object'],
})
# Stats may run in normal, paused and terminal frames: preserve each real caller.
stats_callers={
 'normal':['renderer_countdown_finish','renderer_countdown_finish_impl','hud_draw','draw_impl'],
 'pause':['renderer_pause_show','renderer_pause_restore'],
 'restore':['renderer_pause_restore'],
 'flow':['renderer_flow_restore'],
 'gray':['renderer_flow_gray'],
 'death':['renderer_flow_death_restore'],
 'victory':['renderer_victory'],
}
for label,prefix in stats_callers.items():
 base=['main']+prefix+['hud_draw_stats']
 chains['g225_stats_'+label+'_completed']=base+['render_metrics_completed']
 chains['g225_stats_'+label+'_number']=base+['number','hud_tile','dragon_gpu_copy','bitblt','frame_pipeline_poll']
 chains['g225_stats_'+label+'_fence']=base+['dragon_gpu_fence','gpu_wait_record']
inline_text='\n'.join(p.read_text(errors='replace') for p in stage.rglob('*.inline.txt'))
cgraph_text='\n'.join(p.read_text(errors='replace') for p in stage.rglob('*.cgraph.txt'))
def has_frame(name):
 return any(f['function']==name or f['function'].startswith(name+'.') for f in frames)
def inline_evidence(name):
 # GCC -fopt-info-inline-optimized: Inlining NAME/N into CALLER/N.
 pattern=re.escape(name)+r'(?:[.$][\w.$]+)?/\d+'
 return bool(re.search(r'\bInlin(?:ing|ed)\s+'+pattern+r'\b',inline_text,re.I) or re.search(r'\bFunction '+pattern+r' is inline copy in ',cgraph_text))
# Default compact logging preprocesses report out; this is a different call path,
# not a zero-byte claim. Require source switch AND no emitted GCC graph symbol.
main_source=(root/'src/main.c').read_text(encoding='utf-8')
compact_log=bool(re.search(r'^#define AD_PERF_VERBOSE 0\s*$',main_source,re.M)) and not re.search(r'\breport(?:[.$][\w.$]+)?/\d+',cgraph_text)
if compact_log:
 chains['report_source_only']=['main','logf_line','bsp_printf']
# Silent preprocessing removes only diagnostic paths, not gameplay/metrics.
if config.get('runtime_log_enabled') == 0:
 for key in list(chains):
  if any(n in ('logf_line','log_drain','bsp_printf','load_report','budget_report','report') for n in chains[key]):
   del chains[key]
# Alpha acceptance is a sibling of FIFO/poll, but retains every caller frame.
for key,names in list(chains.items()):
 if 'bitblt' in names:
  chains[key+'_accepted_alpha']=names[:names.index('bitblt')+1]+['render_metrics_alpha_accepted']
chains['g225_alpha_accepted']=['main','bitblt','render_metrics_alpha_accepted']
chains.update({
 'g225_metrics_begin':['main','render_metrics_begin'],
 'g225_metrics_commit':['main','submit_tracked','render_metrics_commit'],
 'g225_metrics_reset':['main','render_metrics_reset_session'],
 'g225_rates_poll':['main','run_rates_poll'],
 'g225_rates_submit':['main','submit_tracked','run_rates_submit'],
 'g225_rates_reset':['main','run_rates_reset'],
 'g225_restart_input':['main','run_restart_input'],
})
chains.update({
 'g225_capture_ready':['main','render_metrics_capture','render_metrics_capture_pending'],
 'g225_commit_snapshot':['main','submit_tracked','render_metrics_commit_snapshot'],
 'g225_commit_capture':['main','submit_tracked','render_metrics_commit','render_metrics_capture_pending'],
 'g225_commit_nested':['main','submit_tracked','render_metrics_commit','render_metrics_commit_snapshot'],
 'g225_record_submitted':['main','submit_tracked','renderer_record_submitted'],
 'g225_enemy_view':['main','renderer_enemy_snapshot_view'],
 'g225_flow_safe_metrics':['main','renderer_flow_freeze','render_metrics_completed'],
 'g225_flow_private_suspend':['main','renderer_flow_freeze','flow_freeze_source','render_metrics_suspend'],
 'g225_flow_private_resume':['main','renderer_flow_freeze','flow_freeze_source','render_metrics_resume'],
})
# Pause capture wrapper now excludes private cache preparation; include its real frame.
for names in chains.values():
 if 'renderer_pause_capture' in names:
  names.insert(names.index('renderer_pause_capture')+1,'renderer_pause_capture_private')
chains.update({
 'g225_pause_restore_record':['main','renderer_pause_restore','renderer_record_submitted'],
 'g225_pause_show_record':['main','renderer_pause_show','renderer_pause_restore','renderer_record_submitted'],
 'g225_pause_capture_suspend':['main','renderer_pause_capture','render_metrics_suspend'],
 'g225_pause_capture_resume':['main','renderer_pause_capture','render_metrics_resume'],
})
# G225 source-observed siblings retain the deepest existing caller prefix.
# No temporary alpha_tile_inside path: exact keyed support now lives in alpha_support.c.
alpha_edges={
 'scene_rebuild':[['scene_alpha_mask','renderer_dragon_alpha'],['scene_alpha_mask','renderer_flow_alpha'],['scene_alpha_mask','countdown_alpha','countdown_plane'],['scene_alpha_mask','countdown_frame']],
 'renderer_draw_impl':[['scene_metrics_visible','render_metrics_alpha_object']],
 'enemy_gpu_support':[['enemy_gpu_pose'],['enemy_cache_support'],['enemy_support']],
 'renderer_dragon_alpha':[['alpha_support_dragon','intersects'],['alpha_support_dragon','plane_index'],['alpha_support_dragon','dragon_frame'],['alpha_support_dragon','dragon_plane'],['dragon_gpu_origin'],['dragon_plane']],
 'renderer_flow_alpha':[['alpha_support_flow','intersects'],['alpha_support_flow','plane_index'],['alpha_support_flow','flow_frame'],['alpha_support_flow','flow_plane'],['dragon_gpu_origin'],['flow_frame'],['flow_plane']],
 'component_player':[['alpha_object','render_metrics_alpha_object']],
 'component_storm':[['alpha_object','render_metrics_alpha_object'],['renderer_dragon_alpha']],
 'enemy_component':[['enemy_alpha_hit','enemy_gpu_support'],['alpha_object','render_metrics_alpha_object']],
 'renderer_flow_sprite':[['renderer_flow_alpha'],['alpha_object','render_metrics_alpha_object']],
 'flow_freeze_source_private':[['renderer_dragon_alpha'],['alpha_object','render_metrics_alpha_object']],
 'flow_freeze_source':[['scene_alpha_objects']],
 'renderer_pause_capture_private':[['render_metrics_completed_player_alpha']],
 'renderer_flow_freeze':[['render_metrics_completed_player_alpha']],
 'renderer_pause_restore':[['render_metrics_frozen_alpha'],['render_metrics_frozen_player_alpha']],
 'renderer_flow_restore':[['render_metrics_frozen_alpha'],['render_metrics_frozen_player_alpha']],
 'renderer_flow_death_restore':[['render_metrics_frozen_alpha'],['render_metrics_frozen_player_alpha']],
 'hud_draw_stats':[['hud_display_objects'],['hud_display_fps']],
}
for caller,tails in alpha_edges.items():
 prefixes={tuple(names[:names.index(caller)+1]) for names in chains.values() if caller in names}
 for index,prefix in enumerate(sorted(prefixes)):
  for branch,tail in enumerate(tails):chains[f'g225_support_{caller}_{index}_{branch}']=list(prefix)+tail
# Expand support callees after their callers have been introduced above.
for caller in ('renderer_dragon_alpha','renderer_flow_alpha','enemy_gpu_support'):
 prefixes={tuple(names[:names.index(caller)+1]) for names in chains.values() if caller in names}
 for index,prefix in enumerate(sorted(prefixes)):
  for branch,tail in enumerate(alpha_edges[caller]):chains[f'g225_lookup_{caller}_{index}_{branch}']=list(prefix)+tail
missing=sorted({n for names in chains.values() for n in names if not has_frame(n) and not inline_evidence(n)})
# O2 folds fixed/dynamic/stored into puff, and report into main: missing frames
# are not silently taken as proof; explicit notes below explain these cases.
result={'runtime_log_enabled':config.get('runtime_log_enabled'),'project':'G2.25_V10Display','map_version':MAP_VERSION,'stack_bytes':4096,'main_exact_bytes':frame('main'),'top_frames':sorted(frames,key=lambda x:x['bytes'],reverse=True)[:25],
 'chains':{name:{'functions':names,'compiler_frame_sum':sum(frame(n) for n in names),'unresolved_or_inlined':[n for n in names if not any(f['function']==n or f['function'].startswith(n+'.') for f in frames)]} for name,names in chains.items()},
 'notes':['GCC O2 inline/cgraph evidence is required; default compact log preprocesses legacy report out and main calls logf_line directly.',
 'codes/decode/bits are conservatively summed even where inlining/tail calls may reduce depth.',
 'Prebuilt libc vsnprintf/_svfprintf_r and libgcc have no .su: report_source_only is NOT a full-stack proof.',
 'Reserve 512 bytes for RV32 trap save (64 bytes) plus trap/ISR C calls and 512 bytes additional startup margin.',
 'external_decode calls external_assets_init then returns from it before puff: init and puff frames are NOT summed together.',
 'Main explicitly initializes Flash before consumers; later external_assets_init calls are cached fast paths.',
 'Header/CRC, Flash transfer, puff, inverse predictor and BOOT printf phases are sequential, not one nested chain.',
 'Missing .su entries are listed as unresolved/inlined, not proof of zero frame; O2 clone frames use maximum matching clone.',
 'Startup puff and BOOT printf execute sequentially, not nested; no ISR UART logging was added.',
 'No board stack-watermark measurement; verify physical board startup and sustained rendering.']}
startup=max(c['compiler_frame_sum'] for n,c in result['chains'].items() if n.startswith('startup_'))
result['worst_known_chain']=max(result['chains'],key=lambda n:result['chains'][n]['compiler_frame_sum'])
result['worst_known_bytes']=max(c['compiler_frame_sum'] for c in result['chains'].values())
# Include known rendering chains in the same 4KiB gate, not startup alone.
startup=max(startup,result['worst_known_bytes'])
result['startup_conservative_plus_1024_reserve']=startup+1024
result['missing_frame_evidence']=missing
result['inline_evidence_functions']=sorted({n for names in chains.values() for n in names if not has_frame(n) and inline_evidence(n)})
result['frame_evidence_complete']=not missing
result['metadata_struct_return_evidence']={n:{'caller_max_frame_bytes':frame(n),'su':[f for f in frames if f['function']==n or f['function'].startswith(n+'.')],'inline_proven':inline_evidence(n)} for n in ('main','submit_tracked','render_metrics_capture','render_metrics_capture_pending','render_metrics_commit','render_metrics_commit_snapshot','renderer_record_submitted','renderer_flow_freeze','flow_freeze_source','flow_freeze_source_private')}
result['notes'].append('Metadata structure-return ABI spill is included in actual caller maximum .su frames; owner size probes are excluded, not counted as linked application evidence. Per-FB safe snapshots are BSS and flow freeze uses submitted metadata; GPU driver behavior unchanged.')
result['known_chain_headroom_bytes']=4096-startup-1024
trap_asm=(root/'platform/common/trap.S').read_text(encoding='utf-8')
if not re.search(r'addi\s+sp,\s*sp,\s*-64',trap_asm) or not re.search(r'addi\s+sp,\s*sp,\s*64',trap_asm):raise SystemExit('RV32 trap context changed: review exact save/restore before acceptance')
result['interrupt_context_by_path']={name:64+sum(frame(n) for n in chains[name]) for name in ('interrupt_frame','interrupt_timer')}
result['interrupt_context_bytes']=max(result['interrupt_context_by_path'].values())
result['interrupt_512_reserve_pass']=result['interrupt_context_bytes']<=512
result['startup_4k_pass']=startup+1024<=4096 and not missing and result['interrupt_512_reserve_pass']
result['notes'].append('Unresolved frames without compiler inline evidence FAIL the gate; sums are only lower bounds until resolved. Original 4KiB unchanged; historical startup puff headroom was only 160 bytes, not a board measurement.')
(stage/'stack_report.json').write_text(json.dumps(result,indent=2))
text=['RV32 -O2 -fstack-usage report',f'main EXACT frame: {frame("main")} bytes','Top compiler frames:']
text += [f"{f['bytes']:5} {f['function']} ({f['su']})" for f in result['top_frames']]
text += [f"{n}: {' -> '.join(c['functions'])} = {c['compiler_frame_sum']} bytes (source frames)" for n,c in result['chains'].items()]
text += [f'Startup + 1024-byte trap/additional reserve = {startup+1024}/4096',*result['notes']]
(stage/'stack_report.txt').write_text('\n'.join(text)+'\n');print('\n'.join(text))
if '--report-only' in sys.argv:
 if not result['startup_4k_pass']:raise SystemExit('Stack gate failed: missing compiler frame evidence or known chain plus reserve exceeds 4K')
 raise SystemExit(0)
from source_inputs import inventory
inputs=inventory(root)
before=stage/'source_hashes.before.json'
if before.is_file():
 original=json.loads(before.read_text(encoding='utf-8-sig'))
 if original!=inputs:
  changed=sorted(k for k in set(original)|set(inputs) if original.get(k)!=inputs.get(k))
  raise SystemExit('Inputs changed while building: '+', '.join(changed))
(stage/'source_hashes.json').write_text(json.dumps(inputs,indent=2))
if not result['startup_4k_pass']:raise SystemExit('Stack gate failed: missing compiler frame evidence or known chain plus reserve exceeds 4K')

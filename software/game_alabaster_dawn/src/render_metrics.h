#ifndef AD_RENDER_METRICS_H
#define AD_RENDER_METRICS_H
#include <stdint.h>
typedef struct {uint32_t visible_objects,alpha_commands,revision,alpha_objects;} RenderMetrics;
/* Main: begin only for a real candidate frame; commit exactly once after GPU
 * completion AND successful submit. Failed submit may retry without begin.
 * Private freeze/source preparation must occur outside this transaction. */
void render_metrics_begin(void);
typedef struct {
 RenderMetrics metrics;
 uint32_t frame_epoch, frozen_objects;
 unsigned has_player, valid;
 uint32_t frozen_alpha;
 unsigned has_player_alpha;
} RenderMetricsSnapshot;
/* Capture after GPU completion, before another begin. Value-owned and immutable
 * until submit: an older submit must never close the newer working transaction. */
void render_metrics_capture(RenderMetricsSnapshot *out);
RenderMetricsSnapshot render_metrics_capture_pending(void);
void render_metrics_commit_snapshot(const RenderMetricsSnapshot *snapshot);
void render_metrics_commit(void);
void render_metrics_abort(void);
/* Exclude private preparation without losing the candidate or token set. */
unsigned render_metrics_suspend(void);
void render_metrics_resume(unsigned previous_active);
void render_metrics_reset_session(void);
/* Single central platform emit hook: accepted (rc==0) mixing operations only.
 * No driver retries/FIFO rejects, no clock reads, no GPU packet changes. */
void render_metrics_alpha_accepted(void);
/* Stable token [0,4096) per logical object, independent of tiles/planes.
 * Caller records only after a nonempty visible draw succeeds. */
void render_metrics_object(unsigned token);
/* Frozen image objects: seed once, before new overlay tokens. */
void render_metrics_frozen(uint32_t objects);
RenderMetrics render_metrics_completed(void);
unsigned render_metrics_completed_has_player(void);
void render_metrics_frozen_player(unsigned visible);
/* Game sprite objects with actual Q1..31 support, not command count or HUD. */
void render_metrics_alpha_object(unsigned token);
void render_metrics_frozen_alpha(uint32_t objects);
unsigned render_metrics_completed_player_alpha(void);
void render_metrics_frozen_player_alpha(unsigned visible);
#endif

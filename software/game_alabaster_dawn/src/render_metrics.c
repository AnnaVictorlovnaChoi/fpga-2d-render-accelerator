#include "render_metrics.h"
#include <string.h>
static RenderMetrics pending,completed;
static uint32_t tokens[128],alpha_bits[128],epoch,pending_epoch,committed_epoch,frozen_objects,frozen_alpha;
static unsigned active,pending_player,completed_player,pending_player_alpha,completed_player_alpha;
unsigned render_metrics_completed_has_player(void){return completed_player;}
unsigned render_metrics_completed_player_alpha(void){return completed_player_alpha;}
void render_metrics_frozen_player(unsigned visible){if(active)pending_player=!!visible;}
void render_metrics_frozen_player_alpha(unsigned visible){if(active)pending_player_alpha=!!visible;}
void render_metrics_begin(void){memset(tokens,0,sizeof tokens);memset(alpha_bits,0,sizeof alpha_bits);pending=(RenderMetrics){0};pending_player=pending_player_alpha=0;frozen_objects=frozen_alpha=0;pending_epoch=++epoch;active=1;}
void render_metrics_abort(void){active=0;}
unsigned render_metrics_suspend(void){unsigned previous=active;active=0;return previous;}
void render_metrics_resume(unsigned previous){active=previous;}
void render_metrics_reset_session(void){active=0;pending_player=completed_player=pending_player_alpha=completed_player_alpha=0;pending=(RenderMetrics){0};completed=(RenderMetrics){0};frozen_objects=frozen_alpha=0;committed_epoch=epoch;memset(tokens,0,sizeof tokens);memset(alpha_bits,0,sizeof alpha_bits);}
RenderMetricsSnapshot render_metrics_capture_pending(void){RenderMetricsSnapshot s={pending,pending_epoch,frozen_objects,pending_player,active,frozen_alpha,pending_player_alpha};active=0;return s;}
void render_metrics_capture(RenderMetricsSnapshot*out){if(out)*out=render_metrics_capture_pending();}
void render_metrics_commit_snapshot(const RenderMetricsSnapshot*s){if(s&&s->valid&&(int32_t)(s->frame_epoch-committed_epoch)>0){uint32_t revision=completed.revision+1u;completed=s->metrics;completed.revision=revision;completed_player=s->has_player;completed_player_alpha=s->has_player_alpha;committed_epoch=s->frame_epoch;}}
void render_metrics_commit(void){RenderMetricsSnapshot s=render_metrics_capture_pending();render_metrics_commit_snapshot(&s);}
void render_metrics_alpha_accepted(void){if(active)++pending.alpha_commands;}
void render_metrics_object(unsigned token){if(active&&token<4096){if(!token)pending_player=1;uint32_t m=1u<<(token&31);if(!(tokens[token>>5]&m)){tokens[token>>5]|=m;++pending.visible_objects;}}}
void render_metrics_alpha_object(unsigned token){if(active&&token<4096){uint32_t m=1u<<(token&31);if(!token)pending_player_alpha=1;if(!(alpha_bits[token>>5]&m)){alpha_bits[token>>5]|=m;++pending.alpha_objects;}}}
void render_metrics_frozen(uint32_t n){if(active){pending.visible_objects=n;frozen_objects=n;}}
void render_metrics_frozen_alpha(uint32_t n){if(active){pending.alpha_objects=n;frozen_alpha=n;}}
RenderMetrics render_metrics_completed(void){return completed;}

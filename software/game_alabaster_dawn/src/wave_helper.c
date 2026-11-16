#include "wave_helper.h"
#include "flow_data.h"
int altar_hint_center_q8(void){
 const FlowFrame*a=flow_frame(FLOW_ALTAR,0),*h=flow_frame(FLOW_START_HINT,0);
 if(!a||!h)return altar_screen_y()*256;
 return altar_screen_y()*256-a->anchor_y_q8-AD_ALTAR_HINT_GAP*256-(int)h->h*256+h->anchor_y_q8;
}

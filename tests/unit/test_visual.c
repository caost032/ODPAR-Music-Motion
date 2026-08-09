#include "test_harness.h"

#include "odm_renderer.h"
#include "odm_visual.h"
#include "visual_internal.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void v_node(odm_score_node *n,uint32_t id,uint32_t scene,uint32_t role,
                   uint32_t cap,uint32_t state,uint32_t slot,int64_t start,int64_t end,
                   int32_t z){
    memset(n,0,sizeof(*n));n->node_id=id;n->scene_id=scene;n->role=role;
    n->capability_id=cap;n->capability_major=1u;n->capability_minor=0u;
    n->state_model=state;n->state_slot=slot;n->z_index=z;n->start_sample=start;n->end_sample=end;
    n->scale_x_micro=ODM_MICRO_ONE;n->scale_y_micro=ODM_MICRO_ONE;n->opacity_micro=ODM_MICRO_ONE;
    n->seed_domain=UINT64_C(0x100000001b3)*(uint64_t)id;
}

static uint8_t *build_visual_ir(uint64_t *out_bytes){
    odm_score_header h={0};odm_score_act act={0};odm_score_scene scenes[3];odm_score_node nodes[12];
    odm_score_cue cues[45];odm_score_state_transfer tr[2];odm_score_view v;odm_music_map_binding music;
    uint8_t *ir=NULL;uint64_t required=0u;uint32_t i,k=0u;odm_status st;
    memset(scenes,0,sizeof(scenes));memset(nodes,0,sizeof(nodes));memset(cues,0,sizeof(cues));memset(tr,0,sizeof(tr));memset(&v,0,sizeof(v));memset(&music,0,sizeof(music));
    h.schema_version_major=ODM_SCORE_SCHEMA_MAJOR;h.schema_version_minor=ODM_SCORE_SCHEMA_MINOR;h.fps=30;h.width=64u;h.height=64u;h.project_end_sample=48000;h.project_seed=UINT64_C(0x4f44504152473731);h.act_count=1u;h.scene_count=3u;h.node_count=12u;h.cue_count=45u;h.transfer_count=2u;
    act.act_id=1u;act.start_sample=0;act.end_sample=48000;
    scenes[0].scene_id=1u;scenes[0].act_id=1u;scenes[0].environment_node_id=1u;scenes[0].primary_node_id=2u;scenes[0].start_sample=0;scenes[0].end_sample=16000;
    scenes[1].scene_id=2u;scenes[1].act_id=1u;scenes[1].environment_node_id=4u;scenes[1].primary_node_id=5u;scenes[1].start_sample=16000;scenes[1].end_sample=32000;
    scenes[2].scene_id=3u;scenes[2].act_id=1u;scenes[2].environment_node_id=7u;scenes[2].primary_node_id=8u;scenes[2].start_sample=32000;scenes[2].end_sample=48000;
    v_node(&nodes[0],1u,1u,ODM_SCORE_NODE_ENVIRONMENT,ODM_CAP_BLACK_VOID,ODM_STATELESS,0u,0,16000,-100);
    v_node(&nodes[1],2u,1u,ODM_SCORE_NODE_PRIMARY,ODM_CAP_PRIMARY_EMPTY,ODM_STATELESS,0u,0,16000,0);
    v_node(&nodes[2],3u,1u,ODM_SCORE_NODE_EFFECT,ODM_CAP_RESIDUAL_EVENT_FIELD,ODM_EVENT_HISTORY,7u,0,16000,10);
    v_node(&nodes[3],4u,2u,ODM_SCORE_NODE_ENVIRONMENT,ODM_CAP_BLACK_VOID,ODM_STATELESS,0u,16000,32000,-100);
    v_node(&nodes[4],5u,2u,ODM_SCORE_NODE_PRIMARY,ODM_CAP_PRIMARY_EMPTY,ODM_STATELESS,0u,16000,32000,0);
    v_node(&nodes[5],6u,2u,ODM_SCORE_NODE_EFFECT,ODM_CAP_RESIDUAL_EVENT_FIELD,ODM_EVENT_HISTORY,7u,16000,32000,10);
    v_node(&nodes[6],7u,3u,ODM_SCORE_NODE_ENVIRONMENT,ODM_CAP_BLACK_VOID,ODM_STATELESS,0u,32000,48000,-100);
    v_node(&nodes[7],8u,3u,ODM_SCORE_NODE_PRIMARY,ODM_CAP_PRIMARY_EMPTY,ODM_STATELESS,0u,32000,48000,0);
    v_node(&nodes[8],9u,3u,ODM_SCORE_NODE_EFFECT,ODM_CAP_GRID_PROOF,ODM_STATELESS,0u,32000,48000,10);
    v_node(&nodes[9],10u,3u,ODM_SCORE_NODE_EFFECT,ODM_CAP_HALO,ODM_STATELESS,0u,32000,48000,20);
    v_node(&nodes[10],11u,3u,ODM_SCORE_NODE_EFFECT,ODM_CAP_RESIDUAL_EVENT_FIELD,ODM_EVENT_HISTORY,7u,32000,48000,30);
    v_node(&nodes[11],12u,3u,ODM_SCORE_NODE_PARTICLE,ODM_CAP_DETERMINISTIC_PARTICLES,ODM_EVENT_HISTORY,7u,32000,48000,40);
    nodes[10].entry_kind=ODM_ENTRY_FADE_SCALE;nodes[10].entry_duration_samples=4800u;nodes[10].exit_kind=ODM_EXIT_FADE_SCALE;nodes[10].exit_duration_samples=4800u;
    for(i=0u;i<20u;++i){cues[k].cue_id=k+1u;cues[k].scene_id=1u;cues[k].kind=ODM_SCORE_CUE_GENERIC;cues[k].sample=1000+(int64_t)i*700;++k;}
    for(i=0u;i<20u;++i){cues[k].cue_id=k+1u;cues[k].scene_id=2u;cues[k].kind=ODM_SCORE_CUE_BEAT_AUTHORED;cues[k].sample=17000+(int64_t)i*700;++k;}
    for(i=0u;i<5u;++i){cues[k].cue_id=k+1u;cues[k].scene_id=3u;cues[k].kind=ODM_SCORE_CUE_SECTION_AUTHORED;cues[k].sample=33000+(int64_t)i*1500;++k;}
    tr[0].transfer_id=1u;tr[0].from_scene_id=1u;tr[0].to_scene_id=2u;tr[0].state_slot=7u;tr[0].mode=ODM_TRANSFER_COPY;tr[0].at_sample=16000;
    tr[1].transfer_id=2u;tr[1].from_scene_id=2u;tr[1].to_scene_id=3u;tr[1].state_slot=7u;tr[1].mode=ODM_TRANSFER_RESET;tr[1].at_sample=32000;
    v.header=&h;v.acts=&act;v.scenes=scenes;v.nodes=nodes;v.cues=cues;v.transfers=tr;
    if(odm_sha256("visual-map",10u,&music.music_map_sha256)!=ODM_STATUS_OK)return NULL;
    if(odm_music_policy_current_sha256(&music.music_policy_sha256)!=ODM_STATUS_OK)return NULL;
    music.canonical_frames=48000u;
    st=odm_score_compile_render_ir(&v,&music,NULL,0u,&required,NULL);if(st!=ODM_STATUS_BUFFER_TOO_SMALL)return NULL;
    ir=(uint8_t*)malloc((size_t)required);if(!ir)return NULL;st=odm_score_compile_render_ir(&v,&music,ir,required,&required,NULL);if(st!=ODM_STATUS_OK){free(ir);return NULL;}*out_bytes=required;return ir;
}

static const odm_frame_node_state *find_state(const odm_frame_node_state *n,uint64_t count,uint32_t id){uint64_t i;for(i=0u;i<count;++i)if(n[i].node_id==id)return &n[i];return NULL;}

void odm_test_visual(odm_test_context *context){
    uint64_t ir_bytes=0u;uint8_t*ir=build_visual_ir(&ir_bytes);odm_visual_history a,b;odm_frame_node_state states[8];odm_frame_transfer_event transfers[2];odm_frame_state fs;uint64_t nr=0u,tr=0u;const odm_frame_node_state*sn;odm_q1_31 one_third=0;odm_render_asset_set assets={0};uint64_t fb=0u,sb=0u;uint8_t *frame1=NULL,*frame2=NULL,*scratch=NULL;odm_reference_frame_info i1,i2;uint64_t rf=0u,rs=0u;uint64_t p;
    ODM_TEST_CHECK(context,ir!=NULL);if(!ir)return;
    memset(&a,0,sizeof(a));memset(&b,0,sizeof(b));
    ODM_TEST_CHECK(context,odm_visual_history_resolve(ir,ir_bytes,6u,31000,&a)==ODM_STATUS_OK);
    ODM_TEST_CHECK(context,a.event_count==32u&&a.events[0].cue_id==9u&&a.events[31].cue_id==40u);
    ODM_TEST_CHECK(context,a.events[31].age_samples==700);
    ODM_TEST_CHECK(context,odm_visual_history_resolve(ir,ir_bytes,6u,31000,&b)==ODM_STATUS_OK);
    ODM_TEST_CHECK(context,memcmp(&a,&b,sizeof(a))==0);
    ODM_TEST_CHECK(context,odm_visual_history_resolve(ir,ir_bytes,11u,40000,&a)==ODM_STATUS_OK);
    ODM_TEST_CHECK(context,a.event_count==5u&&a.events[0].cue_id==41u&&a.events[4].cue_id==45u);
    ODM_TEST_CHECK(context,a.events[0].scene_id==3u&&a.events[0].age_samples==7000);
    ODM_TEST_CHECK(context,odm_q1_31_from_ratio(1,3,&one_third)==ODM_STATUS_OK);
    ODM_TEST_CHECK(context,odm_frame_state_resolve(ir,ir_bytes,20,NULL,states,8u,transfers,2u,&nr,&tr,&fs)==ODM_STATUS_OK);
    sn=find_state(states,nr,11u);ODM_TEST_CHECK(context,sn!=NULL);if(sn){ODM_TEST_CHECK(context,sn->entry_weight_q31==0&&sn->opacity_q31==0&&sn->state_slot==7u);}
    ODM_TEST_CHECK(context,odm_frame_state_resolve(ir,ir_bytes,21,NULL,states,8u,transfers,2u,&nr,&tr,&fs)==ODM_STATUS_OK);
    sn=find_state(states,nr,11u);ODM_TEST_CHECK(context,sn!=NULL);if(sn){ODM_TEST_CHECK(context,sn->entry_weight_q31==one_third&&sn->exit_weight_q31==INT32_MAX&&sn->opacity_q31==one_third);}
    ODM_TEST_CHECK(context,odm_frame_state_resolve(ir,ir_bytes,23,NULL,states,8u,transfers,2u,&nr,&tr,&fs)==ODM_STATUS_OK);
    sn=find_state(states,nr,11u);ODM_TEST_CHECK(context,sn!=NULL);if(sn){ODM_TEST_CHECK(context,sn->entry_weight_q31==INT32_MAX&&sn->opacity_q31==INT32_MAX);}
    ODM_TEST_CHECK(context,odm_frame_state_resolve(ir,ir_bytes,29,NULL,states,8u,transfers,2u,&nr,&tr,&fs)==ODM_STATUS_OK);
    sn=find_state(states,nr,11u);ODM_TEST_CHECK(context,sn!=NULL);if(sn){ODM_TEST_CHECK(context,sn->exit_weight_q31==one_third&&sn->opacity_q31==one_third);}

    ODM_TEST_CHECK(context,odm_reference_render_requirements(ir,ir_bytes,21,NULL,&fb,&sb)==ODM_STATUS_OK);
    frame1=(uint8_t*)malloc((size_t)fb);frame2=(uint8_t*)malloc((size_t)fb);scratch=(uint8_t*)malloc((size_t)sb);
    ODM_TEST_CHECK(context,frame1&&frame2&&scratch);
    if(frame1&&frame2&&scratch){
        ODM_TEST_CHECK(context,odm_reference_render_frame(ir,ir_bytes,21,NULL,&assets,NULL,scratch,sb,frame1,fb,&rf,&rs,&i1)==ODM_STATUS_OK);
        ODM_TEST_CHECK(context,rf==fb&&rs==sb);
        ODM_TEST_CHECK(context,odm_reference_render_frame(ir,ir_bytes,21,NULL,&assets,NULL,scratch,sb,frame2,fb,&rf,&rs,&i2)==ODM_STATUS_OK);
        ODM_TEST_CHECK(context,memcmp(frame1,frame2,(size_t)fb)==0&&odm_sha256_equal(&i1.frame_sha256,&i2.frame_sha256)!=0);
        {int nonzero=0;for(p=0u;p<fb;++p)if(frame1[p]!=0u){nonzero=1;break;}ODM_TEST_CHECK(context,nonzero);}
    }

    /* Large deterministic property sweep over all four procedural systems. */
    {
        odm_frame_node_state n;odm_visual_history h;uint32_t caps[4]={ODM_CAP_GRID_PROOF,ODM_CAP_HALO,ODM_CAP_RESIDUAL_EVENT_FIELD,ODM_CAP_DETERMINISTIC_PARTICLES};uint32_t c,x,y,e;
        memset(&n,0,sizeof(n));memset(&h,0,sizeof(h));n.opacity_q31=INT32_MAX;n.scale_x_q32_32=ODM_Q32_ONE;n.scale_y_q32_32=ODM_Q32_ONE;n.phase=UINT32_C(0x31415926);n.state_slot=7u;h.schema_version=1u;h.event_count=32u;h.sample=96000;
        for(e=0u;e<32u;++e){h.events[e].cue_id=e+1u;h.events[e].sample=96000-(int64_t)e*480;h.events[e].age_samples=(int64_t)e*480;h.events[e].seed=UINT64_C(0x9e3779b97f4a7c15)*(uint64_t)(e+1u);}
        for(c=0u;c<4u;++c){n.capability_id=caps[c];for(y=0u;y<128u;++y)for(x=0u;x<128u;++x){odm_visual_pixel_q31 q1,q2;const odm_visual_history*hp=c<2u?NULL:&h;ODM_TEST_CHECK(context,odm_visual_procedural_pixel(&n,hp,96000,x,y,128u,128u,&q1)==ODM_STATUS_OK);ODM_TEST_CHECK(context,odm_visual_procedural_pixel(&n,hp,96000,x,y,128u,128u,&q2)==ODM_STATUS_OK);ODM_TEST_CHECK(context,memcmp(&q1,&q2,sizeof(q1))==0);ODM_TEST_CHECK(context,q1.a>=0&&q1.r>=0&&q1.g>=0&&q1.b>=0);ODM_TEST_CHECK(context,q1.r<=q1.a&&q1.g<=q1.a&&q1.b<=q1.a);}}
    }

    /* Advanced Composition Layer: sequential, deterministic, bounded and transactional. */
    {
        odm_composition_resolver_state cs1,cs2,cs_bad;
        odm_composition_profile profile;
        odm_composition_frame_state cf1,cf2,raw_last,frames[64],sentinel[64];
        odm_music_analysis_tick ticks[64];
        uint32_t i,j,glitches=0u;
        memset(ticks,0,sizeof(ticks));
        for(i=0u;i<64u;++i){
            ticks[i].tick_index=i;ticks[i].center_sample=(uint64_t)i*480u;
            ticks[i].envelope_short_q31=(i%11u==0u)?UINT32_C(805306368):UINT32_C(268435456)+(i*UINT32_C(1000000));
            ticks[i].envelope_medium_q31=UINT32_C(214748364)+(i*UINT32_C(2000000));
            ticks[i].envelope_long_q31=UINT32_C(180000000)+(i*UINT32_C(1000000));
            ticks[i].spectral_flux_q31=(i==10u||i==30u||i==50u)?UINT32_C(700000000):UINT32_C(30000000);
            ticks[i].energy_delta_q31=(i==10u||i==30u||i==50u)?INT64_C(600000000):INT64_C(-1000000);
            ticks[i].stereo_width_q31=UINT32_C(300000000)+(i*UINT32_C(3000000));
            ticks[i].band_energy_q31[0]=UINT32_C(180000000)+(i*UINT32_C(1000000));
            ticks[i].band_energy_q31[1]=UINT32_C(220000000)+(i*UINT32_C(1200000));
            ticks[i].band_energy_q31[2]=UINT32_C(160000000)+(i*UINT32_C(900000));
            ticks[i].band_energy_q31[3]=UINT32_C(120000000)+(i*UINT32_C(700000));
            ticks[i].band_energy_q31[4]=UINT32_C(80000000)+(i*UINT32_C(500000));
            ticks[i].band_energy_q31[5]=UINT32_C(60000000)+(i*UINT32_C(400000));
        }
        ODM_TEST_CHECK(context,odm_composition_resolver_init(&cs1,UINT64_C(0x1122334455667788))==ODM_STATUS_OK);
        cs2=cs1;
        for(i=0u;i<64u;++i){
            ODM_TEST_CHECK(context,odm_composition_resolve_tick(&cs1,&ticks[i],&cf1)==ODM_STATUS_OK);
            ODM_TEST_CHECK(context,odm_composition_resolve_tick(&cs2,&ticks[i],&cf2)==ODM_STATUS_OK);
            ODM_TEST_CHECK(context,memcmp(&cf1,&cf2,sizeof(cf1))==0);
            ODM_TEST_CHECK(context,cf1.schema_version==ODM_COMPOSITION_SCHEMA_VERSION);
            ODM_TEST_CHECK(context,cf1.tick_index==i&&cf1.center_sample==(uint64_t)i*480u);
            ODM_TEST_CHECK(context,cf1.core_scale_q31>=UINT32_C(1030792151)&&cf1.core_scale_q31<=UINT32_C(1245540516));
            ODM_TEST_CHECK(context,cf1.content_offset_x_q31<=INT32_C(322122547)&&cf1.content_offset_x_q31>=-INT32_C(322122547));
            ODM_TEST_CHECK(context,cf1.content_offset_y_q31<=INT32_C(322122547)&&cf1.content_offset_y_q31>=-INT32_C(322122547));
            for(j=0u;j<ODM_COMPOSITION_RADIAL_SEGMENTS;++j) ODM_TEST_CHECK(context,cf1.radial_q31[j]<=INT32_MAX);
            if((cf1.flags&ODM_COMPOSITION_FLAG_GLITCH_GATE)!=0u)++glitches;
        }
        ODM_TEST_CHECK(context,glitches==3u);
        ODM_TEST_CHECK(context,cs1.next_tick_index==64u&&memcmp(&cs1,&cs2,sizeof(cs1))==0);
        raw_last=cf1;
        memset(&profile,0,sizeof(profile));
        ODM_TEST_CHECK(context,odm_composition_profile_build(ticks,64u,&profile)==ODM_STATUS_OK);
        ODM_TEST_CHECK(context,profile.schema_version==ODM_COMPOSITION_SCHEMA_VERSION&&profile.tick_count==64u);
        ODM_TEST_CHECK(context,profile.first_tick_index==0u&&profile.last_tick_index==63u);
        ODM_TEST_CHECK(context,profile.envelope_medium_ref==UINT32_C(340748364));
        ODM_TEST_CHECK(context,profile.spectral_flux_ref==UINT32_C(700000000));
        ODM_TEST_CHECK(context,profile.positive_delta_ref==UINT64_C(600000000));
        ODM_TEST_CHECK(context,profile.band_ref[5]==UINT32_C(85200000));
        ODM_TEST_CHECK(context,odm_composition_resolver_init(&cs1,UINT64_C(0x1122334455667788))==ODM_STATUS_OK);
        glitches=0u;
        for(i=0u;i<64u;++i){ODM_TEST_CHECK(context,odm_composition_resolve_tick_profiled(&cs1,&profile,&ticks[i],&cf1)==ODM_STATUS_OK);if((cf1.flags&ODM_COMPOSITION_FLAG_GLITCH_GATE)!=0u)++glitches;}
        ODM_TEST_CHECK(context,glitches>=3u&&cs1.next_tick_index==64u);

        ODM_TEST_CHECK(context,odm_composition_resolver_init(&cs1,UINT64_C(0x1122334455667788))==ODM_STATUS_OK);
        memset(frames,0,sizeof(frames));
        ODM_TEST_CHECK(context,odm_composition_resolve_batch(&cs1,ticks,64u,frames,64u)==ODM_STATUS_OK);
        ODM_TEST_CHECK(context,cs1.next_tick_index==64u);
        ODM_TEST_CHECK(context,memcmp(&frames[63],&raw_last,sizeof(raw_last))==0);

        cs_bad=cs1; memset(sentinel,0x5a,sizeof(sentinel));
        ticks[1].tick_index=99u;
        ODM_TEST_CHECK(context,odm_composition_resolver_init(&cs1,UINT64_C(0x99))==ODM_STATUS_OK);
        ODM_TEST_CHECK(context,odm_composition_resolve_batch(&cs1,ticks,64u,sentinel,64u)==ODM_STATUS_INVALID_DATA);
        ODM_TEST_CHECK(context,cs1.next_tick_index==0u&&cs1.initialized==0u);
        {int unchanged=1;const unsigned char*q=(const unsigned char*)sentinel;for(i=0u;i<(uint32_t)sizeof(sentinel);++i)if(q[i]!=0x5au){unchanged=0;break;}ODM_TEST_CHECK(context,unchanged);}
        ODM_TEST_CHECK(context,odm_composition_resolve_batch(&cs_bad,NULL,0u,NULL,0u)==ODM_STATUS_OK);
        memset(&cf1,0x5a,sizeof(cf1)); memset(&ticks[0],0,sizeof(ticks[0]));
        ticks[0].tick_index=UINT64_MAX;
        ODM_TEST_CHECK(context,odm_composition_resolver_init(&cs1,UINT64_C(1))==ODM_STATUS_OK);
        ODM_TEST_CHECK(context,odm_composition_resolve_tick(&cs1,&ticks[0],&cf1)==ODM_STATUS_OVERFLOW);
        ODM_TEST_CHECK(context,cs1.initialized==0u&&cs1.next_tick_index==0u);
    }



    /* Visual Director: long-form macro grammar, deterministic transitions and
     * transactional batch publication. */
    {
        odm_director_state ds1,ds2,dsb;
        odm_director_frame_state df1,df2,*batch=NULL,*sent=NULL;
        odm_composition_frame_state *comp=NULL;
        const uint32_t nframes=1800u;
        uint32_t i,shifts=0u,seen=0u,last_epoch=0u,last_transition=0u;
        ODM_TEST_CHECK(context,sizeof(odm_director_state)==88u);
        ODM_TEST_CHECK(context,sizeof(odm_director_frame_state)==128u);
        comp=(odm_composition_frame_state*)calloc(nframes,sizeof(*comp));
        batch=(odm_director_frame_state*)calloc(nframes,sizeof(*batch));
        sent=(odm_director_frame_state*)malloc((size_t)nframes*sizeof(*sent));
        ODM_TEST_CHECK(context,comp&&batch&&sent);
        if(comp&&batch&&sent){
            for(i=0u;i<nframes;++i){
                odm_composition_frame_state *c=&comp[i];
                c->schema_version=ODM_COMPOSITION_SCHEMA_VERSION;c->tick_index=i;c->center_sample=(uint64_t)i*480u;
                c->mode=ODM_COMPOSITION_MODE_FLOW;c->core_scale_q31=UINT32_C(1116691497); /* ~52% => ~40% energy */
                c->lateral_field_q31=UINT32_C(429496730);c->radial_gain_q31=UINT32_C(858993459);c->halo_q31=UINT32_C(536870912);
                c->memory_echo_q31=UINT32_C(107374182);c->fracture_q31=UINT32_C(107374182);c->accent_q31=UINT32_C(644245094);
                if(i>=300u&&i<1200u)c->lateral_field_q31=UINT32_C(1610612735);
                if(i>=900u&&i<1450u){c->fracture_q31=UINT32_C(1288490189);c->accent_q31=UINT32_C(1503238553);}
                if(i==1300u){c->flags|=ODM_COMPOSITION_FLAG_GLITCH_GATE;c->mode=ODM_COMPOSITION_MODE_IMPACT;}
                if(i>=1450u&&i<1500u){c->mode=ODM_COMPOSITION_MODE_VOID;c->void_q31=INT32_MAX;c->accent_q31=0u;c->core_scale_q31=UINT32_C(1030792151);}
                if(i>=1500u){c->core_scale_q31=UINT32_C(1245540516);c->radial_gain_q31=UINT32_C(1932735282);c->halo_q31=UINT32_C(1503238553);c->accent_q31=UINT32_C(966367642);}
            }
            ODM_TEST_CHECK(context,odm_director_init(&ds1,UINT64_C(0xc011ec7100d1ec70))==ODM_STATUS_OK);ds2=ds1;
            for(i=0u;i<nframes;++i){
                ODM_TEST_CHECK(context,odm_director_resolve_tick(&ds1,&comp[i],&df1)==ODM_STATUS_OK);
                ODM_TEST_CHECK(context,odm_director_resolve_tick(&ds2,&comp[i],&df2)==ODM_STATUS_OK);
                ODM_TEST_CHECK(context,memcmp(&df1,&df2,sizeof(df1))==0);
                ODM_TEST_CHECK(context,df1.schema_version==ODM_DIRECTOR_SCHEMA_VERSION&&df1.tick_index==i);
                ODM_TEST_CHECK(context,df1.layout<=ODM_DIRECTOR_LAYOUT_FRACTURE&&df1.previous_layout<=ODM_DIRECTOR_LAYOUT_FRACTURE);
                ODM_TEST_CHECK(context,df1.transition_q31<=INT32_MAX&&df1.core_extent_q31<=INT32_MAX);
                ODM_TEST_CHECK(context,df1.orbit_q31<=INT32_MAX&&df1.wings_q31<=INT32_MAX&&df1.memory_panels_q31<=INT32_MAX);
                ODM_TEST_CHECK(context,df1.layer_count>=1u&&df1.layer_count<=5u&&df1.pane_count<=4u);
                if(i!=0u&&df1.layout_epoch==last_epoch&&last_transition<INT32_MAX) ODM_TEST_CHECK(context,df1.transition_q31>=last_transition);
                last_epoch=df1.layout_epoch;last_transition=df1.transition_q31;
                if((df1.flags&ODM_DIRECTOR_FLAG_LAYOUT_SHIFT)!=0u)++shifts;
                seen|=UINT32_C(1)<<df1.layout;
            }
            ODM_TEST_CHECK(context,shifts>=3u);
            ODM_TEST_CHECK(context,(seen&(UINT32_C(1)<<ODM_DIRECTOR_LAYOUT_VOID))!=0u);
            ODM_TEST_CHECK(context,(seen&(UINT32_C(1)<<ODM_DIRECTOR_LAYOUT_EXPAND))!=0u);
            ODM_TEST_CHECK(context,(seen&(UINT32_C(1)<<ODM_DIRECTOR_LAYOUT_FRACTURE))!=0u);

            ODM_TEST_CHECK(context,odm_director_init(&dsb,UINT64_C(0xc011ec7100d1ec70))==ODM_STATUS_OK);
            ODM_TEST_CHECK(context,odm_director_resolve_batch(&dsb,comp,nframes,batch,nframes)==ODM_STATUS_OK);
            ODM_TEST_CHECK(context,dsb.next_tick_index==nframes&&memcmp(&batch[nframes-1u],&df1,sizeof(df1))==0);

            memset(sent,0x5a,(size_t)nframes*sizeof(*sent));comp[17].tick_index=99u;
            ODM_TEST_CHECK(context,odm_director_init(&dsb,UINT64_C(7))==ODM_STATUS_OK);
            ODM_TEST_CHECK(context,odm_director_resolve_batch(&dsb,comp,nframes,sent,nframes)==ODM_STATUS_INVALID_DATA);
            ODM_TEST_CHECK(context,dsb.initialized==0u&&dsb.next_tick_index==0u);
            {int unchanged=1;const unsigned char*q=(const unsigned char*)sent;uint64_t z;for(z=0u;z<(uint64_t)nframes*sizeof(*sent);++z)if(q[z]!=0x5au){unchanged=0;break;}ODM_TEST_CHECK(context,unchanged);}
            comp[17].tick_index=17u;
            memset(&df1,0x5a,sizeof(df1));comp[0].tick_index=UINT64_MAX;
            ODM_TEST_CHECK(context,odm_director_init(&dsb,UINT64_C(9))==ODM_STATUS_OK);
            ODM_TEST_CHECK(context,odm_director_resolve_tick(&dsb,&comp[0],&df1)==ODM_STATUS_OVERFLOW);
            ODM_TEST_CHECK(context,dsb.initialized==0u&&dsb.next_tick_index==0u);

            /* Director v3 anti-thrash: a one-tick impact is micro-only, and a
             * sustained WINGS candidate requires exactly 180 stable ticks. */
            {
                uint32_t anti_shifts=0u,saw_impact=0u,wing_candidate_tick=UINT32_MAX,wing_shift_tick=UINT32_MAX;
                for(i=0u;i<nframes;++i){
                    odm_composition_frame_state *c=&comp[i];
                    memset(c,0,sizeof(*c));
                    c->schema_version=ODM_COMPOSITION_SCHEMA_VERSION;c->tick_index=i;c->center_sample=(uint64_t)i*480u;
                    c->mode=ODM_COMPOSITION_MODE_FLOW;c->core_scale_q31=UINT32_C(1116691497);
                    c->lateral_field_q31=UINT32_C(350000000);c->radial_gain_q31=UINT32_C(700000000);c->halo_q31=UINT32_C(450000000);
                    c->memory_echo_q31=UINT32_C(80000000);c->fracture_q31=UINT32_C(50000000);c->accent_q31=UINT32_C(650000000);
                    if(i==600u){c->flags=ODM_COMPOSITION_FLAG_GLITCH_GATE;c->mode=ODM_COMPOSITION_MODE_IMPACT;c->fracture_q31=UINT32_C(1500000000);c->accent_q31=INT32_MAX;}
                    if(i>=1200u)c->lateral_field_q31=UINT32_C(1900000000);
                }
                ODM_TEST_CHECK(context,odm_director_init(&dsb,UINT64_C(0xc011ec7100d1ec70))==ODM_STATUS_OK);
                for(i=0u;i<nframes;++i){
                    ODM_TEST_CHECK(context,odm_director_resolve_tick(&dsb,&comp[i],&df1)==ODM_STATUS_OK);
                    if(i==600u){saw_impact=(df1.flags&ODM_DIRECTOR_FLAG_IMPACT)!=0u;ODM_TEST_CHECK(context,df1.layout==ODM_DIRECTOR_LAYOUT_MONOLITH);}
                    if(dsb.candidate_layout==ODM_DIRECTOR_LAYOUT_WINGS&&wing_candidate_tick==UINT32_MAX)wing_candidate_tick=i;
                    if(dsb.candidate_layout==ODM_DIRECTOR_LAYOUT_WINGS&&dsb.candidate_ticks<UINT32_C(180))ODM_TEST_CHECK(context,df1.layout==ODM_DIRECTOR_LAYOUT_MONOLITH);
                    if((df1.flags&ODM_DIRECTOR_FLAG_LAYOUT_SHIFT)!=0u){++anti_shifts;if(df1.layout==ODM_DIRECTOR_LAYOUT_WINGS&&wing_shift_tick==UINT32_MAX)wing_shift_tick=i;}
                }
                ODM_TEST_CHECK(context,saw_impact!=0u&&anti_shifts==1u);
                ODM_TEST_CHECK(context,wing_candidate_tick!=UINT32_MAX&&wing_shift_tick==wing_candidate_tick+UINT32_C(179));
            }

            /* Director v3 structural fracture: after smoothed fracture crosses
             * the candidate threshold, exactly ten stable FRACTURE candidate
             * ticks are required. Return is held until both candidate recovery
             * and the 180-tick minimum FRACTURE hold are satisfied. */
            {
                uint32_t fracture_in=UINT32_MAX,fracture_out=UINT32_MAX,local_shifts=0u;
                for(i=0u;i<nframes;++i){
                    odm_composition_frame_state *c=&comp[i];
                    memset(c,0,sizeof(*c));
                    c->schema_version=ODM_COMPOSITION_SCHEMA_VERSION;c->tick_index=i;c->center_sample=(uint64_t)i*480u;
                    c->mode=ODM_COMPOSITION_MODE_FLOW;c->core_scale_q31=UINT32_C(1116691497);
                    c->lateral_field_q31=UINT32_C(350000000);c->radial_gain_q31=UINT32_C(700000000);c->halo_q31=UINT32_C(450000000);
                    c->memory_echo_q31=UINT32_C(80000000);c->fracture_q31=UINT32_C(50000000);c->accent_q31=UINT32_C(650000000);
                    if(i>=1200u&&i<1300u){c->fracture_q31=UINT32_C(1500000000);c->accent_q31=INT32_MAX;}
                }
                ODM_TEST_CHECK(context,odm_director_init(&dsb,UINT64_C(0x51f7ac7))==ODM_STATUS_OK);
                for(i=0u;i<nframes;++i){
                    ODM_TEST_CHECK(context,odm_director_resolve_tick(&dsb,&comp[i],&df1)==ODM_STATUS_OK);
                    if((df1.flags&ODM_DIRECTOR_FLAG_LAYOUT_SHIFT)!=0u){
                        ++local_shifts;
                        if(df1.layout==ODM_DIRECTOR_LAYOUT_FRACTURE&&fracture_in==UINT32_MAX)fracture_in=i;
                        if(df1.previous_layout==ODM_DIRECTOR_LAYOUT_FRACTURE&&df1.layout==ODM_DIRECTOR_LAYOUT_MONOLITH&&fracture_out==UINT32_MAX)fracture_out=i;
                    }
                    if(i>=1200u&&i<1215u)ODM_TEST_CHECK(context,df1.layout==ODM_DIRECTOR_LAYOUT_MONOLITH);
                    if(i>=1216u&&i<1395u)ODM_TEST_CHECK(context,df1.layout==ODM_DIRECTOR_LAYOUT_FRACTURE);
                }
                ODM_TEST_CHECK(context,fracture_in==UINT32_C(1215));
                ODM_TEST_CHECK(context,fracture_out==UINT32_C(1395));
                ODM_TEST_CHECK(context,local_shifts==2u);
            }
        }
        free(sent);free(batch);free(comp);
    }


    /* Strict-causal Director: project seed and autonomous phase are forbidden
     * from changing any published geometry when the Composition frame carries
     * the explicit STRICT_CAUSAL authority. Long stagnation must not trigger
     * the legacy seeded alternate path. */
    {
        odm_director_state strict_state_a, strict_state_b;
        odm_director_frame_state strict_frame_a, strict_frame_b;
        odm_composition_frame_state c;
        uint32_t i;
        ODM_TEST_CHECK(context, odm_director_init(&strict_state_a, UINT64_C(1)) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_director_init(&strict_state_b, UINT64_C(0xfedcba9876543210)) == ODM_STATUS_OK);
        for (i = 0u; i < UINT32_C(5000); ++i) {
            memset(&c, 0, sizeof(c));
            c.schema_version = ODM_COMPOSITION_SCHEMA_VERSION;
            c.flags = ODM_COMPOSITION_FLAG_RADIAL_HIRES | ODM_COMPOSITION_FLAG_STRICT_CAUSAL;
            c.tick_index = i;
            c.center_sample = (uint64_t)i * UINT64_C(480);
            c.mode = ODM_COMPOSITION_MODE_FLOW;
            c.core_scale_q31 = UINT32_C(1116691497);
            c.lateral_field_q31 = UINT32_C(350000000);
            c.radial_gain_q31 = UINT32_C(700000000);
            c.halo_q31 = UINT32_C(450000000);
            c.memory_echo_q31 = UINT32_C(80000000);
            c.fracture_q31 = UINT32_C(50000000);
            c.accent_q31 = UINT32_C(700000000);
            /* Deliberately poison procedural fields. Strict mode must ignore
             * these as geometry authorities. */
            c.phase = UINT32_C(0x12345678) + i * UINT32_C(7919);
            c.ring_phase = UINT32_C(0x87654321) - i * UINT32_C(6151);
            ODM_TEST_CHECK(context, odm_director_resolve_tick(&strict_state_a, &c, &strict_frame_a) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context, odm_director_resolve_tick(&strict_state_b, &c, &strict_frame_b) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context, memcmp(&strict_frame_a, &strict_frame_b, sizeof(strict_frame_a)) == 0);
            ODM_TEST_CHECK(context, strict_frame_a.asymmetry_x_q31 == 0 && strict_frame_a.asymmetry_y_q31 == 0);
            ODM_TEST_CHECK(context, strict_frame_a.orbit_phase == 0u);
            ODM_TEST_CHECK(context, (strict_frame_a.flags & ODM_DIRECTOR_FLAG_LAYOUT_SHIFT) == 0u);
            ODM_TEST_CHECK(context, strict_frame_a.layout == ODM_DIRECTOR_LAYOUT_MONOLITH);
        }
    }

    /* Sample-accurate causal presentation: the local 96-lane projection
     * remains untouched while the global contextual event is published in the
     * exact effective-sample frame. Coarse 100 Hz event authority is not
     * allowed to create a second global impact. */
    {
        odm_music_analysis_tick base;
        odm_music_reaction_projection rp;
        odm_music_reaction_event_projection ep;
        odm_composition_frame_state out, sentinel;
        uint32_t i;
        memset(&base, 0, sizeof(base));
        memset(&rp, 0, sizeof(rp));
        memset(&ep, 0, sizeof(ep));
        memset(&out, 0, sizeof(out));
        memset(&sentinel, 0x5a, sizeof(sentinel));
        base.tick_index = UINT64_C(7);
        base.center_sample = UINT64_C(3360);
        base.stereo_width_q31 = UINT32_C(700000000);
        rp.schema_version = ODM_MUSIC_REACTION_PROJECTION_SCHEMA_VERSION;
        rp.previous_presentation_sample = INT64_C(3199);
        rp.presentation_sample = UINT64_C(3400);
        rp.source_tick_index = base.tick_index;
        rp.source_center_sample = base.center_sample;
        rp.first_captured_tick_index = UINT64_C(7);
        rp.last_captured_tick_index = UINT64_C(7);
        rp.captured_tick_count = 1u;
        rp.event_threshold_q31 = UINT32_C(400000000);
        rp.broadband_q31 = UINT32_C(800000000);
        rp.broadband_attack_q31 = UINT32_C(300000000);
        for (i = 0u; i < ODM_MUSIC_REACTION_LANE_COUNT; ++i) {
            rp.lane_q31[i] = UINT32_C(900000000) - i * UINT32_C(1000000);
            rp.lane_fast_q31[i] = UINT32_C(700000000) - i * UINT32_C(700000);
            rp.lane_attack_q31[i] = (i == 40u) ? UINT32_C(1200000000) : UINT32_C(50000000);
        }
        for (i = 0u; i < ODM_MUSIC_REACTION_FAMILY_COUNT; ++i) {
            rp.family_q31[i] = UINT32_C(500000000) + i * UINT32_C(30000000);
            rp.family_attack_q31[i] = UINT32_C(100000000) + i * UINT32_C(20000000);
        }
        ep.schema_version = ODM_MUSIC_REACTION_EVENT_PROJECTION_SCHEMA_VERSION;
        ep.previous_presentation_sample = rp.previous_presentation_sample;
        ep.presentation_sample = rp.presentation_sample;
        ep.first_event_index = UINT64_C(12);
        ep.last_event_index = UINT64_C(12);
        ep.dominant_event_index = UINT64_C(12);
        ep.dominant_effective_sample = UINT64_C(3370);
        ep.pulse_owner_event_index = UINT64_C(12);
        ep.pulse_owner_effective_sample = UINT64_C(3370);
        ep.captured_event_count = 1u;
        ep.event_flags = ODM_MUSIC_REACTION_EVENT_BASS | ODM_MUSIC_REACTION_EVENT_ANY |
                         ODM_MUSIC_REACTION_EVENT_OFFLINE_PEAK |
                         ODM_MUSIC_REACTION_EVENT_SUBTICK_TRUSTED;
        ep.event_salience_q31 = UINT32_C(1950000000);
        ep.event_pulse_q31 = UINT32_C(1950000000);
        ep.timing_confidence_q31 = UINT32_C(600000000);
        ep.strongest_family = ODM_MUSIC_REACTION_FAMILY_BASS;
        ep.second_family = ODM_MUSIC_REACTION_FAMILY_BODY;
        ep.broadband_attack_q31 = UINT32_C(1200000000);
        ep.score_q31 = UINT32_C(1400000000);
        ep.strength_basis_q31 = UINT32_C(1300000000);
        ep.pulse_owner_salience_q31 = UINT32_C(1950000000);
        ep.dominant_pulse_q31 = UINT32_C(1900000000);
        ODM_TEST_CHECK(context,
            odm_composition_resolve_music_reaction_sample_presentation(&base, &rp, &ep, &out) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, out.mode == ODM_COMPOSITION_MODE_IMPACT);
        ODM_TEST_CHECK(context, (out.flags & ODM_COMPOSITION_FLAG_GLITCH_GATE) != 0u);
        ODM_TEST_CHECK(context, out.fracture_q31 != 0u);
        ODM_TEST_CHECK(context, out.radial_attack_q31[40] == UINT32_C(1200000000));
        ODM_TEST_CHECK(context, out.radial_attack_q31[39] == UINT32_C(50000000));

        /* Same local projection with no newly captured event: no second macro
         * impact, while the sample-domain pulse may keep memory alive. */
        ep.event_flags = 0u;
        ep.first_event_index = UINT64_MAX;
        ep.last_event_index = UINT64_MAX;
        ep.dominant_event_index = UINT64_MAX;
        ep.dominant_effective_sample = UINT64_MAX;
        ep.captured_event_count = 0u;
        ep.event_salience_q31 = 0u;
        ep.timing_confidence_q31 = 0u;
        ep.strongest_family = UINT32_MAX;
        ep.second_family = UINT32_MAX;
        ep.broadband_attack_q31 = 0u;
        ep.score_q31 = 0u;
        ep.strength_basis_q31 = 0u;
        ep.dominant_pulse_q31 = 0u;
        ep.event_pulse_q31 = UINT32_C(900000000);
        ep.pulse_owner_salience_q31 = UINT32_C(1950000000);
        ODM_TEST_CHECK(context,
            odm_composition_resolve_music_reaction_sample_presentation(&base, &rp, &ep, &out) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, out.mode != ODM_COMPOSITION_MODE_IMPACT);
        ODM_TEST_CHECK(context, out.memory_echo_q31 != 0u);
        ODM_TEST_CHECK(context, out.radial_attack_q31[40] == UINT32_C(1200000000));

        /* Cross-domain timestamp mismatch is fail-closed and transactional. */
        out = sentinel;
        ep.presentation_sample += 1u;
        ODM_TEST_CHECK(context,
            odm_composition_resolve_music_reaction_sample_presentation(&base, &rp, &ep, &out) == ODM_STATUS_INVALID_DATA);
        ODM_TEST_CHECK(context, memcmp(&out, &sentinel, sizeof(out)) == 0);
    }

    /* Visual Policy v1: fixed canonical bytes + frozen semantic hash. */
    {
        uint8_t policy[ODM_VISUAL_POLICY_BYTES];
        uint8_t small[ODM_VISUAL_POLICY_BYTES - 1u];
        odm_sha256_digest digest;
        uint64_t required = 0u, hex_required = 0u;
        char hex[ODM_SHA256_HEX_BUFFER_BYTES];
        memset(policy, 0xa5, sizeof(policy));
        memset(small, 0x5a, sizeof(small));
        ODM_TEST_CHECK(context, odm_visual_policy_bytes(NULL, 0u, NULL) == ODM_STATUS_INVALID_ARGUMENT);
        ODM_TEST_CHECK(context, odm_visual_policy_bytes(NULL, 0u, &required) == ODM_STATUS_BUFFER_TOO_SMALL);
        ODM_TEST_CHECK(context, required == ODM_VISUAL_POLICY_BYTES);
        ODM_TEST_CHECK(context, odm_visual_policy_bytes(small, sizeof(small), &required) == ODM_STATUS_BUFFER_TOO_SMALL);
        ODM_TEST_CHECK(context, small[0] == 0x5au && small[sizeof(small)-1u] == 0x5au);
        ODM_TEST_CHECK(context, odm_visual_policy_bytes(policy, sizeof(policy), &required) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, required == sizeof(policy));
        ODM_TEST_CHECK(context, memcmp(policy, "ODMVPOL1", 8u) == 0);
        ODM_TEST_CHECK(context, odm_visual_policy_current_sha256(NULL) == ODM_STATUS_INVALID_ARGUMENT);
        ODM_TEST_CHECK(context, odm_visual_policy_current_sha256(&digest) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_sha256_to_hex(&digest, hex, sizeof(hex), &hex_required) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, hex_required == ODM_SHA256_HEX_BUFFER_BYTES);
        ODM_TEST_CHECK(context, strcmp(hex, "07088616242b45547b46278de634f9284e524858c15c86b74c2e9ebf373c0387") == 0);
    }

    free(scratch);free(frame2);free(frame1);free(ir);
}

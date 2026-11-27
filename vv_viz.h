// vv_viz.h - C API for the LUA Visual engine (Swift/Metal side). UI thread only; never call from the audio thread.
#ifndef VV_VIZ_H
#define VV_VIZ_H
#ifdef __cplusplus
extern "C" {
#endif
typedef struct VVViz VVViz;
typedef struct { int enabled, logResponse, stereo; float sensitivity, gain, smoothing, attack, release, threshold; float cross[6]; float user[2]; } VVVizCfg;
VVViz* vv_viz_create(void);
void   vv_viz_destroy(VVViz* v);
/* Compile + run setup. Returns 1 on success. On failure the previous script (if any) stays active; read vv_viz_error. */
int    vv_viz_load(VVViz* v, const char* src);
int    vv_viz_check(const char* src, char* err, int errCap, int* line, int* col);   /* syntax check only */
const char* vv_viz_error(VVViz* v);
const char* vv_viz_meta_json(VVViz* v);       /* {name,category,author,desc,shader,params:[{id,label,group,unit,min,max,def,step,kind,band,depth,pol,options[]}]} */
int    vv_viz_nparams(VVViz* v);
void   vv_viz_set_param(VVViz* v, int idx, float value);
float  vv_viz_get_param(VVViz* v, int idx);
void   vv_viz_set_assign(VVViz* v, int idx, int enabled, int band, float depth, int pol, float smooth, float lo, float hi);
void   vv_viz_get_assign(VVViz* v, int idx, int* enabled, int* band, float* depth, int* pol, float* smooth, float* lo, float* hi);
void   vv_viz_reset_params(VVViz* v);
void   vv_viz_randomise_values(VVViz* v);
void   vv_viz_randomise_assign(VVViz* v);
void   vv_viz_clear_assign(VVViz* v);
void   vv_viz_get_cfg(VVViz* v, VVVizCfg* out);
void   vv_viz_set_cfg(VVViz* v, const VVVizCfg* cfg);
/* One frame. L/R: newest >=2048 master samples (n = count available); pass demo=1 for the synthetic signal. Returns 1 if the script ran, 0 if halted (last picture held). */
int    vv_viz_frame(VVViz* v, float dt, const float* L, const float* R, int n, float sampleRate, int w, int h, int quality, int fps, float scale, int demo);
const float* vv_viz_uniforms(VVViz* v);       /* 32 floats */
const float* vv_viz_post(VVViz* v);           /* 12 floats: bloom chroma feedback zoom rotate warp vignette grain hue saturation brightness contrast */
const float* vv_viz_cmds(VVViz* v);           /* ncmd * 8 floats */
int    vv_viz_ncmd(VVViz* v);
const float* vv_viz_bands(VVViz* v);          /* 9 analysed band levels (full sub bass lowmid mid highmid high treble user) */
float  vv_viz_beat(VVViz* v);
long   vv_viz_last_update(VVViz* v);          /* instruction counts of the last frame, for the readout */
long   vv_viz_last_render(VVViz* v);
#ifdef __cplusplus
}
#endif
#endif

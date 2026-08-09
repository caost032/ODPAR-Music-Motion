#include "compositor_internal.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static uint32_t rect_pixel_coverage(int64_t l,int64_t t,int64_t r,int64_t b,
                                    uint32_t x,uint32_t y){
    int64_t pl=(int64_t)x<<16,pt=(int64_t)y<<16,pr=pl+65536,pb=pt+65536;
    int64_t ox=(r<pr?r:pr)-(l>pl?l:pl),oy=(b<pb?b:pb)-(t>pt?t:pt);
    uint64_t area,num;
    if(ox<=0||oy<=0)return 0u;
    area=(uint64_t)ox*(uint64_t)oy;
    num=area*(uint64_t)INT32_MAX+UINT64_C(2147483648);
    return (uint32_t)(num>>32);
}

static void draw_rect(odm_layered_pixel16*frame,uint32_t w,uint32_t h,
                      int64_t l,int64_t t,int64_t r,int64_t b,
                      const odm_rgba16*color,uint32_t opacity){
    int64_t x0=l>>16,y0=t>>16,x1=(r+65535)>>16,y1=(b+65535)>>16,x,y;
    if(l>=r||t>=b)return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int64_t)w) x1 = (int64_t)w;
    if (y1 > (int64_t)h) y1 = (int64_t)h;
    for(y=y0;y<y1;++y)for(x=x0;x<x1;++x){
        uint32_t cov=rect_pixel_coverage(l,t,r,b,(uint32_t)x,(uint32_t)y);
        if(cov)odm_layered_blend16(&frame[(uint64_t)y*w+(uint64_t)x],color,cov,opacity);
    }
}

static int64_t hud_abs_i64(int64_t v){return v<0?-v:v;}

/* Canonical rounded-rectangle SDF at pixel centers.  HUD capsules use a
 * one-pixel linear coverage ramp; no host font/graphics API participates. */
static int64_t round_rect_sdf_q16(int64_t px,int64_t py,
                                  int64_t l,int64_t t,int64_t r,int64_t b){
    int64_t hw=(r-l)/2,hh=(b-t)/2,cx=l+hw,cy=t+hh,rad=hw<hh?hw:hh;
    int64_t qx=hud_abs_i64(px-cx)-(hw-rad),qy=hud_abs_i64(py-cy)-(hh-rad);
    int64_t ox=qx>0?qx:0,oy=qy>0?qy:0,outside,inside;
    uint64_t ux=(uint64_t)ox,uy=(uint64_t)oy;
    outside=(int64_t)odm_layered_isqrt_u64(ux*ux+uy*uy);
    inside=(qx>qy?qx:qy);if(inside>0)inside=0;
    return outside+inside-rad;
}

static uint32_t signed_sdf_1px_coverage(int64_t sdf){
    const int64_t half=INT64_C(32768),feather=INT64_C(65536);
    int64_t num;
    if(sdf<=-half)return (uint32_t)INT32_MAX;
    if(sdf>=half)return 0u;
    num=(half-sdf)*(int64_t)INT32_MAX+feather/2;
    return (uint32_t)(num/feather);
}

static void draw_round_rect(odm_layered_pixel16*frame,uint32_t w,uint32_t h,
                            int64_t l,int64_t t,int64_t r,int64_t b,
                            const odm_rgba16*color,uint32_t opacity){
    int64_t x0=(l-65536)>>16,y0=(t-65536)>>16;
    int64_t x1=(r+131071)>>16,y1=(b+131071)>>16,x,y;
    if(l>=r||t>=b)return;
    if(x0<0)x0=0;
    if(y0<0)y0=0;
    if(x1>(int64_t)w)x1=(int64_t)w;
    if(y1>(int64_t)h)y1=(int64_t)h;
    for(y=y0;y<y1;++y)for(x=x0;x<x1;++x){
        int64_t px=(x<<16)+32768,py=(y<<16)+32768;
        uint32_t cov=signed_sdf_1px_coverage(round_rect_sdf_q16(px,py,l,t,r,b));
        if(cov)odm_layered_blend16(&frame[(uint64_t)y*w+(uint64_t)x],color,cov,opacity);
    }
}

/* Minimal deterministic 5x7 display alphabet. Lowercase maps to uppercase.
 * Bits are rows, bit 0 is top. This is an optional built-in metadata fallback;
 * a future signed font resource can replace it without changing HUD geometry. */
static void glyph5x7(char in,uint8_t out[5]){
    char c=in;
    if(c>='a'&&c<='z')c=(char)(c-'a'+'A');
    memset(out,0,5);
#define G(a,b,c2,d,e) do{out[0]=(a);out[1]=(b);out[2]=(c2);out[3]=(d);out[4]=(e);}while(0)
    switch(c){
    case 'A':G(0x7e,0x09,0x09,0x09,0x7e);break; case 'B':G(0x7f,0x49,0x49,0x49,0x36);break;
    case 'C':G(0x3e,0x41,0x41,0x41,0x22);break; case 'D':G(0x7f,0x41,0x41,0x22,0x1c);break;
    case 'E':G(0x7f,0x49,0x49,0x49,0x41);break; case 'F':G(0x7f,0x09,0x09,0x09,0x01);break;
    case 'G':G(0x3e,0x41,0x49,0x49,0x7a);break; case 'H':G(0x7f,0x08,0x08,0x08,0x7f);break;
    case 'I':G(0x41,0x41,0x7f,0x41,0x41);break; case 'J':G(0x20,0x40,0x41,0x3f,0x01);break;
    case 'K':G(0x7f,0x08,0x14,0x22,0x41);break; case 'L':G(0x7f,0x40,0x40,0x40,0x40);break;
    case 'M':G(0x7f,0x02,0x0c,0x02,0x7f);break; case 'N':G(0x7f,0x04,0x08,0x10,0x7f);break;
    case 'O':G(0x3e,0x41,0x41,0x41,0x3e);break; case 'P':G(0x7f,0x09,0x09,0x09,0x06);break;
    case 'Q':G(0x3e,0x41,0x51,0x21,0x5e);break; case 'R':G(0x7f,0x09,0x19,0x29,0x46);break;
    case 'S':G(0x46,0x49,0x49,0x49,0x31);break; case 'T':G(0x01,0x01,0x7f,0x01,0x01);break;
    case 'U':G(0x3f,0x40,0x40,0x40,0x3f);break; case 'V':G(0x1f,0x20,0x40,0x20,0x1f);break;
    case 'W':G(0x7f,0x20,0x18,0x20,0x7f);break; case 'X':G(0x63,0x14,0x08,0x14,0x63);break;
    case 'Y':G(0x03,0x04,0x78,0x04,0x03);break; case 'Z':G(0x61,0x51,0x49,0x45,0x43);break;
    case '0':G(0x3e,0x51,0x49,0x45,0x3e);break; case '1':G(0x00,0x42,0x7f,0x40,0x00);break;
    case '2':G(0x62,0x51,0x49,0x49,0x46);break; case '3':G(0x22,0x41,0x49,0x49,0x36);break;
    case '4':G(0x18,0x14,0x12,0x7f,0x10);break; case '5':G(0x2f,0x49,0x49,0x49,0x31);break;
    case '6':G(0x3e,0x49,0x49,0x49,0x32);break; case '7':G(0x01,0x71,0x09,0x05,0x03);break;
    case '8':G(0x36,0x49,0x49,0x49,0x36);break; case '9':G(0x26,0x49,0x49,0x49,0x3e);break;
    case '-':G(0x08,0x08,0x08,0x08,0x08);break; case '_':G(0x40,0x40,0x40,0x40,0x40);break;
    case '.':G(0x00,0x60,0x60,0x00,0x00);break; case ',':G(0x00,0x40,0x20,0x00,0x00);break;
    case ':':G(0x00,0x36,0x36,0x00,0x00);break; case '\'':G(0x00,0x03,0x00,0x00,0x00);break;
    case '/':G(0x60,0x18,0x06,0x01,0x00);break; case '!':G(0x00,0x00,0x5f,0x00,0x00);break;
    case '?':G(0x02,0x01,0x51,0x09,0x06);break; case '&':G(0x36,0x49,0x55,0x22,0x50);break;
    case '(':G(0x00,0x1c,0x22,0x41,0x00);break; case ')':G(0x00,0x41,0x22,0x1c,0x00);break;
    case ' ':break; default:G(0x7f,0x41,0x5d,0x41,0x7f);break;
    }
#undef G
}

static int64_t text_width_q16(const char*s,int64_t scale){
    size_t n=strlen(s); if(n==0u)return 0; return (int64_t)n*6*scale-scale;
}

static void draw_text(odm_layered_pixel16*frame,uint32_t w,uint32_t h,
                      int64_t xq,int64_t yq,int64_t scale,const char*s,
                      const odm_rgba16*color,uint32_t opacity){
    size_t k; if(!s||scale<=0)return;
    for(k=0u;s[k]!='\0';++k){
        uint8_t g[5];uint32_t col,row;glyph5x7(s[k],g);
        for(col=0u;col<5u;++col)for(row=0u;row<7u;++row)if((g[col]&(1u<<row))!=0u){
            int64_t l=xq+((int64_t)k*6+(int64_t)col)*scale;
            int64_t t=yq+(int64_t)row*scale;
            draw_rect(frame,w,h,l,t,l+scale,t+scale,color,opacity);
        }
    }
}

static size_t append_uint(char*b,size_t cap,size_t pos,uint64_t v,unsigned min_digits){
    char tmp[32];unsigned n=0u,i;do{tmp[n++]=(char)('0'+v%10u);v/=10u;}while(v&&n<sizeof(tmp));
    while(n<min_digits&&n<sizeof(tmp))tmp[n++]='0';
    for(i=0u;i<n&&pos+1u<cap;++i)b[pos++]=tmp[n-1u-i];
    if (pos < cap) b[pos] = '\0';
    return pos;
}

static void format_time(char out[32],int64_t sample){
    uint64_t sec=(uint64_t)(sample<0?0:sample)/UINT64_C(48000),m=sec/60u,ss=sec%60u;size_t p=0u;
    out[0]='\0';p=append_uint(out,32u,p,m,1u);if(p+1u<32u){out[p++]=':';out[p]='\0';}(void)append_uint(out,32u,p,ss,2u);
}

static void format_time_mode(char out[68],uint32_t mode,int64_t sample,int64_t duration){
    char elapsed[32],total[32];size_t ne,nt;
    if(sample<0)sample=0;
    if(sample>duration)sample=duration;
    if(mode==ODM_HUD_TIME_REMAINING){
        format_time(total,duration-sample);out[0]='-';
        nt=strlen(total);if(nt+2u<=68u){memcpy(out+1,total,nt+1u);}else out[0]='\0';
        return;
    }
    format_time(elapsed,sample);
    if(mode==ODM_HUD_TIME_ELAPSED){
        ne=strlen(elapsed);if(ne+1u<=68u)memcpy(out,elapsed,ne+1u);else out[0]='\0';
        return;
    }
    format_time(total,duration);ne=strlen(elapsed);nt=strlen(total);
    if(ne+3u+nt<68u){memcpy(out,elapsed,ne);out[ne]=' ';out[ne+1]='/';out[ne+2]=' ';memcpy(out+ne+3,total,nt+1u);}
    else out[0]='\0';
}

static int64_t anchored_x(uint32_t anchor,int64_t safe_l,int64_t safe_r,
                          int64_t margin,int64_t width){
    uint32_t col=anchor%3u;
    if(col==1u)return safe_l+(safe_r-safe_l-width)/2;
    if(col==2u)return safe_r-margin-width;
    return safe_l+margin;
}

static uint32_t sevenseg_mask(char c){
    enum { A=1u<<0, B=1u<<1, C=1u<<2, D=1u<<3, E=1u<<4, F=1u<<5, G=1u<<6 };
    switch(c){
    case '0':return A|B|C|D|E|F; case '1':return B|C; case '2':return A|B|G|E|D;
    case '3':return A|B|G|C|D; case '4':return F|G|B|C; case '5':return A|F|G|C|D;
    case '6':return A|F|G|E|C|D; case '7':return A|B|C; case '8':return A|B|C|D|E|F|G;
    case '9':return A|B|C|D|F|G; default:return 0u;
    }
}

static int64_t vector_time_width_q16(const char*s,int64_t scale){
    int64_t w=0;size_t i;
    if(!s||scale<=0)return 0;
    for(i=0u;s[i]!='\0';++i){
        if(s[i]==':')w+=2*scale; else if(s[i]>='0'&&s[i]<='9')w+=5*scale; else w+=3*scale;
    }
    return w>0?w-scale:0;
}

static void draw_vector_time(odm_layered_pixel16*frame,uint32_t w,uint32_t h,
                             int64_t x,int64_t y,int64_t scale,const char*s,
                             const odm_rgba16*color,uint32_t opacity){
    size_t i;int64_t cursor=x;
    if(!s||scale<=0)return;
    for(i=0u;s[i]!='\0';++i){
        char c=s[i];
        if(c==':'){
            int64_t d=scale/2;if(d<32768)d=32768;
            draw_round_rect(frame,w,h,cursor,y+2*scale-d/2,cursor+d,y+2*scale+d/2,color,opacity);
            draw_round_rect(frame,w,h,cursor,y+5*scale-d/2,cursor+d,y+5*scale+d/2,color,opacity);
            cursor+=2*scale;continue;
        }
        if(c>='0'&&c<='9'){
            uint32_t m=sevenseg_mask(c);int64_t th=scale/2;
            int64_t l=cursor,r=cursor+4*scale,top=y,mid=y+3*scale,bottom=y+6*scale;
            if(th<32768)th=32768;
#define HS(bit,yy) do{if((m&(bit))!=0u)draw_round_rect(frame,w,h,l+th/2,(yy),r-th/2,(yy)+th,color,opacity);}while(0)
#define VS(bit,xx,yt,yb) do{if((m&(bit))!=0u)draw_round_rect(frame,w,h,(xx),(yt)+th/2,(xx)+th,(yb)-th/2,color,opacity);}while(0)
            HS(1u<<0,top); HS(1u<<6,mid); HS(1u<<3,bottom);
            VS(1u<<5,l,top,mid); VS(1u<<1,r-th,top,mid);
            VS(1u<<4,l,mid,bottom); VS(1u<<2,r-th,mid,bottom);
#undef HS
#undef VS
            cursor+=5*scale;continue;
        }
        cursor+=3*scale;
    }
}

static int anchor_bottom(uint32_t anchor){return anchor>=ODM_HUD_ANCHOR_BOTTOM_LEFT;}

void odm_layered_hud_render(const odm_layered_config*c,const odm_layered_frame_plan*p,
                            odm_layered_pixel16*frame){
    const odm_hud_config*h=&c->hud;int64_t margin=h->margin_q16,scale=h->text_scale_q16;
    int64_t safe_l=p->safe_left_q16,safe_t=p->safe_top_q16,safe_r=p->safe_right_q16,safe_b=p->safe_bottom_q16;
    int64_t progress_h=(int64_t)h->progress_height_q16;
    int64_t bottom_reserved=0;
    if(!frame||h->flags==0u||h->opacity_q31==0u)return;
    if((h->flags&ODM_HUD_PROGRESS_BAR)!=0u){
        int64_t full=safe_r-safe_l;
        int64_t bw=(full*(int64_t)h->progress_width_q31+INT32_MAX/2)/INT32_MAX;
        int64_t l=safe_l+(full-bw)/2,r=l+bw,b=safe_b-margin,t,fill;
        if(h->progress_style==ODM_HUD_PROGRESS_HAIRLINE&&progress_h>INT64_C(65536))progress_h=INT64_C(65536);
        t=b-progress_h;fill=l+(bw*(int64_t)p->progress_q31+INT32_MAX/2)/INT32_MAX;
        if(h->progress_style==ODM_HUD_PROGRESS_CAPSULE){
            draw_round_rect(frame,p->width,p->height,l,t,r,b,&h->background_color,h->opacity_q31);
            if(fill>l)draw_round_rect(frame,p->width,p->height,l,t,fill,b,&h->foreground_color,h->opacity_q31);
        }else{
            draw_rect(frame,p->width,p->height,l,t,r,b,&h->background_color,h->opacity_q31);
            draw_rect(frame,p->width,p->height,l,t,fill,b,&h->foreground_color,h->opacity_q31);
        }
        bottom_reserved+=progress_h+h->line_gap_q16;
    }
    if((h->flags&ODM_HUD_TIME_CODE)!=0u){
        char txt[68];int64_t y,x,tw;
        format_time_mode(txt,h->time_mode,p->sample,p->duration_samples);tw=vector_time_width_q16(txt,scale);
        y=safe_b-margin-bottom_reserved-7*scale;
        x=safe_r-margin-tw;
        draw_vector_time(frame,p->width,p->height,x,y,scale,txt,&h->foreground_color,h->opacity_q31);
        bottom_reserved+=7*scale+h->line_gap_q16;
    }
    if(((h->flags&ODM_HUD_TITLE)!=0u&&h->title[0]!='\0')||
       ((h->flags&ODM_HUD_ARTIST)!=0u&&h->artist[0]!='\0')){
        int have_title=((h->flags&ODM_HUD_TITLE)!=0u&&h->title[0]!='\0');
        int have_artist=((h->flags&ODM_HUD_ARTIST)!=0u&&h->artist[0]!='\0');
        int64_t tw=have_title?text_width_q16(h->title,scale):0;
        int64_t aw=have_artist?text_width_q16(h->artist,scale):0;
        int64_t mw=tw>aw?tw:aw;
        int64_t total=(have_title?7*scale:0)+(have_title&&have_artist?h->line_gap_q16:0)+(have_artist?7*scale:0);
        int64_t x=anchored_x(h->metadata_anchor,safe_l,safe_r,margin,mw);
        int64_t y=anchor_bottom(h->metadata_anchor)?safe_b-margin-bottom_reserved-total:safe_t+margin;
        if(have_title){
            int64_t tx=x;if((h->metadata_anchor%3u)==1u)tx=x+(mw-tw)/2;else if((h->metadata_anchor%3u)==2u)tx=x+(mw-tw);
            draw_text(frame,p->width,p->height,tx,y,scale,h->title,&h->foreground_color,h->opacity_q31);y+=7*scale;
            if(have_artist)y+=h->line_gap_q16;
        }
        if(have_artist){
            int64_t ax=x;if((h->metadata_anchor%3u)==1u)ax=x+(mw-aw)/2;else if((h->metadata_anchor%3u)==2u)ax=x+(mw-aw);
            draw_text(frame,p->width,p->height,ax,y,scale,h->artist,&h->foreground_color,h->opacity_q31);
        }
    }
}

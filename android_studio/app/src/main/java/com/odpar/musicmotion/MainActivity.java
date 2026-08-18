package com.odpar.musicmotion;

import android.app.Activity;
import android.app.AlertDialog;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.InputType;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.HorizontalScrollView;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Space;
import android.widget.TextView;
import android.widget.Toast;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicInteger;

public final class MainActivity extends Activity {
    private static final int BG = Color.rgb(8,10,14);
    private static final int PANEL = Color.rgb(14,17,23);
    private static final int PANEL2 = Color.rgb(20,24,32);
    private static final int LINE = Color.rgb(45,52,65);
    private static final int TEXT = Color.rgb(232,237,246);
    private static final int MUTED = Color.rgb(145,157,178);
    private static final int ACCENT = Color.rgb(104,168,255);
    private static final int GOOD = Color.rgb(117,214,152);
    private static final int BAD = Color.rgb(255,109,125);
    private static final int CAP_DOMAIN_ROOT = 0x010A0000;

    private static volatile boolean nativeLoaded;
    private static volatile String nativeLoadError = "";
    static {
        try { System.loadLibrary("odpar_studio"); nativeLoaded = true; }
        catch (Throwable t) { nativeLoadError = t.getClass().getSimpleName()+": "+String.valueOf(t.getMessage()); }
    }

    private native String nativeBootstrap();
    private native String nativeSchemaManifest();
    private native String nativeProjectSnapshot();
    private native String nativeInspector(long objectId);
    private native long nativeAddObject(int capabilityId, long parentId);
    private native boolean nativeDeleteObject(long objectId);
    private native boolean nativeSetObjectFlags(long objectId, int flags);
    private native boolean nativeSetValueLane(long objectId, int parameterId, int lane, long rawValue);
    private native boolean nativeEnterWorld();
    private native boolean nativeEnterDomain(int domain, int contextMask);
    private native boolean nativeSelect(long objectId);
    private native boolean nativeNavigate(double panX, double panY, double dolly);
    private native long nativeTimelinePixelToSample(long pixelQ16);
    private native boolean nativeTimelineResize(int width);
    private native boolean nativeTimelineZoom(long anchorPixelQ16, long newSpanSamples);
    private native boolean nativeTimelinePan(long deltaSamples);
    private native boolean nativeMeshPrimitive(int kind);
    private native boolean nativeMeshExtrudeFace(long faceId, double meters);
    private native boolean nativeMeshInsetFace(long faceId, double remainingScale);
    private native boolean nativeMeshSetDimension(int axis, double meters, int anchor);
    private native int[] nativeRenderViewport(int width, int height);
    private native String nativeLastError();
    private native boolean nativeResetProject();

    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private final Handler ui = new Handler(Looper.getMainLooper());
    private final AtomicInteger renderGeneration = new AtomicInteger();
    private final Map<Integer,JSONObject> capabilityById = new HashMap<>();
    private JSONObject schema;
    private JSONObject project;
    private long selectedObject;
    private int activeDomain;
    private long activeDomainRoot;

    private LinearLayout root, body, sceneList, inspector, toolShelf;
    private ScrollView sceneScroll, inspectorScroll;
    private ImageView viewport;
    private TextView engineBadge, workspaceBadge, titleStatus, viewportStatus;
    private TimelineView timeline;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().setStatusBarColor(BG);
        getWindow().setNavigationBarColor(BG);
        buildUi();
        if (!nativeLoaded) { showFatal("Native loader: "+nativeLoadError); return; }
        worker.submit(() -> {
            try {
                JSONObject boot = new JSONObject(nativeBootstrap());
                JSONObject manifest = new JSONObject(nativeSchemaManifest());
                ui.post(() -> {
                    try {
                        schema = manifest;
                        indexSchema();
                        project = boot;
                        applyProject();
                        engineBadge.setText("ENGINE ONLINE");
                        engineBadge.setTextColor(GOOD);
                        enterWorld();
                    } catch (Throwable t) { showFatal("Bootstrap UI: "+t); }
                });
            } catch (Throwable t) { ui.post(() -> showFatal("Engine bootstrap: "+t+"\n"+nativeLastError())); }
        });
    }

    @Override protected void onDestroy() { worker.shutdownNow(); super.onDestroy(); }

    private void buildUi() {
        root = column(BG);
        setContentView(root);
        root.addView(topBar(), match(dp(58)));
        root.addView(domainBar(), match(dp(52)));

        body = new LinearLayout(this);
        body.setPadding(dp(7),dp(5),dp(7),dp(5));
        root.addView(body,new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,0,1f));

        sceneScroll = new ScrollView(this); sceneScroll.setFillViewport(true);
        sceneList = column(PANEL); sceneList.setPadding(dp(8),dp(7),dp(8),dp(8)); sceneScroll.addView(sceneList);
        inspectorScroll = new ScrollView(this); inspectorScroll.setFillViewport(true);
        inspector = column(PANEL); inspector.setPadding(dp(9),dp(7),dp(9),dp(10)); inspectorScroll.addView(inspector);

        LinearLayout center = column(BG);
        viewport = new ImageView(this); viewport.setScaleType(ImageView.ScaleType.FIT_CENTER); viewport.setBackgroundColor(Color.rgb(5,7,10)); installViewportGestures();
        center.addView(viewport,new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,0,1f));
        viewportStatus = small("WORLD VIEW · EditorView3D · no render-camera authority");
        viewportStatus.setPadding(dp(8),dp(4),dp(8),dp(4)); center.addView(viewportStatus,match(dp(29)));
        HorizontalScrollView tools = new HorizontalScrollView(this); tools.setHorizontalScrollBarEnabled(false);
        toolShelf = new LinearLayout(this); toolShelf.setOrientation(LinearLayout.HORIZONTAL); toolShelf.setGravity(Gravity.CENTER_VERTICAL); tools.addView(toolShelf);
        center.addView(tools,match(dp(47)));

        int sw = getResources().getConfiguration().screenWidthDp;
        if (sw < 700) {
            body.setOrientation(LinearLayout.VERTICAL);
            body.addView(center,new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,0,1f));
            LinearLayout lower = new LinearLayout(this); lower.setOrientation(LinearLayout.HORIZONTAL); lower.setPadding(0,dp(5),0,0);
            lower.addView(sceneScroll,new LinearLayout.LayoutParams(0,ViewGroup.LayoutParams.MATCH_PARENT,.42f));
            lower.addView(inspectorScroll,new LinearLayout.LayoutParams(0,ViewGroup.LayoutParams.MATCH_PARENT,.58f));
            body.addView(lower,new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,dp(220)));
        } else {
            body.setOrientation(LinearLayout.HORIZONTAL);
            body.addView(sceneScroll,new LinearLayout.LayoutParams(dp(235),ViewGroup.LayoutParams.MATCH_PARENT));
            body.addView(center,new LinearLayout.LayoutParams(0,ViewGroup.LayoutParams.MATCH_PARENT,1f));
            body.addView(inspectorScroll,new LinearLayout.LayoutParams(dp(330),ViewGroup.LayoutParams.MATCH_PARENT));
        }

        timeline = new TimelineView(); root.addView(timeline,match(dp(120)));
        rebuildTools(); rebuildScene(); rebuildInspector();
    }

    private View topBar() {
        LinearLayout bar = row(PANEL); bar.setPadding(dp(13),dp(5),dp(8),dp(5)); bar.setGravity(Gravity.CENTER_VERTICAL);
        LinearLayout names = column(Color.TRANSPARENT);
        TextView title = text("ODPAR  MUSIC MOTION",17,TEXT,true); names.addView(title);
        titleStatus = small("Studio · engine-driven shared authoring world"); names.addView(titleStatus);
        bar.addView(names,new LinearLayout.LayoutParams(0,ViewGroup.LayoutParams.WRAP_CONTENT,1f));
        workspaceBadge=badge("WORLD"); bar.addView(workspaceBadge);
        engineBadge=badge(nativeLoaded?"ENGINE…":"NATIVE ERROR"); engineBadge.setTextColor(nativeLoaded?MUTED:BAD); bar.addView(engineBadge);
        Button add=button("＋ ADD"); add.setOnClickListener(v->showCapabilityLibrary()); bar.addView(add);
        return bar;
    }

    private View domainBar() {
        HorizontalScrollView h = new HorizontalScrollView(this); h.setHorizontalScrollBarEnabled(false); h.setBackgroundColor(Color.rgb(11,14,19));
        LinearLayout r = row(Color.TRANSPARENT); r.setPadding(dp(7),dp(5),dp(7),dp(5));
        r.addView(domainButton("WORLD",0));
        String[] n={"CAMERA","MODELS","ENVIRONMENT","COMPOSITION","CORE / MEDIA","PARTICLES","LIGHTING","OVERLAY 2D","AUDIO / REACTION","POST","OUTPUT","ASSETS"};
        for(int i=0;i<n.length;i++) r.addView(domainButton(n[i],i+1));
        h.addView(r); return h;
    }

    private Button domainButton(String label,int domain) { Button b=button(label); b.setOnClickListener(v->{ if(domain==0) enterWorld(); else enterDomain(domain); }); return b; }

    private void indexSchema() throws Exception {
        capabilityById.clear(); JSONArray a=schema.getJSONArray("capabilities");
        for(int i=0;i<a.length();i++){JSONObject c=a.getJSONObject(i);capabilityById.put(c.getInt("id"),c);}
    }

    private void applyProject() throws Exception {
        JSONObject e=project.getJSONObject("engine");
        titleStatus.setText("v"+e.optString("version","?")+" · schema "+e.optInt("schema_major")+"."+e.optInt("schema_minor")+" · "+e.optInt("capabilities")+" capabilities · "+e.optInt("parameters")+" parameters");
        JSONObject s=project.optJSONObject("session"); if(s!=null) selectedObject=s.optLong("selected",selectedObject);
        rebuildScene(); rebuildInspector(); rebuildTools(); if(timeline!=null) timeline.invalidate();
    }

    private void refresh(boolean render) {
        worker.submit(()->{String snap=nativeProjectSnapshot();ui.post(()->{try{project=new JSONObject(snap);applyProject();}catch(Throwable t){toast(t.toString());}if(render)requestRender();});});
    }

    private void enterWorld() {
        worker.submit(()->{boolean ok=nativeEnterWorld();String snap=nativeProjectSnapshot();ui.post(()->{if(!ok){toast(nativeLastError());return;}activeDomain=0;activeDomainRoot=0;workspaceBadge.setText("WORLD");viewportStatus.setText("WORLD VIEW · EditorView3D · navigation does not mutate project camera");try{project=new JSONObject(snap);applyProject();}catch(Throwable ignored){}requestRender();});});
    }

    private void enterDomain(int d) {
        int context=0x0fff & ~(1<<(d-1));
        worker.submit(()->{boolean ok=nativeEnterDomain(d,context);String snap=nativeProjectSnapshot();ui.post(()->{if(!ok){toast(nativeLastError());return;}activeDomain=d;activeDomainRoot=d*100L;workspaceBadge.setText(domainName(d).toUpperCase(Locale.ROOT));viewportStatus.setText(domainName(d)+" · editable authority; other visible domains are context");try{project=new JSONObject(snap);applyProject();}catch(Throwable ignored){}requestRender();});});
    }

    private void rebuildScene() {
        if(sceneList==null)return;sceneList.removeAllViews();sceneList.addView(section("SCENE NAVIGATOR"));
        sceneList.addView(small(activeDomain==0?"Shared world · stable IDs":"Domain authority + read-only context"));
        if(project==null){sceneList.addView(small("Waiting for engine…"));return;}
        JSONArray a=project.optJSONArray("objects");if(a==null)return;
        for(int i=0;i<a.length();i++){JSONObject o=a.optJSONObject(i);if(o==null||o.optInt("capability")==CAP_DOMAIN_ROOT)continue;long id=o.optLong("id");String label=o.optString("label","Object");Button b=button((id==selectedObject?"● ":"   ")+label+"  #"+id);b.setGravity(Gravity.START|Gravity.CENTER_VERTICAL);b.setTextColor(id==selectedObject?ACCENT:TEXT);b.setOnClickListener(v->select(id));sceneList.addView(b,match(dp(40)));}
        Space sp=new Space(this);sceneList.addView(sp,match(dp(8)));Button add=button("＋ Add capability");add.setOnClickListener(v->showCapabilityLibrary());sceneList.addView(add,match(dp(42)));
    }

    private void select(long id) {
        worker.submit(()->{boolean ok=nativeSelect(id);String snap=nativeProjectSnapshot();ui.post(()->{if(!ok){toast(nativeLastError());return;}selectedObject=id;try{project=new JSONObject(snap);applyProject();}catch(Throwable ignored){}});});
    }

    private void rebuildInspector() {
        if(inspector==null)return;inspector.removeAllViews();inspector.addView(section("INSPECTOR"));
        if(selectedObject==0){inspector.addView(paragraph("Select an authored object. Inspector controls come from the engine Presentation Schema, not from an Android-side duplicate catalog."));return;}
        long id=selectedObject;inspector.addView(small("Stable object #"+id));
        worker.submit(()->{String j=nativeInspector(id);ui.post(()->{if(id!=selectedObject)return;try{populateInspector(new JSONObject(j));}catch(Throwable t){inspector.addView(paragraph("Inspector error: "+t));}});});
    }

    private void populateInspector(JSONObject d) throws Exception {
        inspector.removeAllViews();inspector.addView(section("INSPECTOR"));long id=d.getLong("object");int flags=d.optInt("flags",1);JSONObject cap=capabilityById.get(d.getInt("capability"));
        TextView n=text((cap==null?"Object":cap.optString("label"))+"  #"+id,15,TEXT,true);inspector.addView(n);if(cap!=null)inspector.addView(small(cap.optString("group")+" · "+cap.optString("key")));
        LinearLayout acts=row(Color.TRANSPARENT);acts.setPadding(0,dp(6),0,dp(7));Button en=button((flags&1)!=0?"ENABLED":"DISABLED");en.setOnClickListener(v->setFlags(id,flags^1));acts.addView(en,new LinearLayout.LayoutParams(0,dp(40),1f));Button lock=button((flags&2)!=0?"LOCKED":"UNLOCKED");lock.setOnClickListener(v->setFlags(id,flags^2));acts.addView(lock,new LinearLayout.LayoutParams(0,dp(40),1f));Button del=button("DELETE");del.setTextColor(BAD);del.setOnClickListener(v->delete(id));acts.addView(del,new LinearLayout.LayoutParams(0,dp(40),1f));inspector.addView(acts);
        JSONArray ps=d.getJSONArray("parameters");String group="";for(int i=0;i<ps.length();i++){JSONObject p=ps.getJSONObject(i);String g=p.optString("group","Parameters");if(!g.equals(group)){TextView gh=small(g.toUpperCase(Locale.ROOT));gh.setTypeface(Typeface.DEFAULT_BOLD);gh.setPadding(0,dp(10),0,dp(4));inspector.addView(gh);group=g;}inspector.addView(parameterEditor(id,p));}
    }

    private View parameterEditor(long objectId,JSONObject p) throws Exception {
        LinearLayout card=column(PANEL2);card.setPadding(dp(8),dp(7),dp(8),dp(7));LinearLayout.LayoutParams cp=match(ViewGroup.LayoutParams.WRAP_CONTENT);cp.setMargins(0,0,0,dp(5));card.setLayoutParams(cp);
        card.addView(text(p.getString("label"),13,TEXT,true));card.addView(small(p.optString("key")+(p.optString("unit").isEmpty()?"":" · "+p.optString("unit"))));
        int type=p.getInt("type"),pid=p.getInt("id"),flags=p.optInt("flags");boolean ro=(flags&8)!=0;JSONArray lanes=p.getJSONArray("lanes"),options=p.optJSONArray("options");
        if(type==1&&lanes.length()==1){long cur=lanes.getLong(0);Button b=button(cur!=0?"TRUE":"FALSE");b.setEnabled(!ro);b.setOnClickListener(v->setLane(objectId,pid,0,cur==0?1:0));card.addView(b,match(dp(40)));return card;}
        if(type==11&&options!=null&&options.length()>0){long cur=lanes.getLong(0);Button b=button(optionLabel(options,cur));b.setEnabled(!ro);b.setOnClickListener(v->enumPicker(objectId,pid,options));card.addView(b,match(dp(40)));return card;}
        for(int lane=0;lane<lanes.length();lane++){final int li=lane;LinearLayout r=row(Color.TRANSPARENT);TextView axis=small(axisLabel(lane,lanes.length()));axis.setGravity(Gravity.CENTER);r.addView(axis,new LinearLayout.LayoutParams(dp(27),dp(42)));EditText e=numeric(formatValue(type,lanes.getLong(lane)));e.setEnabled(!ro);r.addView(e,new LinearLayout.LayoutParams(0,dp(42),1f));Button set=button("SET");set.setEnabled(!ro);set.setOnClickListener(v->{try{setLane(objectId,pid,li,parseValue(type,e.getText().toString()));}catch(Throwable t){toast("Invalid value");}});r.addView(set,new LinearLayout.LayoutParams(dp(62),dp(40)));card.addView(r);}
        return card;
    }

    private void setLane(long o,int p,int lane,long raw){worker.submit(()->{boolean ok=nativeSetValueLane(o,p,lane,raw);ui.post(()->{if(!ok)toast(nativeLastError());refresh(true);});});}
    private void setFlags(long id,int flags){worker.submit(()->{boolean ok=nativeSetObjectFlags(id,flags);ui.post(()->{if(!ok)toast(nativeLastError());refresh(true);});});}
    private void delete(long id){new AlertDialog.Builder(this).setTitle("Delete #"+id).setMessage("Delete this object and descendants?").setNegativeButton("Cancel",null).setPositiveButton("Delete",(d,w)->worker.submit(()->{boolean ok=nativeDeleteObject(id);ui.post(()->{if(!ok)toast(nativeLastError());else selectedObject=0;refresh(true);});})).show();}

    private void showCapabilityLibrary() {
        if(schema==null){toast("Schema not loaded");return;}List<JSONObject> list=new ArrayList<>();for(JSONObject c:capabilityById.values())if(c.optInt("id")!=CAP_DOMAIN_ROOT)list.add(c);list.sort(Comparator.comparing((JSONObject c)->c.optString("group")).thenComparing(c->c.optString("label")));String[] labels=new String[list.size()];for(int i=0;i<list.size();i++)labels[i]=list.get(i).optString("group")+"  ›  "+list.get(i).optString("label");
        new AlertDialog.Builder(this).setTitle("Engine Capability Library").setMessage("Generated live from odm_presentation_schema_manifest_json().").setItems(labels,(d,w)->{JSONObject c=list.get(w);int cap=c.optInt("id");long parent=selectedObject!=0?selectedObject:(activeDomainRoot!=0?activeDomainRoot:200L);worker.submit(()->{long id=nativeAddObject(cap,parent);ui.post(()->{if(id==0){toast(nativeLastError());return;}selectedObject=id;refresh(true);});});}).setNegativeButton("Close",null).show();
    }

    private void rebuildTools() {
        if(toolShelf==null)return;toolShelf.removeAllViews();TextView t=small(activeDomain==0?"WORLD TOOLS":domainName(activeDomain).toUpperCase(Locale.ROOT)+" TOOLS");t.setTypeface(Typeface.DEFAULT_BOLD);toolShelf.addView(t);
        if(activeDomain==0||activeDomain==2){toolShelf.addView(tool("BOX",()->meshPrimitive(1)));toolShelf.addView(tool("CYLINDER",()->meshPrimitive(2)));toolShelf.addView(tool("CONE",()->meshPrimitive(3)));toolShelf.addView(tool("DIMENSIONS",this::dimensionDialog));toolShelf.addView(tool("EXTRUDE",()->faceDialog(true)));toolShelf.addView(tool("INSET",()->faceDialog(false)));}
        Space s=new Space(this);toolShelf.addView(s,new LinearLayout.LayoutParams(dp(10),1));toolShelf.addView(tool("REFRESH",this::requestRender));toolShelf.addView(tool("RESET",this::reset));
    }

    private void meshPrimitive(int kind){worker.submit(()->{boolean ok=nativeMeshPrimitive(kind);ui.post(()->{if(!ok)toast(nativeLastError());refresh(true);});});}
    private void dimensionDialog(){LinearLayout box=column(PANEL);box.setPadding(dp(18),dp(8),dp(18),dp(8));EditText m=numeric("2.0");box.addView(small("Exact target span in metres"));box.addView(m,match(dp(44)));AlertDialog dialog=new AlertDialog.Builder(this).setTitle("Mesh Dimensions").setView(box).setNegativeButton("Cancel",null).create();LinearLayout axes=row(Color.TRANSPARENT);String[] lab={"WIDTH X","HEIGHT Y","DEPTH Z"};for(int i=0;i<3;i++){final int axis=i;Button b=button(lab[i]);b.setOnClickListener(v->{try{double val=Double.parseDouble(m.getText().toString());worker.submit(()->{boolean ok=nativeMeshSetDimension(axis,val,2);ui.post(()->{if(!ok)toast(nativeLastError());refresh(true);});});dialog.dismiss();}catch(Throwable t){toast("Invalid dimension");}});axes.addView(b,new LinearLayout.LayoutParams(0,dp(42),1f));}box.addView(axes);dialog.show();}
    private void faceDialog(boolean extrude){LinearLayout box=column(PANEL);box.setPadding(dp(18),dp(8),dp(18),dp(8));EditText face=numeric("1"),amt=numeric(extrude?"0.25":"0.75");box.addView(small("Stable face ID"));box.addView(face,match(dp(42)));box.addView(small(extrude?"Distance metres":"Remaining scale 0..1"));box.addView(amt,match(dp(42)));new AlertDialog.Builder(this).setTitle(extrude?"Extrude Face":"Inset Face").setView(box).setPositiveButton("Apply",(d,w)->{try{long f=Long.parseLong(face.getText().toString());double a=Double.parseDouble(amt.getText().toString());worker.submit(()->{boolean ok=extrude?nativeMeshExtrudeFace(f,a):nativeMeshInsetFace(f,a);ui.post(()->{if(!ok)toast(nativeLastError());refresh(true);});});}catch(Throwable t){toast("Invalid operation");}}).setNegativeButton("Cancel",null).show();}
    private void reset(){new AlertDialog.Builder(this).setTitle("Reset working project?").setMessage("Rebuild neutral 12-domain project + editable mesh seed.").setNegativeButton("Cancel",null).setPositiveButton("Reset",(d,w)->worker.submit(()->{boolean ok=nativeResetProject();ui.post(()->{if(!ok)toast(nativeLastError());selectedObject=0;activeDomain=0;refresh(true);});})).show();}

    private void installViewportGestures(){viewport.setOnTouchListener(new View.OnTouchListener(){float lx,ly,span;@Override public boolean onTouch(View v,MotionEvent e){if(!nativeLoaded)return true;if(e.getActionMasked()==MotionEvent.ACTION_DOWN){lx=e.getX();ly=e.getY();return true;}if(e.getActionMasked()==MotionEvent.ACTION_POINTER_DOWN&&e.getPointerCount()>=2){span=span(e);return true;}if(e.getActionMasked()==MotionEvent.ACTION_MOVE){if(e.getPointerCount()>=2){float s=span(e);double dz=-(s-span)/Math.max(1f,v.getWidth())*5.0;span=s;worker.submit(()->nativeNavigate(0,0,dz));}else{float dx=e.getX()-lx,dy=e.getY()-ly;lx=e.getX();ly=e.getY();double px=-dx/Math.max(1.0,v.getWidth())*3.0,py=dy/Math.max(1.0,v.getHeight())*3.0;worker.submit(()->nativeNavigate(px,py,0));}requestRender();return true;}if(e.getActionMasked()==MotionEvent.ACTION_UP||e.getActionMasked()==MotionEvent.ACTION_CANCEL){requestRender();return true;}return true;}float span(MotionEvent e){float dx=e.getX(0)-e.getX(1),dy=e.getY(0)-e.getY(1);return(float)Math.sqrt(dx*dx+dy*dy);}});}

    private void requestRender(){if(!nativeLoaded||viewport==null)return;int gen=renderGeneration.incrementAndGet();int w=viewport.getWidth(),h=viewport.getHeight();if(w<64||h<64){ui.postDelayed(this::requestRender,80);return;}int max=720;double scale=Math.min(1.0,max/(double)Math.max(w,h));int rw=Math.max(64,(int)(w*scale)),rh=Math.max(64,(int)(h*scale));worker.submit(()->{int[] pix=nativeRenderViewport(rw,rh);String status=nativeLastError();if(pix==null||gen!=renderGeneration.get())return;Bitmap bmp=Bitmap.createBitmap(pix,rw,rh,Bitmap.Config.ARGB_8888);ui.post(()->{if(gen!=renderGeneration.get())return;viewport.setImageBitmap(bmp);viewportStatus.setText((activeDomain==0?"WORLD":"DOMAIN")+" VIEW · "+status);});});}

    private final class TimelineView extends View {
        final Paint p=new Paint(Paint.ANTI_ALIAS_FLAG);
        TimelineView(){super(MainActivity.this);setBackgroundColor(Color.rgb(10,13,18));setOnTouchListener((v,e)->touch(e));}
        @Override protected void onSizeChanged(int w,int h,int ow,int oh){super.onSizeChanged(w,h,ow,oh);if(nativeLoaded&&w>0)worker.submit(()->nativeTimelineResize(w));}
        @Override protected void onDraw(Canvas c){super.onDraw(c);int w=getWidth(),h=getHeight();p.setColor(LINE);p.setStrokeWidth(1f);for(int x=0;x<w;x+=Math.max(1,w/12))c.drawLine(x,0,x,h,p);p.setColor(MUTED);p.setTextSize(dp(10));c.drawText("TIMELINE · canonical 48 kHz sample time",dp(9),dp(17),p);if(project!=null){JSONObject t=project.optJSONObject("timeline");if(t!=null){long sample=t.optLong("sample"),dur=t.optLong("duration");float x=dur>0?(float)(Math.max(0,Math.min(1,sample/(double)dur))*w):0;p.setColor(ACCENT);p.setStrokeWidth(dp(2));c.drawLine(x,dp(22),x,h,p);p.setColor(TEXT);p.setTextSize(dp(11));c.drawText("sample "+sample+" · "+String.format(Locale.ROOT,"%.3fs",sample/48000.0),dp(9),h-dp(9),p);}}}
        boolean touch(MotionEvent e){if(!nativeLoaded)return true;if(e.getActionMasked()==MotionEvent.ACTION_DOWN||e.getActionMasked()==MotionEvent.ACTION_MOVE){long px=(long)(Math.max(0,Math.min(getWidth(),e.getX()))*65536.0);worker.submit(()->{nativeTimelinePixelToSample(px);String snap=nativeProjectSnapshot();ui.post(()->{try{project=new JSONObject(snap);}catch(Throwable ignored){}invalidate();});});return true;}return true;}
    }

    private void enumPicker(long obj,int pid,JSONArray options){String[] labels=new String[options.length()];long[] vals=new long[options.length()];for(int i=0;i<options.length();i++){JSONObject o=options.optJSONObject(i);labels[i]=o==null?"?":o.optString("label");vals[i]=o==null?0:o.optLong("value");}new AlertDialog.Builder(this).setTitle("Choose value").setItems(labels,(d,w)->setLane(obj,pid,0,vals[w])).show();}
    private String optionLabel(JSONArray a,long value){for(int i=0;i<a.length();i++){JSONObject o=a.optJSONObject(i);if(o!=null&&o.optLong("value")==value)return o.optString("label");}return Long.toString(value);}
    private String formatValue(int type,long raw){switch(type){case 4:return String.format(Locale.ROOT,"%.7f",raw/2147483648.0);case 5:case 7:case 8:return String.format(Locale.ROOT,"%.6f",raw/4294967296.0);case 6:return String.format(Locale.ROOT,"%.3f",(raw&0xffffffffL)*360.0/4294967296.0);case 9:case 12:return String.format(Locale.ROOT,"%.5f",raw/65535.0);default:return Long.toString(raw);}}
    private long parseValue(int type,String text){double v=Double.parseDouble(text.trim());switch(type){case 4:return Math.round(v*2147483648.0);case 5:case 7:case 8:return Math.round(v*4294967296.0);case 6:return Math.round(v/360.0*4294967296.0)&0xffffffffL;case 9:case 12:return Math.round(v*65535.0);default:return Long.parseLong(text.trim());}}
    private String axisLabel(int i,int n){return n==1?"":new String[]{"X","Y","Z","W"}[Math.min(i,3)];}
    private String domainName(int d){String[] a={"World","Camera","Models3D","Environment","Composition","Core / Media","Particles","Lighting","Overlay2D","Audio / Reaction","Post","Output","Assets"};return d>=0&&d<a.length?a[d]:"Domain";}

    private void showFatal(String s){engineBadge.setText("ENGINE ERROR");engineBadge.setTextColor(BAD);viewportStatus.setText(s);inspector.removeAllViews();TextView t=paragraph(s);t.setTextColor(BAD);inspector.addView(t);}
    private void toast(String s){Toast.makeText(this,s,Toast.LENGTH_LONG).show();}
    private int dp(int v){return(int)(v*getResources().getDisplayMetrics().density+.5f);}
    private LinearLayout.LayoutParams match(int h){return new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,h);}
    private LinearLayout column(int color){LinearLayout l=new LinearLayout(this);l.setOrientation(LinearLayout.VERTICAL);l.setBackgroundColor(color);return l;}
    private LinearLayout row(int color){LinearLayout l=new LinearLayout(this);l.setOrientation(LinearLayout.HORIZONTAL);l.setGravity(Gravity.CENTER_VERTICAL);l.setBackgroundColor(color);return l;}
    private TextView text(String s,int size,int color,boolean bold){TextView t=new TextView(this);t.setText(s);t.setTextSize(size);t.setTextColor(color);if(bold)t.setTypeface(Typeface.DEFAULT_BOLD);return t;}
    private TextView small(String s){return text(s,11,MUTED,false);}
    private TextView paragraph(String s){TextView t=text(s,13,MUTED,false);t.setLineSpacing(0,1.18f);t.setPadding(0,dp(7),0,dp(7));return t;}
    private TextView section(String s){TextView t=text(s,12,TEXT,true);t.setLetterSpacing(.07f);t.setPadding(0,dp(3),0,dp(6));return t;}
    private TextView badge(String s){TextView t=text(s,10,MUTED,true);t.setGravity(Gravity.CENTER);t.setPadding(dp(9),0,dp(9),0);t.setBackgroundColor(PANEL2);LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT,dp(35));lp.setMargins(dp(5),0,0,0);t.setLayoutParams(lp);return t;}
    private Button button(String s){Button b=new Button(this);b.setText(s);b.setTextSize(10);b.setTextColor(TEXT);b.setAllCaps(false);b.setMinWidth(0);b.setMinimumWidth(0);b.setPadding(dp(8),0,dp(8),0);b.setBackgroundColor(PANEL2);return b;}
    private Button tool(String s,Runnable r){Button b=button(s);b.setOnClickListener(v->r.run());LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT,dp(39));lp.setMargins(dp(4),0,0,0);b.setLayoutParams(lp);return b;}
    private EditText numeric(String s){EditText e=new EditText(this);e.setText(s);e.setTextColor(TEXT);e.setTextSize(13);e.setSingleLine(true);e.setInputType(InputType.TYPE_CLASS_NUMBER|InputType.TYPE_NUMBER_FLAG_DECIMAL|InputType.TYPE_NUMBER_FLAG_SIGNED);e.setBackgroundColor(Color.rgb(10,13,18));e.setPadding(dp(7),0,dp(7),0);return e;}
}

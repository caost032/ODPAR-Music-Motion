package com.odpar.musicmotionlab;

import android.app.Activity;
import android.graphics.Bitmap;
import android.graphics.Color;
import android.graphics.Typeface;
import android.os.Bundle;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;

public final class MainActivity extends Activity {
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private final AtomicBoolean rendering = new AtomicBoolean(false);
    private ImageView viewport;
    private TextView telemetry;
    private TextView console;
    private EditText search;
    private String fullSpine = "";
    private int panXmm = 0, panYmm = 0, zoomMm = 0;
    private float lastX, lastY;
    private final int bg = Color.rgb(8,10,14);
    private final int panel = Color.rgb(18,22,29);
    private final int fg = Color.rgb(230,235,242);
    private final int muted = Color.rgb(145,158,171);
    private final int accent = Color.rgb(144,202,249);

    @Override public void onCreate(Bundle b) {
        super.onCreate(b);
        getWindow().setStatusBarColor(bg); getWindow().setNavigationBarColor(bg);
        setContentView(buildUi());
        console.setText(NativeBridge.engineStatus());
        renderScene();
    }

    private TextView text(String s, int sp, int color) {
        TextView v = new TextView(this); v.setText(s); v.setTextSize(sp); v.setTextColor(color);
        v.setPadding(12,8,12,8); return v;
    }
    private Button button(String s) {
        Button b = new Button(this); b.setText(s); b.setAllCaps(false); b.setTextColor(fg); b.setBackgroundColor(panel);
        return b;
    }
    private LinearLayout row() { LinearLayout r=new LinearLayout(this); r.setOrientation(LinearLayout.HORIZONTAL); r.setGravity(Gravity.CENTER_VERTICAL); return r; }
    private void addWeighted(LinearLayout row, View v) { row.addView(v,new LinearLayout.LayoutParams(0,ViewGroup.LayoutParams.WRAP_CONTENT,1f)); }

    private View buildUi() {
        LinearLayout root = new LinearLayout(this); root.setOrientation(LinearLayout.HORIZONTAL); root.setBackgroundColor(bg);
        LinearLayout left = new LinearLayout(this); left.setOrientation(LinearLayout.VERTICAL); left.setPadding(14,10,14,10);
        LinearLayout right = new LinearLayout(this); right.setOrientation(LinearLayout.VERTICAL); right.setPadding(12,10,12,10);
        TextView title=text("ODPAR : MUSIC MOTION  /  ENGINE LAB",18,fg); title.setTypeface(Typeface.DEFAULT_BOLD);
        TextView sub=text("Native C11 • ABI 11 • official 0.52 source",12,muted);
        left.addView(title); left.addView(sub);

        viewport=new ImageView(this); viewport.setBackgroundColor(Color.BLACK); viewport.setScaleType(ImageView.ScaleType.FIT_CENTER);
        viewport.setAdjustViewBounds(true);
        left.addView(viewport,new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,0,1f));
        telemetry=text("rendering…",12,muted); telemetry.setTypeface(Typeface.MONOSPACE); left.addView(telemetry);
        left.addView(text("Drag viewport = camera X/Y   •   Zoom +/- = camera Z",11,muted));
        LinearLayout camera=row();
        Button zin=button("Zoom +"); zin.setOnClickListener(v->{zoomMm+=250; renderScene();});
        Button zout=button("Zoom −"); zout.setOnClickListener(v->{zoomMm-=250; renderScene();});
        Button reset=button("Reset camera"); reset.setOnClickListener(v->{panXmm=panYmm=zoomMm=0; renderScene();});
        addWeighted(camera,zin); addWeighted(camera,zout); addWeighted(camera,reset); left.addView(camera);
        viewport.setOnTouchListener((v,e)->{
            if(e.getAction()==MotionEvent.ACTION_DOWN){lastX=e.getX();lastY=e.getY();return true;}
            if(e.getAction()==MotionEvent.ACTION_MOVE){float dx=e.getX()-lastX,dy=e.getY()-lastY;lastX=e.getX();lastY=e.getY();panXmm+=(int)(dx*7f);panYmm-=(int)(dy*7f);renderScene();return true;}
            return true;
        });

        TextView lab=text("ENGINE AUTHORITY",14,accent); lab.setTypeface(Typeface.DEFAULT_BOLD); right.addView(lab);
        LinearLayout actions=row();
        Button status=button("Status"); status.setOnClickListener(v->console.setText(NativeBridge.engineStatus()));
        Button self=button("Selftest"); self.setOnClickListener(v->runTextTask("Selftest", NativeBridge::selftest));
        Button sum=button("Spine summary"); sum.setOnClickListener(v->runTextTask("Spine", NativeBridge::spineSummary));
        addWeighted(actions,status);addWeighted(actions,self);addWeighted(actions,sum);right.addView(actions);
        Button full=button("Load full Spine / 392 capabilities"); full.setOnClickListener(v->runTextTask("Full Spine",()->{fullSpine=NativeBridge.spineFull();return fullSpine;})); right.addView(full);
        search=new EditText(this); search.setHint("Search Spine JSON: camera, mesh, reaction3d…"); search.setTextColor(fg); search.setHintTextColor(muted); search.setSingleLine(true); right.addView(search);
        Button find=button("Filter loaded Spine"); find.setOnClickListener(v->filterSpine()); right.addView(find);
        console=text("",11,fg); console.setTypeface(Typeface.MONOSPACE); console.setTextIsSelectable(true);
        ScrollView scroll=new ScrollView(this); scroll.setBackgroundColor(panel); scroll.addView(console); right.addView(scroll,new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,0,1f));
        right.addView(text("The image at left is generated by ODPAR Scene3D in native C. Android only displays the returned RGBA framebuffer.",11,muted));
        root.addView(left,new LinearLayout.LayoutParams(0,ViewGroup.LayoutParams.MATCH_PARENT,1.15f));
        root.addView(right,new LinearLayout.LayoutParams(0,ViewGroup.LayoutParams.MATCH_PARENT,0.85f));
        return root;
    }

    private void runTextTask(String label, Producer p) {
        console.setText(label+"…"); worker.execute(()->{String s; try{s=p.get();}catch(Throwable t){s=t.toString();} final String out=s;runOnUiThread(()->console.setText(out));});
    }
    private interface Producer { String get(); }
    private void filterSpine(){
        if(fullSpine.isEmpty()){console.setText("Load the full Spine first.");return;}
        String q=search.getText().toString().trim().toLowerCase(); if(q.isEmpty()){console.setText(fullSpine);return;}
        StringBuilder b=new StringBuilder(); for(String line:fullSpine.replace("},{","},\n{").split("\n")){if(line.toLowerCase().contains(q))b.append(line).append('\n');}
        console.setText(b.length()==0?"No match in loaded Spine":b.toString());
    }
    private void renderScene(){
        if(!rendering.compareAndSet(false,true)) return;
        final int x=panXmm,y=panYmm,z=zoomMm; telemetry.setText("ODPAR Scene3D rendering… camera mm = "+x+", "+y+", "+z);
        worker.execute(()->{
            try{
                int w=480,h=360; int[] pixels=NativeBridge.renderDemo(w,h,x,y,z); String meta=NativeBridge.lastRenderMeta();
                Bitmap bmp=null; if(pixels!=null&&pixels.length==w*h){bmp=Bitmap.createBitmap(pixels,w,h,Bitmap.Config.ARGB_8888);} final Bitmap fb=bmp; final String fm=meta;
                runOnUiThread(()->{if(fb!=null)viewport.setImageBitmap(fb);telemetry.setText(fm);});
            } finally { rendering.set(false); if(x!=panXmm||y!=panYmm||z!=zoomMm) runOnUiThread(this::renderScene); }
        });
    }
    @Override protected void onDestroy(){worker.shutdownNow();super.onDestroy();}
}

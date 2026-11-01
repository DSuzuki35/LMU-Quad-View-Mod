/* global FoveationMapping, FoveationShaders */
(() => {
  'use strict';
  const $=id=>document.getElementById(id), canvas=$('view'), M=FoveationMapping, S=FoveationShaders;
  const state={width:768,height:576,scale:.55,strength:4,focusFraction:.3,gaze:[0,0],camera:0,work:8,ids:false,density:false,filter:true};
  let gl,programs,targets={},busy=false,lastFrame=0,report={};
  const status=text=>$('status').textContent=text;
  try {
    gl=canvas.getContext('webgl2',{antialias:false,depth:false,stencil:false,preserveDrawingBuffer:true});
    if(!gl)throw new Error('WebGL2 is unavailable. Try a browser with hardware acceleration enabled.');
    const debug=gl.getExtension('WEBGL_debug_renderer_info');
    const renderer=debug?gl.getParameter(debug.UNMASKED_RENDERER_WEBGL):gl.getParameter(gl.RENDERER);
    const software=/swiftshader|llvmpipe|software|basic render/i.test(renderer);
    report.device={renderer,software,version:gl.getParameter(gl.VERSION),maxTextureSize:gl.getParameter(gl.MAX_TEXTURE_SIZE)};
    $('device').textContent=`Renderer: ${renderer}\n${software?'SOFTWARE renderer: timings are not hardware performance.':'Reported renderer shown above; timings apply only to this fixture.'}`;
    function compile(type,source) {
      const shader=gl.createShader(type);gl.shaderSource(shader,source);gl.compileShader(shader);
      if(!gl.getShaderParameter(shader,gl.COMPILE_STATUS))throw new Error(gl.getShaderInfoLog(shader));
      return shader;
    }
    function program(body) {
      const result=gl.createProgram(),vs=compile(gl.VERTEX_SHADER,S.vertex);
      const fs=compile(gl.FRAGMENT_SHADER,'#version 300 es\nprecision highp float;\nin vec2 vUv;\nout vec4 outColor;\n'+S.mapping+body);
      gl.attachShader(result,vs);gl.attachShader(result,fs);gl.linkProgram(result);
      gl.deleteShader(vs);gl.deleteShader(fs);
      if(!gl.getProgramParameter(result,gl.LINK_STATUS))throw new Error(gl.getProgramInfoLog(result));
      return {handle:result,locations:new Map()};
    }
    programs={scene:program(S.scene),reconstruct:program(S.reconstruct),compose:program(S.compose),present:program(S.present),mappingProbe:program(S.mappingProbe)};
    gl.bindVertexArray(gl.createVertexArray());
    gl.disable(gl.DITHER);gl.disable(gl.BLEND);gl.disable(gl.DEPTH_TEST);
    function uniform(p,name,value) {
      if(!p.locations.has(name))p.locations.set(name,gl.getUniformLocation(p.handle,name));
      const loc=p.locations.get(name);if(loc===null)return;
      if(Array.isArray(value))gl.uniform2fv(loc,value);
      else if(typeof value==='boolean'||['uMode','uSamples','uWork'].includes(name))gl.uniform1i(loc,Number(value));
      else gl.uniform1f(loc,value);
    }
    function target(name,w,h,float=false) {
      const old=targets[name];if(old&&old.width===w&&old.height===h)return old;
      if(old){gl.deleteTexture(old.texture);gl.deleteFramebuffer(old.framebuffer);}
      const texture=gl.createTexture();gl.bindTexture(gl.TEXTURE_2D,texture);
      gl.texImage2D(gl.TEXTURE_2D,0,float?gl.RGBA32F:gl.RGBA8,w,h,0,gl.RGBA,float?gl.FLOAT:gl.UNSIGNED_BYTE,null);
      gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER,float?gl.NEAREST:gl.LINEAR);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MAG_FILTER,float?gl.NEAREST:gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_S,gl.CLAMP_TO_EDGE);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_T,gl.CLAMP_TO_EDGE);
      const framebuffer=gl.createFramebuffer();gl.bindFramebuffer(gl.FRAMEBUFFER,framebuffer);
      gl.framebufferTexture2D(gl.FRAMEBUFFER,gl.COLOR_ATTACHMENT0,gl.TEXTURE_2D,texture,0);
      if(gl.checkFramebufferStatus(gl.FRAMEBUFFER)!==gl.FRAMEBUFFER_COMPLETE)throw new Error('Incomplete render target');
      return targets[name]={texture,framebuffer,width:w,height:h};
    }
    function plan() {return M.budget(state.width,state.height,state.scale,state.strength,state.focusFraction);}
    function focusCenter() {return state.gaze.map(v=>Math.max(-1+state.focusFraction,Math.min(1-state.focusFraction,v)));}
    function prepare() {
      const p=plan();
      if(state.width*2>report.device.maxTextureSize||state.height*2>report.device.maxTextureSize)throw new Error('Requested size exceeds the device limit');
      if(canvas.width!==2*state.width||canvas.height!==2*state.height){canvas.width=2*state.width;canvas.height=2*state.height;}
      for(const name of ['reference','nonlinear','quad','quality'])target(name,state.width,state.height);
      target('source',...p.source);target('outer',...p.outer);target('focus',...p.focus);
      return p;
    }
    function draw(p,destination,values={},textures={},viewport=null) {
      gl.bindFramebuffer(gl.FRAMEBUFFER,destination?destination.framebuffer:null);
      gl.viewport(...(viewport||[0,0,destination.width,destination.height]));gl.useProgram(p.handle);
      for(const [name,value] of Object.entries({uGaze:state.gaze,uStrength:state.strength,...values}))uniform(p,name,value);
      let unit=0;
      for(const [name,texture] of Object.entries(textures)) {
        gl.activeTexture(gl.TEXTURE0+unit);gl.bindTexture(gl.TEXTURE_2D,texture.texture);
        gl.uniform1i(gl.getUniformLocation(p.handle,name),unit++);
      }
      gl.drawArrays(gl.TRIANGLES,0,3);
    }
    function scene(destination,mode=0,samples=1) {
      draw(programs.scene,destination,{uOutputSize:[state.width,state.height],uTargetSize:[destination.width,destination.height],
        uFocusCenter:focusCenter(),uFocusFraction:state.focusFraction,uCamera:state.camera,uMode:mode,uSamples:samples,uWork:state.work,uIds:state.ids,uFilter:state.filter});
    }
    function path(name,p) {
      if(name==='reference')scene(targets.reference);
      else if(name==='nonlinear') {
        scene(targets.source,1);draw(programs.reconstruct,targets.nonlinear,{}, {uTexture:targets.source});
      } else if(name==='quad'&&p.feasible) {
        scene(targets.outer);scene(targets.focus,2);
        draw(programs.compose,targets.quad,{uFocusCenter:focusCenter(),uFocusFraction:state.focusFraction},{uOuter:targets.outer,uFocus:targets.focus});
      }
    }
    function present(p) {
      const w=state.width,h=state.height;
      const items=[['reference',0,h],['nonlinear',w,h],[p.feasible?'quad':'reference',0,0],['source',w,0]];
      for(const [name,x,y] of items)draw(programs.present,null,{uCrosshair:name!=='source',uDensity:name==='source'&&state.density,uScale:state.scale},
        {uTexture:targets[name]},[x,y,w,h]);
    }
    function sync() {
      const p=plan(),percent=100*p.warpPixels/(state.width*state.height);
      $('budget').textContent=`${percent.toFixed(1)}%`;
      $('budgetDetail').textContent=`${p.source.join(' × ')} = ${p.warpPixels.toLocaleString()} scene samples + ${state.width*state.height} reconstruction pixels.`;
      $('densityStat').textContent=`${(p.centerScale*100).toFixed(1)}% per dimension`;
      $('densityDetail').textContent=`${(p.centerScale*p.centerScale*100).toFixed(1)}% central sample area density; gaze ${state.gaze.map(v=>v.toFixed(2)).join(', ')}.`;
      $('quadStat').textContent=p.feasible?`${(100*p.quadPixels/p.warpPixels).toFixed(2)}% of nonlinear`: 'Cannot match budget';
      $('quadDetail').textContent=p.feasible?`Outer ${p.outer.join(' × ')} + focus ${p.focus.join(' × ')}; same nominal central density + one composition pass.`:'Focus alone consumes the budget. Reduce focus width/height or nonlinear strength.';
      $('quadLabel').textContent=p.feasible?'Quad-style context + focus':'Quad unavailable — reference shown';
      $('sourceLabel').textContent=state.density?'Scene-sample density (orange = denser)':'Warped source (stretched to fit)';
      for(const [id,value] of Object.entries({strength:state.strength,scale:state.scale,focus:state.focusFraction,work:state.work,gazeX:state.gaze[0],gazeY:state.gaze[1]})) {
        $(id).value=value;$(id+'Value').textContent=id==='scale'||id==='focus'?`${(value*100).toFixed(0)}%`:Number(value).toFixed(id==='work'?0:2);
      }
      $('ids').checked=state.ids;$('density').checked=state.density;$('filter').checked=state.filter;$('resolution').value=state.width;
      report.settings=JSON.parse(JSON.stringify(state));report.budget=p;
    }
    function render() {
      const p=prepare();for(const name of ['reference','nonlinear','quad'])path(name,p);present(p);sync();
      const error=gl.getError();if(error!==gl.NO_ERROR)throw new Error('WebGL error '+error);
    }
    function configure(values) {
      const next={...state,...values,gaze:values.gaze?values.gaze.slice():state.gaze.slice()};
      M.forward([0,0],next.gaze,next.strength);M.budget(next.width,next.height,next.scale,next.strength,next.focusFraction);
      if(next.gaze.some(v=>Math.abs(v)>.8)||!Number.isInteger(next.work)||next.work<0||next.work>64||!Number.isFinite(next.camera))throw new RangeError('Unsupported controls');
      Object.assign(state,next);render();return JSON.parse(JSON.stringify(report.budget));
    }
    function read(destination) {
      const data=new Uint8Array(destination.width*destination.height*4);
      gl.bindFramebuffer(gl.FRAMEBUFFER,destination.framebuffer);gl.readPixels(0,0,destination.width,destination.height,gl.RGBA,gl.UNSIGNED_BYTE,data);
      return data;
    }
    function errorStats(candidate,reference) {
      const groups={center:{sum:0,count:0},periphery:{sum:0,count:0}};
      for(let y=0;y<state.height;y++)for(let x=0;x<state.width;x++) {
        const dx=((x+.5)/state.width*2-1-state.gaze[0])*state.width/state.height;
        const dy=(y+.5)/state.height*2-1-state.gaze[1];
        const group=Math.hypot(dx,dy)<.2?groups.center:groups.periphery;
        const index=(y*state.width+x)*4;
        for(let c=0;c<3;c++){group.sum+=(candidate[index+c]-reference[index+c])**2;group.count++;}
      }
      return Object.fromEntries(Object.entries(groups).map(([key,v])=>[key,{rmse255:Math.sqrt(v.sum/v.count),samples:v.count/3}]));
    }
    function measureQuality() {
      const p=prepare();for(const name of ['reference','nonlinear','quad'])path(name,p);
      scene(targets.quality,0,4);const reference=read(targets.quality);
      const quality={reference:errorStats(read(targets.reference),reference),nonlinear:errorStats(read(targets.nonlinear),reference)};
      if(p.feasible)quality.quad=errorStats(read(targets.quad),reference);
      report.quality=quality;present(p);$('results').textContent=JSON.stringify({linearRgbRmse_0_to_255:quality},null,2);
      status('Color error measured against four samples per output pixel. Lower is better; this is not a perceptual score.');return quality;
    }
    async function benchmark(iterations=12,batch=32) {
      if(!Number.isInteger(iterations)||iterations<4||iterations>100||!Number.isInteger(batch)||batch<1||batch>128)throw new RangeError('Invalid benchmark length');
      const ext=gl.getExtension('EXT_disjoint_timer_query_webgl2');
      if(!ext){report.timings={available:false,reason:'GPU timer queries unavailable'};$('results').textContent=JSON.stringify(report.timings,null,2);return report.timings;}
      const p=prepare(),names=p.feasible?['reference','nonlinear','quad']:['reference','nonlinear'];
      const collected=Object.fromEntries(names.map(n=>[n,[]]));
      // Rotate submission order. The results remain a synthetic workload,
      // not isolated application frame timings or a proof of LMU integration.
      for(let i=0;i<iterations+2;i++) {
        const pending=[];
        for(let j=0;j<names.length;j++) {
          const name=names[(i+j)%names.length],query=gl.createQuery();
          gl.beginQuery(ext.TIME_ELAPSED_EXT,query);
          for(let repeat=0;repeat<batch;repeat++)path(name,p);
          gl.endQuery(ext.TIME_ELAPSED_EXT);pending.push({name,query});
        }
        gl.flush();const start=performance.now();
        try {
          while(pending.some(item=>!gl.getQueryParameter(item.query,gl.QUERY_RESULT_AVAILABLE))) {
            if(performance.now()-start>10000)throw new Error('GPU timer query timed out');
            await new Promise(resolve=>setTimeout(resolve,20));
          }
          if(gl.getParameter(ext.GPU_DISJOINT_EXT))throw new Error('Disjoint GPU clock; discard timing results');
          for(const item of pending)if(i>=2)collected[item.name].push(gl.getQueryParameter(item.query,gl.QUERY_RESULT)/1e6/batch);
        } finally {for(const item of pending)gl.deleteQuery(item.query);}
      }
      const summary={available:true,renderer:report.device.renderer,software:report.device.software,iterations,batch,paths:{}};
      for(const [name,values] of Object.entries(collected)) {
        values.sort((a,b)=>a-b);summary.paths[name]={medianMs:(values[Math.floor((values.length-1)/2)]+values[Math.floor(values.length/2)])/2,minMs:values[0],maxMs:values.at(-1)};
      }
      report.timings=summary;$('results').textContent=JSON.stringify(summary,null,2);present(p);
      status('Measured scene rendering + reconstruction/composition; presentation/readback excluded. Compare only this fixture on the reported renderer.');return summary;
    }
    function runTests() {
      const saved=JSON.parse(JSON.stringify(state)),results=[];
      try {
        for(const gaze of [[0,0],[.8,-.8],[-.8,.8]])for(const ids of [false,true]) {
          configure({width:128,height:96,scale:1,strength:0,gaze,ids});
          const a=read(targets.reference),b=read(targets.nonlinear);let maximum=0;
          for(let i=0;i<a.length;i++)maximum=Math.max(maximum,Math.abs(a[i]-b[i]));
          if(maximum>1)throw new Error('Identity reconstruction differs by '+maximum+' levels');
          results.push({gaze,ids,maxIdentityError:maximum});
        }
        for(const gaze of [[.8,.8],[-.8,-.8],[0,0]]) {
          configure({strength:12,scale:.45,gaze});const image=read(targets.nonlinear);
          let black=0;for(let i=0;i<image.length;i+=4)if(image[i]+image[i+1]+image[i+2]===0)black++;
          if(black>0)throw new Error('Uncovered black pixels in opaque owned scene');
          results.push({gaze,strength:12,uncoveredPixels:black});
        }
        configure({strength:4,scale:.55,gaze:[.1,-.2],camera:0,ids:true});
        const before=read(targets.nonlinear);
        configure({gaze:[-.7,.6],camera:1});
        const moved=read(targets.nonlinear);
        configure({gaze:[.1,-.2],camera:0});
        const after=read(targets.nonlinear);
        let changed=0,returned=0;
        for(let i=0;i<before.length;i++) {if(before[i]!==moved[i])changed++;if(before[i]!==after[i])returned++;}
        if(changed===0||returned!==0)throw new Error('Scripted gaze/camera changes are not deterministic');
        results.push({gazeCameraReturnChangedBytes:returned,movedChangedBytes:changed});
        if(gl.getExtension('EXT_color_buffer_float')) {
          const probe=target('mapping-probe',128,96,true);
          for(const strength of [.1,4,12])for(const gaze of [[0,0],[.8,.8],[-.8,.5]]) {
            configure({strength,gaze});draw(programs.mappingProbe,probe);
            const values=new Float32Array(probe.width*probe.height*4);
            gl.bindFramebuffer(gl.FRAMEBUFFER,probe.framebuffer);gl.readPixels(0,0,probe.width,probe.height,gl.RGBA,gl.FLOAT,values);
            let maxRoundtrip=0,maxInverse=0;
            for(let y=0;y<probe.height;y++)for(let x=0;x<probe.width;x++) {
              const i=(y*probe.width+x)*4,p=[(x+.5)/probe.width*2-1,(y+.5)/probe.height*2-1];
              const expected=M.inverse(p,gaze,strength);
              for(let axis=0;axis<2;axis++) {
                if(!Number.isFinite(values[i+axis]))throw new Error('Nonfinite mapping result');
                maxInverse=Math.max(maxInverse,Math.abs(values[i+axis]-expected[axis])*512);
                maxRoundtrip=Math.max(maxRoundtrip,Math.abs(values[i+2+axis])*512);
              }
            }
            if(maxRoundtrip>.1||maxInverse>.1)throw new Error('Mapping error exceeds 0.1 pixels at 1024 resolution');
            results.push({strength,gaze,maxRoundtripPixelsAt1024:maxRoundtrip,maxInversePixelsAt1024:maxInverse});
          }
        } else results.push({mappingProbe:'Skipped: floating-point color attachments unavailable'});
        if(gl.getError()!==gl.NO_ERROR)throw new Error('WebGL error during mapping checks');
        report.tests={passed:true,cases:results};return report.tests;
      } finally {configure(saved);}
    }
    async function action(fn) {
      if(busy)return;busy=true;$('animate').checked=false;
      for(const control of document.querySelectorAll('button,input,select'))control.disabled=true;
      try {return await fn();}catch(error){status(error.message);throw error;}
      finally {busy=false;for(const control of document.querySelectorAll('button,input,select'))control.disabled=false;}
    }
    const changed=()=>{if(!busy)configure({strength:+$('strength').value,scale:+$('scale').value,focusFraction:+$('focus').value,
      width:+$('resolution').value,height:+$('resolution').value*3/4,work:+$('work').value,gaze:[+$('gazeX').value,+$('gazeY').value],ids:$('ids').checked,density:$('density').checked,filter:$('filter').checked});};
    for(const id of ['strength','scale','focus','resolution','work','gazeX','gazeY','ids','density','filter'])$(id).addEventListener('input',changed);
    $('central').onclick=()=>configure({scale:Math.max(.2,Math.min(1,1/M.centerScale(state.strength)))});
    $('center').onclick=()=>configure({gaze:[0,0],camera:0});
    $('quality').onclick=()=>action(measureQuality).catch(()=>{});
    $('benchmark').onclick=()=>action(()=>benchmark()).catch(()=>{});
    $('tests').onclick=()=>action(()=>{$('results').textContent=JSON.stringify(runTests(),null,2);status('Identity and extreme-gaze coverage checks passed.');}).catch(()=>{});
    $('export').onclick=()=>{const link=document.createElement('a');link.download='nonlinear-comparison.png';link.href=canvas.toDataURL('image/png');link.click();};
    canvas.addEventListener('pointermove',event=>{
      if(busy||!$('pointer').checked||$('animate').checked)return;
      const rect=canvas.getBoundingClientRect(),x=((event.clientX-rect.left)/rect.width*2)%1,y=((event.clientY-rect.top)/rect.height*2)%1;
      configure({gaze:[Math.max(-.8,Math.min(.8,x*2-1)),Math.max(-.8,Math.min(.8,1-y*2))]});
    });
    function tick(now) {
      if(!busy&&$('animate').checked&&now-lastFrame>50) {
        const t=now/1000;configure({gaze:[.65*Math.sin(t*.7),.45*Math.cos(t*.91)],camera:t*.3});lastFrame=now;
      }
      requestAnimationFrame(tick);
    }
    window.NonlinearLab={configure,render,measureQuality:()=>action(measureQuality),benchmark:n=>action(()=>benchmark(n)),
      runTests:()=>action(runTests),getReport:()=>JSON.parse(JSON.stringify(report)),getState:()=>JSON.parse(JSON.stringify(state))};
    render();requestAnimationFrame(tick);document.body.dataset.ready='true';status('Ready. Move the pointer to shift gaze; compare the reconstructed image with the reference.');
  } catch(error) {status('Initialization failed: '+error.message);document.body.dataset.error=error.message;console.error(error);}
})();

/* Owned synthetic scene. No game shaders, textures or captures. */
window.FoveationShaders = {
  vertex: `#version 300 es
  precision highp float;
  out vec2 vUv;
  void main() {
    vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));
    vUv=p;gl_Position=vec4(p*2.0-1.0,0,1);
  }`,
  mapping: `
  uniform vec2 uGaze;
  uniform float uStrength;
  float radius(vec2 p) {
    vec2 d=p-uGaze;
    vec2 edge=mix(1.0+uGaze,1.0-uGaze,greaterThanEqual(d,vec2(0)));
    vec2 r=abs(d)/edge;return max(r.x,r.y);
  }
  // GLSL ES 3.00 has no asinh/sinh builtins. Stable in our bounded domain.
  float ash(float x) {return log(x+sqrt(x*x+1.0));}
  float sh(float x) {return (exp(x)-exp(-x))*0.5;}
  vec2 warp(vec2 p,bool inverseMap) {
    float t=radius(p);
    if(uStrength<0.0001||t<0.000001)return p;
    float a=ash(uStrength);
    float q=inverseMap?sh(a*t)/uStrength:ash(uStrength*t)/a;
    return uGaze+(p-uGaze)*(q/t);
  }
  `,
  scene: `
  uniform vec2 uOutputSize;
  uniform vec2 uTargetSize;
  uniform vec2 uFocusCenter;
  uniform float uFocusFraction;
  uniform float uCamera;
  uniform int uMode; // 0 ordinary, 1 inverse-mapped source, 2 cropped focus
  uniform int uSamples; // 1 or four deterministic subpixel samples
  uniform int uWork;
  uniform bool uIds;
  uniform bool uFilter;
  float waveIntegral(float x) {float r=mod(x,2.0);return r<1.0?r:2.0-r;}
  float filteredWave(float x,float width) {
    width=max(width,0.0001);return (waveIntegral(x+width*.5)-waveIntegral(x-width*.5))/width;
  }
  float checker(vec2 p) {
    if(!uFilter)return mod(floor(p.x)+floor(p.y),2.0);
    // Derivatives are measured in the actual source image. They therefore
    // follow the nonlinear sample footprint without shading four extra rays.
    vec2 width=fwidth(p);
    return .5-.5*filteredWave(p.x,width.x)*filteredWave(p.y,width.y);
  }
  struct Hit {float t;vec3 n;vec3 color;int id;};
  void acceptHit(inout Hit hit,float t,vec3 normal,vec3 color,int id,vec3 ray) {
    // Near plane is in ordinary eye space, not radial ray distance.
    if(t>0.0&&-ray.z*t>=0.15&&t<hit.t) {
      hit.t=t;hit.n=normal;hit.color=color;hit.id=id;
    }
  }
  void sphere(inout Hit hit,vec3 origin,vec3 ray,vec3 center,float r,vec3 color,int id) {
    vec3 oc=origin-center;float b=dot(oc,ray),c=dot(oc,oc)-r*r,d=b*b-c;
    if(d<0.0)return;
    float t=-b-sqrt(d);if(t<=0.0)t=-b+sqrt(d);
    acceptHit(hit,t,normalize(origin+t*ray-center),color,id,ray);
  }
  void triangle(inout Hit hit,vec3 origin,vec3 ray,vec3 a,vec3 b,vec3 c,vec3 color,int id) {
    vec3 e1=b-a,e2=c-a,p=cross(ray,e2);float det=dot(e1,p);
    if(abs(det)<0.000001)return;
    vec3 s=origin-a;float u=dot(s,p)/det;
    vec3 q=cross(s,e1);float v=dot(ray,q)/det,t=dot(e2,q)/det;
    if(u<0.0||v<0.0||u+v>1.0)return;
    vec3 normal=normalize(cross(e1,e2));if(dot(normal,ray)>0.0)normal=-normal;
    acceptHit(hit,t,normal,color,id,ray);
  }
  vec3 evaluateScene(vec2 ndc) {
    vec3 origin=vec3(sin(uCamera)*0.32,1.15,4.5+cos(uCamera)*0.08);
    vec3 ray=normalize(vec3(ndc.x*uOutputSize.x/uOutputSize.y*0.72,ndc.y*0.72,-1.0));
    Hit hit=Hit(10000.0,vec3(0,1,0),vec3(0),0);
    if(abs(ray.y)>0.00001) {
      float t=(-0.45-origin.y)/ray.y;vec3 p=origin+t*ray;
      float pattern=checker(p.xz*3.0);
      vec3 color=mix(vec3(0.035,0.045,0.055),vec3(0.30,0.33,0.36),pattern);
      if(abs(p.x)<0.035||abs(abs(p.x)-2.1)<0.025)color=vec3(0.95,0.80,0.25);
      acceptHit(hit,t,vec3(0,1,0),color,1,ray);
    }
    if(abs(ray.z)>0.00001) {
      float t=(-9.0-origin.z)/ray.z;vec3 p=origin+t*ray;
      float pattern=checker(p.xy*6.0);
      if(abs(p.x)<8.0&&p.y>-0.45&&p.y<4.0)
        acceptHit(hit,t,vec3(0,0,1),mix(vec3(.09,.13,.17),vec3(.56,.65,.70),pattern),2,ray);
      // Thin fence bars and their horizontal rail stress peripheral aliasing.
      t=(-3.8-origin.z)/ray.z;p=origin+t*ray;
      if(abs(p.x)<3.5&&p.y>-.45&&p.y<1.9&&
        (abs(fract(p.x*3.0)-.5)<.045||abs(p.y-.7)<.025))
        acceptHit(hit,t,vec3(0,0,1),vec3(.73,.75,.70),3,ray);
    }
    sphere(hit,origin,ray,vec3(-1.35,.25,-1.7),.70,vec3(.055,.28,.72),4);
    sphere(hit,origin,ray,vec3(1.2,.15,-.7),.60,vec3(.70,.085,.04),5);
    triangle(hit,origin,ray,vec3(-2.4,-.3,-5.4),vec3(3.3,.1,-6.8),vec3(-.3,3.3,-5.8),vec3(.07,.42,.30),6);
    // Sloped, near geometry: a simple cockpit-like panel and a crossing edge.
    triangle(hit,origin,ray,vec3(-1.7,-.4,2.7),vec3(1.9,-.35,2.0),vec3(1.2,.3,2.2),vec3(.13,.14,.17),7);
    triangle(hit,origin,ray,vec3(-1.7,-.4,2.7),vec3(1.2,.3,2.2),vec3(-1.5,.25,2.9),vec3(.13,.14,.17),7);
    triangle(hit,origin,ray,vec3(-3,2.2,4.6),vec3(-1.9,.3,2.2),vec3(-2.5,2.8,-2.0),vec3(.5,.18,.07),8);
    if(hit.id==0)return mix(vec3(.10,.18,.29),vec3(.42,.57,.72),clamp(ray.y*.7+.5,0.0,1.0));
    if(uIds)return .15+.75*fract(vec3(.37,.61,.83)*float(hit.id));
    vec3 p=origin+ray*hit.t,light=normalize(vec3(-.6,1,.4));
    float diffuse=max(0.0,dot(hit.n,light));
    float spec=pow(max(0.0,dot(reflect(-light,hit.n),-ray)),64.0);
    float detail=0.0;
    for(int i=0;i<64;i++) {
      if(i>=uWork)break;
      float f=float(i+1);detail+=sin(p.x*f*.7)*cos(p.z*f*.53)/max(1.0,float(uWork));
    }
    return clamp(hit.color*(.23+.77*diffuse)*(1.0+detail*.08)+spec*.65,0.0,1.0);
  }
  void main() {
    vec3 color=vec3(0);int count=uSamples==4?4:1;
    for(int i=0;i<4;i++) {
      if(i>=count)break;
      vec2 offset=count==1?vec2(0):vec2(float(i&1)-.5,float(i>>1)-.5)*.5;
      vec2 p=(vUv+offset/uTargetSize)*2.0-1.0;
      if(uMode==1)p=warp(p,true);
      else if(uMode==2)p=uFocusCenter+p*uFocusFraction;
      color+=evaluateScene(p);
    }
    outColor=vec4(color/float(count),1);
  }
  `,
  reconstruct: `
  uniform sampler2D uTexture;
  void main() {outColor=texture(uTexture,warp(vUv*2.0-1.0,false)*.5+.5);}
  `,
  mappingProbe: `
  void main() {
    vec2 p=vUv*2.0-1.0;
    outColor=vec4(warp(p,true),warp(warp(p,false),true)-p);
  }
  `,
  compose: `
  uniform sampler2D uOuter;
  uniform sampler2D uFocus;
  uniform vec2 uFocusCenter;
  uniform float uFocusFraction;
  void main() {
    vec2 local=(vUv*2.0-1.0-uFocusCenter)/uFocusFraction;
    // Hard boundary isolates sampling cost; no hidden blend/guard-band budget.
    outColor=all(lessThanEqual(abs(local),vec2(1)))?texture(uFocus,local*.5+.5):texture(uOuter,vUv);
  }
  `,
  present: `
  uniform sampler2D uTexture;
  uniform bool uCrosshair;
  uniform bool uDensity;
  uniform float uScale;
  void main() {
    vec3 color=texture(uTexture,vUv).rgb;
    if(uDensity) {
      float t=max(radius(vUv*2.0-1.0),.00001),a=ash(max(uStrength,.0001));
      float density=uStrength<.0001?1.0:uStrength/(a*sqrt(1.0+pow(uStrength*t,2.0)))*ash(uStrength*t)/(a*t);
      float value=clamp(log2(max(.001,density*uScale*uScale))*.25+.5,0.0,1.0);
      color=mix(vec3(.025,.1,.5),vec3(1,.35,.03),value);
    } else color=pow(color,vec3(1.0/2.2));
    vec2 d=abs(vUv*2.0-1.0-uGaze);
    if(uCrosshair&&((d.x<.002&&d.y<.025)||(d.y<.002&&d.x<.025)))color=vec3(1,.1,.75);
    outColor=vec4(color,1);
  }
  `
};

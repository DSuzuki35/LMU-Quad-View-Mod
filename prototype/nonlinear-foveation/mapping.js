(function(root) {
  'use strict';
  function validate(point,gaze,strength) {
    if(!Number.isFinite(strength)||strength<0||strength>12||point.length!==2||gaze.length!==2||
      point.some(v=>!Number.isFinite(v)||Math.abs(v)>1+1e-9)||gaze.some(v=>!Number.isFinite(v)||Math.abs(v)>=1))
      throw new RangeError('Expected an eye-image point, interior gaze, and strength 0..12.');
  }
  // Distance along this ray, normalized to its rectangular image boundary.
  // At any edge t=1. Scaling the displacement keeps the same ray/sector.
  function radius(point,gaze) {
    const d=point.map((v,i)=>v-gaze[i]);
    return Math.max(...d.map((v,i)=>Math.abs(v)/(v>=0?1-gaze[i]:1+gaze[i])));
  }
  function transform(point,gaze,strength,inverse) {
    validate(point,gaze,strength);
    const t=radius(point,gaze);
    if(strength===0||t===0||Math.abs(t-1)<1e-14)return point.slice();
    const a=Math.asinh(strength);
    const mapped=inverse?Math.sinh(a*t)/strength:Math.asinh(strength*t)/a;
    return point.map((v,i)=>gaze[i]+(v-gaze[i])*mapped/t);
  }
  function centerScale(strength) {return strength===0?1:strength/Math.asinh(strength);}
  function density(point,gaze,strength) {
    validate(point,gaze,strength);
    const t=radius(point,gaze);
    if(strength===0)return 1;
    if(t<1e-10)return centerScale(strength)**2;
    // det(J) = radial derivative * tangential scale. Positive throughout
    // each sector; the continuous map has derivative seams at sector borders.
    const a=Math.asinh(strength);
    return strength/(a*Math.sqrt(1+(strength*t)**2))*Math.asinh(strength*t)/(a*t);
  }
  function budget(width,height,scale,strength,focusFraction) {
    if(!Number.isInteger(width)||!Number.isInteger(height)||width<1||height<1||
      !Number.isFinite(scale)||scale<=0||scale>1||!Number.isFinite(strength)||strength<0||strength>12||
      !Number.isFinite(focusFraction)||focusFraction<=0||focusFraction>1)throw new RangeError('Invalid render budget.');
    const center=scale*centerScale(strength);
    const remaining=scale**2-(focusFraction*center)**2;
    const dims=s=>[Math.max(1,Math.round(width*s)),Math.max(1,Math.round(height*s))];
    const source=dims(scale),focus=dims(focusFraction*center),outer=dims(Math.sqrt(Math.max(0,remaining)));
    return {width,height,scale,strength,focusFraction,centerScale:center,outerScale:Math.sqrt(Math.max(0,remaining)),
      feasible:remaining>0,source,focus,outer,warpPixels:source[0]*source[1],quadPixels:focus[0]*focus[1]+outer[0]*outer[1]};
  }
  const api={forward:(p,g,k)=>transform(p,g,k,false),inverse:(p,g,k)=>transform(p,g,k,true),radius,density,centerScale,budget};
  if(typeof module!=='undefined'&&module.exports)module.exports=api;
  root.FoveationMapping=api;
})(typeof globalThis!=='undefined'?globalThis:this);

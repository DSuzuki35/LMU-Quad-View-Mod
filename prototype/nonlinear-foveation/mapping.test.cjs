const test = require('node:test');
const assert = require('node:assert/strict');
const M = require('./mapping.js');
const near = (a,b,eps=1e-10) => assert.ok(Math.abs(a-b)<eps, `${a} != ${b}`);

test('identity and center are exact', () => {
  for (const p of [[0,0],[-1,1],[.7,-.4]]) assert.deepEqual(M.forward(p,[.2,-.3],0),p);
  assert.deepEqual(M.forward([.2,-.3],[.2,-.3],8),[.2,-.3]);
  assert.deepEqual(M.inverse([.2,-.3],[.2,-.3],8),[.2,-.3]);
});
test('dense full rectangle roundtrips including extreme gaze and corners', () => {
  for (const gaze of [[0,0],[.8,.8],[-.8,.5],[.4,-.8]]) for(const k of [0,.00001,.2,4,12]) {
    for(let y=0;y<=40;y++) for(let x=0;x<=40;x++) {
      const p=[x/20-1,y/20-1], q=M.forward(p,gaze,k), back=M.inverse(q,gaze,k);
      assert.ok(q.every(v => Number.isFinite(v)&&v>=-1-1e-12&&v<=1+1e-12));
      near(back[0],p[0]); near(back[1],p[1]);
      if(x===0||x===40||y===0||y===40) {near(q[0],p[0]);near(q[1],p[1]);}
      assert.ok(M.density(p,gaze,k)>0);
    }
  }
});
test('analytic area agrees with finite differences away from sector boundaries', () => {
  const p=[.51,.23],g=[-.1,.13],k=5,h=1e-6;
  const a=M.forward(p,g,k),x=M.forward([p[0]+h,p[1]],g,k),y=M.forward([p[0],p[1]+h],g,k);
  const det=((x[0]-a[0])*(y[1]-a[1])-(y[0]-a[0])*(x[1]-a[1]))/(h*h);
  near(det,M.density(p,g,k),1e-5);
  near(M.centerScale(4),4/Math.asinh(4));
});
test('invalid domain is rejected', () => {
  for(const [p,g,k] of [[[NaN,0],[0,0],1],[[0,0],[1,0],1],[[1.1,0],[0,0],1],[[0,0],[0,0],-1]])
    assert.throws(()=>M.forward(p,g,k));
});
test('quad comparison matches central density and nominal pixel budget', () => {
  const plan=M.budget(1024,768,.55,4,.3);
  assert.ok(plan.feasible);
  near(plan.centerScale,.55*4/Math.asinh(4));
  near(plan.outerScale**2+(plan.focusFraction*plan.centerScale)**2,.55**2);
  assert.ok(Math.abs(plan.quadPixels-plan.warpPixels)/plan.warpPixels<.015);
  assert.equal(M.budget(1024,768,.4,12,.8).feasible,false);
});

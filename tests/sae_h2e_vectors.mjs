// Independent affine BigInt model; all inputs here are PUBLIC test fixtures.
// RFC9380 anchors the map. Node/OpenSSL checks curve encoding and PWE X.
import assert from 'node:assert/strict';
import {createHmac,createHash,createECDH,ECDH} from 'node:crypto';
import {writeFileSync} from 'node:fs';
const p=BigInt('0xffffffff00000001000000000000000000000000ffffffffffffffffffffffff');
const n=BigInt('0xffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551');
const b=BigInt('0x5ac635d8aa3a93e7b3ebbd55769886bc651d06b0cc53b0f63bce3c3e27d2604b');
const mod=x=>(x%p+p)%p;
const pow=(x,k)=>{let y=1n;x=mod(x);for(;k;k>>=1n,x=x*x%p)if(k&1n)y=y*x%p;return y;};
const inv=x=>pow(x,p-2n);
const hex=(x,bytes=32)=>x.toString(16).padStart(bytes*2,'0');
const bytes=x=>Buffer.from(hex(x),'hex');
const num=x=>BigInt('0x'+x.toString('hex'));
const enc=q=>hex(q[0])+hex(q[1]);
const hmac=(k,...data)=>{const h=createHmac('sha256',k);for(const d of data)h.update(d);return h.digest();};
const expand=(key,label)=>{const info=Buffer.from(label);const t=hmac(key,info,Buffer.from([1]));return Buffer.concat([t,hmac(key,t,info,Buffer.from([2]))]).subarray(0,48);};
function map(value) {
    const u=mod(value),t=mod(-10n*u*u),m=mod(t*t+t);
    const x1=m===0n?mod(b*inv(30n)):mod(b*inv(3n)*(1n+inv(m)));
    const x2=mod(t*x1),g=x=>mod(x*x*x-3n*x+b);
    let x=x1,y=pow(g(x1),(p+1n)/4n);
    if(mod(y*y)!==g(x1)){x=x2;y=pow(g(x2),(p+1n)/4n);}
    assert.equal(mod(y*y),g(x));if((y&1n)!==(u&1n))y=mod(-y);
    ECDH.convertKey(Buffer.from('04'+hex(x)+hex(y),'hex'),'prime256v1');return [x,y];
}
function add(a,c) {
    if(a===null)return c;if(c===null)return a;
    if(a[0]===c[0]&&mod(a[1]+c[1])===0n)return null;
    const slope=a[0]===c[0]?mod((3n*a[0]*a[0]-3n)*inv(2n*a[1])):mod((c[1]-a[1])*inv(c[0]-a[0]));
    const x=mod(slope*slope-a[0]-c[0]);return [x,mod(slope*(a[0]-x)-a[1])];
}
function mul(k,q) {let r=null;for(;k;k>>=1n,q=add(q,q))if(k&1n)r=add(r,q);return r;}
const rfcU=BigInt('0xad5342c66a6dd0ff080df1da0ea1c04b96e0330dd89406465eeba11582515009');
assert.equal(enc(map(rfcU)),'ab640a12220d3ff283510ff3f4b1953d09fad35795140b1c5d64f313967934d5dccb558863804a881d4fff3455716c836cef230e5209594ddd33d85c565b19b1');
const exception=pow(inv(10n),(p+1n)/4n);
assert.equal(mod(exception*exception),inv(10n));
const fieldInputs=[0n,1n,2n,p-1n,p,p+1n,(1n<<384n)-1n,exception,p-exception,rfcU];
for(let i=0;i<22;i++)fieldInputs.push(num(Buffer.concat([createHash('sha256').update('public SSWU '+i).digest(),Buffer.alloc(16,i)])));
writeFileSync(process.argv[2]+'/sswu-public-vectors.txt',fieldInputs.map(u=>hex(u,48)+' '+enc(map(u))).join('\n')+'\n');
const hashes=[0n,1n,n-2n,n-1n,n,(1n<<256n)-1n];
writeFileSync(process.argv[2]+'/h2e-scalar-vectors.txt',hashes.map(u=>hex(u)+' '+hex(u%(n-1n)+1n)).join('\n')+'\n');
const a=Buffer.from('020000000001','hex'),c=Buffer.from('020000000002','hex');
const rows=[];let first;
for(let i=0;i<16;i++) {
    const ssid=i===0?Buffer.from('OurOS-H2E-public-test'):Buffer.alloc(i===15?32:i+1,i);
    const password=i===0?Buffer.from('not-a-network-secret'):Buffer.alloc(i===15?128:i+1,i^0xa5);
    const id=i===0||i%2===0?Buffer.alloc(0):Buffer.alloc(i===15?64:i,255-i);
    const seed=hmac(ssid,password,id);
    const pt=add(map(num(expand(seed,'SAE Hash to Element u1 P1'))),map(num(expand(seed,'SAE Hash to Element u2 P2'))));assert(pt);
    const scalar=num(hmac(Buffer.alloc(32),c,a))%(n-1n)+1n,pwe=mul(scalar,pt);assert(pwe);
    const e=createECDH('prime256v1');e.setPrivateKey(bytes(scalar));assert.equal(e.computeSecret(Buffer.from('04'+enc(pt),'hex')).toString('hex'),hex(pwe[0]));
    rows.push([ssid.toString('hex'),password.toString('hex'),id.length?id.toString('hex'):'-',a.toString('hex'),c.toString('hex'),enc(pt),enc(pwe)].join(' '));
    if(i===0)first={ssid:ssid.toString(),password:password.toString(),pt:enc(pt),pwe:enc(pwe)};
}
writeFileSync(process.argv[2]+'/h2e-public-vectors.txt',rows.join('\n')+'\n');
console.log(JSON.stringify(first));

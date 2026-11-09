// PUBLIC synthetic exchange fixtures; independent affine group math and
// Node/OpenSSL HMAC/ECDH. No user/network credentials are used.
import assert from 'node:assert/strict';
import {createHmac,createHash,createECDH} from 'node:crypto';
import {writeFileSync} from 'node:fs';
const p=BigInt('0xffffffff00000001000000000000000000000000ffffffffffffffffffffffff');
const n=BigInt('0xffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551');
const mod=x=>(x%p+p)%p;
const pow=(x,k)=>{let y=1n;for(x=mod(x);k;k>>=1n,x=x*x%p)if(k&1n)y=y*x%p;return y;};
const hex=x=>x.toString(16).padStart(64,'0');
const buf=x=>Buffer.from(hex(x),'hex');
const pt=q=>hex(q[0])+hex(q[1]);
function add(a,b){
    if(a===null)return b;if(b===null)return a;
    if(a[0]===b[0]&&mod(a[1]+b[1])===0n)return null;
    const s=a[0]===b[0]?mod((3n*a[0]*a[0]-3n)*pow(2n*a[1],p-2n)):mod((b[1]-a[1])*pow(b[0]-a[0],p-2n));
    const x=mod(s*s-a[0]-b[0]);return [x,mod(s*(a[0]-x)-a[1])];
}
function mul(k,a){let r=null;for(;k;k>>=1n,a=add(a,a))if(k&1n)r=add(r,a);return r;}
const pweHex='f9681ec9f5e39390b50ffaa69bdf04621f662f42fe85e2f101f7dcdcaa6a5bab948a94f8e068a5e976060fc5cb751f876e4346d7b7fabb5c6b636ed17aaf59f5';
const pwe=[BigInt('0x'+pweHex.slice(0,64)),BigInt('0x'+pweHex.slice(64))];
const hmac=(key,...pieces)=>{const h=createHmac('sha256',key);for(const piece of pieces)h.update(piece);return h.digest();};
const le16=n=>{const b=Buffer.alloc(2);b.writeUInt16LE(n);return b;};
const rnd=i=>BigInt('0x'+createHash('sha256').update('OurOS public SAE exchange '+i).digest('hex'))%(n-2n)+2n;
const makeCommit=(r,m)=>Buffer.concat([le16(19),buf((r+m)%n),Buffer.from(pt(mul(n-m,pwe)),'hex')]);
const lines=[];let first;
for(let i=0;i<12;i++) {
    const ra=i===0?2n:rnd(4*i),ma=i===0?3n:rnd(4*i+1),rb=i===0?5n:rnd(4*i+2),mb=i===0?7n:rnd(4*i+3);
    assert(ra!==ma&&rb!==mb&&(ra+ma)%n>1n&&(rb+mb)%n>1n);
    const ca=makeCommit(ra,ma),cb=makeCommit(rb,mb),shared=mul(ra*rb%n,pwe);
    const ec=createECDH('prime256v1');ec.setPrivateKey(buf(ra*rb%n));assert.equal(ec.computeSecret(Buffer.from('04'+pweHex,'hex')).toString('hex'),hex(shared[0]));
    const context=buf((ra+ma+rb+mb)%n),seed=hmac(Buffer.alloc(32),buf(shared[0]));
    const keys=Buffer.concat([1,2].map(counter=>hmac(seed,le16(counter),Buffer.from('SAE KCK and PMK'),context,le16(512))));
    const kck=keys.subarray(0,32),pmk=keys.subarray(32),pmkid=context.subarray(0,16);
    const fa=Buffer.concat([le16(1),hmac(kck,le16(1),ca.subarray(2),cb.subarray(2))]);
    const fb=Buffer.concat([le16(1),hmac(kck,le16(1),cb.subarray(2),ca.subarray(2))]);
    const row=[pweHex,hex(ra),hex(ma),hex(rb),hex(mb),ca.toString('hex'),cb.toString('hex'),fa.toString('hex'),fb.toString('hex'),pmk.toString('hex'),pmkid.toString('hex')];
    lines.push(row.join(' '));if(!i)first={pwe:pweHex,commit:row[5],confirm:row[7],pmk:row[9],pmkid:row[10]};
}
writeFileSync(process.argv[2],lines.join('\n')+'\n');console.log(JSON.stringify(first));

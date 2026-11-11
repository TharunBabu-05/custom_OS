// Independent public-fixture model using Node HMAC + OpenSSL SEC1 decoding.
// No real network credentials. Variable search here is test-only.
import {createHmac,createHash,ECDH} from 'node:crypto';
import {writeFileSync} from 'node:fs';
import assert from 'node:assert/strict';
const prime=Buffer.from('ffffffff00000001000000000000000000000000ffffffffffffffffffffffff','hex');
const p=BigInt('0x'+prime.toString('hex')), b=BigInt('0x5ac635d8aa3a93e7b3ebbd55769886bc651d06b0cc53b0f63bce3c3e27d2604b');
const mod=x=>(x%p+p)%p;
const pow=(a,k)=>{let r=1n;for(;k;k>>=1n,a=a*a%p)if(k&1n)r=r*a%p;return r;};
const bytes=x=>Buffer.from(x.toString(16).padStart(64,'0'),'hex');
const hmac=(k,...parts)=>{const h=createHmac('sha256',k);for(const part of parts)h.update(part);return h.digest();};
const fromX=(x,parity)=>{
    const v=BigInt('0x'+x.toString('hex'));if(v>=p)return null;
    const g=mod(v*v*v-3n*v+b);let y=pow(g,(p+1n)/4n);if(y*y%p!==g)return null;
    if(Number(y&1n)!==parity)y=mod(-y);
    const openssl=ECDH.convertKey(Buffer.concat([Buffer.from([2+parity]),x]),'prime256v1',undefined,undefined,'uncompressed');
    assert.equal(openssl.subarray(33).toString('hex'),bytes(y).toString('hex'));
    return Buffer.concat([x,bytes(y)]);
};
const roots=[];for(const x of [0n,1n,2n,p-1n,p,p+1n,(1n<<256n)-1n,...Array.from({length:24},(_,i)=>BigInt('0x'+createHash('sha256').update('public root '+i).digest('hex')))]) {
    for(let parity=0;parity<2;parity++){const q=fromX(bytes(x),parity);roots.push([bytes(x).toString('hex'),parity,q?q.toString('hex'):'-'].join(' '));}
}
writeFileSync(process.argv[2]+'/hunt-root-vectors.txt',roots.join('\n')+'\n');
const rows=[];for(let i=0;i<24;i++) {
    const password=i===0?Buffer.from('public-hunting-fixture'):Buffer.alloc(i===23?128:i+1,i*11&255);
    const a=Buffer.from([2,0,0,0,i,1]),c=Buffer.from([2,0,0,0,i,2]);
    const key=Buffer.concat(Buffer.compare(a,c)>0?[a,c]:[c,a]);let point=null,first=0;
    for(let count=1;count<=40;count++) {
        const seed=hmac(key,password,Buffer.from([count]));
        const value=hmac(seed,Buffer.from([1,0]),Buffer.from('SAE Hunting and Pecking'),prime,Buffer.from([0,1]));
        const q=fromX(value,seed[31]&1);if(!point&&q){point=q;first=count;}
    }
    assert(point);rows.push([password.toString('hex'),a.toString('hex'),c.toString('hex'),point.toString('hex'),first].join(' '));
}
writeFileSync(process.argv[2]+'/hunt-public-vectors.txt',rows.join('\n')+'\n');
console.log('PASS: 62 canonical/noncanonical root vectors and 24 SAE H&P PWE vectors checked with independent BigInt arithmetic and OpenSSL');

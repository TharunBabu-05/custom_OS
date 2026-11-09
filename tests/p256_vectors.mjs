// Deterministic PUBLIC fixtures only; Node's OpenSSL backend is the oracle.
import {createECDH,createHash} from 'node:crypto';
import {writeFileSync} from 'node:fs';
const order=BigInt('0xffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551');
const hex=n=>n.toString(16).padStart(64,'0');
const pub=n=>{const e=createECDH('prime256v1');e.setPrivateKey(Buffer.from(hex(n),'hex'));return e.getPublicKey(undefined,'uncompressed').subarray(1).toString('hex');};
const scalar=i=>BigInt('0x'+createHash('sha256').update('OurOS public P-256 fixture '+i).digest('hex'))%(order-2n)+1n;
const lines=[];
for(let i=0;i<40;i++) {
    const a=i<8?1n<<BigInt(i*31):scalar(2*i);
    const b=i===0?1n:scalar(2*i+1);
    lines.push([hex(a),pub(b),pub(a*b%order),pub((b+1n)%order),pub(order-b)].join(' '));
}
writeFileSync(process.argv[2],lines.join('\n')+'\n');

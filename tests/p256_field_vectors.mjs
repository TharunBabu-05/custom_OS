// Public arithmetic fixtures; independent BigInt modulo oracle, no secrets.
import {createHash} from 'node:crypto';
import {writeFileSync} from 'node:fs';
const p=(1n<<256n)-(1n<<224n)+(1n<<192n)+(1n<<96n)-1n;
const max=(1n<<256n)-1n;
const hex=x=>x.toString(16).padStart(64,'0');
const values=[0n,1n,2n,p-2n,p-1n,p,p+1n,max-1n,max];
for(let i=0;i<256;i++) { values.push(1n<<BigInt(i),max^(1n<<BigInt(i))); }
const lines=[];
const add=(a,b)=>lines.push(`${hex(a)} ${hex(b)} ${hex(a*b%p)}`);
for(const a of values) { for(const b of [0n,1n,2n,p-1n,p,max]) { add(a,b); }add(a,a); }
for(let i=0;i<10000;i++) {
    const v=n=>BigInt('0x'+createHash('sha256').update('OurOS public field '+n).digest('hex'));
    add(v(2*i),v(2*i+1));
}
writeFileSync(process.argv[2],lines.join('\n')+'\n');

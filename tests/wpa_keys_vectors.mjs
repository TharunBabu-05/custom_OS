// Public fixtures only. Node/OpenSSL ECB/CMAC/wrap and SHA256 PRF oracle.
import {createCipheriv,createHmac,createHash} from 'node:crypto';
import {writeFileSync} from 'node:fs';
import assert from 'node:assert/strict';
const hash=(s,n=32)=>createHash('sha256').update(s).digest().subarray(0,n);
const aes=(key,p)=>{const c=createCipheriv('aes-128-ecb',key,null);c.setAutoPadding(false);return Buffer.concat([c.update(p),c.final()]);};
const xor=(a,b)=>Buffer.from(a.map((x,i)=>x^b[i]));
const dbl=b=>{const n=BigInt('0x'+b.toString('hex')),r=((n<<1n)&((1n<<128n)-1n))^((n>>127n)?0x87n:0n);return Buffer.from(r.toString(16).padStart(32,'0'),'hex');};
function cmac(key,message) {
    const k1=dbl(aes(key,Buffer.alloc(16))),k2=dbl(k1);let acc=Buffer.alloc(16),n=Math.max(1,Math.ceil(message.length/16));
    for(let i=0;i<n-1;i++)acc=aes(key,xor(acc,message.subarray(i*16,i*16+16)));
    const used=message.length-16*(n-1),last=Buffer.alloc(16);message.copy(last,0,16*(n-1));if(used<16)last[used]=128;
    return aes(key,xor(acc,xor(last,used===16?k1:k2)));
}
const katKey=Buffer.from('2b7e151628aed2a6abf7158809cf4f3c','hex');
assert.equal(cmac(katKey,Buffer.alloc(0)).toString('hex'),'bb1d6929e95937287fa37d129b756746');
assert.equal(cmac(katKey,Buffer.from('6bc1bee22e409f96e93d7e117393172a','hex')).toString('hex'),'070a16b46b4d4144f79bdd9dd04a287c');
const lines=[],keyData=[];
for(let i=0;i<24;i++){const key=hash('public key '+i,16),plain=hash('public block '+i,16);lines.push(['A',key.toString('hex'),plain.toString('hex'),aes(key,plain).toString('hex')].join(' '));}
for(const n of [0,1,15,16,17,31,32,40,64,113,141,4096]){const key=hash('public cmac '+n,16),msg=Buffer.from(Array.from({length:n},(_,i)=>i*13&255));lines.push(['C',key.toString('hex'),n?msg.toString('hex'):'-',cmac(key,msg).toString('hex')].join(' '));}
for(const n of [16,24,32,64,128,512]){const key=hash('public wrap '+n,16),plain=Buffer.from(Array.from({length:n},(_,i)=>i*7&255)),c=createCipheriv('id-aes128-wrap',key,Buffer.alloc(8,0xa6));const wrapped=Buffer.concat([c.update(plain),c.final()]);lines.push(['U',key.toString('hex'),wrapped.toString('hex'),plain.toString('hex')].join(' '));}
for(let i=0;i<16;i++){
    const pmk=hash('public PMK '+i),a=Buffer.from([2,0,0,0,i,1]),b=Buffer.from([2,0,0,0,i,2]),sn=hash('public SNonce '+i),an=hash('public ANonce '+i);
    const context=Buffer.concat([a,b,...(Buffer.compare(sn,an)<0?[sn,an]:[an,sn])]);
    const hmac=(counter)=>createHmac('sha256',pmk).update(Buffer.from([counter,0])).update('Pairwise key expansion').update(context).update(Buffer.from([0x80,1])).digest();
    const ptk=Buffer.concat([hmac(1),hmac(2)]).subarray(0,48);lines.push(['P',pmk.toString('hex'),a.toString('hex'),b.toString('hex'),sn.toString('hex'),an.toString('hex'),ptk.toString('hex')].join(' '));
    const rsn=Buffer.from('301a0100000fac040100000fac040100000fac08c0000000000fac06','hex'),packet=Buffer.alloc(113+rsn.length);b.copy(packet);a.copy(packet,6);packet.writeUInt16BE(0x888e,12);
    packet[14]=2;packet[15]=3;packet.writeUInt16BE(95+rsn.length,16);packet[18]=2;packet.writeUInt16BE(0x108,19);packet.writeBigUInt64BE(BigInt(i+1),23);sn.copy(packet,31);packet.writeUInt16BE(rsn.length,111);rsn.copy(packet,113);
    cmac(ptk.subarray(0,16),packet.subarray(14)).copy(packet,95);
    lines.push(['M',pmk.toString('hex'),a.toString('hex'),b.toString('hex'),sn.toString('hex'),an.toString('hex'),packet.toString('hex')].join(' '));
    const m1=Buffer.alloc(99);m1[0]=2;m1[1]=3;m1.writeUInt16BE(95,2);m1[4]=2;m1.writeUInt16BE(0x88,5);m1.writeUInt16BE(16,7);m1.writeBigUInt64BE(BigInt(i+1),9);an.copy(m1,17);
    const plain=Buffer.from(Array.from({length:16+8*i},(_,j)=>j*7&255)),wrap=createCipheriv('id-aes128-wrap',ptk.subarray(16,32),Buffer.alloc(8,0xa6));
    const encrypted=Buffer.concat([wrap.update(plain),wrap.final()]),m3=Buffer.alloc(99+encrypted.length);
    m1.copy(m3);m3.writeUInt16BE(95+encrypted.length,2);m3.writeUInt16BE(0x13c8,5);m3.writeBigUInt64BE(BigInt(i+2),9);m3.writeUInt16BE(encrypted.length,97);encrypted.copy(m3,99);cmac(ptk.subarray(0,16),m3).copy(m3,81);
    lines.push(['H',pmk.toString('hex'),a.toString('hex'),b.toString('hex'),sn.toString('hex'),m1.toString('hex'),packet.toString('hex'),m3.toString('hex'),String(plain.length)].join(' '));
    const apRsn=Buffer.from(i%2?'30140100000fac040100000fac040100000fac088000':'30180100000fac040100000fac040200000fac02000fac088000','hex');
    if(i%4===2 || i%4===3){const expanded=Buffer.concat([apRsn,Buffer.from(i%4===2?'0000':'0000000fac06','hex')]);expanded[1]=expanded.length-2;keyDataRsn(expanded);}else keyDataRsn(apRsn);
    function keyDataRsn(adv){
        const gtk=Buffer.alloc(24),igtk=Buffer.alloc(30);gtk.set([221,22,0,15,172,1,i%4|(i%2?4:0),0]);hash('public GTK '+i,16).copy(gtk,8);
        igtk.set([221,28,0,15,172,9,4+i%2,0]);igtk.set([7,8,9,10,11,i],8);hash('public IGTK '+i,16).copy(igtk,14);
        let data=Buffer.concat(i%3?[adv,gtk,igtk]:[igtk,adv,gtk]);const pad=(8-data.length%8)%8;if(pad)data=Buffer.concat([data,Buffer.from([221,...Array(pad-1).fill(0)])]);
        const c=createCipheriv('id-aes128-wrap',ptk.subarray(16,32),Buffer.alloc(8,0xa6)),wrapped=Buffer.concat([c.update(data),c.final()]),third=Buffer.alloc(99+wrapped.length);
        m1.copy(third);third.writeUInt16BE(95+wrapped.length,2);third.writeUInt16BE(0x13c8,5);third.writeBigUInt64BE(BigInt(i+2),9);third.set([i+1,2,3,4,5,6,0,0],65);third.writeUInt16BE(wrapped.length,97);wrapped.copy(third,99);cmac(ptk.subarray(0,16),third).copy(third,81);
        const fourth=Buffer.alloc(113);b.copy(fourth);a.copy(fourth,6);fourth.writeUInt16BE(0x888e,12);fourth[14]=2;fourth[15]=3;fourth.writeUInt16BE(95,16);fourth[18]=2;fourth.writeUInt16BE(0x308,19);fourth.writeBigUInt64BE(BigInt(i+2),23);cmac(ptk.subarray(0,16),fourth.subarray(14)).copy(fourth,95);
        const retry=Buffer.from(third),answer=Buffer.from(fourth);retry.writeBigUInt64BE(BigInt(i+5),9);retry.fill(0,81,97);cmac(ptk.subarray(0,16),retry).copy(retry,81);
        answer.writeBigUInt64BE(BigInt(i+5),23);answer.fill(0,95,111);cmac(ptk.subarray(0,16),answer.subarray(14)).copy(answer,95);
        keyData.push(['D',pmk,a,b,sn,m1,packet,adv,data,third,fourth,retry,answer].map(x=>Buffer.isBuffer(x)?x.toString('hex'):x).join(' '));
    }
}
writeFileSync(process.argv[2],lines.filter(x=>!x.startsWith('H ')).join('\n')+'\n');
writeFileSync(process.argv[2]+'.handshake',lines.filter(x=>x.startsWith('H ')).join('\n')+'\n');
writeFileSync(process.argv[2]+'.keydata',keyData.join('\n')+'\n');
console.log('PASS: independent OpenSSL AES/CMAC/unwrap plus 16 SHA256 PTK and exact EAPOL M2 fixtures generated');

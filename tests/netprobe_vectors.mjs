// Independent public ARP/IPv4/ICMP fixtures. No network access.
import {writeFileSync} from 'node:fs';
const checksum=b=>{let s=0;for(let i=0;i<b.length;i+=2)s+=(b[i]<<8)|(b[i+1]??0);while(s>>>16)s=(s&65535)+(s>>>16);return (~s)&65535;};
const gateway=Buffer.from([192,0,2,1]),gwmac=Buffer.from([2,0,0,0,1,1]),broadcast=Buffer.alloc(6,255),zero=Buffer.alloc(6);
function arp(dest,src,op,spa,tha,tpa){const b=Buffer.alloc(60);dest.copy(b);src.copy(b,6);b.writeUInt16BE(0x806,12);b.writeUInt16BE(1,14);b.writeUInt16BE(0x800,16);b[18]=6;b[19]=4;b.writeUInt16BE(op,20);src.copy(b,22);spa.copy(b,28);tha.copy(b,32);tpa.copy(b,38);return b;}
function echo(mac,address,entropy,reply){const b=Buffer.alloc(74);(reply?mac:gwmac).copy(b);(reply?gwmac:mac).copy(b,6);b.writeUInt16BE(0x800,12);b[14]=0x45;b.writeUInt16BE(60,16);b.writeUInt16BE(reply?0xabcd:1,18);b.writeUInt16BE(0x4000,20);b[22]=reply?57:64;b[23]=1;(reply?gateway:address).copy(b,26);(reply?address:gateway).copy(b,30);b[34]=reply?0:8;b.writeUInt16BE(entropy.readUInt16BE(24),38);b.writeUInt16BE(1,40);entropy.copy(b,42,8,24);Buffer.from('OurOS ICMP probe').copy(b,58);b.writeUInt16BE(checksum(b.subarray(34)),36);b.writeUInt16BE(checksum(b.subarray(14,34)),24);return b;}
const rows=[];
for(let i=0;i<8;i++){
 const mac=Buffer.from([2,0,0,0,0,1+i]),address=Buffer.from([192,0,2,10+i]),mask=Buffer.from([255,255,255,0]),entropy=Buffer.from(Array.from({length:32},(_,j)=>(i*17+j+1)&255));
 const probe=arp(broadcast,mac,1,Buffer.alloc(4),zero,address),announce=arp(broadcast,mac,1,address,zero,address),resolve=arp(broadcast,mac,1,address,zero,gateway),answer=arp(mac,gwmac,2,gateway,mac,address);
 const request=arp(broadcast,gwmac,1,gateway,zero,address),response=arp(gwmac,mac,2,address,gwmac,gateway);
 rows.push(['N',mac,entropy,address,gateway,mask,probe,announce,resolve,answer,echo(mac,address,entropy,false),echo(mac,address,entropy,true),request,response].map(v=>Buffer.isBuffer(v)?v.toString('hex'):v).join(' '));
}
writeFileSync(process.argv[2],rows.join('\n')+'\n');console.log('PASS: 8 independent exact Ethernet/ARP/IPv4/ICMP packet fixture sets generated');

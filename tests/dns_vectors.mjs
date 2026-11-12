// Independent public DNS/IPv4/UDP fixtures; no DNS or network access.
import {writeFileSync} from 'node:fs';
const u16=n=>{const b=Buffer.alloc(2);b.writeUInt16BE(n);return b;};
const u32=n=>{const b=Buffer.alloc(4);b.writeUInt32BE(n);return b;};
const name=s=>Buffer.concat([...s.split('.').filter(Boolean).map(l=>Buffer.concat([Buffer.from([l.length]),Buffer.from(l)])),Buffer.from([0])]);
const rr=(owner,type,data,ttl=60)=>Buffer.concat([owner,u16(type),u16(1),u32(ttl),u16(data.length),data]);
const pointer=Buffer.from([0xc0,12]),a=Buffer.from([203,0,113,80]),alias=name('edge.example.net');
const checksum=b=>{let s=0;for(let i=0;i<b.length;i+=2)s+=(b[i]<<8)|(b[i+1]??0);while(s>>>16)s=(s&65535)+(s>>>16);return (~s)&65535;};
function frame(payload,local,server,port,reply,id){
 const b=Buffer.alloc(Math.max(60,42+payload.length)),mac=Buffer.from([2,0,0,0,0,1]),gw=Buffer.from([2,0,0,0,1,1]);
 (reply?mac:gw).copy(b);(reply?gw:mac).copy(b,6);b.writeUInt16BE(0x800,12);b[14]=0x45;b.writeUInt16BE(28+payload.length,16);b.writeUInt16BE(id,18);b.writeUInt16BE(0x4000,20);b[22]=64;b[23]=17;
 (reply?server:local).copy(b,26);(reply?local:server).copy(b,30);b.writeUInt16BE(reply?53:port,34);b.writeUInt16BE(reply?port:53,36);b.writeUInt16BE(8+payload.length,38);payload.copy(b,42);
 const pseudo=Buffer.concat([b.subarray(26,34),Buffer.from([0,17]),u16(8+payload.length),b.subarray(34,42+payload.length)]);b.writeUInt16BE(checksum(pseudo)||65535,40);b.writeUInt16BE(checksum(b.subarray(14,34)),24);return b;
}
const rows=[];
for(let i=0;i<13;i++){
 const hostname=i===11 ? `${'a'.repeat(63)}.${'b'.repeat(63)}.${'c'.repeat(63)}.${'d'.repeat(61)}` : i===10 ? 'a.b' : 'www.example.com';
 const entropy=Buffer.from(Array.from({length:32},(_,j)=>(j+17*i+1)&255)),id=entropy.readUInt16BE(),port=1024+entropy.readUInt16BE(2);
 const q=Buffer.concat([u16(id),u16(0x100),u16(1),Buffer.alloc(6),name(hostname),u16(1),u16(1)]);
 let answers=[rr(pointer,1,a)],authority=[],extra=[],flags=0x8180,status=2,hops=0,ttl=60,answer=a;
 if(i===1)answers.push(rr(pointer,1,Buffer.from([203,0,113,81])));
 if(i===2){answers=[rr(pointer,5,alias,90),rr(alias,1,a,40)];hops=1;ttl=40;}
 if(i===3){const middle=name('middle.example.net');answers=[rr(alias,1,a,90),rr(middle,5,alias,50),rr(pointer,5,middle,70)];hops=2;ttl=50;}
 if(i===4){answers=[];authority=[rr(name('example.com'),6,Buffer.alloc(20))];flags=0x8183;status=3;ttl=0;answer=Buffer.alloc(4);}
 if(i===5){answers=[];extra=[rr(pointer,1,a)];status=4;ttl=0;answer=Buffer.alloc(4);}
 if(i===6){answers=[];flags=0x8380;status=6;ttl=0;answer=Buffer.alloc(4);}
 if(i===7){answers=[rr(pointer,5,alias)];status=4;hops=1;ttl=0;answer=Buffer.alloc(4);}
 if(i===8){answers=[];flags=0x8182;status=5;ttl=0;answer=Buffer.alloc(4);}
 if(i===9)answers.unshift(rr(name('unrelated.invalid'),1,Buffer.from([9,9,9,9])));
 if(i===10)extra=[Buffer.concat([Buffer.from([0]),u16(41),u16(512),u32(0),u16(0)])];
 if(i===12){answers=[];let owner=pointer;for(let j=0;j<9;j++){const target=name(`n${j}.example.net`);answers.push(rr(owner,5,target));owner=target;}answers.push(rr(owner,1,a));status=7;hops=8;ttl=0;answer=Buffer.alloc(4);}
 const reply=Buffer.concat([u16(id),u16(flags),u16(1),u16(answers.length),u16(authority.length),u16(extra.length),q.subarray(12),...answers,...authority,...extra]);
 const local=Buffer.from([192,0,2,10]),server=Buffer.from(i===9?[8,8,8,8]:[192,0,2,1]);
 rows.push(['D',hostname,entropy,server,q,reply,status,answer,hops,ttl,frame(q,local,server,port,false,id),frame(reply,local,server,port,true,id)].map(v=>Buffer.isBuffer(v)?v.toString('hex'):v).join(' '));
}
writeFileSync(process.argv[2],rows.join('\n')+'\n');console.log('PASS: 13 independent DNS question/answer and exact Ethernet/IPv4/UDP fixture sets');

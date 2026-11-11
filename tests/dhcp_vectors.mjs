// Independent public Ethernet/IPv4/UDP/BOOTP/DHCP fixtures, no network I/O.
import {writeFileSync} from 'node:fs';
function checksum(data){let s=0;for(let i=0;i<data.length;i+=2)s+=(data[i]<<8)|(data[i+1]??0);while(s>>>16)s=(s&65535)+(s>>>16);return (~s)&65535;}
const ip=x=>Buffer.from(x),server=ip([192,0,2,1]);
function packet(mac,xid,type,address,options,zeroUDP=false){
    let opts=Buffer.from(options),boot=Buffer.alloc(Math.max(300,240+opts.length));boot[0]=type===1||type===3||type===4?1:2;boot[1]=1;boot[2]=6;boot.writeUInt32BE(xid,4);
    if(type===1||type===3)boot.writeUInt16BE(0x8000,10);else if(boot[0]===2)address.copy(boot,16);mac.copy(boot,28);boot.writeUInt32BE(0x63825363,236);opts.copy(boot,240);
    const udp=Buffer.alloc(8+boot.length);udp.writeUInt16BE(boot[0]===1?68:67);udp.writeUInt16BE(boot[0]===1?67:68,2);udp.writeUInt16BE(udp.length,4);boot.copy(udp,8);
    const v=Buffer.alloc(20);v[0]=0x45;v.writeUInt16BE(20+udp.length,2);v.writeUInt16BE(0x4000,6);v[8]=64;v[9]=17;if(boot[0]===2)server.copy(v,12);v.fill(255,16,20);v.writeUInt16BE(checksum(v),10);
    const pseudo=Buffer.concat([v.subarray(12,20),Buffer.from([0,17,udp.length>>8,udp.length&255]),udp]);if(!zeroUDP)udp.writeUInt16BE(checksum(pseudo)||65535,6);
    const ether=Buffer.alloc(14);ether.fill(255,0,6);(boot[0]===1?mac:Buffer.from([2,0,0,0,1,1])).copy(ether,6);ether.writeUInt16BE(0x800,12);return Buffer.concat([ether,v,udp]);
}
const lines=[];
for(let i=0;i<16;i++){
    const mac=Buffer.from([2,0,0,0,0,i+1]),entropy=Buffer.alloc(10);entropy.writeUInt32BE(0x12345678+i);entropy.writeUInt16BE(i*379,4);entropy.writeUInt16BE(i*101,6);entropy.writeUInt16BE(i*239,8);
    const xid=entropy.readUInt32BE(),address=ip([192,0,2,10+i]),client=[61,7,1,...mac],prl=[55,4,1,3,6,51,57,2,2,64,255];
    const discover=packet(mac,xid,1,address,[53,1,1,...client,...prl]),request=packet(mac,xid,3,address,[53,1,3,...client,50,4,...address,54,4,...server,...prl]);
    const config=[54,4,...server,51,4,0,0,14,16,1,4,255,255,255,0,3,4,...server,6,8,8,8,8,8,1,1,1,1,...client];
    const opts=t=>i%3?[53,1,t,...config,255]:[0,...config,53,1,t,255];
    const offer=packet(mac,xid,2,address,opts(2),!(i%2)),ack=packet(mac,xid,5,address,opts(5),!(i%2));
    const decline=packet(mac,xid,4,address,[53,1,4,...client,50,4,...address,54,4,...server,255]);
    lines.push(['D',mac,entropy,discover,offer,request,ack,decline].map(x=>Buffer.isBuffer(x)?x.toString('hex'):x).join(' '));
}
writeFileSync(process.argv[2],lines.join('\n')+'\n');console.log('PASS: 16 independent Ethernet/IPv4/UDP/DHCP public DORA fixture sets generated');

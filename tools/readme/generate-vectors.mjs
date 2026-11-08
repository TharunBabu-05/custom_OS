import {writeFileSync, mkdirSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
const out = fileURLToPath(new URL('../../docs/assets/', import.meta.url));
mkdirSync(out,{recursive:true});
const head=(w,h,title,desc)=>`<svg xmlns="http://www.w3.org/2000/svg" width="${w}" height="${h}" viewBox="0 0 ${w} ${h}" role="img" aria-labelledby="title desc"><title id="title">${title}</title><desc id="desc">${desc}</desc>`;
const save=(name,s)=>writeFileSync(out+name,s+'\n','utf8');
const pins = Array.from({length:9},(_,i)=>`<path d="M${880+i*22} 163v-20m0 264v20"/><path d="M849 ${184+i*22}h-20m274 0h20"/>`).join('');
const traces = [
  'M852 199H773L735 161H666','M850 243H739L695 287H605',
  'M850 332H783L732 383H676','M1100 199H1170L1200 169H1250',
  'M1100 287H1200L1240 327H1270','M998 164V100L1043 55H1208',
  'M932 409V439L901 470','M1042 410V450H1160'
];
save('hero.svg',head(1280,570,'OurOS — from first instruction to real silicon','An original AArch64 kernel. Physical Raspberry Pi 5 boot observed over a CP2102 serial console. Animated circuit artwork.')+`
<defs>
 <linearGradient id="bg" x2="1" y2="1"><stop stop-color="#08101d"/><stop offset="1" stop-color="#101c2c"/></linearGradient>
 <linearGradient id="ink"><stop stop-color="#72f2c8"/><stop offset="1" stop-color="#62c1f1"/></linearGradient>
 <radialGradient id="halo"><stop stop-color="#24726b" stop-opacity=".38"/><stop offset="1" stop-color="#0b1421" stop-opacity="0"/></radialGradient>
 <pattern id="grid" width="32" height="32" patternUnits="userSpaceOnUse"><path d="M32 0H0V32" fill="none" stroke="#23354a" stroke-width=".6"/></pattern>
 <filter id="glow"><feGaussianBlur stdDeviation="3"/></filter>
</defs>
<rect width="1280" height="570" rx="24" fill="url(#bg)"/>
<rect x="640" width="640" height="500" fill="url(#grid)" opacity=".5"/>
<ellipse cx="975" cy="272" rx="310" ry="260" fill="url(#halo)"/>
<path d="M40 38h38m-38 0v38M1240 38h-38m38 0v38" stroke="#355468" fill="none"/>
<text x="64" y="77" fill="#82a2b5" font-family="monospace" font-size="15" letter-spacing="4">INDEPENDENT KERNEL / REAL HARDWARE</text>
<text x="58" y="213" font-family="Arial,Helvetica,sans-serif" font-size="128" font-weight="800" letter-spacing="-8" fill="#eef5ff">Our<tspan fill="url(#ink)">OS</tspan><tspan fill="#ffd17d" font-size="75">.</tspan></text>
<text x="65" y="275" font-family="Arial,Helvetica,sans-serif" font-size="34" font-weight="600" fill="#d7e5f5">From first instruction</text>
<text x="65" y="320" font-family="Arial,Helvetica,sans-serif" font-size="34" font-weight="600" fill="#d7e5f5">to real silicon.</text>
<text x="66" y="365" font-family="monospace" font-size="16" fill="#8299b3">C + AArch64 assembly. Built from the ground up.</text>
<rect x="65" y="402" width="330" height="41" rx="20" fill="#112d2b" stroke="#28564e"/>
<circle cx="85" cy="423" r="5" fill="#72f2c8"><animate attributeName="opacity" values="1;.35;1" dur="2.8s" repeatCount="indefinite"/></circle>
<text x="102" y="428" fill="#8ff4cf" font-family="monospace" font-size="13" letter-spacing="1">PI 5 / KERNEL BOOT OBSERVED</text>
<g stroke="#244452" stroke-width="2" fill="none">${traces.map(d=>`<path d="${d}"/>`).join('')}</g>
<g stroke="#63e6c1" stroke-width="2" fill="none" stroke-dasharray="8 100" opacity=".8">${traces.map((d,i)=>`<path d="${d}"><animate attributeName="stroke-dashoffset" from="216" to="0" dur="${4+i*.35}s" repeatCount="indefinite"/></path>`).join('')}</g>
<g fill="none" stroke="#396a70" stroke-width="8">${pins}</g>
<rect x="847" y="162" width="257" height="249" rx="20" fill="#10272d" stroke="#4ca498" stroke-width="2"/>
<rect x="868" y="182" width="214" height="208" rx="12" fill="#0b1623" stroke="#29414f"/>
<path d="M882 200h33m-33 0v33M1067 376h-33m33 0v-33" stroke="#63e6c1" fill="none"/>
<text x="975" y="252" text-anchor="middle" font-family="monospace" font-size="14" fill="#6a8f9f" letter-spacing="3">ARMV8.2-A</text>
<text x="975" y="306" text-anchor="middle" font-family="Arial,sans-serif" font-weight="700" font-size="43" fill="#e5f3fa">A76</text>
<text x="975" y="340" text-anchor="middle" font-family="monospace" font-size="14" fill="#72f2c8">EL2 → EL1</text>
<circle cx="1065" cy="199" r="4" fill="#ffd17d"/>
<path d="M66 482H1214" stroke="#273b4c"/>
<g font-family="monospace"><text x="66" y="518" font-size="18" fill="#eef5ff">64-BIT</text><text x="66" y="541" font-size="12" fill="#7e98af" letter-spacing="2">AARCH64</text>
<text x="366" y="518" font-size="18" fill="#eef5ff">BARE METAL</text><text x="366" y="541" font-size="12" fill="#7e98af" letter-spacing="2">ORIGINAL KERNEL</text>
<text x="718" y="518" font-size="18" fill="#eef5ff">WINDOWS NATIVE</text><text x="718" y="541" font-size="12" fill="#7e98af" letter-spacing="2">NO WSL REQUIRED</text>
<text x="1070" y="518" font-size="18" fill="#72f2c8">08 NOV 2026</text><text x="1070" y="541" font-size="12" fill="#7e98af" letter-spacing="2">HARDWARE MILESTONE</text></g>
</svg>`);
const stages=[['01','FIRMWARE','Load image + DTB','PI 5 EEPROM'],['02','ENTRY','Stack · BSS · EL','AARCH64 ASM'],['03','BOARD','Attach RP1 UART','BOARD LAYER'],['04','KERNEL','Timer + console','FREESTANDING C'],['05','MONITOR','Commands over TTL','INTERACTIVE EL1']];
save('boot-path.svg',head(1280,264,'The boot path','Five implemented stages: firmware loads the image and DTB; AArch64 entry sets up the CPU; board attaches UART; kernel initializes the timer and console; the monitor handles serial commands.')+`
<rect width="1280" height="264" rx="20" fill="#0b1421"/>
<text x="36" y="39" font-family="monospace" font-size="13" letter-spacing="3" fill="#718fa5">ONE BOOT. FIVE BOUNDARIES. OWNED IN SOURCE.</text>
${stages.map((s,i)=>{const x=32+i*247;return `<rect x="${x}" y="67" width="226" height="162" rx="12" fill="#111f30" stroke="#2a4256"/><text x="${x+18}" y="98" font-family="monospace" font-size="16" fill="#65e6bb">${s[0]}</text><text x="${x+18}" y="133" font-family="Arial,sans-serif" font-weight="700" font-size="21" fill="#e1edf9">${s[1]}</text><text x="${x+18}" y="166" font-family="Arial,sans-serif" font-size="17" fill="#9ab0c5">${s[2]}</text><text x="${x+18}" y="204" font-family="monospace" font-size="11" letter-spacing="1" fill="#7193a9">${s[3]}</text>${i<4?`<path d="M${x+229} 148h14m-5-5 5 5-5 5" fill="none" stroke="#65e6bb"/>`:''}`}).join('')}
<path d="M50 243H1224" stroke="#65e6bb" stroke-dasharray="18 140" opacity=".45"><animate attributeName="stroke-dashoffset" from="316" to="0" dur="9s" repeatCount="indefinite"/></path></svg>`);
save('footer.svg',head(1280,148,'Next: memory management','The next planned milestone is RAM discovery and a physical page allocator. Keep building, one verified boundary at a time.')+`
<defs><linearGradient id="f"><stop stop-color="#102b2c"/><stop offset="1" stop-color="#101b2d"/></linearGradient></defs>
<rect width="1280" height="148" rx="18" fill="url(#f)"/>
<path d="M36 112H1244" stroke="#31545b"/><path d="M36 112H1244" stroke="#65e6bb" stroke-dasharray="35 300"><animate attributeName="stroke-dashoffset" from="670" to="0" dur="10s" repeatCount="indefinite"/></path>
<text x="40" y="48" font-family="monospace" font-size="13" fill="#65e6bb" letter-spacing="3">NEXT FRONTIER / MEMORY</text>
<text x="40" y="85" font-family="Arial,sans-serif" font-size="26" fill="#e0edf7" font-weight="600">One verified boundary at a time.</text>
<text x="1228" y="80" text-anchor="end" font-family="monospace" font-size="15" fill="#96b4c5">DISCOVER → RESERVE → ALLOCATE</text></svg>`);
console.log('PASS: three original SVG assets generated.');

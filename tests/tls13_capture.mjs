// Generate a disposable loopback fixture using Node's OpenSSL TLS stack.
// Only the public certificate, plaintext handshake, and test Finished inputs
// enter the header. Never use production keys or captures here.
import { createDecipheriv, createHmac, createHash } from 'node:crypto';
import { readFileSync, writeFileSync } from 'node:fs';
import net from 'node:net';
import tls from 'node:tls';
import assert from 'node:assert/strict';

const [, , keyFile, certFile, output] = process.argv;
const cert = readFileSync(certFile);
const options = { minVersion: 'TLSv1.3', maxVersion: 'TLSv1.3',
    ciphers: 'TLS_AES_128_GCM_SHA256', ecdhCurve: 'prime256v1', sigalgs: 'ecdsa_secp256r1_sha256' };
const frontend = [], backend = [];
const endpoint = '127.0.0.1';
const server = tls.createServer({ ...options, cert, key: readFileSync(keyFile) }, socket => socket.end());
const proxy = net.createServer(client => {
    const peer = net.connect(server.address().port, endpoint);
    client.on('data', chunk => frontend.push(chunk));
    peer.on('data', chunk => backend.push(chunk));
    client.pipe(peer).pipe(client);
});
const listen = instance => new Promise(resolve => instance.listen(0, endpoint, resolve));
await listen(server);
await listen(proxy);
let secret;
try {
    await new Promise((resolve, reject) => {
        const client = tls.connect({ ...options, host: endpoint, port: proxy.address().port,
            ca: cert, servername: 'demo.ouros.test', rejectUnauthorized: true });
        client.on('keylog', line => {
            const [label, , hex] = line.toString().trim().split(' ');
            if (label === 'SERVER_HANDSHAKE_TRAFFIC_SECRET') secret = Buffer.from(hex, 'hex');
        });
        client.on('error', reject);
        client.on('close', resolve);
    });
} finally { proxy.close(); server.close(); }
assert.equal(secret?.length, 32);
function records(chunks) {
    const bytes = Buffer.concat(chunks), result = [];
    for (let at = 0; at < bytes.length;) {
        const end = at + 5 + bytes.readUInt16BE(at + 3);
        assert(end <= bytes.length);
        result.push(bytes.subarray(at, end)); at = end;
    }
    return result;
}
// HKDF-Expand for one digest block, with the TLS 1.3 label encoding.
function expand(label, length) {
    const text = Buffer.from(`tls13 ${label}`);
    return createHmac('sha256', secret).update(Buffer.concat([Buffer.from([0, length, text.length]), text,
        Buffer.from([0, 1])])).digest().subarray(0, length);
}
const key = expand('key', 16), iv = expand('iv', 12);
const clientHello = records(frontend)[0];
const flight = [];
let sequence = 0n, finished = false;
for (const record of records(backend)) {
    if (record[0] === 22) { flight.push(record.subarray(5)); continue; }
    if (record[0] !== 23) continue;
    const nonce = Buffer.from(iv), seq = Buffer.alloc(12);
    seq.writeBigUInt64BE(sequence++, 4);
    for (let i = 0; i < nonce.length; ++i) nonce[i] ^= seq[i];
    const decipher = createDecipheriv('aes-128-gcm', key, nonce);
    decipher.setAAD(record.subarray(0, 5)); decipher.setAuthTag(record.subarray(-16));
    const plain = Buffer.concat([decipher.update(record.subarray(5, -16)), decipher.final()]);
    let end = plain.length - 1;
    while (end >= 0 && plain[end] === 0) --end;
    assert.equal(plain[end], 22);
    flight.push(plain.subarray(0, end));
    const gathered = Buffer.concat(flight);
    for (let at = 0; at + 4 <= gathered.length;) {
        const next = at + 4 + gathered.readUIntBE(at + 1, 3);
        if (next > gathered.length) break;
        if (gathered[at] === 20) { finished = true; break; }
        at = next;
    }
    if (finished) break;
}
assert(finished && clientHello[0] === 22);
const transcript = Buffer.concat([clientHello.subarray(5), ...flight]);
const types = [];
let certificatePin;
for (let at = 0; at < transcript.length;) {
    const type = transcript[at], end = at + 4 + transcript.readUIntBE(at + 1, 3);
    assert(end <= transcript.length); types.push(type);
    if (type === 11) {
        const start = at + 11, size = transcript.readUIntBE(at + 8, 3);
        certificatePin = createHash('sha256').update(transcript.subarray(start, start + size)).digest('hex');
    }
    if (type === 20) {
        const hash = createHash('sha256').update(transcript.subarray(0, at)).digest();
        const verify = createHmac('sha256', expand('finished', 32)).update(hash).digest();
        assert(verify.equals(transcript.subarray(at + 4, end)));
    }
    at = end;
}
assert.deepEqual(types, [1, 2, 8, 11, 15, 20]);
function array(name, bytes) {
    const rows = [];
    for (let i = 0; i < bytes.length; i += 12) {
        rows.push('    ' + Array.from(bytes.subarray(i, i+12), b => `0x${b.toString(16).padStart(2, '0')}`).join(',') + ',');
    }
    return `static const unsigned char ${name}[]={\n${rows.join('\n')}\n};\n`;
}
writeFileSync(output, '/* Disposable, authenticated loopback TLS 1.3 capture. Public fixture only. */\n' +
    '#ifndef OS_TLS13_DEMO_H\n#define OS_TLS13_DEMO_H\n' +
    array('tls13_demo_transcript', transcript) + array('tls13_demo_secret', secret) +
    `#define TLS13_DEMO_PIN "${certificatePin}"\n#endif\n`);
console.log(`${transcript.length} handshake bytes, certificate SHA-256 ${certificatePin}`);

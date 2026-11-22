import { X509Certificate } from 'node:crypto';
import { readFileSync, unlinkSync } from 'node:fs';
import tls from 'node:tls';

const [, , host, peer, portText, timeoutText, certPath, keyPath] = process.argv;
const cert = readFileSync(certPath);
const key = readFileSync(keyPath);
unlinkSync(keyPath);
const sockets = new Set();
const message = Buffer.from('ouros-tls');
let passed = false;
let timer;

const server = tls.createServer({
    cert, key, minVersion: 'TLSv1.3', maxVersion: 'TLSv1.3', ecdhCurve: 'prime256v1',
    ciphers: 'TLS_AES_128_GCM_SHA256', sigalgs: 'ecdsa_secp256r1_sha256', handshakeTimeout: 65000
}, socket => {
    socket.setNoDelay(true);
    let received = Buffer.alloc(0);
    socket.on('error', error => console.error(error.message));
    socket.on('data', chunk => {
        if (received.length + chunk.length > message.length) {
            socket.destroy();
            return;
        }
        received = Buffer.concat([received, chunk]);
        if (received.length !== message.length) return;
        if (!received.equals(message) || socket.servername !== 'demo.ouros.test') {
            socket.destroy();
            return;
        }
        socket.write(received.subarray(0, 3));
        setTimeout(() => socket.end(received.subarray(3)), 25);
    });
    socket.on('end', () => {
        if (!received.equals(message) || passed) return;
        passed = true;
        console.log(`PASS: authenticated TLSv1.3 client Finished and echoed ${received.length} encrypted bytes.`);
        clearTimeout(timer);
        server.close();
        for (const other of sockets) if (other !== socket) other.destroy();
    });
});
key.fill(0);
server.on('connection', socket => {
    if (socket.remoteAddress !== peer) { socket.destroy(); return; }
    sockets.add(socket);
    socket.on('close', () => sockets.delete(socket));
});
server.on('tlsClientError', (error, socket) => {
    if (socket.remoteAddress === peer) console.error(`Connection ended during TLS handshake: ${error.message}`);
});
server.on('error', error => {
    clearTimeout(timer);
    console.error(error.message);
    process.exitCode = 1;
});
server.listen(Number(portText), host, () => {
    const pin = new X509Certificate(cert).fingerprint256.replaceAll(':', '').toLowerCase();
    console.log(`Waiting for ${peer} on ${host}:${server.address().port}`);
    console.log(`trustadd ${pin}`);
    timer = setTimeout(() => {
        console.error('TLS echo timed out.');
        process.exitCode = 1;
        server.close();
        for (const socket of sockets) socket.destroy();
    }, Number(timeoutText) * 1000);
});

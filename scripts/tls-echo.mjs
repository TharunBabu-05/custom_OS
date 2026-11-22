import { createHash, X509Certificate } from 'node:crypto';
import { readFileSync, statSync, unlinkSync } from 'node:fs';
import tls from 'node:tls';

const [, , host, peer, portText, timeoutText, certPath, keyPath, packagePath] = process.argv;
const cert = readFileSync(certPath);
const key = readFileSync(keyPath);
unlinkSync(keyPath);
const sockets = new Set();
let packageFile;
if (packagePath) {
    const info = statSync(packagePath);
    if (!info.isFile() || info.size < 64 || info.size > 32768) throw new Error('Package file size outside 64 to 32768 bytes.');
    packageFile = readFileSync(packagePath);
    if (packageFile.length !== info.size) throw new Error('Package file size changed.');
}
const message = Buffer.from(packageFile
    ? 'GET /package.elf HTTP/1.1\r\nHost: demo.ouros.test\r\nConnection: close\r\n\r\n'
    : 'ouros-tls');
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
        if (packageFile) {
            socket.setMaxSendFragment(2048);
            socket.write(`HTTP/1.1 200 OK\r\nContent-Length: ${packageFile.length}\r\nConnection: close\r\n\r\n`);
            socket.end(packageFile);
        } else {
            socket.write(received.subarray(0, 3));
            setTimeout(() => socket.end(received.subarray(3)), 25);
        }
    });
    socket.on('end', () => {
        if (!received.equals(message) || passed) return;
        passed = true;
        if (packageFile) console.log(`PASS: authenticated TLSv1.3 client Finished and served ${packageFile.length} package bytes.`);
        else console.log(`PASS: authenticated TLSv1.3 client Finished and echoed ${received.length} encrypted bytes.`);
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
    if (packageFile) {
        const sha256 = createHash('sha256').update(packageFile).digest('hex');
        console.log(`Package SHA-256 ${sha256}`);
        console.log(`appterm 120 /boot/PKGGET.ELF ${host} ${server.address().port} ${sha256} DLHELLO.ELF`);
    }
    timer = setTimeout(() => {
        console.error('TLS echo timed out.');
        process.exitCode = 1;
        server.close();
        for (const socket of sockets) socket.destroy();
    }, Number(timeoutText) * 1000);
});

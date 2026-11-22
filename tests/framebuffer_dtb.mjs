// Add a synthetic framebuffer to a QEMU-generated DTB for guest tests.
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync } from 'node:fs';

const [, , input, output, profile] = process.argv;
assert(['valid', 'bad-stride'].includes(profile));
const data = readFileSync(input);
assert.equal(data.readUInt32BE(0), 0xd00dfeed);
const structureOffset = data.readUInt32BE(8);
const stringOffset = data.readUInt32BE(12);
const structure = data.subarray(structureOffset, structureOffset + data.readUInt32BE(36));
let strings = data.subarray(stringOffset, stringOffset + data.readUInt32BE(32));
let chosenEnd = -1;
const path = [];
for (let at = 0; at < structure.length;) {
    const start = at;
    const token = structure.readUInt32BE(at);
    at += 4;
    if (token === 1) {
        const end = structure.indexOf(0, at);
        assert(end >= at);
        path.push(structure.toString('ascii', at, end));
        at = (end + 4) & ~3;
    } else if (token === 2) {
        if (path.join('/') === '/chosen') chosenEnd = start;
        path.pop();
    } else if (token === 3) {
        at += 8 + ((structure.readUInt32BE(at) + 3) & ~3);
    } else if (token === 9) {
        break;
    } else {
        assert.equal(token, 4);
    }
}
assert(chosenEnd >= 0);
const words = (...values) => {
    const bytes = Buffer.alloc(values.length * 4);
    values.forEach((value, index) => bytes.writeUInt32BE(value, index * 4));
    return bytes;
};
const string = value => Buffer.from(value + '\0');
const padded = bytes => Buffer.concat([bytes, Buffer.alloc((-bytes.length) & 3)]);
const prop = (name, bytes) => {
    const offset = strings.length;
    strings = Buffer.concat([strings, string(name)]);
    return Buffer.concat([words(3, bytes.length, offset), padded(bytes)]);
};
const node = (name, ...body) => Buffer.concat([words(1), padded(string(name)), ...body, words(2)]);
const base = 0x46000000;
const width = 320;
const height = 192;
const stride = 1288;
const inserted = Buffer.concat([
    node('framebuffer@45000000',
        prop('compatible', string('simple-framebuffer')), prop('status', string('disabled'))),
    node('framebuffer@46000000',
        prop('compatible', string('simple-framebuffer')),
        prop('reg', words(0, base, 0, stride * height)),
        prop('width', words(width)), prop('height', words(height)),
        prop('stride', words(profile === 'bad-stride' ? stride - 16 : stride)),
        prop('format', string('x8r8g8b8')))
]);
const prefix = Buffer.from(data.subarray(0, structureOffset));
const body = Buffer.concat([structure.subarray(0, chosenEnd), inserted, structure.subarray(chosenEnd)]);
prefix.writeUInt32BE(prefix.length + body.length + strings.length, 4);
prefix.writeUInt32BE(prefix.length + body.length, 12);
prefix.writeUInt32BE(strings.length, 32);
prefix.writeUInt32BE(body.length, 36);
writeFileSync(output, Buffer.concat([prefix, body, strings]));

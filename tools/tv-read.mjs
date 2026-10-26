#!/usr/bin/env node
// Read this app's diagnostics/screenshots using the existing ares pairing.
// The passphrase and SSH private key stay in memory and never enter argv/logs.
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { createRequire } from 'node:module';
import { execFileSync } from 'node:child_process';

const [deviceName, remotePath, destination] = process.argv.slice(2);
if (!deviceName || !destination || !/^\/media\/developer\/temp\/immich-tv-[a-z0-9.-]+$/.test(remotePath || '')) {
  throw new Error('Usage: node tools/tv-read.mjs <ares-device> /media/developer/temp/immich-tv-<artifact> <local-file>');
}
const cliHome = process.env.WEBOS_CLI_HOME || path.join(execFileSync('npm', ['root', '-g'], { encoding: 'utf8' }).trim(), '@webos-tools/cli');
const require = createRequire(path.join(cliHome, 'package.json'));
const { Client } = require('ssh2');
const devices = JSON.parse(fs.readFileSync(path.join(os.homedir(), '.webos/tv/novacom-devices.json'), 'utf8'));
const device = devices.find(d => d.name === deviceName && d.profile === 'tv');
if (!device || !device.privateKey?.openSsh) throw new Error('Pair the television with ares first.');
const client = new Client();
await new Promise((resolve, reject) => {
  client.on('error', reject);
  client.on('ready', () => {
    client.exec(`cat '${remotePath}'`, (error, stream) => {
      if (error) { client.end(); reject(error); return; }
      const chunks = [];
      stream.on('data', chunk => chunks.push(chunk));
      stream.on('close', code => {
        client.end();
        if (code !== 0) { reject(new Error('Television artifact is not readable.')); return; }
        fs.mkdirSync(path.dirname(path.resolve(destination)), { recursive: true });
        fs.writeFileSync(destination, Buffer.concat(chunks));
        resolve();
      });
    });
  });
  client.connect({ host: device.host, port: Number(device.port), username: device.username,
    privateKey: fs.readFileSync(path.isAbsolute(device.privateKey.openSsh) ? device.privateKey.openSsh : path.join(os.homedir(), '.ssh', device.privateKey.openSsh)),
    passphrase: device.passphrase, readyTimeout: 10_000 });
});
console.log(`Saved ${destination}`);

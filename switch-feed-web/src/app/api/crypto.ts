import { createCipheriv, createDecipheriv, createHash, randomBytes } from 'crypto';

// The KV store is a public third-party service keyed by 6-digit PINs, so
// anything written there must be assumed enumerable. The sessionid is
// therefore sealed with AES-256-GCM under a key derived from a server-side
// secret plus the PIN: someone scraping the KV directly gets ciphertext they
// cannot open without SESSION_SECRET, and a leaked secret alone is useless
// without the matching PIN.

const PIN_TTL_MS = 10 * 60 * 1000;

function keyFor(pin: string): Buffer {
  const secret = process.env.SESSION_SECRET;
  if (!secret) {
    throw new Error('SESSION_SECRET is not set');
  }
  return createHash('sha256').update(`${secret}:${pin}`).digest();
}

export function seal(pin: string, sessionid: string): string {
  const iv = randomBytes(12);
  const cipher = createCipheriv('aes-256-gcm', keyFor(pin), iv);
  const payload = JSON.stringify({ sid: sessionid, ts: Date.now() });
  const ct = Buffer.concat([cipher.update(payload, 'utf8'), cipher.final()]);
  return Buffer.concat([iv, cipher.getAuthTag(), ct]).toString('base64url');
}

// Returns the sessionid, or null when the blob is garbage, forged, sealed for
// a different PIN, or older than the 10-minute window.
export function open(pin: string, sealed: string): string | null {
  try {
    const raw = Buffer.from(sealed, 'base64url');
    const iv = raw.subarray(0, 12);
    const tag = raw.subarray(12, 28);
    const ct = raw.subarray(28);
    const decipher = createDecipheriv('aes-256-gcm', keyFor(pin), iv);
    decipher.setAuthTag(tag);
    const payload = JSON.parse(
      Buffer.concat([decipher.update(ct), decipher.final()]).toString('utf8')
    );
    if (typeof payload.sid !== 'string' || typeof payload.ts !== 'number') {
      return null;
    }
    if (Date.now() - payload.ts > PIN_TTL_MS) {
      return null;
    }
    return payload.sid;
  } catch {
    return null;
  }
}

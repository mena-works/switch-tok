import { NextResponse } from 'next/server';
import { getStore } from '../store';

export async function GET(request: Request) {
  const { searchParams } = new URL(request.url);
  const sid = searchParams.get('sid');
  const format = searchParams.get('format');
  
  if (!sid) {
    return NextResponse.json({ error: 'Eksik sessionid' }, { status: 400 });
  }

  // Rastgele 6 haneli PIN üret
  const pin = Math.floor(100000 + Math.random() * 900000).toString();
  
  const store = getStore();
  store.set(pin, { sessionid: sid, timestamp: Date.now() });

  // 10 dakikadan eski PIN'leri temizle
  const tenMinsAgo = Date.now() - 10 * 60 * 1000;
  for (const [key, value] of store.entries()) {
    if (value.timestamp < tenMinsAgo) store.delete(key);
  }

  if (format === 'json') {
    return NextResponse.json({ pin });
  }

  return NextResponse.redirect(new URL(`/success?pin=${pin}`, request.url));
}

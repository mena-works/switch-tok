import { NextResponse } from 'next/server';

export async function GET(request: Request) {
  const { searchParams } = new URL(request.url);
  const sid = searchParams.get('sid');
  const format = searchParams.get('format');
  
  if (!sid) {
    return NextResponse.json({ error: 'Eksik sessionid' }, { status: 400 });
  }

  // Rastgele 6 haneli PIN üret
  const pin = Math.floor(100000 + Math.random() * 900000).toString();
  
  // Vercel Serverless mimarisinde memory (RAM) sıfırlandığı için ücretsiz dış KV API kullanıyoruz
  try {
    await fetch(`https://keyvalue.immanuel.co/api/KeyVal/UpdateValue/switch_tok_pin_store_v2/${pin}/${sid}`, { method: 'POST' });
  } catch (err) {
    console.error('KV Store error:', err);
  }

  if (format === 'json') {
    return NextResponse.json({ pin });
  }

  return NextResponse.redirect(new URL(`/success?pin=${pin}`, request.url));
}

import { NextResponse } from 'next/server';

export async function GET(request: Request) {
  const { searchParams } = new URL(request.url);
  const pin = searchParams.get('pin');
  
  if (!pin) {
    return NextResponse.json({ error: 'Eksik PIN' }, { status: 400 });
  }

  try {
    const response = await fetch(`https://keyvalue.immanuel.co/api/KeyVal/GetValue/switch_tok_pin_store_v2/${pin}`);
    let sid = await response.text();

    // Temizle (tırnak işaretlerini veya boşlukları)
    sid = sid.replace(/"/g, '').trim();

    if (!sid || sid === '' || sid === 'deleted' || sid.includes('Not Found')) {
      return NextResponse.json({ error: 'Geçersiz veya süresi dolmuş PIN' }, { status: 404 });
    }

    // Güvenlik için PIN'i tek kullanımlık yap (deleted olarak işaretle)
    await fetch(`https://keyvalue.immanuel.co/api/KeyVal/UpdateValue/switch_tok_pin_store_v2/${pin}/deleted`, { method: 'POST' });

    return NextResponse.json({ sessionid: sid });
  } catch (err) {
    return NextResponse.json({ error: 'Veritabanı bağlantı hatası' }, { status: 500 });
  }
}

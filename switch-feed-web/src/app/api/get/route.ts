import { NextResponse } from 'next/server';
import { open } from '../crypto';

export async function GET(request: Request) {
  const { searchParams } = new URL(request.url);
  const pin = searchParams.get('pin');

  if (!pin) {
    return NextResponse.json({ error: 'Eksik PIN' }, { status: 400 });
  }

  try {
    const response = await fetch(`https://keyvalue.immanuel.co/api/KeyVal/GetValue/switch_tok_pin_store_v3/${pin}`);
    let sealed = await response.text();

    // Temizle (tırnak işaretlerini veya boşlukları)
    sealed = sealed.replace(/"/g, '').trim();

    if (!sealed || sealed === 'deleted' || sealed.includes('Not Found')) {
      return NextResponse.json({ error: 'Geçersiz veya süresi dolmuş PIN' }, { status: 404 });
    }

    // Şifre çözme; süresi dolmuş veya bozuk blob da geçersiz PIN sayılır
    const sid = open(pin, sealed);
    if (!sid) {
      return NextResponse.json({ error: 'Geçersiz veya süresi dolmuş PIN' }, { status: 404 });
    }

    // Güvenlik için PIN'i tek kullanımlık yap (deleted olarak işaretle)
    await fetch(`https://keyvalue.immanuel.co/api/KeyVal/UpdateValue/switch_tok_pin_store_v3/${pin}/deleted`, { method: 'POST' });

    return NextResponse.json({ sessionid: sid });
  } catch (err) {
    return NextResponse.json({ error: 'Veritabanı bağlantı hatası' }, { status: 500 });
  }
}

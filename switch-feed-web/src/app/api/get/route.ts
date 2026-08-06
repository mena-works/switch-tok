import { NextResponse } from 'next/server';
import { getStore } from '../store';

export async function GET(request: Request) {
  const { searchParams } = new URL(request.url);
  const pin = searchParams.get('pin');
  
  if (!pin) {
    return NextResponse.json({ error: 'Eksik PIN' }, { status: 400 });
  }

  const store = getStore();
  const data = store.get(pin);

  if (!data) {
    return NextResponse.json({ error: 'Geçersiz veya süresi dolmuş PIN' }, { status: 404 });
  }

  // PIN tek kullanımlık olmalı
  store.delete(pin);

  return NextResponse.json({ sessionid: data.sessionid });
}

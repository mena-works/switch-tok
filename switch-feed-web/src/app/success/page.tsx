'use client';
import { useSearchParams } from 'next/navigation';
import { Suspense } from 'react';

function SuccessContent() {
  const searchParams = useSearchParams();
  const pin = searchParams.get('pin');

  if (!pin) {
    return (
      <main className="card">
        <div className="wordmark">Switch-Tok</div>
        <h1 className="title error-title">Hata</h1>
        <p className="success-text">Geçerli bir PIN kodu bulunamadı.</p>
      </main>
    );
  }

  return (
    <main className="card">
      <div className="wordmark">Switch-Tok</div>
      <h1 className="title">Giriş başarılı</h1>
      <p className="success-text">
        Switch&apos;te <strong>Y</strong> menüsünden &quot;Giriş Yap (TikTok
        PIN)&quot; seçeneğine girip bu kodu yaz:
      </p>
      <div className="pin-display">{pin}</div>
      <p className="footnote">
        Bu PIN 10 dakika geçerlidir ve tek kullanımlıktır.
      </p>
    </main>
  );
}

export default function SuccessPage() {
  return (
    <div className="container">
      <Suspense fallback={<div className="card">Yükleniyor...</div>}>
        <SuccessContent />
      </Suspense>
    </div>
  );
}

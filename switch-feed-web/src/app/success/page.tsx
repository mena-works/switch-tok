'use client';
import { useSearchParams } from 'next/navigation';
import { Suspense } from 'react';

function SuccessContent() {
  const searchParams = useSearchParams();
  const pin = searchParams.get('pin');

  if (!pin) {
    return (
      <div className="glass-card">
        <h1 className="title" style={{ color: '#ef4444' }}>Hata!</h1>
        <p className="success-text">Geçerli bir PIN kodu bulunamadı.</p>
      </div>
    );
  }

  return (
    <div className="glass-card">
      <h1 className="title">Giriş Başarılı!</h1>
      <p className="success-text">Nintendo Switch uygulamanıza girmeniz gereken PIN Kodu:</p>
      <div className="pin-display">
        {pin}
      </div>
      <p style={{ color: '#94a3b8', fontSize: '0.9rem' }}>Bu PIN kodunun geçerlilik süresi 10 dakikadır ve tek kullanımlıktır.</p>
    </div>
  );
}

export default function SuccessPage() {
  return (
    <div className="container">
      <Suspense fallback={<div className="glass-card">Yükleniyor...</div>}>
        <SuccessContent />
      </Suspense>
    </div>
  );
}

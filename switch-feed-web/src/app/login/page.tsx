'use client';
import { useEffect, useState } from 'react';

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
      <main className="glass-card">
        <h1 className="title">TikTok Session Bridge</h1>
        <p className="subtitle">Switch uygulamanız için tek tıkla oturum taşıma aracı</p>
        
        <div style={{ margin: '3rem 0', display: 'flex', flexDirection: 'column', gap: '1rem', alignItems: 'center' }}>
          <a href={bookmarklet} className="bookmarklet-btn">
            Tıkla ve Yer İmlerine Ekle (PC için)
          </a>
          
          <button 
            onClick={() => {
              navigator.clipboard.writeText(bookmarklet);
              setCopied(true);
              setTimeout(() => setCopied(false), 3000);
            }}
            style={{ 
              background: 'transparent', 
              border: '1px solid var(--accent)', 
              color: 'var(--accent)',
              padding: '0.75rem 1.5rem',
              borderRadius: '9999px',
              cursor: 'pointer',
              fontWeight: 'bold'
            }}
          >
            {copied ? 'Kopyalandı! ✅' : 'Kodu Kopyala (Telefondan Yapanlar İçin)'}
          </button>
        </div>

        <div className="instructions">
          <h3>Nasıl Kullanılır?</h3>
          
          <h4 style={{ color: '#e2e8f0', marginTop: '1rem' }}>📱 Telefondaysanız (Önerilen Yöntem):</h4>
          <ol>
            <li>Yukarıdaki <strong>"Kodu Kopyala"</strong> butonuna basıp kodu hafızaya alın.</li>
            <li>Tarayıcınızda şu an bulunduğunuz bu sayfayı yer imlerine (favorilere) ekleyin.</li>
            <li>Favoriler menünüze girip kaydettiğiniz bu yer imini <strong>düzenleyin</strong> (Adını "TikTok Login" yapın).</li>
            <li>URL / Web Adresi kısmını tamamen silip kopyaladığınız kodu yapıştırın ve kaydedin.</li>
            <li>Yeni bir sekme açıp <a href="https://www.tiktok.com" target="_blank" rel="noreferrer" style={{color: '#60a5fa'}}>tiktok.com</a> adresine gidin ve hesabınıza giriş yapın.</li>
            <li>Adres çubuğuna tıklayıp "TikTok Login" yazın ve çıkan yer iminize dokunun (veya direkt yer imleri menüsünden tıklayın). PIN kodunuz görünecektir!</li>
          </ol>

          <h4 style={{ color: '#e2e8f0', marginTop: '1.5rem' }}>💻 Bilgisayardaysanız:</h4>
          <ol>
            <li>Yukarıdaki mavi butonu basılı tutup <strong>Yer İmlerinize (Sık Kullanılanlar Çubuğuna)</strong> sürükleyin.</li>
            <li><a href="https://www.tiktok.com" target="_blank" rel="noreferrer" style={{color: '#60a5fa'}}>tiktok.com</a> adresine gidip giriş yapın.</li>
            <li>TikTok açıkken favoriler çubuğuna eklediğiniz butona tıklayın, size hemen 6 haneli PIN'i verecektir!</li>
          </ol>
        </div>
      </main>
    </div>
  );
}

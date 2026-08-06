'use client';
import { useEffect, useState } from 'react';

export default function Home() {
  const [bookmarklet, setBookmarklet] = useState('');

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
        
        <div style={{ margin: '3rem 0' }}>
          <a href={bookmarklet} className="bookmarklet-btn">
            Tıkla ve Yer İmlerine Ekle (TikTok Login)
          </a>
        </div>

        <div className="instructions">
          <h3>Nasıl Kullanılır?</h3>
          <ol>
            <li>Yukarıdaki mavi butonu basılı tutup <strong>Yer İmlerinize (Sık Kullanılanlar)</strong> sürükleyin veya direkt tıklayarak favorilerinize ekleyin.</li>
            <li>Bu sekmede işimiz bitti. Yeni bir sekme açıp <a href="https://www.tiktok.com" target="_blank" rel="noreferrer" style={{color: '#60a5fa'}}>tiktok.com</a> adresine gidin.</li>
            <li>Hesabınıza giriş yapın (Eğer zaten girdiyseniz bu adımı atlayın).</li>
            <li>TikTok açıkken, yer imlerinize (favorilere) kaydettiğiniz <strong>"TikTok Login"</strong> bağlantısına tıklayın.</li>
            <li>Sizi tekrar bu siteye yönlendirecek ve size <strong>6 Haneli bir PIN Kodu</strong> verecek!</li>
          </ol>
        </div>
      </main>
    </div>
  );
}

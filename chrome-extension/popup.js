document.getElementById('loginBtn').addEventListener('click', async () => {
  const statusDiv = document.getElementById('status');
  const resultDiv = document.getElementById('result');
  const loginBtn = document.getElementById('loginBtn');
  
  statusDiv.textContent = 'Çerez aranıyor...';
  resultDiv.textContent = '';
  
  try {
    // Eklenti yetkisiyle HttpOnly cookie'yi okuyoruz
    const cookie = await chrome.cookies.get({ url: 'https://www.tiktok.com', name: 'sessionid' });
    
    if (!cookie) {
      statusDiv.innerHTML = '<span style="color:#fe2c55">Hata: TikTok sessionid bulunamadı. Lütfen tiktok.com adresinden hesabınıza giriş yapın.</span>';
      return;
    }
    
    statusDiv.textContent = 'Vercel API\'ye bağlanılıyor...';
    
    // Cookie'yi Vercel'a atıp PIN alıyoruz
    const response = await fetch(`https://tok.menaworks.xyz/api/save?sid=${cookie.value}`);
    const data = await response.json();
    
    if (data.pin) {
      statusDiv.innerHTML = 'Başarılı! Switch uygulamasında <b>Sağ Analoğa (R3)</b> basarak bu PIN kodunu girin:';
      resultDiv.textContent = data.pin;
      loginBtn.style.display = 'none';
    } else {
      statusDiv.textContent = 'Sunucudan geçersiz yanıt alındı.';
    }
  } catch (err) {
    statusDiv.textContent = 'Hata: ' + err.message;
  }
});

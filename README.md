# switch-feed

Nintendo Switch (Tegra X1 / Atmosphère) için dikey kaydırmalı video akışı istemcisi.
UI için **borealis**, oynatma için **libmpv**, besleme için **kendi sunucun**.

## Mimari

Yardımcı makine yok. `.nro` kendi kendine yeter:

```
  Switch (.nro)
        |
        |  1. HTTPS GET  https://www.tikwm.com/api/feed/list?region=..&count=..
        |     -> JSON: her videoda filigransız doğrudan mp4 URL'i
        |     libcurl + mbedtls, CA bundle romfs:/cacert.pem
        |
        |  2. mpv o URL'i doğrudan CDN'den stream eder
        v
  borealis : UI, girdi, fokus
  libmpv   : demux, decode, A/V sync, ses, cache
```

### Neden bu yol, doğrudan tiktok.com değil

Ölçtüm (Ağustos 2026):

| Uç | Sonuç |
|---|---|
| `www.tiktok.com/@user` (tam tarayıcı header'ları ile) | 1462 B **WAF challenge** — `SlardarWAF`, `_wafchallengeid` |
| `www.tiktok.com/oembed` | 200, gerçek JSON (ama oynatma URL'i yok) |
| `api16-normal-*.tiktokv.com/aweme/v1/feed` | 429 ratelimit — WAF yok, ama cihaz kaydı + imza ister |
| `tikwm.com/api/feed/list` | **200, doğrudan mp4 URL'leri** |

Kontrol olarak aynı makineden google/instagram/devkitpro tam sayfa dönüyor,
yani bu bir IP itibarı sorunu değil: tiktok.com tarayıcı olmayan istemcileri
WAF katmanında kesiyor. O challenge `navigator`, `screen`, canvas/WebGL
üzerinden fingerprint istiyor; konsolda JS motoru (QuickJS) çalıştırmak bunu
çözmez, çünkü challenge tam olarak o yüzeylerin sahteliğini ölçüyor.

CDN tarafında ölçülenler: **H.264 + AAC**, **720x1280**, `moov` `mdat`'tan önce
(faststart), ve **çıplak GET yetiyor** — Referer, cookie, User-Agent hiçbiri
gerekmiyor.

### Bunun bedeli

`tikwm` üçüncü partinin ücretsiz servisi. Rate limit yer, düşebilir, şemasını
değiştirebilir. Kod bunu istisna değil normal işletme koşulu sayıyor: HTTP 200
içinde gelen `code != 0` durumu ayrıca kontrol ediliyor ve ekrana hata olarak
düşüyor. Servis kalıcı olarak giderse
[source/feed_activity.cpp](source/feed_activity.cpp) içindeki tek parse
fonksiyonu değişir; oynatma zinciri etkilenmez.

## Neden libmpv, neden elle ffmpeg değil

devkitPro'nun stock `switch-ffmpeg` / `switch-mpv` paketleri **ağdan video
oynatamıyor**. wiliwili (Switch üzerinde çalışan üçüncü parti Bilibili
istemcisi) bu yüzden kendi derlediği paketleri dağıtıyor ve bu build'ler
hazır. Elle ffmpeg pipeline'ı yazmak; A/V sync, ses underrun'ları, seek,
demuxer cache ve ağ stall'larını sıfırdan çözmek demekti — mpv bunların hepsini
zaten çözüyor. `loop-file=inf` sayesinde TikTok tarzı sonsuz döngü de bedava.

## Ön koşullar

### 1. devkitPro

Windows'ta grafik installer (`devkitProUpdater`) **sessiz modda çalışmaz** —
`/S` ile hiçbir bileşen seçilmediği için exit 0 döner ve hiçbir şey kurmaz.
Ya wizard'ı elle tıklayarak geç, ya da manuel kur:

```sh
# MSYS2 tabanını indir (sürüm numarası devkitProUpdate.ini'den gelir)
curl -LO https://downloads.devkitpro.org/msys-2.10.0.1.7z
7z x msys-2.10.0.1.7z -oC:\devkitPro
```

Sonra `C:\devkitPro\msys2\etc\fstab` içindeki şablon satırını gerçek yola
çevir — installer normalde bunu yapar, manuel kurulumda atlanır:

```
# map devkitPro to /opt
C:/devkitPro	/opt/devkitpro
```

Dosyayı **BOM'suz** yaz, yoksa her shell açılışında
`fstab_read_flags: invalid fstab option` uyarısı alırsın.

Paketler `/opt/devkitpro`'ya kurulur; mount sayesinde Windows'tan
`C:\devkitPro\` görünür.

### 2. Paketler

```sh
pacman-key --init && pacman-key --populate msys2 devkitPro
pacman -Syu --noconfirm
pacman -S --needed --noconfirm \
    switch-dev switch-cmake switch-glfw switch-glm \
    switch-curl switch-mbedtls switch-libwebp \
    switch-libplacebo ninja
```

`switch-libplacebo` şart: `libmpv.a` ona ~107 tanımsız sembolle bağlı ama
`switch-ffmpeg` bağımlılık olarak çekmiyor, çünkü ffmpeg'in ihtiyacı yok.
Kurulmazsa `mpv.pc` çözülmez ve configure aşamasında patlar.

### 3. Özel mpv/ffmpeg

Stock devkitPro ffmpeg/mpv **ağdan video oynatmaz**. wiliwili'nin `v0.1.0`
etiketindeki sabit asset release'inden al (son release'lerde yoklar):

```sh
BASE=https://github.com/xfangfang/wiliwili/releases/download/v0.1.0/
curl -LO ${BASE}switch-ffmpeg-7.1-1-any.pkg.tar.zst
curl -LO ${BASE}switch-libmpv-0.36.0-3-any.pkg.tar.zst
pacman -U --noconfirm switch-ffmpeg-*.pkg.tar.zst switch-libmpv-*.pkg.tar.zst
```

Kurulumda çıkan `._` dosyası ve "Cannot restore extended attributes"
uyarıları zararsız (tarball'daki macOS artıkları + NTFS xattr desteklemiyor).

### 4. borealis

```sh
git submodule update --init --recursive
```

## Derleme

**Ninja kullan, make değil.** devkitA64 native bir Windows binary'si ve
depfile'lara `C:/...` yazıyor; MSYS'teki make bunları göreli sanıp
`lib/borealis/library/C:/Users/...` gibi hedefler üretiyor ve sürücü harfindeki
iki nokta yüzünden `multiple target patterns` ile ölüyor. Ninja depfile'ları
kendi tükettiği için etkilenmiyor.

```sh
cmake -B build -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Switch.cmake \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Çıktı: `build/switch-feed.nro` (~34 MB). Konsola göndermek için:

```sh
nxlink -s build/switch-feed.nro
```

## Ayarlar

[source/config.hpp](source/config.hpp) içinde `kFeedUrl`. `region` ve `count`
parametrelerini değiştirebilirsin; `region` boş bırakılırsa servis
`"region empty"` ile reddediyor.

## Opsiyonel: yerel sunucu

[server/feed_server.py](server/feed_server.py) artık **gerekli değil** — ilk
mimariden kalma. Kendi mp4'lerini LAN üzerinden servis etmek istersen duruyor
(Range destekli, stdlib): `python3 feed_server.py --media ./media --port 8080`,
sonra `kFeedUrl`'i ona çevir ve parse'ı `{"items":[...]}` şemasına döndür.

### Transcode profili (yalnızca yerel sunucu kullanırsan)

Switch 1'de NVDEC bu build'de bağlı değil, yani **her şey yazılım decode**.
Cortex-A57 için baseline profil (CABAC yok, B-frame yok) belirgin fark yaratır:

```sh
ffmpeg -i input.mp4 \
  -vf "scale=-2:960,fps=30" \
  -c:v libx264 -profile:v baseline -level 3.1 -preset slow -crf 24 \
  -maxrate 2500k -bufsize 5000k -pix_fmt yuv420p \
  -c:a aac -b:a 128k -ar 48000 -ac 2 \
  -movflags +faststart \
  media/out.mp4
```

`+faststart` şart: moov atom'u başa alır, yoksa mpv oynatmaya başlamadan önce
dosyanın sonunu çekmek zorunda kalır.

## Kontroller

| Tuş | İşlev |
|-----|-------|
| ↑ / ↓ | önceki / sonraki video |
| ← / → | 5 sn geri / ileri sar |
| A | duraklat / devam |
| X | sessize al |
| L / R | ses azalt / artır |
| Y | feed'i yenile |
| + | çıkış |

Konsolu handheld modda yan çevirirsen görüntü döner ve dikey video tüm ekranı
doldurur.

### Dokunmatik

| Hareket | İşlev |
|---|---|
| Yukarı / aşağı kaydır | sonraki / önceki video |
| Yatay sürükle | zamanda gezinme |
| Dokun | duraklat / devam |

Tek bir `PanAxis::ANY` jest tanımlayıcı kullanılıyor. Ayrı yatay ve dikey
tanımlayıcılar aynı dokunuş için yarışır ve ilk kazanan diğerini kilitler; bunun
yerine ilk 24 pikselden sonra hangi jest olduğuna karar veriliyor. Daha erken
karar vermek, hafif eğik bir kaydırmayı scrub sanıp videoyu atlatıyor.

Scrub sırasında arama altı karede bir gönderiliyor — her karede göndermek
decoder'ı sürekli sıfırlar ve görüntü hiç oturmaz — ama çubuk ve tutamak
parmakla anında hareket ettiği için gecikme hissedilmiyor.

## Sonsuz akış

Servis sayfalama yapmıyor — `cursor` parametresi yok sayılıyor ve **her çağrı
tamamen farklı bir parti** döndürüyor. Ölçüm:

```
1. çağrı: 20 video -> 916180, 705991, 762254...
2. çağrı: 16 video -> 354006, 859412, 459284...   (aynı parametreler, hepsi yeni)
```

Bu yüzden akış basitçe şöyle büyüyor: listenin sonuna 4 video kala arka planda
yeni bir parti çekilip `video_id` ile tekrar edenler elenerek ekleniyor. İleri
gitmek yeni malzemeye girer, geri gitmek başta durur.

Dönen video sayısı `count` ile istenenden az olabiliyor (20 isteyip 16 gelebilir),
o yüzden sabit sayıya güvenme.

## Backend bağımlılığı

Vendor'lanan borealis sürümüne karşı doğrulandı:

- Switch'te varsayılan backend GLFW + OpenGL (`glfw3 EGL glapi drm_nouveau nx`)
- nanovg `NANOVG_GL3_IMPLEMENTATION` ile derleniyor → `nvglCreateImageFromHandleGL3`
- `Box::draw(..., Style style, FrameContext* ctx)` imzası eşleşiyor
- `brls::sync(const std::function<void()>&)` mevcut
- `BRLS_PLATFORM_RESOURCES_PATH` = `romfs:/`

`nvglCreateImageFromHandleGL3` seçimi link aşamasında da doğrulandı — yanlış
varyant olsa undefined reference verirdi.

Bunlar **sadece GLFW/GL backend'i seçiliyken** geçerli. CMakeLists.txt bu yüzden
`BOREALIS_USE_DEKO3D` ve `USE_SDL2` seçeneklerini açıkça OFF'a sabitliyor;
deko3d yoluna geçersen [source/mpv_player.cpp](source/mpv_player.cpp) baştan
yazılmalı (deko3d'de GL FBO yok).

## Donanımda doğrulananlar

Gerçek konsolda (fw 19.0.1, Atmosphère 1.8.0, title takeover) çalıştırıldı:

- borealis + GL ayağa kalkıyor: nouveau / NV120 / GL 4.3 Core / Mesa 20.1.0-rc3
  / GLFW 3.3.4, 1280x720
- **mpv render context borealis'in GL context'i üzerinde kuruluyor** — en riskli
  varsayım buydu, tuttu
- Konsoldan HTTPS: DNS, TCP, TLS el sıkışması, `romfs:/cacert.pem` okuma
- Feed cihazda çekiliyor ve ayrıştırılıyor: ~70 KB, 20 video
- **Video ekrana çiziliyor** (mpv → FBO → nanovg) ve ↑/↓ ile videolar arasında
  gezinme çalışıyor

### Feed'in şekli tekdüze değil

20 videoluk bir örnekte bir kayıtta `author.unique_id` string yerine **object**
geliyordu. `nlohmann`'ın `value(key, default)` fonksiyonu tip uyuşmazlığında
istisna atar, o yüzden tek bozuk kayıt tüm feed'i düşürüyordu.

[source/feed_activity.cpp](source/feed_activity.cpp) içindeki `readString()` her
alanı tipini kontrol ederek okuyor. Üçüncü parti bir kaynakta şema garantisi
yok; tek bir tuhaf kayıt diğer on dokuzunu kaybettirmemeli.

### En-oran: tek yerde ölçekle

FBO videonun **kendi çözünürlüğünde** açılıyor, mpv 1:1 çiziyor, ölçekleme
yalnızca çizim anında bir kez yapılıyor. FBO'yu görünüm boyutunda açarsan mpv
kareyi bir kez sığdırır, sen bir kez daha sığdırırsın; sonuç sünmüş görüntüdür.

### En pahalı ders: `switch_wrapper.c`

borealis bu dosyayı kütüphaneye koymuyor; uygulamanın derlemesi gerekiyor
(CMakeLists'te açıkça ekli). İçindeki `userAppInit()` `pl:u`, `set`, `setsys`,
`romfs`, socket ve `psm`'i başlatıyor.

Unutursan **link hatası almazsın**. Çalışma anında borealis hiç başlatılmamış
font servisinden tampon isteyip nanovg'ye verir, stb_truetype de çöpü
ayrıştırırken çöker. Belirti font gibi görünmez, alakasız bir data abort gibi
görünür.

### Fatal ekranından kaynak satırına

Atmosphère'in hata ekranındaki adresler doğrudan kullanılabilir:

```sh
# offset = PC - "Backtrace - Start Address"
aarch64-none-elf-addr2line -e build/switch-feed.elf -f -C -i 0xE985C
```

Uyarı: `-O2` derlemede backtrace'in bir kısmı bayat yığın artığıdır. Gerçek
çerçeveyi ayırt etmenin yolu, PC'nin **tutarlı bir inline zincirine** çözülmesi.
Kodu değiştirip offset'ler hiç kaymıyorsa, çökme senin dokunduğun yerde değildir.

## Geçiş akıcılığı: klip önbelleği

mpv'nin kendi oynatma listesi ön-çekmesi burada işe yaramıyor: dosya bitmeye
yaklaşınca devreye giriyor, ama `loop-file=inf` yüzünden dosya hiç bitmiyor.
İkisi birbirini iptal ediyor.

Çözüm ön-çekmeyi kendimiz yapmak. Bu libmpv build'i `mpv_stream_cb_add_ro`
içeriyor, yani kendi protokolümüzü kaydedip mpv'ye veriyi RAM'den sunabiliyoruz:
[source/clip_cache.cpp](source/clip_cache.cpp) `feedcache://<video_id>` sağlıyor,
sonraki iki klip libcurl ile arka planda iniyor. Önbellekteki bir klip DNS, TLS
ve CDN gidiş-dönüşü olmadan başlıyor — geçişteki beklemenin neredeyse tamamı bu.

Üç şey donanımda öğrenildi ve tasarımı değiştirdi:

**İndirmeler sırayla olmalı.** İki paralel indirme + mpv'nin kendi akışı,
konsolun wifi'ında yarışıyor. Belirtisi ağ hatası gibi görünmüyordu:
`mbedtls_ssl_read returned -0x0`, ardından `Invalid NAL unit size` — yani
decoder'a yarım kalmış H.264 gidiyordu.

**Boyut sınırı transfer sırasında uygulanmalı.** İndirme bittikten sonra bakmak
işe yaramıyor; bir klip 136 MB çekip zaman aşımına düştü. Chunked yanıtta
`Content-Length` olmadığı için `CURLOPT_MAXFILESIZE` de yetmiyor, yazma geri
çağrısında kesmek gerekiyor.

**Budama indeksle değil kullanımla yapılmalı.** Önce `[şimdiki, +2]` penceresi
tutuluyordu; geri gitmek saniyeler önce inen klibi atıyordu ve aynı dosya yarım
dakikada dört kez indi. Yerine bayt bütçesi (64 MB) ve en az kullanılanı atma
kondu — hangi gezinme deseninde olursa olsun doğru davranıyor.

Önbellek bir hızlanma, bir bağımlılık değil: kaydırma indirmeyi geçerse
`uriFor` sessizce CDN URL'ine döner.

## Login neden yok

Araştırıldı ve ölçüldü (Ağustos 2026). Kişiselleştirilmiş "For You" akışı için
oturum açmanın önündeki engel çerez değil, **imza**:

| Yol | Sonuç |
|---|---|
| Web uçları + çerez | WAF challenge — kapalı |
| Mobil uçlar, tam uygulama parametreleri, imzasız | `429 ratelimit` |
| Çerez tek başına | Yetmiyor; imza oturumdan bağımsız isteniyor |
| Resmî Display API (OAuth) | Çalışır ama yalnızca kendi videoların; akış yok |

Mobil API dört başlığı **birlikte** istiyor: `X-Argus` (protobuf → SM3 → SIMON →
AES), `X-Gorgon`, `X-Ladon`, `X-Khronos`. X-Gorgon tek başına artık yetmiyor.

Bunlar saf algoritma — native `.so` ya da uzak imzalama servisi değil — yani
C++'a taşınabilirler ve AES zaten mbedtls'te var. Yani **teknik olarak mümkün**.
Yapılmama sebebi maliyet: protobuf alan düzenini birebir tutturmak gerekiyor,
algoritmalar dönemsel değişiyor ve değiştiğinde yeniden port gerekiyor, ve
alışılmadık istemci imzaları gerçek hesabın işaretlenmesine yol açabilir.

Yapılırsa doğru yaklaşım oturum çerezini taşımak olur (traktok gibi akademik
araçların yaptığı): giriş tarayıcıda yapılır, `sessionid` SD kartta kalır,
hiçbir üçüncü partiye gönderilmez.

## Bilinen eksikler
- **Decode payı.** Kaynak 720x1280 H.264 ve NVDEC bu build'de bağlı değil, yani
  3 çekirdekle yazılım decode. Kare düşerse sırayla: `vd-lavc-skiploopfilter=all`,
  `framedrop=vo`, ve son çare `--vf=scale` ile küçültme.
- **Akış kopmaları.** Log'da ara ara `mpv/ffmpeg: tls: mbedtls_ssl_read
  returned -0x0` ve ardından `Packet corrupt` / `Invalid NAL unit size`
  görünüyor; mpv yeniden bağlanıp toparlıyor. Önce klip önbelleğinin soket
  yarışına yol açtığı sanıldı, ama **ön-çekme tamamen kapatıldığında da devam
  ediyor** — yani sebep uygulama değil. Aynı ağdaki PC'den (Ethernet) CDN
  sorunsuz cevap veriyor, konsol WiFi'da; fark muhtemelen orada.
- **Ses döngü artığı (kozmetik).** Her sarmada log'a
  `ao/hos: audio end or underrun` → `starting AO` → `Error writing audio to
  device` düşüyor. Duyulabilir ses etkilenmiyor; klip başa sararken ses akışı
  bitiyor, AO yeniden açılıyor ve o anda bir yazma kaybediyor.
  `audio-stream-silence=yes` denendi, **çözmedi** ve mpv'nin kendisi bu
  seçeneğin bazı oynatıcı davranışlarını bozacağını uyarıyor — o yüzden
  kullanılmıyor.
- **romfs şişkinliği.** CMake `lib/borealis/resources/` dizinini olduğu gibi
  kopyalıyor, içinde borealis demo varlıkları da var (`img/pokemon/`,
  `img/tiles.png`, `xml/`). `img/sys/` gerekli (durum çubuğu ikonları), diğerleri
  atılabilir; ~1.9 MB kazandırır.

## Notlar

- Bu bir homebrew projesi: Atmosphère gerektirir, eShop'a giremez, ban riski
  taşır.
- Gerçek bir TikTok kaynağı bağlarsan TikTok'un kullanım şartlarını ihlal
  etmiş olursun. Resmî API (Display API) yalnızca yetki veren kullanıcının
  kendi videolarını döner; "For You" akışı dışarıya açık değildir.
- Referans implementasyon: [wiliwili](https://github.com/xfangfang/wiliwili) —
  aynı yığın (borealis + libmpv), üretim kalitesinde.

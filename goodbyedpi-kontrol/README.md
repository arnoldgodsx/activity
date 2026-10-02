# GoodbyeDPI Kontrol

[GoodbyeDPI Türkiye sürümü 0.2.3rc3](https://github.com/cagritaskn/GoodbyeDPI-Turkey/releases/tag/release-0.2.3rc3-turkey)'ı
**istediğiniz zaman tek tıkla açıp kapatmanızı** sağlayan, tek dosyalık bir Windows uygulaması.

İndir: [`dist/GoodbyeDPI-Kontrol.exe`](dist/GoodbyeDPI-Kontrol.exe) — başka dosya gerekmez
(goodbyedpi.exe, WinDivert.dll ve WinDivert64.sys exe'nin içine gömülüdür).

## Özellikler

- **AÇ / KAPAT** düğmesi; durum yeşil (açık) / kırmızı (kapalı) gösterilir.
- Orijinal paketteki 7 yöntemin hepsi seçilebilir (varsayılan `turkey_dnsredir`, alternatif 1–6).
  Açıkken yöntem değiştirilirse GoodbyeDPI yeni yöntemle yeniden başlatılır.
- Siyah konsol penceresi açılmaz.
- Simge durumuna küçültünce **sistem tepsisine** gizlenir; tepsi simgesine sağ tıklayıp aç/kapat yapılabilir.
- Pencere (X) ile kapatılınca GoodbyeDPI da kapanır. Uygulama çökse bile GoodbyeDPI açık kalmaz.
- Daha önce `service_install_...cmd` ile kurulmuş **GoodbyeDPI Windows hizmetini algılar** ve kaldırmayı önerir
  (GoodbyeDPI'ın sürekli açık kalmasının sebebi genelde bu hizmettir).
- İsteğe bağlı: Windows açılışında tepside başlatma ve uygulama açılınca GoodbyeDPI'ı otomatik açma.

## Kullanım

1. `GoodbyeDPI-Kontrol.exe`'yi çalıştırın (WinDivert sürücüsü için yönetici izni ister).
2. Eski hizmet kuruluysa çıkan soruya **Evet** deyin.
3. **AÇ** ile başlatın, **KAPAT** ile durdurun.

Dosyalar `%LOCALAPPDATA%\GoodbyeDPI-Kontrol` klasörüne çıkarılır; ayarlar ve son çalıştırmanın çıktısı
(`goodbyedpi.log`) da buradadır. Antivirüs WinDivert'i engellerse bu klasörü dışlamalara ekleyin.

Yalnızca 64 bit Windows (7 / 8 / 10 / 11) desteklenir.

## Derleme (Linux)

```sh
sudo apt-get install gcc-mingw-w64-x86-64 binutils-mingw-w64-x86-64 python3 curl unzip
./build.sh   # çıktı: dist/GoodbyeDPI-Kontrol.exe
```

## Lisanslar

GoodbyeDPI (Apache 2.0) ve WinDivert (LGPLv3) lisans metinleri `dist/licenses/` klasöründedir.

// BetaMenu.asi - GTA San Andreas (1.0 US) beta-style shop menu
// Tüm satın alma menüleri (dövme, kıyafet, berber, silah, yemek...) CMenuSystem::DisplayStandardMenu
// üzerinden çizildiği için tek hook hepsini kapsar.
//
// Çalışma mantığı:
//   1) Orijinal fonksiyondan ÖNCE beta görünümünü çizer: siyah kutu + beyaz çerçeve + alt ornament.
//   2) Orijinal fonksiyonu çağırır (yazılar, fiyatlar, seçili satır vurgusu oyunun kendi koduyla çizilir).
//   3) Orijinalin kendi arka planı çizmesin diye m_bColumnBackground geçici olarak kapatılır.
//
// Not: Derlenmemiş/test edilmemiş koddur. Kutu ölçüleri BetaMenu.ini ile ayarlanır (F10 = ini'yi yeniden yükle).

#include "plugin.h"
#include "common.h"
#include "CMenuSystem.h"
#include "CSprite2d.h"
#include "CRect.h"
#include "CRGBA.h"
#include "RenderWare.h"
#include "MinHook.h"
#include "ornament_data.h"   // süs dokusu (sol/sağ yarı), .asi içine gömülü

#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace plugin;

// ---------------------------------------------------------------- ayarlar
struct Config {
    bool  enabled                = true;
    // DİKKAT: true yapılırsa oyun yazıları/başlığı bozuk çiziyor (test edildi). Varsayılan kapalı.
    bool  hideOriginalBackground = false;
    bool  ornament               = true;
    bool  log                    = false;   // tanı logu (BetaMenu.log)
    unsigned lineColor           = 0xE1E1E1; // çerçeve çizgisi ve süs rengi (RRGGBB)

    // Kutu, orijinal panelle AYNI ölçüde. Orijinal panel (1920x1080 ekran görüntüsünden ölçüldü):
    //   sol kenar = posX, üst kenar = posY, genişlik = sütun toplamı + 44 px (=14.67 sanal),
    //   yükseklik = 46 + 15 * satır + 19 * (başlık satırı varsa)  sanal birim
    //   (5 farklı menüden ölçüldü: 9 satır/başlıksız 436 px, 2 satır/başlıksız 184 px,
    //    1 satır+başlık 193 px, 2 satır+başlık 228 px; 1080p'de sy=2.4107).
    // Değerler 640x448 sanal birim; oyun çözünürlüğe göre ölçekler.
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    float extraW  = 14.67f;  // sütun genişlikleri toplamına eklenir
    float extraH  = 0.0f;    // alt pay

    float titleH  = 46.0f;   // satırlardan bağımsız sabit yükseklik (başlık alanı + alt pay)
    float headerH = 19.0f;   // sütun başlığı satırı (sadece başlığı olan panellerde eklenir)
    float rowH    = 15.0f;   // satır aralığı

    // Ekstra kutu koyuluğu (0-255). Orijinal panel zaten ~%74 siyah; ikisi üst üste biner.
    // 0 = orijinalle aynı koyuluk. Sahne parlaklığı kalan oran = 0.255 * (1 - alpha/255)  (ölçüldü).
    int   alpha    = 0;
    float frame    = 2.0f;   // beyaz çerçeve kalınlığı
    float frameGap = 0.0f;   // kutu ile çerçeve arası boşluk (çerçeve kutunun DIŞINDA çizilir)
    float outline  = 2.0f;   // beyaz çerçevenin dışındaki siyah kontur kalınlığı (sanal birim)

    // Süs ölçüleri beta ekran görüntüsünden ölçüldü: yükseklik ~30, yarım parça genişliği ~96,
    // kenarlardan dışarı taşma ~11 sanal birim. Görselin oranı beta'dan daha "yüksek" olduğu için
    // yatayda biraz gerilir (ornStretchX).
    float ornH       = 34.0f;  // süsün yüksekliği (sanal birim)
    float ornStretchX = 1.3f;  // yatay germe çarpanı (1 = görselin kendi oranı)
    float ornX       = 11.0f;  // süsü kenarlardan DIŞARI kaydır (+ dışarı, - içeri)
    float ornY       = 0.0f;   // süsü aşağı kaydır (+ aşağı, - yukarı)
    float ornOutline = 1.0f;   // süsün etrafına eklenen ekstra siyah kontur (sanal birim, 0 = yok)

    // Sol üstteki yardım kutuları (ornamentsiz çerçeve). Varsayılan KAPALI = oyunun orijinal görünümü.
    bool  helpFrame   = false;
    float helpFrameTh = 2.0f;  // beyaz çizgi kalınlığı (sanal birim)
    float helpOutline = 2.0f;  // dış siyah kontur (sanal birim)
};
static Config g_cfg;

static std::string IniPath() {
    char path[MAX_PATH] = {};
    HMODULE h = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&IniPath, &h);
    GetModuleFileNameA(h, path, MAX_PATH);
    std::string s(path);
    size_t dot = s.find_last_of('.');
    return (dot == std::string::npos ? s : s.substr(0, dot)) + ".ini";
}

static float ReadF(const char* key, float def, const std::string& ini) {
    char def_s[32], buf[32];
    sprintf_s(def_s, "%f", def);
    GetPrivateProfileStringA("BetaMenu", key, def_s, buf, sizeof(buf), ini.c_str());
    return (float)atof(buf);
}

static bool ReadB(const char* key, bool def, const std::string& ini) {
    return GetPrivateProfileIntA("BetaMenu", key, def ? 1 : 0, ini.c_str()) != 0;
}

static void LoadConfig() {
    std::string ini = IniPath();
    Config d;
    g_cfg.enabled                = ReadB("Enabled", d.enabled, ini);
    g_cfg.hideOriginalBackground = ReadB("HideOriginalBackground", d.hideOriginalBackground, ini);
    g_cfg.ornament               = ReadB("Ornament", d.ornament, ini);
    g_cfg.log                    = ReadB("Log", d.log, ini);
    {
        // LineColor=E1E1E1 (başında # olabilir)
        char buf[16] = {};
        GetPrivateProfileStringA("BetaMenu", "LineColor", "E1E1E1", buf, sizeof(buf), ini.c_str());
        const char* s = (buf[0] == '#') ? buf + 1 : buf;
        g_cfg.lineColor = (unsigned)strtoul(s, nullptr, 16) & 0xFFFFFF;
    }
    g_cfg.offsetX  = ReadF("OffsetX", d.offsetX, ini);
    g_cfg.offsetY  = ReadF("OffsetY", d.offsetY, ini);
    g_cfg.extraW   = ReadF("ExtraWidth", d.extraW, ini);
    g_cfg.extraH   = ReadF("ExtraHeight", d.extraH, ini);
    g_cfg.titleH   = ReadF("TitleHeight", d.titleH, ini);
    g_cfg.headerH  = ReadF("HeaderHeight", d.headerH, ini);
    g_cfg.rowH     = ReadF("RowHeight", d.rowH, ini);
    g_cfg.alpha    = GetPrivateProfileIntA("BetaMenu", "BoxAlpha", d.alpha, ini.c_str());
    g_cfg.frame    = ReadF("FrameThickness", d.frame, ini);
    g_cfg.frameGap = ReadF("FrameGap", d.frameGap, ini);
    g_cfg.outline  = ReadF("OutlineThickness", d.outline, ini);
    g_cfg.ornH     = ReadF("OrnamentHeight", d.ornH, ini);
    g_cfg.ornStretchX = ReadF("OrnamentStretchX", d.ornStretchX, ini);
    g_cfg.ornX     = ReadF("OrnamentOffsetX", d.ornX, ini);
    g_cfg.ornY     = ReadF("OrnamentOffsetY", d.ornY, ini);
    g_cfg.ornOutline = ReadF("OrnamentOutline", d.ornOutline, ini);
    g_cfg.helpFrame   = ReadB("HelpBoxFrame", d.helpFrame, ini);
    g_cfg.helpFrameTh = ReadF("HelpBoxFrameThickness", d.helpFrameTh, ini);
    g_cfg.helpOutline = ReadF("HelpBoxOutlineThickness", d.helpOutline, ini);
}

// ---------------------------------------------------------------- çizim yardımcıları
// GTA'da CRect sırası: (sol, alt, sağ, üst). Ekranda alt > üst (y aşağı artar).
static void Rect(float l, float t, float r, float b, const CRGBA& c) {
    CSprite2d::DrawRect(CRect(l, b, r, t), c);
}

static void FrameRect(float l, float t, float r, float b, float th, const CRGBA& c) {
    Rect(l, t, r, t + th, c);
    Rect(l, b - th, r, b, c);
    Rect(l, t, l + th, b, c);
    Rect(r - th, t, r, b, c);
}

// Beta ekran görüntüsündeki alt süs: ortada ince çizgi, iki uçta kıvrım benzeri yığılmış bloklar,
// ortada küçük elmas. Gerçek doku yerine DrawRect ile çizilir.
// cy = süsün dikey ortası (alt çerçeve çizgisinin ortasına verilir).
static void DrawOrnament(float l, float r, float cy, float sx, float sy, const CRGBA& c) {
    float cx = (l + r) * 0.5f;
    float th = 1.5f * sy;

    // ince ana çizgi (ortada boşluklu)
    Rect(l + 26.0f * sx, cy - th * 0.5f, cx - 14.0f * sx, cy + th * 0.5f, c);
    Rect(cx + 14.0f * sx, cy - th * 0.5f, r - 26.0f * sx, cy + th * 0.5f, c);

    // uç kıvrımları (basamaklı bloklar)
    struct Step { float w, h; };
    const Step steps[] = { {22.f, 2.f}, {16.f, 5.f}, {10.f, 8.f} };
    for (const Step& s : steps) {
        Rect(l,                 cy - s.h * 0.5f * sy, l + s.w * sx, cy + s.h * 0.5f * sy, c);
        Rect(r - s.w * sx,      cy - s.h * 0.5f * sy, r,            cy + s.h * 0.5f * sy, c);
    }

    // orta elmas
    const Step dia[] = { {2.f, 8.f}, {6.f, 6.f}, {10.f, 4.f}, {6.f, 6.f}, {2.f, 8.f} };
    float x = cx - 10.0f * sx;
    float wsum[] = { 4.f, 4.f, 4.f, 4.f, 4.f };
    for (int i = 0; i < 5; ++i) {
        Rect(x, cy - dia[i].h * 0.5f * sy, x + wsum[i] * sx, cy + dia[i].h * 0.5f * sy, c);
        x += wsum[i] * sx;
    }
}

// ---------------------------------------------------------------- süs dokusu
// ornament_data.h içindeki RLE verisini (gri+alfa) RenderWare dokusuna açar. D3D9 8888 formatı bellekte BGRA'dır.
static CSprite2d* g_ornL = nullptr;
static CSprite2d* g_ornR = nullptr;

static RwTexture* CreateOrnamentTexture(const unsigned char* rle, size_t len) {
    RwRaster* ras = RwRasterCreate(kOrnTexW, kOrnTexH, 32, rwRASTERTYPETEXTURE | rwRASTERFORMAT8888);
    if (!ras) return nullptr;
    RwUInt8* dst = RwRasterLock(ras, 0, rwRASTERLOCKWRITE);
    if (!dst) { RwRasterDestroy(ras); return nullptr; }

    const size_t stride = ras->stride > 0 ? (size_t)ras->stride : (size_t)kOrnTexW * 4;
    const size_t total  = (size_t)kOrnTexW * kOrnTexH;
    size_t pix = 0;
    for (size_t i = 0; i + 2 < len && pix < total; i += 3) {
        const int cnt = rle[i];
        const unsigned char L = rle[i + 1], A = rle[i + 2];
        for (int k = 0; k < cnt && pix < total; ++k, ++pix) {
            RwUInt8* d = dst + (pix / kOrnTexW) * stride + (pix % kOrnTexW) * 4;
            d[0] = L; d[1] = L; d[2] = L; d[3] = A;
        }
    }
    RwRasterUnlock(ras);

    RwTexture* tex = RwTextureCreate(ras);
    if (!tex) { RwRasterDestroy(ras); return nullptr; }
    RwTextureSetFilterMode(tex, rwFILTERLINEAR);
    RwTextureSetAddressing(tex, rwTEXTUREADDRESSCLAMP);
    return tex;
}

static void CreateOrnamentSprites() {
    if (g_ornL && g_ornR) return;
    RwTexture* tl = CreateOrnamentTexture(kOrnLeftRle,  sizeof(kOrnLeftRle));
    RwTexture* tr = CreateOrnamentTexture(kOrnRightRle, sizeof(kOrnRightRle));
    if (!tl || !tr) return;                      // olmazsa DrawRect süsü (yedek) kullanılır
    // CSprite2d'yi bilerek silmiyoruz (oyun kapanırken yıkıcı çağrılırsa çökme riski olmasın).
    g_ornL = new CSprite2d(); g_ornL->m_pTexture = tl;
    g_ornR = new CSprite2d(); g_ornR->m_pTexture = tr;
}

// Süs iki yarım parçadır: sol yarı sol köşeye, sağ yarı sağ köşeye. Parçaların "kuyruk çizgisi"
// (kOrnLineV) alt çerçeve çizgisinin ortasına oturur; aradaki boşlukta çerçevenin kendi çizgisi görünür.
static bool DrawOrnamentTextured(float fl, float fr, float cy, float sx, float sy, const CRGBA& white) {
    if (!g_ornL || !g_ornR) return false;

    float h = g_cfg.ornH * sy;
    float w = h / kOrnHalfAspect * g_cfg.ornStretchX;   // yarım parçanın piksel genişliği
    // Dar menülerde (2 sütunlu paneller) iki parça çakışmasın: aralarında en az %16 boşluk kalsın
    // (beta'da boşluk toplam genişliğin ~%15.5'i). Taşma payı da genişliğe dahildir.
    const float span = (fr - fl) + 2.0f * g_cfg.ornX * sx;
    if (2.0f * w > span * 0.84f) { const float k = span * 0.84f / (2.0f * w); w *= k; h *= k; }

    const float top = cy - kOrnLineV * h + g_cfg.ornY * sy;
    const float bot = top + h;
    const float lx  = fl - g_cfg.ornX * sx;
    const float rx  = fr + g_cfg.ornX * sx;

    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATESRCBLEND,  (void*)rwBLENDSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);

    // Ekstra siyah kontur: sprite siyaha boyanıp 8 yöne kaydırılarak çizilir (doku değişmez).
    const float oo = g_cfg.ornOutline * sy;
    if (oo > 0.0f) {
        static const float dx[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
        static const float dy[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
        const CRGBA blk(0, 0, 0, 255);
        for (int i = 0; i < 8; ++i) {
            const float ox = dx[i] * oo, oy = dy[i] * oo;
            g_ornL->Draw(CRect(lx + ox,     bot + oy, lx + w + ox, top + oy), blk);
            g_ornR->Draw(CRect(rx - w + ox, bot + oy, rx + ox,     top + oy), blk);
        }
    }

    g_ornL->Draw(CRect(lx,     bot, lx + w, top), white);   // CRect: (sol, alt, sağ, üst)
    g_ornR->Draw(CRect(rx - w, bot, rx,     top), white);
    return true;
}

// ---------------------------------------------------------------- tanı logu
// Her çağrıyı zaman damgasıyla BetaMenu.log dosyasına yazar. Amaç: oyun bu fonksiyonu bir karede
// kaç kez ve hangi panelle çağırıyor görmek. Panel durumu (id, konum, satır, yazı anahtarları)
// değiştiğinde, o andan sonraki ilk 8 çağrı kaydedilir; toplam en fazla 250 satır.
static double NowMs() {
    static LARGE_INTEGER freq = {};
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER c; QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)freq.QuadPart;
}

static void RowKey(const tMenuPanel* p, int col, int row, char* out /*11 byte*/) {
    if (col < 0 || col >= 4 || row < 0 || row >= 12) { out[0] = '\0'; return; }
    memcpy(out, p->m_aaacRowTitles[col][row], 10);
    out[10] = '\0';
}

static void LogPanel(unsigned char id, const tMenuPanel* p, float sx, float sy,
                     float width, float l, float t, float r, float b) {
    static int  lines = 0;
    static int  sinceChange = 0;
    static char lastSig[256] = {};
    if (lines >= 250) return;

    char k00[11], k01[11], k10[11], k11[11];
    RowKey(p, 0, 0, k00); RowKey(p, 0, 1, k01); RowKey(p, 1, 0, k10); RowKey(p, 1, 1, k11);

    char sig[256];
    sprintf_s(sig, "%d|%.1f|%.1f|%d|%d|%s|%s|%s|%s", (int)id, p->m_vPosn.x, p->m_vPosn.y,
              (int)p->m_nNumColumns, (int)p->m_nNumRows, k00, k01, k10, k11);
    if (strcmp(sig, lastSig) != 0) { strcpy_s(lastSig, sig); sinceChange = 0; }
    if (sinceChange++ >= 8) return;

    std::string path = IniPath();
    if (path.size() > 4) path = path.substr(0, path.size() - 4) + ".log";
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "a") != 0 || !f) return;
    if (lines == 0)
        fprintf(f, "ekran %d x %d | sx=%.4f sy=%.4f\n", (int)RsGlobal.maximumWidth,
                (int)RsGlobal.maximumHeight, sx, sy);
    fprintf(f, "[t=%.3f ms] id=%d inUse=%d%d type=%d pos=(%.1f,%.1f) cols=%d rows=%d colBg=%d "
               "w=%.0f/%.0f key00='%s' key01='%s' key10='%s' key11='%s' selRow=%d box=(%.0f,%.0f,%.0f,%.0f)\n",
            NowMs(), (int)id, (int)CMenuSystem::MenuInUse[0], (int)CMenuSystem::MenuInUse[1],
            (int)p->m_nType, p->m_vPosn.x, p->m_vPosn.y, (int)p->m_nNumColumns, (int)p->m_nNumRows,
            (int)p->m_bColumnBackground, p->m_afColumnWidth[0], p->m_afColumnWidth[1],
            k00, k01, k10, k11, (int)p->m_nSelectedRow, l, t, r, b);
    fclose(f);
    ++lines;
}

// ---------------------------------------------------------------- hook
typedef void(__cdecl* DisplayStandardMenuFn)(unsigned char, bool);
static DisplayStandardMenuFn g_origDisplay = nullptr;

static void __cdecl HookedDisplayStandardMenu(unsigned char panelId, bool brightFont) {
    tMenuPanel* p = (panelId < 2) ? MenuNumber[panelId] : nullptr;

    if (!g_cfg.enabled || !p || p->m_nType != (unsigned char)MENU_TYPE_DEFAULT) {
        g_origDisplay(panelId, brightFont);
        return;
    }

    const float sx = (float)RsGlobal.maximumWidth  / 640.0f;
    const float sy = (float)RsGlobal.maximumHeight / 448.0f;

    // NOT: m_vPosn ve m_afColumnWidth oyun tarafından ZATEN ekran pikseline çevrilmiş halde saklanıyor
    // (örn. posX=87 = 29*3). Bu yüzden onları tekrar sx/sy ile çarpmıyoruz; sadece ini'deki
    // sanal (640x448) ayarlar sx/sy ile piksele çevrilir.
    float width = 0.0f;
    bool  hasHeader = false;
    for (int c = 0; c < p->m_nNumColumns && c < 4; ++c) {
        width += p->m_afColumnWidth[c];
        if (p->m_aacColumnHeaders[c][0] != '\0') hasHeader = true;
    }

    const float l = p->m_vPosn.x + g_cfg.offsetX * sx;
    const float t = p->m_vPosn.y + g_cfg.offsetY * sy;
    const float r = l + width + g_cfg.extraW * sx;
    const float contentH = g_cfg.titleH + (hasHeader ? g_cfg.headerH : 0.0f)
                         + p->m_nNumRows * g_cfg.rowH + g_cfg.extraH;
    const float b = t + contentH * sy;

    if (g_cfg.log)
        LogPanel(panelId, p, sx, sy, width, l, t, r, b);

    const CRGBA black(0, 0, 0, (unsigned char)g_cfg.alpha);
    // "white" adı eski kaldı; renk artık ini'deki LineColor (varsayılan #E1E1E1). Süs dokusunun beyaz
    // dolgusu bu renkle çarpıldığı için aynı renge döner, siyah kontur siyah kalır.
    const CRGBA white((unsigned char)((g_cfg.lineColor >> 16) & 0xFF),
                      (unsigned char)((g_cfg.lineColor >> 8) & 0xFF),
                      (unsigned char)(g_cfg.lineColor & 0xFF), 255);

    // 1) Siyah kutu: orijinal panelle aynı ölçüde, orijinalin ALTINA çizilir.
    Rect(l, t, r, b, black);

    // 2) Beyaz çerçeve: kutunun hemen DIŞINDA ve orijinal koddan ÖNCE çizilir.
    //    Böylece "Tattoo" başlığı (panelin üst kenarına taşıyor) çizginin ÜSTÜNDE kalır
    //    ve orijinal yarı saydam panel çerçeveyi soldurmaz.
    const float th  = g_cfg.frame * sy;      // her iki yönde aynı piksel kalınlığı
    const float gap = g_cfg.frameGap * sy;
    const float fl = l - gap - th, ft = t - gap - th, fr = r + gap + th, fb = b + gap + th;
    // Önce siyah kontur (beyaz çizginin hemen dışında), sonra beyaz çizgi.
    const float ot = g_cfg.outline * sy;
    if (ot > 0.0f)
        FrameRect(fl - ot, ft - ot, fr + ot, fb + ot, ot, CRGBA(0, 0, 0, 255));
    FrameRect(fl, ft, fr, fb, th, white);

    // 3) Orijinal kod: panel, "Tattoo" başlığı, yazılar, fiyatlar, seçim vurgusu.
    //    m_bColumnBackground'a DOKUNMUYORUZ (kapatınca yazılar bozuluyordu).
    bool oldBg = p->m_bColumnBackground;
    if (g_cfg.hideOriginalBackground)
        p->m_bColumnBackground = false;

    g_origDisplay(panelId, brightFont);

    p->m_bColumnBackground = oldBg;

    // 4) Alt süs: alt çerçeve çizgisinin ÜSTÜNE, çizgiyle aynı hizada çizilir.
    //    Doku yüklenemediyse eski DrawRect süsü yedek olarak çizilir.
    if (g_cfg.ornament) {
        const float cy = fb - th * 0.5f;
        if (!DrawOrnamentTextured(fl, fr, cy, sx, sy, white))
            DrawOrnament(fl, fr, cy, sx, sy, white);
    }
}

// ---------------------------------------------------------------- yardım kutusu çerçevesi
// CHud::DrawHelpText sırasında çağrılan CSprite2d::DrawRect çağrılarının sınırlayıcı kutusu yakalanır,
// yardım metni bitince bu kutunun DIŞINA siyah kontur + LineColor çizgi çizilir (süs yok).
// Adresler (SA 1.0 US) derleme sırasında plugin-sdk'dan çıkarılıp /D ile verilir; yoksa bunlar kullanılır.
#ifndef BM_ADDR_DRAWHELPTEXT
#define BM_ADDR_DRAWHELPTEXT 0x58B6E0
#endif
#ifndef BM_ADDR_DRAWRECT
#define BM_ADDR_DRAWRECT 0x727B60
#endif
typedef void(__cdecl* DrawHelpTextFn)();
typedef void(__cdecl* DrawRectFn)(const CRect&, const CRGBA&);
static DrawHelpTextFn g_origHelp = nullptr;
static DrawRectFn     g_origRect = nullptr;
static bool  g_inHelp = false;
static bool  g_haveRect = false;
static float g_hl, g_ht, g_hr, g_hb;

static void __cdecl HookedDrawRect(const CRect& rc, const CRGBA& c) {
    if (g_inHelp) {
        float l = rc.left < rc.right ? rc.left : rc.right,  r = rc.left < rc.right ? rc.right : rc.left;
        float t = rc.top < rc.bottom ? rc.top : rc.bottom,  b = rc.top < rc.bottom ? rc.bottom : rc.top;
        if (!g_haveRect) { g_hl = l; g_ht = t; g_hr = r; g_hb = b; g_haveRect = true; }
        else {
            if (l < g_hl) g_hl = l;
            if (t < g_ht) g_ht = t;
            if (r > g_hr) g_hr = r;
            if (b > g_hb) g_hb = b;
        }
    }
    g_origRect(rc, c);
}

static void __cdecl HookedDrawHelpText() {
    if (!g_cfg.enabled || !g_cfg.helpFrame) { g_origHelp(); return; }
    g_inHelp = true; g_haveRect = false;
    g_origHelp();
    g_inHelp = false;
    if (!g_haveRect) return;
    const float sy = (float)RsGlobal.maximumHeight / 448.0f;
    const float th = g_cfg.helpFrameTh * sy, ot = g_cfg.helpOutline * sy;
    const CRGBA line((unsigned char)((g_cfg.lineColor >> 16) & 0xFF),
                     (unsigned char)((g_cfg.lineColor >> 8) & 0xFF),
                     (unsigned char)(g_cfg.lineColor & 0xFF), 255);
    const float fl = g_hl - th, ft = g_ht - th, fr = g_hr + th, fb = g_hb + th;
    if (ot > 0.0f) FrameRect(fl - ot, ft - ot, fr + ot, fb + ot, ot, CRGBA(0, 0, 0, 255));
    FrameRect(fl, ft, fr, fb, th, line);
    if (g_cfg.log) {
        FILE* f = fopen("BetaMenu.log", "a");
        if (f) { fprintf(f, "help rect l=%.1f t=%.1f r=%.1f b=%.1f\n", g_hl, g_ht, g_hr, g_hb); fclose(f); }
    }
}

static void InstallHook() {
    if (MH_Initialize() != MH_OK) return;
    LPVOID target = (LPVOID)0x580E00; // CMenuSystem::DisplayStandardMenu (1.0 US)
    if (MH_CreateHook(target, (LPVOID)&HookedDisplayStandardMenu, (LPVOID*)&g_origDisplay) == MH_OK)
        MH_EnableHook(target);

    // Yardım kutusu çerçevesi: sadece ini'de HelpBoxFrame=1 ise kurulur (kapalıyken oyuna hiç dokunmaz).
    if (g_cfg.helpFrame) {
        LPVOID tr = (LPVOID)BM_ADDR_DRAWRECT, th = (LPVOID)BM_ADDR_DRAWHELPTEXT;
        // Tanı: adresler ve ilk baytlar BetaMenu.log'a yazılır (çökme olursa nedenini görmek için).
        FILE* f = fopen("BetaMenu.log", "a");
        if (f) {
            fprintf(f, "helpframe: DrawRect=%p DrawHelpText=%p\n", tr, th);
            const unsigned char* a = (const unsigned char*)tr; const unsigned char* b = (const unsigned char*)th;
            fprintf(f, "  DrawRect bytes: "); for (int i = 0; i < 8; ++i) fprintf(f, "%02X ", a[i]);
            fprintf(f, "\n  DrawHelpText bytes: "); for (int i = 0; i < 8; ++i) fprintf(f, "%02X ", b[i]);
            fprintf(f, "\n"); fclose(f);
        }
        // Sadece geleneksel prologlar (push ebp / sub esp / push reg / mov) kabul edilir; aksi halde kurma.
        auto plausible = [](const unsigned char* p) {
            return p[0] == 0x55 || p[0] == 0x83 || p[0] == 0x81 || p[0] == 0x6A || p[0] == 0x64 ||
                   p[0] == 0x56 || p[0] == 0x53 || p[0] == 0x51 || p[0] == 0x8B || p[0] == 0xA1 || p[0] == 0xD9;
        };
        if (!plausible((const unsigned char*)tr) || !plausible((const unsigned char*)th)) return;
        if (MH_CreateHook(tr, (LPVOID)&HookedDrawRect, (LPVOID*)&g_origRect) == MH_OK &&
            MH_CreateHook(th, (LPVOID)&HookedDrawHelpText, (LPVOID*)&g_origHelp) == MH_OK) {
            MH_EnableHook(tr);
            MH_EnableHook(th);
        }
    }
}

// ---------------------------------------------------------------- plugin girişi
class BetaMenuMod {
public:
    BetaMenuMod() {
        LoadConfig();

        Events::initRwEvent += [] { CreateOrnamentSprites(); InstallHook(); };

        // F10: ini'yi oyunu kapatmadan yeniden yükle (ölçü ayarı için)
        Events::gameProcessEvent += [] {
            static bool wasDown = false;
            bool down = (GetAsyncKeyState(VK_F10) & 0x8000) != 0;
            if (down && !wasDown) LoadConfig();
            wasDown = down;
        };
    }
} g_betaMenuMod;

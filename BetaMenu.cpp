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
#include "MinHook.h"

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

    // Kutu, orijinal panelle AYNI ölçüde. Orijinal panel (1920x1080 ekran görüntüsünden ölçüldü):
    //   sol kenar = posX, üst kenar = posY, genişlik = sütun toplamı + 44 px (=14.67 sanal),
    //   yükseklik = 120 px + 35 px * (satır + başlık satırı)  (= 49.78 + 14.52*(satır+başlık) sanal).
    // Değerler 640x448 sanal birim; oyun çözünürlüğe göre ölçekler.
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    float extraW  = 14.67f;  // sütun genişlikleri toplamına eklenir
    float extraH  = 0.0f;    // alt pay

    float titleH  = 49.78f;  // panel üstünden ilk satıra kadar olan sabit yükseklik
    float headerH = 14.52f;  // sütun başlığı satırı (sadece başlığı olan panellerde eklenir)
    float rowH    = 14.52f;  // satır aralığı

    int   alpha    = 215;    // kutu şeffaflığı (0-255)
    float frame    = 2.0f;   // beyaz çerçeve kalınlığı
    float frameGap = 0.0f;   // kutu ile çerçeve arası boşluk (çerçeve kutunun DIŞINDA çizilir)

    float ornH     = 22.0f;  // ornament bandının yüksekliği
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
    g_cfg.ornH     = ReadF("OrnamentHeight", d.ornH, ini);
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
    const CRGBA white(255, 255, 255, 255);

    // 1) Siyah kutu: orijinal panelle aynı ölçüde, orijinalin ALTINA çizilir.
    Rect(l, t, r, b, black);

    // 2) Beyaz çerçeve: kutunun hemen DIŞINDA ve orijinal koddan ÖNCE çizilir.
    //    Böylece "Tattoo" başlığı (panelin üst kenarına taşıyor) çizginin ÜSTÜNDE kalır
    //    ve orijinal yarı saydam panel çerçeveyi soldurmaz.
    const float th  = g_cfg.frame * sy;      // her iki yönde aynı piksel kalınlığı
    const float gap = g_cfg.frameGap * sy;
    const float fl = l - gap - th, ft = t - gap - th, fr = r + gap + th, fb = b + gap + th;
    FrameRect(fl, ft, fr, fb, th, white);

    // 3) Orijinal kod: panel, "Tattoo" başlığı, yazılar, fiyatlar, seçim vurgusu.
    //    m_bColumnBackground'a DOKUNMUYORUZ (kapatınca yazılar bozuluyordu).
    bool oldBg = p->m_bColumnBackground;
    if (g_cfg.hideOriginalBackground)
        p->m_bColumnBackground = false;

    g_origDisplay(panelId, brightFont);

    p->m_bColumnBackground = oldBg;

    // 4) Alt süs: alt çerçeve çizgisinin ÜSTÜNE, çizgiyle aynı hizada çizilir.
    if (g_cfg.ornament)
        DrawOrnament(fl, fr, fb - th * 0.5f, sx, sy, white);
}

static void InstallHook() {
    if (MH_Initialize() != MH_OK) return;
    LPVOID target = (LPVOID)0x580E00; // CMenuSystem::DisplayStandardMenu (1.0 US)
    if (MH_CreateHook(target, (LPVOID)&HookedDisplayStandardMenu, (LPVOID*)&g_origDisplay) != MH_OK) return;
    MH_EnableHook(target);
}

// ---------------------------------------------------------------- plugin girişi
class BetaMenuMod {
public:
    BetaMenuMod() {
        LoadConfig();

        Events::initRwEvent += [] { InstallHook(); };

        // F10: ini'yi oyunu kapatmadan yeniden yükle (ölçü ayarı için)
        Events::gameProcessEvent += [] {
            static bool wasDown = false;
            bool down = (GetAsyncKeyState(VK_F10) & 0x8000) != 0;
            if (down && !wasDown) LoadConfig();
            wasDown = down;
        };
    }
} g_betaMenuMod;

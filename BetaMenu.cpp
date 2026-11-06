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
#include <string>

using namespace plugin;

// ---------------------------------------------------------------- ayarlar
struct Config {
    bool  enabled                = true;
    bool  hideOriginalBackground = true;
    bool  ornament               = true;

    // Kutu, panelin (m_vPosn) konumuna göre; hepsi 640x448 sanal koordinat
    float offsetX = -4.0f;
    float offsetY = -8.0f;
    float extraW  = 12.0f;   // sütun genişlikleri toplamına eklenir
    float extraH  = 16.0f;   // satır yüksekliklerine eklenir

    float titleH  = 23.0f;   // panel üstünden ilk satıra kadar boşluk (başlık alanı)
    float headerH = 14.0f;   // sütun başlığı satırı (sadece başlığı olan panellerde eklenir)
    float rowH    = 14.4f;   // satır aralığı (ekran görüntüsünden ölçüldü)

    int   alpha    = 215;    // kutu şeffaflığı (0-255)
    float frame    = 2.0f;   // beyaz çerçeve kalınlığı
    float frameGap = 2.0f;   // kutu ile çerçeve arası boşluk

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
static void DrawOrnament(float l, float r, float top, float sx, float sy, const CRGBA& c) {
    float cy = top + (g_cfg.ornH * 0.5f) * sy;
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
// Panel değerlerini BetaMenu.log dosyasına yazar (aynı değerler tekrar yazılmaz, en fazla 30 kayıt).
static void LogPanel(unsigned char id, const tMenuPanel* p, float sx, float sy,
                     float width, float l, float t, float r, float b) {
    static int   count    = 0;
    static float lastX    = -12345.0f, lastY = -12345.0f;
    static int   lastRows = -1;
    if (count >= 30) return;
    if (p->m_vPosn.x == lastX && p->m_vPosn.y == lastY && p->m_nNumRows == lastRows) return;
    lastX = p->m_vPosn.x; lastY = p->m_vPosn.y; lastRows = p->m_nNumRows;
    ++count;

    std::string path = IniPath();
    if (path.size() > 4) path = path.substr(0, path.size() - 4) + ".log";
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "a") != 0 || !f) return;
    fprintf(f, "--- panel %d | ekran %d x %d | sx=%.4f sy=%.4f\n",
            (int)id, (int)RsGlobal.maximumWidth, (int)RsGlobal.maximumHeight, sx, sy);
    fprintf(f, "type=%d title='%.8s' cols=%d rows=%d colBg=%d\n",
            (int)p->m_nType, p->m_acTitle, (int)p->m_nNumColumns, (int)p->m_nNumRows,
            (int)p->m_bColumnBackground);
    fprintf(f, "posX=%.3f posY=%.3f\n", p->m_vPosn.x, p->m_vPosn.y);
    fprintf(f, "colWidth: %.3f %.3f %.3f %.3f (toplam %.3f)\n",
            p->m_afColumnWidth[0], p->m_afColumnWidth[1], p->m_afColumnWidth[2], p->m_afColumnWidth[3], width);
    fprintf(f, "cizilen kutu (piksel): l=%.1f t=%.1f r=%.1f b=%.1f\n\n", l, t, r, b);
    fclose(f);
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

    LogPanel(panelId, p, sx, sy, width, l, t, r, b);

    const CRGBA black(0, 0, 0, (unsigned char)g_cfg.alpha);
    const CRGBA white(255, 255, 255, 255);

    // siyah kutu
    Rect(l, t, r, b, black);
    // beyaz çerçeve (kutunun hemen içinde)
    float gap = g_cfg.frameGap * sx;
    FrameRect(l + gap, t + gap * (sy / sx), r - gap, b - gap * (sy / sx), g_cfg.frame * sy, white);
    // alt süs
    if (g_cfg.ornament)
        DrawOrnament(l, r, b + 3.0f * sy, sx, sy, white);

    // yazılar / fiyatlar / seçim vurgusu: orijinal kod
    bool oldBg = p->m_bColumnBackground;
    if (g_cfg.hideOriginalBackground)
        p->m_bColumnBackground = false;

    g_origDisplay(panelId, brightFont);

    p->m_bColumnBackground = oldBg;
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

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
#include <intrin.h>
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

    float titleH  = 46.5f;   // satırlardan bağımsız sabit yükseklik (başlık alanı + alt pay)
    float headerH = 18.5f;   // sütun başlığı satırı (sadece başlığı olan panellerde eklenir)
    float rowH    = 14.9f;   // satır aralığı

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
    bool  helpClone   = false;
    bool  probe       = false;   // Probe=1: diğer modların (boxborder vb.) yamaladığı yerleri BetaMenu.log'a yazar   // HelpBoxClone=1: boxborder.asi'yi birebir taklit eden deneme modu
    float helpFrameTh = 2.0f;  // beyaz çizgi kalınlığı (sanal birim)
    float helpOutline = 2.0f;  // dış siyah kontur (sanal birim)
    unsigned ornColor = 0xE1E1E1; // süsün rengi (RRGGBB), LineColor'dan bağımsız
    bool  statsOrnament = false;  // Stats paneline de alt süs
    float statsOrnY   = 0.0f;     // Stats süsünü aşağı (+) / yukarı (-) kaydır (sanal birim)
    float cornerCut   = 1.5f;  // çizgi köşelerinin yassılığı (sanal birim, 0 = dik köşe)
    bool  statsFrame  = false; // Tab ile açılan Stats paneli için çerçeve (kalınlık: HelpBox ayarları)
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
    g_cfg.helpClone   = ReadB("HelpBoxClone", d.helpClone, ini);
    g_cfg.probe       = ReadB("Probe", d.probe, ini);
    g_cfg.statsFrame  = ReadB("StatsBoxFrame", d.statsFrame, ini);
    g_cfg.cornerCut   = ReadF("CornerCut", d.cornerCut, ini);
    g_cfg.statsOrnament = ReadB("StatsBoxOrnament", d.statsOrnament, ini);
    g_cfg.statsOrnY   = ReadF("StatsBoxOrnamentOffsetY", d.statsOrnY, ini);
    {
        // OrnamentColor=E1E1E1 (başında # olabilir). Ini'de yoksa LineColor'a değil, kendi varsayılanına döner.
        char cb[16] = {};
        GetPrivateProfileStringA("BetaMenu", "OrnamentColor", "E1E1E1", cb, sizeof(cb), ini.c_str());
        const char* cs = (cb[0] == '#') ? cb + 1 : cb;
        g_cfg.ornColor = (unsigned)strtoul(cs, nullptr, 16) & 0xFFFFFF;
    }
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

// Köşeleri yassı (pah kırılmış) çerçeve. k = köşe kesiği (piksel). k<2 ise düz FrameRect.
// Çizgi kalınlığı th; köşelerde 45 derecelik basamaklı çizgi (1 piksel yüksekliğinde satırlar) çizilir.
static void ChamferFrame(float l, float t, float r, float b, float th, float k, const CRGBA& c) {
    int n = (int)(k + 0.5f);
    if (n < 2) { FrameRect(l, t, r, b, th, c); return; }
    float kk = (float)n;
    Rect(l + kk, t, r - kk, t + th, c);          // üst
    Rect(l + kk, b - th, r - kk, b, c);          // alt
    Rect(l, t + kk, l + th, b - kk, c);          // sol
    Rect(r - th, t + kk, r, b - kk, c);          // sağ
    for (int i = 0; i < n; ++i) {
        float f = (float)i;
        Rect(l + kk - f,       t + f,       l + kk - f + th, t + f + 1.0f, c);   // sol üst
        Rect(r - kk + f - th,  t + f,       r - kk + f,      t + f + 1.0f, c);   // sağ üst
        Rect(l + kk - f,       b - f - 1.0f, l + kk - f + th, b - f,         c);  // sol alt
        Rect(r - kk + f - th,  b - f - 1.0f, r - kk + f,      b - f,         c);  // sağ alt
    }
}

// Çizginin DIŞINA siyah kontur (ot piksel). Siyah kontur DİK köşeli kalır (orijinal beta'daki gibi);
// beyaz çizginin yassı köşesinin açtığı üçgen boşluk da siyahla doldurulur.
// (l,t,r,b) beyaz çerçevenin dış kenarı, k beyazın köşe kesiği.
static void ChamferOutline(float l, float t, float r, float b, float ot, float k, const CRGBA& c) {
    FrameRect(l - ot, t - ot, r + ot, b + ot, ot, c);
    int n = (int)(k + 0.5f);
    if (n < 2) return;
    float kk = (float)n;
    for (int i = 0; i < n; ++i) {
        float f = (float)i, w = kk - f;                 // satır i'de kesilen genişlik
        Rect(l,      t + f,        l + w, t + f + 1.0f, c);   // sol üst
        Rect(r - w,  t + f,        r,     t + f + 1.0f, c);   // sağ üst
        Rect(l,      b - f - 1.0f, l + w, b - f,        c);   // sol alt
        Rect(r - w,  b - f - 1.0f, r,     b - f,        c);   // sağ alt
    }
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
static bool DrawOrnamentTextured(float fl, float fr, float cy, float sx, float sy, const CRGBA& white, float extraY = 0.0f) {
    if (!g_ornL || !g_ornR) return false;

    float h = g_cfg.ornH * sy;
    float w = h / kOrnHalfAspect * g_cfg.ornStretchX;   // yarım parçanın piksel genişliği
    // Dar menülerde (2 sütunlu paneller) iki parça çakışmasın: aralarında en az %16 boşluk kalsın
    // (beta'da boşluk toplam genişliğin ~%15.5'i). Taşma payı da genişliğe dahildir.
    const float span = (fr - fl) + 2.0f * g_cfg.ornX * sx;
    if (2.0f * w > span * 0.84f) { const float k = span * 0.84f / (2.0f * w); w *= k; h *= k; }

    const float top = cy - kOrnLineV * h + (g_cfg.ornY + extraY) * sy;
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
    const float ck = g_cfg.cornerCut * sy;
    if (ot > 0.0f)
        ChamferOutline(fl, ft, fr, fb, ot, ck, CRGBA(0, 0, 0, 255));
    ChamferFrame(fl, ft, fr, fb, th, ck, white);

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
        const CRGBA ornC((unsigned char)((g_cfg.ornColor >> 16) & 0xFF), (unsigned char)((g_cfg.ornColor >> 8) & 0xFF),
                         (unsigned char)(g_cfg.ornColor & 0xFF), 255);
        if (!DrawOrnamentTextured(fl, fr, cy, sx, sy, ornC))
            DrawOrnament(fl, fr, cy, sx, sy, ornC);
    }
}

static FILE* OpenLogAppend() {
    std::string path = IniPath();
    if (path.size() > 4) path = path.substr(0, path.size() - 4) + ".log";
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "a") != 0) return nullptr;
    return f;
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
static float g_hArea = 0.0f;       // en büyük (arka plan) dikdörtgenin alanı
static int   g_hAlpha = 255;       // o dikdörtgenin alfası (fade-out sırasında düşer)
static int   g_hRefAlpha = 1;      // görülen en yüksek arka plan alfası (tam görünürlük referansı)

// Teşhis sayaçları: DrawRect kancasının içinde dosya yazmak yerine sayaç tutulur, dosyaya güvenli yerden
// (HookedDrawHelpText) yazılır. İstisna olursa kodu burada saklanır.
static volatile long g_cntAll = 0, g_cntRegion = 0, g_cntFramed = 0, g_cntExc = 0;
static volatile unsigned g_excCode = 0;
static volatile int g_excStage = 0;
static int g_capLogged = 0;
static void CaptureRect(const CRect& rc, const CRGBA& c) {
    float l = rc.left < rc.right ? rc.left : rc.right,  r = rc.left < rc.right ? rc.right : rc.left;
    float t = rc.top < rc.bottom ? rc.top : rc.bottom,  b = rc.top < rc.bottom ? rc.bottom : rc.top;
    float area = (r - l) * (b - t);
    if (area >= g_hArea) { g_hArea = area; g_hAlpha = c.a; }
    if (!g_haveRect) { g_hl = l; g_ht = t; g_hr = r; g_hb = b; g_haveRect = true; }
    else {
        if (l < g_hl) g_hl = l;
        if (t < g_ht) g_ht = t;
        if (r > g_hr) g_hr = r;
        if (b > g_hb) g_hb = b;
    }
}

static void LogBigRect(const CRect& rc, const CRGBA& c, void* ret) {
    static unsigned seen[256]; static int n = 0;
    float w = rc.right - rc.left; if (w < 0) w = -w;
    float h = rc.bottom - rc.top; if (h < 0) h = -h;
    if (w < 100.0f || h < 30.0f) return;   // 30: tek satırlık küçük kutular (ör. Cheat activated) da görünsün
    unsigned key = (unsigned)(uintptr_t)ret ^ ((unsigned)(int)rc.left * 31u) ^ ((unsigned)(int)rc.top * 17u);
    for (int i = 0; i < n; ++i) if (seen[i] == key) return;
    if (n >= 256) return;
    seen[n++] = key;
    FILE* f = OpenLogAppend();
    if (f) { fprintf(f, "helpframe: BUYUK rect cagiran=%08X l=%.0f t=%.0f r=%.0f b=%.0f alpha=%d\n", (unsigned)(uintptr_t)ret, rc.left, rc.top, rc.right, rc.bottom, (int)c.a); fclose(f); }
}

// Yardım kutusu çerçevesi (DrawHelpText'in dışında çizilen tek satırlık kutular için: "Use Videogame",
// "Buy Drink", "Cheat activated" ...). Bu kutular CFont arka planı olarak 0x71A7EE'deki DrawRect çağrısından
// çiziliyor (boxborder.asi de aynı yeri yamalıyor). Kutu çizildikten hemen sonra çerçeve çizilir; yazılar
// sonra geldiği için çizginin üstünde kalır.
static int g_fbRef = 1;
static int g_fbLogged = 0;
static void FontBoxFrame(const CRect& rc, const CRGBA& c) {
    if (!g_cfg.enabled || !g_cfg.helpFrame) return;
    const float sx = (float)RsGlobal.maximumWidth / 640.0f, sy = (float)RsGlobal.maximumHeight / 448.0f;
    float l = rc.left < rc.right ? rc.left : rc.right,  r = rc.left < rc.right ? rc.right : rc.left;
    float t = rc.top < rc.bottom ? rc.top : rc.bottom,  b = rc.top < rc.bottom ? rc.bottom : rc.top;
    // Sadece sol üstteki yardım kutusu bölgesi (22.1, 21.9 sanal birim) ve yeterince geniş kutular.
    if (l < 17.0f * sx || l > 27.0f * sx || t < 17.0f * sy || t > 27.0f * sy || (r - l) < 150.0f * sx) return;
    ++g_cntRegion;
    int a = c.a;
    if (a > g_fbRef) g_fbRef = a;
    int fa = (a * 255) / (g_fbRef > 0 ? g_fbRef : 1);
    if (fa <= 0) return; if (fa > 255) fa = 255;
    const float th = g_cfg.helpFrameTh * sy, ot = g_cfg.helpOutline * sy, ck = g_cfg.cornerCut * sy;
    CRGBA line((unsigned char)((g_cfg.lineColor >> 16) & 0xFF), (unsigned char)((g_cfg.lineColor >> 8) & 0xFF),
               (unsigned char)(g_cfg.lineColor & 0xFF), (unsigned char)fa);
    if (ot > 0.0f) ChamferOutline(l - th, t - th, r + th, b + th, ot, ck, CRGBA(0, 0, 0, (unsigned char)fa));
    ChamferFrame(l - th, t - th, r + th, b + th, th, ck, line);
    ++g_cntFramed;
}

// Teşhis (Log=1): sol üst yardım bölgesindeki HER dikdörtgen (boyut ne olursa olsun) loglanır.
static int g_regLogged = 0;
static void LogRegionRect(const CRect& rc, const CRGBA& c, void* ret) {
    if (g_regLogged >= 120) return;
    const float sx = (float)RsGlobal.maximumWidth / 640.0f, sy = (float)RsGlobal.maximumHeight / 448.0f;
    float l = rc.left < rc.right ? rc.left : rc.right,  r = rc.left < rc.right ? rc.right : rc.left;
    float t = rc.top < rc.bottom ? rc.top : rc.bottom,  b = rc.top < rc.bottom ? rc.bottom : rc.top;
    if (l > 140.0f * sx || t > 60.0f * sy || r < 10.0f * sx || b < 5.0f * sy) return;
    if ((r - l) < 20.0f || (b - t) < 8.0f) return;
    if ((r - l) > 0.8f * (float)RsGlobal.maximumWidth) return;   // tüm ekranı kaplayan karartma (fade) dikdörtgenlerini atla
    ++g_regLogged;
    FILE* f = OpenLogAppend();
    if (f) { fprintf(f, "helpframe: BOLGE rect inHelp=%d cagiran=%08X l=%.0f t=%.0f r=%.0f b=%.0f alpha=%d\n", (int)g_inHelp, (unsigned)(uintptr_t)ret, l, t, r, b, (int)c.a); fclose(f); }
}

static void __cdecl HookedDrawRect(const CRect& rc, const CRGBA& c) {
    ++g_cntAll;
    if (g_inHelp) {
        __try { CaptureRect(rc, c); } __except (g_excCode = GetExceptionCode(), g_excStage = 1, ++g_cntExc, EXCEPTION_EXECUTE_HANDLER) {}
    }
    g_origRect(rc, c);
    if (!g_inHelp) {
        __try { FontBoxFrame(rc, c); } __except (g_excCode = GetExceptionCode(), g_excStage = 2, ++g_cntExc, EXCEPTION_EXECUTE_HANDLER) {}
    }
}

// Stats paneli (Tab): kutu 0x573EF4'teki "call DrawRect" ile çiziliyor (loglardan bulundu, SA 1.0 US).
// Kutu çizildikten hemen sonra çerçeve çizilir; "Stats" başlığı ve yazılar sonradan çizildiği için çizginin üstünde kalır.
static int g_statsRef = 1;
static void __cdecl HookedStatsRect(const CRect& rc, const CRGBA& c) {
    g_origRect(rc, c);
    if (!g_cfg.enabled || !g_cfg.statsFrame) return;
    float w = rc.right - rc.left; if (w < 0) w = -w;
    float h = rc.bottom - rc.top; if (h < 0) h = -h;
    if (w < 150.0f || h < 100.0f) return;            // sadece büyük arka plan kutusu
    float l = rc.left < rc.right ? rc.left : rc.right,  r = rc.left < rc.right ? rc.right : rc.left;
    float t = rc.top < rc.bottom ? rc.top : rc.bottom,  b = rc.top < rc.bottom ? rc.bottom : rc.top;
    int a = c.a;
    if (a > g_statsRef) g_statsRef = a;
    int fa = (a * 255) / (g_statsRef > 0 ? g_statsRef : 1);
    if (fa <= 0) return; if (fa > 255) fa = 255;
    const float sy = (float)RsGlobal.maximumHeight / 448.0f;
    const float th = g_cfg.helpFrameTh * sy, ot = g_cfg.helpOutline * sy;
    CRGBA line((unsigned char)((g_cfg.lineColor >> 16) & 0xFF), (unsigned char)((g_cfg.lineColor >> 8) & 0xFF),
               (unsigned char)(g_cfg.lineColor & 0xFF), (unsigned char)fa);
    const float ck = g_cfg.cornerCut * sy;
    if (ot > 0.0f) ChamferOutline(l - th, t - th, r + th, b + th, ot, ck, CRGBA(0, 0, 0, (unsigned char)fa));
    ChamferFrame(l - th, t - th, r + th, b + th, th, ck, line);
    if (g_cfg.statsOrnament) {
        const float sx2 = (float)RsGlobal.maximumWidth / 640.0f;
        CRGBA ornC((unsigned char)((g_cfg.ornColor >> 16) & 0xFF), (unsigned char)((g_cfg.ornColor >> 8) & 0xFF),
                   (unsigned char)(g_cfg.ornColor & 0xFF), (unsigned char)fa);
        DrawOrnamentTextured(l - th, r + th, b + th * 0.5f, sx2, sy, ornC, g_cfg.statsOrnY);
    }
}

static void PatchStatsCall() {
    unsigned char* p = (unsigned char*)0x573EF4;
    bool ok = p[0] == 0xE8 && ((uintptr_t)(p + 5) + (uintptr_t)*(int*)(p + 1)) == (uintptr_t)BM_ADDR_DRAWRECT;
    if (ok) {
        DWORD old = 0;
        if (VirtualProtect(p + 1, 4, PAGE_EXECUTE_READWRITE, &old)) {
            *(int*)(p + 1) = (int)((uintptr_t)&HookedStatsRect - (uintptr_t)(p + 5));
            VirtualProtect(p + 1, 4, old, &old);
            FlushInstructionCache(GetCurrentProcess(), p, 5);
        } else ok = false;
    }
    FILE* f = OpenLogAppend();
    if (f) { fprintf(f, "helpframe: stats call yamasi %s\n", ok ? "tamam" : "BASARISIZ (bayt/hedef uymadi)"); fclose(f); }
}


// ---- Teşhis (Log=1): DrawHelpText içindeki 0x58BFB8 "call 0x728640" (kutu çizen fonksiyon) --------------
// Tüm yardım kutuları (hile, Buy Drink, dövme ...) metinden hemen önce bu çağrıdan geçiyor. 10 argümanı ve
// içinde kaç DrawRect çağrıldığını loglar.
typedef void(__cdecl* BoxFn10)(unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned);
static BoxFn10 g_origBox = nullptr;
static int g_boxLogged = 0;
static void __cdecl HookedBoxCall(unsigned a0,unsigned a1,unsigned a2,unsigned a3,unsigned a4,unsigned a5,unsigned a6,unsigned a7,unsigned a8,unsigned a9) {
    long before = g_cntAll;
    g_origBox(a0,a1,a2,a3,a4,a5,a6,a7,a8,a9);
    long inner = g_cntAll - before;
    if (g_boxLogged < 60) {
        static unsigned last[10] = {0}; unsigned cur[10] = {a0,a1,a2,a3,a4,a5,a6,a7,a8,a9};
        if (memcmp(last, cur, sizeof(cur)) != 0) {
            memcpy(last, cur, sizeof(cur)); ++g_boxLogged;
            FILE* f = OpenLogAppend();
            if (f) {
                fprintf(f, "helpframe: KUTUCAGRI ic_drawrect=%ld", inner);
                for (int i = 0; i < 10; ++i) { float v; memcpy(&v, &cur[i], 4); fprintf(f, " a%d=%08X(%g)", i, cur[i], (double)v); }
                fprintf(f, "\n"); fclose(f);
            }
        }
    }
}
static void PatchBoxCall() {
    unsigned char* p = (unsigned char*)0x58BFB8;
    uintptr_t tgt = (uintptr_t)(p + 5) + (uintptr_t)*(int*)(p + 1);
    bool ok = p[0] == 0xE8 && tgt == 0x728640;
    if (ok) {
        DWORD old = 0;
        g_origBox = (BoxFn10)tgt;
        if (VirtualProtect(p + 1, 4, PAGE_EXECUTE_READWRITE, &old)) {
            *(int*)(p + 1) = (int)((uintptr_t)&HookedBoxCall - (uintptr_t)(p + 5));
            VirtualProtect(p + 1, 4, old, &old);
            FlushInstructionCache(GetCurrentProcess(), p, 5);
        } else ok = false;
    }
    FILE* f = OpenLogAppend();
    if (f) { fprintf(f, "helpframe: kutu call yamasi %s\n", ok ? "tamam" : "BASARISIZ"); fclose(f); }
}

// ---- Teşhis (Log=1): CFont::PrintString (0x71A700) çağıranları -------------------------------------------
// Tek satırlık kutuların metnini kimin çizdiğini bulmak için: sol üst bölgeye (x,y) yazı basan çağıranların
// dönüş adresleri toplanır, sonra çağıran fonksiyonun makine kodu BetaMenu.log'a hex olarak dökülür.
#define BM_ADDR_PRINTSTRING 0x71A700
static volatile unsigned g_psRet[16];
static volatile int g_psN = 0;
static volatile int g_psDumped = 0;
static void __cdecl NoteText(unsigned ret, unsigned xb, unsigned yb) {
    float x, y; memcpy(&x, &xb, 4); memcpy(&y, &yb, 4);
    if (x < 10.0f || x > 400.0f || y < 10.0f || y > 150.0f) return;     // sol üst bölge (piksel)
    for (int i = 0; i < g_psN; ++i) if (g_psRet[i] == ret) return;
    if (g_psN < 16) { g_psRet[g_psN] = ret; g_psN = g_psN + 1; }
}
static unsigned g_psOrig = BM_ADDR_PRINTSTRING;
__declspec(naked) static void PrintStringStub() {
    __asm {
        pushad
        pushfd
        push dword ptr [esp + 44]      // y
        push dword ptr [esp + 44]      // x
        push dword ptr [esp + 44]      // dönüş adresi
        call NoteText
        add esp, 12
        popfd
        popad
        jmp dword ptr [g_psOrig]
    }
}
static void DumpPrintCallers() {
    while (g_psDumped < g_psN) {
        unsigned ret = g_psRet[g_psDumped++];
        FILE* f = OpenLogAppend();
        if (!f) return;
        fprintf(f, "helpframe: YAZI cagiran=%08X\n", ret);
        unsigned lo = ret > 0x400 ? ret - 0x400 : ret, hi = ret + 0x100;
        if (lo < 0x401000) lo = 0x401000;
        if (hi > 0x860000) hi = 0x860000;
        fprintf(f, "helpframe: KOD %08X ", lo);
        for (unsigned a = lo; a < hi; ++a) fprintf(f, "%02X", *(const unsigned char*)a);
        fprintf(f, "\n");
        fclose(f);
    }
}

// DrawRect'i (başka modlarla paylaşılan, açılışta çok çağrılan fonksiyon) HOOKLAMIYORUZ: çökme yapıyordu.
// Onun yerine DrawHelpText'in KENDİ kodu içindeki "call DrawRect" komutlarının hedefini bizim fonksiyona
// çeviriyoruz (sadece o çağrı yerleri değişir, DrawRect'in kendisine dokunulmaz). Oyun içinde, yardım
// metni ~120 kez çizildikten sonra bir kez yapılır.

// ---- Deneme modu (HelpBoxClone=1): boxborder.asi'nin yaptığını birebir yapar -------------------------------
// Sadece 0x71A7EE (yazı arka planı) yamalanır; orijinal DrawRect çizilir, ardından HER dikdörtgenin
// etrafına (tüm ekranı kaplayanlar hariç) çerçeve çizilir. DrawHelpText sarmalayıcısı/filtre yok.
struct CloneRec { int l, t, r, b, a; };
static CloneRec g_cloneRing[40];
static volatile long g_cloneN = 0, g_cloneDumped = 0;
static void CloneFrame(const CRect& rc, const CRGBA& c) {
    const float sx = (float)RsGlobal.maximumWidth / 640.0f, sy = (float)RsGlobal.maximumHeight / 448.0f;
    float l = rc.left < rc.right ? rc.left : rc.right,  r = rc.left < rc.right ? rc.right : rc.left;
    float t = rc.top < rc.bottom ? rc.top : rc.bottom,  b = rc.top < rc.bottom ? rc.bottom : rc.top;
    (void)sx;
    if ((r - l) > 0.8f * (float)RsGlobal.maximumWidth || (r - l) < 4.0f || (b - t) < 4.0f) return;
    long n = g_cloneN;
    if (n < 40) {
        bool dup = false;
        for (long i = 0; i < n; ++i) if (g_cloneRing[i].l == (int)l && g_cloneRing[i].t == (int)t && g_cloneRing[i].r == (int)r && g_cloneRing[i].b == (int)b) dup = true;
        if (!dup) { g_cloneRing[n].l = (int)l; g_cloneRing[n].t = (int)t; g_cloneRing[n].r = (int)r; g_cloneRing[n].b = (int)b; g_cloneRing[n].a = c.a; g_cloneN = n + 1; }
    }
    int a = c.a;
    if (a > g_fbRef) g_fbRef = a;
    int fa = (a * 255) / (g_fbRef > 0 ? g_fbRef : 1);
    if (fa <= 0) return; if (fa > 255) fa = 255;
    const float th = g_cfg.helpFrameTh * sy, ot = g_cfg.helpOutline * sy, ck = g_cfg.cornerCut * sy;
    CRGBA line((unsigned char)((g_cfg.lineColor >> 16) & 0xFF), (unsigned char)((g_cfg.lineColor >> 8) & 0xFF),
               (unsigned char)(g_cfg.lineColor & 0xFF), (unsigned char)fa);
    if (ot > 0.0f) ChamferOutline(l - th, t - th, r + th, b + th, ot, ck, CRGBA(0, 0, 0, (unsigned char)fa));
    ChamferFrame(l - th, t - th, r + th, b + th, th, ck, line);
}
static void __cdecl HookedCloneRect(const CRect& rc, const CRGBA& c) {
    ++g_cntAll;
    g_origRect(rc, c);
    __try { CloneFrame(rc, c); } __except (g_excCode = GetExceptionCode(), g_excStage = 3, ++g_cntExc, EXCEPTION_EXECUTE_HANDLER) {}
}
static void PatchCloneSite() {
    unsigned char* p = (unsigned char*)0x71A7EE;
    FILE* f = OpenLogAppend();
    if (f) { fprintf(f, "helpframe: CLONE 0x71A7EE bayt oncesi: %02X %02X %02X %02X %02X\n", p[0], p[1], p[2], p[3], p[4]); fclose(f); }
    {   // Fonksiyon başları başka bir mod tarafından yönlendirilmiş mi (E9 = jmp)?
        const unsigned char* q1 = (const unsigned char*)0x71A5E0; const unsigned char* q2 = (const unsigned char*)0x71A700;
        FILE* fq = OpenLogAppend();
        if (fq) { fprintf(fq, "helpframe: CLONE 0x71A5E0: %02X %02X %02X %02X %02X | 0x71A700: %02X %02X %02X %02X %02X\n", q1[0], q1[1], q1[2], q1[3], q1[4], q2[0], q2[1], q2[2], q2[3], q2[4]); fclose(fq); }
    }
    g_origRect = (DrawRectFn)(uintptr_t)BM_ADDR_DRAWRECT;
    uintptr_t tgt = (uintptr_t)(p + 5) + (uintptr_t)*(int*)(p + 1);
    bool ok = p[0] == 0xE8 && tgt == (uintptr_t)BM_ADDR_DRAWRECT;
    if (ok) {
        DWORD old = 0;
        if (VirtualProtect(p + 1, 4, PAGE_EXECUTE_READWRITE, &old)) {
            *(int*)(p + 1) = (int)((uintptr_t)&HookedCloneRect - (uintptr_t)(p + 5));
            VirtualProtect(p + 1, 4, old, &old);
            FlushInstructionCache(GetCurrentProcess(), p, 5);
        } else ok = false;
    }
    f = OpenLogAppend();
    if (f) { fprintf(f, "helpframe: CLONE yama %s (hedef=%08X, DrawRect=%08X)\n", ok ? "tamam" : "BASARISIZ", (unsigned)tgt, (unsigned)BM_ADDR_DRAWRECT); fclose(f); }
}
static void CloneStatus() {
    static int tick = 0;
    if (++tick < 60) return;
    tick = 0;
    FILE* f = OpenLogAppend();
    if (!f) return;
    unsigned char* p = (unsigned char*)0x71A7EE;
    long n = g_cloneN;
    fprintf(f, "helpframe: CLONE durum sayac=%ld istisna=%ld bayt=%02X %02X %02X %02X %02X\n", g_cntAll, g_cntExc, p[0], p[1], p[2], p[3], p[4]);
    for (long i = g_cloneDumped; i < n; ++i)
        fprintf(f, "helpframe: CLONE rect l=%d t=%d r=%d b=%d alpha=%d\n", g_cloneRing[i].l, g_cloneRing[i].t, g_cloneRing[i].r, g_cloneRing[i].b, g_cloneRing[i].a);
    g_cloneDumped = n;
    fclose(f);
}


// ---- Probe=1: exe kodunda başka modüllere (başka bir ASI'ye) giden TÜM call/jmp yerlerini listeler -------------
static void RunProbe() {
    FILE* f = OpenLogAppend();
    if (!f) return;
    fprintf(f, "PROBE basladi (boxborder.asi yuklu mu: %s)\n", GetModuleHandleA("boxborder.asi") ? "EVET" : "HAYIR");
    int n = 0;
    for (uintptr_t a = 0x401000; a < 0x85F000 && n < 1500; ++a) {
        const unsigned char* p = (const unsigned char*)a;
        if (p[0] != 0xE8 && p[0] != 0xE9) continue;
        uintptr_t t = a + 5 + (uintptr_t)*(const int*)(p + 1);
        if (t >= 0x401000 && t < 0x860000) continue;          // exe içi: normal
        HMODULE hm = nullptr; char name[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)t, &hm) && hm)
            GetModuleFileNameA(hm, name, MAX_PATH);
        else continue;                                        // hiçbir modüle ait değil: rastgele bayt
        const char* base = strrchr(name, '\\'); base = base ? base + 1 : name;
        if (_stricmp(base, "BetaMenu.asi") == 0) continue;     // kendi yamalarımız
        { size_t bl = strlen(base); if (bl < 4 || _stricmp(base + bl - 4, ".asi") != 0) continue; }   // sadece .asi modülleri
        fprintf(f, "PROBE %s %08X -> %08X (%s +0x%X)\n", p[0] == 0xE8 ? "call" : "jmp ", (unsigned)a, (unsigned)t, base, (unsigned)(t - (uintptr_t)hm));
        ++n;
    }
    fprintf(f, "PROBE bitti: %d yer\n", n);
    fclose(f);
}

static void TryInstallRectHook() {
    static bool tried = false;
    static int frames = 0;
    if (tried || ++frames < 120) return;
    tried = true;
    g_origRect = (DrawRectFn)(uintptr_t)BM_ADDR_DRAWRECT;
    if (g_cfg.helpClone) { if (g_cfg.statsFrame) PatchStatsCall(); return; }
    // Sadece exe kod bölgesindeki adresler okunur (yanlış eşleşen bayt dizilerinde çökmemek için).
    auto inExe = [](uintptr_t a) { return a >= 0x401000 && a < 0x860000; };
    int found = 0;
    // fn'in ilk len baytında "call DrawRect" olan yerleri yamalar; bulunan çağrıların hedeflerini out'a ekler.
    auto scan = [&](uintptr_t fn, int len, uintptr_t* callees, int& nCallees, int maxCallees) {
        unsigned char* base = (unsigned char*)fn;
        for (int i = (fn == (uintptr_t)BM_ADDR_DRAWHELPTEXT ? 5 : 0); i < len; ++i) {
            if (base[i] != 0xE8) continue;
            uintptr_t target = (uintptr_t)(base + i + 5) + (uintptr_t)*(int*)(base + i + 1);
            if (target == (uintptr_t)BM_ADDR_DRAWRECT) {
                DWORD old = 0;
                if (!VirtualProtect(base + i + 1, 4, PAGE_EXECUTE_READWRITE, &old)) continue;
                *(int*)(base + i + 1) = (int)((uintptr_t)&HookedDrawRect - (uintptr_t)(base + i + 5));
                VirtualProtect(base + i + 1, 4, old, &old);
                FlushInstructionCache(GetCurrentProcess(), base + i, 5);
                ++found;
                FILE* fl = OpenLogAppend();
                if (fl) { fprintf(fl, "helpframe: call yamalandi: fn %08X +0x%X\n", (unsigned)fn, i); fclose(fl); }
            } else if (callees && inExe(target) && nCallees < maxCallees) {
                bool dup = false;
                for (int k = 0; k < nCallees; ++k) if (callees[k] == target) dup = true;
                if (!dup) callees[nCallees++] = target;
            }
        }
    };
    // Genişlik öncelikli tarama: kök -> alt fonksiyonlar -> ... (en çok 4 seviye).
    static uintptr_t fns[4096]; int nFns = 0;
    int level1 = 0;
    auto bfs = [&](uintptr_t root, int rootLen) {
        uintptr_t cur[128]; int nCur = 0;
        scan(root, rootLen, cur, nCur, 128);
        int start = nFns;
        for (int k = 0; k < nCur && nFns < 4096; ++k) fns[nFns++] = cur[k];
        int levelStart = start, levelEnd = nFns;
        for (int depth = 2; depth <= 4; ++depth) {
            for (int k = levelStart; k < levelEnd; ++k) {
                uintptr_t c2[128]; int n2 = 0;
                scan(fns[k], 0x400, c2, n2, 128);
                for (int j = 0; j < n2 && nFns < 4096; ++j) {
                    bool dup = false;
                    for (int q = 0; q < nFns; ++q) if (fns[q] == c2[j]) { dup = true; break; }
                    if (!dup) fns[nFns++] = c2[j];
                }
            }
            levelStart = levelEnd; levelEnd = nFns;
        }
        return nCur;
    };
    if (g_cfg.statsFrame) PatchStatsCall();
    if (g_cfg.log && g_cfg.helpFrame) PatchBoxCall();
    if (g_cfg.helpFrame) level1 = bfs((uintptr_t)BM_ADDR_DRAWHELPTEXT, 0x1000);
    // Keşif: Log=1 iken CHud::Draw'dan (0x58FAE0, doğrulanmadı) da taranır; büyük kutuların çağıranı loglanır.
    if (g_cfg.log) bfs((uintptr_t)0x58FAE0, 0x1000);
    // Keşif (sadece Log=1): exe'deki TÜM "call DrawRect" yerleri yamalanır (hepsi gated geçiş; sadece logluyor).
    if (g_cfg.log) {
        int all = 0;
        for (uintptr_t a = 0x401000; a < 0x85F000; ++a) {
            unsigned char* p = (unsigned char*)a;
            if (p[0] != 0xE8 && p[0] != 0xE9) continue;   // call ve kuyruk-atlaması (jmp) ile DrawRect'e gidenler
            if ((uintptr_t)(p + 5) + (uintptr_t)*(int*)(p + 1) != (uintptr_t)BM_ADDR_DRAWRECT) continue;
            DWORD old = 0;
            if (!VirtualProtect(p + 1, 4, PAGE_EXECUTE_READWRITE, &old)) continue;
            *(int*)(p + 1) = (int)((uintptr_t)&HookedDrawRect - (uintptr_t)(p + 5));
            VirtualProtect(p + 1, 4, old, &old);
            FlushInstructionCache(GetCurrentProcess(), p, 5);
            ++all;
            FILE* fs = OpenLogAppend();
            if (fs) { fprintf(fs, "helpframe: tum tarama: %s yeri %08X\n", p[0] == 0xE9 ? "JMP" : "call", (unsigned)a); fclose(fs); }
        }
        FILE* fa = OpenLogAppend();
        if (fa) { fprintf(fa, "helpframe: tum exe taramasi: %d call yeri daha yamalandi\n", all); fclose(fa); }
        int ps = 0;
        for (uintptr_t a = 0x401000; a < 0x85F000; ++a) {
            unsigned char* p = (unsigned char*)a;
            if (p[0] != 0xE8) continue;
            if ((uintptr_t)(p + 5) + (uintptr_t)*(int*)(p + 1) != (uintptr_t)BM_ADDR_PRINTSTRING) continue;
            DWORD old2 = 0;
            if (!VirtualProtect(p + 1, 4, PAGE_EXECUTE_READWRITE, &old2)) continue;
            *(int*)(p + 1) = (int)((uintptr_t)&PrintStringStub - (uintptr_t)(p + 5));
            VirtualProtect(p + 1, 4, old2, &old2);
            FlushInstructionCache(GetCurrentProcess(), p, 5);
            ++ps;
        }
        FILE* fp = OpenLogAppend();
        if (fp) { fprintf(fp, "helpframe: PrintString cagri yeri yamalandi: %d\n", ps); fclose(fp); }
    }
    FILE* f = OpenLogAppend();
    if (f) { fprintf(f, "helpframe: DrawRect cagri yeri sayisi = %d (alt fonksiyon: ilk seviye %d, toplam %d; 0 ise cerceve cikmaz)\n", found, level1, nFns); fclose(f); }
}

static void __cdecl HookedDrawHelpText() {
    if (g_cfg.probe) { static int pt = 0; if (++pt == 400) RunProbe(); g_origHelp(); return; }
    if (g_cfg.enabled && g_cfg.helpClone) { TryInstallRectHook(); if (g_cfg.log) CloneStatus(); g_origHelp(); return; }
    if (!g_cfg.enabled || !(g_cfg.helpFrame || g_cfg.statsFrame)) { g_origHelp(); return; }
    TryInstallRectHook();
    {   // Teşhis: ~2 sn'de bir, sayaçlar değiştiyse durum satırı (Log=1)
        static int tick = 0; static long lastAll = -1;
        if (g_cfg.log && ++tick >= 120) {
            tick = 0;
            DumpPrintCallers();
            if (g_cntAll != lastAll) {
                lastAll = g_cntAll;
                FILE* fd = OpenLogAppend();
                if (fd) { fprintf(fd, "helpframe: sayac DrawRect=%ld bolgeKutu=%ld cerceveli=%ld istisna=%ld (son kod=%08X asama=%d)\n", g_cntAll, g_cntRegion, g_cntFramed, g_cntExc, g_excCode, g_excStage); fclose(fd); }
            }
        }
    }
    if (!g_cfg.helpFrame) { g_origHelp(); return; }
    g_inHelp = true; g_haveRect = false; g_hArea = 0.0f; g_hAlpha = 255;
    static int enters = 0;
    if (enters < 3) { ++enters; FILE* fe = OpenLogAppend(); if (fe) { fprintf(fe, "helpframe: DrawHelpText'e giriliyor (%d)\n", enters); fclose(fe); } }
    g_origHelp();
    g_inHelp = false;
    static int calls = 0;
    if (calls < 3) { ++calls; FILE* fc = OpenLogAppend(); if (fc) { fprintf(fc, "helpframe: DrawHelpText cagrildi, rect yakalandi=%d\n", (int)g_haveRect); fclose(fc); } }
    if (!g_haveRect) return;
    const float sy = (float)RsGlobal.maximumHeight / 448.0f;
    const float th = g_cfg.helpFrameTh * sy, ot = g_cfg.helpOutline * sy;
    const CRGBA line((unsigned char)((g_cfg.lineColor >> 16) & 0xFF),
                     (unsigned char)((g_cfg.lineColor >> 8) & 0xFF),
                     (unsigned char)(g_cfg.lineColor & 0xFF), 255);
    // Fade-out: kutunun arka plan alfası düştükçe çerçeve de aynı oranda solar.
    if (g_hAlpha > g_hRefAlpha) g_hRefAlpha = g_hAlpha;
    int fa = (g_hAlpha * 255) / (g_hRefAlpha > 0 ? g_hRefAlpha : 1);
    if (fa > 255) fa = 255;
    if (fa <= 0) return;
    CRGBA lineF = line; lineF.a = (unsigned char)fa;
    const float fl = g_hl - th, ft = g_ht - th, fr = g_hr + th, fb = g_hb + th;
    const float ck = g_cfg.cornerCut * sy;
    if (ot > 0.0f) ChamferOutline(fl, ft, fr, fb, ot, ck, CRGBA(0, 0, 0, (unsigned char)fa));
    ChamferFrame(fl, ft, fr, fb, th, ck, lineF);
    static int helpLogged = 0;
    if (helpLogged < 150) {   // sadece kutu değişince yaz (en çok 150 satır; Log ayarından bağımsız) (her kare yazmak logu şişiriyordu)
        static float ll = 0, lt = 0, lr = 0, lb = 0;
        if (g_hl != ll || g_ht != lt || g_hr != lr || g_hb != lb) {
            ll = g_hl; lt = g_ht; lr = g_hr; lb = g_hb; ++helpLogged;
            FILE* f = OpenLogAppend();
            if (f) { fprintf(f, "help rect l=%.1f t=%.1f r=%.1f b=%.1f alpha=%d ref=%d fa=%d\n", g_hl, g_ht, g_hr, g_hb, g_hAlpha, g_hRefAlpha, fa); fclose(f); }
        }
    }
}

static void InstallHook() {
    if (MH_Initialize() != MH_OK) return;
    LPVOID target = (LPVOID)0x580E00; // CMenuSystem::DisplayStandardMenu (1.0 US)
    if (MH_CreateHook(target, (LPVOID)&HookedDisplayStandardMenu, (LPVOID*)&g_origDisplay) == MH_OK)
        MH_EnableHook(target);

    // Yardım kutusu çerçevesi: sadece ini'de HelpBoxFrame=1 ise kurulur (kapalıyken oyuna hiç dokunmaz).
    if (g_cfg.helpFrame || g_cfg.statsFrame || g_cfg.helpClone || g_cfg.probe) {
        LPVOID tr = (LPVOID)BM_ADDR_DRAWRECT, th = (LPVOID)BM_ADDR_DRAWHELPTEXT;
        // Tanı: adresler ve ilk baytlar BetaMenu.log'a yazılır (çökme olursa nedenini görmek için).
        FILE* f = OpenLogAppend();
        if (f) {
            fprintf(f, "helpframe: DrawRect=%p DrawHelpText=%p ayarlar: enabled=%d HelpBoxFrame=%d StatsBoxFrame=%d Log=%d\n", tr, th, (int)g_cfg.enabled, (int)g_cfg.helpFrame, (int)g_cfg.statsFrame, (int)g_cfg.log);
            const unsigned char* a = (const unsigned char*)tr; const unsigned char* b = (const unsigned char*)th;
            fprintf(f, "  DrawRect bytes: "); for (int i = 0; i < 8; ++i) fprintf(f, "%02X ", a[i]);
            fprintf(f, "\n  DrawHelpText bytes: "); for (int i = 0; i < 8; ++i) fprintf(f, "%02X ", b[i]);
            fprintf(f, "\n"); fclose(f);
        }
        // Sadece geleneksel prologlar (push ebp / sub esp / push reg / mov) kabul edilir; aksi halde kurma.
        auto plausible = [](const unsigned char* p) {
            return p[0] == 0xE9 || p[0] == 0xA0 || p[0] == 0x55 || p[0] == 0x83 || p[0] == 0x81 || p[0] == 0x6A || p[0] == 0x64 ||
                   p[0] == 0x56 || p[0] == 0x53 || p[0] == 0x51 || p[0] == 0x8B || p[0] == 0xA1 || p[0] == 0xD9;
        };
        if (!plausible((const unsigned char*)tr) || !plausible((const unsigned char*)th)) {
            FILE* f3 = OpenLogAppend();
            if (f3) { fprintf(f3, "helpframe: adres gecersiz gorunuyor, kurulmadi\n"); fclose(f3); }
            return;
        }
        MH_STATUS s1 = MH_CreateHook(th, (LPVOID)&HookedDrawHelpText, (LPVOID*)&g_origHelp);
        MH_STATUS s2 = (s1 == MH_OK) ? MH_EnableHook(th) : s1;
        MH_STATUS s3 = MH_OK, s4 = MH_OK;
        FILE* f2 = OpenLogAppend();
        if (f2) { fprintf(f2, "helpframe hook status: %d %d %d %d (0 = tamam)\n", (int)s1, (int)s2, (int)s3, (int)s4); fclose(f2); }
    }
}

// ---------------------------------------------------------------- plugin girişi
class BetaMenuMod {
public:
    BetaMenuMod() {
        LoadConfig();
        if (g_cfg.enabled && g_cfg.helpClone) PatchCloneSite();   // boxborder gibi ASI yüklenir yüklenmez (diğer modlar kodu kopyalamadan önce)

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

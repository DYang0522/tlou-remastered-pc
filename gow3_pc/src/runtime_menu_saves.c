#include "runtime_menu_saves.h"
#include "gpu/gow3gpu.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* The game's own texts for the page title: "LOAD GAME", "SAVE GAME" and "Delete". */
#define TEXT_LOAD_GAME 0x7c894f27u
#define TEXT_SAVE_GAME 0x0615b1acu
#define TEXT_DELETE 0x47d83f63u
enum { MAX_ROWS = 33, LABEL_SIZE = 160 };
/* The menu's update must open the page this soon after the dialog asked, else the overlay list. */
#define OPEN_TIMEOUT_SECONDS 1.5

typedef ABI void (*PageOpen)(void *menu, uint32_t page, uint32_t a, uint32_t b);
typedef ABI void (*PageBack)(void *menu, uint32_t a);
static PageOpen page_open;
static PageBack page_back;
static atomic_int page_added, update_hooked;

static char labels[MAX_ROWS][LABEL_SIZE], names[MAX_ROWS][LABEL_SIZE], dates[MAX_ROWS][LABEL_SIZE];
/* Each save's chapter (its detail's first line, in the language it was saved in) and the area of
 * the game's Chapters list it belongs to (-1: none found; AREA_UNKNOWN: not looked up yet). */
enum { AREA_UNKNOWN = -2 };
static char chapters[MAX_ROWS][LABEL_SIZE];
static int areas[MAX_ROWS];
static atomic_int row_count, new_first, list_type;
/* The list's value (the shown row), read and written by the game's widget. */
static _Alignas(16) int32_t selected;
/* 0 closed, 1 asked (the menu's next update opens the page), 2 open. */
enum { CLOSED, ASKED, OPEN };
static atomic_int state;
static atomic_int chosen = -1;
static int seen_page;
static double asked_at;
static void *_Atomic open_menu;

static double now_seconds(void) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* The page on top of the menu's page stack (its hash), read on the menu's update. */
static uint32_t top_page(const unsigned char *menu) {
    if (!menu) return 0;
    const int32_t top = (int32_t)gow3_menu_u32(menu + GOW3_MENU_PAGE_TOP);
    if (top < 0 || top >= GOW3_MENU_PAGE_STACK_SIZE) return 0;
    const unsigned char *page = (const unsigned char *)(uintptr_t)gow3_menu_u64(menu + GOW3_MENU_PAGE_STACK + 8 * top);
    return page ? gow3_menu_u32(page + GOW3_MENU_PAGE_OBJECT_HASH) : 0;
}
static atomic_int left_page, showing;

void runtime_menu_saves_set_navigation(void *open, void *back) {
    page_open = (PageOpen)open;
    page_back = (PageBack)back;
}

void runtime_menu_saves_set_update_hooked(void) {
    atomic_store(&update_hooked, 1);
}

void runtime_menu_saves_page_added(void) {
    atomic_store(&page_added, 1);
}

/* The menu's per-frame update (its entry hook): the asked page opens here, in the menu's own
 * context, as the dispatcher's buttons do. */
ABI void runtime_menu_saves_update(void *menu, uint32_t unused) {
    (void)unused;
    if (!menu) return;
    if (atomic_load(&state) == OPEN && menu == atomic_load(&open_menu)) {
        /* Our page on top, or left without a choice (Back). */
        const int on_top = top_page(menu) == GOW3_MENU_SAVES_PAGE;
        if (on_top) seen_page = 1;
        else if (seen_page) atomic_store(&left_page, 1);
        atomic_store(&showing, on_top);
        return;
    }
    if (atomic_load(&state) != ASKED) return;
    seen_page = 0;
    atomic_store(&left_page, 0);
    atomic_store(&showing, 0);
    atomic_store(&open_menu, menu);
    atomic_store(&state, OPEN);
    page_open(menu, GOW3_MENU_SAVES_PAGE, 0, 0);
    printf("Menu: save list on the game's own page (%d rows)\n", atomic_load(&row_count));
}

int runtime_menu_saves_open(int type, int count, int has_new, const char *const *row_labels,
                            const char *const *pictures, const char *const *row_chapters) {
    static int disabled = -1;
    if (disabled < 0) {
        const char *env = getenv("GOW3_NATIVE_SAVE_LIST");
        disabled = env && env[0] == '0';
    }
    const char *why = disabled ? "GOW3_NATIVE_SAVE_LIST=0"
                    : !page_open || !page_back ? "page navigation not found"
                    : !atomic_load(&update_hooked) ? "menu update not hooked"
                    : !atomic_load(&page_added) ? "page not in the menu data"
                    : count <= 0 || count > MAX_ROWS ? "row count"
                                                     : NULL;
    if (why) {
        printf("Menu: save list in the overlay (%s)\n", why);
        return 0;
    }
    /* "name - date" rows: the name and the date apart (part 0 and part 1 of the label). */
    for (int i = 0; i < count; ++i) {
        snprintf(labels[i], LABEL_SIZE, "%s", row_labels[i] ? row_labels[i] : "");
        const char *dash = strstr(labels[i], " - ");
        snprintf(names[i], LABEL_SIZE, "%.*s", dash ? (int)(dash - labels[i]) : (int)strlen(labels[i]), labels[i]);
        snprintf(dates[i], LABEL_SIZE, "%s", dash ? dash + 3 : "");
        snprintf(chapters[i], LABEL_SIZE, "%s", row_chapters && row_chapters[i] ? row_chapters[i] : "");
        areas[i] = AREA_UNKNOWN;
    }
    atomic_store(&row_count, count);
    atomic_store(&new_first, has_new);
    atomic_store(&list_type, type);
    selected = 0;
    atomic_store(&chosen, -1);
    gow3gpu_preview_begin(pictures, count);
    asked_at = now_seconds();
    atomic_store(&state, ASKED);
    return 1;
}


int runtime_menu_saves_poll(void) {
    const int st = atomic_load(&state);
    if (st == CLOSED) return -2;
    if (st == ASKED) {
        /* No menu update ran (the menu is not on screen): the overlay list instead. */
        int expected = ASKED;
        if (now_seconds() - asked_at > OPEN_TIMEOUT_SECONDS && atomic_compare_exchange_strong(&state, &expected, CLOSED)) {
            puts("Menu: save list in the overlay (the menu did not open the page)");
            return -3;
        }
        return -1;
    }
    const int pick = atomic_load(&chosen);
    if (pick >= 0) {
        atomic_store(&state, CLOSED);
        gow3gpu_preview_show(-1);
        return pick;
    }
    if (atomic_load(&left_page)) { /* Back: the page was left without a choice */
        atomic_store(&state, CLOSED);
        gow3gpu_preview_show(-1);
        return -2;
    }
    /* The overlay's picture only for saves whose area is not known (the game draws the others). */
    const int row = selected;
    const int known = row >= 0 && row < atomic_load(&row_count) && areas[row] >= 0;
    gow3gpu_preview_show(atomic_load(&showing) && !known ? row : -1);
    return -1;
}

/* Runs inside the dispatcher, in the menu's update: the shown row is chosen and the page closes. */
int runtime_menu_saves_button(void *menu, uint32_t hash) {
    if (hash != GOW3_MENU_SAVES_LIST) return 0;
    if (atomic_load(&state) != OPEN || atomic_load(&chosen) >= 0) return 1;
    const int count = atomic_load(&row_count);
    const int row = selected < 0 ? 0 : selected >= count ? count - 1 : selected;
    atomic_store(&chosen, row);
    gow3gpu_preview_show(-1);
    if (page_back && menu) page_back(menu, 0);
    return 1;
}

/* --- The list's binding --------------------------------------------------------------------- */
static uint64_t vtable[GOW3_BINDING_VTABLE_SLOTS];
static atomic_int vtable_ready;

/* label(self, row, part), as Chapters' list provider answers it: part 0 the row's name, both in
 * the list and under the picture beside it; the other parts are a second column and panel lines
 * that the picture layout does not show. The list asks for every row each frame, the panel only
 * for the shown one: a caller (its return address) that ever asked for another row is the
 * list's. The panel's line gets the save's date too. */
enum { MAX_LIST_SITES = 8 };
static uintptr_t list_sites[MAX_LIST_SITES];
static int list_site_count;
static char panel_text[LABEL_SIZE * 2];

static int is_list_site(uintptr_t site, int index) {
    for (int i = 0; i < list_site_count; ++i)
        if (list_sites[i] == site) return 1;
    if (index != selected && list_site_count < MAX_LIST_SITES) {
        list_sites[list_site_count++] = site;
        return 1;
    }
    return 0;
}

static ABI __attribute__((noinline)) const char *row_label(void *self, int index, int part) {
    (void)self;
    if (index < 0 || index >= atomic_load(&row_count)) return "";
    if (index == 0 && atomic_load(&new_first)) return part == 0 ? runtime_menu_pc_new_save_text() : "";
    if (part != 0) return "";
    if (is_list_site((uintptr_t)__builtin_return_address(0), index) || !dates[index][0]) return names[index];
    snprintf(panel_text, sizeof(panel_text), "%s\n%s", names[index], dates[index]);
    return panel_text;
}

/* The game's chapters (update 1.11, dc1/tasks.bin): each sub-chapter's name, in every language
 * of the game (text1/<N>.common), and its area, in the order of the Chapters list (the area list
 * provider counts them: 1, 4, 4, 4, 4, 2, 2, 2, 2, 2, 1, 1). A save's detail starts with the name
 * in the language it was saved in; its picture is its area's, as Chapters shows it. Generated
 * from the 29 sub-chapter text ids (no name belongs to two areas). */
static const struct { const char *name; int area; } subchapters[] = {
    {"Proloog", 0},
    {"プロローグ", 0},
    {"프롤로그", 0},
    {"Prologi", 0},
    {"Prolog", 0},
    {"序幕", 0},
    {"Пролог", 0},
    {"Prólogo", 0},
    {"Prologue", 0},
    {"Πρόλογος", 0},
    {"Başlangıç", 0},
    {"Prologo", 0},
    {"Twintig jaar later", 1},
    {"２０年後", 1},
    {"20년 후", 1},
    {"20 vuotta myöhemmin", 1},
    {"20 år senere", 1},
    {"20 år senare", 1},
    {"二十年後", 1},
    {"Карантинная зона", 1},
    {"20 lat później", 1},
    {"20 años después", 1},
    {"20 Years Later", 1},
    {"Vinte anos depois", 1},
    {"20 χρόνια μετά", 1},
    {"O 20 let později", 1},
    {"20 Yıl Sonra", 1},
    {"20 ans plus tard", 1},
    {"20 anni dopo", 1},
    {"20 Jahre später", 1},
    {"20 Anos Depois", 1},
    {"Voorbij de muur", 1},
    {"壁の向こう", 1},
    {"벽을 넘어", 1},
    {"Muurin tuolla puolen", 1},
    {"Bortenfor muren", 1},
    {"Utanför muren", 1},
    {"牆外", 1},
    {"За стеной", 1},
    {"Poza murem", 1},
    {"Más allá de la pared", 1},
    {"Beyond the Wall", 1},
    {"Além do muro", 1},
    {"Πέρα από τον Τοίχο", 1},
    {"Za zdí", 1},
    {"Duvarın Ötesi", 1},
    {"Derrière le mur", 1},
    {"Oltre il muro", 1},
    {"Jenseits der Mauer", 1},
    {"Al otro lado del muro", 1},
    {"Uden for muren", 1},
    {"Para lá do muro", 1},
    {"抜け穴", 1},
    {"高牆之外", 1},
    {"Beyond The Wall", 1},
    {"Hinter der Mauer", 1},
    {"Para Lá do Muro", 1},
    {"De vracht", 1},
    {"仕事の依頼", 1},
    {"짐짝", 1},
    {"Salakuljetettava", 1},
    {"Lasten", 1},
    {"貨物", 1},
    {"Цикады", 1},
    {"Ładunek", 1},
    {"El cargamento", 1},
    {"The Cargo", 1},
    {"A carga", 1},
    {"Το Φορτίο", 1},
    {"Náklad", 1},
    {"Kargo", 1},
    {"Le cargo", 1},
    {"Il carico", 1},
    {"Die Ladung", 1},
    {"A Carga", 1},
    {"De sloppenwijken", 1},
    {"スラム街", 1},
    {"빈민가", 1},
    {"Slummit", 1},
    {"Slummen", 1},
    {"貧民窟", 1},
    {"Трущобы", 1},
    {"Slumsy", 1},
    {"Los barrios bajos", 1},
    {"The Slums", 1},
    {"A favela", 1},
    {"Οι Φτωχογειτονιές", 1},
    {"Slumy", 1},
    {"Kenar Semt", 1},
    {"Bidonville", 1},
    {"I bassifondi", 1},
    {"Die Slums", 1},
    {"Barrios bajos", 1},
    {"Bairro de Lata", 1},
    {"Buiten", 2},
    {"壁の外", 2},
    {"바깥 세상", 2},
    {"Ulkona", 2},
    {"Utsiden", 2},
    {"På utsidan", 2},
    {"外面的世界", 2},
    {"Пустыри", 2},
    {"Na zewnątrz", 2},
    {"Afuera", 2},
    {"Outside", 2},
    {"Lá fora", 2},
    {"Έξω", 2},
    {"Venku", 2},
    {"Dışarısı", 2},
    {"Sortie", 2},
    {"Là fuori", 2},
    {"Draußen", 2},
    {"Fuera", 2},
    {"Udenfor!", 2},
    {"Lá fora, pela primeira vez", 2},
    {"De stad", 2},
    {"ダウンタウン", 2},
    {"도심지", 2},
    {"Keskusta", 2},
    {"Sentrum", 2},
    {"City", 2},
    {"市區", 2},
    {"Центр", 2},
    {"Śródmieście", 2},
    {"Centro", 2},
    {"Downtown", 2},
    {"Centro da cidade", 2},
    {"Κέντρο της Πόλης", 2},
    {"Centrum města", 2},
    {"Şehir Merkezi", 2},
    {"Centre ville", 2},
    {"Centro città", 2},
    {"Zentrum", 2},
    {"Midtbyen", 2},
    {"Baixa", 2},
    {"Museum", 2},
    {"美術館", 2},
    {"박물관", 2},
    {"Museo", 2},
    {"Museet", 2},
    {"博物館", 2},
    {"Музей", 2},
    {"Muzeum", 2},
    {"Museu", 2},
    {"Μουσείο", 2},
    {"Müze", 2},
    {"Musée", 2},
    {"Het gemeentehuis", 2},
    {"州議事堂", 2},
    {"중앙청 건물", 2},
    {"Valtiontalo", 2},
    {"Statshuset", 2},
    {"Stadshuset", 2},
    {"議會大廈", 2},
    {"Рандеву", 2},
    {"Budynek ratusza", 2},
    {"El edificio del capitolio", 2},
    {"The Capitol Building", 2},
    {"O prédio do congresso", 2},
    {"Καπιτώλιο", 2},
    {"Budova kapitolu", 2},
    {"Hükümet Binası", 2},
    {"Le capitole", 2},
    {"L'edificio governativo", 2},
    {"Das Rathaus", 2},
    {"El ayuntamiento", 2},
    {"Parlamentsbygning", 2},
    {"Câmara Municipal", 2},
    {"De bossen", 3},
    {"森", 3},
    {"숲", 3},
    {"Metsä", 3},
    {"Skogen", 3},
    {"樹林", 3},
    {"Американский городок", 3},
    {"Las", 3},
    {"El bosque", 3},
    {"The Woods", 3},
    {"O bosque", 3},
    {"Το Δάσος", 3},
    {"Lesy", 3},
    {"Ormanlık Alan", 3},
    {"Les bois", 3},
    {"I boschi", 3},
    {"Der Wald", 3},
    {"Los bosques", 3},
    {"Skoven", 3},
    {"Os Bosques", 3},
    {"Schuilplaats", 3},
    {"ビルのアジト", 3},
    {"안전가옥", 3},
    {"Turvatalo", 3},
    {"Skjulested", 3},
    {"Gömstället", 3},
    {"藏身處", 3},
    {"Ответная услуга", 3},
    {"Kryjówka", 3},
    {"Refugio", 3},
    {"Safehouse", 3},
    {"Esconderijo", 3},
    {"Κρησφύγετο", 3},
    {"Úkryt", 3},
    {"Güvenli Ev", 3},
    {"Abri", 3},
    {"Rifugio", 3},
    {"Unterschlupf", 3},
    {"Casa franca", 3},
    {"Sikkert hus", 3},
    {"Begraafplaats", 3},
    {"墓場", 3},
    {"묘지", 3},
    {"Hautausmaa", 3},
    {"Kirkegården", 3},
    {"Kyrkogården", 3},
    {"墓園", 3},
    {"Терра инкогнита", 3},
    {"Cmentarz", 3},
    {"Cementerio", 3},
    {"Graveyard", 3},
    {"Cemitério", 3},
    {"Νεκροταφείο", 3},
    {"Hřbitov", 3},
    {"Mezarlık", 3},
    {"Cimetière", 3},
    {"Cimitero", 3},
    {"Friedhof", 3},
    {"Kirkegård", 3},
    {"Ontsnapping uit de middelbare school", 3},
    {"高校", 3},
    {"고등학교 탈출", 3},
    {"Pako koulusta", 3},
    {"Flukt fra skolen", 3},
    {"Flykten från skolan", 3},
    {"逃離高中", 3},
    {"Средняя школа", 3},
    {"Ucieczka z budynku szkoły", 3},
    {"Escape de la escuela", 3},
    {"High School Escape", 3},
    {"Fuga da escola secundária", 3},
    {"Απόδραση από το Γυμνάσιο", 3},
    {"Útěk ze střední školy", 3},
    {"Liseden Kaçış", 3},
    {"Fuite du lycée", 3},
    {"Fuga dal liceo", 3},
    {"Highschool-Flucht", 3},
    {"Huida del instituto", 3},
    {"Flugt fra highschool", 3},
    {"Fuga do Liceu", 3},
    {"Alleen en verlaten", 4},
    {"待ち伏せ", 4},
    {"우회로", 4},
    {"Yksin ja hylättynä", 4},
    {"Ensom og forlatt", 4},
    {"Ensam och övergiven", 4},
    {"孤獨和被遺棄", 4},
    {"Обходной путь", 4},
    {"Samotna i porzucona", 4},
    {"Solos y abandonados", 4},
    {"Alone and Forsaken", 4},
    {"Sozinho e abandonado", 4},
    {"Μόνος κι έρημος", 4},
    {"Sami a opuštění", 4},
    {"Yalnız ve Terk edilmiş", 4},
    {"Solitude et oubli", 4},
    {"Solitudine e abbandono", 4},
    {"Allein und verlassen", 4},
    {"Solo y olvidado", 4},
    {"Alene og forladt", 4},
    {"Sozinho e esquecido", 4},
    {"Hotellobby", 4},
    {"ホテル", 4},
    {"호텔 로비", 4},
    {"Hotellin aula", 4},
    {"Hotellobbyn", 4},
    {"飯店大廳", 4},
    {"Отель", 4},
    {"Hotelowy hol", 4},
    {"Lobby del hotel", 4},
    {"Hotel Lobby", 4},
    {"Lobby do hotel", 4},
    {"Λόμπι ξενοδοχείου", 4},
    {"Hotelová hala", 4},
    {"Otel Lobisi", 4},
    {"Accueil de l'hôtel", 4},
    {"Lobby dell'hotel", 4},
    {"Vestíbulo del hotel", 4},
    {"Receção do Hotel", 4},
    {"Financieel district", 4},
    {"金融街", 4},
    {"금융가", 4},
    {"Talouskeskus", 4},
    {"Finansdistriktet", 4},
    {"Finansdistrikt", 4},
    {"金融區", 4},
    {"Финансовый квартал", 4},
    {"Dzielnica biznesowa", 4},
    {"Distrito financiero", 4},
    {"Financial District", 4},
    {"Distrito financeiro", 4},
    {"Οικονομικό Κέντρο", 4},
    {"Finanční čtvrť", 4},
    {"Mali Semt", 4},
    {"Quartier des affaires", 4},
    {"Distretto finanziario", 4},
    {"Finanzviertel", 4},
    {"Bairro Financeiro", 4},
    {"Ontsnap uit de stad", 4},
    {"街からの脱出", 4},
    {"도시 탈출", 4},
    {"Pako kaupungista", 4},
    {"Unnslipp byen", 4},
    {"Fly från staden", 4},
    {"逃離市區", 4},
    {"Лагерь Охотников", 4},
    {"Ucieczka z miasta", 4},
    {"Escape de la ciudad", 4},
    {"Escape the City", 4},
    {"Fuga da cidade", 4},
    {"Απόδρασε από την Πόλη", 4},
    {"Útěk z města", 4},
    {"Şehirden Kaç", 4},
    {"Quitter la ville", 4},
    {"Fuggi dalla città", 4},
    {"Die Stadt verlassen", 4},
    {"Huye de la ciudad", 4},
    {"Slip ud af byen", 4},
    {"Foge da Cidade", 4},
    {"Riool", 5},
    {"地下水路", 5},
    {"하수도", 5},
    {"Viemäri", 5},
    {"Kloakken", 5},
    {"Kloakerna", 5},
    {"下水道", 5},
    {"Канализация", 5},
    {"Kanały", 5},
    {"Alcantarillas", 5},
    {"Sewers", 5},
    {"Esgoto", 5},
    {"Υπόνομοι", 5},
    {"Stoky", 5},
    {"Kanalizasyon", 5},
    {"Égouts", 5},
    {"Fogne", 5},
    {"Kanalisation", 5},
    {"Kloakker", 5},
    {"Esgotos", 5},
    {"Buitenwijk", 5},
    {"住宅街", 5},
    {"교외", 5},
    {"Esikaupunki", 5},
    {"Forstedene", 5},
    {"Förorten", 5},
    {"郊區", 5},
    {"Пригород", 5},
    {"Przedmieścia", 5},
    {"Suburbios", 5},
    {"Suburbs", 5},
    {"Subúrbio", 5},
    {"Προάστια", 5},
    {"Předměstí", 5},
    {"Banliyö", 5},
    {"Banlieue", 5},
    {"Sobborghi", 5},
    {"Vorstadt", 5},
    {"Forstaden", 5},
    {"Subúrbios", 5},
    {"Hydro-elektrische dam", 6},
    {"水力発電所", 6},
    {"수력발전 댐", 6},
    {"Vesivoimalaitos", 6},
    {"Vannkraftverk", 6},
    {"Hydroelektrisk damm", 6},
    {"水力發電水壩", 6},
    {"Гидроэлектростанция", 6},
    {"Hydroelektrownia", 6},
    {"Represa hidroeléctrica", 6},
    {"Hydroelectric Dam", 6},
    {"Usina hidrelétrica", 6},
    {"Υδροηλεκτρικό Φράγμα", 6},
    {"Vodní elektrárna", 6},
    {"Hidroelektrik Santrali", 6},
    {"Barrage hydroélectrique", 6},
    {"Diga idroelettrica", 6},
    {"Hydroelektrischer Damm", 6},
    {"Presa hidroeléctrica", 6},
    {"Hydroelektrisk dæmning", 6},
    {"Barragem Hidroelétrica", 6},
    {"Ranch", 6},
    {"牧場の家", 6},
    {"목장", 6},
    {"Farmi", 6},
    {"Gårdshus", 6},
    {"Ranchen", 6},
    {"牧場房子", 6},
    {"Ранчо", 6},
    {"Ranczo", 6},
    {"Casa de rancho", 6},
    {"Ranch House", 6},
    {"Casa de campo", 6},
    {"Ράντσο", 6},
    {"Ranč", 6},
    {"Çiftlik Evi", 6},
    {"Il ranch", 6},
    {"Farmhaus", 6},
    {"Casa del rancho", 6},
    {"Ranch-husbygning", 6},
    {"Casa do Rancho", 6},
    {"Hup Big Horns", 7},
    {"キャンパス", 7},
    {"동부 콜로라도 대학", 7},
    {"Tsemppiä Big Horns", 7},
    {"Heia Big Horns", 7},
    {"Universitet", 7},
    {"上吧，大角羊隊", 7},
    {"Университет", 7},
    {"Do boju, Big Horns!", 7},
    {"Vamos Big Horns", 7},
    {"Go Big Horns", 7},
    {"Vamos, carneiros selvagens", 7},
    {"Πάμε, Big Horns", 7},
    {"Do toho, Big Horns!", 7},
    {"Bastır Büyük Boynuzlar", 7},
    {"Allez les Big Horns", 7},
    {"Forza Big Horns", 7},
    {"Auf geht's, Big Horns", 7},
    {"Adelante, Big Horns", 7},
    {"Heya Bighorns", 7},
    {"Força, Big Horns", 7},
    {"Wetenschapsgebouw", 7},
    {"研究所", 7},
    {"과학관", 7},
    {"Tiederakennus", 7},
    {"Forskningsbygget", 7},
    {"Forskningsanläggningen", 7},
    {"科學大樓", 7},
    {"Лаборатория", 7},
    {"Budynek naukowy", 7},
    {"Edificio de ciencias", 7},
    {"Science Building", 7},
    {"Prédio de ciência", 7},
    {"Κτίριο θετικών επιστημών", 7},
    {"Výzkumná budova", 7},
    {"Bilim Laboratuvarı", 7},
    {"Bâtiment de sciences", 7},
    {"Edificio scientifico", 7},
    {"Forschungstrakt", 7},
    {"Den naturvidenskabelige bygning", 7},
    {"Edifício Científico", 7},
    {"De jacht", 8},
    {"狩り", 8},
    {"사냥", 8},
    {"Metsällä", 8},
    {"Jakten", 8},
    {"狩獵", 8},
    {"Лес", 8},
    {"Polowanie", 8},
    {"La cacería", 8},
    {"The Hunt", 8},
    {"A caçada", 8},
    {"Το Κυνήγι", 8},
    {"Lov", 8},
    {"Av", 8},
    {"La chasse", 8},
    {"La caccia", 8},
    {"Die Jagd", 8},
    {"Jagten", 8},
    {"A Caçada", 8},
    {"Vakantiehuisje", 8},
    {"吹雪の街", 8},
    {"휴양지 오두막", 8},
    {"Lomakylä", 8},
    {"Hytteområde", 8},
    {"Stugbyn", 8},
    {"木屋度假村", 8},
    {"Городок", 8},
    {"Kurort z domkami", 8},
    {"Complejo turístico de cabañas", 8},
    {"Cabin Resort", 8},
    {"Resort com cabanas", 8},
    {"Καλύβα παραθερισμού", 8},
    {"Chatová osada", 8},
    {"Kulübe Kampı", 8},
    {"Village de vacances", 8},
    {"Resort baita", 8},
    {"Ferienhütten", 8},
    {"Complejo de cabañas", 8},
    {"Feriehytte", 8},
    {"Cabana da Estância", 8},
    {"Afrit", 9},
    {"ハイウェイ出口", 9},
    {"고속도로 출구", 9},
    {"Moottoritien liittymä", 9},
    {"Motorveiavkjørsel", 9},
    {"Motorvägsavfarten", 9},
    {"公路出口", 9},
    {"Шоссе", 9},
    {"Autostrada", 9},
    {"Salida por la autopista", 9},
    {"Highway Exit", 9},
    {"Saída pela rodovia", 9},
    {"Έξοδος Αυτοκινητόδρομου", 9},
    {"Dálniční sjezd", 9},
    {"Otoban Çıkışı", 9},
    {"Sortie d'autoroute", 9},
    {"Uscita autostradale", 9},
    {"Autobahnausfahrt", 9},
    {"Salida de la autopista", 9},
    {"Motorvejsafkørsel", 9},
    {"Saída da Autoestrada", 9},
    {"Ondergrondse tunnel", 9},
    {"地下トンネル", 9},
    {"지하 터널", 9},
    {"Maanalainen tunneli", 9},
    {"Undergrunnstunnel", 9},
    {"Underjordisk tunnel", 9},
    {"地下隧道", 9},
    {"Подземный туннель", 9},
    {"Podziemny tunel", 9},
    {"Túnel subterráneo", 9},
    {"Underground Tunnel", 9},
    {"Túnel subterrâneo", 9},
    {"Υπόγειο τούνελ", 9},
    {"Podzemní tunel", 9},
    {"Yeraltı Tüneli", 9},
    {"Tunnel sous-terrain", 9},
    {"Tunnel sotterraneo", 9},
    {"Unterirdischer Tunnel", 9},
    {"Túnel Subterrâneo", 9},
    {"Het ziekenhuis", 10},
    {"奪還", 10},
    {"병원", 10},
    {"Sairaala", 10},
    {"Sykehuset", 10},
    {"Sjukhuset", 10},
    {"醫院", 10},
    {"Больница", 10},
    {"Szpital", 10},
    {"El hospital", 10},
    {"The Hospital", 10},
    {"O hospital", 10},
    {"Το Νοσοκομείο", 10},
    {"Nemocnice", 10},
    {"Hastane", 10},
    {"L'hôpital", 10},
    {"L'ospedale", 10},
    {"Das Krankenhaus", 10},
    {"Hospitalet", 10},
    {"O Hospital", 10},
    {"Epiloog", 11},
    {"エピローグ", 11},
    {"에필로그", 11},
    {"Epilogi", 11},
    {"Epilog", 11},
    {"尾聲", 11},
    {"Эпилог", 11},
    {"Epílogo", 11},
    {"Epilogue", 11},
    {"Επίλογος", 11},
    {"Kapanış", 11},
    {"Épilogue", 11},
    {"Epilogo", 11},
};

static int area_of(int row) {
    if (areas[row] != AREA_UNKNOWN) return areas[row];
    int area = -1;
    for (size_t i = 0; chapters[row][0] && i < sizeof(subchapters) / sizeof(subchapters[0]); ++i)
        if (!strcmp(subchapters[i].name, chapters[row])) {
            area = subchapters[i].area;
            break;
        }
    areas[row] = area;
    return area;
}

/* image(out, self, row, kind) (slot 14, returns out): the row's picture, as the area list
 * provider of the game's Chapters page gives it (its own slot 14, for the save's area); none
 * (that provider's answer for another kind) when the area is not known. */
typedef ABI void *(*ImageSlot)(void *out, void *self, int index, uint32_t kind);
enum { IMAGE_SLOT = 14, MENU_AREA_LIST = 0x1480 };
static ABI void *row_image(void *out, void *self, int index, uint32_t kind) {
    (void)self;
    const unsigned char *menu = atomic_load(&open_menu);
    unsigned char *areas_list = menu ? (unsigned char *)(uintptr_t)gow3_menu_u64(menu + MENU_AREA_LIST) : NULL;
    if (!areas_list) {
        memset(out, 0, 0x2c);
        return out;
    }
    const ImageSlot image = (ImageSlot)(uintptr_t)gow3_menu_u64(
        (const unsigned char *)(uintptr_t)gow3_menu_u64(areas_list) + 8 * IMAGE_SLOT);
    const int area = index >= 0 && index < atomic_load(&row_count) ? area_of(index) : -1;
    return image(out, areas_list, area >= 0 ? area : 0, area >= 0 ? kind : 0);
}
static ABI int row_min(void *self) {
    (void)self;
    return 0;
}
static ABI int row_max(void *self) {
    (void)self;
    const int count = atomic_load(&row_count);
    return count > 0 ? count - 1 : 0;
}

void runtime_menu_saves_bind(unsigned char *binding) {
    if (!atomic_load(&vtable_ready)) {
        memcpy(vtable, (const void *)(uintptr_t)gow3_menu_u64(binding), sizeof(vtable));
        vtable[GOW3_BINDING_LABEL_SLOT] = (uint64_t)(uintptr_t)row_label;
        vtable[GOW3_BINDING_MIN_SLOT] = (uint64_t)(uintptr_t)row_min;
        vtable[GOW3_BINDING_MAX_SLOT] = (uint64_t)(uintptr_t)row_max;
        vtable[IMAGE_SLOT] = (uint64_t)(uintptr_t)row_image;
        atomic_store(&vtable_ready, 1);
    }
    gow3_menu_put64(binding, (uint64_t)(uintptr_t)vtable);
    gow3_menu_put64(binding + GOW3_BINDING_VALUE, (uint64_t)(uintptr_t)&selected);
    gow3_menu_put32(binding + GOW3_BINDING_MIN, 0);
    gow3_menu_put32(binding + GOW3_BINDING_MAX, (uint32_t)row_max(NULL));
}

const char *runtime_menu_saves_text(Gow3TextLookup lookup, void *table, uint32_t sid) {
    if (sid != GOW3_MENU_SAVES_TITLE) return NULL;
    const int type = atomic_load(&list_type);
    const char *text = lookup(table, type == 1 ? TEXT_SAVE_GAME : type == 3 ? TEXT_DELETE : TEXT_LOAD_GAME);
    return text ? text : "";
}

/*
 * demo.c - PC上のモックLCDとモックDeviceControlでGUIを動かすデモ
 *
 * ビルド: cc -std=c11 -Wall -Wextra -Wpedantic -o demo gui.c demo.c
 * 実行:   ./demo
 *
 * 実機LCDの代わりに20x4のテキストバッファへ描画し、
 * 専用ハードウェアの代わりにモックDeviceControlへ操作を記録します。
 * malloc / calloc / realloc / free は使いません。
 */
#include "gui.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define LCD_COLUMNS 20
#define LCD_ROWS    4

/* ------------------------------------------------------------------ */
/* モックLCD（GuiDisplayPortの実装）                                   */
/* ------------------------------------------------------------------ */

typedef struct {
    char cells[LCD_ROWS][LCD_COLUMNS + 1];
} MockLcd;

static void mock_clear(void *context)
{
    MockLcd *lcd = (MockLcd *)context;
    int y;

    for (y = 0; y < LCD_ROWS; y++) {
        memset(lcd->cells[y], ' ', LCD_COLUMNS);
        lcd->cells[y][LCD_COLUMNS] = '\0';
    }
}

static void mock_draw_text(void *context, uint8_t x, uint8_t y,
                           const char *text)
{
    MockLcd *lcd = (MockLcd *)context;
    size_t i;

    if (y >= LCD_ROWS) {
        return;
    }
    for (i = 0; text[i] != '\0'; i++) {
        size_t col = (size_t)x + i;

        if (col >= LCD_COLUMNS) {
            break;
        }
        lcd->cells[y][col] = text[i];
    }
}

/* 外字は意味で受け取り、この環境で表現できる文字へ落とす。
 * 実機アダプタでは、ここがCGRAMへの登録番号になる。 */
static void mock_draw_icon(void *context, uint8_t x, uint8_t y, GuiIcon icon)
{
    MockLcd *lcd = (MockLcd *)context;
    char glyph;

    if (y >= LCD_ROWS || x >= LCD_COLUMNS) {
        return;
    }
    switch (icon) {
    case GUI_ICON_CURSOR:  glyph = '>'; break;
    case GUI_ICON_EDITING: glyph = '*'; break;
    case GUI_ICON_LOCKED:  glyph = '#'; break;
    case GUI_ICON_UP_DOWN: glyph = '^'; break;
    default:               glyph = ' '; break;
    }
    lcd->cells[y][x] = glyph;
}

static void mock_flush(void *context)
{
    (void)context;
}

static const GuiDisplayPortOps MOCK_LCD_OPS = {
    .clear = mock_clear,
    .draw_text = mock_draw_text,
    .draw_icon = mock_draw_icon,
    .flush = mock_flush
};

static MockLcd lcd;

static const GuiDisplayPort MOCK_DISPLAY = {
    .ops = &MOCK_LCD_OPS,
    .context = &lcd
};

static void mock_dump(const char *caption)
{
    int y;

    printf("%s\n", caption);
    printf("+--------------------+\n");
    for (y = 0; y < LCD_ROWS; y++) {
        printf("|%s|\n", lcd.cells[y]);
    }
    printf("+--------------------+\n\n");
}

/* ------------------------------------------------------------------ */
/* モックDeviceControl（専用ハードウェア側の代役）                     */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t rate;      /* 実際に適用されている設定 */
    int16_t level;
    uint8_t preview_count;
    uint8_t commit_count;
    uint8_t restore_count;
} MockDevice;

static MockDevice device = { 1, 5, 0, 0, 0 };

static void device_preview_rate(uint8_t rate)
{
    device.rate = rate;
    device.preview_count++;
}

static void device_commit_rate(uint8_t rate)
{
    device.rate = rate;
    device.commit_count++;
}

static void device_restore_rate(uint8_t rate)
{
    device.rate = rate;
    device.restore_count++;
}

/* ------------------------------------------------------------------ */
/* 画面（GuiPageの派生）                                               */
/* ------------------------------------------------------------------ */

static const char *const RATE_ITEMS[] = { "9600", "115k", "1M" };
static const char *const PORT_ITEMS[] = { "P1", "P2", "USB" };

/* mallocは使わない。Widgetも子配列もすべて静的に確保する。 */
static GuiLabel  title;
static GuiCombo  rate;
static GuiButton apply;
static GuiSpin   level;
static GuiCombo  port;
static GuiButton locked; /* 非表示。読み飛ばしの確認用 */

static GuiWidget *settings_children[] = {
    (GuiWidget *)&title,
    (GuiWidget *)&rate,
    (GuiWidget *)&apply,
    (GuiWidget *)&level,
    (GuiWidget *)&port,
    (GuiWidget *)&locked
};

typedef struct {
    GuiPage page; /* 必ず先頭 */
    uint8_t applied_rate;
} SettingsPage;

static SettingsPage settings;

static int apply_count = 0;

static void on_apply(GuiButton *button, void *user_data)
{
    SettingsPage *self = (SettingsPage *)user_data;

    (void)button;
    self->applied_rate = gui_combo_value(&rate);
    apply_count++;
}

/* Change: まだ確定していない。ハードウェアへは「試す」だけ。 */
static void on_rate_change(GuiCombo *combo, uint8_t candidate, void *user_data)
{
    (void)combo;
    (void)user_data;
    device_preview_rate(candidate);
}

/* Submit: ここで確定する。 */
static void on_rate_submit(GuiCombo *combo, uint8_t value, void *user_data)
{
    (void)combo;
    (void)user_data;
    device_commit_rate(value);
}

/* Cancel: 編集開始前の値とハードウェア状態へ戻す。 */
static void on_rate_cancel(GuiCombo *combo, uint8_t original, void *user_data)
{
    (void)combo;
    (void)user_data;
    device_restore_rate(original);
}

/* Pageの表示前に、装置の現在状態を画面のモデルへ読み込む。 */
static void settings_before_show(GuiPage *page)
{
    SettingsPage *self = (SettingsPage *)page;

    rate.value = device.rate;
    rate.editing = device.rate;
    level.value = device.level;
    level.editing = device.level;
    self->applied_rate = device.rate;
}

/* この画面用のクラス関数表。基底のshow／hideはヘッダが公開する
 * 初期化子マクロで埋めるので、実行時の代入なしにconstのままROMへ置ける。 */
static const GuiPageClass SETTINGS_PAGE_CLASS = {
    .parent_class = GUI_PAGE_WIDGET_CLASS_INITIALIZER,
    .before_show  = settings_before_show,
    .after_hide   = NULL,
    .time_up      = NULL,
    .repaint      = NULL
};

static void build_page(void)
{
    gui_label_init(&title, 0, 0, LCD_COLUMNS, "Device Settings");
    gui_combo_init(&rate, 0, 1, 10, "Rate", RATE_ITEMS, 3, 1);
    gui_button_init(&apply, 10, 1, 10, "Apply", on_apply, &settings);
    gui_spin_init(&level, 0, 2, 10, "Lvl", 0, 9, 1, 5);
    gui_combo_init(&port, 10, 2, 10, "Port", PORT_ITEMS, 3, 0);
    gui_button_init(&locked, 0, 3, LCD_COLUMNS, "Service", NULL, NULL);

    /* 読み飛ばしの確認用。非表示かつ無効。 */
    gui_widget_set_visible((GuiWidget *)&locked, false);
    gui_widget_set_sensitive((GuiWidget *)&locked, false);

    rate.on_change = on_rate_change;
    rate.on_submit = on_rate_submit;
    rate.on_cancel = on_rate_cancel;

    /* 画面ごとの方向リンク。左右、上下、ファンクションキーで組む。 */
    rate.named.item.links.right  = &locked.item;   /* 無効なので読み飛ばす */
    locked.item.links.right      = &apply.item;
    locked.item.links.left       = &rate.named.item;
    apply.item.links.left        = &rate.named.item;
    rate.named.item.links.down   = &level.named.item;
    level.named.item.links.up    = &rate.named.item;
    apply.item.links.down        = &port.named.item;
    port.named.item.links.up     = &apply.item;
    level.named.item.links.right = &port.named.item;
    port.named.item.links.left   = &level.named.item;
    rate.named.item.links.function[0]  = &apply.item;
    level.named.item.links.function[0] = &apply.item;
    port.named.item.links.function[0]  = &apply.item;

    gui_page_init(&settings.page, &SETTINGS_PAGE_CLASS,
                  settings_children,
                  (uint8_t)(sizeof(settings_children)
                            / sizeof(settings_children[0])),
                  &MOCK_DISPLAY);
    gui_page_set_focus(&settings.page, &rate.named.item);
}

static void feed(const GuiKey *keys, size_t count)
{
    size_t i;

    for (i = 0; i < count; i++) {
        gui_page_dispatch_key_down(&settings.page, keys[i]);
    }
}

#define FEED(...)                                                    \
    do {                                                             \
        static const GuiKey keys_[] = { __VA_ARGS__ };               \
        feed(keys_, sizeof(keys_) / sizeof(keys_[0]));               \
    } while (0)

/* ------------------------------------------------------------------ */
/* シナリオ                                                            */
/* ------------------------------------------------------------------ */

int main(void)
{
    uint8_t i;

    printf("sizeof(GuiWidget) = %zu\n", sizeof(GuiWidget));
    printf("sizeof(GuiVisual) = %zu\n", sizeof(GuiVisual));
    printf("sizeof(GuiItem)   = %zu\n", sizeof(GuiItem));
    printf("sizeof(GuiCombo)  = %zu\n", sizeof(GuiCombo));
    printf("sizeof(GuiPage)   = %zu\n\n", sizeof(GuiPage));

    /* 装置側の現在値をあらかじめ変えておく。 */
    device.rate = 2;
    device.level = 3;

    build_page();
    gui_page_show(&settings.page);
    mock_dump("[1][2] before_showで現在値を読み込み、Rateにフォーカス");

    /* 1. Page表示前に現在値が読み込まれる */
    assert(gui_combo_value(&rate) == 2);
    assert(gui_spin_value(&level) == 3);
    assert(settings.applied_rate == 2);

    /* 2. 初期フォーカスが正しい */
    assert(settings.page.selected_child == (GuiWidget *)&rate);

    /* 3. 上下左右で指定したリンク先へ移動する */
    FEED(GUI_KEY_DOWN);
    assert(settings.page.selected_child == (GuiWidget *)&level);
    FEED(GUI_KEY_RIGHT);
    assert(settings.page.selected_child == (GuiWidget *)&port);
    FEED(GUI_KEY_UP);
    assert(settings.page.selected_child == (GuiWidget *)&apply);
    FEED(GUI_KEY_LEFT);
    assert(settings.page.selected_child == (GuiWidget *)&rate);
    mock_dump("[3] 上下左右でリンク先へ移動し、Rateへ戻った");

    /* 4. 非表示または無効なItemを選ばない
     *    rate → locked（非表示・無効）→ apply と読み飛ばす */
    FEED(GUI_KEY_RIGHT);
    assert(settings.page.selected_child == (GuiWidget *)&apply);
    printf("[4] 非表示のServiceを読み飛ばしてApplyへ移動\n\n");
    FEED(GUI_KEY_LEFT);
    assert(settings.page.selected_child == (GuiWidget *)&rate);

    /* 5. ChangeでプレビューがDeviceControlへ届く */
    {
        uint8_t before = device.preview_count;

        FEED(GUI_KEY_ENTER, GUI_KEY_UP); /* 編集開始 → 候補を1つ戻す */
        assert(device.preview_count == (uint8_t)(before + 1));
        assert(device.rate == 1);
        assert(gui_combo_value(&rate) == 2); /* 確定値はまだ変わらない */
        mock_dump("[5] 編集中。プレビューは効くが確定値は元のまま");
    }

    /* 7. Cancelで編集開始前の値とハードウェア状態へ戻る */
    FEED(GUI_KEY_CANCEL);
    assert(device.restore_count == 1);
    assert(device.rate == 2);
    assert(gui_combo_value(&rate) == 2);
    mock_dump("[7] Cancelで編集開始前へ戻る");

    /* 6. Submitで確定値が更新される */
    FEED(GUI_KEY_ENTER, GUI_KEY_DOWN, GUI_KEY_ENTER);
    assert(gui_combo_value(&rate) == 2); /* 2が上限なのでDOWNは効かない */
    FEED(GUI_KEY_ENTER, GUI_KEY_UP, GUI_KEY_ENTER);
    assert(gui_combo_value(&rate) == 1);
    assert(device.commit_count == 2);
    assert(device.rate == 1);
    mock_dump("[6] SubmitでRateが115kへ確定し、DeviceControlへcommitされる");

    /* Spinも同じChange／Submit／Cancelで動く */
    FEED(GUI_KEY_DOWN, GUI_KEY_ENTER, GUI_KEY_UP, GUI_KEY_UP, GUI_KEY_ENTER);
    assert(gui_spin_value(&level) == 5);
    FEED(GUI_KEY_ENTER, GUI_KEY_DOWN, GUI_KEY_CANCEL);
    assert(gui_spin_value(&level) == 5);
    mock_dump("[6b] SpinもSubmitで確定し、Cancelで戻る");

    /* 8. Buttonのexecuteが1回だけ呼ばれる */
    FEED(GUI_KEY_UP, GUI_KEY_RIGHT, GUI_KEY_ENTER);
    assert(settings.page.selected_child == (GuiWidget *)&apply);
    assert(apply_count == 1);
    assert(settings.applied_rate == 1);
    mock_dump("[8] ApplyのENTERでexecuteが1回だけ走る");

    /* ファンクションキーのリンクも同じ仕組みで動く */
    FEED(GUI_KEY_LEFT, GUI_KEY_F1);
    assert(settings.page.selected_child == (GuiWidget *)&apply);
    printf("[9] F1でApplyへ直行（リンクはItemが持つ）\n\n");

    /* 9. GuiDisplayPortをモックへ差し替えて描画できている */
    assert(strncmp(lcd.cells[0], "Device Settings", 15) == 0);
    assert(lcd.cells[1][10] == '>'); /* Applyのカーソル位置 */

    /* 11. 各childのparentがPageを指している */
    for (i = 0; i < settings.page.child_count; i++) {
        assert(settings.page.children[i]->parent == &settings.page.widget);
    }
    assert(settings.page.widget.parent == NULL); /* 最上位なのでNULL */
    printf("[11] 全childのparentがPageを指し、Page自身のparentはNULL\n\n");

    /* 12. 各Widgetのtypeが正しい */
    assert(settings.page.widget.type == GUI_WIDGET_PAGE);
    assert(title.visual.widget.type == GUI_WIDGET_LABEL);
    assert(apply.item.visual.widget.type == GUI_WIDGET_BUTTON);
    assert(rate.named.item.visual.widget.type == GUI_WIDGET_COMBO);
    assert(level.named.item.visual.widget.type == GUI_WIDGET_SPIN);
    assert(!gui_widget_is_item(&title.visual.widget));
    assert(gui_widget_is_item(&rate.named.item.visual.widget));
    printf("[12] Page／Label／Button／Combo／Spinのtypeが正しい\n\n");

    /* 13. 画面用クラス関数表が実行時に書き換えられていない。
     *     const オブジェクトなので、Pageが指す先が定義そのものであり、
     *     基底の show／hide をそのまま引き継いでいることを確認する。 */
    assert((const GuiPageClass *)settings.page.widget.klass == &SETTINGS_PAGE_CLASS);
    assert(SETTINGS_PAGE_CLASS.parent_class.show == gui_page_widget_show);
    assert(SETTINGS_PAGE_CLASS.parent_class.hide == gui_page_widget_hide);
    assert(SETTINGS_PAGE_CLASS.before_show == settings_before_show);
    printf("[13] 画面用クラス関数表はstatic constのまま（実行時代入なし）\n\n");

    printf("すべてのシナリオが期待どおりに動作しました。\n");
    return 0;
}

/*
 * gui.h - 4行キャラクタLCD向け最小GUIライブラリ（記事用の公開サンプル）
 *
 * 記事の説明のために新規に書き起こしたコードです。製品のソースではありません。
 * malloc / calloc / realloc / free は使いません。すべて静的に確保します。
 *
 * 型階層（オブジェクト側）        クラス側（クラス関数表）
 *   GuiWidget                       GuiWidgetClass
 *   ├ GuiPage                       ├ GuiPageClass
 *   └ GuiVisual                     └ GuiVisualClass
 *      ├ GuiLabel                      ├ GuiLabelClass
 *      └ GuiItem                       └ GuiItemClass
 *         ├ GuiButton                     ├ GuiButtonClass
 *         └ GuiNamedItem                  └ GuiNamedItemClass
 *            ├ GuiCombo                      ├ GuiComboClass
 *            └ GuiSpin                       └ GuiSpinClass
 *
 * オブジェクト側もクラス側も、派生型の先頭に親型を実体で埋め込みます。
 * これにより派生型のポインタを親型のポインタへそのままキャストできます。
 */
#ifndef GUI_H
#define GUI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* 表示ポート                                                          */
/* ------------------------------------------------------------------ */

/* 外字を意味で扱う。ビットマップそのものは実機アダプタ側の関心事。 */
typedef enum {
    GUI_ICON_NONE = 0,
    GUI_ICON_CURSOR,
    GUI_ICON_UP_DOWN,
    GUI_ICON_LOCKED,
    GUI_ICON_EDITING
} GuiIcon;

typedef struct {
    void (*clear)(void *context);
    void (*draw_text)(void *context,
                      uint8_t x,
                      uint8_t y,
                      const char *text);
    void (*draw_icon)(void *context,
                      uint8_t x,
                      uint8_t y,
                      GuiIcon icon);
    void (*flush)(void *context);
} GuiDisplayPortOps;

typedef struct {
    const GuiDisplayPortOps *ops;
    void *context;
} GuiDisplayPort;

/* ------------------------------------------------------------------ */
/* キー                                                                */
/* ------------------------------------------------------------------ */

typedef enum {
    GUI_KEY_UP = 0,
    GUI_KEY_DOWN,
    GUI_KEY_LEFT,
    GUI_KEY_RIGHT,
    GUI_KEY_ENTER,
    GUI_KEY_CANCEL,
    GUI_KEY_F1,
    GUI_KEY_F2,
    GUI_KEY_F3,
    GUI_KEY_F4
} GuiKey;

/* ファンクションキーの本数は機器依存。ここでは公開例として4本にする。 */
#define GUI_FUNCTION_KEY_COUNT 4

/* ------------------------------------------------------------------ */
/* GuiWidget（すべての基底）                                           */
/* ------------------------------------------------------------------ */

typedef struct GuiWidget      GuiWidget;
typedef struct GuiWidgetClass GuiWidgetClass;

/* boolを並べるとRAMが増えるのでビットフラグにする。 */
enum {
    GUI_FLAG_VISIBLE   = 1u << 0,
    GUI_FLAG_SENSITIVE = 1u << 1,
    GUI_FLAG_FOCUSABLE = 1u << 2,
    GUI_FLAG_DIRTY     = 1u << 3,
    GUI_FLAG_FOCUSED   = 1u << 4,
    GUI_FLAG_EDITING   = 1u << 5
};

/* Widgetの型。デバッグ時の型検査、ログ、キャスト前確認に使う。
 * 列挙型の実サイズは処理系依存なので、格納は uint8_t に固定し、
 * 値の一覧だけをenumで定義する。 */
typedef enum {
    GUI_WIDGET_PAGE = 0,
    GUI_WIDGET_LABEL,
    GUI_WIDGET_BUTTON,
    GUI_WIDGET_COMBO,
    GUI_WIDGET_SPIN
} GuiWidgetType;

struct GuiWidgetClass {
    void (*show)(GuiWidget *widget);
    void (*hide)(GuiWidget *widget);
    bool (*key_down)(GuiWidget *widget, GuiKey key);
    bool (*key_press)(GuiWidget *widget, GuiKey key);
    bool (*key_up)(GuiWidget *widget, GuiKey key);
};

struct GuiWidget {
    const GuiWidgetClass *klass; /* ROM上の共有クラス関数表 */
    GuiWidget *parent;           /* 所有元へ戻る片方向参照。最上位はNULL */
    uint8_t type;                /* GuiWidgetType の値 */
    uint8_t flags;
};

/* 型検査ヘルパ。デバッグ時のキャスト前確認に使う。 */
bool gui_widget_is_item(const GuiWidget *widget);

void gui_widget_show(GuiWidget *widget);
void gui_widget_hide(GuiWidget *widget);
bool gui_widget_key_down(GuiWidget *widget, GuiKey key);
bool gui_widget_key_press(GuiWidget *widget, GuiKey key);
bool gui_widget_key_up(GuiWidget *widget, GuiKey key);

void gui_widget_set_visible(GuiWidget *widget, bool visible);
void gui_widget_set_sensitive(GuiWidget *widget, bool sensitive);
void gui_widget_queue_draw(GuiWidget *widget);

/* ------------------------------------------------------------------ */
/* GuiVisual（画面上に場所を持つWidget）                               */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t x;
    uint8_t y;
    uint8_t width;
    uint8_t height;
} GuiAllocation;

typedef struct {
    GuiWidget widget; /* 必ず先頭 */
    GuiAllocation allocation;
} GuiVisual;

typedef struct {
    GuiWidgetClass parent_class; /* 必ず先頭 */
} GuiVisualClass;

/* ------------------------------------------------------------------ */
/* GuiLabel（表示だけ。フォーカスを取らない）                          */
/* ------------------------------------------------------------------ */

typedef struct {
    GuiVisual visual; /* 必ず先頭 */
    const char *text;
} GuiLabel;

typedef struct {
    GuiVisualClass parent_class; /* 必ず先頭 */
} GuiLabelClass;

extern const GuiLabelClass GUI_LABEL_CLASS;

void gui_label_init(GuiLabel *label,
                    uint8_t x, uint8_t y, uint8_t width,
                    const char *text);

/* ------------------------------------------------------------------ */
/* GuiItem（フォーカスを取り、方向リンクを持つWidget）                 */
/* ------------------------------------------------------------------ */

typedef struct GuiItem GuiItem;

/* 単一リストではなく、方向ごとの明示リンクにする。
 * 2列配置やファンクションキーへ、画面ごとに素直に対応できる。 */
typedef struct {
    GuiItem *left;
    GuiItem *right;
    GuiItem *up;
    GuiItem *down;
    GuiItem *function[GUI_FUNCTION_KEY_COUNT];
} GuiItemLinks;

struct GuiItem {
    GuiVisual visual; /* 必ず先頭 */
    GuiItemLinks links;
};

typedef struct {
    GuiVisualClass parent_class; /* 必ず先頭 */
    void (*select)(GuiItem *item);
    void (*deselect)(GuiItem *item);
    void (*focus_in)(GuiItem *item);
    void (*focus_out)(GuiItem *item);
} GuiItemClass;

void gui_item_focus_in(GuiItem *item);
void gui_item_focus_out(GuiItem *item);
GuiItem *gui_item_link(const GuiItem *item, GuiKey key);

/* ------------------------------------------------------------------ */
/* GuiButton                                                           */
/* ------------------------------------------------------------------ */

typedef struct GuiButton GuiButton;

struct GuiButton {
    GuiItem item; /* 必ず先頭 */
    const char *label;
    void (*on_execute)(GuiButton *button, void *user_data);
    void *user_data;
};

typedef struct {
    GuiItemClass parent_class; /* 必ず先頭 */
    void (*execute)(GuiButton *button);
} GuiButtonClass;

extern const GuiButtonClass GUI_BUTTON_CLASS;

void gui_button_init(GuiButton *button,
                     uint8_t x, uint8_t y, uint8_t width,
                     const char *label,
                     void (*on_execute)(GuiButton *, void *),
                     void *user_data);

/* ------------------------------------------------------------------ */
/* GuiNamedItem（「名前：値」で表示する編集系の共通部）                */
/* ------------------------------------------------------------------ */

typedef struct {
    GuiItem item; /* 必ず先頭 */
    const char *name;
} GuiNamedItem;

typedef struct {
    GuiItemClass parent_class; /* 必ず先頭 */
} GuiNamedItemClass;

/* ------------------------------------------------------------------ */
/* GuiCombo                                                            */
/* ------------------------------------------------------------------ */

typedef struct GuiCombo GuiCombo;

struct GuiCombo {
    GuiNamedItem named; /* 必ず先頭 */
    const char *const *items;
    uint8_t item_count;
    uint8_t value;    /* 確定値 */
    uint8_t editing;  /* 編集中の候補 */
    uint8_t original; /* 編集開始時のスナップショット */
    void (*on_change)(GuiCombo *combo, uint8_t candidate, void *user_data);
    void (*on_submit)(GuiCombo *combo, uint8_t value, void *user_data);
    void (*on_cancel)(GuiCombo *combo, uint8_t original, void *user_data);
    void *user_data;
};

typedef struct {
    GuiNamedItemClass parent_class; /* 必ず先頭 */
    void (*change)(GuiCombo *combo, uint8_t candidate);
    void (*submit)(GuiCombo *combo);
    void (*cancel)(GuiCombo *combo);
} GuiComboClass;

extern const GuiComboClass GUI_COMBO_CLASS;

void gui_combo_init(GuiCombo *combo,
                    uint8_t x, uint8_t y, uint8_t width,
                    const char *name,
                    const char *const *items, uint8_t item_count,
                    uint8_t value);
uint8_t gui_combo_value(const GuiCombo *combo);

/* ------------------------------------------------------------------ */
/* GuiSpin                                                             */
/* ------------------------------------------------------------------ */

typedef struct GuiSpin GuiSpin;

struct GuiSpin {
    GuiNamedItem named; /* 必ず先頭 */
    int16_t value;
    int16_t editing;
    int16_t original;
    int16_t min;
    int16_t max;
    int16_t step;
    void (*on_change)(GuiSpin *spin, int16_t candidate, void *user_data);
    void (*on_submit)(GuiSpin *spin, int16_t value, void *user_data);
    void (*on_cancel)(GuiSpin *spin, int16_t original, void *user_data);
    void *user_data;
};

typedef struct {
    GuiNamedItemClass parent_class; /* 必ず先頭 */
    void (*change)(GuiSpin *spin, int16_t candidate);
    void (*submit)(GuiSpin *spin);
    void (*cancel)(GuiSpin *spin);
} GuiSpinClass;

extern const GuiSpinClass GUI_SPIN_CLASS;

void gui_spin_init(GuiSpin *spin,
                   uint8_t x, uint8_t y, uint8_t width,
                   const char *name,
                   int16_t min, int16_t max, int16_t step,
                   int16_t value);
int16_t gui_spin_value(const GuiSpin *spin);

/* ------------------------------------------------------------------ */
/* GuiPage（画面。子Widgetの静的配列と選択中Widgetを管理する）         */
/* ------------------------------------------------------------------ */

typedef struct GuiPage GuiPage;

struct GuiPage {
    GuiWidget widget; /* 必ず先頭 */
    GuiWidget **children;
    uint8_t child_count;
    GuiWidget *selected_child;
    const GuiDisplayPort *display;
};

typedef struct {
    GuiWidgetClass parent_class; /* 必ず先頭 */
    void (*before_show)(GuiPage *page);
    void (*after_hide)(GuiPage *page);
    void (*time_up)(GuiPage *page);
    void (*repaint)(GuiPage *page);
} GuiPageClass;

extern const GuiPageClass GUI_PAGE_CLASS;

/* 基底Pageのshow／hideを指す初期化子。
 * Cでは他のconstオブジェクトを静的初期化子に使えないため、
 * 派生Classもコンパイル時にROMへ置けるようマクロで公開する。 */
void gui_page_widget_show(GuiWidget *widget);
void gui_page_widget_hide(GuiWidget *widget);

#define GUI_PAGE_WIDGET_CLASS_INITIALIZER \
    {                                     \
        .show      = gui_page_widget_show,\
        .hide      = gui_page_widget_hide,\
        .key_down  = NULL,                \
        .key_press = NULL,                \
        .key_up    = NULL                 \
    }

void gui_page_init(GuiPage *page,
                   const GuiPageClass *klass,
                   GuiWidget **children, uint8_t child_count,
                   const GuiDisplayPort *display);
void gui_page_set_focus(GuiPage *page, GuiItem *item);
void gui_page_show(GuiPage *page);
void gui_page_hide(GuiPage *page);
void gui_page_time_up(GuiPage *page);
void gui_page_repaint(GuiPage *page);
bool gui_page_dispatch_key_down(GuiPage *page, GuiKey key);

#endif /* GUI_H */

/*
 * gui.c - gui.h の実装
 *
 * 方針:
 *   - malloc / calloc / realloc / free を使わない
 *   - クラス関数表はconstでROMへ置き、インスタンス側はポインタ1本だけ持つ
 *   - 再描画はDIRTYフラグの立ったWidgetだけ
 */
#include "gui.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* 派生型の先頭に親型が来ていることを、コンパイル時に固定する。 */
_Static_assert(offsetof(GuiVisual, widget) == 0, "GuiVisual head");
_Static_assert(offsetof(GuiLabel, visual) == 0, "GuiLabel head");
_Static_assert(offsetof(GuiItem, visual) == 0, "GuiItem head");
_Static_assert(offsetof(GuiButton, item) == 0, "GuiButton head");
_Static_assert(offsetof(GuiNamedItem, item) == 0, "GuiNamedItem head");
_Static_assert(offsetof(GuiCombo, named) == 0, "GuiCombo head");
_Static_assert(offsetof(GuiSpin, named) == 0, "GuiSpin head");
_Static_assert(offsetof(GuiPage, widget) == 0, "GuiPage head");

_Static_assert(offsetof(GuiVisualClass, parent_class) == 0, "VisualClass head");
_Static_assert(offsetof(GuiLabelClass, parent_class) == 0, "LabelClass head");
_Static_assert(offsetof(GuiItemClass, parent_class) == 0, "ItemClass head");
_Static_assert(offsetof(GuiButtonClass, parent_class) == 0, "ButtonClass head");
_Static_assert(offsetof(GuiNamedItemClass, parent_class) == 0, "NamedClass head");
_Static_assert(offsetof(GuiComboClass, parent_class) == 0, "ComboClass head");
_Static_assert(offsetof(GuiSpinClass, parent_class) == 0, "SpinClass head");
_Static_assert(offsetof(GuiPageClass, parent_class) == 0, "PageClass head");

#define GUI_TEXT_MAX 32

/* ------------------------------------------------------------------ */
/* GuiWidget                                                           */
/* ------------------------------------------------------------------ */

void gui_widget_show(GuiWidget *widget)
{
    if (widget->klass->show != NULL) {
        widget->klass->show(widget);
    }
}

void gui_widget_hide(GuiWidget *widget)
{
    if (widget->klass->hide != NULL) {
        widget->klass->hide(widget);
    }
}

bool gui_widget_key_down(GuiWidget *widget, GuiKey key)
{
    if (widget->klass->key_down == NULL) {
        return false;
    }
    return widget->klass->key_down(widget, key);
}

bool gui_widget_key_press(GuiWidget *widget, GuiKey key)
{
    if (widget->klass->key_press == NULL) {
        return false;
    }
    return widget->klass->key_press(widget, key);
}

bool gui_widget_key_up(GuiWidget *widget, GuiKey key)
{
    if (widget->klass->key_up == NULL) {
        return false;
    }
    return widget->klass->key_up(widget, key);
}

/* Item派生（Button／Combo／Spin）かどうか。GuiItem *へのキャスト前に使う。 */
bool gui_widget_is_item(const GuiWidget *widget)
{
    return widget != NULL &&
           (widget->type == GUI_WIDGET_BUTTON ||
            widget->type == GUI_WIDGET_COMBO ||
            widget->type == GUI_WIDGET_SPIN);
}

void gui_widget_queue_draw(GuiWidget *widget)
{
    widget->flags |= GUI_FLAG_DIRTY;
}

void gui_widget_set_visible(GuiWidget *widget, bool visible)
{
    if (visible) {
        widget->flags |= GUI_FLAG_VISIBLE;
    } else {
        widget->flags &= (uint8_t)~GUI_FLAG_VISIBLE;
    }
    gui_widget_queue_draw(widget);
}

void gui_widget_set_sensitive(GuiWidget *widget, bool sensitive)
{
    if (sensitive) {
        widget->flags |= GUI_FLAG_SENSITIVE;
    } else {
        widget->flags &= (uint8_t)~GUI_FLAG_SENSITIVE;
    }
    gui_widget_queue_draw(widget);
}

static bool widget_selectable(const GuiWidget *widget)
{
    const uint8_t need = GUI_FLAG_VISIBLE | GUI_FLAG_SENSITIVE
                       | GUI_FLAG_FOCUSABLE;

    return (widget->flags & need) == need;
}

/* ------------------------------------------------------------------ */
/* 描画ヘルパ                                                          */
/* ------------------------------------------------------------------ */

/* ページ描画中だけ有効な表示ポート。Widget全部へ持たせるとRAMが増える。 */
static const GuiDisplayPort *g_display;

static void draw_padded(const GuiAllocation *allocation,
                        uint8_t offset,
                        const char *text)
{
    char line[GUI_TEXT_MAX + 1];
    size_t width;
    size_t i;
    size_t length;

    if (g_display == NULL || allocation->width <= offset) {
        return;
    }
    width = (size_t)(allocation->width - offset);
    if (width > GUI_TEXT_MAX) {
        width = GUI_TEXT_MAX;
    }

    length = strlen(text);
    for (i = 0; i < width; i++) {
        line[i] = (i < length) ? text[i] : ' ';
    }
    line[width] = '\0';

    g_display->ops->draw_text(g_display->context,
                              (uint8_t)(allocation->x + offset),
                              allocation->y,
                              line);
}

static void draw_marker(const GuiVisual *visual, GuiIcon icon)
{
    if (g_display == NULL) {
        return;
    }
    g_display->ops->draw_icon(g_display->context,
                              visual->allocation.x,
                              visual->allocation.y,
                              icon);
}

static GuiIcon item_marker(const GuiItem *item)
{
    if ((item->visual.widget.flags & GUI_FLAG_EDITING) != 0) {
        return GUI_ICON_EDITING;
    }
    if ((item->visual.widget.flags & GUI_FLAG_SENSITIVE) == 0) {
        return GUI_ICON_LOCKED;
    }
    if ((item->visual.widget.flags & GUI_FLAG_FOCUSED) != 0) {
        return GUI_ICON_CURSOR;
    }
    return GUI_ICON_NONE;
}

static void visual_init(GuiVisual *visual,
                        const GuiWidgetClass *klass,
                        GuiWidgetType type,
                        uint8_t x, uint8_t y, uint8_t width,
                        uint8_t flags)
{
    visual->widget.klass = klass;
    visual->widget.parent = NULL; /* 親はPageへ組み込むときに設定される */
    visual->widget.type = (uint8_t)type;
    visual->widget.flags = (uint8_t)(GUI_FLAG_VISIBLE | GUI_FLAG_DIRTY | flags);
    visual->allocation.x = x;
    visual->allocation.y = y;
    visual->allocation.width = width;
    visual->allocation.height = 1;
}

/* ------------------------------------------------------------------ */
/* GuiLabel                                                            */
/* ------------------------------------------------------------------ */

static void label_show(GuiWidget *widget)
{
    GuiLabel *label = (GuiLabel *)widget;

    draw_padded(&label->visual.allocation, 0, label->text);
}

const GuiLabelClass GUI_LABEL_CLASS = {
    .parent_class = {
        .parent_class = {
            .show = label_show,
            .hide = NULL,
            .key_down = NULL,
            .key_press = NULL,
            .key_up = NULL
        }
    }
};

void gui_label_init(GuiLabel *label,
                    uint8_t x, uint8_t y, uint8_t width,
                    const char *text)
{
    /* Labelはフォーカスを取らない。ここがItemとの違い。 */
    visual_init(&label->visual,
                (const GuiWidgetClass *)&GUI_LABEL_CLASS,
                GUI_WIDGET_LABEL,
                x, y, width, 0);
    label->text = text;
}

/* ------------------------------------------------------------------ */
/* GuiItem 共通                                                        */
/* ------------------------------------------------------------------ */

static void item_init(GuiItem *item,
                      const GuiWidgetClass *klass,
                      GuiWidgetType type,
                      uint8_t x, uint8_t y, uint8_t width)
{
    size_t i;

    visual_init(&item->visual, klass, type, x, y, width,
                GUI_FLAG_SENSITIVE | GUI_FLAG_FOCUSABLE);
    item->links.left = NULL;
    item->links.right = NULL;
    item->links.up = NULL;
    item->links.down = NULL;
    for (i = 0; i < GUI_FUNCTION_KEY_COUNT; i++) {
        item->links.function[i] = NULL;
    }
}

void gui_item_focus_in(GuiItem *item)
{
    const GuiItemClass *klass = (const GuiItemClass *)item->visual.widget.klass;

    item->visual.widget.flags |= GUI_FLAG_FOCUSED;
    gui_widget_queue_draw(&item->visual.widget);
    if (klass->focus_in != NULL) {
        klass->focus_in(item);
    }
}

void gui_item_focus_out(GuiItem *item)
{
    const GuiItemClass *klass = (const GuiItemClass *)item->visual.widget.klass;

    item->visual.widget.flags &= (uint8_t)~GUI_FLAG_FOCUSED;
    gui_widget_queue_draw(&item->visual.widget);
    if (klass->focus_out != NULL) {
        klass->focus_out(item);
    }
}

GuiItem *gui_item_link(const GuiItem *item, GuiKey key)
{
    switch (key) {
    case GUI_KEY_LEFT:  return item->links.left;
    case GUI_KEY_RIGHT: return item->links.right;
    case GUI_KEY_UP:    return item->links.up;
    case GUI_KEY_DOWN:  return item->links.down;
    case GUI_KEY_F1:    return item->links.function[0];
    case GUI_KEY_F2:    return item->links.function[1];
    case GUI_KEY_F3:    return item->links.function[2];
    case GUI_KEY_F4:    return item->links.function[3];
    default:            return NULL;
    }
}

/* ------------------------------------------------------------------ */
/* GuiButton                                                           */
/* ------------------------------------------------------------------ */

static void button_show(GuiWidget *widget)
{
    GuiButton *button = (GuiButton *)widget;
    char text[GUI_TEXT_MAX + 1];

    snprintf(text, sizeof(text), "[%s]", button->label);
    draw_marker(&button->item.visual, item_marker(&button->item));
    draw_padded(&button->item.visual.allocation, 1, text);
}

static void button_execute(GuiButton *button)
{
    if (button->on_execute != NULL) {
        button->on_execute(button, button->user_data);
    }
}

static bool button_key_down(GuiWidget *widget, GuiKey key)
{
    GuiButton *button = (GuiButton *)widget;
    const GuiButtonClass *klass = (const GuiButtonClass *)widget->klass;

    if (key != GUI_KEY_ENTER) {
        return false;
    }
    if (klass->execute != NULL) {
        klass->execute(button);
    }
    return true;
}

const GuiButtonClass GUI_BUTTON_CLASS = {
    .parent_class = {
        .parent_class = {
            .parent_class = {
                .show = button_show,
                .hide = NULL,
                .key_down = button_key_down,
                .key_press = NULL,
                .key_up = NULL
            }
        },
        .select = NULL,
        .deselect = NULL,
        .focus_in = NULL,
        .focus_out = NULL
    },
    .execute = button_execute
};

void gui_button_init(GuiButton *button,
                     uint8_t x, uint8_t y, uint8_t width,
                     const char *label,
                     void (*on_execute)(GuiButton *, void *),
                     void *user_data)
{
    item_init(&button->item,
              (const GuiWidgetClass *)&GUI_BUTTON_CLASS,
              GUI_WIDGET_BUTTON,
              x, y, width);
    button->label = label;
    button->on_execute = on_execute;
    button->user_data = user_data;
}

/* ------------------------------------------------------------------ */
/* GuiCombo                                                            */
/* ------------------------------------------------------------------ */

static void combo_show(GuiWidget *widget)
{
    GuiCombo *combo = (GuiCombo *)widget;
    GuiItem *item = &combo->named.item;
    uint8_t index = ((widget->flags & GUI_FLAG_EDITING) != 0)
                  ? combo->editing : combo->value;
    char text[GUI_TEXT_MAX + 1];

    snprintf(text, sizeof(text), "%s:%s",
             combo->named.name, combo->items[index]);
    draw_marker(&item->visual, item_marker(item));
    draw_padded(&item->visual.allocation, 1, text);
}

static void combo_change(GuiCombo *combo, uint8_t candidate)
{
    combo->editing = candidate;
    gui_widget_queue_draw(&combo->named.item.visual.widget);
    if (combo->on_change != NULL) {
        combo->on_change(combo, candidate, combo->user_data);
    }
}

static void combo_submit(GuiCombo *combo)
{
    combo->value = combo->editing;
    combo->named.item.visual.widget.flags &= (uint8_t)~GUI_FLAG_EDITING;
    gui_widget_queue_draw(&combo->named.item.visual.widget);
    if (combo->on_submit != NULL) {
        combo->on_submit(combo, combo->value, combo->user_data);
    }
}

static void combo_cancel(GuiCombo *combo)
{
    combo->editing = combo->original;
    combo->value = combo->original;
    combo->named.item.visual.widget.flags &= (uint8_t)~GUI_FLAG_EDITING;
    gui_widget_queue_draw(&combo->named.item.visual.widget);
    if (combo->on_cancel != NULL) {
        combo->on_cancel(combo, combo->original, combo->user_data);
    }
}

static bool combo_key_down(GuiWidget *widget, GuiKey key)
{
    GuiCombo *combo = (GuiCombo *)widget;
    const GuiComboClass *klass = (const GuiComboClass *)widget->klass;

    if ((widget->flags & GUI_FLAG_EDITING) == 0) {
        if (key != GUI_KEY_ENTER) {
            return false;
        }
        /* 編集開始。ここで開始前の値を控える。 */
        combo->original = combo->value;
        combo->editing = combo->value;
        widget->flags |= GUI_FLAG_EDITING;
        gui_widget_queue_draw(widget);
        return true;
    }

    /* 編集中はキーを外へ逃がさない。フォーカスは動かない。 */
    switch (key) {
    case GUI_KEY_DOWN:
        if (combo->editing + 1 < combo->item_count) {
            klass->change(combo, (uint8_t)(combo->editing + 1));
        }
        break;
    case GUI_KEY_UP:
        if (combo->editing > 0) {
            klass->change(combo, (uint8_t)(combo->editing - 1));
        }
        break;
    case GUI_KEY_ENTER:
        klass->submit(combo);
        break;
    case GUI_KEY_CANCEL:
        klass->cancel(combo);
        break;
    default:
        break;
    }
    return true;
}

const GuiComboClass GUI_COMBO_CLASS = {
    .parent_class = {
        .parent_class = {
            .parent_class = {
                .parent_class = {
                    .show = combo_show,
                    .hide = NULL,
                    .key_down = combo_key_down,
                    .key_press = NULL,
                    .key_up = NULL
                }
            },
            .select = NULL,
            .deselect = NULL,
            .focus_in = NULL,
            .focus_out = NULL
        }
    },
    .change = combo_change,
    .submit = combo_submit,
    .cancel = combo_cancel
};

void gui_combo_init(GuiCombo *combo,
                    uint8_t x, uint8_t y, uint8_t width,
                    const char *name,
                    const char *const *items, uint8_t item_count,
                    uint8_t value)
{
    item_init(&combo->named.item,
              (const GuiWidgetClass *)&GUI_COMBO_CLASS,
              GUI_WIDGET_COMBO,
              x, y, width);
    combo->named.name = name;
    combo->items = items;
    combo->item_count = item_count;
    combo->value = value;
    combo->editing = value;
    combo->original = value;
    combo->on_change = NULL;
    combo->on_submit = NULL;
    combo->on_cancel = NULL;
    combo->user_data = NULL;
}

uint8_t gui_combo_value(const GuiCombo *combo)
{
    return combo->value;
}

/* ------------------------------------------------------------------ */
/* GuiSpin                                                             */
/* ------------------------------------------------------------------ */

static void spin_show(GuiWidget *widget)
{
    GuiSpin *spin = (GuiSpin *)widget;
    GuiItem *item = &spin->named.item;
    int16_t shown = ((widget->flags & GUI_FLAG_EDITING) != 0)
                  ? spin->editing : spin->value;
    char text[GUI_TEXT_MAX + 1];

    snprintf(text, sizeof(text), "%s:%d", spin->named.name, (int)shown);
    draw_marker(&item->visual, item_marker(item));
    draw_padded(&item->visual.allocation, 1, text);
}

static void spin_change(GuiSpin *spin, int16_t candidate)
{
    spin->editing = candidate;
    gui_widget_queue_draw(&spin->named.item.visual.widget);
    if (spin->on_change != NULL) {
        spin->on_change(spin, candidate, spin->user_data);
    }
}

static void spin_submit(GuiSpin *spin)
{
    spin->value = spin->editing;
    spin->named.item.visual.widget.flags &= (uint8_t)~GUI_FLAG_EDITING;
    gui_widget_queue_draw(&spin->named.item.visual.widget);
    if (spin->on_submit != NULL) {
        spin->on_submit(spin, spin->value, spin->user_data);
    }
}

static void spin_cancel(GuiSpin *spin)
{
    spin->editing = spin->original;
    spin->value = spin->original;
    spin->named.item.visual.widget.flags &= (uint8_t)~GUI_FLAG_EDITING;
    gui_widget_queue_draw(&spin->named.item.visual.widget);
    if (spin->on_cancel != NULL) {
        spin->on_cancel(spin, spin->original, spin->user_data);
    }
}

static bool spin_key_down(GuiWidget *widget, GuiKey key)
{
    GuiSpin *spin = (GuiSpin *)widget;
    const GuiSpinClass *klass = (const GuiSpinClass *)widget->klass;

    if ((widget->flags & GUI_FLAG_EDITING) == 0) {
        if (key != GUI_KEY_ENTER) {
            return false;
        }
        spin->original = spin->value;
        spin->editing = spin->value;
        widget->flags |= GUI_FLAG_EDITING;
        gui_widget_queue_draw(widget);
        return true;
    }

    switch (key) {
    case GUI_KEY_UP:
        if (spin->editing + spin->step <= spin->max) {
            klass->change(spin, (int16_t)(spin->editing + spin->step));
        }
        break;
    case GUI_KEY_DOWN:
        if (spin->editing - spin->step >= spin->min) {
            klass->change(spin, (int16_t)(spin->editing - spin->step));
        }
        break;
    case GUI_KEY_ENTER:
        klass->submit(spin);
        break;
    case GUI_KEY_CANCEL:
        klass->cancel(spin);
        break;
    default:
        break;
    }
    return true;
}

const GuiSpinClass GUI_SPIN_CLASS = {
    .parent_class = {
        .parent_class = {
            .parent_class = {
                .parent_class = {
                    .show = spin_show,
                    .hide = NULL,
                    .key_down = spin_key_down,
                    .key_press = NULL,
                    .key_up = NULL
                }
            },
            .select = NULL,
            .deselect = NULL,
            .focus_in = NULL,
            .focus_out = NULL
        }
    },
    .change = spin_change,
    .submit = spin_submit,
    .cancel = spin_cancel
};

void gui_spin_init(GuiSpin *spin,
                   uint8_t x, uint8_t y, uint8_t width,
                   const char *name,
                   int16_t min, int16_t max, int16_t step,
                   int16_t value)
{
    item_init(&spin->named.item,
              (const GuiWidgetClass *)&GUI_SPIN_CLASS,
              GUI_WIDGET_SPIN,
              x, y, width);
    spin->named.name = name;
    spin->min = min;
    spin->max = max;
    spin->step = step;
    spin->value = value;
    spin->editing = value;
    spin->original = value;
    spin->on_change = NULL;
    spin->on_submit = NULL;
    spin->on_cancel = NULL;
    spin->user_data = NULL;
}

int16_t gui_spin_value(const GuiSpin *spin)
{
    return spin->value;
}

/* ------------------------------------------------------------------ */
/* GuiPage                                                             */
/* ------------------------------------------------------------------ */

void gui_page_widget_show(GuiWidget *widget)
{
    GuiPage *page = (GuiPage *)widget;
    const GuiPageClass *klass = (const GuiPageClass *)widget->klass;
    uint8_t i;

    /* 表示の前に、装置の現在状態を画面のモデルへ読み込む。 */
    if (klass->before_show != NULL) {
        klass->before_show(page);
    }

    g_display = page->display;
    g_display->ops->clear(g_display->context);
    for (i = 0; i < page->child_count; i++) {
        GuiWidget *child = page->children[i];

        if ((child->flags & GUI_FLAG_VISIBLE) != 0) {
            gui_widget_show(child);
        }
        child->flags &= (uint8_t)~GUI_FLAG_DIRTY;
    }
    g_display->ops->flush(g_display->context);
    g_display = NULL;
}

void gui_page_widget_hide(GuiWidget *widget)
{
    GuiPage *page = (GuiPage *)widget;
    const GuiPageClass *klass = (const GuiPageClass *)widget->klass;

    if (klass->after_hide != NULL) {
        klass->after_hide(page);
    }
}

const GuiPageClass GUI_PAGE_CLASS = {
    .parent_class = GUI_PAGE_WIDGET_CLASS_INITIALIZER,
    .before_show = NULL,
    .after_hide = NULL,
    .time_up = NULL,
    .repaint = NULL
};

void gui_page_init(GuiPage *page,
                   const GuiPageClass *klass,
                   GuiWidget **children, uint8_t child_count,
                   const GuiDisplayPort *display)
{
    uint8_t i;

    page->widget.klass = (const GuiWidgetClass *)klass;
    page->widget.parent = NULL; /* 最上位のPageなのでNULL */
    page->widget.type = (uint8_t)GUI_WIDGET_PAGE;
    page->widget.flags = GUI_FLAG_VISIBLE | GUI_FLAG_DIRTY;
    /* 所有関係はPage側の配列が持ち、子は親へ戻る参照だけを持つ。 */
    for (i = 0; i < child_count; i++) {
        children[i]->parent = &page->widget;
    }
    page->children = children;
    page->child_count = child_count;
    page->selected_child = NULL;
    page->display = display;
}

void gui_page_set_focus(GuiPage *page, GuiItem *item)
{
    /* フォーカスを持てるのはItem派生だけ。キャスト前に型で確認する。 */
    assert(item == NULL || gui_widget_is_item(&item->visual.widget));
    if (page->selected_child != NULL) {
        assert(gui_widget_is_item(page->selected_child));
        gui_item_focus_out((GuiItem *)page->selected_child);
    }
    page->selected_child = (item != NULL) ? &item->visual.widget : NULL;
    if (item != NULL) {
        gui_item_focus_in(item);
    }
}

void gui_page_show(GuiPage *page)
{
    gui_widget_show(&page->widget);
}

void gui_page_hide(GuiPage *page)
{
    gui_widget_hide(&page->widget);
}

void gui_page_time_up(GuiPage *page)
{
    const GuiPageClass *klass = (const GuiPageClass *)page->widget.klass;

    if (klass->time_up != NULL) {
        klass->time_up(page);
    }
}

/* DIRTYの立った子だけを描き直す。全面クリアはしない。 */
void gui_page_repaint(GuiPage *page)
{
    const GuiPageClass *klass = (const GuiPageClass *)page->widget.klass;
    uint8_t i;

    if (klass->repaint != NULL) {
        klass->repaint(page);
    }

    g_display = page->display;
    for (i = 0; i < page->child_count; i++) {
        GuiWidget *child = page->children[i];

        if ((child->flags & GUI_FLAG_DIRTY) == 0) {
            continue;
        }
        if ((child->flags & GUI_FLAG_VISIBLE) != 0) {
            gui_widget_show(child);
        }
        child->flags &= (uint8_t)~GUI_FLAG_DIRTY;
    }
    g_display->ops->flush(g_display->context);
    g_display = NULL;
}

bool gui_page_dispatch_key_down(GuiPage *page, GuiKey key)
{
    GuiItem *current;
    GuiItem *next;
    uint8_t guard;

    if (page->selected_child == NULL) {
        return false;
    }

    /* 1. まず選択中のWidgetへ渡す。消費されたらそこで終わり。 */
    if (gui_widget_key_down(page->selected_child, key)) {
        gui_page_repaint(page);
        return true;
    }

    /* 2. 消費されなければ、方向リンクをたどってフォーカスを移す。 */
    current = (GuiItem *)page->selected_child;
    next = gui_item_link(current, key);
    for (guard = 0; next != NULL && guard < 16u; guard++) {
        if (widget_selectable(&next->visual.widget)) {
            gui_page_set_focus(page, next);
            gui_page_repaint(page);
            return true;
        }
        /* 非表示や無効のItemは選ばず、同じ方向へ読み飛ばす。 */
        next = gui_item_link(next, key);
    }
    return false;
}

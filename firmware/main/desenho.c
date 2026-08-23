#include "desenho.h"

void so_decoracao(lv_obj_t *o)
{
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_EVENT_BUBBLE);   /* gesto sobe para o pai */
}

lv_obj_t *disco(lv_obj_t *pai, int d, lv_color_t cor, int x, int y)
{
    lv_obj_t *o = lv_obj_create(pai);
    lv_obj_set_size(o, d, d);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_bg_color(o, cor, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    so_decoracao(o);
    lv_obj_align(o, LV_ALIGN_CENTER, x, y);
    return o;
}

lv_obj_t *barra(lv_obj_t *pai, int w, int h, lv_color_t cor, int x, int y)
{
    lv_obj_t *o = lv_obj_create(pai);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, h < w ? h / 2 : w / 2, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_bg_color(o, cor, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    so_decoracao(o);
    lv_obj_align(o, LV_ALIGN_CENTER, x, y);
    return o;
}

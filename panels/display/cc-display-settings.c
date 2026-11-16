/* cc-display-settings.c
 *
 * Copyright (C) 2007, 2008, 2018, 2019  Red Hat, Inc.
 * Copyright (C) 2013 Intel, Inc.
 *
 * Written by: Benjamin Berg <bberg@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <glib/gi18n.h>
#include <math.h>
#include "cc-display-settings.h"
#include "cc-display-config.h"


#define WID(s) GTK_WIDGET (gtk_builder_get_object (self->builder, s))

struct _CcDisplaySettings
{
  GtkBin            parent_instance;

  gboolean          updating;
  guint             idle_udpate_id;

  gboolean          has_accelerometer;
  CcDisplayConfig  *config;
  CcDisplayMonitor *selected_output;

  GtkListStore     *orientation_list;
  GtkListStore     *refresh_rate_list;
  GtkListStore     *resolution_list;

  GtkBuilder       *builder;

  GtkWidget        *orientation_combo;
  GtkWidget        *refresh_rate_combo;
  GtkWidget        *resolution_combo;
  GtkWidget        *scale_combo;
  GtkListStore     *scale_list;
  GtkWidget        *scale_row;
  GtkWidget        *scale_label;
  GtkWidget        *underscanning_row;
  GtkWidget        *underscanning_switch;
  GtkWidget        *color_row;
  GtkWidget        *color_range_combo;
  GtkWidget        *color_advanced_button;
  GtkWidget        *color_advanced_popover;
  GtkWidget        *color_format_combo;
  GtkWidget        *hdr_row;
  GtkWidget        *hdr_combo;
  GtkWidget        *hdr_advanced_button;
  GtkWidget        *sdr_white_label;
  GtkWidget        *sdr_white_box;
  GtkWidget        *sdr_white_spin;
  GtkWidget        *sdr_white_help;
  GtkWidget        *hdr_peak_label;
  GtkWidget        *hdr_peak_box;
  GtkWidget        *hdr_peak_spin;
  GtkWidget        *hdr_peak_help;
  GtkWidget        *hdr_advanced_popover;

};

typedef struct _CcDisplaySettings CcDisplaySettings;

enum {
  PROP_0,
  PROP_HAS_ACCELEROMETER,
  PROP_CONFIG,
  PROP_SELECTED_OUTPUT,
  PROP_LAST
};

G_DEFINE_TYPE (CcDisplaySettings, cc_display_settings, GTK_TYPE_BIN)

static GParamSpec *props[PROP_LAST];

static void on_scale_selection_changed_cb (GtkComboBox       *box,
                                           GParamSpec        *pspec,
                                           CcDisplaySettings *self);

static gboolean
should_show_rotation (CcDisplaySettings *self)
{
  gboolean supports_rotation;

  supports_rotation = cc_display_monitor_supports_rotation (self->selected_output,
                                                            CC_DISPLAY_ROTATION_90 |
                                                            CC_DISPLAY_ROTATION_180 |
                                                            CC_DISPLAY_ROTATION_270);

  /* Doesn't support rotation at all */
  if (!supports_rotation)
    return FALSE;

  /* We can always rotate displays that aren't builtin */
  if (!cc_display_monitor_is_builtin (self->selected_output))
    return TRUE;

  /* Only offer rotation if there's no accelerometer */
  return !self->has_accelerometer;
}

static const gchar *
string_for_rotation (CcDisplayRotation rotation)
{
  switch (rotation)
    {
    case CC_DISPLAY_ROTATION_NONE:
    case CC_DISPLAY_ROTATION_180_FLIPPED:
      return C_("Display rotation", "Landscape");
    case CC_DISPLAY_ROTATION_90:
    case CC_DISPLAY_ROTATION_270_FLIPPED:
      return C_("Display rotation", "Portrait Right");
    case CC_DISPLAY_ROTATION_270:
    case CC_DISPLAY_ROTATION_90_FLIPPED:
      return C_("Display rotation", "Portrait Left");
    case CC_DISPLAY_ROTATION_180:
    case CC_DISPLAY_ROTATION_FLIPPED:
      return C_("Display rotation", "Landscape (flipped)");
    }
  return "";
}

static const gchar *
make_aspect_string (gint width,
                    gint height)
{
  int ratio;
  const gchar *aspect = NULL;

    /* We use a number of Unicode characters below:
     * ∶ is U+2236 RATIO
     *   is U+2009 THIN SPACE,
     * × is U+00D7 MULTIPLICATION SIGN
     */
  if (width && height) {
    if (width > height)
      ratio = width * 10 / height;
    else
      ratio = height * 10 / width;

    switch (ratio) {
    case 13:
      aspect = "4∶3";
      break;
    case 16:
      aspect = "16∶10";
      break;
    case 17:
      aspect = "16∶9";
      break;
    case 23:
      aspect = "21∶9";
      break;
    case 12:
      aspect = "5∶4";
      break;
      /* This catches 1.5625 as well (1600x1024) when maybe it shouldn't. */
    case 15:
      aspect = "3∶2";
      break;
    case 18:
      aspect = "9∶5";
      break;
    case 10:
      aspect = "1∶1";
      break;
    }
  }

  return aspect;
}

static char *
make_resolution_string (CcDisplayMode *mode)
{
  const char *interlaced = cc_display_mode_is_interlaced (mode) ? "i" : "";
  const char *aspect;
  int width, height;

  cc_display_mode_get_resolution (mode, &width, &height);
  aspect = make_aspect_string (width, height);

  if (aspect != NULL)
    return g_strdup_printf ("%d × %d%s (%s)", width, height, interlaced, aspect);
  else
    return g_strdup_printf ("%d × %d%s", width, height, interlaced);
}

static gchar *
get_frequency_string (CcDisplayMode *mode)
{
  return g_strdup_printf (_("%.2lf Hz"), cc_display_mode_get_freq_f (mode));
}

static gchar *
make_scale_string (gdouble scale, gint width, gint height)
{
  /* Muffin offers exact quotients, which include thirds and fifths. Rounding
   * the label to the nearest quarter would render 1.25 and 1.3333 both as
   * "125 %" - two entries selecting different scales. */
  return g_strdup_printf ("%d%% (%d × %d)",
                          (int) round (scale * 100),
                          (int) round (width / scale),
                          (int) round (height / scale));
}

static gint
sort_modes_by_area_desc (CcDisplayMode *a, CcDisplayMode *b)
{
  gint wa, ha, wb, hb;
  gint res;

  cc_display_mode_get_resolution (a, &wa, &ha);
  cc_display_mode_get_resolution (b, &wb, &hb);

  /* Prefer wide screen if the size is equal */
  res = wb*hb - wa*ha;
  if (res == 0)
    return wb - wa;
  return res;
}

static void update_hdr_advanced (CcDisplaySettings *self);
static void update_color (CcDisplaySettings *self);

static gboolean
cc_display_settings_rebuild_ui (CcDisplaySettings *self)
{
  GList *modes;
  GList *item;
  gint width, height;
  CcDisplayMode *current_mode;
  gint n_scales = 0;
  gint mode_width, mode_height;
  const gdouble *scales, *scale;

  self->idle_udpate_id = 0;

  if (!self->config || !self->selected_output)
    {
      gtk_widget_set_sensitive (self->orientation_combo, FALSE);
      gtk_widget_set_sensitive (self->refresh_rate_combo, FALSE);
      gtk_widget_set_sensitive (self->resolution_combo, FALSE);
      gtk_widget_set_sensitive (self->scale_row, FALSE);
      gtk_widget_set_sensitive (self->underscanning_row, FALSE);
      gtk_widget_set_sensitive (self->hdr_row, FALSE);
      gtk_widget_set_sensitive (self->color_row, FALSE);

      return G_SOURCE_REMOVE;
    }

  g_object_freeze_notify ((GObject*) self->orientation_combo);
  g_object_freeze_notify ((GObject*) self->refresh_rate_combo);
  g_object_freeze_notify ((GObject*) self->resolution_combo);
  g_object_freeze_notify ((GObject*) self->underscanning_switch);

  cc_display_monitor_get_geometry (self->selected_output, NULL, NULL, &width, &height);

  /* Selecte the first mode we can find if the monitor is disabled. */
  current_mode = cc_display_monitor_get_mode (self->selected_output);
  if (current_mode == NULL)
    current_mode = cc_display_monitor_get_preferred_mode (self->selected_output);
  if (current_mode == NULL) {
    modes = cc_display_monitor_get_modes (self->selected_output);
    /* Lets assume that a monitor always has at least one mode. */
    g_assert (modes);
    current_mode = CC_DISPLAY_MODE (modes->data);
  }

  if (should_show_rotation (self))
    {
      guint i;
      CcDisplayRotation rotations[] = { CC_DISPLAY_ROTATION_NONE,
                                        CC_DISPLAY_ROTATION_90,
                                        CC_DISPLAY_ROTATION_270,
                                        CC_DISPLAY_ROTATION_180 };

      gtk_widget_set_sensitive (self->orientation_combo, TRUE);

      gtk_list_store_clear (self->orientation_list);
      for (i = 0; i < G_N_ELEMENTS (rotations); i++)
        {
          if (!cc_display_monitor_supports_rotation (self->selected_output, rotations[i]))
            continue;

          GtkTreeIter iter;

          gtk_list_store_append (self->orientation_list, &iter);

          gtk_list_store_set (self->orientation_list,
                              &iter,
                              0, string_for_rotation (rotations[i]),
                              1, rotations[i],
                              -1);

          if (cc_display_monitor_get_rotation (self->selected_output) == rotations[i])
            gtk_combo_box_set_active_iter (GTK_COMBO_BOX (self->orientation_combo), &iter);
        }
    }
  else
    {
      gtk_widget_set_sensitive (self->orientation_combo, FALSE);
    }

  /* Only show refresh rate if we are not in cloning mode. */
  if (!cc_display_config_is_cloning (self->config))
    {
      GList *item;
      gdouble freq;

      freq = cc_display_mode_get_freq_f (current_mode);

      modes = g_list_copy (cc_display_monitor_get_modes (self->selected_output));
      modes = g_list_reverse (modes);

      gtk_list_store_clear (self->refresh_rate_list);

      for (item = modes; item != NULL; item = item->next)
        {
          gint w, h;
          CcDisplayMode *mode = CC_DISPLAY_MODE (item->data);

          cc_display_mode_get_resolution (mode, &w, &h);
          if (w != width || h != height)
            continue;

          GtkTreeIter iter;

          gtk_list_store_append (self->refresh_rate_list, &iter);

          gchar *rate_string = get_frequency_string (mode);

          gtk_list_store_set (self->refresh_rate_list,
                              &iter,
                              0, rate_string,
                              1, mode,
                              -1);

          g_free (rate_string);
          /* At some point we used to filter very close resolutions,
           * but we don't anymore these days.
           */
          if (freq == cc_display_mode_get_freq_f (mode))
            gtk_combo_box_set_active_iter (GTK_COMBO_BOX (self->refresh_rate_combo), &iter);
        }

      g_list_free (modes);

      /* Show if we have more than one frequency to choose from. */
      gtk_widget_set_sensitive (self->refresh_rate_combo,
                                gtk_tree_model_iter_n_children (GTK_TREE_MODEL (self->refresh_rate_list), NULL) > 1);
    }
  else
    {
      gtk_widget_set_sensitive (self->refresh_rate_combo, FALSE);
    }

  if (cc_display_config_is_cloning (self->config))
    modes = g_list_copy (cc_display_config_get_cloning_modes (self->config));
  else
    modes = g_list_copy (cc_display_monitor_get_modes (self->selected_output));

  gtk_list_store_clear (self->resolution_list);

  modes = g_list_reverse (modes);
  GList *unique_resolutions = g_list_prepend (NULL, current_mode);
  GList *l;

  for (item = modes; item != NULL; item = item->next)
    {
      gint w, h;
      CcDisplayMode *mode = CC_DISPLAY_MODE (item->data);

      /* Exclude unusable low resolutions */
      if (!cc_display_config_is_scaled_mode_valid (self->config, mode, 1.0))
        continue;

      cc_display_mode_get_resolution (mode, &w, &h);

      gint ins = 0;

      for (l = unique_resolutions; l != NULL; l = l->next, ins++)
        {
          CcDisplayMode *m = l->data;
          gint cmp;

          cmp = sort_modes_by_area_desc (mode, m);

          if (cmp < 0)
            break;

          /* Don't insert if it is already in the list */
          if (cmp == 0)
            {
              ins = -1;
              break;
            }

        }

      if (ins >= 0)
        {
          unique_resolutions = g_list_insert (unique_resolutions, mode, ins);
        }
    }

  g_list_free (modes);

  for (l = unique_resolutions; l != NULL; l = l->next)
    {
      GtkTreeIter iter;
      gchar *resolution_string;

      gtk_list_store_append (self->resolution_list, &iter);
      resolution_string = make_resolution_string (l->data);

      gtk_list_store_set (self->resolution_list,
                          &iter,
                          0, resolution_string,
                          1, l->data,
                          -1);

      g_free (resolution_string);

      if (current_mode == l->data)
        {
           gtk_combo_box_set_active_iter (GTK_COMBO_BOX (self->resolution_combo), &iter);
        }
    }
  gtk_widget_set_sensitive (self->resolution_combo,
                            gtk_tree_model_iter_n_children (GTK_TREE_MODEL (self->resolution_list), NULL) > 1);
  g_list_free (unique_resolutions);

  /* Scale row is usually shown. */
  gtk_list_store_clear (self->scale_list);
  cc_display_mode_get_resolution (current_mode, &mode_width, &mode_height);
  scales = cc_display_mode_get_supported_scales (current_mode);
  for (scale = scales; *scale != 0.0; scale++)
    {
      g_autofree gchar *scale_str = NULL;
      GtkTreeIter iter;

      if (!cc_display_config_is_scaled_mode_valid (self->config,
                                                   current_mode,
                                                   *scale) &&
          cc_display_monitor_get_scale (self->selected_output) != *scale)
        continue;

      scale_str = make_scale_string (*scale, mode_width, mode_height);

      gtk_list_store_append (self->scale_list, &iter);
      gtk_list_store_set (self->scale_list,
                          &iter,
                          0, scale_str,
                          1, *scale,
                          -1);

      if (cc_display_monitor_get_scale (self->selected_output) == *scale)
        gtk_combo_box_set_active_iter (GTK_COMBO_BOX (self->scale_combo), &iter);

      n_scales += 1;
    }

  gtk_widget_set_sensitive (self->scale_row, n_scales > 1);
  gtk_widget_set_sensitive (self->scale_combo, n_scales > 1);

  if (cc_display_config_get_fractional_scaling (self->config))
    {
      gtk_label_set_text (GTK_LABEL (self->scale_label), _("Monitor scale"));
    }
  else
    {
      gtk_label_set_text (GTK_LABEL (self->scale_label), _("User interface scale"));
    }

  gtk_widget_set_visible (self->underscanning_row,
                          cc_display_monitor_supports_underscanning (self->selected_output) &&
                          !cc_display_config_is_cloning (self->config));
  gtk_widget_set_sensitive (self->underscanning_row, TRUE);
  gtk_switch_set_active (GTK_SWITCH (self->underscanning_switch),
                         cc_display_monitor_get_underscanning (self->selected_output));

  self->updating = TRUE;
  update_color (self);
  self->updating = FALSE;

  /* HDR, for monitors that can show it */
  self->updating = TRUE;
  gtk_combo_box_text_remove_all (GTK_COMBO_BOX_TEXT (self->hdr_combo));
  if (cc_display_monitor_supports_hdr (self->selected_output, CC_DISPLAY_HDR_ON))
    {
      CcDisplayHdr hdr = cc_display_monitor_get_hdr (self->selected_output);

      gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (self->hdr_combo), "off", _("Off"));
      if (cc_display_monitor_supports_hdr (self->selected_output, CC_DISPLAY_HDR_AUTOMATIC))
        gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (self->hdr_combo), "auto", _("Automatic"));
      gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (self->hdr_combo), "on", _("On"));
      gtk_combo_box_set_active_id (GTK_COMBO_BOX (self->hdr_combo),
                                   hdr == CC_DISPLAY_HDR_ON ? "on" :
                                   hdr == CC_DISPLAY_HDR_AUTOMATIC ? "auto" : "off");
      gtk_widget_set_visible (self->hdr_row, TRUE);
      update_hdr_advanced (self);
    }
  else
    {
      gtk_widget_set_visible (self->hdr_row, FALSE);
    }
  gtk_widget_set_sensitive (self->hdr_row, TRUE);
  self->updating = FALSE;

  self->updating = TRUE;
  g_object_thaw_notify ((GObject*) self->orientation_combo);
  g_object_thaw_notify ((GObject*) self->refresh_rate_combo);
  g_object_thaw_notify ((GObject*) self->resolution_combo);
  g_object_thaw_notify ((GObject*) self->underscanning_switch);
  self->updating = FALSE;

  return G_SOURCE_REMOVE;
}

static void
on_output_changed_cb (CcDisplaySettings *self,
                      GParamSpec        *pspec,
                      CcDisplayMonitor  *output)
{
  /* Do this frmo an idle handler, because otherwise we may create an
   * infinite loop triggering the notify::selected-index from the
   * combo rows. */
  if (self->idle_udpate_id)
    return;

  self->idle_udpate_id = g_idle_add ((GSourceFunc) cc_display_settings_rebuild_ui, self);
}

static void
on_orientation_selection_changed_cb (GtkComboBox       *box,
                                     GParamSpec        *pspec,
                                     CcDisplaySettings *panel)
{
  if (panel->updating)
    return;

  GtkTreeIter iter;
  if (gtk_combo_box_get_active_iter (box, &iter))
    {
      CcDisplayRotation rotation;

      gtk_tree_model_get (GTK_TREE_MODEL (panel->orientation_list),
                          &iter,
                          1, &rotation,
                          -1);

      cc_display_monitor_set_rotation (panel->selected_output, rotation);

      g_signal_emit_by_name (G_OBJECT (panel), "updated", panel->selected_output);
    }
}

static void
on_refresh_rate_selection_changed_cb (GtkComboBox       *box,
                                      GParamSpec        *pspec,
                                      CcDisplaySettings *panel)
{
  GtkTreeIter iter;

  if (panel->updating)
    return;

  if (gtk_combo_box_get_active_iter (box, &iter))
    {
      g_autoptr(CcDisplayMode) mode = NULL;

      gtk_tree_model_get (GTK_TREE_MODEL (panel->refresh_rate_list),
                          &iter,
                          1, &mode,
                          -1);

      cc_display_monitor_set_mode (panel->selected_output, mode);
      g_signal_emit_by_name (G_OBJECT (panel), "updated", panel->selected_output);
    }
}

static void
on_resolution_selection_changed_cb (GtkComboBox       *box,
                                    GParamSpec        *pspec,
                                    CcDisplaySettings *panel)
{
  GtkTreeIter iter;

  if (panel->updating)
    return;

  if (gtk_combo_box_get_active_iter (box, &iter))
    {
      g_autoptr(CcDisplayMode) mode = NULL;

      gtk_tree_model_get (GTK_TREE_MODEL (panel->resolution_list),
                          &iter,
                          1, &mode,
                          -1);

      /* This is the only row that can be changed when in cloning mode. */
      if (!cc_display_config_is_cloning (panel->config))
        cc_display_monitor_set_mode (panel->selected_output, mode);
      else
        cc_display_config_set_mode_on_all_outputs (panel->config, mode);

      g_signal_emit_by_name (G_OBJECT (panel), "updated", panel->selected_output);
    }
}

static void
on_scale_selection_changed_cb (GtkComboBox       *box,
                               GParamSpec        *pspec,
                               CcDisplaySettings *self)
{
  GtkTreeIter iter;
  gdouble scale;

  if (self->updating)
    return;

  if (!gtk_combo_box_get_active_iter (box, &iter))
    return;

  gtk_tree_model_get (GTK_TREE_MODEL (self->scale_list), &iter, 1, &scale, -1);
  cc_display_monitor_set_scale (self->selected_output, scale);

  g_signal_emit_by_name (G_OBJECT (self), "updated", self->selected_output);
}

static void
on_underscanning_switch_active_changed_cb (GtkWidget         *widget,
                                           GParamSpec        *pspec,
                                           CcDisplaySettings *self)
{
  if (self->updating)
    return;

  cc_display_monitor_set_underscanning (self->selected_output,
                                        gtk_switch_get_active (GTK_SWITCH (self->underscanning_switch)));

  g_signal_emit_by_name (G_OBJECT (self), "updated", self->selected_output);
}

/* Color range and, under Advanced, color format: shown for monitors whose
 * driver offers a choice in either (the NVIDIA driver has neither for
 * DisplayPort). Muffin lists only the formats the monitor allows in its
 * current mode. */
static const struct
{
  guint value;
  const gchar *id;
  const gchar *name;
} rgb_range_names[] = {
  { CC_DISPLAY_RGB_RANGE_AUTO, "1", N_("Automatic") },
  { CC_DISPLAY_RGB_RANGE_FULL, "2", N_("Full") },
  { CC_DISPLAY_RGB_RANGE_LIMITED, "3", N_("Limited") },
}, color_format_names[] = {
  { CC_DISPLAY_COLOR_FORMAT_AUTO, "1", N_("Automatic") },
  { CC_DISPLAY_COLOR_FORMAT_RGB, "2", N_("RGB") },
  { CC_DISPLAY_COLOR_FORMAT_YCBCR444, "3", N_("YCbCr 4:4:4") },
  { CC_DISPLAY_COLOR_FORMAT_YCBCR422, "4", N_("YCbCr 4:2:2") },
  { CC_DISPLAY_COLOR_FORMAT_YCBCR420, "5", N_("YCbCr 4:2:0") },
};

static gboolean
more_than_one (guint mask)
{
  return (mask & (mask - 1)) != 0;
}

static void
fill_combo (GtkWidget *combo,
            guint      supported,
            guint      current,
            gboolean   is_range)
{
  gsize i, n = is_range ? G_N_ELEMENTS (rgb_range_names) : G_N_ELEMENTS (color_format_names);

  gtk_combo_box_text_remove_all (GTK_COMBO_BOX_TEXT (combo));
  for (i = 0; i < n; i++)
    {
      guint value = is_range ? rgb_range_names[i].value : color_format_names[i].value;

      if (supported & (1u << value))
        gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (combo),
                                   is_range ? rgb_range_names[i].id : color_format_names[i].id,
                                   _(is_range ? rgb_range_names[i].name : color_format_names[i].name));
    }

  if (current >= 1 && current <= 9)
    {
      gchar id[2] = { '0' + current, '\0' };

      gtk_combo_box_set_active_id (GTK_COMBO_BOX (combo), id);
    }
}

static void
update_color (CcDisplaySettings *self)
{
  CcDisplayMonitor *output = self->selected_output;
  guint ranges, formats;

  ranges = cc_display_monitor_get_supported_rgb_ranges (output);
  formats = cc_display_monitor_get_supported_color_formats (output);

  if (!more_than_one (ranges) && !more_than_one (formats))
    {
      gtk_widget_set_visible (self->color_row, FALSE);
      return;
    }

  /* A driver with a color format but no color range (NVIDIA on HDMI)
   * shows the range as automatic */
  if (more_than_one (ranges))
    {
      fill_combo (self->color_range_combo, ranges,
                  cc_display_monitor_get_rgb_range (output), TRUE);
      gtk_widget_set_sensitive (self->color_range_combo, TRUE);
    }
  else
    {
      fill_combo (self->color_range_combo, 1u << CC_DISPLAY_RGB_RANGE_AUTO,
                  CC_DISPLAY_RGB_RANGE_AUTO, TRUE);
      gtk_widget_set_sensitive (self->color_range_combo, FALSE);
    }

  gtk_widget_set_visible (self->color_advanced_button, more_than_one (formats));
  if (more_than_one (formats))
    fill_combo (self->color_format_combo, formats,
                cc_display_monitor_get_color_format (output), FALSE);

  gtk_widget_set_visible (self->color_row, TRUE);
  gtk_widget_set_sensitive (self->color_row, TRUE);
}

static void
on_color_range_combo_changed_cb (GtkComboBox       *combo,
                                 CcDisplaySettings *self)
{
  const gchar *id;

  if (self->updating || !self->selected_output)
    return;

  id = gtk_combo_box_get_active_id (combo);
  if (id == NULL)
    return;

  cc_display_monitor_set_rgb_range (self->selected_output,
                                    (guint) g_ascii_strtoull (id, NULL, 10));

  g_signal_emit_by_name (G_OBJECT (self), "updated", self->selected_output);
}

static void
on_color_advanced_ok_cb (GtkButton         *button,
                         CcDisplaySettings *self)
{
  const gchar *id;

  /* (Closing the menu resets it to the monitor's format: read it first) */
  id = gtk_combo_box_get_active_id (GTK_COMBO_BOX (self->color_format_combo));
  if (self->selected_output && id != NULL)
    cc_display_monitor_set_color_format (self->selected_output,
                                         (guint) g_ascii_strtoull (id, NULL, 10));

  gtk_popover_popdown (GTK_POPOVER (self->color_advanced_popover));

  if (!self->selected_output || id == NULL)
    return;

  g_signal_emit_by_name (G_OBJECT (self), "updated", self->selected_output);
}

static void
on_color_advanced_cancel_cb (GtkButton         *button,
                             CcDisplaySettings *self)
{
  gtk_popover_popdown (GTK_POPOVER (self->color_advanced_popover));
}

/* Each time the menu closes, it goes back to the monitor's format */
static void
on_color_advanced_closed_cb (GtkPopover        *popover,
                             CcDisplaySettings *self)
{
  if (!self->selected_output)
    return;

  self->updating = TRUE;
  update_color (self);
  self->updating = FALSE;
}

static void
setup_color_advanced (CcDisplaySettings *self)
{
  GtkWidget *popover, *grid, *label, *help, *buttons, *cancel, *ok;

  grid = gtk_grid_new ();
  g_object_set (grid, "margin", 12, NULL);
  gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
  gtk_grid_set_column_spacing (GTK_GRID (grid), 12);

  label = gtk_label_new (_("Color format"));
  gtk_label_set_xalign (GTK_LABEL (label), 1.0);
  gtk_grid_attach (GTK_GRID (grid), label, 0, 0, 1, 1);

  self->color_format_combo = gtk_combo_box_text_new ();
  gtk_widget_set_halign (self->color_format_combo, GTK_ALIGN_START);
  gtk_grid_attach (GTK_GRID (grid), self->color_format_combo, 1, 0, 1, 1);

  help = gtk_label_new (_("How the picture is encoded on the cable. RGB suits a desktop best. YCbCr 4:2:2 and 4:2:0 need less bandwidth, for high resolutions and refresh rates, but blur colored text. Only the formats this display accepts in its current mode are listed."));
  gtk_label_set_line_wrap (GTK_LABEL (help), TRUE);
  gtk_label_set_max_width_chars (GTK_LABEL (help), 48);
  gtk_label_set_xalign (GTK_LABEL (help), 0.0);
  gtk_style_context_add_class (gtk_widget_get_style_context (help), "dim-label");
  gtk_grid_attach (GTK_GRID (grid), help, 1, 1, 1, 1);

  buttons = gtk_button_box_new (GTK_ORIENTATION_HORIZONTAL);
  gtk_button_box_set_layout (GTK_BUTTON_BOX (buttons), GTK_BUTTONBOX_END);
  gtk_box_set_spacing (GTK_BOX (buttons), 6);
  gtk_widget_set_margin_top (buttons, 6);
  cancel = gtk_button_new_with_mnemonic (_("_Cancel"));
  g_signal_connect (cancel, "clicked", G_CALLBACK (on_color_advanced_cancel_cb), self);
  gtk_container_add (GTK_CONTAINER (buttons), cancel);
  ok = gtk_button_new_with_mnemonic (_("_OK"));
  gtk_style_context_add_class (gtk_widget_get_style_context (ok), "suggested-action");
  g_signal_connect (ok, "clicked", G_CALLBACK (on_color_advanced_ok_cb), self);
  gtk_container_add (GTK_CONTAINER (buttons), ok);
  gtk_grid_attach (GTK_GRID (grid), buttons, 0, 2, 2, 1);

  gtk_widget_show_all (grid);

  popover = gtk_popover_new (self->color_advanced_button);
  gtk_container_add (GTK_CONTAINER (popover), grid);
  g_signal_connect (popover, "closed", G_CALLBACK (on_color_advanced_closed_cb), self);
  gtk_menu_button_set_popover (GTK_MENU_BUTTON (self->color_advanced_button), popover);
  self->color_advanced_popover = popover;
}

/* The advanced HDR settings: SDR content's brightness and the display's peak
 * brightness while it shows HDR. They matter only in HDR, so the button is
 * sensitive only when HDR isn't off. */
static void
update_hdr_advanced (CcDisplaySettings *self)
{
  CcDisplayMonitor *output = self->selected_output;
  guint sdr_white, edid_peak;
  gboolean has_peak;
  g_autofree gchar *peak_help = NULL;

  sdr_white = cc_display_monitor_get_sdr_white_level (output);
  has_peak = cc_display_monitor_supports_hdr_peak (output);

  gtk_widget_set_visible (self->hdr_advanced_button, sdr_white != 0 || has_peak);
  gtk_widget_set_sensitive (self->hdr_advanced_button,
                            cc_display_monitor_get_hdr (output) != CC_DISPLAY_HDR_OFF);

  gtk_widget_set_visible (self->sdr_white_label, sdr_white != 0);
  gtk_widget_set_visible (self->sdr_white_box, sdr_white != 0);
  gtk_widget_set_visible (self->sdr_white_help, sdr_white != 0);
  if (sdr_white != 0)
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (self->sdr_white_spin), sdr_white);

  gtk_widget_set_visible (self->hdr_peak_label, has_peak);
  gtk_widget_set_visible (self->hdr_peak_box, has_peak);
  gtk_widget_set_visible (self->hdr_peak_help, has_peak);
  if (has_peak)
    {
      gtk_spin_button_set_value (GTK_SPIN_BUTTON (self->hdr_peak_spin),
                                 cc_display_monitor_get_hdr_peak (output));

      edid_peak = cc_display_monitor_get_edid_hdr_peak (output);
      if (edid_peak != 0)
        peak_help = g_strdup_printf (_("The brightest the display gets in HDR. HDR games and videos are tone mapped to it, so their highlights keep their detail. 0 uses the %u cd/m² the display reports."),
                                     edid_peak);
      else
        peak_help = g_strdup (_("The brightest the display gets in HDR. HDR games and videos are tone mapped to it, so their highlights keep their detail. 0 uses what the display reports; this display reports none, so HDR content isn't tone mapped. Look up its peak brightness."));
      gtk_label_set_text (GTK_LABEL (self->hdr_peak_help), peak_help);
    }
}

/* The menu edits its own copy of the values: OK gives them to the monitor
 * (for Apply, as the other settings), Cancel or closing the menu drops them */
static void
on_hdr_advanced_ok_cb (GtkButton         *button,
                       CcDisplaySettings *self)
{
  if (!self->selected_output)
    return;

  gtk_spin_button_update (GTK_SPIN_BUTTON (self->sdr_white_spin));
  gtk_spin_button_update (GTK_SPIN_BUTTON (self->hdr_peak_spin));

  if (cc_display_monitor_get_sdr_white_level (self->selected_output) != 0)
    cc_display_monitor_set_sdr_white_level (self->selected_output,
                                            gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (self->sdr_white_spin)));
  if (cc_display_monitor_supports_hdr_peak (self->selected_output))
    cc_display_monitor_set_hdr_peak (self->selected_output,
                                     gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (self->hdr_peak_spin)));

  gtk_popover_popdown (GTK_POPOVER (self->hdr_advanced_popover));

  g_signal_emit_by_name (G_OBJECT (self), "updated", self->selected_output);
}

static void
on_hdr_advanced_cancel_cb (GtkButton         *button,
                           CcDisplaySettings *self)
{
  gtk_popover_popdown (GTK_POPOVER (self->hdr_advanced_popover));
}

/* Each time the menu closes, it goes back to the monitor's values */
static void
on_hdr_advanced_closed_cb (GtkPopover        *popover,
                           CcDisplaySettings *self)
{
  if (self->selected_output)
    update_hdr_advanced (self);
}

static GtkWidget *
hdr_advanced_value (CcDisplaySettings *self,
                    GtkWidget         *grid,
                    gint               row,
                    const gchar       *title,
                    gdouble            min,
                    gdouble            max,
                    GtkWidget        **label_out,
                    GtkWidget        **box_out,
                    GtkWidget        **help_out)
{
  GtkWidget *label, *box, *spin, *unit, *help;

  label = gtk_label_new (title);
  gtk_label_set_xalign (GTK_LABEL (label), 1.0);
  gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);

  box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  spin = gtk_spin_button_new_with_range (min, max, 10);
  gtk_spin_button_set_digits (GTK_SPIN_BUTTON (spin), 0);
  gtk_box_pack_start (GTK_BOX (box), spin, FALSE, FALSE, 0);
  unit = gtk_label_new (_("cd/m²"));
  gtk_box_pack_start (GTK_BOX (box), unit, FALSE, FALSE, 0);
  gtk_grid_attach (GTK_GRID (grid), box, 1, row, 1, 1);

  help = gtk_label_new (NULL);
  gtk_label_set_line_wrap (GTK_LABEL (help), TRUE);
  gtk_label_set_max_width_chars (GTK_LABEL (help), 48);
  gtk_label_set_xalign (GTK_LABEL (help), 0.0);
  gtk_style_context_add_class (gtk_widget_get_style_context (help), "dim-label");
  gtk_grid_attach (GTK_GRID (grid), help, 1, row + 1, 1, 1);

  gtk_widget_show_all (box);
  *label_out = label;
  *box_out = box;
  *help_out = help;

  return spin;
}

static void
setup_hdr_advanced (CcDisplaySettings *self)
{
  GtkWidget *popover, *grid, *buttons, *cancel, *ok;

  grid = gtk_grid_new ();
  g_object_set (grid, "margin", 12, NULL);
  gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
  gtk_grid_set_column_spacing (GTK_GRID (grid), 12);

  self->sdr_white_spin = hdr_advanced_value (self, grid, 0, _("SDR brightness"),
                                             80, 1000,
                                             &self->sdr_white_label,
                                             &self->sdr_white_box,
                                             &self->sdr_white_help);
  gtk_label_set_text (GTK_LABEL (self->sdr_white_help),
                      _("How bright the desktop and other SDR content are in HDR, as the brightness of white. 203 cd/m² is the reference for HDR video; raise it if the desktop looks dim in HDR."));

  self->hdr_peak_spin = hdr_advanced_value (self, grid, 2, _("Peak brightness"),
                                            0, 10000,
                                            &self->hdr_peak_label,
                                            &self->hdr_peak_box,
                                            &self->hdr_peak_help);

  buttons = gtk_button_box_new (GTK_ORIENTATION_HORIZONTAL);
  gtk_button_box_set_layout (GTK_BUTTON_BOX (buttons), GTK_BUTTONBOX_END);
  gtk_box_set_spacing (GTK_BOX (buttons), 6);
  gtk_widget_set_margin_top (buttons, 6);
  cancel = gtk_button_new_with_mnemonic (_("_Cancel"));
  g_signal_connect (cancel, "clicked", G_CALLBACK (on_hdr_advanced_cancel_cb), self);
  gtk_container_add (GTK_CONTAINER (buttons), cancel);
  ok = gtk_button_new_with_mnemonic (_("_OK"));
  gtk_style_context_add_class (gtk_widget_get_style_context (ok), "suggested-action");
  g_signal_connect (ok, "clicked", G_CALLBACK (on_hdr_advanced_ok_cb), self);
  gtk_container_add (GTK_CONTAINER (buttons), ok);
  gtk_grid_attach (GTK_GRID (grid), buttons, 0, 4, 2, 1);
  gtk_widget_show_all (buttons);

  popover = gtk_popover_new (self->hdr_advanced_button);
  gtk_container_add (GTK_CONTAINER (popover), grid);
  gtk_widget_show (grid);
  g_signal_connect (popover, "closed", G_CALLBACK (on_hdr_advanced_closed_cb), self);
  self->hdr_advanced_popover = popover;
  gtk_menu_button_set_popover (GTK_MENU_BUTTON (self->hdr_advanced_button), popover);
}

static void
on_hdr_combo_changed_cb (GtkComboBox       *combo,
                         CcDisplaySettings *self)
{
  const gchar *id;
  CcDisplayHdr hdr;

  if (self->updating || !self->selected_output)
    return;

  id = gtk_combo_box_get_active_id (combo);
  if (id == NULL)
    return;

  if (g_str_equal (id, "on"))
    hdr = CC_DISPLAY_HDR_ON;
  else if (g_str_equal (id, "auto"))
    hdr = CC_DISPLAY_HDR_AUTOMATIC;
  else
    hdr = CC_DISPLAY_HDR_OFF;

  cc_display_monitor_set_hdr (self->selected_output, hdr);
  gtk_widget_set_sensitive (self->hdr_advanced_button, hdr != CC_DISPLAY_HDR_OFF);

  g_signal_emit_by_name (G_OBJECT (self), "updated", self->selected_output);
}

static void
cc_display_settings_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  CcDisplaySettings *self = CC_DISPLAY_SETTINGS (object);

  switch (prop_id)
    {
    case PROP_HAS_ACCELEROMETER:
      g_value_set_boolean (value, cc_display_settings_get_has_accelerometer (self));
      break;

    case PROP_CONFIG:
      g_value_set_object (value, self->config);
      break;

    case PROP_SELECTED_OUTPUT:
      g_value_set_object (value, self->selected_output);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
cc_display_settings_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  CcDisplaySettings *self = CC_DISPLAY_SETTINGS (object);

  switch (prop_id)
    {
    case PROP_HAS_ACCELEROMETER:
      cc_display_settings_set_has_accelerometer (self, g_value_get_boolean (value));
      break;

    case PROP_CONFIG:
      cc_display_settings_set_config (self, g_value_get_object (value));
      break;

    case PROP_SELECTED_OUTPUT:
      cc_display_settings_set_selected_output (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
cc_display_settings_finalize (GObject *object)
{
  CcDisplaySettings *self = CC_DISPLAY_SETTINGS (object);

  g_clear_object (&self->config);

  g_clear_object (&self->orientation_list);
  g_clear_object (&self->refresh_rate_list);
  g_clear_object (&self->resolution_list);
  g_clear_object (&self->scale_list);
  g_clear_object (&self->builder);

  if (self->idle_udpate_id)
    g_source_remove (self->idle_udpate_id);
  self->idle_udpate_id = 0;

  G_OBJECT_CLASS (cc_display_settings_parent_class)->finalize (object);
}

static void
cc_display_settings_class_init (CcDisplaySettingsClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = cc_display_settings_finalize;
  gobject_class->get_property = cc_display_settings_get_property;
  gobject_class->set_property = cc_display_settings_set_property;

  props[PROP_HAS_ACCELEROMETER] =
    g_param_spec_boolean ("has-accelerometer", "Has Accelerometer",
                          "If an accelerometre is available for the builtin display",
                          FALSE,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  props[PROP_CONFIG] =
    g_param_spec_object ("config", "Display Config",
                         "The display configuration to work with",
                         CC_TYPE_DISPLAY_CONFIG,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  props[PROP_SELECTED_OUTPUT] =
    g_param_spec_object ("selected-output", "Selected Output",
                         "The output that is currently selected on the configuration",
                         CC_TYPE_DISPLAY_MONITOR,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class,
                                     PROP_LAST,
                                     props);

  g_signal_new ("updated",
                CC_TYPE_DISPLAY_SETTINGS,
                G_SIGNAL_RUN_LAST,
                0, NULL, NULL, NULL,
                G_TYPE_NONE, 1, CC_TYPE_DISPLAY_MONITOR);
}

static void
cc_display_settings_init (CcDisplaySettings *self)
{
  self->builder = gtk_builder_new_from_resource ("/org/cinnamon/control-center/display/cc-display-settings.ui");

  gtk_container_add (GTK_CONTAINER (self), WID ("display_settings_toplevel"));

  self->orientation_combo = WID ("orientation_combo");
  self->refresh_rate_combo = WID ("refresh_rate_combo");
  self->resolution_combo = WID ("resolution_combo");
  self->scale_combo = WID ("scale_combo");
  self->scale_row = WID ("scale_row");
  self->scale_label = WID ("scale_label");
  self->underscanning_row = WID ("underscanning_row");
  self->underscanning_switch = WID ("underscanning_switch");
  self->color_row = WID ("color_row");
  self->color_range_combo = WID ("color_range_combo");
  self->color_advanced_button = WID ("color_advanced_button");
  setup_color_advanced (self);
  self->hdr_row = WID ("hdr_row");
  self->hdr_combo = WID ("hdr_combo");
  self->hdr_advanced_button = WID ("hdr_advanced_button");
  setup_hdr_advanced (self);

  gtk_builder_add_callback_symbol (self->builder, "on_orientation_selection_changed_cb", G_CALLBACK (on_orientation_selection_changed_cb));
  gtk_builder_add_callback_symbol (self->builder, "on_refresh_rate_selection_changed_cb", G_CALLBACK (on_refresh_rate_selection_changed_cb));
  gtk_builder_add_callback_symbol (self->builder, "on_resolution_selection_changed_cb", G_CALLBACK (on_resolution_selection_changed_cb));
  gtk_builder_add_callback_symbol (self->builder, "on_scale_selection_changed_cb", G_CALLBACK (on_scale_selection_changed_cb));
  gtk_builder_add_callback_symbol (self->builder, "on_underscanning_switch_active_changed_cb", G_CALLBACK (on_underscanning_switch_active_changed_cb));
  gtk_builder_add_callback_symbol (self->builder, "on_hdr_combo_changed_cb", G_CALLBACK (on_hdr_combo_changed_cb));
  gtk_builder_add_callback_symbol (self->builder, "on_color_range_combo_changed_cb", G_CALLBACK (on_color_range_combo_changed_cb));

  GtkCellRenderer *renderer;

  self->updating = TRUE;

  self->orientation_list = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_INT);
  gtk_combo_box_set_model (GTK_COMBO_BOX (self->orientation_combo), GTK_TREE_MODEL (self->orientation_list));
  gtk_cell_layout_clear (GTK_CELL_LAYOUT (self->orientation_combo));
  renderer = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (self->orientation_combo),
                              renderer,
                              TRUE);
  gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (self->orientation_combo), renderer,
                                  "text", 0,
                                  NULL);
  gtk_cell_renderer_set_visible (renderer, TRUE);

  self->refresh_rate_list = gtk_list_store_new (2, G_TYPE_STRING, CC_TYPE_DISPLAY_MODE);
  gtk_combo_box_set_model (GTK_COMBO_BOX (self->refresh_rate_combo), GTK_TREE_MODEL (self->refresh_rate_list));
  gtk_cell_layout_clear (GTK_CELL_LAYOUT (self->refresh_rate_combo));
  renderer = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (self->refresh_rate_combo),
                              renderer,
                              TRUE);
  gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (self->refresh_rate_combo), renderer,
                                  "text", 0,
                                  NULL);
  gtk_cell_renderer_set_visible (renderer, TRUE);

  self->scale_list = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_DOUBLE);
  gtk_combo_box_set_model (GTK_COMBO_BOX (self->scale_combo), GTK_TREE_MODEL (self->scale_list));
  gtk_cell_layout_clear (GTK_CELL_LAYOUT (self->scale_combo));
  renderer = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (self->scale_combo),
                              renderer,
                              TRUE);
  gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (self->scale_combo), renderer,
                                  "text", 0,
                                  NULL);
  gtk_cell_renderer_set_visible (renderer, TRUE);

  self->resolution_list = gtk_list_store_new (2, G_TYPE_STRING, CC_TYPE_DISPLAY_MODE);

  gtk_combo_box_set_model (GTK_COMBO_BOX (self->resolution_combo), GTK_TREE_MODEL (self->resolution_list));
  gtk_cell_layout_clear (GTK_CELL_LAYOUT (self->resolution_combo));
  renderer = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (self->resolution_combo),
                              renderer,
                              TRUE);
  gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (self->resolution_combo), renderer,
                                  "text", 0,
                                  NULL);
  gtk_cell_renderer_set_visible (renderer, TRUE);

  gtk_builder_connect_signals (self->builder, self);

  self->updating = FALSE;
}

CcDisplaySettings*
cc_display_settings_new (void)
{
  return g_object_new (CC_TYPE_DISPLAY_SETTINGS,
                       NULL);
}

gboolean
cc_display_settings_get_has_accelerometer (CcDisplaySettings    *settings)
{
  return settings->has_accelerometer;
}

void
cc_display_settings_set_has_accelerometer (CcDisplaySettings    *self,
                                           gboolean              has_accelerometer)
{
  self->has_accelerometer = has_accelerometer;

  cc_display_settings_rebuild_ui (self);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_CONFIG]);
}

CcDisplayConfig*
cc_display_settings_get_config (CcDisplaySettings *self)
{
  return self->config;
}

void
cc_display_settings_set_config (CcDisplaySettings *self,
                                CcDisplayConfig   *config)
{
  const gchar *signals[] = { "rotation", "mode", "scale", "is-usable", "active" };
  GList *outputs, *l;
  guint i;

  if (self->config)
    {
      outputs = cc_display_config_get_monitors (self->config);
      for (l = outputs; l; l = l->next)
        {
          CcDisplayMonitor *output = l->data;

          g_signal_handlers_disconnect_by_data (output, self);
        }
    }
  g_clear_object (&self->config);

  self->config = g_object_ref (config);

  /* Listen to all the signals */
  if (self->config)
    {
      outputs = cc_display_config_get_monitors (self->config);
      for (l = outputs; l; l = l->next)
        {
          CcDisplayMonitor *output = l->data;

          for (i = 0; i < G_N_ELEMENTS (signals); ++i)
            g_signal_connect_object (output, signals[i], G_CALLBACK (on_output_changed_cb), self, G_CONNECT_SWAPPED);
        }
    }

  cc_display_settings_set_selected_output (self, NULL);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_CONFIG]);
}

CcDisplayMonitor*
cc_display_settings_get_selected_output (CcDisplaySettings *self)
{
  return self->selected_output;
}

void
cc_display_settings_set_selected_output (CcDisplaySettings *self,
                                         CcDisplayMonitor  *output)
{
  self->selected_output = output;

  cc_display_settings_rebuild_ui (self);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SELECTED_OUTPUT]);
}


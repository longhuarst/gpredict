/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/*
  Gpredict: Real-time satellite tracking and orbit prediction program

  Copyright (C)  2001-2007  Alexandru Csete, OZ9AEC.

  Authors: Alexandru Csete <oz9aec@gmail.com>

  Comments, questions and bugreports should be submitted via
  http://sourceforge.net/projects/gpredict/
  More details can be found at the project home page:

  http://gpredict.oz9aec.net/
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, visit http://www.fsf.org/
*/
/** \brief ROTOR control window.
 *  \ingroup widgets
 *
 * The master rotator control UI is implemented as a Gtk+ Widget in order
 * to allow multiple instances. The widget is created from the module
 * popup menu and each module can have several rotator control windows
 * attached to it. Note, however, that current implementation only
 * allows one rotor control window per module.
 * 
 */
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <math.h>
#include "compat.h"
#include "sat-log.h"
#include "predict-tools.h"
#include "gtk-polar-plot.h"
#include "gtk-rot-knob.h"
#include "gtk-rot-ctrl.h"
#ifdef HAVE_CONFIG_H
#  include <build-config.h>
#endif

/* NETWORK */
//#include <sys/types.h>
#include <sys/socket.h>     /* socket(), connect(), send() */
#include <netinet/in.h>     /* struct sockaddr_in */
#include <arpa/inet.h>      /* htons() */
#include <netdb.h>          /* gethostbyname() */
/* END */

#define FMTSTR "%7.2f\302\260"


static void gtk_rot_ctrl_class_init (GtkRotCtrlClass *class);
static void gtk_rot_ctrl_init       (GtkRotCtrl      *list);
static void gtk_rot_ctrl_destroy    (GtkObject       *object);


static GtkWidget *create_az_widgets (GtkRotCtrl *ctrl);
static GtkWidget *create_el_widgets (GtkRotCtrl *ctrl);
static GtkWidget *create_target_widgets (GtkRotCtrl *ctrl);
static GtkWidget *create_conf_widgets (GtkRotCtrl *ctrl);
static GtkWidget *create_plot_widget (GtkRotCtrl *ctrl);

static void store_sats (gpointer key, gpointer value, gpointer user_data);

static void sat_selected_cb (GtkComboBox *satsel, gpointer data);
static void track_toggle_cb (GtkToggleButton *button, gpointer data);
static void delay_changed_cb (GtkSpinButton *spin, gpointer data);
static void toler_changed_cb (GtkSpinButton *spin, gpointer data);
static void rot_selected_cb (GtkComboBox *box, gpointer data);
static void rot_locked_cb (GtkToggleButton *button, gpointer data);
static gboolean rot_ctrl_timeout_cb (gpointer data);
static void update_count_down (GtkRotCtrl *ctrl, gdouble t);

static void get_pos (GtkRotCtrl *ctrl, gdouble *az, gdouble *el);
static void set_pos (GtkRotCtrl *ctrl, gdouble az, gdouble el);

static GtkVBoxClass *parent_class = NULL;

static GdkColor ColBlack = { 0, 0, 0, 0};
static GdkColor ColWhite = { 0, 0xFFFF, 0xFFFF, 0xFFFF};
static GdkColor ColRed =   { 0, 0xFFFF, 0, 0};
static GdkColor ColGreen = {0, 0, 0xFFFF, 0};


GType
gtk_rot_ctrl_get_type ()
{
	static GType gtk_rot_ctrl_type = 0;

	if (!gtk_rot_ctrl_type) {

		static const GTypeInfo gtk_rot_ctrl_info = {
			sizeof (GtkRotCtrlClass),
			NULL,  /* base_init */
			NULL,  /* base_finalize */
			(GClassInitFunc) gtk_rot_ctrl_class_init,
			NULL,  /* class_finalize */
			NULL,  /* class_data */
			sizeof (GtkRotCtrl),
			5,     /* n_preallocs */
			(GInstanceInitFunc) gtk_rot_ctrl_init,
		};

		gtk_rot_ctrl_type = g_type_register_static (GTK_TYPE_VBOX,
												    "GtkRotCtrl",
													&gtk_rot_ctrl_info,
													0);
	}

	return gtk_rot_ctrl_type;
}


static void
gtk_rot_ctrl_class_init (GtkRotCtrlClass *class)
{
	GObjectClass      *gobject_class;
	GtkObjectClass    *object_class;
	GtkWidgetClass    *widget_class;
	GtkContainerClass *container_class;

	gobject_class   = G_OBJECT_CLASS (class);
	object_class    = (GtkObjectClass*) class;
	widget_class    = (GtkWidgetClass*) class;
	container_class = (GtkContainerClass*) class;

	parent_class = g_type_class_peek_parent (class);

	object_class->destroy = gtk_rot_ctrl_destroy;
 
}



static void
gtk_rot_ctrl_init (GtkRotCtrl *ctrl)
{
    ctrl->sats = NULL;
    ctrl->target = NULL;
    ctrl->pass = NULL;
    ctrl->qth = NULL;
    ctrl->plot = NULL;
    
    ctrl->tracking = FALSE;
    ctrl->busy = FALSE;
    ctrl->engaged = FALSE;
    ctrl->delay = 1000;
    ctrl->timerid = 0;
    ctrl->tolerance = 1.0;
}

static void
gtk_rot_ctrl_destroy (GtkObject *object)
{
    GtkRotCtrl *ctrl = GTK_ROT_CTRL (object);
    
    
    /* stop timer */
    if (ctrl->timerid > 0) 
        g_source_remove (ctrl->timerid);

    /* free configuration */
    if (ctrl->conf != NULL) {
        g_free (ctrl->conf->name);
        g_free (ctrl->conf->host);
        g_free (ctrl->conf);
        ctrl->conf = NULL;
    }

	(* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}



/** \brief Create a new rotor control widget.
 * \return A new rotor control window.
 * 
 */
GtkWidget *
gtk_rot_ctrl_new (GtkSatModule *module)
{
    GtkWidget *widget;
    GtkWidget *table;

	widget = g_object_new (GTK_TYPE_ROT_CTRL, NULL);
    
    /* store satellites */
    g_hash_table_foreach (module->satellites, store_sats, widget);
    
    GTK_ROT_CTRL (widget)->target = SAT (g_slist_nth_data (GTK_ROT_CTRL (widget)->sats, 0));
    
    /* store current time (don't know if real or simulated) */
    GTK_ROT_CTRL (widget)->t = module->tmgCdnum;
    
    /* store QTH */
    GTK_ROT_CTRL (widget)->qth = module->qth;
    
    /* get next pass for target satellite */
    if (GTK_ROT_CTRL (widget)->target->el > 0.0) {
        GTK_ROT_CTRL (widget)->pass = get_current_pass (GTK_ROT_CTRL (widget)->target,
                                                        GTK_ROT_CTRL (widget)->qth,
                                                        0.0);
    }
    else {
        GTK_ROT_CTRL (widget)->pass = get_next_pass (GTK_ROT_CTRL (widget)->target,
                                                     GTK_ROT_CTRL (widget)->qth,
                                                     3.0);
    }

        
        /* initialise custom colors */
    gdk_rgb_find_color (gtk_widget_get_colormap (widget), &ColBlack);
    gdk_rgb_find_color (gtk_widget_get_colormap (widget), &ColWhite);
    gdk_rgb_find_color (gtk_widget_get_colormap (widget), &ColRed);
    gdk_rgb_find_color (gtk_widget_get_colormap (widget), &ColGreen);

    /* create contents */
    table = gtk_table_new (2, 3, FALSE);
    gtk_table_set_row_spacings (GTK_TABLE (table), 0);
    gtk_table_set_col_spacings (GTK_TABLE (table), 0);
    gtk_container_set_border_width (GTK_CONTAINER (table), 10);
    gtk_table_attach (GTK_TABLE (table), create_az_widgets (GTK_ROT_CTRL (widget)),
                      0, 1, 0, 1, GTK_FILL, GTK_SHRINK, 0, 0);
    gtk_table_attach (GTK_TABLE (table), create_el_widgets (GTK_ROT_CTRL (widget)),
                      1, 2, 0, 1, GTK_FILL, GTK_SHRINK, 0, 0);
    gtk_table_attach (GTK_TABLE (table), create_target_widgets (GTK_ROT_CTRL (widget)),
                      0, 1, 1, 2, GTK_FILL, GTK_SHRINK, 0, 0);
    gtk_table_attach (GTK_TABLE (table), create_conf_widgets (GTK_ROT_CTRL (widget)),
                      1, 2, 1, 2, GTK_FILL, GTK_SHRINK, 0, 0);
    gtk_table_attach (GTK_TABLE (table), create_plot_widget (GTK_ROT_CTRL (widget)),
                      2, 3, 0, 2, GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND, 0, 0);

    gtk_container_add (GTK_CONTAINER (widget), table);
    
    GTK_ROT_CTRL (widget)->timerid = g_timeout_add (GTK_ROT_CTRL (widget)->delay,
                                                    rot_ctrl_timeout_cb,
                                                    GTK_ROT_CTRL (widget));
    
	return widget;
}


/** \brief Update rotator control state.
 * \param ctrl Pointer to the GtkRotCtrl.
 * 
 * This function is called by the parent, i.e. GtkSatModule, indicating that
 * the satellite data has been updated. The function updates the internal state
 * of the controller and the rotator.
 */
void
gtk_rot_ctrl_update   (GtkRotCtrl *ctrl, gdouble t)
{
    gchar *buff;
    
    ctrl->t = t;
    
    if (ctrl->target) {
        /* update target displays */
        buff = g_strdup_printf (FMTSTR, ctrl->target->az);
        gtk_label_set_text (GTK_LABEL (ctrl->AzSat), buff);
        g_free (buff);
        buff = g_strdup_printf (FMTSTR, ctrl->target->el);
        gtk_label_set_text (GTK_LABEL (ctrl->ElSat), buff);
        g_free (buff);
        
        update_count_down (ctrl, t);
        
        /* update next pass if necessary */
        if (ctrl->pass != NULL) {
            if ((ctrl->target->aos > ctrl->pass->aos) && (ctrl->target->el <= 0.0)) {
                /* we need to update the pass */
                free_pass (ctrl->pass);
                ctrl->pass = get_pass (ctrl->target, ctrl->qth, t, 3.0);
                
                /* update polar plot */
                gtk_polar_plot_set_pass (GTK_POLAR_PLOT (ctrl->plot), ctrl->pass);
            }
        }
        else {
            /* we don't have any current pass; store the current one */
            if (ctrl->target->el > 0.0) {
                ctrl->pass = get_current_pass (ctrl->target, ctrl->qth, t);
            }
            else {
                ctrl->pass = get_pass (ctrl->target, ctrl->qth, t, 3.0);
            }
            
            /* update polar plot */
            gtk_polar_plot_set_pass (GTK_POLAR_PLOT (ctrl->plot), ctrl->pass);
        }
    }
}


/** \brief Create azimuth control widgets.
 * \param ctrl Pointer to the GtkRotCtrl widget.
 * 
 * This function creates and initialises the widgets for controlling the
 * azimuth of the the rotator.
 */
static
GtkWidget *create_az_widgets (GtkRotCtrl *ctrl)
{
    GtkWidget   *frame;
    GtkWidget   *table;
    GtkWidget   *label;
    
    
    frame = gtk_frame_new (_("Azimuth"));
    
    table = gtk_table_new (2, 2, FALSE);
    gtk_container_set_border_width (GTK_CONTAINER (table), 5);
    gtk_table_set_col_spacings (GTK_TABLE (table), 5);
    gtk_table_set_row_spacings (GTK_TABLE (table), 5);
    gtk_container_add (GTK_CONTAINER (frame), table);
    
    ctrl->AzSet = gtk_rot_knob_new (0.0, 360.0, 180.0);
    gtk_table_attach_defaults (GTK_TABLE (table), ctrl->AzSet, 0, 2, 0, 1);
                       
    label = gtk_label_new (NULL);
    gtk_label_set_markup (GTK_LABEL (label), _("Read:"));
    gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
    gtk_table_attach (GTK_TABLE (table), label, 0, 1, 1, 2,
                     GTK_SHRINK, GTK_SHRINK, 10, 0);
    
    ctrl->AzRead = gtk_label_new (" --- ");
    gtk_misc_set_alignment (GTK_MISC (ctrl->AzRead), 0.0, 0.5);
    gtk_table_attach_defaults (GTK_TABLE (table), ctrl->AzRead, 1, 2, 1, 2);
    
    return frame;
}


/** \brief Create elevation control widgets.
 * \param ctrl Pointer to the GtkRotCtrl widget.
 * 
 * This function creates and initialises the widgets for controlling the
 * elevation of the the rotator.
 */
static
GtkWidget *create_el_widgets (GtkRotCtrl *ctrl)
{
    GtkWidget   *frame;
    GtkWidget   *table;
    GtkWidget   *label;

    
    frame = gtk_frame_new (_("Elevation"));

    table = gtk_table_new (2, 2, FALSE);
    gtk_container_set_border_width (GTK_CONTAINER (table), 5);
    gtk_table_set_col_spacings (GTK_TABLE (table), 5);
    gtk_table_set_row_spacings (GTK_TABLE (table), 5);
    gtk_container_add (GTK_CONTAINER (frame), table);
    
    ctrl->ElSet = gtk_rot_knob_new (0.0, 90.0, 45.0);
    gtk_table_attach_defaults (GTK_TABLE (table), ctrl->ElSet, 0, 2, 0, 1);
                       
    label = gtk_label_new (NULL);
    gtk_label_set_markup (GTK_LABEL (label), _("Read: "));
    gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
    gtk_table_attach (GTK_TABLE (table), label, 0, 1, 1, 2,
                     GTK_SHRINK, GTK_SHRINK, 10, 0);
    
    ctrl->ElRead = gtk_label_new (" --- ");
    gtk_misc_set_alignment (GTK_MISC (ctrl->ElRead), 0.0, 0.5);
    gtk_table_attach_defaults (GTK_TABLE (table), ctrl->ElRead, 1, 2, 1, 2);

    return frame;
}

/** \brief Create target widgets.
 * \param ctrl Pointer to the GtkRotCtrl widget.
 */
static
GtkWidget *create_target_widgets (GtkRotCtrl *ctrl)
{
    GtkWidget *frame,*table,*label,*satsel,*track;
    gchar *buff;
    guint i, n;
    sat_t *sat = NULL;
    

    buff = g_strdup_printf (FMTSTR, 0.0);
    
    table = gtk_table_new (4, 3, FALSE);
    gtk_container_set_border_width (GTK_CONTAINER (table), 5);
    gtk_table_set_col_spacings (GTK_TABLE (table), 5);
    gtk_table_set_row_spacings (GTK_TABLE (table), 5);

    /* sat selector */
    satsel = gtk_combo_box_new_text ();
    n = g_slist_length (ctrl->sats);
    for (i = 0; i < n; i++) {
        sat = SAT (g_slist_nth_data (ctrl->sats, i));
        if (sat) {
            gtk_combo_box_append_text (GTK_COMBO_BOX (satsel), sat->tle.sat_name);
        }
    }
    gtk_combo_box_set_active (GTK_COMBO_BOX (satsel), 0);
    gtk_widget_set_tooltip_text (satsel, _("Select target object"));
    g_signal_connect (satsel, "changed", G_CALLBACK (sat_selected_cb), ctrl);
    gtk_table_attach_defaults (GTK_TABLE (table), satsel, 0, 2, 0, 1);
    
    /* tracking button */
    track = gtk_toggle_button_new_with_label (_("Track"));
    gtk_widget_set_tooltip_text (track, _("Track the satellite when it is within range"));
    gtk_table_attach_defaults (GTK_TABLE (table), track, 2, 3, 0, 1);
    g_signal_connect (track, "toggled", G_CALLBACK (track_toggle_cb), ctrl);
    
    /* Azimuth */
    label = gtk_label_new (_("Az:"));
    gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
    gtk_table_attach_defaults (GTK_TABLE (table), label, 0, 1, 1, 2);
    
    ctrl->AzSat = gtk_label_new (buff);
    gtk_misc_set_alignment (GTK_MISC (ctrl->AzSat), 1.0, 0.5);
    gtk_table_attach_defaults (GTK_TABLE (table), ctrl->AzSat, 1, 2, 1, 2);
    
    
    /* Elevation */
    label = gtk_label_new (_("El:"));
    gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
    gtk_table_attach_defaults (GTK_TABLE (table), label, 0, 1, 2, 3);
    
    ctrl->ElSat = gtk_label_new (buff);
    gtk_misc_set_alignment (GTK_MISC (ctrl->ElSat), 1.0, 0.5);
    gtk_table_attach_defaults (GTK_TABLE (table), ctrl->ElSat, 1, 2, 2, 3);
    
    /* count down */
    label = gtk_label_new (_("\316\224T:"));
    gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
    gtk_table_attach_defaults (GTK_TABLE (table), label, 0, 1, 3, 4);
    ctrl->SatCnt = gtk_label_new ("00:00:00");
    gtk_misc_set_alignment (GTK_MISC (ctrl->SatCnt), 1.0, 0.5);
    gtk_table_attach_defaults (GTK_TABLE (table), ctrl->SatCnt, 1, 2, 3, 4);
    
    frame = gtk_frame_new (_("Target"));
    gtk_container_add (GTK_CONTAINER (frame), table);
    
    g_free (buff);
    
    return frame;
}


static GtkWidget *
create_conf_widgets (GtkRotCtrl *ctrl)
{
    GtkWidget *frame,*table,*label,*timer,*toler;
    GtkWidget   *lock;
    GDir        *dir = NULL;   /* directory handle */
    GError      *error = NULL; /* error flag and info */
    gchar       *cfgdir;
    gchar       *dirname;      /* directory name */
    gchar      **vbuff;
    const gchar *filename;     /* file name */

    
    
    table = gtk_table_new (3, 3, FALSE);
    gtk_container_set_border_width (GTK_CONTAINER (table), 5);
    gtk_table_set_col_spacings (GTK_TABLE (table), 5);
    gtk_table_set_row_spacings (GTK_TABLE (table), 5);
    
    
    label = gtk_label_new (_("Device:"));
    gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
    gtk_table_attach_defaults (GTK_TABLE (table), label, 0, 1, 0, 1);
    
    ctrl->DevSel = gtk_combo_box_new_text ();
    gtk_widget_set_tooltip_text (ctrl->DevSel, _("Select antenna rotator device"));
    
    /* open configuration directory */
    cfgdir = get_conf_dir ();
    dirname = g_strconcat (cfgdir, G_DIR_SEPARATOR_S,
                           "hwconf", NULL);
    g_free (cfgdir);
    
    dir = g_dir_open (dirname, 0, &error);
    if (dir) {
        /* read each .rig file */
        while ((filename = g_dir_read_name (dir))) {
            
            if (g_strrstr (filename, ".rot")) {
                
                vbuff = g_strsplit (filename, ".rot", 0);
                gtk_combo_box_append_text (GTK_COMBO_BOX (ctrl->DevSel), vbuff[0]);
                g_strfreev (vbuff);
            }
        }
    }
    else {
        sat_log_log (SAT_LOG_LEVEL_ERROR,
                     _("%s:%d: Failed to open hwconf dir (%s)"),
                       __FILE__, __LINE__, error->message);
        g_clear_error (&error);
    }

    g_free (dirname);
    g_dir_close (dir);

    gtk_combo_box_set_active (GTK_COMBO_BOX (ctrl->DevSel), 0);
    g_signal_connect (ctrl->DevSel, "changed", G_CALLBACK (rot_selected_cb), ctrl);
    gtk_table_attach_defaults (GTK_TABLE (table), ctrl->DevSel, 1, 2, 0, 1);

    /* Engage button */
    lock = gtk_toggle_button_new_with_label (_("Engage"));
    gtk_widget_set_tooltip_text (lock, _("Engage the selcted rotor device"));
    g_signal_connect (lock, "toggled", G_CALLBACK (rot_locked_cb), ctrl);
    gtk_table_attach_defaults (GTK_TABLE (table), lock, 2, 3, 0, 1);
    
    /* Timeout */
    label = gtk_label_new (_("Cycle:"));
    gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
    gtk_table_attach_defaults (GTK_TABLE (table), label, 0, 1, 1, 2);
    
    timer = gtk_spin_button_new_with_range (100, 5000, 10);
    gtk_spin_button_set_digits (GTK_SPIN_BUTTON (timer), 0);
    gtk_widget_set_tooltip_text (timer,
                                 _("This parameter controls the delay between "\
                                   "commands sent to the rotator."));
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (timer), ctrl->delay);
    g_signal_connect (timer, "value-changed", G_CALLBACK (delay_changed_cb), ctrl);
    gtk_table_attach (GTK_TABLE (table), timer, 1, 2, 1, 2,
                      GTK_FILL, GTK_FILL, 0, 0);
    
    label = gtk_label_new (_("msec"));
    gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
    gtk_table_attach_defaults (GTK_TABLE (table), label, 2, 3, 1, 2);

    /* Tolerance */
    label = gtk_label_new (_("Tolerance:"));
    gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
    gtk_table_attach_defaults (GTK_TABLE (table), label, 0, 1, 2, 3);
    
    toler = gtk_spin_button_new_with_range (0.0, 10.0, 0.1);
    gtk_spin_button_set_digits (GTK_SPIN_BUTTON (toler), 1);
    gtk_widget_set_tooltip_text (toler,
                                 _("This parameter controls the tolerance between "\
                                   "the target and rotator values for the rotator.\n"\
                                   "If the difference between the target and rotator values "\
                                   "is smaller than the tolerance, no new commands are sent"));
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (toler), ctrl->tolerance);
    g_signal_connect (toler, "value-changed", G_CALLBACK (toler_changed_cb), ctrl);
    gtk_table_attach (GTK_TABLE (table), toler, 1, 2, 2, 3,
                      GTK_FILL, GTK_FILL, 0, 0);
    
    
    label = gtk_label_new (_("deg"));
    gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
    gtk_table_attach_defaults (GTK_TABLE (table), label, 2, 3, 2, 3);
    
    /* load initial rotator configuration */
    rot_selected_cb (GTK_COMBO_BOX (ctrl->DevSel), ctrl);
    
    frame = gtk_frame_new (_("Settings"));
    gtk_container_add (GTK_CONTAINER (frame), table);
    
    return frame;
}


/** \brief Create target widgets.
 * \param ctrl Pointer to the GtkRotCtrl widget.
 */
static
GtkWidget *create_plot_widget (GtkRotCtrl *ctrl)
{
    GtkWidget *frame;
    
    ctrl->plot = gtk_polar_plot_new (ctrl->qth, ctrl->pass);
    
    frame = gtk_frame_new (NULL);
    gtk_container_add (GTK_CONTAINER (frame), ctrl->plot);
    
    return frame;
}


/** \brief Copy satellite from hash table to singly linked list.
 */
static void
store_sats (gpointer key, gpointer value, gpointer user_data)
{
    GtkRotCtrl *ctrl = GTK_ROT_CTRL( user_data);
    sat_t        *sat = SAT (value);

    ctrl->sats = g_slist_append (ctrl->sats, sat);
}


/** \brief Manage satellite selections
 * \param satsel Pointer to the GtkComboBox.
 * \param data Pointer to the GtkRotCtrl widget.
 * 
 * This function is called when the user selects a new satellite.
 */
static void
sat_selected_cb (GtkComboBox *satsel, gpointer data)
{
    GtkRotCtrl *ctrl = GTK_ROT_CTRL (data);
    gint i;
    
    i = gtk_combo_box_get_active (satsel);
    if (i >= 0) {
        ctrl->target = SAT (g_slist_nth_data (ctrl->sats, i));
        
        /* update next pass */
        if (ctrl->pass != NULL)
            free_pass (ctrl->pass);
        
        if (ctrl->target->el > 0.0)
            ctrl->pass = get_current_pass (ctrl->target, ctrl->qth, ctrl->t);
        else
            ctrl->pass = get_pass (ctrl->target, ctrl->qth, ctrl->t, 3.0);
    }
    else {
        sat_log_log (SAT_LOG_LEVEL_ERROR,
                     _("%s:%s: Invalid satellite selection: %d"),
                     __FILE__, __FUNCTION__, i);
        
        /* clear pass just in case... */
        if (ctrl->pass != NULL) {
            free_pass (ctrl->pass);
            ctrl->pass = NULL;
        }
        
    }
    
    /* in either case, we set the new pass (even if NULL) on the polar plot */
    if (ctrl->plot != NULL)
        gtk_polar_plot_set_pass (GTK_POLAR_PLOT (ctrl->plot), ctrl->pass);
}


/** \brief Manage toggle signals (tracking)
 * \param button Pointer to the GtkToggle button.
 * \param data Pointer to the GtkRotCtrl widget.
 */
static void
track_toggle_cb (GtkToggleButton *button, gpointer data)
{
    GtkRotCtrl *ctrl = GTK_ROT_CTRL (data);
    
    ctrl->tracking = gtk_toggle_button_get_active (button);
}


/** \brief Manage cycle delay changes.
 * \param spin Pointer to the spin button.
 * \param data Pointer to the GtkRotCtrl widget.
 * 
 * This function is called when the user changes the value of the
 * cycle delay.
 */
static void
delay_changed_cb (GtkSpinButton *spin, gpointer data)
{
    GtkRotCtrl *ctrl = GTK_ROT_CTRL (data);
    
    
    ctrl->delay = (guint) gtk_spin_button_get_value (spin);

    if (ctrl->timerid > 0) 
        g_source_remove (ctrl->timerid);

    ctrl->timerid = g_timeout_add (ctrl->delay, rot_ctrl_timeout_cb, ctrl);
}



/** \brief Manage tolerance changes.
 * \param spin Pointer to the spin button.
 * \param data Pointer to the GtkRotCtrl widget.
 * 
 * This function is called when the user changes the value of the
 * tolerance.
 */
static void
toler_changed_cb (GtkSpinButton *spin, gpointer data)
{
    GtkRotCtrl *ctrl = GTK_ROT_CTRL (data);
    
    ctrl->tolerance = gtk_spin_button_get_value (spin);
}


/** \brief New rotor device selected.
 * \param box Pointer to the rotor selector combo box.
 * \param data Pointer to the GtkRotCtrl widget.
 * 
 * This function is called when the user selects a new rotor controller
 * device.
 */
static void
rot_selected_cb (GtkComboBox *box, gpointer data)
{
    GtkRotCtrl *ctrl = GTK_ROT_CTRL (data);
    
    /* free previous configuration */
    if (ctrl->conf != NULL) {
        g_free (ctrl->conf->name);
        g_free (ctrl->conf->host);
        g_free (ctrl->conf);
    }
    
    ctrl->conf = g_try_new (rotor_conf_t, 1);
    if (ctrl->conf == NULL) {
        sat_log_log (SAT_LOG_LEVEL_ERROR,
                     _("%s:%d: Failed to allocate memory for rotator config"),
                       __FILE__, __LINE__);
        return;
    }
    
    /* load new configuration */
    ctrl->conf->name = gtk_combo_box_get_active_text (box);
    if (rotor_conf_read (ctrl->conf)) {
        sat_log_log (SAT_LOG_LEVEL_MSG,
                     _("Loaded new rotator configuration %s"),
                       ctrl->conf->name);
    }
    else {
        sat_log_log (SAT_LOG_LEVEL_ERROR,
                     _("%s:%d: Failed to load rotator configuration %s"),
                       __FILE__, __LINE__, ctrl->conf->name);

        g_free (ctrl->conf->name);
        if (ctrl->conf->host)
            g_free (ctrl->conf->host);
        g_free (ctrl->conf);
        ctrl->conf = NULL;
    }
}


/** \brief Rotor locked.
 * \param button Pointer to the "Engage" button.
 * \param data Pointer to the GtkRotCtrl widget.
 * 
 * This function is called when the user toggles the "Engage" button.
 */
static void
rot_locked_cb (GtkToggleButton *button, gpointer data)
{
    GtkRotCtrl *ctrl = GTK_ROT_CTRL (data);
    
    if (gtk_toggle_button_get_active (button)) {
        gtk_widget_set_sensitive (ctrl->DevSel, FALSE);
        ctrl->engaged = TRUE;
    }
    else {
        if (ctrl->conf == NULL) {
            /* we don't have a working configuration */
            sat_log_log (SAT_LOG_LEVEL_ERROR,
                         _("%s: Controller does not have a valid configuration"),
                         __FUNCTION__);
            return;
        }
        gtk_widget_set_sensitive (ctrl->DevSel, TRUE);
        ctrl->engaged = FALSE;
        
        gtk_label_set_text (GTK_LABEL (ctrl->AzRead), "---");
        gtk_label_set_text (GTK_LABEL (ctrl->ElRead), "---");
    }
}


/** \brief Rotator controller timeout function
 * \param data Pointer to the GtkRotCtrl widget.
 * \return Always TRUE to let the timer continue.
 */
static gboolean
rot_ctrl_timeout_cb (gpointer data)
{
    GtkRotCtrl *ctrl = GTK_ROT_CTRL (data);
    gdouble rotaz=0.0, rotel=0.0;
    gchar *text;
    
    
    if (ctrl->busy) {
        sat_log_log (SAT_LOG_LEVEL_ERROR,_("%s missed the deadline"),__FUNCTION__);
        return TRUE;
    }
    
    ctrl->busy = TRUE;
    
    /* If we are tracking and the target satellite is within
       range, set the rotor position controller knob values to
       the target values. If the target satellite is out of range
       set the rotor controller to 0 deg El and to the Az where the
       target sat is expected to come up
    */
    if (ctrl->tracking) {
        if (ctrl->target->el < 0.0) {
            gdouble aosaz = 0.0;
            
            if (ctrl->pass != NULL) {
                aosaz = ctrl->pass->aos_az;
            }
            gtk_rot_knob_set_value (GTK_ROT_KNOB (ctrl->AzSet), aosaz);
            gtk_rot_knob_set_value (GTK_ROT_KNOB (ctrl->ElSet), 0.0);
        }
        else {
            gtk_rot_knob_set_value (GTK_ROT_KNOB (ctrl->AzSet), ctrl->target->az);
            gtk_rot_knob_set_value (GTK_ROT_KNOB (ctrl->ElSet), ctrl->target->el);
        }
        
        /* TODO: Update controller thread on polar plot */
    }

    if ((ctrl->engaged) && (ctrl->conf != NULL)) {
        
        /* read back current value from device */
        get_pos (ctrl, &rotaz, &rotel);
        
        /* update display widgets */
        text = g_strdup_printf ("%.2f\302\260", rotaz);
        gtk_label_set_text (GTK_LABEL (ctrl->AzRead), text);
        g_free (text);
        text = g_strdup_printf ("%.2f\302\260", rotel);
        gtk_label_set_text (GTK_LABEL (ctrl->ElRead), text);
        g_free (text);
        
        /* if tolerance exceeded */
        /* TODO: send controller values to rotator device */
        /* TODO: update polar plot */
    }
    
    
    ctrl->busy = FALSE;
    
    return TRUE;
}


/** \brief Read rotator position from device.
 * \param ctrl Pointer to the GtkRotCtrl widget.
 * \param az The current Az as read from the device
 * \param el The current El as read from the device
 */
static void get_pos (GtkRotCtrl *ctrl, gdouble *az, gdouble *el)
{
    gchar  *buff,**vbuff;
    gint    written,size;
    gint    status;
    struct hostent *h;
    struct sockaddr_in ServAddr;
    gint  sock;          /*!< Network socket */

                         
    if ((az == NULL) || (el == NULL)) {
        sat_log_log (SAT_LOG_LEVEL_BUG,
                     _("%s:%d: NULL storage."),
                     __FILE__, __LINE__);
        return;
    }
    
    /* create socket */
    sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        sat_log_log (SAT_LOG_LEVEL_ERROR,
                     _("%s:%d: Failed to create socket"),
                       __FILE__, __LINE__);
        return;
    }
    else {
        sat_log_log (SAT_LOG_LEVEL_DEBUG,
                     _("%s:%d Network socket created successfully"),
                       __FILE__, __LINE__);
    }
        
    memset(&ServAddr, 0, sizeof(ServAddr));     /* Zero out structure */
    ServAddr.sin_family = AF_INET;             /* Internet address family */
    h = gethostbyname(ctrl->conf->host);
    memcpy((char *) &ServAddr.sin_addr.s_addr, h->h_addr_list[0], h->h_length);
    ServAddr.sin_port = htons(ctrl->conf->port); /* Server port */

    /* establish connection */
    status = connect(sock, (struct sockaddr *) &ServAddr, sizeof(ServAddr));
    if (status < 0) {
        sat_log_log (SAT_LOG_LEVEL_ERROR,
                     _("%s:%d: Failed to connect to %s:%d"),
                       __FILE__, __LINE__, ctrl->conf->host, ctrl->conf->port);
        return;
    }
    else {
        sat_log_log (SAT_LOG_LEVEL_DEBUG,
                     _("%s:%d: Connection opened to %s:%d"),
                       __FILE__, __LINE__, ctrl->conf->host, ctrl->conf->port);
    }
    
    /* send command */
    buff = g_strdup_printf ("p\n");
    
    /* number of bytes to write depends on platform (EOL) */
#ifdef G_OS_WIN32
    size = 3;
#else
    size = 2;
#endif
    written = send(sock, buff, size, 0);
    if (written != size) {
        sat_log_log (SAT_LOG_LEVEL_ERROR,
                     _("%s:%d: SIZE ERROR %d / %d"),
                       __FILE__, __LINE__, written, size);
    }
    g_free (buff);
    
    
    /* try to read answer */
    buff = g_try_malloc (128);
    if (buff == NULL) {
        sat_log_log (SAT_LOG_LEVEL_ERROR,
                     _("%s:%s: Failed to allocate 128 bytes (yes, this means trouble)"),
                       __FILE__, __FUNCTION__);
        shutdown (sock, SHUT_RDWR);
        close (sock);
        return;
    }
        
    size = read (sock, buff, 127);
    if (size == 0) {
        sat_log_log (SAT_LOG_LEVEL_ERROR,
                     _("%s:%s: Got 0 bytes from rotctld"),
                       __FILE__, __FUNCTION__);
    }
    else {
        sat_log_log (SAT_LOG_LEVEL_DEBUG,
                     _("%s:%s: Read %d bytes from rotctld"),
                      __FILE__, __FUNCTION__, size);
        
        buff[size] = 0;
        vbuff = g_strsplit (buff, "\n", 3);
        *az = g_strtod (vbuff[0], NULL);
        *el = g_strtod (vbuff[1], NULL);
                
        g_free (buff);
        g_strfreev (vbuff);
    }
    
    shutdown (sock, SHUT_RDWR);
    close (sock);

    
}


static void set_pos (GtkRotCtrl *ctrl, gdouble az, gdouble el)
{
    
}




/** \brief Update count down label.
 * \param[in] ctrl Pointer to the RotCtrl widget.
 * \param[in] t The current time.
 * 
 * This function calculates the new time to AOS/LOS of the currently
 * selected target and updates the ctrl->SatCnt label widget.
 */
static void update_count_down (GtkRotCtrl *ctrl, gdouble t)
{
    gdouble  targettime;
    gdouble  delta;
    gchar   *buff;
    guint    h,m,s;
    gchar   *ch,*cm,*cs;

    
    /* select AOS or LOS time depending on target elevation */
    if (ctrl->target->el < 0.0)
        targettime = ctrl->target->aos;
    else
        targettime = ctrl->target->los;
    
    delta = targettime - t;
    
    /* convert julian date to seconds */
    s = (guint) (delta * 86400);

    /* extract hours */
    h = (guint) floor (s/3600);
    s -= 3600*h;

    /* leading zero */
    if ((h > 0) && (h < 10))
        ch = g_strdup ("0");
    else
        ch = g_strdup ("");

    /* extract minutes */
    m = (guint) floor (s/60);
    s -= 60*m;

    /* leading zero */
    if (m < 10)
        cm = g_strdup ("0");
    else
        cm = g_strdup ("");

    /* leading zero */
    if (s < 10)
        cs = g_strdup (":0");
    else
        cs = g_strdup (":");

    if (h > 0) 
        buff = g_strdup_printf ("%s%d:%s%d%s%d", ch, h, cm, m, cs, s);
    else
        buff = g_strdup_printf ("%s%d%s%d", cm, m, cs, s);

    gtk_label_set_text (GTK_LABEL (ctrl->SatCnt), buff);

    g_free (buff);
    g_free (ch);
    g_free (cm);
    g_free (cs);

}

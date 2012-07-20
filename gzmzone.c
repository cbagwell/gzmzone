#include <stdio.h>
#include <setjmp.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <dirent.h>
#include <string.h>
#include <stdint.h>


GdkPixbuf *ref_pixbuf = NULL; /* Holds reference image */
GdkPixbuf *display_pixbuf = NULL; /* Holds display image */

guchar *gdiff_buffer; /* Reusable temp buffer to calc image difference */

char *dirname = NULL; /* Directory to read jpegs from */
struct dirent **namelist; /* List from jpeg's filenames in dirname */
int nl_count = 0; /* Count of jpeg's in current directory */
int nl_current = 0; /* Current jpeg to read in from list */

#define FILE_PATH "/media/Ext4 750G WD/1/12/07/10/08/20/39"
unsigned char *abs_table, *y_r_table, *y_g_table, *y_b_table;

typedef struct
{
    GtkWidget *filechooser;
    GtkWidget *zonevideo;
    GtkWidget *status;
    int playing;
    int frames_per_minute;
    int reference;
    int blend;
    int minthreshold;
    int maxthreshold;
    int alarmmethod;
    int minalarmed;
    int maxalarmed;
    int xerosion;
    int yerosion;
    int minfiltered;
    int maxfiltered;
    int blendpercent;
    float diff_percent;
    int alarmed;
    int percent_count;
} gzm_data;

/* blend2 - instead of blending a percentage of the current frame into the
 * reference image, increment the pixel 1 value towards current pixel.
 * Besides being fast and simple, in my test it appears to take much
 * longer for new objects to become part of reference image; and thus
 * causes motion to contine to be detect at higher percent.
 * Since it takes worste case of 255 frames to align then its not
 * huge difference.
 */
void blend2(unsigned char *ref_buffer, unsigned char *curr_buffer, int size,
	    int transparency)
{
    unsigned char *srcp, *destp;

    srcp = curr_buffer;
    destp = ref_buffer;

    while (destp < (ref_buffer+size))
    {
	if (*destp > *srcp)
	    *destp -= 1;
	else if (*destp < *srcp)
	    *destp += 1;
	destp++;
	srcp++;
    }
}

/* Blends specified percentage of current image into reference image.
 *
 * Uses same lookup table as zoneminder.
 *
 * TODO: Zoneminder has an option to turn off lookup table since it
 * can have rounding errors.  It would be interesting to see what
 * affect that has.
 */
void blend(unsigned char *ref_buffer, unsigned char *curr_buffer, int size,
	   int transparency)
{
    static uint16_t *blend_buffer = 0;
    unsigned char *srcp, *destp;
    uint16_t *blendp;
    int opacity;

    if (!blend_buffer)
    {
	blend_buffer = malloc(size*2);
	blendp = blend_buffer;
	destp = ref_buffer;
	while (destp < (ref_buffer+size))
	       *blendp++ = (uint16_t)((*destp++)<<8);
    }

    srcp = curr_buffer;
    destp = ref_buffer;
    blendp = blend_buffer;
    opacity = 100-transparency;

    while (destp < (ref_buffer+size))
    {
	*blendp = (uint16_t)(((*blendp * opacity)+(((*srcp++)<<8) * transparency))/100);
	*destp++ = (unsigned char)((*blendp++)>>8);
    }
}

/* time for a new frame.
 *
 * Approximation of zoneminders logic for motion detection.  Minor coding
 * differences but intent is the same.  Seems to be making same decisions
 * in basic tests.
 */
gboolean timeout_draw(gpointer user_data)
{
    gzm_data *data = (gzm_data *)user_data;
    char fn[1024];
    GdkPixbuf *current_pixbuf;
    unsigned char *current_buffer, *end_buffer, *ref_buffer, *diff_buffer;
    unsigned char *display_buffer;
    int diff_count = 0;
    int x, y;
    int width=320, height=240;

    if (nl_current >= nl_count || !data->playing)
    {
	if (nl_current >= nl_count)
	{
	    data->playing = 0;
	    nl_count = 0;
	}
	return FALSE;
    }

    if (dirname == NULL || nl_current >= nl_count)
	return TRUE;

    if (nl_current == 0)
    {
	data->percent_count = 0;
	data->alarmed = 0;
    }

    /* Load a new jpeg */
    sprintf(fn, "%s/%s", dirname, namelist[nl_current++]->d_name);
    printf("filename = %s\n", fn);
    current_pixbuf = gdk_pixbuf_new_from_file(fn, NULL);
    width = gdk_pixbuf_get_width(current_pixbuf);
    height = gdk_pixbuf_get_height(current_pixbuf);

    /* For first image, make reference frame same as current frame.
     * Zoneminder will have been running for longer then this and
     * reference frame will be slightly defferent based on blending.
     */
    if (ref_pixbuf == NULL)
	ref_pixbuf = current_pixbuf;

    if (display_pixbuf == NULL)
	display_pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8,
					width, height);

    /* Loop over current image and reference image and store differences
     * as black and white image. */
    current_buffer = gdk_pixbuf_get_pixels(current_pixbuf);
    end_buffer = current_buffer + (width*3*height);
    ref_buffer = gdk_pixbuf_get_pixels(ref_pixbuf);
    diff_buffer = gdiff_buffer;

    // TODO: This only does the Y difference.  Need to supporting turning
    // it off.
    while (current_buffer < end_buffer)
    {
	unsigned char diff, red, green, blue;

	red = y_r_table[*current_buffer - *ref_buffer++];
	green = y_g_table[*(current_buffer+1) - *ref_buffer++];
	blue = y_b_table[*(current_buffer+2) - *ref_buffer++];
	diff = abs_table[red + green + blue];
	if (diff >= data->minthreshold && diff < data->maxthreshold)
	{
	    *diff_buffer++ = 0xff;
	    diff_count++;
	}
	else
	    *diff_buffer++ = 0x00;
	current_buffer += 3;
    }

    data->diff_percent = diff_count*100.0/(width*height);

    if (data->alarmmethod == 0)
    {
	if (data->diff_percent >= data->minalarmed &&
	    data->diff_percent <= data->maxalarmed)
	    data->alarmed = 1;
    }
    else if (data->diff_percent >= data->minalarmed &&
	     data->diff_percent <= data->maxalarmed)
    {
	/* 3x3 erosion - ZM calls it "filter" without discussing what its
	 * good for (removing unwanted noise/speckles).  Using full 9
	 * squares seems a little agressive with my camera.  A + pattern
	 * may be better.
	 */
	int x_erosion_offset = data->xerosion / 2;
	int y_erosion_offset = data->yerosion / 2;

	diff_buffer = gdiff_buffer;
	for (y = 0+y_erosion_offset; y < height-y_erosion_offset; y++)
	{
	    for (x = 0+x_erosion_offset; x < width-x_erosion_offset; x++)
	    {
		unsigned char *pdiff = diff_buffer + (y * width) + x;
		int dx, dy;
		int block=1;

		for (dy = y_erosion_offset*-1;
		     *pdiff && dy <= y_erosion_offset; dy++)
		{
		    for (dx = x_erosion_offset*-1;
			 dx <= x_erosion_offset; dx++)
		    {
			int offset = (width * dy) + dx;
			if ((*(pdiff+offset) == 0x00))
			    block = 0;
		    }
		}
		/* Subtract instead of clearing so that this can be
		 * done in place.
		 */
		if (!block)
		    *pdiff++ -= 1;
	    }
	}
    }

    display_buffer = gdk_pixbuf_get_pixels(display_pixbuf);
    end_buffer = display_buffer + (width*3*height);
    current_buffer = gdk_pixbuf_get_pixels(current_pixbuf);
    ref_buffer = gdk_pixbuf_get_pixels(ref_pixbuf);
    if (!data->reference)
	memcpy(display_buffer, current_buffer, width*3*height);
    else
	memcpy(display_buffer, ref_buffer, width*3*height);
    diff_buffer = gdiff_buffer;

    diff_count = 0;
    while (display_buffer < end_buffer)
    {
	/* Check specifically for 0xff since erode decrements */
	if (*diff_buffer++ == 0xff)
	{
	    *display_buffer++ = 0x00;
	    *display_buffer++ = 0xff;
	    *display_buffer++ = 0x00;
	    diff_count++;
	}
	else
	    display_buffer += 3;

	data->diff_percent = diff_count*100.0/(width*height);
	if (data->diff_percent >= data->minfiltered &&
	    data->diff_percent <= data->maxfiltered)
	    data->alarmed = 1;
    }

    if (data->diff_percent >= 1)
	data->percent_count++;

    if (ref_pixbuf != current_pixbuf)
    {
    	current_buffer = gdk_pixbuf_get_pixels(current_pixbuf);
	ref_buffer = gdk_pixbuf_get_pixels(ref_pixbuf);
	if (!data->blend)
	    blend(ref_buffer, current_buffer, width*3*height, data->blendpercent);
	else
	    blend2(ref_buffer, current_buffer, width*3*height, data->blendpercent);
    }

    gtk_widget_queue_draw_area(data->zonevideo, 0, 0, width, height);

    return TRUE;
}

int jpeg_filter(const struct dirent *d)
{
    int len = strlen(d->d_name);

    return (len >= 4 && strcmp(d->d_name+len-4, ".jpg") == 0);
}

/*
 * Begin UI Callbacks
 */

G_MODULE_EXPORT void cb_play(GtkToolButton *widget, gpointer user_data)
{
    gzm_data *data = user_data;

    if (data->playing)
	return;

    nl_count = scandir(dirname, &namelist, jpeg_filter, alphasort);
    nl_current = 0;
    if (ref_pixbuf)
    {
	g_object_unref(ref_pixbuf);
	ref_pixbuf = NULL;
    }

    data->playing = 1;

    g_timeout_add(1000 * 60 / data->frames_per_minute, timeout_draw, user_data);
}

G_MODULE_EXPORT void cb_stop(GtkToolButton *widget, gpointer user_data)
{
    gzm_data *data = user_data;

    data->playing = 0;
}

G_MODULE_EXPORT void cb_file_set(GtkFileChooserButton *widget, gpointer user_data)
{
    if (dirname)
	free(dirname);

    dirname = strdup(gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(widget)));
    printf("dirname = %s\n", dirname);
}

G_MODULE_EXPORT void cb_method(GtkComboBox *widget, gpointer user_data)
{
    gzm_data *data = user_data;

    data->alarmmethod = gtk_combo_box_get_active(widget);
}

G_MODULE_EXPORT void cb_frames(GtkSpinButton *widget, gpointer user_data)
{
    gzm_data *data = user_data;

    data->frames_per_minute = gtk_spin_button_get_value_as_int(widget);
}

G_MODULE_EXPORT void cb_blend(GtkToggleButton *widget, gpointer user_data)
{
    gzm_data *data = user_data;

    data->blend = gtk_toggle_button_get_active(widget);
}

G_MODULE_EXPORT void cb_reference(GtkToggleButton *widget, gpointer user_data)
{
    gzm_data *data = user_data;

    data->reference = gtk_toggle_button_get_active(widget);
}


G_MODULE_EXPORT void cb_minpixel(GtkSpinButton *widget, gpointer user_data)
{
    gzm_data *data = user_data;

    data->minthreshold = gtk_spin_button_get_value_as_int(widget);
}

G_MODULE_EXPORT void cb_maxpixel(GtkSpinButton *widget, gpointer user_data)
{
    gzm_data *data = user_data;

    data->maxthreshold = gtk_spin_button_get_value_as_int(widget);
}

G_MODULE_EXPORT void cb_minalarmed(GtkSpinButton *widget, gpointer user_data)
{
    gzm_data *data = user_data;

    data->minalarmed = gtk_spin_button_get_value_as_int(widget);
}

G_MODULE_EXPORT void cb_maxalarmed(GtkSpinButton *widget, gpointer user_data)
{
    gzm_data *data = user_data;

    data->maxalarmed = gtk_spin_button_get_value_as_int(widget);
}

G_MODULE_EXPORT void cb_xerosion(GtkSpinButton *widget, gpointer user_data)
{
    gzm_data *data = user_data;

    data->xerosion = gtk_spin_button_get_value_as_int(widget);
}

G_MODULE_EXPORT void cb_yerosion(GtkSpinButton *widget, gpointer user_data)
{
    gzm_data *data = user_data;

    data->yerosion = gtk_spin_button_get_value_as_int(widget);
}

G_MODULE_EXPORT void cb_minfiltered(GtkSpinButton *widget, gpointer user_data)
{
    gzm_data *data = user_data;

    data->minfiltered = gtk_spin_button_get_value_as_int(widget);
}

G_MODULE_EXPORT void cb_maxfiltered(GtkSpinButton *widget, gpointer user_data)
{
    gzm_data *data = user_data;

    data->maxfiltered = gtk_spin_button_get_value_as_int(widget);
}

G_MODULE_EXPORT void cb_blendpercent(GtkSpinButton *widget, gpointer user_data)
{
    gzm_data *data = user_data;

    data->blendpercent = gtk_spin_button_get_value_as_int(widget);
}

/*
 * End UI Callbacks
 */

G_MODULE_EXPORT gboolean cb_draw_zone(GtkWidget *widget, cairo_t *cr,
				      gpointer user_data)
{
    gzm_data *data = user_data;
    int width, height;
    char status[80];

    if (display_pixbuf == NULL)
	return TRUE;

    width = gdk_pixbuf_get_width(display_pixbuf);
    height = gdk_pixbuf_get_height(display_pixbuf);
    gtk_widget_set_size_request(GTK_WIDGET(widget), width, height);

    gdk_cairo_set_source_pixbuf(cr, display_pixbuf, 0, 0);
    cairo_paint(cr);

    sprintf(status, "Diff Percentage: %.2f%% - %s (%d)", data->diff_percent,
	    data->alarmed ? "ALARMED" : "Idle", data->percent_count);
    gtk_label_set_text(GTK_LABEL(data->status), status);

    return TRUE;
}

int main(int argc, char **argv)
{
    GtkBuilder *builder;
    GtkWidget *window;
    GError *error = NULL;
    gzm_data *data;
    int i;

    gtk_init(&argc, &argv);

    builder = gtk_builder_new();
    if (!gtk_builder_add_from_file(builder, "zmzone.glade", &error))
    {
	g_warning("%s", error->message);
	g_free(error);
	return 1;
    }

    abs_table = malloc((6 * 255)+1);
    abs_table += (3*255);
    y_r_table = malloc(511);
    y_r_table += 255;
    y_g_table = malloc(511);
    y_g_table += 255;
    y_b_table = malloc(511);
    y_b_table += 255;

    for (i = -(3*255); i <= (3*255); i++)
	abs_table[i] = abs(i);

    for (i = -255; i <= 255; i++)
    {
	y_r_table[i] = (2990*abs(i))/10000;
	y_g_table[i] = (5670*abs(i))/10000;
	y_b_table[i] = (1140*abs(i))/10000;
    }

    gdiff_buffer = malloc(640 * 3 * 480);

    window = GTK_WIDGET(gtk_builder_get_object(builder, "zmzone"));

    data = g_slice_new(gzm_data);
    data->filechooser = GTK_WIDGET(gtk_builder_get_object(builder, "filechooser"));
    data->zonevideo = GTK_WIDGET(gtk_builder_get_object(builder, "zonevideo"));
    data->status = GTK_WIDGET(gtk_builder_get_object(builder, "status"));
    data->playing = 0;
    data->frames_per_minute = 60;
    data->reference = 0;
    data->blend = 0;
    data->minthreshold = 20;
    data->maxthreshold = 255;
    data->alarmmethod = 1;
    data->minalarmed = 9;
    data->maxalarmed = 100;
    data->xerosion = 3;
    data->yerosion = 3;
    data->minfiltered = 6;
    data->maxfiltered = 100;
    data->blendpercent = 7;

    gtk_builder_connect_signals(builder, data);

    g_object_unref(G_OBJECT(builder));

    gtk_widget_show(window);

    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(data->filechooser), FILE_PATH);
    dirname = strdup(FILE_PATH);

    gtk_main();

    g_slice_free(gzm_data, data);

    return 0;
}

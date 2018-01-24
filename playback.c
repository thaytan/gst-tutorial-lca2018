
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <gst/gst.h>

typedef struct
{
  GMainLoop *loop;
  GstElement *playbin;
  guint bus_watch;
  guint io_watch_id;
} GlobalData;

static gboolean handle_bus_msg (GstBus * bus, GstMessage * msg,
    GlobalData * data);
static gboolean io_callback (GIOChannel * io, GIOCondition condition,
    GlobalData * data);

static GstElement *
create_element (const gchar * type, const gchar * name)
{
  GstElement *e;

  e = gst_element_factory_make (type, name);
  if (!e) {
    g_print ("Failed to create element %s\n", type);
    exit (1);
  }

  return e;
}

static gchar *
canonicalise_uri (const gchar * in)
{
  if (gst_uri_is_valid (in))
    return g_strdup (in);

  return gst_filename_to_uri (in, NULL);
}

int
main (int argc, char *argv[])
{
  GlobalData data;
  GIOChannel *io = NULL;
  GstBus *bus;
  gchar *uri;

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  if (argc < 2) {
    g_print ("Usage: %s <file|URI>\n", argv[0]);
    g_print ("When running, pressing 'q' quits the application\n"
        "'f' seeks backwards 10 seconds\n"
        "'g' seeks forwards 10 seconds\n"
        "For this trivial example, you need to press enter after each command\n");
    return 1;
  }

  /* Build the pipeline */
  data.playbin = create_element ("playbin", NULL);

  /* Make sure the input filename or uri is a uri */
  uri = canonicalise_uri (argv[1]);

  /* Set the uri property on playbin */
  g_object_set (data.playbin, "uri", uri, NULL);

  /* Connect to the bus to receive callbacks */
  bus = gst_element_get_bus (data.playbin);

  data.bus_watch = gst_bus_add_watch (bus, (GstBusFunc) handle_bus_msg, &data);

  gst_object_unref (bus);

  /* Set up the main loop */
  data.loop = g_main_loop_new (NULL, FALSE);

  /* Start playing */
  gst_element_set_state (data.playbin, GST_STATE_PLAYING);
  g_print ("Now playing %s\n", uri);

  g_free (uri);

  /* Listen to stdin input */
  io = g_io_channel_unix_new (fileno (stdin));
  data.io_watch_id = g_io_add_watch (io, G_IO_IN, (GIOFunc) (io_callback),
      &data);
  g_io_channel_unref (io);

  /* Run the mainloop until it is exited by the message handler */
  g_main_loop_run (data.loop);

  /* Clean everything up before exiting */
  g_source_remove (data.bus_watch);
  g_source_remove (data.io_watch_id);
  gst_element_set_state (data.playbin, GST_STATE_NULL);
  gst_object_unref (data.playbin);
  g_main_loop_unref (data.loop);

  return 0;
}

static gboolean
handle_bus_msg (GstBus * bus, GstMessage * msg, GlobalData * data)
{
  /* Wait until error or EOS */
  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_EOS:{
      g_print ("Finished playback. Exiting.\n");
      g_main_loop_quit (data->loop);
      break;
    }
    case GST_MESSAGE_ERROR:{
      GError *err = NULL;
      gchar *dbg_info = NULL;

      gst_message_parse_error (msg, &err, &dbg_info);
      g_printerr ("ERROR from element %s: %s\n",
          GST_OBJECT_NAME (msg->src), err->message);
      g_printerr ("Debugging info: %s\n", (dbg_info) ? dbg_info : "none");
      g_print ("Exiting.\n");
      g_error_free (err);
      g_free (dbg_info);

      g_main_loop_quit (data->loop);
      break;
    }
    case GST_MESSAGE_TAG:{
      GstTagList *tags;
      gchar *value;

      gst_message_parse_tag (msg, &tags);

      g_print ("Found tags\n");
      if (gst_tag_list_get_string (tags, GST_TAG_ARTIST, &value)) {
        g_print ("Artist: %s\n", value);
        g_free (value);
      }

      if (gst_tag_list_get_string (tags, GST_TAG_TITLE, &value)) {
        g_print ("Title: %s\n", value);
        g_free (value);
      }

      if (gst_tag_list_get_string (tags, GST_TAG_ALBUM, &value)) {
        g_print ("Album: %s\n", value);
        g_free (value);
      }

      gst_tag_list_free (tags);
      break;
    }
    case GST_MESSAGE_ASYNC_DONE:{
      GstPad *video_pad;
      GstCaps *caps;
      GstStructure *s;

      g_signal_emit_by_name (data->playbin, "get-video-pad", 0, &video_pad);
      if (video_pad) {
        gint width, height;
        gint par_n, par_d;

        caps = gst_pad_get_current_caps (video_pad);
        s = gst_caps_get_structure (caps, 0);

        gst_structure_get_int (s, "width", &width);
        gst_structure_get_int (s, "height", &height);
        par_n = par_d = 1;
        gst_structure_get_fraction (s, "pixel-aspect-ratio", &par_n, &par_d);

        width = width * par_n / par_d;
        g_print ("Video size: %dx%d\n", width, height);
        gst_caps_unref (caps);
        gst_object_unref (video_pad);
      }

      break;
    }
    default:
      /* Ignore messages we don't know about */
      break;
  }

  return TRUE;
}

static void
seek (GlobalData * data, gboolean forward)
{
  gint64 position;

  if (!gst_element_query_position (data->playbin, GST_FORMAT_TIME, &position))
    return;

  if (forward)
    position += 10 * GST_SECOND;
  else if (position > 10 * GST_SECOND)
    position -= 10 * GST_SECOND;
  else
    position = 0;

  gst_element_seek_simple (data->playbin, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH,
      position);
}

static gboolean
io_callback (GIOChannel * io, GIOCondition condition, GlobalData * data)
{
  gchar in;
  GError *error = NULL;

  switch (g_io_channel_read_chars (io, &in, 1, NULL, &error)) {
    case G_IO_STATUS_NORMAL:
      switch (in) {
        case 'q':
          g_main_loop_quit (data->loop);
          break;
        case 'f':
          seek (data, FALSE);
          break;
        case 'g':
          seek (data, TRUE);
          break;
      }
      break;
    case G_IO_STATUS_AGAIN:
      break;
    case G_IO_STATUS_ERROR:
      g_printerr ("stdin IO error: %s\n", error->message);
      g_error_free (error);
      g_main_loop_quit (data->loop);
      return FALSE;
    default:
      return FALSE;
  }

  return TRUE;
}

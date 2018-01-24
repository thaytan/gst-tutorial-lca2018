
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#include <gst/net/gstnetclientclock.h>

static gchar *clock_host = NULL;
static gint clock_port = 0;
static GstClockTime base_time = GST_CLOCK_TIME_NONE;

static GOptionEntry opt_entries[] = {
  {"clock-host", 'c', 0, G_OPTION_ARG_STRING, &clock_host,
      "Network clock provider host IP", NULL},
  {"clock-port", 'p', 0, G_OPTION_ARG_INT, &clock_port,
      "Network clock provider port", NULL},
  {"base-time", 'b', 0, G_OPTION_ARG_INT64, &base_time,
      "Playback base time to sync to", NULL},
};

enum
{
  PLAY_FLAGS_VIDEO = 0x1,
  PLAY_FLAGS_AUDIO = 0x2,
  PLAY_FLAGS_SUBTITLES = 0x4,
  PLAY_FLAGS_VISUALISATIONS = 0x8,
  PLAY_FLAGS_DOWNLOAD = 0x80
};

typedef struct
{
  GMainLoop *loop;
  GstElement *playbin;
  guint bus_watch;
  guint io_watch_id;

  gboolean buffering;
  gboolean is_live;
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
  GOptionContext *opt_ctx;
  GstClock *net_clock;
  GError *err = NULL;
  GlobalData data;
  GIOChannel *io = NULL;
  GstBus *bus;
  gchar *uri;
  GstStateChangeReturn sret;
  gint flags;

  /* Initialize GStreamer */
  opt_ctx = g_option_context_new ("- Network clock playback");
  g_option_context_add_main_entries (opt_ctx, opt_entries, NULL);
  g_option_context_add_group (opt_ctx, gst_init_get_option_group ());
  if (!g_option_context_parse (opt_ctx, &argc, &argv, &err))
    g_error ("Error parsing options: %s", err->message);
  g_clear_error (&err);
  g_option_context_free (opt_ctx);

  if (argc < 2 || clock_host == NULL || clock_port == 0) {
    g_print ("Usage: %s -c netclock-host-IP -p netclock-host-port -b base-time <file>\n", argv[0]);
    g_print ("When running, pressing 'q' quits the application\n"
        "'f' seeks backwards 10 seconds\n"
        "'g' seeks forwards 10 seconds\n"
        "'a' switches to the next audio track\n"
        "'d' enables/disables subtitles\n"
        "'s' switches to the next subtitle track\n"
        "'v' enables/disables visualisations\n"
        "For this trivial example, you need to press enter after each command\n");
    return 1;
  }

  net_clock = gst_net_client_clock_new (NULL, clock_host, clock_port, 0);
  /* Wait until the local clock synchronises to the master */
  gst_clock_wait_for_sync (net_clock, GST_CLOCK_TIME_NONE);
  g_print ("Network clock is synched to master\n");

  /* Build the pipeline */
  data.playbin = create_element ("playbin", "playbin");

  /* Tell the pipeline to always use this clock, and disable
   * automatic selection */
  gst_pipeline_use_clock (GST_PIPELINE (data.playbin), net_clock);

  /* We can release the clock now - the pipeline has a handle already */
  gst_object_unref (GST_OBJECT (net_clock));

  /* If a base-time was supplied, pass that to the pipeline */
  if (base_time != GST_CLOCK_TIME_NONE) {
    gst_element_set_start_time(GST_ELEMENT (data.playbin), GST_CLOCK_TIME_NONE);
    gst_element_set_base_time (GST_ELEMENT (data.playbin), base_time);
  }

  /* Make everyone try and play with 100ms latency */
  gst_pipeline_set_latency (GST_PIPELINE (data.playbin), 100 * GST_MSECOND);

  /* Make sure the input filename or uri is a uri */
  uri = canonicalise_uri (argv[1]);

  /* Set the uri property on playbin */
  g_object_set (data.playbin, "uri", uri, NULL);

  /* Set the playbin download flag */
  g_object_get (data.playbin, "flags", &flags, NULL);
  flags |= PLAY_FLAGS_DOWNLOAD;
  g_object_set (data.playbin, "flags", flags, NULL);

  /* Connect to the bus to receive callbacks */
  bus = gst_element_get_bus (data.playbin);

  data.bus_watch = gst_bus_add_watch (bus, (GstBusFunc) handle_bus_msg, &data);

  gst_object_unref (bus);

  /* Set up the main loop */
  data.loop = g_main_loop_new (NULL, FALSE);

  /* Start playing */
  sret = gst_element_set_state (data.playbin, GST_STATE_PLAYING);

  g_print ("Now playing %s\n", uri);
  g_free (uri);

  switch (sret) {
    case GST_STATE_CHANGE_FAILURE:
      /* ignore, there will be an error message on the bus */
      break;
    case GST_STATE_CHANGE_NO_PREROLL:
      g_print ("Pipeline is live.\n");
      data.is_live = TRUE;
      break;
    case GST_STATE_CHANGE_ASYNC:
      g_print ("Prerolling...\r");
      break;
    default:
      break;
  }

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

      g_print ("Prerolled.\r");

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
    case GST_MESSAGE_BUFFERING:{
      gint percent;

      if (!data->buffering)
        g_print ("\n");

      gst_message_parse_buffering (msg, &percent);
      g_print ("Buffering... %d%%  \r", percent);

      /* no state management needed for live pipelines */
      if (data->is_live)
        break;

      if (percent == 100) {
        /* a 100% message means buffering is done */
        if (data->buffering) {
          data->buffering = FALSE;
          gst_element_set_state (data->playbin, GST_STATE_PLAYING);
        }
      } else {
        /* buffering... */
        if (!data->buffering) {
          gst_element_set_state (data->playbin, GST_STATE_PAUSED);
          data->buffering = TRUE;
        }
      }
      break;
    }
    case GST_MESSAGE_CLOCK_LOST:{
      /* Clock-lost means the pipeline wants to select a new clock,
       * which is done by pausing/playing */
      g_print ("Clock lost, selecting a new one\n");
      gst_element_set_state (data->playbin, GST_STATE_PAUSED);
      gst_element_set_state (data->playbin, GST_STATE_PLAYING);
      break;
    }
    case GST_MESSAGE_LATENCY:
      g_print ("Redistribute latency...\n");
      gst_bin_recalculate_latency (GST_BIN (data->playbin));
      break;
    case GST_MESSAGE_REQUEST_STATE:{
      GstState state;
      gchar *name;

      name = gst_object_get_path_string (GST_MESSAGE_SRC (msg));

      gst_message_parse_request_state (msg, &state);

      g_print ("Setting state to %s as requested by %s...\n",
          gst_element_state_get_name (state), name);

      gst_element_set_state (data->playbin, state);
      g_free (name);
      break;
    }
    case GST_MESSAGE_WARNING:{
      GError *err;
      gchar *dbg = NULL;

      gst_message_parse_warning (msg, &err, &dbg);
      g_printerr ("WARNING %s\n", err->message);
      if (dbg != NULL)
        g_printerr ("WARNING debug information: %s\n", dbg);
      g_error_free (err);
      g_free (dbg);
      break;
    }
    case GST_MESSAGE_STATE_CHANGED: {
      if (GST_MESSAGE_SRC (msg) == GST_OBJECT_CAST (data->playbin)) {
        GstState old, new, pending;
        gst_message_parse_state_changed (msg, &old, &new, &pending);
        if (old == GST_STATE_PAUSED && new == GST_STATE_PLAYING) {
          g_print ("Reached playing. Base time is %" G_GUINT64_FORMAT "\n",
              gst_element_get_base_time (GST_ELEMENT (data->playbin)));
        }
      }
      break;
    }
    default:{
      if (gst_is_missing_plugin_message (msg)) {
        gchar *desc;

        desc = gst_missing_plugin_message_get_description (msg);
        g_print ("Missing plugin: %s\n", desc);
        g_free (desc);
      }
      /* Ignore messages we don't know about */
      break;
    }
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

static void
next_audio (GlobalData * data)
{
  gint current, count;

  /* Switch to the next audio track */
  g_object_get (data->playbin,
      "current-audio", &current, "n-audio", &count, NULL);
  current += 1;
  if (current >= count)
    current = 0;

  g_object_set (data->playbin, "current-audio", current, NULL);
  g_print ("Now playing audio track %d of %d\n", current, count);
}

static void
toggle_subtitle (GlobalData * data)
{
  gint flags;

  g_object_get (data->playbin, "flags", &flags, NULL);
  if (flags & PLAY_FLAGS_SUBTITLES) {
    g_print ("Disabling subtitles\n");
    flags &= ~PLAY_FLAGS_SUBTITLES;
  } else {
    g_print ("Enabling subtitles\n");
    flags |= PLAY_FLAGS_SUBTITLES;
  }

  g_object_set (data->playbin, "flags", flags, NULL);
}

static void
next_subtitle (GlobalData * data)
{
  gint flags;
  gint current, count;

  /* Switch to the next subtitle track */
  g_object_get (data->playbin,
      "current-text", &current, "n-text", &count, NULL);
  current += 1;
  if (current >= count)
    current = 0;
  g_object_set (data->playbin, "current-text", current, NULL);

  /* Make sure subtitles are enabled */
  g_object_get (data->playbin, "flags", &flags, NULL);
  flags |= PLAY_FLAGS_SUBTITLES;
  g_object_set (data->playbin, "flags", flags, NULL);
  g_print ("Now showing subtitles track %d of %d\n", current, count);
}

static void
toggle_vis (GlobalData * data)
{
  gint flags;

  g_object_get (data->playbin, "flags", &flags, NULL);
  if (flags & PLAY_FLAGS_VISUALISATIONS) {
    g_print ("Disabling visualisations\n");
    flags &= ~PLAY_FLAGS_VISUALISATIONS;
  } else {
    g_print ("Enabling visualisations\n");
    flags |= PLAY_FLAGS_VISUALISATIONS;
  }

  g_object_set (data->playbin, "flags", flags, NULL);
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
        case 'a':
          next_audio (data);
          break;
        case 'd':
          toggle_subtitle (data);
          break;
        case 's':
          next_subtitle (data);
          break;
        case 'v':
          toggle_vis (data);
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

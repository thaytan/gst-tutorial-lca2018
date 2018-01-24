#include <stdlib.h>
#include <gst/gst.h>
#include <gst/net/gstnettimeprovider.h>

gboolean print_time (gpointer user_data)
{
  GstClock *clock = GST_CLOCK (user_data);
  g_print("Base time %" G_GUINT64_FORMAT "\r",
      gst_clock_get_time (clock));

  return G_SOURCE_CONTINUE;
}

gint
main (gint argc, gchar * argv[])
{
  GMainLoop *loop;
  GstClock *clock;
  GstNetTimeProvider *net_clock;
  int clock_port = 0;

  gst_init (&argc, &argv);

  if (argc > 1)
    clock_port = atoi (argv[1]);

  loop = g_main_loop_new (NULL, FALSE);

  clock = gst_system_clock_obtain ();
  net_clock = gst_net_time_provider_new (clock, NULL, clock_port);

  g_object_get (net_clock, "port", &clock_port, NULL);

  g_print ("Published network clock on port %u\n", clock_port);

  g_timeout_add_seconds (1, print_time, clock);
  g_main_loop_run (loop);

  /* cleanup */
  gst_object_unref (clock);
  g_main_loop_unref (loop);

  return 0;
}

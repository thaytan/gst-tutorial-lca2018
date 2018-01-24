To use this example, run ./netclock-server

Every second, it will print a base time. Copy that base time, and
use it in several instances of the ./playback-sync app to play
a file or URI synchronised across multiple instances like this:

./playback-sync -c netclock-host-IP -p netclock-host-port -b base-time <file>

providing the values for the netclock-server IP address, port and selected
base time. Use the same base time on each player to get synchronised playback

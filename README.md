# lutron-integration
linux service for controlling Lutron Caseta Wireless Smart Bridge 2 Pro over telnet

planned features:
* json control api
  * abstraction of lutron protocol into something more robust
* smarter schedules
  * conditional actions
    * example: only dim lights if they are at a higher level
  * eventual support for light sensors
    * match interior intensity to exterior amibient level
  * anticipatory geo-fencing
    * start arriving schedule when leaving work
* InfluxDB event logging support

# lutron-integration
Linux service for controlling Lutron Caseta Wireless Smart Bridge 2 Pro over telnet

Planned features:
* JSON control API (via SSH)
  * abstraction of Lutron telnet protocol into something more robust and secure
* smarter schedules
  * conditional actions
    * example: only dim lights if they are at a higher level
  * eventual support for light sensors
    * match interior intensity to exterior amibient level
  * anticipatory geo-fencing
    * start arriving schedule when leaving work
* InfluxDB event logging support

#!/usr/bin/env bash
# Configure THIS machine as the other end of the board's USB gadget link, and
# share its internet connection with the board.
#
# Board side is already done by the image: usb0, static 10.42.0.2/24.
# This side takes 10.42.0.1/24 and NATs the board out through wlan0.
#
#   sudo ./image/host-gadget-net.sh            # auto-detect the interface
#   sudo ./image/host-gadget-net.sh enp0s20u1  # or name it
set -euo pipefail
[ "$(id -u)" -eq 0 ] || { echo "run me with sudo"; exit 1; }

HOST_IP=10.42.0.1/24
UPLINK="${UPLINK:-$(ip route show default | awk '{print $5; exit}')}"

IFACE="${1:-}"
if [ -z "$IFACE" ]; then
  # A CDC gadget shows up as usb0 or as a predictable enx<mac> name.
  IFACE=$(ip -o link | awk -F': ' '{print $2}' | grep -E '^(usb[0-9]+|enx[0-9a-f]{12})$' | head -1 || true)
fi
[ -n "$IFACE" ] || {
  echo "No gadget interface found."
  echo
  echo "The board side is confirmed working — g_ether reports ready and usb0"
  echo "is up. If nothing appears here, the cable or the socket is wrong:"
  echo "  * use the cable that showed 1f3a:efe8 during FEL (some are charge-only)"
  echo "  * use the OTG Type-C socket, not the power-only one"
  exit 1
}

echo ":: gadget interface : ${IFACE}"
echo ":: uplink           : ${UPLINK}"

# NetworkManager will try DHCP and fail; there is no server on the board.
command -v nmcli >/dev/null && nmcli device set "$IFACE" managed no 2>/dev/null || true

ip addr flush dev "$IFACE" 2>/dev/null || true
ip addr add "$HOST_IP" dev "$IFACE"
ip link set "$IFACE" up

# Share the uplink so pacman works on the board.
sysctl -qw net.ipv4.ip_forward=1
iptables -t nat -C POSTROUTING -s 10.42.0.0/24 -o "$UPLINK" -j MASQUERADE 2>/dev/null \
  || iptables -t nat -A POSTROUTING -s 10.42.0.0/24 -o "$UPLINK" -j MASQUERADE
iptables -C FORWARD -i "$IFACE" -o "$UPLINK" -j ACCEPT 2>/dev/null \
  || iptables -I FORWARD -i "$IFACE" -o "$UPLINK" -j ACCEPT
iptables -C FORWARD -i "$UPLINK" -o "$IFACE" -m state --state RELATED,ESTABLISHED -j ACCEPT 2>/dev/null \
  || iptables -I FORWARD -i "$UPLINK" -o "$IFACE" -m state --state RELATED,ESTABLISHED -j ACCEPT

echo
echo "Host ready. Now check the link:"
echo "  ping -c3 10.42.0.2"
echo "  ssh alarm@10.42.0.2       # password: alarm"
echo
echo "On the board, add a route and DNS so pacman can reach the internet:"
echo "  sudo ip route add default via 10.42.0.1"
echo "  echo 'nameserver 1.1.1.1' | sudo tee /etc/resolv.conf"

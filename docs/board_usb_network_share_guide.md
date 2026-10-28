Your PocketBeagle is connected to the computer via a USB network interface, but it doesn't have direct internet access by default. To run `apt` on the board, you need to let your computer "share" its internet connection with the board. This process is called Internet Connection Sharing (ICS), and the setup method depends on your computer's operating system.

### 💻 If Your Computer Runs Windows

This is the most common setup for beginners. You need to enable ICS and let the PocketBeagle automatically obtain an IP address.

1.  **Enable ICS on Your Computer**: Open **Control Panel** > **Network and Sharing Center** > **Change adapter settings** (or enter `ncpa.cpl` in the Run dialog). Right-click on the network adapter that is **currently connected to the internet** (usually Wi-Fi or Ethernet), select **Properties**, go to the **Sharing** tab, check "Allow other network users to connect through this computer's Internet connection", and select the PocketBeagle's USB network adapter in the dropdown menu below (it may appear as "Ethernet" or "RNDIS") .
2.  **Let the Board Obtain a New IP**: After enabling sharing, Windows will typically assign an address in the `192.168.137.x` range to the board's network interface . On the PocketBeagle's serial console (connected via P1-30/P1-32), run the following command to request a new IP from the computer:
    ```bash
    sudo dhclient usb0
    ```
    Or, if your system uses `udhcpc`:
    ```bash
    sudo udhcpc -i usb0
    ```
3.  **Verify the Connection**: Run `ping 8.8.8.8` to check if you can connect to the internet. If it works, you can proceed with `sudo apt update`.

**⚠️ Note**: Some Windows users report that after enabling ICS, they lose the ability to connect to the PocketBeagle via SSH . If you encounter this situation, you may need to reconfigure the IP address of your computer's USB network adapter to `192.168.137.1` (or restore it to automatic acquisition) after completing the `apt` installation.

### 🐧 If Your Computer Runs Linux

Linux requires manual configuration of IP forwarding and NAT (Network Address Translation).

1.  **Enable IP Forwarding**:
    ```bash
    sudo sysctl -w net.ipv4.ip_forward=1
    ```
2.  **Configure NAT Rules** (assume your internet interface is `wlan0` and the PocketBeagle's interface is `usb0`; adjust according to your actual situation):
    ```bash
    sudo iptables -t nat -A POSTROUTING -o wlan0 -j MASQUERADE
    sudo iptables -A FORWARD -i wlan0 -o usb0 -m state --state RELATED,ESTABLISHED -j ACCEPT
    sudo iptables -A FORWARD -i usb0 -o wlan0 -j ACCEPT
    ```
3.  **Set the Default Gateway on the Board**: On the PocketBeagle, manually add the default route, pointing to the IP address of your computer's USB network adapter (typically `192.168.7.1`):
    ```bash
    sudo ip route add default via 192.168.7.1
    ```
4.  **Configure DNS**: Edit `/etc/resolv.conf` on the board and add a nameserver:
    ```bash
    echo "nameserver 8.8.8.8" | sudo tee /etc/resolv.conf
    ```

### 🍎 If Your Computer Runs macOS

macOS's "Internet Sharing" feature can conflict with the PocketBeagle's built-in USB network addressing (the board defaults to `192.168.7.x`, while macOS's sharing uses the `192.168.6.x` subnet), which may cause issues . If you encounter problems, a more reliable alternative is to **use a USB-to-Ethernet adapter**: connect the adapter to your Mac, then use an Ethernet cable to connect the adapter to the PocketBeagle's network port (if you have an expansion board), or directly use the Mac's internet sharing on that adapter.

### 💡 A "Quick and Dirty" Method: Using a Proxy

If you don't want to deal with NAT and routing configurations, you can run a proxy on your computer, and then have the PocketBeagle access the internet through the proxy.

1.  Run a simple HTTP proxy on your computer (for example, using Python): `python3 -m http.server 8080 --bind 127.0.0.1` (this only provides a simple HTTP service, not a full proxy; you need a real proxy tool like `tinyproxy`).
2.  On the PocketBeagle, set the environment variables to point to your computer's IP (usually `192.168.7.1`) and the proxy port:
    ```bash
    export http_proxy="http://192.168.7.1:8080"
    export https_proxy="http://192.168.7.1:8080"
    ```
    Then run `sudo apt update`. This method has been proven to work on the BeagleBone platform .

### ✅ Step Summary

Whichever method you choose, the core idea is: **let your computer become the PocketBeagle's "router"**. After completing the configuration, verify the connection by running `ping 8.8.8.8` on the board, and then run `sudo apt update`.
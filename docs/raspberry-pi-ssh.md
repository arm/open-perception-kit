# Raspberry Pi SSH Setup

Use this page when you want to control the Raspberry Pi from your normal computer. SSH lets you open a Raspberry Pi terminal over the network, and VS Code uses the same connection for **Remote - SSH**.

The best time to enable SSH is when you write the Raspberry Pi OS image to the SD card or SSD. That avoids needing a monitor and keyboard on the Pi later.

## 1. Enable SSH In Raspberry Pi Imager

Do this on your normal computer before the first Raspberry Pi boot.

1. Open **Raspberry Pi Imager**.
2. Choose the Raspberry Pi OS image.
3. Choose the SD card or SSD.
4. Open the advanced settings.
5. Set the hostname to:

```text
raspberrypi
```

6. Enable SSH.
7. Choose password authentication for the first setup.
8. Set the username and password.
9. Configure Wi-Fi if you are not using Ethernet.
10. Write the image.

Expected result: after the Raspberry Pi boots, it should accept SSH connections at `raspberrypi.local`.

## 2. Boot And Find The Raspberry Pi

Connect power and wait a few minutes for the first boot.

Run on your normal computer in the **host shell**:

```bash
ssh <username>@raspberrypi.local
```

Replace `<username>` with the username you set in Raspberry Pi Imager.

When the terminal asks for a password, enter the password you set in Raspberry Pi Imager. The password may not visibly appear while you type. That is normal.

If that works, continue with the quick start.

## 3. If `raspberrypi.local` Does Not Work

Use the Raspberry Pi IP address instead.

The easiest ways to find it are:

- Check your router's connected devices page.
- Connect a monitor and run `hostname -I` on the Raspberry Pi.

Then connect from your normal computer:

```bash
ssh <username>@<raspberry-pi-ip-address>
```

Expected result: your terminal logs into the Raspberry Pi.

## 4. If SSH Was Not Enabled During Imaging

If you have a monitor and keyboard connected to the Raspberry Pi, open Terminal on the Pi and run:

```bash
sudo systemctl enable ssh
sudo systemctl start ssh
```

Then test again from your normal computer:

```bash
ssh <username>@raspberrypi.local
```

## 5. Add The Raspberry Pi To VS Code

After terminal SSH works, open VS Code on your normal computer.

1. Install the **Remote - SSH** extension.
2. Open the Command Palette.
   - Windows/Linux: `Ctrl+Shift+P`.
   - macOS: `Cmd+Shift+P`.
3. Run **Remote-SSH: Connect to Host...**.
4. Enter:

```text
<username>@raspberrypi.local
```

If `.local` did not work in the terminal, use the IP address instead:

```text
<username>@<raspberry-pi-ip-address>
```

Expected result: VS Code opens a new remote window connected to the Raspberry Pi.

If VS Code asks for a password, enter the same Raspberry Pi password you used in the terminal SSH test.

## 6. Optional: Switch To SSH Keys Later

Password authentication is fine for the first setup. After the quick start works, you can switch to SSH keys if you want a cleaner login flow.

Run on your normal computer:

```bash
ssh-keygen -t ed25519
ssh-copy-id <username>@raspberrypi.local
```

Then test:

```bash
ssh <username>@raspberrypi.local
```

Do this only after the password-based connection already works.

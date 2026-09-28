---
title: Raspberry Pi SSH Setup
sidebar_position: 5
sidebar_label: Raspberry Pi SSH
description: Set up SSH keys so terminal sessions and VS Code can connect to a Raspberry Pi without repeated password prompts.
---
<!-- SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates -->


# Raspberry Pi SSH Setup

This guide sets up **SSH key authentication** so you can connect from your normal
computer without entering the Raspberry Pi account password each time. VS Code
**Remote - SSH** can use the same key. For this path, start at section 2.

If you are happy to type the Pi password each time, follow the optional section 1
and return to Get Started immediately afterwards.

Run host commands in a Linux/macOS terminal or **Windows PowerShell**. If you use
VS Code on Windows, use PowerShell so the key is available to Windows OpenSSH.
WSL has a separate SSH setup; a key created only in WSL is not automatically
available to Windows VS Code.

Replace `<username>` with your Pi username throughout. The examples use the
hostname `raspberrypi`. If `raspberrypi.local` does not resolve, replace it with
the Pi IP address from your router's device list, or from `hostname -I` in a
terminal on the Pi.

On the first connection, SSH asks you to trust the Pi's host key. Compare the
displayed fingerprint with `ssh-keygen -lf /etc/ssh/ssh_host_ed25519_key.pub` run
on the Pi's local console before accepting it. This one-time identity check is
separate from your login password.

## 1. Optional: Use Password Authentication

For a new installation:

1. Open **Raspberry Pi Imager** on your normal computer and select Raspberry Pi OS
   and the SD card or SSD.
2. In OS customisation, set the hostname to `raspberrypi`, choose a username and
   password, and configure Wi-Fi if you are not using Ethernet.
3. Under **Remote Access**, enable SSH and select **Use password authentication**.
4. Write the image, boot the Pi, and wait a few minutes for it to join the network.

If Raspberry Pi OS is already installed, keep that installation. If SSH is
disabled, use a monitor and keyboard to run this in a **Pi terminal**:

```bash
sudo systemctl enable --now ssh
```

From your **host shell**, connect:

```bash
ssh <username>@raspberrypi.local
```

Enter the Pi account password when prompted. Nothing appears while you type.
Expected result: a shell on the Raspberry Pi. Future connections can prompt for
the same password; VS Code may ask more than once.

**If that is fine for you, SSH setup is complete. Return to
[Get Started](/getting-started).** The remaining sections set up key-based login.

## 2. Create Or Reuse An SSH Key On Your Computer

Keep the private key on the computer you connect **from**. Only its public
`.pub` file belongs on the Pi.

Check for an existing key pair in your **host shell**:

Linux/macOS:

```bash
ls ~/.ssh
```

Windows PowerShell:

```powershell
Get-ChildItem "$env:USERPROFILE\.ssh"
```

If `id_ed25519` and `id_ed25519.pub` already exist, reuse them and skip key
generation. A missing directory is normal if you have never created a key.
If you use another key filename, substitute it in the commands below.

Otherwise, generate a key on your **host**, using the same command on all three
platforms:

```bash
ssh-keygen -t ed25519
```

Accept the default file location. Do not overwrite an existing private key.
Choose a passphrase to protect the private key; section 4
shows how to unlock it once for repeated connections. This passphrase protects
your local key and is separate from the Pi account password.

## 3. Authorize The Public Key On The Raspberry Pi

Choose the path matching your Pi's current state.

### New Installation: Add The Key In Raspberry Pi Imager

1. Open **Raspberry Pi Imager** and select Raspberry Pi OS and the SD card or SSD.
2. In OS customisation, set the hostname to `raspberrypi`, choose a username and
   account password, and configure Wi-Fi if needed.
3. Under **Remote Access**, enable SSH and select **Use public key authentication**.
4. Browse to the public key created in section 2: `~/.ssh/id_ed25519.pub` on
   Linux/macOS, or `%USERPROFILE%\.ssh\id_ed25519.pub` on Windows. Check that
   Imager uses this key if it has prefilled a different one.
5. Write the image, boot the Pi, and wait for it to join the network.

The public key is installed during imaging, so no initial SSH password login is
needed. Continue to section 4. See the official
[Raspberry Pi SSH instructions](https://www.raspberrypi.com/documentation/computers/remote-access.html#configure-ssh-without-a-password)
for the Imager options.

### Existing Installation: Copy The Key Over SSH

Use the working password connection from section 1 to install your public key.
You do not need to reimage the Pi.

Run on your **host**, using Linux/macOS:

```bash
ssh-copy-id -i ~/.ssh/id_ed25519.pub <username>@raspberrypi.local
```

On **Windows PowerShell**, use:

```powershell
Get-Content "$env:USERPROFILE\.ssh\id_ed25519.pub" | ssh <username>@raspberrypi.local 'umask 077; mkdir -p ~/.ssh && echo >> ~/.ssh/authorized_keys && cat >> ~/.ssh/authorized_keys && chmod 700 ~/.ssh && chmod 600 ~/.ssh/authorized_keys'
```

If `ssh-copy-id` is unavailable on Linux/macOS, use:

```bash
cat ~/.ssh/id_ed25519.pub | ssh <username>@raspberrypi.local 'umask 077; mkdir -p ~/.ssh && echo >> ~/.ssh/authorized_keys && cat >> ~/.ssh/authorized_keys && chmod 700 ~/.ssh && chmod 600 ~/.ssh/authorized_keys'
```

Enter the **Pi account password** when prompted for this installation step.
These commands append the public key to that user's `authorized_keys` file and
preserve existing keys. Adding a key does not disable password authentication;
the final check below confirms that your connection actually uses a key.

## 4. Load The Key Into Your SSH Agent

The SSH agent keeps the unlocked key available so you do not need to enter its
passphrase for every connection.

### Linux And macOS

Run in your **host shell**:

```bash
ssh-add ~/.ssh/id_ed25519
```

If this reports that it cannot connect to an authentication agent, start one in
the same shell and try again:

```bash
eval "$(ssh-agent -s)"
ssh-add ~/.ssh/id_ed25519
```

Enter the **key passphrase** when asked. The key stays unlocked while that agent
retains it; after a reboot or agent restart, you may need to add it again.
If you started the agent manually, launch VS Code from that shell with `code`
after closing existing VS Code windows so it inherits the agent environment.

### Windows PowerShell

Once, open **PowerShell as Administrator** and enable the Windows agent:

```powershell
Set-Service ssh-agent -StartupType Automatic
Start-Service ssh-agent
```

Return to your **normal, non-administrator PowerShell** and add your key:

```powershell
ssh-add "$env:USERPROFILE\.ssh\id_ed25519"
```

Enter the **key passphrase** when asked, then restart VS Code if it was open.
The service setup follows
[Microsoft's Windows SSH agent guidance](https://learn.microsoft.com/en-us/windows-server/administration/openssh/openssh_keymanagement#user-key-generation).

On any platform, `ssh-add -l` lists loaded keys. For VS Code, run it in a
**local** VS Code terminal to confirm the editor can access the same agent.
See [VS Code's SSH agent guidance](https://code.visualstudio.com/docs/remote/troubleshooting#setting-up-the-ssh-agent)
if terminal login works but the editor still asks for a passphrase.

## 5. Verify Login Without Password Prompts

From your **host shell**, connect normally and complete the host-key check if
this is your first connection:

```bash
ssh <username>@raspberrypi.local
```

Expected result: a Pi shell without a Pi password or key-passphrase prompt.
Run `exit` to return to your host, then explicitly verify key authentication
with interactive prompts disabled:

```bash
ssh -o BatchMode=yes -o PreferredAuthentications=publickey -o ControlPath=none <username>@raspberrypi.local whoami
```

Expected result: your Pi username is printed with no password prompt. If this
fails, check that the public key was installed for the correct Pi user and that
`ssh-add -l` lists the matching key on your host. A key with a custom filename
must also be loaded into the agent or selected with `ssh -i <private-key-path>`.

You can now use `ssh <username>@raspberrypi.local` for normal sessions and the
same `username@hostname` in VS Code Remote - SSH. Commands such as `sudo` on the
Pi may still ask for your Pi password; that is separate from SSH login.

**SSH key setup is complete. Return to [Get Started](/getting-started).**

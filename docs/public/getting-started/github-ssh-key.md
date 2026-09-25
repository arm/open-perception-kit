---
title: GitHub SSH Key Setup
sidebar_position: 6
sidebar_label: GitHub SSH Key
description: Create and register a GitHub SSH key when you need to clone Open Perception Kit with SSH.
---

# GitHub SSH Key Setup

This page is only needed if you want to clone OPK with an SSH URL such as:

```text
git@github.com:arm/open-perception-kit.git
```

For the quickest first run, you can skip this page and clone with HTTPS instead:

```text
https://github.com/arm/open-perception-kit.git
```

## 1. Check Whether You Already Have A Key

Run in the **host shell**. On Windows, use the **WSL shell** if you clone inside WSL.

```bash
ls ~/.ssh
```

If you see files such as `id_ed25519` and `id_ed25519.pub`, you may already have a key. The `.pub` file is the public key. The file without `.pub` is private and must not be shared.

## 2. Create A New Key

Run in the **host shell** or **WSL shell**:

```bash
ssh-keygen -t ed25519 -C "your-email@example.com"
```

When asked where to save the key, press Enter to accept the default path.

## 3. Start The SSH Agent

Run in the **host shell** or **WSL shell**:

```bash
eval "$(ssh-agent -s)"
ssh-add ~/.ssh/id_ed25519
```

Expected result: `ssh-add` says the identity was added.

## 4. Copy The Public Key

Run in the **host shell** or **WSL shell**:

```bash
cat ~/.ssh/id_ed25519.pub
```

Copy the full line that starts with `ssh-ed25519`.

## 5. Add The Key To GitHub

In GitHub:

1. Open **Settings**.
2. Open **SSH and GPG keys**.
3. Choose **New SSH key**.
4. Paste the public key.
5. Save it.

Do not paste the private key.

## 6. Test GitHub SSH

Run in the **host shell** or **WSL shell**:

```bash
ssh -T git@github.com
```

Expected result: GitHub says that you successfully authenticated. It may also say that GitHub does not provide shell access. That is normal.

You can now clone with SSH and open the folder:

```bash
git clone git@github.com:arm/open-perception-kit.git
cd open-perception-kit
```

[Back to Get Started](/getting-started)

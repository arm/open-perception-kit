---
sidebar_position: 12
sidebar_label: Performance Measurement
---

# Performance Measurement With Performix

Performix is a standalone tool for monitoring performance metrics on the target device.

Download Performix here:
https://developer.arm.com/servers-and-cloud-computing/arm-performix

---

## SSH

Both tools require the target to be reachable via an SSH connection. The container uses port `2222` to accept SSH connections.

Both tools work if the host and the target share an RSA key pair.

---

## Generate SSH Keys

You need to generate two files — these are the SSH keys for the connection. If your target is already reachable via SSH, you can skip this step.

```bash
ssh-keygen -m PEM -t rsa -b 4096 -f ~/.ssh/performance_key_rsa -C "performance-container-key"
```

This creates:

- ~/.ssh/performance_key_rsa (private key)
- ~/.ssh/performance_key_rsa.pub (public key)

---

## Set Permissions

```bash
chmod 600 ~/.ssh/performance_key_rsa  
chmod 700 ~/.ssh
```

---

## Install Public Key on Target

On the target (or dev container):

```bash
cat ~/.ssh/performance_key_rsa.pub >> ~/.ssh/authorized_keys  
chmod 600 ~/.ssh/authorized_keys
```

---

## Test SSH Connection

Test the SSH connection from the host:

```bash
ssh -i ~/.ssh/performance_key_rsa devgoblin@127.0.0.1 -p 2222
```

It should let you in without a password. Use different IP for remote target devices.

---

## If SSH Does Not Work

If SSH does not work:

- Check permissions on target:  
  chmod 700 ~/.ssh  
  chmod 600 ~/.ssh/authorized_keys  

- Verify SSH server is running in the container

- Ensure port 2222 is exposed and mapped correctly

---

## Performix SSH

In Performix the SSH setup is very similar.

![Performix SSH setup](../static/img/performix-ssh.jpg)

After clicking 'Add Target' you have to populate the form with information:
- Host: 127.0.0.1 or the IP address of the target device
- Name: An arbitrary name for the target
- Port: SSH port (2222)
- User: User name on target (devgoblin)
- Key selection: `Select key manually` works with the generated key above

You can also use `Test Connection` here.

## Performix Recipes

To take measurements in Performix, you need a recipe.
Not all kinds of measurements are possible in a container.

Now here is an example of setting up one that works:
- Target: Name of the target
- Workload type: Launch a new process
- Workload: The process that will be executed and measured on the target (for example `/work/tools/pek-menu 01-full-onnx`)
- Set profiling duration: Limitless or execution for a limited time only
- Different other settings

The `Run Recipe` button executes the target application and performs the measurement.

![Performix recipe setup](../static/img/performix-recipe.jpg)

After running a recipe by clicking `Run Recipe`, you will get the measurement results.

![Performix results](../static/img/performix-results.jpg)

[Back to README](../../README.md)

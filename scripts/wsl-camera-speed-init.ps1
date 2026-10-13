# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

param(
    [ValidateNotNullOrEmpty()]
    [string]$CameraName = 'Integrated Camera'
)

$ErrorActionPreference = 'Stop'
$stateJson = & usbipd.exe state
if ($LASTEXITCODE -ne 0) { throw 'Cannot read usbipd device state.' }
$devices = @(($stateJson | ConvertFrom-Json).Devices | Where-Object {
    ($_.Description -split ',\s*') -contains $CameraName
})
if ($devices.Count -ne 1) {
    throw "Expected one device named '$CameraName', found $($devices.Count). Check usbipd.exe list."
}
if (-not $devices[0].ClientIPAddress -or -not $devices[0].StubInstanceId) {
    throw "'$CameraName' is not attached through usbipd. Attach it before running this helper."
}
$devicePath = '\\?\' + $devices[0].StubInstanceId.Replace('\', '#') + '#{00873fdf-cafe-80ee-aa5e-00c04fb1720b}'

# GET_DEVICE initializes VBoxUSB's high-speed flag without claiming or resetting it.
Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

public static class OpkCameraSpeed {
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern SafeFileHandle CreateFile(string name, uint access, uint share,
        IntPtr security, uint creation, uint flags, IntPtr template);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool DeviceIoControl(SafeFileHandle handle, uint code,
        byte[] input, uint inputLength, byte[] output, uint outputLength,
        out uint returned, IntPtr overlapped);

    public static void Initialize(string path) {
        using (var handle = CreateFile(path, 0x40000000, 3, IntPtr.Zero, 3, 0, IntPtr.Zero)) {
            if (handle.IsInvalid)
                throw new Win32Exception(Marshal.GetLastWin32Error(), "Open camera driver");
            var data = new byte[IntPtr.Size];
            uint returned;
            // SUPUSB_IOCTL_GET_DEVICE, with a pointer-sized USBSUP_GETDEV buffer.
            if (!DeviceIoControl(handle, 0x22980c, data, (uint)data.Length,
                    data, (uint)data.Length, out returned, IntPtr.Zero))
                throw new Win32Exception(Marshal.GetLastWin32Error(), "GET_DEVICE");
            if (returned != data.Length)
                throw new InvalidOperationException("Unexpected GET_DEVICE response size");
        }
    }
}
'@
[OpkCameraSpeed]::Initialize($devicePath)
Write-Output "Initialized USB speed for $($devices[0].Description) ($($devices[0].BusId))."

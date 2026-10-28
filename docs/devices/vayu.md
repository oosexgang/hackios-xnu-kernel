<!-- Made by tools/docs-gen.py from devices/vayu/device.conf. Do not edit: change that, then run tools/docs-gen.py. -->


# POCO X3 Pro (`vayu`)

| | |
|---|---|
| Vendor | Xiaomi |
| SoC | `sm8150` |
| Configuration | `make vayu_defconfig` |
| Maintainer | nomorecoolnicknames (README.md) |
| Repository | [`devices/vayu/`](https://github.com/oosexgang/hakios/tree/main/devices/vayu) |
| Firmware | `devices/vayu/pull-firmware.sh` pulls it from your own phone |

## What works

| Part | State | |
|---|---|---|
| display | works | the panel through UEFI's framebuffer, 1080x2400 up to 120 Hz (vayu_vsync, vayu_wled) |
| touch | works | Novatek NT36672C on QUP SPI, multitouch (vayu_nvt, vayu_qup_spi, touchscreen=1) |
| buttons | works | power and volume through PM8150 / PM8150L PON and GPIO over SPMI (CONFIG_HAKIOS_BUTTONS) |
| cpu | works | all 8 Kryo 485 cores up to 2.96 GHz, OSM / EPSS LUT (cpus=8 bootcpu=7 cpufreq=1) |
| gpu | partly | Adreno 640 GMU bring-up in progress; CoreAnimation / UIKit rendered via Mesa at 120 fps |
| storage | works | UFS 3.1 read through the loader; /private/var kept with ufs=2 var_store=1 ufs_write=1 |
| audio | works | ADSP PIL + Cirrus Logic CS35L41 stereo speakers + WCD9340 + 3.5 mm jack (vayu_audio, vayu_jack) |
| battery | works | PM8150B QG/SMB5 fuel gauge and charger over SPMI, level and state in IOPMPowerSource |
| haptic | works | Awinic AW8624 LRA on I2C with RTP waveform playback (vayu_haptic) |
| camera | works | IMX582 48 MP + S5K3T2 20 MP + IMX355 8 MP + OV02B10 2 MP on CCI + CSID/IFE ISP (vayu_camera) |
| torch | works | PM8150L SCHG flash and torch LED over SPMI, Control Center button |
| wifi | works | WCN3990 WLAN DSP PIL + QMI WLFW + WMI/HTT/CE host rings (vayu_wifi) |
| bluetooth | works | WCN3990 UART HCI + QCA Rome TLV firmware + BLE advertising and scanning (vayu_bt) |
| cellular | partly | MPSS PIL + rmtfs (modemst1/2, fsg) + QRTR QMI DMS/NAS/UIM/WDS + IPA v4.1 en1 (vayu_cell) |
| network | works | DWC3 USB CDC-NCM / CDC-ECM Ethernet en0 + Wi-Fi + cellular en1 (vayu_usbnet) |
| thermal | works | TSENS v2.3 temperature sensors (CONFIG_HAKIOS_THERMAL, thermal=1) |
| usb | works | Synopsys DWC3 peripheral mode, fastboot channel and CDC-NCM/ECM network |
| sensors | works | SLPI DSP PIL + QMI SSC accelerometer, gyroscope, ALS and proximity + SPI IR blaster (vayu_ssc, vayu_ir) |

The state is what has been seen on this device, written down in `devices/vayu/device.conf`; a part the table does not name has not been looked at.

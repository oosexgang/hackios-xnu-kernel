<!-- Made by tools/docs-gen.py from devices/*/device.conf and socs/*/soc.conf. Do not edit: change that, then run tools/docs-gen.py. -->


# Devices

Every device with a repository, and how far each part of it works. A device of yours: [Port a device](../porting/index.md).

| Device | SoC | display | touch | buttons | cpu | gpu | storage | audio | battery | haptic | camera | torch | wifi | bluetooth | cellular | network | thermal | usb | sensors |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| [QEMU virt (aarch64)](qemu.md) | `none` | yes | yes | yes |  | no | yes | yes |  |  |  |  |  |  |  | yes |  |  |  |
| [POCO X5 Pro 5G](redwood.md) | `sc7280` | yes | yes | yes | yes | yes | yes | yes | partly | yes | yes | partly | yes | no | partly |  | partly |  |  |
| [POCO X3 Pro](vayu.md) | `sm8150` | yes | yes | yes | yes | partly | yes | yes | yes | yes | yes | yes | yes | yes | partly | yes | yes | yes | yes |

An empty cell is a part nobody has looked at on that device yet.

## SoC families

| SoC | What | |
|---|---|---|
| `sc7280` | Qualcomm SC7280 family (SM7325 "Snapdragon 778G", QCM6490; "yupik", "kodiak") | carried by redwood (`devices/redwood/soc/`) |
| `sm8150` | Qualcomm SM8150 family ("Snapdragon 855 / 855+", "msmnile") | carried by vayu (`devices/vayu/soc/`) |
